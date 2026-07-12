
#include "crash_test_helpers.h"

#include <global/filesystem/utility.h>
#include <tests/test_context.h>

#include <gtest/gtest.h>

#include <global/core/crash/module.h>
#include <global/core/crash/package_names.h>
#include <global/core/common/log.h>
#include <global/assert.h>
#include <global/environment.h>
#include <global/filesystem/directory_iterator.h>
#include <global/filesystem/operations.h>
#include <global/process_execution.h>
#include <logger/server/crash_auth.h>
#include <logger/server/crash_ingest.h>
#include <logger/server/crash_paths.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <dlfcn.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_server_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
using TestArena = NWB::Tests::TestArena<struct LoggerServerCrashTestsTag>;
using ::WaitForDirectory;
using namespace NWB::Tests::LoggerServerCrash;
namespace CrashNames = NWB::Core::Crash::PackageNames;
inline constexpr AStringView s_InvalidArchiveHeader("NWBCRASHPKG 0\n");
inline constexpr Name s_AssertChildInstallArena("tests/logger_server/assert_child_install");
inline constexpr Name s_RecoverableErrorInstallArena("tests/logger_server/recoverable_error_install");
#if defined(_MSC_VER)
#define NWB_LOGSERVER_TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define NWB_LOGSERVER_TEST_NOINLINE [[gnu::noinline]]
#else
#define NWB_LOGSERVER_TEST_NOINLINE
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
NWB_LOGSERVER_TEST_NOINLINE static u64 LinuxCrashSymbolicationProbe(){
    return 0x4E57424352415348ull;
}

static void AppendDecimalText(CrashTestText& outText, const u64 value){
    char buffer[32] = {};
    outText += FormatDecimal(static_cast<usize>(value), buffer);
}

static void AppendHexAddressText(NWB::Core::Alloc::GlobalArena& arena, CrashTestText& outText, const u64 value){
    outText += "0x";
    outText += FormatHex64A(arena, value);
}

[[nodiscard]] static bool BuildSelfProcMapLineForAddress(
    NWB::Core::Alloc::GlobalArena& arena,
    const u64 address,
    CrashTestText& outMapLine,
    u64& outSymbolicationOffset
){
    outSymbolicationOffset = 0u;

    Dl_info info = {};
    if(dladdr(reinterpret_cast<const void*>(static_cast<usize>(address)), &info) == 0 || !info.dli_fname || !info.dli_fbase)
        return false;

    const u64 imageBegin = static_cast<u64>(reinterpret_cast<usize>(info.dli_fbase));
    if(address < imageBegin)
        return false;

    constexpr u64 s_FrameOffsetWithinSyntheticMap = 0x20u;
    outSymbolicationOffset = address - imageBegin;
    if(outSymbolicationOffset < s_FrameOffsetWithinSyntheticMap)
        return false;

    const u64 mapBegin = address - s_FrameOffsetWithinSyntheticMap;
    const u64 mapFileOffset = outSymbolicationOffset - s_FrameOffsetWithinSyntheticMap;

    outMapLine.clear();
    outMapLine += FormatHex64A(arena, mapBegin);
    outMapLine += "-";
    outMapLine += FormatHex64A(arena, address + 1u);
    outMapLine += " r-xp ";
    outMapLine += FormatHex64A(arena, mapFileOffset);
    outMapLine += " 00:00 0 ";
    outMapLine += info.dli_fname;
    outMapLine += "\n";
    return true;
}

[[nodiscard]] static bool LinuxExternalSymbolizerAvailable(NWB::Core::Alloc::GlobalArena& arena){
    CrashTestText pathText(arena);
    if(!ReadEnvironmentVariable("PATH", pathText) || pathText.empty())
        return false;

    const AStringView searchPath(pathText.data(), pathText.size());
    return ExecutableAvailableInPath(arena, searchPath, "llvm-symbolizer")
        || ExecutableAvailableInPath(arena, searchPath, "addr2line")
    ;
}

static void LinuxSilenceExpectedCrashChildConsole(){
    const int nullFd = open("/dev/null", O_WRONLY);
    if(nullFd < 0)
        return;

    if(dup2(nullFd, STDOUT_FILENO) < 0){
        close(nullFd);
        return;
    }
    if(dup2(nullFd, STDERR_FILENO) < 0){
        close(nullFd);
        return;
    }
    if(nullFd > STDERR_FILENO)
        close(nullFd);
}

[[nodiscard]] static bool PendingDirectoryContainsManifestTexts(
    NWB::Core::Alloc::GlobalArena& arena,
    const CrashTestPath& pendingDirectory,
    const AStringView firstNeedle,
    const AStringView secondNeedle
){
    ErrorCode error;
    DirectoryIterator directory(pendingDirectory, error);
    if(error)
        return false;

    for(const auto& entry : directory){
        ErrorCode entryError;
        if(!IsDirectory(entry.path(), entryError) || entryError)
            continue;

        CrashTestText manifest(arena);
        if(!ReadTextFile(entry.path() / CrashNames::s_ManifestFileName, manifest))
            continue;
        if(Contains(manifest, firstNeedle) && Contains(manifest, secondNeedle))
            return true;
    }

    return false;
}

[[nodiscard]] static const char* LinuxObservableAssertCategory(){
#if NWB_OCCUR_ASSERT
    return DiagnosticEventCategory::s_Assert;
#else
    return DiagnosticEventCategory::s_FatalAssert;
#endif
}

NWB_LOGSERVER_TEST_NOINLINE static void LinuxForceAssertFalseForCrashObservation(){
#if NWB_OCCUR_ASSERT
    NWB_ASSERT(false);
#else
    NWB_FATAL_ASSERT(false);
#endif
    _exit(120);
}
#endif

// CaptureRecoverableErrorForCrashObservation is used by RecoverableErrorDiagnosticProducesObservableLoggerReport
// on Windows AND Linux, so it must live OUTSIDE the Linux-only block above (it only uses cross-platform
// CaptureDiagnosticEvent). Guard it with that test's own platform condition so it is not defined-but-unused on
// Android (where the test is compiled out).
#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
NWB_LOGSERVER_TEST_NOINLINE static void CaptureRecoverableErrorForCrashObservation(const AStringView message){
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error,
        .category = NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError,
        .message = message.data(),
        .file = "tests/logger_server/tests.cpp",
        .line = __LINE__,
    });
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB::Log::CrashIngestResult ProcessCrashArchive(
    NWB::Core::Alloc::GlobalArena& arena,
    const AStringView testGroup,
    const AStringView stem,
    const CrashTestText& archive,
    const NWB::Log::CrashIngestConfig& config
){
    EXPECT_TRUE(WriteArchive(arena, testGroup, stem, archive));
    return NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, testGroup, stem), config);
}

static NWB::Log::CrashIngestResult ProcessCrashArchive(
    NWB::Core::Alloc::GlobalArena& arena,
    const AStringView testGroup,
    const AStringView stem,
    const CrashTestText& archive
){
    const NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, testGroup);
    return ProcessCrashArchive(arena, testGroup, stem, archive, config);
}

static NWB::Log::CrashIngestResult ProcessCrashArchiveBytes(
    NWB::Core::Alloc::GlobalArena& arena,
    const AStringView testGroup,
    const AStringView stem,
    const CrashTestBytes& archive,
    const NWB::Log::CrashIngestConfig& config
){
    EXPECT_TRUE(WriteArchiveBytes(arena, testGroup, stem, archive));
    return NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, testGroup, stem), config);
}

static NWB::Log::CrashIngestResult ProcessCrashArchiveBytes(
    NWB::Core::Alloc::GlobalArena& arena,
    const AStringView testGroup,
    const AStringView stem,
    const CrashTestBytes& archive
){
    const NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, testGroup);
    return ProcessCrashArchiveBytes(arena, testGroup, stem, archive, config);
}

[[nodiscard]] static usize FindText(const CrashTestText& text, const AStringView needle){
    return AStringView(text.data(), text.size()).find(needle);
}

[[nodiscard]] static bool TextAppearsBefore(const CrashTestText& text, const AStringView first, const AStringView second){
    const AStringView view(text.data(), text.size());
    const usize firstPosition = view.find(first);
    const usize secondPosition = view.find(second);
    return firstPosition != AStringView::npos && secondPosition != AStringView::npos && firstPosition < secondPosition;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(LoggerServerCrash, LinuxCrashPackageMapsInstructionPointer){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_crash_test");
    constexpr AStringView s_Stem("linux_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BuildLinuxCrashArchive(arena, archive, "linux-test");
    AppendArchiveFile(archive, CrashNames::s_MetadataFileName, "build_channel=qa\ngpu=test-adapter\n");
    AppendArchiveFile(archive, CrashNames::s_BreadcrumbsFileName, "1 [general] entered render loop\n2 [io] opened scene file\n");

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    config.symbolication.symbolStoreDirectory = StorageDirectory(arena, s_Group) / "test_symbols";
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive, config);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Log::Type::EssentialInfo);
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("module_relative_ip=0x0000000000001234")));

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, "platform=linux"));
    EXPECT_TRUE(Contains(report, "[event]"));
    EXPECT_TRUE(Contains(report, "event=crash"));
    EXPECT_TRUE(Contains(report, "exception=SIGSEGV (11)"));
    EXPECT_TRUE(Contains(report, "status=callstack_captured"));
    EXPECT_TRUE(Contains(report, "symbol_store="));
    EXPECT_TRUE(Contains(report, "symbol_store_status=missing"));
    EXPECT_TRUE(Contains(report, "instruction_pointer_module=/tmp/nwb_loader"));
    EXPECT_TRUE(Contains(report, "module_relative_ip=0x0000000000001234"));
    EXPECT_TRUE(Contains(report, "callstack:"));
    EXPECT_TRUE(Contains(report, "#0 0x0000000000401234 /tmp/nwb_loader+0x0000000000001234"));
    EXPECT_TRUE(Contains(report, "#1 0x0000000000401240 /tmp/nwb_loader+0x0000000000001240"));
    EXPECT_TRUE(Contains(report, "core_artifact=missing"));
    // Client-shipped metadata and breadcrumbs must surface in the rendered crash report.
    EXPECT_TRUE(Contains(report, "[metadata]"));
    EXPECT_TRUE(Contains(report, "build_channel=qa"));
    EXPECT_TRUE(Contains(report, "[breadcrumbs]"));
    EXPECT_TRUE(Contains(report, "entered render loop"));
    EXPECT_TRUE(TextAppearsBefore(report, "callstack:", "details:"));
    EXPECT_TRUE(TextAppearsBefore(report, "details:", "[event]"));

    PreserveObservedReport(arena, report, "linux_maps");

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, LinuxCrashPackageSymbolicatesSelfFrame){
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    TestArena testArena;
    auto& arena = testArena.arena;
    if(!LinuxExternalSymbolizerAvailable(arena))
        return;

    constexpr AStringView s_Group("logger_server_linux_symbolized_crash_test");
    constexpr AStringView s_Stem("linux_symbolized_001");
    RemoveTestArtifacts(arena, s_Group);

    const u64 frameAddress = static_cast<u64>(reinterpret_cast<usize>(&LinuxCrashSymbolicationProbe));

    CrashTestText procMapLine(arena);
    u64 expectedSymbolicationOffset = 0u;
    EXPECT_TRUE(BuildSelfProcMapLineForAddress(arena, frameAddress, procMapLine, expectedSymbolicationOffset));

    CrashTestText archive(arena);
    BeginArchiveWithManifest(arena, archive, "linux-symbolized-test", "linux", "crash", "signal", 11u);

    CrashTestText cpuContext(arena);
    cpuContext += "fault_address=0\ninstruction_pointer=";
    AppendDecimalText(cpuContext, frameAddress);
    cpuContext += "\nstack_pointer=0\nframe_pointer=0\n";
    AppendArchiveFile(archive, CrashNames::s_CpuContextFileName, AStringView(cpuContext.data(), cpuContext.size()));

    CrashTestText callstack(arena);
    callstack += "#0 ";
    AppendHexAddressText(arena, callstack, frameAddress);
    callstack += "\n";
    AppendArchiveFile(archive, CrashNames::s_CallstackFileName, AStringView(callstack.data(), callstack.size()));
    AppendArchiveFile(archive, CrashNames::s_ProcMapsFileName, AStringView(procMapLine.data(), procMapLine.size()));

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, "platform=linux"));
    EXPECT_TRUE(Contains(report, "module frames are symbolized with DWARF"));
    EXPECT_TRUE(Contains(report, "LinuxCrashSymbolicationProbe") || Contains(report, "tests/logger_server/tests.cpp"));
    CrashTestText expectedSymbolicationIp(arena);
    expectedSymbolicationIp += "symbolication_relative_ip=";
    AppendHexAddressText(arena, expectedSymbolicationIp, expectedSymbolicationOffset);
    EXPECT_TRUE(Contains(report, AStringView(expectedSymbolicationIp.data(), expectedSymbolicationIp.size())));

    PreserveObservedReport(arena, report, "linux_symbolicated");

    RemoveTestArtifacts(arena, s_Group);
#else
#endif
}

TEST(LoggerServerCrash, LinuxAssertCrashProducesObservableLoggerReport){
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_assert_observe_test");
    constexpr AStringView s_Stem("linux_assert_observe_001");
    RemoveTestArtifacts(arena, s_Group);
    const AStringView expectedAssertCategory(LinuxObservableAssertCategory());

    const CrashTestPath spoolDirectory = SpoolDirectory(arena, s_Group);
    const pid_t childPid = fork();
    ASSERT_GE(childPid, 0);
    if(childPid == 0){
        NWB::Core::Alloc::PersistentArena installArena(
            s_AssertChildInstallArena,
            NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u)
        );
        NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
        config.applicationName = AStringView("logserver_crash_tests");
        config.version = AStringView("1");
        config.buildId = AStringView("linux-assert-observe-test");
        config.spoolDirectory = spoolDirectory;

        if(!NWB::Core::Crash::InstallCrashHandler(installArena, config))
            _exit(121);

        LinuxSilenceExpectedCrashChildConsole();
        LinuxForceAssertFalseForCrashObservation();
        _exit(122);
    }

    int status = 0;
    while(waitpid(childPid, &status, 0) < 0){
        if(errno == EINTR)
            continue;
        break;
    }

    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGABRT);

    const CrashTestPath pendingDirectory = spoolDirectory / CrashNames::s_PendingDirectoryName;
    EXPECT_TRUE(WaitForDirectory(pendingDirectory, 3000u));

    CrashTestText expectedAbortCode(arena);
    expectedAbortCode += "\"reason_code\": ";
    AppendDecimalText(expectedAbortCode, static_cast<u64>(SIGABRT));
    EXPECT_FALSE(PendingDirectoryContainsManifestTexts(
            arena,
            pendingDirectory,
            AStringView("\"reason_kind\": \"signal\""),
            AStringView(expectedAbortCode.data(), expectedAbortCode.size())
        ));

    CrashTestPath assertPackageDirectory(arena);
    EXPECT_TRUE(WaitForTriggerPackage(
            arena,
            pendingDirectory,
            expectedAssertCategory,
            "false",
            AStringView(),
            "tests/logger_server/tests.cpp",
            assertPackageDirectory
        ));

    CrashTestBytes archive(arena);
    EXPECT_TRUE(BuildArchiveFromPackageDirectory(arena, assertPackageDirectory, archive));
    const NWB::Log::CrashIngestResult result = ProcessCrashArchiveBytes(arena, s_Group, s_Stem, archive);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Log::Type::Assert);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, "platform=linux"));
    EXPECT_TRUE(Contains(report, "reason=manual_dump"));
    EXPECT_TRUE(Contains(report, "[event]"));
    EXPECT_TRUE(Contains(report, "event=assert"));
    EXPECT_TRUE(Contains(report, "status=callstack_captured"));
    EXPECT_TRUE(Contains(report, "callstack:"));
    EXPECT_EQ(FindText(report, "false\nat "), 0u);
    EXPECT_TRUE(Contains(report, "tests/logger_server/tests.cpp"));
    if(LinuxExternalSymbolizerAvailable(arena)){
        EXPECT_TRUE(Contains(report, "LinuxForceAssertFalseForCrashObservation"));
    }

    PreserveObservedReport(arena, report, "linux_assert");

    RemoveTestArtifacts(arena, s_Group);
#else
#endif
}

TEST(LoggerServerCrash, RecoverableErrorDiagnosticProducesObservableLoggerReport){
#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(
        s_RecoverableErrorInstallArena,
        NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u)
    );
    constexpr AStringView s_Group("logger_server_recoverable_error_observe_test");
    constexpr AStringView s_Stem("recoverable_error_observe_001");
    constexpr AStringView s_ErrorMessage("recoverable logger error observation");
    RemoveTestArtifacts(arena, s_Group);

    const CrashTestPath spoolDirectory = SpoolDirectory(arena, s_Group);
    NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> crashConfig(installArena);
    crashConfig.applicationName = AStringView("logserver_crash_tests");
    crashConfig.version = AStringView("1");
    crashConfig.buildId = AStringView("recoverable-error-observe-test");
    crashConfig.spoolDirectory = spoolDirectory;

    const bool installed = NWB::Core::Crash::InstallCrashHandler(installArena, crashConfig);
    EXPECT_TRUE(installed);

    bool continuedAfterError = false;
    if(installed){
        CaptureRecoverableErrorForCrashObservation(s_ErrorMessage);
        continuedAfterError = true;
    }

    const CrashTestPath pendingDirectory = spoolDirectory / CrashNames::s_PendingDirectoryName;
    CrashTestPath errorPackageDirectory(arena);
    EXPECT_TRUE(WaitForTriggerPackage(
            arena,
            pendingDirectory,
            NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError,
            AStringView(),
            s_ErrorMessage,
            "tests/logger_server/tests.cpp",
            errorPackageDirectory
        ));
    EXPECT_TRUE(continuedAfterError);
    NWB::Core::Crash::UninstallCrashHandler();

    CrashTestBytes archive(arena);
    EXPECT_TRUE(BuildArchiveFromPackageDirectory(arena, errorPackageDirectory, archive));
    const NWB::Log::CrashIngestResult result = ProcessCrashArchiveBytes(arena, s_Group, s_Stem, archive);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Log::Type::Error);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, "[event]"));
    EXPECT_TRUE(Contains(report, "event=error"));
    EXPECT_TRUE(Contains(report, s_ErrorMessage));
    EXPECT_TRUE(Contains(report, "tests/logger_server/tests.cpp"));
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    EXPECT_TRUE(Contains(report, "platform=linux"));
    EXPECT_TRUE(Contains(report, "status=callstack_captured"));
    EXPECT_TRUE(Contains(report, "callstack:"));
    if(LinuxExternalSymbolizerAvailable(arena))
        EXPECT_TRUE(Contains(report, "CaptureRecoverableErrorForCrashObservation"));
#elif defined(NWB_PLATFORM_WINDOWS)
    EXPECT_TRUE(Contains(report, "platform=windows"));
    EXPECT_TRUE(Contains(report, "resolver=windows_pdb_minidump"));
#endif

    PreserveObservedReport(arena, report, "recoverable_error");

    RemoveTestArtifacts(arena, s_Group);
#else
#endif
}

TEST(LoggerServerCrash, AndroidCrashPackageCopiesTombstoneFrames){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_android_crash_test");
    constexpr AStringView s_Stem("android_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest(arena, archive, "android-test", "android", "crash", "manual_dump", 0u);
    AppendArchiveFile(
        archive,
        CrashNames::s_AndroidTombstoneFileName,
        "backtrace:\n      #00 pc 0000000000012344  /data/app/lib/arm64/libnwb.so (CrashHere+16)\n      #01 pc 0000000000012450  /data/app/lib/arm64/libnwb.so (Caller+12)\n"
    );

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("status=tombstone_parsed")));
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("callstack:")));
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("#00 pc 0000000000012344")));

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, "platform=android"));
    EXPECT_TRUE(Contains(report, "android_tombstone=present"));
    EXPECT_TRUE(Contains(report, "#01 pc 0000000000012450"));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, LinuxCrashPackageReportsMissingProcMaps){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_missing_maps_test");
    constexpr AStringView s_Stem("linux_missing_maps_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest(arena, archive, "linux-missing-maps-test", "linux", "crash", "signal", 11u);
    AppendArchiveFile(archive, CrashNames::s_CpuContextFileName, "instruction_pointer=4198964\n");

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, "platform=linux"));
    EXPECT_TRUE(Contains(report, "proc_maps=missing"));
    EXPECT_TRUE(Contains(report, "proc maps missing for module lookup"));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, LinuxCrashPackageReportsUnmappedInstructionPointer){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_unmapped_ip_test");
    constexpr AStringView s_Stem("linux_unmapped_ip_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest(arena, archive, "linux-unmapped-ip-test", "linux", "crash", "signal", 11u);
    AppendArchiveFile(archive, CrashNames::s_CpuContextFileName, "instruction_pointer=7340032\n");
    AppendArchiveFile(archive, CrashNames::s_ProcMapsFileName, "00400000-00452000 r-xp 00000000 08:01 123 /tmp/nwb_loader\n");

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, "platform=linux"));
    EXPECT_TRUE(Contains(report, "proc_maps=present"));
    EXPECT_TRUE(Contains(report, "instruction pointer was not found in proc maps"));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, AndroidCrashPackageReportsTombstoneWithoutFrames){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_android_no_frames_test");
    constexpr AStringView s_Stem("android_no_frames_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest(arena, archive, "android-no-frames-test", "android", "crash", "manual_dump", 0u);
    AppendArchiveFile(archive, CrashNames::s_AndroidTombstoneFileName, "pid: 7, tid: 7, name: nwb\nbacktrace:\n");

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, "platform=android"));
    EXPECT_TRUE(Contains(report, "status=not_decoded"));
    EXPECT_TRUE(Contains(report, "android_tombstone=present"));
    EXPECT_TRUE(Contains(report, "no native frame lines were recognized"));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, WindowsCrashPackageReportsMissingMinidump){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_windows_missing_dump_test");
    constexpr AStringView s_Stem("windows_missing_dump_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest(arena, archive, "windows-missing-dump-test", "windows", "crash", "windows_exception", 0xC0000005u);

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, "platform=windows"));
    EXPECT_TRUE(Contains(report, "[event]"));
    EXPECT_TRUE(Contains(report, "event=crash"));
    EXPECT_TRUE(Contains(report, "exception=access_violation 0x00000000c0000005"));
    EXPECT_TRUE(Contains(report, "resolver=windows_pdb_minidump"));
#if defined(NWB_PLATFORM_WINDOWS)
    CrashTestText missingDumpMessage(arena);
    missingDumpMessage += CrashNames::s_ProcessDumpFileName;
    missingDumpMessage += " is missing or unreadable";
    EXPECT_TRUE(Contains(report, AStringView(missingDumpMessage.data(), missingDumpMessage.size())));
#else
    EXPECT_TRUE(Contains(report, "only available on Windows logserver builds"));
#endif

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, WindowsCrashPackageDecodesGpuDetectiveCaptureInProcess){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_gpu_detective_test");
    constexpr AStringView s_Stem("gpu_detective_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest(arena, archive, "gpu-detective-test", "windows", "crash", "windows_exception", 0xC0000005u);
    // A non-RDF blob exercises the in-process Radeon GPU Detective decoder end to end: it must reject the garbage
    // gracefully, with no crash or exception escaping the boundary, and surface a decode failure rather than aborting
    // the surrounding crash ingest.
    AppendArchiveFile(archive, CrashNames::s_GpuDetectiveCaptureFileName, "not a valid radeon gpu detective capture\n");

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);
    EXPECT_TRUE(result.accepted);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    // The section header is always emitted (the decoder ran), and garbage input degrades to a reported failure.
    EXPECT_TRUE(Contains(report, "[gpu_detective]"));
    EXPECT_TRUE(Contains(report, "status=decode_failed"));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, AssertCrashPackageUsesAssertLogType){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_assert_log_type_test");
    constexpr AStringView s_Stem("assert_log_type_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    const ManifestTriggerFields trigger{
        .category = DiagnosticEventCategory::s_Assert,
        .expression = "value != nullptr",
        .message = "missing pointer",
        .file = "tests/logger_server/tests.cpp",
        .line = 123u,
    };
    BeginArchiveWithManifest(
        arena,
        archive,
        "assert-log-type-test",
        "windows",
        DiagnosticEventName::s_Assert,
        "manual_dump",
        0u,
        ManifestEventField::Include,
        trigger
    );
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Log::Type::Assert);
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("event=assert")));
    EXPECT_EQ(result.message.find(NWB_TEXT("value != nullptr\nmissing pointer\nat tests/logger_server/tests.cpp:123\n\ncallstack:\n")), 0u);
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("\ndetails:\n")));

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_EQ(FindText(report, "value != nullptr\nmissing pointer\nat tests/logger_server/tests.cpp:123\n\ncallstack:\n"), 0u);

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, FatalCrashPackageUsesFatalLogType){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_fatal_log_type_test");
    constexpr AStringView s_Stem("fatal_log_type_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    const ManifestTriggerFields trigger{
        .category = NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryFatal,
        .expression = "",
        .message = "fatal logger observation",
        .file = "tests/logger_server/tests.cpp",
        .line = 321u,
    };
    BeginArchiveWithManifest(
        arena,
        archive,
        "fatal-log-type-test",
        "windows",
        DiagnosticEventName::s_Fatal,
        "manual_dump",
        0u,
        ManifestEventField::Include,
        trigger
    );
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Log::Type::Fatal);
    EXPECT_EQ(TStringView(NWB::Log::MessageTypeToString(NWB::Log::Type::Fatal)), TStringView(NWB_TEXT("FATAL")));
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("event=fatal")));
    EXPECT_FALSE(ContainsMessage(result.message, NWB_TEXT("category=logger_Fatal")));
    EXPECT_FALSE(ContainsMessage(result.message, NWB_TEXT("message=fatal logger observation")));
    EXPECT_FALSE(ContainsMessage(result.message, NWB_TEXT("file=tests/logger_server/tests.cpp")));

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_EQ(FindText(report, "fatal logger observation\nat tests/logger_server/tests.cpp:321\n\ncallstack:\n"), 0u);

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, InvalidCrashPackageIsRejected){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_invalid_crash_test");
    constexpr AStringView s_Stem("invalid_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    archive += s_InvalidArchiveHeader;
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.type, NWB::Log::Type::Error);
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("Crash upload rejected")));
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("invalid crash archive header")));

    EXPECT_TRUE(PathIsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem)));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, CrashManifestWithoutEventIsRejected){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_missing_event_manifest_crash_test");
    constexpr AStringView s_Stem("missing_event_manifest_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest(
        arena,
        archive,
        "missing-event-manifest-test",
        "linux",
        "crash",
        "signal",
        11u,
        ManifestEventField::Omit
    );
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.type, NWB::Log::Type::Error);
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("manifest.json is missing required fields")));
    EXPECT_TRUE(PathIsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem)));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, CrashRetentionPrunesOldestAcceptedUploads){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_retention_accepted_test");
    constexpr AStringView s_Stem0("retention_001");
    constexpr AStringView s_Stem1("retention_002");
    constexpr AStringView s_Stem2("retention_003");
    RemoveTestArtifacts(arena, s_Group);

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    config.retention.maxExtractedPackages = 2u;
    config.retention.maxRawArchives = 2u;
    config.retention.maxInvalidArchives = 0u;

    {
        CrashTestText archive(arena);
        BuildLinuxCrashArchive(arena, archive, s_Stem0);
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem0, archive, config);
        EXPECT_TRUE(result.accepted);
    }
    {
        CrashTestText archive(arena);
        BuildLinuxCrashArchive(arena, archive, s_Stem1);
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem1, archive, config);
        EXPECT_TRUE(result.accepted);
    }
    {
        CrashTestText archive(arena);
        BuildLinuxCrashArchive(arena, archive, s_Stem2);
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem2, archive, config);
        EXPECT_TRUE(result.accepted);
    }

    EXPECT_TRUE(PathIsMissing(ExtractedPackageDirectory(arena, s_Group, s_Stem0)));
    EXPECT_TRUE(PathIsMissing(RawArchivePath(arena, s_Group, s_Stem0)));
    EXPECT_TRUE(PathIsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem1)));
    EXPECT_TRUE(PathIsRegularFile(RawArchivePath(arena, s_Group, s_Stem1)));
    EXPECT_TRUE(PathIsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem2)));
    EXPECT_TRUE(PathIsRegularFile(RawArchivePath(arena, s_Group, s_Stem2)));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, AcceptedCrashWarnsWhenRawArchiveCannotBeRetained){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_raw_archive_failed_test");
    constexpr AStringView s_Stem("raw_blocked_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BuildLinuxCrashArchive(arena, archive, s_Stem);

    ErrorCode error;
    EXPECT_TRUE(EnsureDirectories(StorageDirectory(arena, s_Group), error));
    EXPECT_TRUE(WriteTextFile(StorageDirectory(arena, s_Group) / NWB::Log::s_CrashRawDirectoryName, AStringView("blocked")));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive, config);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Log::Type::Warning);
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("raw upload archive could not be retained")));
    EXPECT_TRUE(PathIsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem)));
    EXPECT_TRUE(PathIsMissing(ArchivePath(arena, s_Group, s_Stem)));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, CrashRetentionPrunesOldestInvalidUploads){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_retention_invalid_test");
    constexpr AStringView s_Stem0("invalid_retention_001");
    constexpr AStringView s_Stem1("invalid_retention_002");
    RemoveTestArtifacts(arena, s_Group);

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    config.retention.maxExtractedPackages = 0u;
    config.retention.maxRawArchives = 0u;
    config.retention.maxInvalidArchives = 1u;

    {
        CrashTestText archive(arena);
        archive += s_InvalidArchiveHeader;
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem0, archive, config);
        EXPECT_FALSE(result.accepted);
    }
    {
        CrashTestText archive(arena);
        archive += s_InvalidArchiveHeader;
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem1, archive, config);
        EXPECT_FALSE(result.accepted);
    }

    EXPECT_TRUE(PathIsMissing(InvalidArchivePath(arena, s_Group, s_Stem0)));
    EXPECT_TRUE(PathIsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem1)));

    RemoveTestArtifacts(arena, s_Group);
}

TEST(LoggerServerCrash, CrashUploadAuthorizationMatchesBearerToken){
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches(AStringView(), nullptr));
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches(AStringView(), "bad"));
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches("secret-token", "Bearer secret-token"));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches("secret-token", nullptr));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches("secret-token", "secret-token"));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches("secret-token", "Bearer wrong"));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches("secret-token", "Bearer secret-token "));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#undef NWB_LOGSERVER_TEST_NOINLINE


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

