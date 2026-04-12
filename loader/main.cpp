// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pch.h"


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


struct CallbackShutdownGuard{
    NWB::IProjectEntryCallbacks* callbacks = nullptr;
    bool active = false;

    ~CallbackShutdownGuard(){
        if(!active || !callbacks)
            return;

        try{
            callbacks->onShutdown();
        }
        catch(...){
            NWB_LOGGER_ERROR(NWB_TEXT("Project shutdown callback threw an exception"));
        }
    }
};

struct UpdateCallbackContext{
    NWB::IProjectEntryCallbacks* callbacks = nullptr;
};

struct CommonInitializerGuard{
    bool active = false;

    ~CommonInitializerGuard(){
        if(active)
            NWB::Core::Common::Initializer::instance().finalize();
    }
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int MainLogic(NotNull<const char*> logAddress, void* inst){
    {
        NWB::Log::Client logger;
        if(!logger.init(logAddress))
            return -1;
        NWB_LOGGER_REGISTER(&logger);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: connected to log server '{}'"), StringConvert(logAddress.get()));

        try{
            const NWB::ProjectFrameClientSize frameClientSize = NWB::QueryProjectFrameClientSize();
            if(frameClientSize.width == 0 || frameClientSize.height == 0){
                NWB_LOGGER_FATAL(NWB_TEXT("Invalid project frame client size: {}x{}"), frameClientSize.width, frameClientSize.height);
                return -1;
            }

            NWB::Core::Frame frame(inst, frameClientSize.width, frameClientSize.height);
            const Path resourceMountDirectory = __hidden_loader::ResolveResourceMountDirectory();
            frame.graphics().setPipelineCacheDirectory(resourceMountDirectory);

            if(!frame.init())
                return -1;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: frame initialized ({}x{})"), frameClientSize.width, frameClientSize.height);

            NWB::Core::Filesystem::VolumeSession graphicsVolume(frame.projectObjectArena());
            if(!graphicsVolume.load("graphics", resourceMountDirectory))
                return -1;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: mounted graphics volume from '{}'"), PathToString<tchar>(resourceMountDirectory));

            __hidden_loader::VolumeAssetBinarySource assetBinarySource(graphicsVolume);

            NWB::Core::Assets::AssetRegistry assetRegistry;
            NWB::Core::Assets::RegisterAutoCollectedAssetCodecs(assetRegistry);

            NWB::Core::Assets::AssetManager assetManager(assetRegistry, assetBinarySource);

            Vector<NWB::Core::ShaderArchive::Record> shaderArchiveRecords;
            if(!__hidden_loader::LoadShaderArchiveRecords(assetBinarySource, shaderArchiveRecords)){
                NWB_LOGGER_FATAL(
                    NWB_TEXT("Failed to load shader archive index '{}'"),
                    StringConvert(NWB::Core::ShaderArchive::s_IndexVirtualPath)
                );
                return -1;
            }

            NWB::ProjectRuntimeContext context = {
                frame.graphics(),
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

            callbackShutdownGuard.active = true;
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
            NWB_LOGGER_REGISTER(nullptr);
            return -1;
        }
        NWB_LOGGER_REGISTER(nullptr);
    }

    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
static int EntryPoint(isize argc, CharT** argv, void* inst){
    int ret;

    AString logAddress;
    {
        CLI::App app{ "loader" };

        AString address = Get<static_cast<usize>(NWB::ArgCommand::LogAddress)>(NWB::g_ArgDefault);
        u16 port = Get<static_cast<usize>(NWB::ArgCommand::LogPort)>(NWB::g_ArgDefault);
        NWB::ArgAddOption<NWB::ArgCommand::LogAddress>(app, address);
        NWB::ArgAddOption<NWB::ArgCommand::LogPort>(app, port);

        try{
            const bool hasValidArgv = argc > 0 && argv && argv[0];
            if(hasValidArgv)
                app.parse(static_cast<int>(argc), argv);
            else
                app.parse(std::vector<std::string>{});
        }
        catch(const CLI::ParseError& e){
            app.exit(e, NWB_COUT, NWB_CERR);
            return -1;
        }

        logAddress = StringFormat("{}:{}", address, port);
    }

    try{
        __hidden_loader::CommonInitializerGuard commonInitializerGuard;
        if(!NWB::Core::Common::Initializer::instance().initialize())
            return -1;
        commonInitializerGuard.active = true;
        ret = MainLogic(MakeNotNull(logAddress.c_str()), inst);
    }
    catch(...){
        return -1;
    }

    return ret;
}


#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#if defined(NWB_UNICODE)
int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow){
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return EntryPoint(__argc, __wargv, hInstance);
}
#else
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow){
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;
    return EntryPoint(__argc, __argv, hInstance);
}
#endif
#elif defined(NWB_PLATFORM_LINUX)
int main(int argc, char** argv){
    return EntryPoint(static_cast<isize>(argc), argv, nullptr);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
