// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/filesystem_helpers.h>
#include <tests/test_context.h>

#include <core/crash/package_internal.h>

#include <global/filesystem/operations.h>
#include <global/thread.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using TestArena = NWB::Tests::TestArena<struct CrashTestsTag>;
using CrashTestPath = Path<NWB::Core::Alloc::GlobalArena>;
using NWB::Tests::TextFileContains;
using NWB::Tests::WaitForDirectory;
namespace CrashNames = NWB::Core::Crash::PackageNames;

#define NWB_CRASH_TEST_CHECK NWB_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static CrashTestPath CrashRootDirectory(NWB::Core::Alloc::GlobalArena& arena){
    CrashTestPath executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / CrashNames::s_DefaultRootDirectoryName;

    return CrashTestPath(arena, CrashNames::s_DefaultRootDirectoryName);
}

[[nodiscard]] static CrashTestPath TestRootDirectory(NWB::Core::Alloc::GlobalArena& arena){
    return CrashRootDirectory(arena) / "test_storage";
}

[[nodiscard]] static CrashTestPath TestCaseDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return TestRootDirectory(arena) / testGroup;
}

[[nodiscard]] static CrashTestPath SpoolDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return TestCaseDirectory(arena, testGroup) / "spool";
}

[[nodiscard]] static CrashTestPath BucketDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView bucketName){
    return SpoolDirectory(arena, testGroup) / bucketName;
}

[[nodiscard]] static CrashTestPath PackageDirectory(
    NWB::Core::Alloc::GlobalArena& arena,
    const AStringView testGroup,
    const AStringView bucketName,
    const AStringView packageName
){
    return BucketDirectory(arena, testGroup, bucketName) / packageName;
}

static void RemoveTestArtifacts(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    ErrorCode error;
    static_cast<void>(RemoveAllIfExists(TestCaseDirectory(arena, testGroup), error));
}

[[nodiscard]] static bool CreatePackageDirectory(
    NWB::Core::Alloc::GlobalArena& arena,
    const AStringView testGroup,
    const AStringView bucketName,
    const AStringView packageName
){
    const CrashTestPath packageDirectory = PackageDirectory(arena, testGroup, bucketName, packageName);
    ErrorCode error;
    static_cast<void>(EnsureDirectories(packageDirectory, error));
    if(error)
        return false;

    return WriteTextFile(packageDirectory / "marker.txt", AStringView("package"));
}

template<usize N>
static void CopyPathText(NWB::Core::Alloc::GlobalArena& arena, char (&outText)[N], const CrashTestPath& path){
    const AString<NWB::Core::Alloc::GlobalArena> text = PathToString<char>(arena, path);
    CopyFixedBuffer(outText, AStringView(text.data(), text.size()));
}

template<usize N>
static void BuildCrashIdForProcess(char (&outCrashId)[N], const u32 processId, const u64 sequence){
    CopyFixedBuffer(outCrashId, AStringView(CrashNames::s_CrashIdPrefix));
    AppendUnsignedToFixedBuffer(outCrashId, static_cast<u64>(processId));
    AppendFixedBuffer(outCrashId, "-");
    AppendUnsignedToFixedBuffer(outCrashId, sequence);
}

[[nodiscard]] static NWB::Core::Crash::Detail::PlatformKind::Enum CurrentCrashPlatformKind()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    return NWB::Core::Crash::Detail::PlatformKind::Windows;
#elif defined(NWB_PLATFORM_ANDROID)
    return NWB::Core::Crash::Detail::PlatformKind::Android;
#elif defined(NWB_PLATFORM_LINUX)
    return NWB::Core::Crash::Detail::PlatformKind::Linux;
#else
    return NWB::Core::Crash::Detail::PlatformKind::Unknown;
#endif
}

static void FillPackageRequest(
    NWB::Core::Alloc::GlobalArena& arena,
    NWB::Core::Crash::Detail::CrashRequest& request,
    const CrashTestPath& spoolDirectory,
    const AStringView crashId
){
    request = NWB::Core::Crash::Detail::CrashRequest{};
    request.platform = CurrentCrashPlatformKind();
    request.reasonKind = NWB::Core::Crash::Detail::CrashReasonKind::ManualDump;
    request.processId = CurrentProcessId();
    request.threadId = CurrentThreadId();
    CopyFixedBuffer(request.crashId, crashId);
    CopyFixedBuffer(request.applicationName, AStringView("crash_tests"));
    CopyFixedBuffer(request.versionText, AStringView("1"));
    CopyFixedBuffer(request.buildId, AStringView("test"));
    CopyFixedBuffer(request.abi, CurrentAbiName());
    CopyPathText(arena, request.spoolDirectory, spoolDirectory);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestWriteCrashPackageCreatesRequiredFiles(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_package_write_test");
    constexpr AStringView s_CrashId("crash-package-required-files");
    RemoveTestArtifacts(arena, s_Group);

    NWB::Core::Crash::Detail::CrashRequest request;
    FillPackageRequest(arena, request, SpoolDirectory(arena, s_Group), s_CrashId);
    request.instructionPointer = 1u;
    request.callstackFrameCount = 2u;
    request.callstackFrames[0] = 1u;
    request.callstackFrames[1] = 2u;
    request.triggerLine = 7u;
    CopyFixedBuffer(request.triggerCategory, AStringView("test"));
    CopyFixedBuffer(request.triggerMessage, AStringView("required-files"));
    CopyFixedBuffer(request.triggerFile, AStringView("tests/crash/tests.cpp"));

    NWB_CRASH_TEST_CHECK(context, NWB::Core::Crash::Detail::WriteCrashPackage(request));

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CrashId);
    NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_ManifestFileName));
    NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_MetadataFileName));
    NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_BreadcrumbsFileName));
    NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_EmergencyFileName));
    NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_ArtifactStrategyFileName));
    NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_CpuContextFileName));
    NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_CallstackFileName));
    NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_SymbolicationFileName));
    NWB_CRASH_TEST_CHECK(context, TextFileContains(packageDirectory / CrashNames::s_ManifestFileName, AStringView("\"trigger_message\": \"required-files\"")));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestWriteCrashPackageFailsWhenSpoolPathIsFile(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_package_bad_spool_test");
    RemoveTestArtifacts(arena, s_Group);

    ErrorCode error;
    static_cast<void>(EnsureDirectories(TestCaseDirectory(arena, s_Group), error));
    NWB_CRASH_TEST_CHECK(context, !error);
    const CrashTestPath spoolPath = SpoolDirectory(arena, s_Group);
    NWB_CRASH_TEST_CHECK(context, WriteTextFile(spoolPath, AStringView("not a directory")));

    NWB::Core::Crash::Detail::CrashRequest request;
    FillPackageRequest(arena, request, spoolPath, AStringView("crash-package-bad-spool"));
    NWB_CRASH_TEST_CHECK(context, !NWB::Core::Crash::Detail::WriteCrashPackage(request));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestCrashBreadcrumbPersistsCurrentRing(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena breadcrumbArena(
        NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u),
        "NWB::Tests::Crash::BreadcrumbArena"
    );
    constexpr AStringView s_Group("crash_breadcrumb_persist_test");
    RemoveTestArtifacts(arena, s_Group);

    const CrashTestPath spoolDirectory = SpoolDirectory(arena, s_Group);
    ErrorCode error;
    static_cast<void>(EnsureDirectories(spoolDirectory, error));
    NWB_CRASH_TEST_CHECK(context, !error);

    char previousSpoolDirectory[NWB::Core::Crash::Detail::s_MaxPathText] = {};
    NWB::Core::Crash::Detail::FixedBreadcrumb previousBreadcrumbs[NWB::Core::Crash::Detail::s_MaxBreadcrumbs] = {};
    usize previousNextBreadcrumb = 0u;
    const u64 previousBreadcrumbOrder = NWB::Core::Crash::Detail::g_State.breadcrumbOrder.load(MemoryOrder::relaxed);

    {
        ScopedLock lock(NWB::Core::Crash::Detail::g_State.mutex);
        CopyFixedBuffer(previousSpoolDirectory, NWB::Core::Crash::Detail::g_State.spoolDirectoryText);
        previousNextBreadcrumb = NWB::Core::Crash::Detail::g_State.nextBreadcrumb;
        for(usize i = 0u; i < NWB::Core::Crash::Detail::s_MaxBreadcrumbs; ++i)
            previousBreadcrumbs[i] = NWB::Core::Crash::Detail::g_State.breadcrumbs[i];

        CopyPathText(arena, NWB::Core::Crash::Detail::g_State.spoolDirectoryText, spoolDirectory);
        NWB::Core::Crash::Detail::g_State.nextBreadcrumb = 0u;
        for(NWB::Core::Crash::Detail::FixedBreadcrumb& breadcrumb : NWB::Core::Crash::Detail::g_State.breadcrumbs)
            breadcrumb = NWB::Core::Crash::Detail::FixedBreadcrumb{};
        NWB::Core::Crash::Detail::g_State.breadcrumbOrder.store(1u, MemoryOrder::relaxed);
    }

    NWB_CRASH_TEST_CHECK(context, NWB::Core::Crash::AddCrashBreadcrumb(breadcrumbArena, AStringView("test"), AStringView("persisted breadcrumb")));
    NWB_CRASH_TEST_CHECK(context, TextFileContains(spoolDirectory / CrashNames::s_CurrentBreadcrumbsFileName, AStringView("[test] persisted breadcrumb")));

    {
        ScopedLock lock(NWB::Core::Crash::Detail::g_State.mutex);
        CopyFixedBuffer(NWB::Core::Crash::Detail::g_State.spoolDirectoryText, AStringView(previousSpoolDirectory));
        NWB::Core::Crash::Detail::g_State.nextBreadcrumb = previousNextBreadcrumb;
        for(usize i = 0u; i < NWB::Core::Crash::Detail::s_MaxBreadcrumbs; ++i)
            NWB::Core::Crash::Detail::g_State.breadcrumbs[i] = previousBreadcrumbs[i];
        NWB::Core::Crash::Detail::g_State.breadcrumbOrder.store(previousBreadcrumbOrder, MemoryOrder::relaxed);
    }

    RemoveTestArtifacts(arena, s_Group);
}

static void TestCrashSpoolRetentionPrunesOldestPackages(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_test");
    RemoveTestArtifacts(arena, s_Group);

    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-001"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-002"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-003"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "bad package name"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, "crash-001"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, "crash-002"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, "crash-001"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, "crash-002"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, "crash-001"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, "crash-002"));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 2u;
    retention.maxUploadedPackages = 1u;
    retention.maxFailedPackages = 1u;
    retention.maxUploadingPackages = 1u;
    NWB_CRASH_TEST_CHECK(context, NWB::Core::Crash::Detail::ApplyCrashSpoolRetention(arena, SpoolDirectory(arena, s_Group), retention));

    NWB_CRASH_TEST_CHECK(context, PathIsMissing(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-001")));
    NWB_CRASH_TEST_CHECK(context, PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-002")));
    NWB_CRASH_TEST_CHECK(context, PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-003")));
    NWB_CRASH_TEST_CHECK(context, PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "bad package name")));
    NWB_CRASH_TEST_CHECK(context, PathIsMissing(PackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, "crash-001")));
    NWB_CRASH_TEST_CHECK(context, PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, "crash-002")));
    NWB_CRASH_TEST_CHECK(context, PathIsMissing(PackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, "crash-001")));
    NWB_CRASH_TEST_CHECK(context, PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, "crash-002")));
    NWB_CRASH_TEST_CHECK(context, PathIsMissing(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, "crash-001")));
    NWB_CRASH_TEST_CHECK(context, PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, "crash-002")));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestCrashSpoolRetentionZeroDisablesPruning(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_zero_test");
    RemoveTestArtifacts(arena, s_Group);

    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-001"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-002"));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 0u;
    retention.maxUploadedPackages = 0u;
    retention.maxFailedPackages = 0u;
    retention.maxUploadingPackages = 0u;
    NWB_CRASH_TEST_CHECK(context, NWB::Core::Crash::Detail::ApplyCrashSpoolRetention(arena, SpoolDirectory(arena, s_Group), retention));

    NWB_CRASH_TEST_CHECK(context, PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-001")));
    NWB_CRASH_TEST_CHECK(context, PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-002")));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestCrashSpoolRetentionProtectsActivePendingPackage(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_protect_test");
    RemoveTestArtifacts(arena, s_Group);

    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-001"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-002"));
    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-003"));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 1u;
    retention.maxUploadedPackages = 0u;
    retention.maxFailedPackages = 0u;
    retention.maxUploadingPackages = 0u;
    NWB_CRASH_TEST_CHECK(context, NWB::Core::Crash::Detail::ApplyCrashSpoolRetention(
        arena,
        SpoolDirectory(arena, s_Group),
        retention,
        AStringView("crash-001")
    ));

    NWB_CRASH_TEST_CHECK(context, PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-001")));
    NWB_CRASH_TEST_CHECK(context, PathIsMissing(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-002")));
    NWB_CRASH_TEST_CHECK(context, PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "crash-003")));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestFlushReportsFailsWhenUploadingRecoveryIsBlocked(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_uploading_recovery_blocked_test");
    constexpr AStringView s_CrashId("crash-recovery-blocked");
    RemoveTestArtifacts(arena, s_Group);

    NWB_CRASH_TEST_CHECK(context, CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CrashId));
    NWB_CRASH_TEST_CHECK(context, WriteTextFile(BucketDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName), AStringView("blocked")));

    NWB::Core::Crash::Detail::CrashUploadSnapshot snapshot;
    CopyPathText(arena, snapshot.spoolDirectory, SpoolDirectory(arena, s_Group));
    CopyFixedBuffer(snapshot.logServerUrl, AStringView("http://127.0.0.1:1"));

    NWB_CRASH_TEST_CHECK(context, !NWB::Core::Crash::Detail::FlushPendingCrashReportsImpl(arena, snapshot));
    NWB_CRASH_TEST_CHECK(context, PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CrashId)));
    NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(BucketDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName)));
    NWB_CRASH_TEST_CHECK(
        context,
        TextFileContains(
            PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CrashId) / CrashNames::s_UploadAttemptFileName,
            AStringView(CrashNames::s_UploadAttemptRetryInterruptedState)
        )
    );

    RemoveTestArtifacts(arena, s_Group);
}

#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
static void TestDesktopInstalledHandlerWritesManualDumpPackage(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(
        NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u),
        "NWB::Tests::Crash::InstallArena"
    );
    constexpr AStringView s_Group("crash_desktop_handler_runtime_test");
    RemoveTestArtifacts(arena, s_Group);

    NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
    config.applicationName = AStringView("crash_tests");
    config.version = AStringView("1");
    config.buildId = AStringView("desktop-handler-runtime-test");
    config.spoolDirectory = SpoolDirectory(arena, s_Group);

    {
        ScopedLock lock(NWB::Core::Crash::Detail::g_State.mutex);
        NWB::Core::Crash::Detail::g_State.crashSequence.store(1u, MemoryOrder::relaxed);
    }

    const bool installed = NWB::Core::Crash::InstallCrashHandler(installArena, config);
    NWB_CRASH_TEST_CHECK(context, installed);

    if(installed){
        NWB::Core::Crash::Detail::CrashDumpRequestOptions options;
        options.waitMilliseconds = NWB::Core::Crash::Detail::s_PlatformCrashHandlerWaitMilliseconds;
        options.triggerCategory = AStringView("test");
        options.triggerMessage = AStringView("desktop handler runtime");
        options.triggerFile = AStringView("tests/crash/tests.cpp");
        NWB::Core::Crash::Detail::ManualDumpContextStorage contextStorage;
        NWB::Core::Crash::Detail::CaptureManualDumpContext(options, contextStorage);
        const NWB::Core::Crash::CrashDumpResult result = NWB::Core::Crash::Detail::RequestCrashDump(
            NWB::Core::Crash::Detail::CrashReasonKind::ManualDump,
            0u,
            options
        );
        NWB_CRASH_TEST_CHECK(
            context,
            result.status == NWB::Core::Crash::CrashDumpStatus::PackageWritten
        );

        char crashId[NWB::Core::Crash::Detail::s_MaxShortText] = {};
        BuildCrashIdForProcess(crashId, CurrentProcessId(), 1u);

        const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(crashId));
        NWB_CRASH_TEST_CHECK(context, WaitForDirectory(packageDirectory, 3000u));
        NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_ManifestFileName));
        NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_SymbolicationFileName));
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
        NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_CallstackFileName));
#endif
        NWB_CRASH_TEST_CHECK(context, TextFileContains(packageDirectory / CrashNames::s_ManifestFileName, AStringView("\"trigger_message\": \"desktop handler runtime\"")));
        NWB::Core::Crash::UninstallCrashHandler();
    }

    RemoveTestArtifacts(arena, s_Group);
}
#endif

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
static void TestLinuxSignalHandlerWritesCrashPackage(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_linux_signal_runtime_test");
    RemoveTestArtifacts(arena, s_Group);

    const CrashTestPath spoolDirectory = SpoolDirectory(arena, s_Group);
    const pid_t childPid = fork();
    NWB_CRASH_TEST_CHECK(context, childPid >= 0);
    if(childPid == 0){
        NWB::Core::Alloc::PersistentArena installArena(
            NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u),
            "NWB::Tests::Crash::SignalChildInstallArena"
        );
        NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
        config.applicationName = AStringView("crash_tests");
        config.version = AStringView("1");
        config.buildId = AStringView("linux-signal-runtime-test");
        config.spoolDirectory = spoolDirectory;

        {
            ScopedLock lock(NWB::Core::Crash::Detail::g_State.mutex);
            NWB::Core::Crash::Detail::g_State.crashSequence.store(1u, MemoryOrder::relaxed);
        }

        if(!NWB::Core::Crash::InstallCrashHandler(installArena, config))
            _exit(111);

        raise(SIGSEGV);
        _exit(112);
    }

    if(childPid > 0){
        int status = 0;
        while(waitpid(childPid, &status, 0) < 0){
            if(errno == EINTR)
                continue;
            break;
        }

        NWB_CRASH_TEST_CHECK(context, WIFSIGNALED(status));
        NWB_CRASH_TEST_CHECK(context, WTERMSIG(status) == SIGSEGV);

        char crashId[NWB::Core::Crash::Detail::s_MaxShortText] = {};
        BuildCrashIdForProcess(crashId, static_cast<u32>(childPid), 1u);

        const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(crashId));
        NWB_CRASH_TEST_CHECK(context, WaitForDirectory(packageDirectory, 1000u));
        NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_ManifestFileName));
        NWB_CRASH_TEST_CHECK(context, PathIsRegularFile(packageDirectory / CrashNames::s_CallstackFileName));
        NWB_CRASH_TEST_CHECK(context, TextFileContains(packageDirectory / CrashNames::s_ManifestFileName, AStringView("\"reason_kind\": \"signal\"")));
    }

    RemoveTestArtifacts(arena, s_Group);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_CRASH_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("crash", [](NWB::Tests::TestContext& context){
    __hidden_crash_tests::TestWriteCrashPackageCreatesRequiredFiles(context);
    __hidden_crash_tests::TestWriteCrashPackageFailsWhenSpoolPathIsFile(context);
    __hidden_crash_tests::TestCrashBreadcrumbPersistsCurrentRing(context);
    __hidden_crash_tests::TestCrashSpoolRetentionPrunesOldestPackages(context);
    __hidden_crash_tests::TestCrashSpoolRetentionZeroDisablesPruning(context);
    __hidden_crash_tests::TestCrashSpoolRetentionProtectsActivePendingPackage(context);
    __hidden_crash_tests::TestFlushReportsFailsWhenUploadingRecoveryIsBlocked(context);
#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
    __hidden_crash_tests::TestDesktopInstalledHandlerWritesManualDumpPackage(context);
#endif
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    __hidden_crash_tests::TestLinuxSignalHandlerWritesCrashPackage(context);
#endif
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

