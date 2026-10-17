// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/filesystem_helpers.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/crash/package_internal.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/directory_iterator.h>
#include <global/thread.h>
#include <global/diagnostics.h>
#include <global/exception.h>
#include <global/scope_exit.h>
#include <global/termination.h>

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
using NWB::Tests::TextFileContains;
using NWB::Tests::WaitForDirectory;
namespace CrashNames = NWB::Core::Crash::PackageNames;

inline constexpr Name s_OperationScratch("tests/integration/crash/operation_scratch");
inline constexpr Name s_InstallArena("tests/integration/crash/install");
inline constexpr Name s_SignalChildInstallArena("tests/integration/crash/signal_child_install");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static CrashTestPath CrashRootDirectory(NWB::Core::Alloc::GlobalArena& arena){
    const auto executableDirectory = GetExecutableDirectory(arena);
    if(executableDirectory)
        return *executableDirectory / CrashNames::s_DefaultRootDirectoryName;

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

[[nodiscard]] static CrashTestPath PackageDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView bucketName, const AStringView packageName){
    return BucketDirectory(arena, testGroup, bucketName) / packageName;
}

static void RemoveTestArtifacts(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    EXPECT_TRUE(RemoveAllIfExists(TestCaseDirectory(arena, testGroup)));
}

[[nodiscard]] static bool CreatePackageDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView bucketName, const AStringView packageName){
    const CrashTestPath packageDirectory = PackageDirectory(arena, testGroup, bucketName, packageName);
    if(!EnsureDirectories(packageDirectory))
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

static void FillPackageRequest(NWB::Core::Alloc::GlobalArena& arena, NWB::Core::Crash::Detail::CrashRequest& request, const CrashTestPath& spoolDirectory, const AStringView crashId){
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


#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
static u32 g_NativeLifecycleDiagnostics = 0u;
static u32 g_UnlockedLifecycleDiagnostics = 0u;
static char g_LastLifecycleDiagnostic[192u] = {};
static char g_LastLifecycleOperation[32u] = {};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
static void ObserveNativeLifecycle(const DiagnosticEventRecord& record)noexcept{
    if(record.category != AStringView("crash_native_lifecycle"))
        return;
    ++g_NativeLifecycleDiagnostics;
    CopyFixedBuffer(g_LastLifecycleDiagnostic, record.message);
    CopyFixedBuffer(g_LastLifecycleOperation, record.expression);
    if(NWB::Core::Crash::Detail::g_State.mutex.try_lock()){
        ++g_UnlockedLifecycleDiagnostics;
        NWB::Core::Crash::Detail::g_State.mutex.unlock();
    }
}

TEST(Crash, SpoolAdmissionPreservesFilesystemCategoryAndCodeAfterUnlock){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("crash_failed_spool_admission");
    RemoveTestArtifacts(arena, s_Group);
    ASSERT_TRUE(EnsureDirectories(TestCaseDirectory(arena, s_Group)));
    const CrashTestPath blocked = TestCaseDirectory(arena, s_Group) / "blocked";
    ASSERT_TRUE(WriteTextFile(blocked, AStringView("This file cannot be a parent directory.\n")));
    const CrashTestPath spoolDirectory = blocked / "spool";
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
    g_NativeLifecycleDiagnostics = 0u;
    g_UnlockedLifecycleDiagnostics = 0u;
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    NWB::Core::Crash::CrashConfig config(installArena);
    config.applicationName = "bad-spool-admission";
    config.spoolDirectory = spoolDirectory;
    EXPECT_FALSE(NWB::Core::Crash::InstallCrashHandler(config));
    EXPECT_FALSE(NWB::Core::Crash::Detail::g_State.installed);
    EXPECT_FALSE(NWB::Core::Crash::Detail::g_State.handlerStarted);
    EXPECT_FALSE(NWB::Core::Crash::Detail::g_State.platformRestorePending);
    EXPECT_EQ(g_NativeLifecycleDiagnostics, 1u);
    EXPECT_EQ(g_UnlockedLifecycleDiagnostics, 1u);
    EXPECT_EQ(AStringView(g_LastLifecycleOperation), AStringView("prepare_spool"));
    EXPECT_NE(AStringView(g_LastLifecycleDiagnostic).find(AStringView(expectedCategory)), AStringView::npos);
    EXPECT_NE(AStringView(g_LastLifecycleDiagnostic).find(AStringView(expectedCode)), AStringView::npos);
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
    g_NativeLifecycleDiagnostics = 0u;
    g_UnlockedLifecycleDiagnostics = 0u;
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
    const auto before = DirectoryIterator<NWB::Core::Alloc::GlobalArena>::Create(CrashTestPath(arena, "/proc/self/fd"));
    ASSERT_TRUE(before);
    const auto beforeCount = static_cast<usize>(before->end() - before->begin());
#endif
    for(u32 attempt = 0u; attempt < 4u; ++attempt){
        EXPECT_FALSE(NWB::Core::Crash::InstallCrashHandler(config));
        EXPECT_FALSE(NWB::Core::Crash::Detail::g_State.installed);
        EXPECT_FALSE(NWB::Core::Crash::Detail::g_State.handlerStarted);
        EXPECT_FALSE(NWB::Core::Crash::Detail::g_State.platformRestorePending);
    }
#if defined(NWB_PLATFORM_WINDOWS)
    DWORD after = 0u;
    ASSERT_TRUE(GetProcessHandleCount(GetCurrentProcess(), &after));
    EXPECT_EQ(after, before);
#else
    const auto after = DirectoryIterator<NWB::Core::Alloc::GlobalArena>::Create(CrashTestPath(arena, "/proc/self/fd"));
    ASSERT_TRUE(after);
    EXPECT_EQ(static_cast<usize>(after->end() - after->begin()), beforeCount);
#endif
    EXPECT_EQ(g_NativeLifecycleDiagnostics, 5u);
    EXPECT_EQ(g_UnlockedLifecycleDiagnostics, g_NativeLifecycleDiagnostics);
    EXPECT_NE(AStringView(g_LastLifecycleDiagnostic).find("code="), AStringView::npos);
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
    EXPECT_FALSE(NWB::Core::Crash::Detail::g_State.platformRestorePending);
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
    EXPECT_FALSE(NWB::Core::Crash::Detail::g_State.platformRestorePending);
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
    NWB::Core::Alloc::ScratchArena scratchArena(s_OperationScratch);
    EXPECT_TRUE(NWB::Core::Crash::Detail::ApplyCrashSpoolRetention(scratchArena, SpoolDirectory(arena, s_Group), retention));

    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_001));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_003));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, "bad package name"));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_CRASH_001));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadedDirectoryName, s_CRASH_002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_CRASH_001));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_FailedDirectoryName, s_CRASH_002));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CRASH_001));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_UploadingDirectoryName, s_CRASH_002));
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

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_002));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 0u;
    retention.maxUploadedPackages = 0u;
    retention.maxFailedPackages = 0u;
    retention.maxUploadingPackages = 0u;
    NWB::Core::Alloc::ScratchArena scratchArena(s_OperationScratch);
    EXPECT_TRUE(NWB::Core::Crash::Detail::ApplyCrashSpoolRetention(scratchArena, SpoolDirectory(arena, s_Group), retention));

    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_001));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_002));
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

    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_001));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_002));
    EXPECT_TRUE(CreatePackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_003));

    NWB::Core::Crash::CrashSpoolRetentionConfig retention;
    retention.maxPendingPackages = 1u;
    retention.maxUploadedPackages = 0u;
    retention.maxFailedPackages = 0u;
    retention.maxUploadingPackages = 0u;
    NWB::Core::Alloc::ScratchArena scratchArena(s_OperationScratch);
    EXPECT_TRUE(NWB::Core::Crash::Detail::ApplyCrashSpoolRetention(scratchArena, SpoolDirectory(arena, s_Group), retention, AStringView("crash-001")));

    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_001));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_002));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, s_CRASH_003));
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

    NWB::Core::Crash::Detail::CrashUploadSnapshot snapshot;
    CopyPathText(arena, snapshot.spoolDirectory, SpoolDirectory(arena, s_Group));
    CopyFixedBuffer(snapshot.logServerUrl, AStringView("http://127.0.0.1:1"));

    EXPECT_FALSE(NWB::Core::Crash::Detail::FlushPendingCrashReportsImpl(arena, snapshot));
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

#if defined(NWB_PLATFORM_WINDOWS)
TEST(Crash, ManualContextTimeoutRetainsOwnedDumpAndReusableHandlerAfterSourceStorageExpires){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::ScratchArena scratchArena(s_OperationScratch);
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    constexpr AStringView s_Group("crash_manual_owned_context_test");
    RemoveTestArtifacts(arena, s_Group);
    NWB::Core::Crash::CrashConfig config(installArena);
    config.applicationName = AStringView(s_CRASH_TESTS);
    config.spoolDirectory = SpoolDirectory(arena, s_Group);
    ASSERT_TRUE(NWB::Core::Crash::InstallCrashHandler(config));
    ScopeExit uninstall([]()noexcept{ NWB::Core::Crash::UninstallCrashHandler(); });

    NWB::Core::Crash::Detail::CrashRequest request;
    u32 capturedContextFlags = 0u;
    u64 capturedInstructionPointer = 0u;
    {
        NWB::Core::Crash::Detail::ManualDumpContextStorage storage;
        NWB::Core::Crash::Detail::CrashDumpRequestOptions options;
        NWB::Core::Crash::Detail::CaptureManualDumpContext(options, storage);
        capturedContextFlags = storage.context.ContextFlags & ~NWB::Core::Crash::Detail::s_WindowsExtendedContextFlag;
        capturedInstructionPointer = options.instructionPointer;
        auto sourceRequest = NWB::Core::Crash::Detail::BuildCrashDumpRequest(NWB::Core::Crash::Detail::CrashReasonKind::ManualDump, 0u, options);
        NWB_MEMCPY(&request, sizeof(request), &sourceRequest, sizeof(sourceRequest));
        sourceRequest = NWB::Core::Crash::Detail::CrashRequest{};
        storage.context = CONTEXT{};
        storage.exceptionRecord = EXCEPTION_RECORD{};
        storage.exceptionPointers = EXCEPTION_POINTERS{};
    }
    ASSERT_TRUE(NWB::Core::Crash::Detail::IsCurrentCrashRequest(request));
    EXPECT_EQ(request.exceptionPointers, 0u);
    EXPECT_EQ(request.manualContext.ContextFlags, capturedContextFlags);
    EXPECT_NE(capturedContextFlags, 0u);
    NWB::Core::Crash::Detail::ManualDumpContextStorage handlerStorage(request);
    EXPECT_EQ(handlerStorage.exceptionPointers.ContextRecord, &handlerStorage.context);
    EXPECT_EQ(handlerStorage.exceptionPointers.ExceptionRecord, &handlerStorage.exceptionRecord);
    EXPECT_EQ(handlerStorage.exceptionRecord.ExceptionRecord, nullptr);
    EXPECT_EQ(handlerStorage.exceptionRecord.ExceptionAddress, reinterpret_cast<void*>(static_cast<usize>(capturedInstructionPointer)));

    const HANDLE handlerThread = NWB::Core::Crash::Detail::g_State.handlerProcessInfo.hThread;
    ASSERT_NE(handlerThread, nullptr);
    const DWORD previousSuspendCount = SuspendThread(handlerThread);
    ASSERT_NE(previousSuspendCount, static_cast<DWORD>(-1));
    ScopeExit resume([handlerThread]()noexcept{
        if(ResumeThread(handlerThread) == static_cast<DWORD>(-1))
            TerminateInvariant();
    }
    );
    ASSERT_EQ(previousSuspendCount, 0u);
    const auto transport = NWB::Core::Crash::Detail::RequestCrashHandler(request, 1u);
    EXPECT_EQ(transport, NWB::Core::Crash::Detail::CrashDumpTransportStatus::TimedOut);
    ASSERT_NE(ResumeThread(handlerThread), static_cast<DWORD>(-1));
    resume.release();
    ASSERT_EQ(WaitForSingleObject(NWB::Core::Crash::Detail::g_State.crashHandledEvent, NWB::Core::Crash::Detail::s_ManualCrashDumpWaitMilliseconds), WAIT_OBJECT_0);

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(request.crashId));
    EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_SymbolicationFileName, AStringView("minidump captured;")));
    EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_ManifestFileName, AStringView("\"has_exception_context\": true")));
    Vector<u8, NWB::Core::Alloc::ScratchArena> dumpBytes(scratchArena);
    const auto read = ReadBinaryFile(packageDirectory / CrashNames::s_ProcessDumpFileName, dumpBytes);
    ASSERT_TRUE(read);
    MINIDUMP_DIRECTORY* directory = nullptr;
    void* streamData = nullptr;
    ULONG streamSize = 0u;
    ASSERT_TRUE(MiniDumpReadDumpStream(dumpBytes.data(), ExceptionStream, &directory, &streamData, &streamSize));
    ASSERT_NE(streamData, nullptr);
    ASSERT_GE(streamSize, sizeof(MINIDUMP_EXCEPTION_STREAM));
    const usize dumpBegin = reinterpret_cast<usize>(dumpBytes.data());
    const usize streamAddress = reinterpret_cast<usize>(streamData);
    ASSERT_GE(streamAddress, dumpBegin);
    const usize streamOffset = streamAddress - dumpBegin;
    ASSERT_LE(streamOffset, dumpBytes.size());
    ASSERT_LE(sizeof(MINIDUMP_EXCEPTION_STREAM), dumpBytes.size() - streamOffset);
    MINIDUMP_EXCEPTION_STREAM exceptionStream = {};
    NWB_MEMCPY(&exceptionStream, sizeof(exceptionStream), dumpBytes.data() + streamOffset, sizeof(exceptionStream));
    EXPECT_EQ(exceptionStream.ThreadId, request.threadId);
    EXPECT_EQ(exceptionStream.ExceptionRecord.ExceptionCode, NWB::Core::Crash::Detail::s_ManualDumpExceptionCode);
    EXPECT_EQ(exceptionStream.ExceptionRecord.ExceptionAddress, capturedInstructionPointer);
    ASSERT_GE(exceptionStream.ThreadContext.DataSize, sizeof(CONTEXT));
    ASSERT_LE(static_cast<u64>(exceptionStream.ThreadContext.Rva) + sizeof(CONTEXT), dumpBytes.size());
    CONTEXT dumpedContext = {};
    NWB_MEMCPY(&dumpedContext, sizeof(dumpedContext), dumpBytes.data() + exceptionStream.ThreadContext.Rva, sizeof(dumpedContext));
#if defined(_M_X64) || defined(__x86_64__)
    EXPECT_EQ(dumpedContext.Rip, capturedInstructionPointer);
#elif defined(_M_IX86) || defined(__i386__)
    EXPECT_EQ(dumpedContext.Eip, capturedInstructionPointer);
#elif defined(_M_ARM64) || defined(__aarch64__)
    EXPECT_EQ(dumpedContext.Pc, capturedInstructionPointer);
#endif
    EXPECT_TRUE(NWB::Core::Crash::CaptureCrashDump(AStringView("after_timeout"), AStringView("handler remains reusable")).packageWritten());
    NWB::Core::Crash::UninstallCrashHandler();
    uninstall.release();
    RemoveTestArtifacts(arena, s_Group);
}

TEST(Crash, ManualNativeContextAdmissionRejectsWrongVersionSizeArchitectureAndLinkedRecords){
    NWB::Core::Crash::Detail::ManualDumpContextStorage storage;
    NWB::Core::Crash::Detail::CrashDumpRequestOptions options;
    NWB::Core::Crash::Detail::CaptureManualDumpContext(options, storage);
    auto request = NWB::Core::Crash::Detail::BuildCrashDumpRequest(NWB::Core::Crash::Detail::CrashReasonKind::ManualDump, 0u, options);
    ASSERT_TRUE(NWB::Core::Crash::Detail::IsCurrentCrashRequest(request));
    request.version = 1u;
    EXPECT_FALSE(NWB::Core::Crash::Detail::IsCurrentCrashRequest(request));
    request.version = NWB::Core::Crash::Detail::s_RequestVersion + 1u;
    EXPECT_FALSE(NWB::Core::Crash::Detail::IsCurrentCrashRequest(request));
    request.version = NWB::Core::Crash::Detail::s_RequestVersion;
    --request.manualContextSize;
    EXPECT_FALSE(NWB::Core::Crash::Detail::IsCurrentCrashRequest(request));
    request.manualContextSize = sizeof(CONTEXT);
    request.manualContextArchitecture ^= 0x00010000u;
    EXPECT_FALSE(NWB::Core::Crash::Detail::IsCurrentCrashRequest(request));
    request.manualContextArchitecture = NWB::Core::Crash::Detail::s_WindowsContextArchitecture;
    request.manualExceptionRecord.ExceptionRecord = &storage.exceptionRecord;
    EXPECT_FALSE(NWB::Core::Crash::Detail::IsCurrentCrashRequest(request));
    request.manualExceptionRecord.ExceptionRecord = nullptr;
    request.manualContext.ContextFlags |= NWB::Core::Crash::Detail::s_WindowsExtendedContextFlag;
    EXPECT_FALSE(NWB::Core::Crash::Detail::IsCurrentCrashRequest(request));
}

TEST(Crash, DesktopHandlerDoesNotRetainUnrelatedInheritableHandles){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    constexpr AStringView s_Group("crash_handler_handle_inheritance_test");
    RemoveTestArtifacts(arena, s_Group);

    const CrashTestPath testDirectory = TestCaseDirectory(arena, s_Group);
    ASSERT_TRUE(EnsureDirectories(testDirectory));

    const CrashTestPath sentinelPath = testDirectory / "inheritable_handle_sentinel.tmp";
    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;
    const HANDLE sentinelHandle = CreateFileW(sentinelPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &securityAttributes, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    ASSERT_NE(sentinelHandle, INVALID_HANDLE_VALUE);

    NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
    config.applicationName = AStringView(s_CRASH_TESTS);
    config.version = AStringView("1");
    config.buildId = AStringView("handle-inheritance-test");
    config.spoolDirectory = SpoolDirectory(arena, s_Group);

    const bool installed = NWB::Core::Crash::InstallCrashHandler(config);
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
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
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

    const bool installed = NWB::Core::Crash::InstallCrashHandler(config);
    ASSERT_TRUE(installed);

    NWB::Core::Crash::Detail::CrashDumpRequestOptions options;
    options.waitMilliseconds = NWB::Core::Crash::Detail::s_PlatformCrashHandlerWaitMilliseconds;
    options.triggerCategory = AStringView(s_TEST);
    options.triggerMessage = AStringView("desktop handler runtime");
    options.triggerFile = AStringView("tests/integration/crash/crash_package_tests.cpp");
    NWB::Core::Crash::Detail::ManualDumpContextStorage contextStorage;
    NWB::Core::Crash::Detail::CaptureManualDumpContext(options, contextStorage);
    const NWB::Core::Crash::CrashDumpResult result = NWB::Core::Crash::Detail::RequestCrashDump(NWB::Core::Crash::Detail::CrashReasonKind::ManualDump, 0u, options);
    EXPECT_EQ(result.status, NWB::Core::Crash::CrashDumpStatus::PackageWritten);

    char crashId[NWB::Core::Crash::Detail::s_MaxShortText] = {};
    BuildCrashIdForProcess(crashId, CurrentProcessId(), 1u);

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(crashId));
    {
        const auto directoryReady = WaitForDirectory(packageDirectory, 3000u);
        ASSERT_TRUE(directoryReady);
        EXPECT_TRUE(*directoryReady);
    }
    {
        const auto pathQuery = IsRegularFile(packageDirectory / CrashNames::s_CallstackFileName);
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    // A "#1 " frame proves the unwinder walked past the leaf frame; final builds omit the frame pointer, so this guards that .eh_frame-based capture keeps producing a full callstack.
    EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_CallstackFileName, AStringView("#1 0x")));
    NWB::Core::Crash::UninstallCrashHandler();
    RemoveTestArtifacts(arena, s_Group);
}
#endif


TEST(Crash, DesktopInstalledHandlerWritesGpuCrashTextOnlyPackage){
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(s_InstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
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

    const bool installed = NWB::Core::Crash::InstallCrashHandler(config);
    ASSERT_TRUE(installed);

    NWB::Core::Crash::Detail::CrashDumpRequestOptions options;
    options.waitMilliseconds = NWB::Core::Crash::Detail::s_PlatformCrashHandlerWaitMilliseconds;
    options.triggerCategory = AStringView(NWB::Core::Crash::Detail::s_GpuCrashCategory);
    options.gpuReport = AStringView("minimal GPU crash report: no vendor GPU dump or device-fault details were available\n");
    options.gpuDumpKind = NWB::Core::Crash::GpuCrashDumpKind::None;
    NWB::Core::Crash::Detail::ManualDumpContextStorage contextStorage;
    NWB::Core::Crash::Detail::CaptureManualDumpContext(options, contextStorage);
    const NWB::Core::Crash::CrashDumpResult result = NWB::Core::Crash::Detail::RequestCrashDump(NWB::Core::Crash::Detail::CrashReasonKind::GpuCrash, 0u, options);
    EXPECT_EQ(result.status, NWB::Core::Crash::CrashDumpStatus::PackageWritten);

    char crashId[NWB::Core::Crash::Detail::s_MaxShortText] = {};
    BuildCrashIdForProcess(crashId, CurrentProcessId(), 1u);

    const CrashTestPath packageDirectory = PackageDirectory(arena, s_Group, CrashNames::s_PendingDirectoryName, AStringView(crashId));
    {
        const auto directoryReady = WaitForDirectory(packageDirectory, 3000u);
        ASSERT_TRUE(directoryReady);
        EXPECT_TRUE(*directoryReady);
    }
    {
        const auto pathQuery = IsRegularFile(packageDirectory / CrashNames::s_GpuCrashReportFileName);
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(packageDirectory / CrashNames::s_GpuDetectiveCaptureFileName);
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(packageDirectory / CrashNames::s_AftermathGpuDumpFileName);
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
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
        NWB::Core::Alloc::PersistentArena installArena(s_SignalChildInstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
        NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
        config.applicationName = AStringView(s_CRASH_TESTS);
        config.version = AStringView("1");
        config.buildId = AStringView("linux-signal-runtime-test");
        config.spoolDirectory = spoolDirectory;

        {
            ScopedLock lock(NWB::Core::Crash::Detail::g_State.mutex);
            NWB::Core::Crash::Detail::g_State.crashSequence.store(1u, MemoryOrder::relaxed);
        }

        if(!NWB::Core::Crash::InstallCrashHandler(config))
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
        {
            const auto directoryReady = WaitForDirectory(packageDirectory, 1000u);
            ASSERT_TRUE(directoryReady);
            EXPECT_TRUE(*directoryReady);
        }
        {
            const auto pathQuery = IsRegularFile(packageDirectory / CrashNames::s_ManifestFileName);
            ASSERT_TRUE(pathQuery);
            EXPECT_TRUE(*pathQuery);
        }
        {
            const auto pathQuery = IsRegularFile(packageDirectory / CrashNames::s_CallstackFileName);
            ASSERT_TRUE(pathQuery);
            EXPECT_TRUE(*pathQuery);
        }
        // The faulting callstack must reach past the leaf frame even with the frame pointer omitted (final builds).
        EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_CallstackFileName, AStringView("#1 0x")));
        {
            const auto pathQuery = IsRegularFile(packageDirectory / CrashNames::s_ProcMapsFileName);
            ASSERT_TRUE(pathQuery);
            EXPECT_TRUE(*pathQuery);
        }
        EXPECT_TRUE(TextFileContains(packageDirectory / CrashNames::s_ManifestFileName, AStringView("\"reason_kind\": \"signal\"")));
    }

    RemoveTestArtifacts(arena, s_Group);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

