// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/filesystem/utility.h>
#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <core/crash/package_internal.h>

#include <global/filesystem/operations.h>
#include <global/thread.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_CRASH_001 = "crash-001";
static constexpr AStringView s_CRASH_002 = "crash-002";
static constexpr AStringView s_CRASH_003 = "crash-003";
static constexpr AStringView s_CRASH_TESTS = "crash_tests";
static constexpr AStringView s_TEST = "test";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = NWB::Tests::TestArena<struct CrashTestsTag>;
using CrashTestPath = Path<NWB::Core::Alloc::GlobalArena>;
using ::TextFileContains;
using ::WaitForDirectory;
namespace CrashNames = NWB::Core::Crash::PackageNames;

inline constexpr Name s_InstallArena("tests/integration/crash/install");
inline constexpr Name s_SignalChildInstallArena("tests/integration/crash/signal_child_install");


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
    if(!RemoveAllIfExists(TestCaseDirectory(arena, testGroup), error))
        EXPECT_FALSE(error);
}

[[nodiscard]] static bool CreatePackageDirectory(
    NWB::Core::Alloc::GlobalArena& arena,
    const AStringView testGroup,
    const AStringView bucketName,
    const AStringView packageName
){
    const CrashTestPath packageDirectory = PackageDirectory(arena, testGroup, bucketName, packageName);
    ErrorCode error;
    if(!EnsureDirectories(packageDirectory, error))
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
    CopyFixedBuffer(request.applicationName, AStringView(s_CRASH_TESTS));
    CopyFixedBuffer(request.versionText, AStringView("1"));
    CopyFixedBuffer(request.buildId, AStringView(s_TEST));
    CopyFixedBuffer(request.abi, CurrentAbiName());
    CopyPathText(arena, request.spoolDirectory, spoolDirectory);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Crash, WriteCrashPackageFailsWhenSpoolPathIsFile){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_package_bad_spool_test");
    RemoveTestArtifacts(arena, s_Group);

    ErrorCode error;
    EXPECT_TRUE(EnsureDirectories(TestCaseDirectory(arena, s_Group), error));
    const CrashTestPath spoolPath = SpoolDirectory(arena, s_Group);
    EXPECT_TRUE(WriteTextFile(spoolPath, AStringView("not a directory")));

    NWB::Core::Crash::Detail::CrashRequest request;
    FillPackageRequest(arena, request, spoolPath, AStringView("crash-package-bad-spool"));
    EXPECT_FALSE(NWB::Core::Crash::Detail::WriteCrashPackage(request));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, CrashSpoolRetentionPrunesOldestPackages){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_test");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_003));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "bad package name"));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_CRASH_001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_CRASH_002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_CRASH_001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_CRASH_002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CRASH_001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CRASH_002));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 2u;
    retention.maxUploadedPackages = 1u;
    retention.maxFailedPackages = 1u;
    retention.maxUploadingPackages = 1u;
    EXPECT_TRUE(NWB::Core::Crash::Detail::ApplyCrashSpoolRetention(arena, SpoolDirectory(arena, s_Group), retention));

    EXPECT_TRUE(PathIsMissing(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_001)));
    EXPECT_TRUE(PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_002)));
    EXPECT_TRUE(PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_003)));
    EXPECT_TRUE(PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "bad package name")));
    EXPECT_TRUE(PathIsMissing(PackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_CRASH_001)));
    EXPECT_TRUE(PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_CRASH_002)));
    EXPECT_TRUE(PathIsMissing(PackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_CRASH_001)));
    EXPECT_TRUE(PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_CRASH_002)));
    EXPECT_TRUE(PathIsMissing(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CRASH_001)));
    EXPECT_TRUE(PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CRASH_002)));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, CrashSpoolRetentionZeroDisablesPruning){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_zero_test");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_002));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 0u;
    retention.maxUploadedPackages = 0u;
    retention.maxFailedPackages = 0u;
    retention.maxUploadingPackages = 0u;
    EXPECT_TRUE(NWB::Core::Crash::Detail::ApplyCrashSpoolRetention(arena, SpoolDirectory(arena, s_Group), retention));

    EXPECT_TRUE(PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_001)));
    EXPECT_TRUE(PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_002)));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, CrashSpoolRetentionProtectsActivePendingPackage){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_protect_test");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_003));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 1u;
    retention.maxUploadedPackages = 0u;
    retention.maxFailedPackages = 0u;
    retention.maxUploadingPackages = 0u;
    EXPECT_TRUE(NWB::Core::Crash::Detail::ApplyCrashSpoolRetention(
        arena,
        SpoolDirectory(arena, s_Group),
        retention,
        AStringView("crash-001")
    ));

    EXPECT_TRUE(PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_001)));
    EXPECT_TRUE(PathIsMissing(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_002)));
    EXPECT_TRUE(PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_003)));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, FlushReportsFailsWhenUploadingRecoveryIsBlocked){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_uploading_recovery_blocked_test");
    constexpr AStringView s_CrashId("crash-recovery-blocked");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CrashId));
    EXPECT_TRUE(WriteTextFile(BucketDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName), AStringView("blocked")));

    NWB::Core::Crash::Detail::CrashUploadSnapshot snapshot;
    CopyPathText(arena, snapshot.spoolDirectory, SpoolDirectory(arena, s_Group));
    CopyFixedBuffer(snapshot.logServerUrl, AStringView("http://127.0.0.1:1"));

    EXPECT_FALSE(NWB::Core::Crash::Detail::FlushPendingCrashReportsImpl(arena, snapshot));
    EXPECT_TRUE(PathIsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CrashId)));
    EXPECT_TRUE(PathIsRegularFile(BucketDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName)));
    EXPECT_TRUE(TextFileContains(
            PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CrashId) / CrashNames::s_UploadAttemptFileName,
            AStringView(CrashNames::s_UploadAttemptRetryInterruptedState)
        ));

    RemoveTestArtifacts(arena, s_Group);
}

#if defined(NWB_PLATFORM_WINDOWS)
TEST(Crash, DesktopHandlerDoesNotRetainUnrelatedInheritableHandles){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(
        s_InstallArena,
        NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u)
    );
    constexpr AStringView s_Group("crash_handler_handle_inheritance_test");
    RemoveTestArtifacts(arena, s_Group);

    const CrashTestPath testDirectory = TestCaseDirectory(arena, s_Group);
    ErrorCode error;
    ASSERT_TRUE(EnsureDirectories(testDirectory, error));

    const CrashTestPath sentinelPath = testDirectory / "inheritable_handle_sentinel.tmp";
    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;
    const HANDLE sentinelHandle = CreateFileW(
        sentinelPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &securityAttributes,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        nullptr
    );
    ASSERT_NE(sentinelHandle, INVALID_HANDLE_VALUE);

    NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
    config.applicationName = AStringView(s_CRASH_TESTS);
    config.version = AStringView("1");
    config.buildId = AStringView("handle-inheritance-test");
    config.spoolDirectory = SpoolDirectory(arena, s_Group);

    const bool installed = NWB::Core::Crash::InstallCrashHandler(installArena, config);
    EXPECT_TRUE(installed);
    EXPECT_TRUE(CloseHandle(sentinelHandle));
    EXPECT_TRUE(DeleteFileW(sentinelPath.c_str()));
    if(installed)
        NWB::Core::Crash::UninstallCrashHandler();

    RemoveTestArtifacts(arena, s_Group);
}
#endif

#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
TEST(Crash, LinuxManualDumpUnwindsBeyondTheLeafWithoutFramePointers){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(
        s_InstallArena,
        NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u)
    );
    constexpr AStringView s_Group("crash_desktop_handler_runtime_test");
    RemoveTestArtifacts(arena, s_Group);

    NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
    config.applicationName = AStringView(s_CRASH_TESTS);
    config.version = AStringView("1");
    config.buildId = AStringView("desktop-handler-runtime-test");
    config.spoolDirectory = SpoolDirectory(arena, s_Group);

    {
        ScopedLock lock(NWB::Core::Crash::Detail::g_State.mutex);
        NWB::Core::Crash::Detail::g_State.crashSequence.store(1u, MemoryOrder::relaxed);
    }

    const bool installed = NWB::Core::Crash::InstallCrashHandler(installArena, config);
    ASSERT_TRUE(installed);

    NWB::Core::Crash::Detail::CrashDumpRequestOptions options;
    options.waitMilliseconds = NWB::Core::Crash::Detail::s_PlatformCrashHandlerWaitMilliseconds;
    options.triggerCategory = AStringView(s_TEST);
    options.triggerMessage = AStringView("desktop handler runtime");
    options.triggerFile = AStringView("tests/integration/crash/crash_package_tests.cpp");
    NWB::Core::Crash::Detail::ManualDumpContextStorage contextStorage;
    NWB::Core::Crash::Detail::CaptureManualDumpContext(options, contextStorage);
    const NWB::Core::Crash::CrashDumpResult result = NWB::Core::Crash::Detail::RequestCrashDump(
        NWB::Core::Crash::Detail::CrashReasonKind::ManualDump,
        0u,
        options
    );
    EXPECT_EQ(result.status, NWB::Core::Crash::CrashDumpStatus::PackageWritten);

    char crashId[NWB::Core::Crash::Detail::s_MaxShortText] = {};
    BuildCrashIdForProcess(crashId, CurrentProcessId(), 1u);

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(crashId));
    EXPECT_TRUE(WaitForDirectory(packageDirectory, 3000u));
    EXPECT_TRUE(PathIsRegularFile(packageDirectory / CrashNames::s_CallstackFileName));
    // A "#1 " frame proves the unwinder walked past the leaf frame; final builds omit the frame pointer, so
    // this guards that .eh_frame-based capture keeps producing a full callstack.
    EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_CallstackFileName, AStringView("#1 0x")));
    NWB::Core::Crash::UninstallCrashHandler();
    RemoveTestArtifacts(arena, s_Group);
}
#endif


TEST(Crash, DesktopInstalledHandlerWritesGpuCrashTextOnlyPackage){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(
        s_InstallArena,
        NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u)
    );
    constexpr AStringView s_Group("crash_gpu_text_only_runtime_test");
    RemoveTestArtifacts(arena, s_Group);

    NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
    config.applicationName = AStringView(s_CRASH_TESTS);
    config.version = AStringView("1");
    config.buildId = AStringView("gpu-text-only-runtime-test");
    config.spoolDirectory = SpoolDirectory(arena, s_Group);

    {
        ScopedLock lock(NWB::Core::Crash::Detail::g_State.mutex);
        NWB::Core::Crash::Detail::g_State.crashSequence.store(1u, MemoryOrder::relaxed);
    }

    const bool installed = NWB::Core::Crash::InstallCrashHandler(installArena, config);
    ASSERT_TRUE(installed);

    NWB::Core::Crash::Detail::CrashDumpRequestOptions options;
    options.waitMilliseconds = NWB::Core::Crash::Detail::s_PlatformCrashHandlerWaitMilliseconds;
    options.triggerCategory = AStringView(NWB::Core::Crash::Detail::s_GpuCrashCategory);
    options.gpuReport = AStringView("minimal GPU crash report: no vendor GPU dump or device-fault details were available\n");
    options.gpuDumpKind = NWB::Core::Crash::GpuCrashDumpKind::None;
    NWB::Core::Crash::Detail::ManualDumpContextStorage contextStorage;
    NWB::Core::Crash::Detail::CaptureManualDumpContext(options, contextStorage);
    const NWB::Core::Crash::CrashDumpResult result = NWB::Core::Crash::Detail::RequestCrashDump(
        NWB::Core::Crash::Detail::CrashReasonKind::GpuCrash,
        0u,
        options
    );
    EXPECT_EQ(result.status, NWB::Core::Crash::CrashDumpStatus::PackageWritten);

    char crashId[NWB::Core::Crash::Detail::s_MaxShortText] = {};
    BuildCrashIdForProcess(crashId, CurrentProcessId(), 1u);

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(crashId));
    EXPECT_TRUE(WaitForDirectory(packageDirectory, 3000u));
    EXPECT_TRUE(PathIsRegularFile(packageDirectory / CrashNames::s_GpuCrashReportFileName));
    EXPECT_TRUE(PathIsMissing(packageDirectory / CrashNames::s_GpuDetectiveCaptureFileName));
    EXPECT_TRUE(PathIsMissing(packageDirectory / CrashNames::s_AftermathGpuDumpFileName));
    EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_GpuCrashReportFileName, AStringView("minimal GPU crash report")));
    NWB::Core::Crash::UninstallCrashHandler();

    RemoveTestArtifacts(arena, s_Group);
}
#endif

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
TEST(Crash, LinuxSignalHandlerWritesCrashPackage){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_linux_signal_runtime_test");
    RemoveTestArtifacts(arena, s_Group);

    const CrashTestPath spoolDirectory = SpoolDirectory(arena, s_Group);
    const pid_t childPid = fork();
    ASSERT_GE(childPid, 0);
    if(childPid == 0){
        NWB::Core::Alloc::PersistentArena installArena(
            s_SignalChildInstallArena,
            NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u)
        );
        NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
        config.applicationName = AStringView(s_CRASH_TESTS);
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

        EXPECT_TRUE(WIFSIGNALED(status));
        EXPECT_EQ(WTERMSIG(status), SIGSEGV);

        char crashId[NWB::Core::Crash::Detail::s_MaxShortText] = {};
        BuildCrashIdForProcess(crashId, static_cast<u32>(childPid), 1u);

        const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(crashId));
        EXPECT_TRUE(WaitForDirectory(packageDirectory, 1000u));
        EXPECT_TRUE(PathIsRegularFile(packageDirectory / CrashNames::s_ManifestFileName));
        EXPECT_TRUE(PathIsRegularFile(packageDirectory / CrashNames::s_CallstackFileName));
        // The faulting callstack must reach past the leaf frame even with the frame pointer omitted (final builds).
        EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_CallstackFileName, AStringView("#1 0x")));
        EXPECT_TRUE(PathIsRegularFile(packageDirectory / CrashNames::s_ProcMapsFileName));
        EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_ManifestFileName, AStringView("\"reason_kind\": \"signal\"")));
    }

    RemoveTestArtifacts(arena, s_Group);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

