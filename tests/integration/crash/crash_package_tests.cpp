// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_support.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <dbghelp.h>
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_package_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace CrashTestSupport;
using namespace NWB::Tests;
using CrashTestSupport::TestArena;
using namespace NWB::Tests::CrashTestPaths;
namespace CrashNames = NWB::Core::Crash::PackageNames;


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
    Array<char, 64u> expectedCategory = {};
    CopyFixedBuffer(expectedCategory, "category=");
    AppendFixedBuffer(expectedCategory, AStringView(expectedFailure.error().category().name()));
    Array<char, 32u> expectedCode = {};
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
    EXPECT_EQ(FixedBufferView(s_LastLifecycleOperation), AStringView("prepare_spool"));
    EXPECT_NE(FixedBufferView(s_LastLifecycleDiagnostic).find(FixedBufferView(expectedCategory)), AStringView::npos);
    EXPECT_NE(FixedBufferView(s_LastLifecycleDiagnostic).find(FixedBufferView(expectedCode)), AStringView::npos);
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
    EXPECT_NE(FixedBufferView(s_LastLifecycleDiagnostic).find("code="), AStringView::npos);
    NWB::Core::Crash::UninstallCrashHandler();
    RemoveTestArtifacts(arena, s_Group);
}
#endif

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
alignas(MaxAlign) static Array<u8, 256u * 1024u> g_ForeignSignalStack = {};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void OriginalSignalOwner(const int)noexcept{}

static void ForeignSignalOwner(const int)noexcept{}

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
    });
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
    });
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
    foreignStack.ss_sp = g_ForeignSignalStack.data();
    foreignStack.ss_size = g_ForeignSignalStack.size();
    ASSERT_EQ(sigaltstack(&foreignStack, nullptr), 0);
    ExchangeTerminateHandler(ForeignTerminateOwner);
    NWB::Core::Crash::UninstallCrashHandler();
    stack_t retainedStack = {};
    struct sigaction retainedAction = {};
    ASSERT_EQ(sigaltstack(nullptr, &retainedStack), 0);
    ASSERT_EQ(sigaction(SIGILL, nullptr, &retainedAction), 0);
    EXPECT_EQ(retainedStack.ss_sp, g_ForeignSignalStack.data());
    EXPECT_EQ(retainedAction.sa_handler, ForeignSignalOwner);
    EXPECT_EQ(QueryTerminateHandler(), ForeignTerminateOwner);
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::g_State.platformRestorePending);
    RemoveTestArtifacts(arena, s_Group);
}
#endif




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

