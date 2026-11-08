// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "callback_shutdown.h"
#include "crash_reporting.h"
#include "resource_mount_resolver.h"

#include <loader/runtime_constants.adhd.h>

#include <logger/client/module.h>

#include <core/alloc/module.h>
#include <core/common/application_entry.h>
#include <core/crash/module.h>
#include <core/crash/package_names.adhd.h>
#include <core/frame/module.h>
#include <core/assets/registry.h>
#include <core/assets/manager.h>
#include <core/assets/auto_registration.h>
#include <core/graphics/shader_archive.h>
#include <core/filesystem/volume_naming.h>

#include <global/filesystem/executable_paths.h>
#include <global/command_line.h>
#include <global/terminal_entry.h>
#include <global/filesystem.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_loader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::LoaderDetail;
using CrashArena = NWB::Core::Alloc::PersistentArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UpdateCallbackContext{
    NWB::IProjectEntryCallbacks& callbacks;
};

struct LoaderOptions{
    AString<NWB::Core::Alloc::ScratchArena> logAddress;
    AString<NWB::Core::Alloc::ScratchArena> crashUploadToken;
    b8 enableGpuDebug = false;
    b8 forceSdrOutput = false;
    b8 disableHardwareRayTracing = false;
    b8 useStandaloneLogger = false;


public:
    explicit LoaderOptions(NWB::Core::Alloc::ScratchArena& arena)noexcept(noexcept(AString<NWB::Core::Alloc::ScratchArena>(arena)))
        : logAddress(arena)
        , crashUploadToken(arena)
    {}
};

b8 ProjectTickCallback(void* userData, f32 delta){
    NWB_FATAL_ASSERT_MSG(userData, NWB_TEXT("ProjectTickCallback received null user data"));
    auto& updateContext = *MakeNotNull(static_cast<UpdateCallbackContext*>(userData));
    return updateContext.callbacks.onUpdate(delta);
}



class FilesystemAssetBinarySource final : public NWB::Core::Assets::IAssetBinarySource{
public:
    explicit FilesystemAssetBinarySource(NWB::Core::Filesystem::IFilesystem& filesystem)noexcept
        : m_filesystem(filesystem)
    {}


public:
    virtual b8 readAssetBinary(const Name& virtualPath, NWB::Core::Assets::AssetBytes& outBinary)const override{
        return m_filesystem.readFile(virtualPath, outBinary);
    }


private:
    NWB::Core::Filesystem::IFilesystem& m_filesystem;
};


Expected<NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>> LoadShaderArchiveRecords(NWB::Core::GraphicsArena& arena, const NWB::Core::Assets::IAssetBinarySource& assetBinarySource){
    NWB::Core::Assets::AssetBytes indexBinary{arena};
    if(!assetBinarySource.readAssetBinary(NWB::Core::ShaderArchive::s_IndexVirtualPathName, indexBinary))
        return MakeUnexpected(Failure{});
    return NWB::Core::ShaderArchive::DeserializeIndex(arena, { indexBinary.data(), indexBinary.size() });
}

b8 ApplyGraphicsOptions(NWB::Core::GraphicsRuntime& graphics, const LoaderOptions& options){
    if(options.disableHardwareRayTracing){
        if(!graphics.setHardwareRayTracingPolicy(NWB::Core::HardwareRayTracingPolicy::Disabled)){
            NWB_LOGGER_FATAL(NWB_TEXT("Loader: hardware ray tracing policy must be selected before graphics initialization"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: hardware ray tracing disabled before device creation"));
    }

    if(options.forceSdrOutput){
        if(!graphics.setHDR10OutputEnabled(false)){
            NWB_LOGGER_FATAL(NWB_TEXT("Loader: SDR output must be selected before graphics initialization"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: forcing SDR presentation"));
    }

    if(options.enableGpuDebug){
        if(!graphics.setDebugRuntimeEnabled(true)){
            NWB_LOGGER_FATAL(NWB_TEXT("Loader: GPU debug runtime must be enabled before graphics initialization"));
            return false;
        }
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: GPU debug validation enabled"));
    }

    return true;
}

b8 UploadTelemetry(void* userData, const void* bytes, const usize byteCount){
    auto* logger = static_cast<NWB::Log::Client*>(userData);
    return logger && logger->enqueueTelemetry(bytes, byteCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int RunProjectRuntime(NWB::Core::Alloc::ScratchArena& scratchArena, const __hidden_loader::LoaderOptions& options, void* inst, NWB::Log::Client* telemetryClient){
    {
        const NWB::ProjectFrameClientSize frameClientSize = NWB::QueryProjectFrameClientSize();
        if(frameClientSize.width == __hidden_loader::s_EmptyFrameClientExtent || frameClientSize.height == __hidden_loader::s_EmptyFrameClientExtent){
            NWB_LOGGER_FATAL(NWB_TEXT("Invalid project frame client size: {}x{}"), frameClientSize.width, frameClientSize.height);
            return __hidden_loader::s_LoaderExitFailure;
        }

        const TStringView projectWindowTitle = NWB::QueryProjectWindowTitle();
        if(projectWindowTitle.empty()){
            NWB_LOGGER_FATAL(NWB_TEXT("Invalid project window title"));
            return __hidden_loader::s_LoaderExitFailure;
        }

        NWB::Core::Frame frame(inst, frameClientSize.width, frameClientSize.height);
        if(telemetryClient)
            frame.setTelemetryUploadCallback(&__hidden_loader::UploadTelemetry, telemetryClient);
        frame.graphics().setWindowTitle(projectWindowTitle);
        auto resourceDirectoryResult = NWB::LoaderDetail::ResourceMountResolver::resolve(frame.projectObjectArena(), scratchArena);
        if(resourceDirectoryResult)
            frame.graphics().setPipelineCacheDirectory(*resourceDirectoryResult);
        NWB::ProjectStartupContext startupContext{ frame.graphics(), frame.projectObjectArena(), {} };
        if(!NWB::ConfigureProjectRuntime(startupContext)){
            NWB_LOGGER_FATAL(NWB_TEXT("Loader: project runtime configuration failed before device initialization"));
            return __hidden_loader::s_LoaderExitFailure;
        }
        if(!resourceDirectoryResult){
            const auto& error = resourceDirectoryResult.error();
            if(!startupContext.filesystemFactory){
                NWB_LOGGER_ERROR(NWB_TEXT("Loader: failed to inspect {} '{}': {}"), ConvertString(error.stage), PathToString<tchar>(error.path), ConvertString(error.code.message()));
                return __hidden_loader::s_LoaderExitFailure;
            }
            NWB_LOGGER_WARNING(NWB_TEXT("Loader: failed to inspect {} '{}': {}; continuing with custom filesystem factory"), ConvertString(error.stage), PathToString<tchar>(error.path), ConvertString(error.code.message()));
        }
        const NWB::Path resourceMountDirectory = resourceDirectoryResult ? Move(*resourceDirectoryResult) : NWB::Path(frame.projectObjectArena(), NWB::LoaderDetail::s_ResourceDirectoryName);
        if(!frame.graphics().setFilesystemFactory(startupContext.filesystemFactory))
            return __hidden_loader::s_LoaderExitFailure;
        if(!__hidden_loader::ApplyGraphicsOptions(frame.graphics(), options))
            return __hidden_loader::s_LoaderExitFailure;

        if(!frame.init())
            return __hidden_loader::s_LoaderExitFailure;
        i32 initializedFrameWidth = __hidden_loader::s_UninitializedFrameExtent;
        i32 initializedFrameHeight = __hidden_loader::s_UninitializedFrameExtent;
        frame.graphics().getWindowDimensions(initializedFrameWidth, initializedFrameHeight);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: frame initialized ({}x{})"), initializedFrameWidth, initializedFrameHeight);

        NWB::Core::Filesystem::VolumeMountDesc graphicsMount(frame.projectObjectArena());
        if(!graphicsMount.volumeName.assign(NWB::LoaderDetail::s_GraphicsVolumeName))
            return __hidden_loader::s_LoaderExitFailure;
        graphicsMount.mountDirectory = resourceMountDirectory;
        auto graphicsFilesystemResult = NWB::Core::Filesystem::CreateFilesystem(frame.projectObjectArena(), graphicsMount, startupContext.filesystemFactory);
        if(!graphicsFilesystemResult)
            return __hidden_loader::s_LoaderExitFailure;
        UniquePtr<NWB::Core::Filesystem::IFilesystem> graphicsFilesystem = Move(*graphicsFilesystemResult);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: mounted graphics volume from '{}'"), PathToString<tchar>(resourceMountDirectory));

        {
            __hidden_loader::FilesystemAssetBinarySource assetBinarySource(*graphicsFilesystem);

            NWB::Core::Assets::AssetRegistry assetRegistry(frame.projectObjectArena());
            NWB::Core::Assets::RegisterAutoCollectedAssetCodecs(assetRegistry);

            NWB::Core::Assets::AssetManager assetManager(frame.projectObjectArena(), assetRegistry, assetBinarySource);

            auto archiveRecords = __hidden_loader::LoadShaderArchiveRecords(frame.projectObjectArena(), assetBinarySource);
            if(!archiveRecords){
                NWB_LOGGER_FATAL(NWB_TEXT("Failed to load shader archive index '{}'"), ConvertString(NWB::Core::ShaderArchive::s_IndexVirtualPath));
                return __hidden_loader::s_LoaderExitFailure;
            }

            const NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>& shaderArchiveRecords = *archiveRecords;
            NWB::Core::CpuTaskScope projectTasks(frame.cpuTasks(), frame.cpuTasks().registerProfileLabel(__hidden_loader::s_ProjectTaskProfileName));
            NWB::ProjectRuntimeContext context = {
                frame.graphics(),
                frame.input(),
                frame.clipboard(),
                frame.textInput(),
                frame.projectObjectArena(),
                frame.cpuTasks(),
                frame.gpuTasks(),
                projectTasks,
                assetManager,
                *graphicsFilesystem,
                frame.frameGraphRegistry(),
                frame.perfSession(),
                {},
                {},
                {},
                {},
                {},
            };
            context.shaderPathResolver = [&shaderArchiveRecords](const Name& shaderName, const AStringView variantName, const Name& stageName){
                return NWB::Core::ShaderArchive::FindVirtualPath(shaderArchiveRecords, shaderName, variantName, stageName);
            };
            context.telemetryCapture = [&frame](const NWB::Core::Telemetry::CaptureOptions& options){
                frame.setTelemetryCapture(options);
            };
            context.telemetryUploadFlush = [&frame](const b8 clearAfterUpload){
                return frame.flushTelemetryUpload(clearAfterUpload);
            };
            context.perfCapture = [&frame](const NWB::Core::Perf::CaptureOptions& options){
                frame.setPerfCapture(options);
            };
            context.requestQuit = [&frame]()noexcept{
                frame.requestQuit();
            };

            auto callbacks = NWB::CreateProjectEntryCallbacks(context);
            if(!callbacks){
                NWB_LOGGER_FATAL(NWB_TEXT("CreateProjectEntryCallbacks failed: callback instance is null"));
                return __hidden_loader::s_LoaderExitFailure;
            }
            NWB::LoaderDetail::CallbackShutdownGuard callbackShutdownGuard{ *callbacks, projectTasks };
            __hidden_loader::UpdateCallbackContext updateCallbackContext{ *callbacks };

            callbackShutdownGuard.activate();
            if(!callbacks->onStartup()){
                NWB_LOGGER_FATAL(NWB_TEXT("Project startup callback returned false"));
                return __hidden_loader::s_LoaderExitFailure;
            }
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: project startup complete"));
            frame.setProjectUpdateCallback(&__hidden_loader::ProjectTickCallback, &updateCallbackContext);

            if(!frame.showFrame()){
                NWB_LOGGER_ERROR(NWB_TEXT("Loader: frame show failed"));
                return __hidden_loader::s_LoaderExitFailure;
            }

            if(!frame.mainLoop()){
                NWB_LOGGER_ERROR(NWB_TEXT("Loader: frame main loop failed"));
                return __hidden_loader::s_LoaderExitFailure;
            }
        }
        if(!graphicsFilesystem->unmount()){
            NWB_LOGGER_ERROR(NWB_TEXT("Loader: failed to unmount project filesystem"));
            return __hidden_loader::s_LoaderExitFailure;
        }
    }

    return __hidden_loader::s_LoaderExitSuccess;
}

static int MainLogic(NWB::Core::Alloc::ScratchArena& scratchArena, const __hidden_loader::LoaderOptions& options, void* inst, const b8 crashReportingInstalled){
    if(options.useStandaloneLogger){
        NWB::Log::ClientStandalone logger;
        if(!logger.init())
            return __hidden_loader::s_LoaderExitFailure;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: using standalone log output"));
        NWB::LoaderDetail::CrashReporting::configure(crashReportingInstalled, options.useStandaloneLogger, AStringView(options.logAddress.data(), options.logAddress.size()), AStringView(options.crashUploadToken.data(), options.crashUploadToken.size()), options.enableGpuDebug);

        return RunProjectRuntime(scratchArena, options, inst, nullptr);
    }

    NWB::Log::Client logger;
    if(!logger.init(AStringView(options.logAddress.data(), options.logAddress.size())))
        return __hidden_loader::s_LoaderExitFailure;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: connected to log server '{}'"), ConvertString(options.logAddress));
    NWB::LoaderDetail::CrashReporting::configure(crashReportingInstalled, options.useStandaloneLogger, AStringView(options.logAddress.data(), options.logAddress.size()), AStringView(options.crashUploadToken.data(), options.crashUploadToken.size()), options.enableGpuDebug);

    return RunProjectRuntime(scratchArena, options, inst, &logger);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
static int EntryPoint(isize argc, CharT** argv, void* inst){
    // Capture early startup failures; configure uploads after CLI and logger setup.
    const usize crashArenaReserveSize = __hidden_loader::CrashArena::StructureAlignedSize(__hidden_loader::s_CrashArenaPayloadSize);
    __hidden_loader::CrashArena crashArena(__hidden_loader::s_CrashReportingArena, crashArenaReserveSize);
    const b8 crashReportingInstalled = NWB::LoaderDetail::CrashReporting::install(crashArena);
    ScopeExit crashCaptureGuard([crashReportingInstalled]()noexcept{
        if(crashReportingInstalled){
            NWB::Core::RegisterGpuCrashSink(nullptr, nullptr);
            NWB::Core::Crash::UninstallCrashHandler();
        }
    });

    NWB::Core::Alloc::ScratchArena commandLineArena(__hidden_loader::s_CommandLineArena);
    __hidden_loader::LoaderOptions options(commandLineArena);
    CLI::App app{ __hidden_loader::s_LoaderAppName.data() };

    AInteropString address(Get<static_cast<usize>(NWB::Log::ArgCommand::LogAddress)>(NWB::Log::s_ArgDefault));
    u16 port = Get<static_cast<usize>(NWB::Log::ArgCommand::LogPort)>(NWB::Log::s_ArgDefault);
    NWB::Log::ArgAddOption<NWB::Log::ArgCommand::LogAddress>(app, address);
    NWB::Log::ArgAddOption<NWB::Log::ArgCommand::LogPort>(app, port);
    app.add_option(NWB::Log::s_CrashUploadTokenOption.data(), options.crashUploadToken, "Bearer token sent with crash uploads");
    app.add_flag(__hidden_loader::s_ForceSdrOutputFlag.data(), options.forceSdrOutput, "Force SDR presentation even when the project requests HDR10");
    app.add_flag(__hidden_loader::s_DisableHardwareRayTracingFlag.data(), options.disableHardwareRayTracing, "Create the graphics device without hardware ray tracing capabilities");
#if !defined(NWB_FINAL)
    app.add_flag("--gpudbg", options.enableGpuDebug, "Enable graphics backend validation layer (dbg/opt builds only)");
#endif

    return ::InvokeTerminalEntry<CLI::ParseError>([&](){
        CommandLineParseApp(app, argc, argv);

        options.useStandaloneLogger = address.empty() || port == __hidden_loader::s_StandaloneLoggerPort;
        if(!options.useStandaloneLogger)
            options.logAddress = StringFormat(commandLineArena, "{}:{}", AStringView(address.data(), address.size()), port);

        return MainLogic(commandLineArena, options, inst, crashReportingInstalled);
    }, [&](const CLI::ParseError& error){ return app.exit(error, NWB_COUT, NWB_CERR); }, []()noexcept{ return __hidden_loader::s_LoaderExitFailure; });
}


NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

