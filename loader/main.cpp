// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/standalone_runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <CLI.hpp>

#include <global/global.h>
#include <global/filesystem.h>

#include <core/common/command_line.h>
#include <logger/client/logger.h>
#include <core/common/module.h>
#include <core/crash/module.h>
#include <core/frame/module.h>
#include <core/assets/registry.h>
#include <core/assets/manager.h>
#include <core/assets/auto_registration.h>
#include <core/graphics/shader_archive.h>
#include <core/filesystem/module.h>
#include <core/filesystem/volume_naming.h>

#include "project_entry.h"

#include <string>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_loader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Path = NWB::Path;
using CrashArena = NWB::Core::Alloc::PersistentArena;

inline constexpr usize s_CrashArenaPayloadSize = 256u * 1024u;

class CallbackShutdownGuard : NoCopy{
public:
    explicit CallbackShutdownGuard(NWB::IProjectEntryCallbacks& callbacks)
        : m_callbacks(callbacks)
    {}

    ~CallbackShutdownGuard(){
        if(!m_active)
            return;

        try{
            m_callbacks.onShutdown();
        }
        catch(...){
            NWB_LOGGER_ERROR(NWB_TEXT("Project shutdown callback threw an exception"));
        }
    }

    void activate(){
        m_active = true;
    }

private:
    NWB::IProjectEntryCallbacks& m_callbacks;
    bool m_active = false;
};

struct UpdateCallbackContext{
    NWB::IProjectEntryCallbacks& callbacks;
};

struct LoaderOptions{
    AString<NWB::Core::Alloc::GlobalArena> logAddress;
    AString<NWB::Core::Alloc::GlobalArena> crashUploadToken;
    ACompactString crashTest;
    bool enableGpuDebug = false;
    bool useStandaloneLogger = false;

    explicit LoaderOptions(NWB::Core::Alloc::GlobalArena& arena)
        : logAddress(arena)
        , crashUploadToken(arena)
    {}
};

bool ProjectTickCallback(void* userData, f32 delta){
    NWB_FATAL_ASSERT_MSG(userData, NWB_TEXT("ProjectTickCallback received null user data"));
    auto* updateContext = static_cast<UpdateCallbackContext*>(userData);
    return updateContext->callbacks.onUpdate(delta);
}

bool HasGraphicsVolumeSegment(const Path& mountDirectory){
    ErrorCode errorCode;
    if(!IsDirectory(mountDirectory, errorCode) || errorCode)
        return false;

    const Path segmentPath = mountDirectory / NWB::Core::Filesystem::MakeVolumeSegmentFileName("graphics", 0).c_str();
    errorCode.clear();
    return FileExists(segmentPath, errorCode) && !errorCode;
}

Path ResolveResourceMountDirectory(NWB::Core::Alloc::GlobalArena& arena){
    ErrorCode errorCode;
    Path currentDirectory(arena);
    if(GetCurrentPath(currentDirectory, errorCode)){
        const Path currentResDirectory = currentDirectory / "res";
        if(HasGraphicsVolumeSegment(currentResDirectory))
            return currentResDirectory;
    }

    Path executableDirectory(arena);
    if(!GetExecutableDirectory(executableDirectory))
        return Path(arena, "res");

    const Path executableResDirectory = executableDirectory / "res";
    if(HasGraphicsVolumeSegment(executableResDirectory))
        return executableResDirectory;

    const Path parentDirectory = executableDirectory.parent_path();
    if(parentDirectory.empty())
        return Path(arena, "res");

    const Path parentResDirectory = parentDirectory / "res";
    if(HasGraphicsVolumeSegment(parentResDirectory))
        return parentResDirectory;

    return Path(arena, "res");
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
    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>& outRecords
){
    NWB::Core::Assets::AssetBytes indexBinary{outRecords.get_allocator().arena()};
    if(!assetBinarySource.readAssetBinary(NWB::Core::ShaderArchive::IndexVirtualPathName(), indexBinary))
        return false;

    return NWB::Core::ShaderArchive::deserializeIndex(indexBinary, outRecords);
}

void AddDebugCommandLineOptions(CLI::App& app, LoaderOptions& options, std::string& crashTest){
#if !defined(NWB_FINAL)
    app.add_flag("--gpudbg", options.enableGpuDebug, "Enable graphics backend validation layer");
    app.add_option(
        "--crash-test",
        crashTest,
        "Run crash reporting test: manual-dump, access-violation, assert, fatal-assert, logger-error, logger-fatal"
    );
#else
    static_cast<void>(app);
    static_cast<void>(options);
    static_cast<void>(crashTest);
#endif
}

bool ApplyGraphicsOptions(NWB::Core::Graphics& graphics, const LoaderOptions& options){
#if !defined(NWB_FINAL)
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

bool InstallCrashReporting(CrashArena& crashArena, const LoaderOptions& options){
    ::Path<CrashArena> executableDirectory(crashArena);
    if(!GetExecutableDirectory(executableDirectory))
        executableDirectory = ::Path<CrashArena>(crashArena, ".");

    ::Path<CrashArena> executableName(crashArena);
    if(!GetExecutableName(executableName))
        executableName = ::Path<CrashArena>(crashArena, "nwb");

    AString<CrashArena> applicationName = PathToString<char>(crashArena, executableName);

    NWB::Core::Crash::CrashConfig crashConfig(crashArena);
    crashConfig.applicationName = AStringView(applicationName.data(), applicationName.size());
    crashConfig.buildId = AStringView("unknown");
    crashConfig.version = AStringView("unknown");
    crashConfig.spoolDirectory = executableDirectory / "crashes";
    crashConfig.logServerUrl = options.useStandaloneLogger
        ? AStringView()
        : AStringView(options.logAddress.data(), options.logAddress.size())
    ;
    crashConfig.crashUploadToken = AStringView(options.crashUploadToken.data(), options.crashUploadToken.size());
    crashConfig.dumpDetailMode = NWB::Core::Crash::DumpDetailMode::Small;

    if(NWB::Core::Crash::InstallCrashHandler(crashArena, crashConfig)){
        static_cast<void>(NWB::Core::Crash::SetCrashMetadata("runtime", "loader"));
        static_cast<void>(NWB::Core::Crash::SetCrashMetadata("gpu_debug", options.enableGpuDebug ? "true" : "false"));
        return true;
    }

    return false;
}

[[noreturn]] void TriggerAccessViolationCrashTest(){
    volatile u8* crashAddress = reinterpret_cast<volatile u8*>(static_cast<usize>(0));
    *crashAddress = 0xFFu;
    ::std::abort();
}

bool RunCrashTestIfRequested(const LoaderOptions& options, const bool crashReportingInstalled, int& outExitCode){
    if(options.crashTest.empty())
        return false;

    const AStringView crashTest = options.crashTest.view();

    if(!crashReportingInstalled){
        NWB_LOGGER_FATAL(NWB_TEXT("Crash test '{}' requested but crash reporting is unavailable"), StringConvert(crashTest));
        outExitCode = -1;
        return true;
    }

    if(crashTest == "manual-dump"){
        const NWB::Core::Crash::CrashDumpResult result = NWB::Core::Crash::CaptureCrashDump("loader_crash_test", "manual_dump");
        outExitCode = result.packageWritten() ? 0 : -1;
        return true;
    }

    if(crashTest == "access-violation")
        TriggerAccessViolationCrashTest();

    if(crashTest == "assert"){
        NWB_ASSERT_MSG(false, NWB_TEXT("Loader crash-test assert"));
        outExitCode = -1;
        return true;
    }

    if(crashTest == "fatal-assert"){
        NWB_FATAL_ASSERT_MSG(false, NWB_TEXT("Loader crash-test fatal assert"));
        outExitCode = -1;
        return true;
    }

    if(crashTest == "logger-error"){
        NWB_LOGGER_ERROR(NWB_TEXT("Loader crash-test logger error"));
        outExitCode = -1;
        return true;
    }

    if(crashTest == "logger-fatal"){
        NWB_LOGGER_FATAL(NWB_TEXT("Loader crash-test logger fatal"));
        outExitCode = -1;
        return true;
    }

    NWB_LOGGER_FATAL(NWB_TEXT("Unknown crash test mode '{}'"), StringConvert(crashTest));
    outExitCode = -1;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int RunProjectRuntime(NWB::Core::Alloc::GlobalArena& arena, const __hidden_loader::LoaderOptions& options, void* inst){
    try{
        const NWB::ProjectFrameClientSize frameClientSize = NWB::QueryProjectFrameClientSize();
        if(frameClientSize.width == 0 || frameClientSize.height == 0){
            NWB_LOGGER_FATAL(NWB_TEXT("Invalid project frame client size: {}x{}"), frameClientSize.width, frameClientSize.height);
            return -1;
        }

        const tchar* projectWindowTitle = NWB::QueryProjectWindowTitle();
        if(!projectWindowTitle || projectWindowTitle[0] == 0){
            NWB_LOGGER_FATAL(NWB_TEXT("Invalid project window title"));
            return -1;
        }

        NWB::Core::Frame frame(inst, frameClientSize.width, frameClientSize.height);
        frame.graphics().setWindowTitle(MakeNotNull(projectWindowTitle));
        const NWB::Path resourceMountDirectory = __hidden_loader::ResolveResourceMountDirectory(arena);
        frame.graphics().setPipelineCacheDirectory(resourceMountDirectory);
        if(!__hidden_loader::ApplyGraphicsOptions(frame.graphics(), options))
            return -1;

        if(!frame.init())
            return -1;
        i32 initializedFrameWidth = 0;
        i32 initializedFrameHeight = 0;
        frame.graphics().getWindowDimensions(initializedFrameWidth, initializedFrameHeight);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: frame initialized ({}x{})"), initializedFrameWidth, initializedFrameHeight);

        NWB::Core::Filesystem::VolumeSession graphicsVolume(frame.projectObjectArena());
        if(!graphicsVolume.load("graphics", resourceMountDirectory))
            return -1;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: mounted graphics volume from '{}'"), PathToString<tchar>(resourceMountDirectory));

        __hidden_loader::VolumeAssetBinarySource assetBinarySource(graphicsVolume);

        NWB::Core::Assets::AssetRegistry assetRegistry(frame.projectObjectArena());
        NWB::Core::Assets::RegisterAutoCollectedAssetCodecs(assetRegistry);

        NWB::Core::Assets::AssetManager assetManager(frame.projectObjectArena(), assetRegistry, assetBinarySource);

        NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> shaderArchiveRecords{ frame.projectObjectArena() };
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
            {},
            {},
            {},
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
        context.perfCapture = [&frame](const NWB::Core::Perf::CaptureOptions& options){
            frame.setPerfCapture(options);
        };
        context.perfSampleFlush = [&frame](){
            return frame.flushPerfSamples();
        };
        context.perfReportReader = [&frame](){
            return frame.perfReport();
        };
        context.requestQuit = [&frame](){
            frame.requestQuit();
        };

        auto callbacks = NWB::CreateProjectEntryCallbacks(context);
        if(!callbacks){
            NWB_LOGGER_FATAL(NWB_TEXT("CreateProjectEntryCallbacks failed: callback instance is null"));
            return -1;
        }
        __hidden_loader::CallbackShutdownGuard callbackShutdownGuard{ *callbacks };
        __hidden_loader::UpdateCallbackContext updateCallbackContext{ *callbacks };

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

    return 0;
}

static int MainLogic(NWB::Core::Alloc::GlobalArena& arena, const __hidden_loader::LoaderOptions& options, void* inst){
    const usize crashArenaReserveSize = __hidden_loader::CrashArena::StructureAlignedSize(__hidden_loader::s_CrashArenaPayloadSize);
    __hidden_loader::CrashArena crashArena(
        crashArenaReserveSize,
        "NWB::Loader::CrashReportingArena"
    );
    const bool crashReportingInstalled = __hidden_loader::InstallCrashReporting(crashArena, options);

    if(options.useStandaloneLogger){
        NWB::Log::ClientStandalone logger;
        if(!logger.init())
            return -1;
        NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: using standalone log output"));
        if(!crashReportingInstalled)
            NWB_LOGGER_WARNING(NWB_TEXT("Loader: crash reporting unavailable"));

        int crashTestExitCode = 0;
        if(__hidden_loader::RunCrashTestIfRequested(options, crashReportingInstalled, crashTestExitCode))
            return crashTestExitCode;

        return RunProjectRuntime(arena, options, inst);
    }

    NWB::Log::Client logger;
    if(!logger.init(MakeNotNull(options.logAddress.c_str())))
        return -1;
    NWB::Log::ClientLoggerRegistrationGuard loggerRegistrationGuard(logger);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Loader: connected to log server '{}'"), StringConvert(options.logAddress.c_str()));
    if(!crashReportingInstalled)
        NWB_LOGGER_WARNING(NWB_TEXT("Loader: crash reporting unavailable"));

    int crashTestExitCode = 0;
    if(__hidden_loader::RunCrashTestIfRequested(options, crashReportingInstalled, crashTestExitCode))
        return crashTestExitCode;

    return RunProjectRuntime(arena, options, inst);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
static int EntryPoint(isize argc, CharT** argv, void* inst){
    NWB::Core::Alloc::GlobalArena commandLineArena("NWB::Loader::CommandLine");
    __hidden_loader::LoaderOptions options(commandLineArena);
    {
        CLI::App app{ "loader" };

        std::string address = Get<static_cast<usize>(NWB::Core::Common::ArgCommand::LogAddress)>(NWB::Core::Common::g_ArgDefault);
        std::string crashTest;
        u16 port = Get<static_cast<usize>(NWB::Core::Common::ArgCommand::LogPort)>(NWB::Core::Common::g_ArgDefault);
        NWB::Core::Common::ArgAddOption<NWB::Core::Common::ArgCommand::LogAddress>(app, address);
        NWB::Core::Common::ArgAddOption<NWB::Core::Common::ArgCommand::LogPort>(app, port);
        app.add_option("--crash-upload-token", options.crashUploadToken, "Bearer token sent with crash uploads");
        __hidden_loader::AddDebugCommandLineOptions(app, options, crashTest);

        try{
            NWB::Core::Common::ArgParseApp(app, argc, argv);
        }
        catch(const CLI::ParseError& e){
            app.exit(e, NWB_COUT, NWB_CERR);
            return -1;
        }

        options.useStandaloneLogger = address.empty() || port == 0u;
        if(!options.useStandaloneLogger)
            options.logAddress = StringFormat(commandLineArena, "{}:{}", AStringView(address.data(), address.size()), port);
        if(!options.crashTest.assign(AStringView(crashTest.data(), crashTest.size()))){
            NWB_CERR << "--crash-test exceeds ACompactString capacity\n";
            return -1;
        }
    }

    return MainLogic(commandLineArena, options, inst);
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

