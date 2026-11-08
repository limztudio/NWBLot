// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_helpers.h"

#include <tests/common/filesystem_helpers.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/crash/module.h>
#include <core/crash/package_internal.h>
#include <core/crash/package_archive.h>
#include <core/crash/package_names.h>
#include <core/common/log.h>

#include <global/assert.h>
#include <global/environment.h>
#include <global/filesystem/directory_iterator.h>
#include <global/filesystem/operations.h>
#include <global/process.h>
#include <global/process_execution.h>

#include <logger/common.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logserver_crash_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_TestSourceFilename = "tests/integration/logger_server/logserver_crash_tests.cpp";
constexpr AStringView s_ApplicationName = "logserver_crash_tests";
constexpr AStringView s_ApplicationVersion = "1";
constexpr AStringView s_WindowsExceptionCategory = "windows_exception";

static constexpr AStringView s_PlatformLinux = "platform=linux";
static constexpr AStringView s_Event = "[event]";
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
static constexpr AStringView s_Callstack = "callstack:";
static constexpr AStringView s_StatusCallstackCaptured = "status=callstack_captured";
#endif
static constexpr AStringView s_Linux = "linux";
static constexpr AStringView s_Crash = "crash";
static constexpr AStringView s_Signal = "signal";
static constexpr AStringView s_ManualDump = "manual_dump";
static constexpr AStringView s_Windows = "windows";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = NWB::Tests::TestArena<>;
using CapturingLogger = NWB::Tests::CapturingLogger;
using NWB::Tests::WaitForDirectory;
using namespace NWB::Tests::CrashTestPaths;
using namespace NWB::Tests::LoggerServerCrash;
using namespace NWB::Core::Crash::PackageNames;
inline constexpr AStringView s_InvalidArchiveHeader("NWBCRASHPKG 0\n");
inline constexpr Name s_ArchiveScratch("tests/integration/logger_server/archive_scratch");
inline constexpr Name s_AssertChildInstallArena("tests/integration/logger_server/assert_child_install");
inline constexpr Name s_RecoverableErrorInstallArena("tests/integration/logger_server/recoverable_error_install");


class LoggerServerCrash : public ::testing::Test{
public:
    LoggerServerCrash()
        : m_loggerRegistration(m_logger)
    {}


private:
    CapturingLogger m_logger;
    NWB::Core::Common::LoggerRegistrationGuard m_loggerRegistration;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)


static void AppendDecimalText(const u64 value, CrashTestText& outText){
    Array<char, 32u> buffer = {};
    outText += FormatDecimal(static_cast<usize>(value), buffer);
}


[[nodiscard]] static Expected<b8, ErrorCode> LinuxExternalSymbolizerAvailable(NWB::Core::Alloc::GlobalArena& arena){
    const auto pathText = ReadEnvironmentVariable(arena, "PATH");
    if(!pathText)
        return MakeUnexpected(pathText.error());
    if(!*pathText || (*pathText)->empty())
        return false;

    const AStringView searchPath((*pathText)->data(), (*pathText)->size());
    const auto llvmAvailable = ExecutableAvailableInPath(arena, searchPath, "llvm-symbolizer");
    if(!llvmAvailable)
        return MakeUnexpected(llvmAvailable.error());
    if(*llvmAvailable)
        return true;
    return ExecutableAvailableInPath(arena, searchPath, "addr2line");
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

[[nodiscard]] static b8 PendingDirectoryContainsManifestTexts(NWB::Core::Alloc::GlobalArena& arena, const CrashTestPath& pendingDirectory, const AStringView firstNeedle, const AStringView secondNeedle){
    const auto directory = DirectoryIterator<NWB::Core::Alloc::GlobalArena>::Create(pendingDirectory);
    if(!directory)
        return false;

    for(const auto& entry : *directory){
        const auto isDirectory = IsDirectory(entry.path());
        if(!isDirectory || !*isDirectory)
            continue;

        CrashTestText manifest(arena);
        if(!ReadTextFile(entry.path() / s_ManifestFileName, manifest))
            continue;
        if(Contains(manifest, firstNeedle) && Contains(manifest, secondNeedle))
            return true;
    }

    return false;
}

[[nodiscard]] static AStringView LinuxObservableAssertCategory()noexcept{
#if NWB_OCCUR_ASSERT
    return DiagnosticEventCategory::s_Assert;
#else
    return DiagnosticEventCategory::s_FatalAssert;
#endif
}

NWB_NOINLINE static void LinuxForceAssertFalseForCrashObservation(){
#if NWB_OCCUR_ASSERT
    NWB_ASSERT(false);
#else
    NWB_FATAL_ASSERT(false);
#endif
    _exit(120);
}
#endif

// Guard this helper on Android where its caller test is disabled.
#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
NWB_NOINLINE static void CaptureRecoverableErrorForCrashObservation(const AStringView message)noexcept{
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error,
        .category = NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError,
        .message = message,
        .file = s_TestSourceFilename,
        .line = __LINE__,
    });
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB::Log::CrashIngestResult ProcessCrashArchive(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const AStringView archive, const NWB::Log::CrashIngestConfig& config){
    EXPECT_TRUE(WriteArchive(arena, testGroup, stem, archive));
    return NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, testGroup, stem), config);
}

static NWB::Log::CrashIngestResult ProcessCrashArchive(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const AStringView archive){
    const NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, testGroup);
    return ProcessCrashArchive(arena, testGroup, stem, archive, config);
}

static NWB::Log::CrashIngestResult ProcessCrashArchiveBytes(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const Span<const u8> archive, const NWB::Log::CrashIngestConfig& config){
    EXPECT_TRUE(WriteArchiveBytes(arena, testGroup, stem, archive));
    return NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, testGroup, stem), config);
}

static NWB::Log::CrashIngestResult ProcessCrashArchiveBytes(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const Span<const u8> archive){
    const NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, testGroup);
    return ProcessCrashArchiveBytes(arena, testGroup, stem, archive, config);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(LoggerServerCrash, LinuxAssertCrashProducesObservableLoggerReport){
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
        NWB::Core::Alloc::PersistentArena installArena(s_AssertChildInstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
        NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
        config.applicationName = AStringView(s_ApplicationName);
        config.version = AStringView(s_ApplicationVersion);
        config.buildId = AStringView("linux-assert-observe-test");
        config.spoolDirectory = spoolDirectory;

        if(!NWB::Core::Crash::InstallCrashHandler(config))
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

    const CrashTestPath pendingDirectory = spoolDirectory / s_PendingDirectoryName;
    {
        constexpr u32 s_PendingDirectoryReadyTimeoutMS = 3000u;
        const auto directoryReady = WaitForDirectory(pendingDirectory, s_PendingDirectoryReadyTimeoutMS);
        ASSERT_TRUE(directoryReady);
        EXPECT_TRUE(*directoryReady);
    }

    CrashTestText expectedAbortCode(arena);
    expectedAbortCode += "\"reason_code\": ";
    AppendDecimalText(static_cast<u64>(SIGABRT), expectedAbortCode);
    EXPECT_FALSE(PendingDirectoryContainsManifestTexts(arena, pendingDirectory, AStringView("\"reason_kind\": \"signal\""), AStringView(expectedAbortCode.data(), expectedAbortCode.size())));

    const auto assertPackageDirectory = WaitForTriggerPackage(arena, pendingDirectory, expectedAssertCategory, "false", AStringView(), s_TestSourceFilename);
    EXPECT_TRUE(assertPackageDirectory);

    ASSERT_TRUE(assertPackageDirectory);
    NWB::Core::Alloc::ScratchArena archiveScratch(s_ArchiveScratch);
    const auto archive = NWB::Core::Crash::CrashDetail::CrashPackageArchive::build(arena, archiveScratch, *assertPackageDirectory);
    ASSERT_TRUE(archive);
    const NWB::Log::CrashIngestResult result = ProcessCrashArchiveBytes(arena, s_Group, s_Stem, *archive);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Assert);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    EXPECT_TRUE(Contains(report, s_PlatformLinux));
    EXPECT_TRUE(Contains(report, "reason=manual_dump"));
    EXPECT_TRUE(Contains(report, s_Event));
    EXPECT_TRUE(Contains(report, "event=assert"));
    EXPECT_TRUE(Contains(report, s_StatusCallstackCaptured));
    EXPECT_TRUE(Contains(report, s_Callstack));
    EXPECT_EQ(report.find("false\nat "), 0u);
    EXPECT_TRUE(Contains(report, s_TestSourceFilename));
    const auto externalSymbolizer = LinuxExternalSymbolizerAvailable(arena);
    ASSERT_TRUE(externalSymbolizer);
    if(*externalSymbolizer){
        EXPECT_TRUE(Contains(report, "LinuxForceAssertFalseForCrashObservation"));
    }

    PreserveObservedReport(arena, report, "linux_assert");

    RemoveTestArtifacts(arena, s_Group);
#endif
}

TEST_F(LoggerServerCrash, RecoverableErrorDiagnosticProducesObservableLoggerReport){
#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(s_RecoverableErrorInstallArena, NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u));
    constexpr AStringView s_Group("logger_server_recoverable_error_observe_test");
    constexpr AStringView s_Stem("recoverable_error_observe_001");
    constexpr AStringView s_ErrorMessage("recoverable logger error observation");
    RemoveTestArtifacts(arena, s_Group);

    const CrashTestPath spoolDirectory = SpoolDirectory(arena, s_Group);
    NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> crashConfig(installArena);
    crashConfig.applicationName = AStringView(s_ApplicationName);
    crashConfig.version = AStringView(s_ApplicationVersion);
    crashConfig.buildId = AStringView("recoverable-error-observe-test");
    crashConfig.spoolDirectory = spoolDirectory;

    const b8 installed = NWB::Core::Crash::InstallCrashHandler(crashConfig);
    EXPECT_TRUE(installed);

    b8 continuedAfterError = false;
    if(installed){
        CaptureRecoverableErrorForCrashObservation(s_ErrorMessage);
        continuedAfterError = true;
    }

    const CrashTestPath pendingDirectory = spoolDirectory / s_PendingDirectoryName;
    const auto errorPackageDirectory = WaitForTriggerPackage(arena, pendingDirectory, NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError, AStringView(), s_ErrorMessage, s_TestSourceFilename);
    EXPECT_TRUE(errorPackageDirectory);
    EXPECT_TRUE(continuedAfterError);
    NWB::Core::Crash::UninstallCrashHandler();

    ASSERT_TRUE(errorPackageDirectory);
    NWB::Core::Alloc::ScratchArena archiveScratch(s_ArchiveScratch);
    const auto archive = NWB::Core::Crash::CrashDetail::CrashPackageArchive::build(arena, archiveScratch, *errorPackageDirectory);
    ASSERT_TRUE(archive);
    const NWB::Log::CrashIngestResult result = ProcessCrashArchiveBytes(arena, s_Group, s_Stem, *archive);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Error);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    EXPECT_TRUE(Contains(report, s_Event));
    EXPECT_TRUE(Contains(report, "event=error"));
    EXPECT_TRUE(Contains(report, s_ErrorMessage));
    EXPECT_TRUE(Contains(report, s_TestSourceFilename));
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    EXPECT_TRUE(Contains(report, s_PlatformLinux));
    EXPECT_TRUE(Contains(report, s_StatusCallstackCaptured));
    EXPECT_TRUE(Contains(report, s_Callstack));
    const auto externalSymbolizer = LinuxExternalSymbolizerAvailable(arena);
    ASSERT_TRUE(externalSymbolizer);
    if(*externalSymbolizer)
        EXPECT_TRUE(Contains(report, "CaptureRecoverableErrorForCrashObservation"));
#elif defined(NWB_PLATFORM_WINDOWS)
    EXPECT_TRUE(Contains(report, "platform=windows"));
    EXPECT_TRUE(Contains(report, "resolver=windows_pdb_minidump"));
#endif

    PreserveObservedReport(arena, report, "recoverable_error");

    RemoveTestArtifacts(arena, s_Group);
#endif
}


TEST_F(LoggerServerCrash, LinuxCrashPackageReportsMissingProcMaps){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_missing_maps_test");
    constexpr AStringView s_Stem("linux_missing_maps_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("linux-missing-maps-test", s_Linux, s_Crash, s_Signal, 11u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);
    AppendArchiveFile(s_CpuContextFileName, "instruction_pointer=4198964\n", archive);

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    EXPECT_TRUE(Contains(report, s_PlatformLinux));
    EXPECT_TRUE(Contains(report, "proc_maps=missing"));
    EXPECT_TRUE(Contains(report, "proc maps missing for module lookup"));

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, LinuxCrashPackageReportsUnmappedInstructionPointer){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_unmapped_ip_test");
    constexpr AStringView s_Stem("linux_unmapped_ip_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("linux-unmapped-ip-test", s_Linux, s_Crash, s_Signal, 11u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);
    AppendArchiveFile(s_CpuContextFileName, "instruction_pointer=7340032\n", archive);
    AppendArchiveFile(s_ProcMapsFileName, "00400000-00452000 r-xp 00000000 08:01 123 /tmp/nwb_loader\n", archive);

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    EXPECT_TRUE(Contains(report, s_PlatformLinux));
    EXPECT_TRUE(Contains(report, "proc_maps=present"));
    EXPECT_TRUE(Contains(report, "instruction pointer was not found in proc maps"));

    RemoveTestArtifacts(arena, s_Group);
}

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
TEST_F(LoggerServerCrash, LinuxCrashPackageDoesNotWrapSymbolOffset){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_offset_overflow_test");
    constexpr AStringView s_Stem("linux_offset_overflow_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("linux-offset-overflow-test", s_Linux, s_Crash, s_Signal, 11u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);
    AppendArchiveFile(s_CpuContextFileName, "instruction_pointer=4097\n", archive);
    AppendArchiveFile(s_ProcMapsFileName, "1000-2000 r-xp ffffffffffffffff 00:00 0 /tmp/overflow\n", archive);
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);
    ASSERT_TRUE(result.accepted);
    auto report = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(report);
    EXPECT_TRUE(Contains(*report, "instruction_pointer_module=/tmp/overflow"));
    EXPECT_TRUE(Contains(*report, "symbolication_relative_ip=unavailable"));
    RemoveTestArtifacts(arena, s_Group);
}
#endif

TEST_F(LoggerServerCrash, AndroidCrashPackageReportsTombstoneWithoutFrames){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_android_no_frames_test");
    constexpr AStringView s_Stem("android_no_frames_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("android-no-frames-test", "android", s_Crash, s_ManualDump, 0u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);
    AppendArchiveFile(s_AndroidTombstoneFileName, "pid: 7, tid: 7, name: nwb\nbacktrace:\n", archive);

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    EXPECT_TRUE(Contains(report, "platform=android"));
    EXPECT_TRUE(Contains(report, "status=not_decoded"));
    EXPECT_TRUE(Contains(report, "android_tombstone=present"));
    EXPECT_TRUE(Contains(report, "no native frame lines were recognized"));

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, WindowsCrashPackageReportsMissingMinidump){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_windows_missing_dump_test");
    constexpr AStringView s_Stem("windows_missing_dump_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("windows-missing-dump-test", s_Windows, s_Crash, s_WindowsExceptionCategory, 0xC0000005u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    EXPECT_TRUE(Contains(report, "platform=windows"));
    EXPECT_TRUE(Contains(report, s_Event));
    EXPECT_TRUE(Contains(report, "event=crash"));
    EXPECT_TRUE(Contains(report, "exception=access_violation 0x00000000c0000005"));
    EXPECT_TRUE(Contains(report, "resolver=windows_pdb_minidump"));
#if defined(NWB_PLATFORM_WINDOWS)
    CrashTestText missingDumpMessage(arena);
    missingDumpMessage += s_ProcessDumpFileName;
    missingDumpMessage += " is missing or unreadable";
    EXPECT_TRUE(Contains(report, AStringView(missingDumpMessage.data(), missingDumpMessage.size())));
#else
    EXPECT_TRUE(Contains(report, "only available on Windows logserver builds"));
#endif

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, WindowsCrashPackageDecodesGpuDetectiveCaptureInProcess){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_gpu_detective_test");
    constexpr AStringView s_Stem("gpu_detective_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("gpu-detective-test", s_Windows, s_Crash, s_WindowsExceptionCategory, 0xC0000005u, ManifestEventField::Include, ManifestTriggerFields{}, arena, archive);
    // Malformed RGD input must report decode failure without aborting crash ingest.
    AppendArchiveFile(s_GpuDetectiveCaptureFileName, "not a valid radeon gpu detective capture\n", archive);

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);
    EXPECT_TRUE(result.accepted);

    auto reportResult = ReadServerSymbolication(arena, s_Group, s_Stem);
    ASSERT_TRUE(reportResult);
    CrashTestText report = Move(*reportResult);
    // The decoder always emits the section header before reporting invalid input.
    EXPECT_TRUE(Contains(report, "[gpu_detective]"));
    EXPECT_TRUE(Contains(report, "status=decode_failed"));

    RemoveTestArtifacts(arena, s_Group);
}


TEST_F(LoggerServerCrash, InvalidCrashPackageIsRejected){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_invalid_crash_test");
    constexpr AStringView s_Stem("invalid_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    archive += s_InvalidArchiveHeader;
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Error);
    EXPECT_TRUE(Contains(result.message, NWB_TEXT("Crash upload rejected")));
    EXPECT_TRUE(Contains(result.message, NWB_TEXT("invalid crash archive header")));

    {
        const auto pathQuery = IsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, UnsafeArchiveEntryCannotEscapePackageDirectory){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_unsafe_archive_20261107");
    constexpr AStringView s_Stem("unsafe_001");
    const auto testDirectory = NWB::Tests::CrashTestPaths::TestCaseDirectory(arena, s_Group);
    const auto existed = FileExistsNoFollow(testDirectory);
    ASSERT_TRUE(existed);
    ASSERT_FALSE(*existed);

    CrashTestText archive(arena);
    archive += s_ArchiveHeaderText;
    AppendArchiveFile("../escape", "payload", archive);
    const auto result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);
    EXPECT_FALSE(result.accepted);
    EXPECT_TRUE(Contains(result.message, NWB_TEXT("malformed crash archive file header")));
    const auto escaped = FileExistsNoFollow(StorageDirectory(arena, s_Group) / "packages" / "escape");
    ASSERT_TRUE(escaped);
    EXPECT_FALSE(*escaped);
    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, CrashManifestWithoutEventIsRejected){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_missing_event_manifest_crash_test");
    constexpr AStringView s_Stem("missing_event_manifest_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest("missing-event-manifest-test", s_Linux, s_Crash, s_Signal, 11u, ManifestEventField::Omit, ManifestTriggerFields{}, arena, archive);
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Error);
    EXPECT_TRUE(Contains(result.message, NWB_TEXT("manifest.json is missing required fields")));
    {
        const auto pathQuery = IsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, InvalidManifestBooleanIsRejectedWhileFalseAndZeroRemainValid){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_boolean_manifest_test");
    constexpr AStringView s_ValidStem("valid_false_001");
    constexpr AStringView s_InvalidStem("invalid_boolean_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText validArchive(arena);
    BeginArchiveWithManifest(s_ValidStem, s_Linux, s_Crash, s_Signal, 0u, ManifestEventField::Include, ManifestTriggerFields{}, arena, validArchive);
    const auto validResult = ProcessCrashArchive(arena, s_Group, s_ValidStem, validArchive);
    ASSERT_TRUE(validResult.accepted);

    CrashTestText manifest = BuildManifest(s_InvalidStem, s_Linux, s_Crash, s_Signal, 0u, ManifestEventField::Include, ManifestTriggerFields{}, arena);
    constexpr AStringView s_BooleanPrefix("\"has_exception_context\": ");
    const usize fieldOffset = AStringView(manifest.data(), manifest.size()).find(s_BooleanPrefix);
    ASSERT_NE(fieldOffset, AStringView::npos);
    manifest.replace(fieldOffset + s_BooleanPrefix.size(), 5u, "null");

    CrashTestText invalidArchive(arena);
    invalidArchive += s_ArchiveHeaderText;
    AppendArchiveFile(s_ManifestFileName, AStringView(manifest.data(), manifest.size()), invalidArchive);
    const auto invalidResult = ProcessCrashArchive(arena, s_Group, s_InvalidStem, invalidArchive);
    EXPECT_FALSE(invalidResult.accepted);
    EXPECT_TRUE(Contains(invalidResult.message, NWB_TEXT("manifest.json is missing required fields")));
    {
        const auto pathQuery = IsRegularFile(InvalidArchivePath(arena, s_Group, s_InvalidStem));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(ExtractedPackageDirectory(arena, s_Group, s_ValidStem));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, CrashRetentionPrunesOldestAcceptedUploads){
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
        BuildLinuxCrashArchive(s_Stem0, arena, archive);
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem0, archive, config);
        EXPECT_TRUE(result.accepted);
    }
    {
        CrashTestText archive(arena);
        BuildLinuxCrashArchive(s_Stem1, arena, archive);
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem1, archive, config);
        EXPECT_TRUE(result.accepted);
    }
    {
        CrashTestText archive(arena);
        BuildLinuxCrashArchive(s_Stem2, arena, archive);
        const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem2, archive, config);
        EXPECT_TRUE(result.accepted);
    }

    {
        const auto pathQuery = FileExists(ExtractedPackageDirectory(arena, s_Group, s_Stem0));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(RawArchivePath(arena, s_Group, s_Stem0));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem1));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsRegularFile(RawArchivePath(arena, s_Group, s_Stem1));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem2));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = IsRegularFile(RawArchivePath(arena, s_Group, s_Stem2));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, AcceptedCrashWarnsWhenRawArchiveCannotBeRetained){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_raw_archive_failed_test");
    constexpr AStringView s_Stem("raw_blocked_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BuildLinuxCrashArchive(s_Stem, arena, archive);

    EXPECT_TRUE(EnsureDirectories(StorageDirectory(arena, s_Group)));
    EXPECT_TRUE(WriteTextFile(StorageDirectory(arena, s_Group) / NWB::Log::s_CrashRawDirectoryName, AStringView("blocked")));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive, config);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Warning);
    EXPECT_TRUE(Contains(result.message, NWB_TEXT("raw upload archive could not be retained")));
    {
        const auto pathQuery = IsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }
    {
        const auto pathQuery = FileExists(ArchivePath(arena, s_Group, s_Stem));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, CrashRetentionPrunesOldestInvalidUploads){
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

    {
        const auto pathQuery = FileExists(InvalidArchivePath(arena, s_Group, s_Stem0));
        ASSERT_TRUE(pathQuery);
        EXPECT_FALSE(*pathQuery);
    }
    {
        const auto pathQuery = IsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem1));
        ASSERT_TRUE(pathQuery);
        EXPECT_TRUE(*pathQuery);
    }

    RemoveTestArtifacts(arena, s_Group);
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

