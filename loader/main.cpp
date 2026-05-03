// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/standalone_runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <CLI.hpp>

#include <global/global.h>
#include <global/command.h>
#include <global/filesystem.h>

#include <logger/client/logger.h>
#include <core/common/common.h>
#include <core/frame/frame.h>
#include <core/assets/asset_registry.h>
#include <core/assets/asset_manager.h>
#include <core/assets/asset_auto_registration.h>
#include <core/graphics/shader_archive.h>
#include <core/filesystem/filesystem.h>
#include <core/filesystem/volume_naming.h>

#include "project_entry.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_loader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CallbackShutdownGuard : NoCopy{
public:
    explicit CallbackShutdownGuard(NWB::IProjectEntryCallbacks* callbacks)
        : m_callbacks(callbacks)
    {}

    ~CallbackShutdownGuard(){
        if(!m_active || !m_callbacks)
            return;

        try{
            m_callbacks->onShutdown();
        }
        catch(...){
            NWB_LOGGER_ERROR(NWB_TEXT("Project shutdown callback threw an exception"));
        }
    }

    void activate(){
        m_active = true;
    }

private:
    NWB::IProjectEntryCallbacks* m_callbacks = nullptr;
    bool m_active = false;
};

struct UpdateCallbackContext{
    NWB::IProjectEntryCallbacks* callbacks = nullptr;
};

struct LoaderOptions{
    AString logAddress;
    bool enableGpuDebug = false;
};

bool ProjectTickCallback(void* userData, f32 delta){
    NWB_FATAL_ASSERT_MSG(userData, NWB_TEXT("ProjectTickCallback received null user data"));
    auto* updateContext = static_cast<UpdateCallbackContext*>(userData);
    NWB_FATAL_ASSERT_MSG(updateContext->callbacks, NWB_TEXT("ProjectTickCallback received null callbacks"));
    return updateContext->callbacks->onUpdate(delta);
}

bool HasGraphicsVolumeSegment(const Path& mountDirectory){
    ErrorCode errorCode;
    if(!IsDirectory(mountDirectory, errorCode) || errorCode)
        return false;

    const Path segmentPath = mountDirectory / NWB::Core::Filesystem::MakeVolumeSegmentFileName("graphics", 0);
    errorCode.clear();
    return FileExists(segmentPath, errorCode) && !errorCode;
}

Path ResolveResourceMountDirectory(){
    ErrorCode errorCode;
    Path currentDirectory;
    if(GetCurrentPath(currentDirectory, errorCode)){
        const Path currentResDirectory = currentDirectory / "res";
        if(HasGraphicsVolumeSegment(currentResDirectory))
            return currentResDirectory;
    }

    Path executableDirectory;
    if(!GetExecutableDirectory(executableDirectory))
        return Path("res");

    const Path executableResDirectory = executableDirectory / "res";
    if(HasGraphicsVolumeSegment(executableResDirectory))
        return executableResDirectory;

    const Path parentDirectory = executableDirectory.parent_path();
    if(parentDirectory.empty())
        return Path("res");

    const Path parentResDirectory = parentDirectory / "res";
    if(HasGraphicsVolumeSegment(parentResDirectory))
        return parentResDirectory;

    return Path("res");
}


class VolumeAssetBinarySource final : public NWB::Core::Assets::IAssetBinarySource{
public:
    explicit VolumeAssetBinarySource(NWB::Core::Filesystem::VolumeSession& volumeSession)
        : m_volumeSession(volumeSession)
    {}


public:
    virtual bool readAssetBinary(const Name& virtualPath, NWB::Core::Assets::AssetBytes& outBinary)const override{
        return m_volumeSession.loadData(virtualPath, outBinary);
    }


private:
    NWB::Core::Filesystem::VolumeSession& m_volumeSession;
};


bool LoadShaderArchiveRecords(
    const NWB::Core::Assets::IAssetBinarySource& assetBinarySource,
    Vector<NWB::Core::ShaderArchive::Record>& outRecords
){
    NWB::Core::Assets::AssetBytes indexBinary;
    if(!assetBinarySource.readAssetBinary(NWB::Core::ShaderArchive::IndexVirtualPathName(), indexBinary))
        return false;

    return NWB::Core::ShaderArchive::deserializeIndex(indexBinary, outRecords);
}

void AddDebugCommandLineOptions(CLI::App& app, LoaderOptions& options){
#if defined(NWB_DEBUG)
    app.add_flag("--gpudbg", options.enableGpuDebug, "Enable Vulkan validation layer");
#else
    static_cast<void>(app);
    static_cast<void>(options);
#endif
}

bool ApplyGraphicsOptions(NWB::Core::Graphics& graphics, const LoaderOptions& options){
#if defined(NWB_DEBUG)
    if(options.enableGpuDebug){
        if(!graphics.setDebugRuntimeEnabled(true)){
            NWB_LOGGER_FATAL(NWB_TEXT("Loader: GPU debug runtime must be enabled before graphics initialization"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: GPU debug validation enabled"));
    }
#else
    static_cast<void>(graphics);
    static_cast<void>(options);
#endif

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int MainLogic(const __hidden_loader::LoaderOptions& options, void* inst){
    {
        NWB::Log::Client logger;
        if(!logger.init(MakeNotNull(options.logAddress.c_str())))
            return -1;
        NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: connected to log server '{}'"), StringConvert(options.logAddress.c_str()));

        try{
            const NWB::ProjectFrameClientSize frameClientSize = NWB::QueryProjectFrameClientSize();
            if(frameClientSize.width == 0 || frameClientSize.height == 0){
                NWB_LOGGER_FATAL(NWB_TEXT("Invalid project frame client size: {}x{}"), frameClientSize.width, frameClientSize.height);
                return -1;
            }

            NWB::Core::Frame frame(inst, frameClientSize.width, frameClientSize.height);
            const Path resourceMountDirectory = __hidden_loader::ResolveResourceMountDirectory();
            frame.graphics().setPipelineCacheDirectory(resourceMountDirectory);
            if(!__hidden_loader::ApplyGraphicsOptions(frame.graphics(), options))
                return -1;

            if(!frame.init())
                return -1;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: frame initialized ({}x{})"), frameClientSize.width, frameClientSize.height);

            NWB::Core::Filesystem::VolumeSession graphicsVolume(frame.projectObjectArena());
            if(!graphicsVolume.load("graphics", resourceMountDirectory))
                return -1;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: mounted graphics volume from '{}'"), PathToString<tchar>(resourceMountDirectory));

            __hidden_loader::VolumeAssetBinarySource assetBinarySource(graphicsVolume);

            NWB::Core::Assets::AssetRegistry assetRegistry(frame.projectObjectArena());
            NWB::Core::Assets::RegisterAutoCollectedAssetCodecs(assetRegistry);

            NWB::Core::Assets::AssetManager assetManager(frame.projectObjectArena(), assetRegistry, assetBinarySource);

            Vector<NWB::Core::ShaderArchive::Record> shaderArchiveRecords;
            if(!__hidden_loader::LoadShaderArchiveRecords(assetBinarySource, shaderArchiveRecords)){
                NWB_LOGGER_FATAL(NWB_TEXT("Failed to load shader archive index '{}'")
                    , StringConvert(NWB::Core::ShaderArchive::s_IndexVirtualPath)
                );
                return -1;
            }

            NWB::ProjectRuntimeContext context = {
                frame.graphics(),
                frame.input(),
                frame.projectObjectArena(),
                frame.projectThreadPool(),
                frame.projectJobSystem(),
                assetManager,
                {},
            };
            context.shaderPathResolver = [&shaderArchiveRecords](const Name& shaderName, const AStringView variantName, const Name& stageName, Name& outVirtualPath){
                return NWB::Core::ShaderArchive::findVirtualPath(
                    shaderArchiveRecords,
                    shaderName,
                    variantName,
                    stageName,
                    outVirtualPath
                );
            };

            auto callbacks = NWB::CreateProjectEntryCallbacks(context);
            if(!callbacks){
                NWB_LOGGER_FATAL(NWB_TEXT("CreateProjectEntryCallbacks failed: callback instance is null"));
                return -1;
            }
            __hidden_loader::CallbackShutdownGuard callbackShutdownGuard{ callbacks.get() };
            __hidden_loader::UpdateCallbackContext updateCallbackContext{ callbacks.get() };

            callbackShutdownGuard.activate();
            if(!callbacks->onStartup()){
                NWB_LOGGER_FATAL(NWB_TEXT("Project startup callback returned false"));
                return -1;
            }
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: project startup complete"));
            frame.setProjectUpdateCallback(&__hidden_loader::ProjectTickCallback, &updateCallbackContext);

            if(!frame.showFrame()){
                NWB_LOGGER_ERROR(NWB_TEXT("Loader: frame show failed"));
                return -1;
            }

            if(!frame.mainLoop()){
                NWB_LOGGER_ERROR(NWB_TEXT("Loader: frame main loop failed"));
                return -1;
            }
        }
        catch(const GeneralException& e){
            NWB_LOGGER_FATAL(NWB_TEXT("Exception: {}"), StringConvert(e.what()));
            return -1;
        }
    }

    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
static int EntryPoint(isize argc, CharT** argv, void* inst){
    try{
        NWB::Core::Common::InitializerGuard commonInitializerGuard;
        if(!commonInitializerGuard.initialize())
            return -1;

        __hidden_loader::LoaderOptions options;
        {
            CLI::App app{ "loader" };

            AString address = Get<static_cast<usize>(NWB::ArgCommand::LogAddress)>(NWB::g_ArgDefault);
            u16 port = Get<static_cast<usize>(NWB::ArgCommand::LogPort)>(NWB::g_ArgDefault);
            NWB::ArgAddOption<NWB::ArgCommand::LogAddress>(app, address);
            NWB::ArgAddOption<NWB::ArgCommand::LogPort>(app, port);
            __hidden_loader::AddDebugCommandLineOptions(app, options);

            try{
                NWB::ArgParseApp(app, argc, argv);
            }
            catch(const CLI::ParseError& e){
                app.exit(e, NWB_COUT, NWB_CERR);
                return -1;
            }

            options.logAddress = StringFormat("{}:{}", address, port);
        }

        return MainLogic(options, inst);
    }
    catch(...){
        return -1;
    }
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

