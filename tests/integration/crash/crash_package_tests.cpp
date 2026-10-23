// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_support.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <dbghelp.h>
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_package_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace CrashTestSupport;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
TEST(Crash, SpoolAdmissionPreservesFilesystemCategoryAndCodeAfterUnlock){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_failed_spool_admission");
    RemoveTestArtifacts(arena, s_Group);
    ASSERT_TRUE(EnsureDirectories(TestCaseDirectory(arena, s_Group)));
    const CrashTestPath blocked = TestCaseDirectory(arena, s_Group) / "blocked";
    ASSERT_TRUE(WriteTextFile(blocked, AStringView("This file cannot be a parent directory.\n")));
    const CrashTestPath spoolDirectory = blocked / s_SpoolDirectory;
    const auto expectedFailure = EnsureDirectories(spoolDirectory / CrashNames::s_PendingDirectoryName);
    ASSERT_FALSE(expectedFailure);
    char expectedCategory[64u] = {};
    CopyFixedBuffer(expectedCategory, "category=");
    AppendFixedBuffer(expectedCategory, AStringView(expectedFailure.error().category().name()));
    char expectedCode[32u] = {};
    CopyFixedBuffer(expectedCode, "code=");
    const i64 nativeCode = expectedFailure.error().value();
    if(nativeCode < 0)
        AppendFixedBuffer(expectedCode, "-");
    AppendUnsignedToFixedBuffer(expectedCode, static_cast<u64>(nativeCode < 0 ? -nativeCode : nativeCode));
    NWB::Core::Crash::UninstallCrashHandler();
    const auto previousCallback = DiagnosticDetail::g_EventCallback.exchange(ObserveNativeLifecycle, MemoryOrder::acq_rel);
    ScopeExit restoreCallback([previousCallback]()noexcept{ SetDiagnosticEventCallback(previousCallback); });
    s_NativeLifecycleDiagnostics = 0u;
    s_UnlockedLifecycleDiagnostics = 0u;
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    NWB::Core::Crash::CrashConfig config(installArena);
    config.applicationName = "bad-spool-admission";
    config.spoolDirectory = spoolDirectory;
    EXPECT_FALSE(NWB::Core::Crash::InstallCrashHandler(config));
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::g_State.installed);
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::g_State.handlerStarted);
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::g_State.platformRestorePending);
    EXPECT_EQ(s_NativeLifecycleDiagnostics, 1u);
    EXPECT_EQ(s_UnlockedLifecycleDiagnostics, 1u);
    EXPECT_EQ(AStringView(s_LastLifecycleOperation), AStringView("prepare_spool"));
    EXPECT_NE(AStringView(s_LastLifecycleDiagnostic).find(AStringView(expectedCategory)), AStringView::npos);
    EXPECT_NE(AStringView(s_LastLifecycleDiagnostic).find(AStringView(expectedCode)), AStringView::npos);
    NWB::Core::Crash::UninstallCrashHandler();
    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, FailedExecutableAdmissionReleasesResourcesAndObservesFailureAfterUnlock){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_bad_executable_admission");
    RemoveTestArtifacts(arena, s_Group);
    ASSERT_TRUE(EnsureDirectories(TestCaseDirectory(arena, s_Group)));
    const CrashTestPath invalidExecutable = TestCaseDirectory(arena, s_Group) / "invalid_handler";
    ASSERT_TRUE(WriteTextFile(invalidExecutable, AStringView("This is not an executable image.\n")));
#if defined(NWB_PLATFORM_LINUX)
    ASSERT_EQ(chmod(invalidExecutable.c_str(), 0700), 0);
#endif
    NWB::Core::Crash::UninstallCrashHandler();
    const auto previousCallback = DiagnosticDetail::g_EventCallback.exchange(ObserveNativeLifecycle, MemoryOrder::acq_rel);
    ScopeExit restoreCallback([previousCallback]()noexcept{ SetDiagnosticEventCallback(previousCallback); });
    s_NativeLifecycleDiagnostics = 0u;
    s_UnlockedLifecycleDiagnostics = 0u;
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    const auto executableText = PathToString<char>(arena, invalidExecutable);
    NWB::Core::Crash::CrashConfig config(installArena);
    config.applicationName = "bad-executable-admission";
    config.spoolDirectory = SpoolDirectory(arena, s_Group);
    config.handlerExecutablePath = AStringView(executableText);
    EXPECT_FALSE(NWB::Core::Crash::InstallCrashHandler(config));
#if defined(NWB_PLATFORM_WINDOWS)
    DWORD before = 0u;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &before));
#else
    constexpr AStringView s_FileDescriptorsDirectory = "/proc/self/fd";
    const auto before = DirectoryIterator<NWB::Core::Alloc::GlobalArena>::Create(CrashTestPath(arena, s_FileDescriptorsDirectory));
    ASSERT_TRUE(before);
    const auto beforeCount = static_cast<usize>(before->end() - before->begin());
#endif
    for(u32 attempt = 0u; attempt < 4u; ++attempt){
        EXPECT_FALSE(NWB::Core::Crash::InstallCrashHandler(config));
        EXPECT_FALSE(NWB::Core::Crash::CrashDetail::g_State.installed);
        EXPECT_FALSE(NWB::Core::Crash::CrashDetail::g_State.handlerStarted);
        EXPECT_FALSE(NWB::Core::Crash::CrashDetail::g_State.platformRestorePending);
    }
#if defined(NWB_PLATFORM_WINDOWS)
    DWORD after = 0u;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &after));
    EXPECT_EQ(after, before);
#else
    const auto after = DirectoryIterator<NWB::Core::Alloc::GlobalArena>::Create(CrashTestPath(arena, s_FileDescriptorsDirectory));
    ASSERT_TRUE(after);
    EXPECT_EQ(static_cast<usize>(after->end() - after->begin()), beforeCount);
#endif
    EXPECT_EQ(s_NativeLifecycleDiagnostics, 5u);
    EXPECT_EQ(s_UnlockedLifecycleDiagnostics, s_NativeLifecycleDiagnostics);
    EXPECT_NE(AStringView(s_LastLifecycleDiagnostic).find("code="), AStringView::npos);
    NWB::Core::Crash::UninstallCrashHandler();
    RemoveTestArtifacts(arena, s_Group);
}
#endif

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
alignas(MaxAlign) static u8 g_ForeignSignalStack[256u * 1024u] = {};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void OriginalSignalOwner(const int signalNumber)noexcept{ static_cast<void>(signalNumber); }

static void ForeignSignalOwner(const int signalNumber)noexcept{ static_cast<void>(signalNumber); }

[[noreturn]] static void OriginalTerminateOwner()noexcept{ _exit(123); }

[[noreturn]] static void ForeignTerminateOwner()noexcept{ _exit(124); }

TEST(Crash, UninstallRestoresDisabledAlternateStackAndPriorSignalAndTerminateOwners){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_restore_native_hooks");
    RemoveTestArtifacts(arena, s_Group);
    NWB::Core::Crash::UninstallCrashHandler();
    stack_t originalStack = {};
    struct sigaction originalAction = {};
    ASSERT_EQ(sigaltstack(nullptr, &originalStack), 0);
    ASSERT_EQ(sigaction(SIGILL, nullptr, &originalAction), 0);
    const auto originalTerminate = QueryTerminateHandler();
    ScopeExit restore([&]()noexcept{
        NWB::Core::Crash::UninstallCrashHandler();
        if(sigaction(SIGILL, &originalAction, nullptr) != 0 || sigaltstack(&originalStack, nullptr) != 0)
            TerminateInvariant();
        ExchangeTerminateHandler(originalTerminate);
    }
    );
    stack_t disabled = {};
    disabled.ss_flags = SS_DISABLE;
    ASSERT_EQ(sigaltstack(&disabled, nullptr), 0);
    struct sigaction desired = {};
    desired.sa_handler = OriginalSignalOwner;
    ASSERT_EQ(sigemptyset(&desired.sa_mask), 0);
    ASSERT_EQ(sigaction(SIGILL, &desired, nullptr), 0);
    ExchangeTerminateHandler(OriginalTerminateOwner);
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    NWB::Core::Crash::CrashConfig config(installArena);
    config.applicationName = "restore-native-hooks";
    config.spoolDirectory = SpoolDirectory(arena, s_Group);
    ASSERT_TRUE(NWB::Core::Crash::InstallCrashHandler(config));
    NWB::Core::Crash::UninstallCrashHandler();
    stack_t restoredStack = {};
    struct sigaction restoredAction = {};
    ASSERT_EQ(sigaltstack(nullptr, &restoredStack), 0);
    ASSERT_EQ(sigaction(SIGILL, nullptr, &restoredAction), 0);
    EXPECT_NE(restoredStack.ss_flags & SS_DISABLE, 0);
    EXPECT_EQ(restoredAction.sa_handler, OriginalSignalOwner);
    EXPECT_EQ(QueryTerminateHandler(), OriginalTerminateOwner);
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::g_State.platformRestorePending);
    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, UninstallPreservesNewerForeignNativeHookOwners){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_preserve_foreign_hooks");
    RemoveTestArtifacts(arena, s_Group);
    NWB::Core::Crash::UninstallCrashHandler();
    stack_t originalStack = {};
    struct sigaction originalAction = {};
    ASSERT_EQ(sigaltstack(nullptr, &originalStack), 0);
    ASSERT_EQ(sigaction(SIGILL, nullptr, &originalAction), 0);
    const auto originalTerminate = QueryTerminateHandler();
    ScopeExit restore([&]()noexcept{
        NWB::Core::Crash::UninstallCrashHandler();
        if(sigaction(SIGILL, &originalAction, nullptr) != 0 || sigaltstack(&originalStack, nullptr) != 0)
            TerminateInvariant();
        ExchangeTerminateHandler(originalTerminate);
    }
    );
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    NWB::Core::Crash::CrashConfig config(installArena);
    config.applicationName = "preserve-foreign-hooks";
    config.spoolDirectory = SpoolDirectory(arena, s_Group);
    ASSERT_TRUE(NWB::Core::Crash::InstallCrashHandler(config));
    struct sigaction foreignAction = {};
    foreignAction.sa_handler = ForeignSignalOwner;
    ASSERT_EQ(sigemptyset(&foreignAction.sa_mask), 0);
    ASSERT_EQ(sigaction(SIGILL, &foreignAction, nullptr), 0);
    stack_t foreignStack = {};
    foreignStack.ss_sp = g_ForeignSignalStack;
    foreignStack.ss_size = sizeof(g_ForeignSignalStack);
    ASSERT_EQ(sigaltstack(&foreignStack, nullptr), 0);
    ExchangeTerminateHandler(ForeignTerminateOwner);
    NWB::Core::Crash::UninstallCrashHandler();
    stack_t retainedStack = {};
    struct sigaction retainedAction = {};
    ASSERT_EQ(sigaltstack(nullptr, &retainedStack), 0);
    ASSERT_EQ(sigaction(SIGILL, nullptr, &retainedAction), 0);
    EXPECT_EQ(retainedStack.ss_sp, g_ForeignSignalStack);
    EXPECT_EQ(retainedAction.sa_handler, ForeignSignalOwner);
    EXPECT_EQ(QueryTerminateHandler(), ForeignTerminateOwner);
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::g_State.platformRestorePending);
    RemoveTestArtifacts(arena, s_Group);
}
#endif

TEST(Crash, WriteCrashPackageFailsWhenSpoolPathIsFile){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_package_bad_spool_test");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(EnsureDirectories(TestCaseDirectory(arena, s_Group)));
    const CrashTestPath spoolPath = SpoolDirectory(arena, s_Group);
    EXPECT_TRUE(WriteTextFile(spoolPath, AStringView("not a directory")));

    NWB::Core::Crash::CrashDetail::CrashRequest request;
    FillPackageRequest(arena, request, spoolPath, AStringView("crash-package-bad-spool"));
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::WriteCrashPackage(request));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, CrashSpoolRetentionPrunesOldestPackages){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_test");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash003));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "bad package name"));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_Crash001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_Crash002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_Crash001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_Crash002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_Crash001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_Crash002));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 2u;
    retention.maxUploadedPackages = 1u;
    retention.maxFailedPackages = 1u;
    retention.maxUploadingPackages = 1u;
    NWB::Core::Alloc::ScratchArena scratchArena(s_OperationScratch);
    EXPECT_TRUE(NWB::Core::Crash::CrashDetail::ApplyCrashSpoolRetention(scratchArena, SpoolDirectory(arena, s_Group), retention));

    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash001));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash003));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "bad package name"));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_Crash001));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_Crash002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_Crash001));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_Crash002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_Crash001));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_Crash002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, CrashSpoolRetentionZeroDisablesPruning){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_zero_test");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash002));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 0u;
    retention.maxUploadedPackages = 0u;
    retention.maxFailedPackages = 0u;
    retention.maxUploadingPackages = 0u;
    NWB::Core::Alloc::ScratchArena scratchArena(s_OperationScratch);
    EXPECT_TRUE(NWB::Core::Crash::CrashDetail::ApplyCrashSpoolRetention(scratchArena, SpoolDirectory(arena, s_Group), retention));

    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash001));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, CrashSpoolRetentionProtectsActivePendingPackage){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_spool_retention_protect_test");
    RemoveTestArtifacts(arena, s_Group);

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash003));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 1u;
    retention.maxUploadedPackages = 0u;
    retention.maxFailedPackages = 0u;
    retention.maxUploadingPackages = 0u;
    NWB::Core::Alloc::ScratchArena scratchArena(s_OperationScratch);
    EXPECT_TRUE(NWB::Core::Crash::CrashDetail::ApplyCrashSpoolRetention(scratchArena, SpoolDirectory(arena, s_Group), retention, AStringView("crash-001")));

    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash001));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash002));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_Crash003));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

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

    NWB::Core::Crash::CrashDetail::CrashUploadSnapshot snapshot;
    CopyPathText(arena, snapshot.spoolDirectory, SpoolDirectory(arena, s_Group));
    CopyFixedBuffer(snapshot.logServerUrl, AStringView("http://127.0.0.1:1"));

    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::FlushPendingCrashReportsImpl(arena, snapshot));
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CrashId));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsRegularFile(BucketDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    EXPECT_TRUE(TextFileContains(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CrashId) / CrashNames::s_UploadAttemptFileName, AStringView(CrashNames::s_UploadAttemptRetryInterruptedState)));

    RemoveTestArtifacts(arena, s_Group);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

