// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_helpers.h"

#include <tests/common/filesystem_helpers.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/crash/module.h>
#include <core/crash/package_names.h>
#include <core/common/log.h>
#include <global/assert.h>
#include <global/environment.h>
#include <global/filesystem/directory_iterator.h>
#include <global/filesystem/operations.h>
#include <global/process_execution.h>
#include <logger/common.h>
#include <logger/server/crash_auth.h>
#include <logger/server/crash_ingest.h>
#include <logger/server/crash_paths.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_server_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_PLATFORM_LINUX = "platform=linux";
static constexpr AStringView s_SECRET_TOKEN = "secret-token";
static constexpr AStringView s_EVENT = "[event]";
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
static constexpr AStringView s_CALLSTACK = "callstack:";
static constexpr AStringView s_STATUS_CALLSTACK_CAPTURED = "status=callstack_captured";
#endif
static constexpr AStringView s_TESTS_INTEGRATION_LOGGER_SERVER_LOGSERVE = "tests/integration/logger_server/logserver_crash_tests.cpp";
static constexpr AStringView s_LINUX = "linux";
static constexpr AStringView s_CRASH = "crash";
static constexpr AStringView s_SIGNAL = "signal";
static constexpr AStringView s_MANUAL_DUMP = "manual_dump";
static constexpr AStringView s_WINDOWS = "windows";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = NWB::Tests::TestArena<struct LoggerServerCrashTestsTag>;
using CapturingLogger = NWB::Tests::CapturingLogger;
using NWB::Tests::WaitForDirectory;
using namespace NWB::Tests::LoggerServerCrash;
namespace CrashNames = NWB::Core::Crash::PackageNames;
inline constexpr AStringView s_InvalidArchiveHeader("NWBCRASHPKG 0\n");
inline constexpr Name s_AssertChildInstallArena("tests/integration/logger_server/assert_child_install");
inline constexpr Name s_RecoverableErrorInstallArena("tests/integration/logger_server/recoverable_error_install");
#if defined(_MSC_VER)
#define NWB_LOGSERVER_TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define NWB_LOGSERVER_TEST_NOINLINE [[gnu::noinline]]
#else
#define NWB_LOGSERVER_TEST_NOINLINE
#endif


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


static void AppendDecimalText(CrashTestText& outText, const u64 value){
    char buffer[32] = {};
    outText += FormatDecimal(static_cast<usize>(value), buffer);
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

[[nodiscard]] static AStringView LinuxObservableAssertCategory(){
#if NWB_OCCUR_ASSERT
    return DiagnosticEventCategory::s_Assert.data();
#else
    return DiagnosticEventCategory::s_FatalAssert.data();
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

// Shared by Windows and Linux; match the test guard to avoid an unused Android helper.
#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
NWB_LOGSERVER_TEST_NOINLINE static void CaptureRecoverableErrorForCrashObservation(const AStringView message){
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventName::s_Error.data(),
        .category = NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError.data(),
        .message = message.data(),
        .file = "tests/integration/logger_server/logserver_crash_tests.cpp",
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

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
[[nodiscard]] static usize FindText(const CrashTestText& text, const AStringView needle)noexcept{
    return AStringView(text.data(), text.size()).find(needle);
}
#endif


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
            "tests/integration/logger_server/logserver_crash_tests.cpp",
            assertPackageDirectory
        ));

    CrashTestBytes archive(arena);
    EXPECT_TRUE(BuildArchiveFromPackageDirectory(arena, assertPackageDirectory, archive));
    const NWB::Log::CrashIngestResult result = ProcessCrashArchiveBytes(arena, s_Group, s_Stem, archive);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Assert);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, s_PLATFORM_LINUX));
    EXPECT_TRUE(Contains(report, "reason=manual_dump"));
    EXPECT_TRUE(Contains(report, s_EVENT));
    EXPECT_TRUE(Contains(report, "event=assert"));
    EXPECT_TRUE(Contains(report, s_STATUS_CALLSTACK_CAPTURED));
    EXPECT_TRUE(Contains(report, s_CALLSTACK));
    EXPECT_EQ(FindText(report, "false\nat "), 0u);
    EXPECT_TRUE(Contains(report, s_TESTS_INTEGRATION_LOGGER_SERVER_LOGSERVE));
    if(LinuxExternalSymbolizerAvailable(arena)){
        EXPECT_TRUE(Contains(report, "LinuxForceAssertFalseForCrashObservation"));
    }

    PreserveObservedReport(arena, report, "linux_assert");

    RemoveTestArtifacts(arena, s_Group);
#else
#endif
}

TEST_F(LoggerServerCrash, RecoverableErrorDiagnosticProducesObservableLoggerReport){
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
            "tests/integration/logger_server/logserver_crash_tests.cpp",
            errorPackageDirectory
        ));
    EXPECT_TRUE(continuedAfterError);
    NWB::Core::Crash::UninstallCrashHandler();

    CrashTestBytes archive(arena);
    EXPECT_TRUE(BuildArchiveFromPackageDirectory(arena, errorPackageDirectory, archive));
    const NWB::Log::CrashIngestResult result = ProcessCrashArchiveBytes(arena, s_Group, s_Stem, archive);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Error);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, s_EVENT));
    EXPECT_TRUE(Contains(report, "event=error"));
    EXPECT_TRUE(Contains(report, s_ErrorMessage));
    EXPECT_TRUE(Contains(report, s_TESTS_INTEGRATION_LOGGER_SERVER_LOGSERVE));
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    EXPECT_TRUE(Contains(report, s_PLATFORM_LINUX));
    EXPECT_TRUE(Contains(report, s_STATUS_CALLSTACK_CAPTURED));
    EXPECT_TRUE(Contains(report, s_CALLSTACK));
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


TEST_F(LoggerServerCrash, LinuxCrashPackageReportsMissingProcMaps){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_missing_maps_test");
    constexpr AStringView s_Stem("linux_missing_maps_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest(arena, archive, "linux-missing-maps-test", s_LINUX, s_CRASH, s_SIGNAL, 11u);
    AppendArchiveFile(archive, CrashNames::s_CpuContextFileName, "instruction_pointer=4198964\n");

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, s_PLATFORM_LINUX));
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
    BeginArchiveWithManifest(arena, archive, "linux-unmapped-ip-test", s_LINUX, s_CRASH, s_SIGNAL, 11u);
    AppendArchiveFile(archive, CrashNames::s_CpuContextFileName, "instruction_pointer=7340032\n");
    AppendArchiveFile(archive, CrashNames::s_ProcMapsFileName, "00400000-00452000 r-xp 00000000 08:01 123 /tmp/nwb_loader\n");

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, s_PLATFORM_LINUX));
    EXPECT_TRUE(Contains(report, "proc_maps=present"));
    EXPECT_TRUE(Contains(report, "instruction pointer was not found in proc maps"));

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, AndroidCrashPackageReportsTombstoneWithoutFrames){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_android_no_frames_test");
    constexpr AStringView s_Stem("android_no_frames_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest(arena, archive, "android-no-frames-test", "android", s_CRASH, s_MANUAL_DUMP, 0u);
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

TEST_F(LoggerServerCrash, WindowsCrashPackageReportsMissingMinidump){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_windows_missing_dump_test");
    constexpr AStringView s_Stem("windows_missing_dump_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest(arena, archive, "windows-missing-dump-test", s_WINDOWS, s_CRASH, "windows_exception", 0xC0000005u);

    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_TRUE(result.accepted);

    CrashTestText report(arena);
    EXPECT_TRUE(ReadServerSymbolication(arena, s_Group, s_Stem, report));
    EXPECT_TRUE(Contains(report, "platform=windows"));
    EXPECT_TRUE(Contains(report, s_EVENT));
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

TEST_F(LoggerServerCrash, WindowsCrashPackageDecodesGpuDetectiveCaptureInProcess){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_gpu_detective_test");
    constexpr AStringView s_Stem("gpu_detective_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BeginArchiveWithManifest(arena, archive, "gpu-detective-test", s_WINDOWS, s_CRASH, "windows_exception", 0xC0000005u);
    // Malformed RGD input must report decode failure without aborting crash ingest.
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
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("Crash upload rejected")));
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("invalid crash archive header")));

    EXPECT_TRUE(PathIsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem)));

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, CrashManifestWithoutEventIsRejected){
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
        s_LINUX,
        s_CRASH,
        s_SIGNAL,
        11u,
        ManifestEventField::Omit
    );
    const NWB::Log::CrashIngestResult result = ProcessCrashArchive(arena, s_Group, s_Stem, archive);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Error);
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("manifest.json is missing required fields")));
    EXPECT_TRUE(PathIsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem)));

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

TEST_F(LoggerServerCrash, AcceptedCrashWarnsWhenRawArchiveCannotBeRetained){
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
    EXPECT_EQ(result.type, NWB::Core::Common::LogType::Warning);
    EXPECT_TRUE(ContainsMessage(result.message, NWB_TEXT("raw upload archive could not be retained")));
    EXPECT_TRUE(PathIsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem)));
    EXPECT_TRUE(PathIsMissing(ArchivePath(arena, s_Group, s_Stem)));

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

    EXPECT_TRUE(PathIsMissing(InvalidArchivePath(arena, s_Group, s_Stem0)));
    EXPECT_TRUE(PathIsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem1)));

    RemoveTestArtifacts(arena, s_Group);
}

TEST_F(LoggerServerCrash, MessagePayloadReadsUnalignedBytesAndPreservesEmbeddedNulls){
    TestArena testArena;
    constexpr tchar s_Message[] = { static_cast<tchar>('A'), 0, static_cast<tchar>('Z') };
    const TStringView message(s_Message, LengthOf(s_Message));
    const NWB::Log::MessageType source = MakeTuple(Timer{}, NWB::Core::Common::LogType::Warning, NWB::Log::LogString(message, testArena.arena));
    NWB::Log::LogBytes payload(testArena.arena);
    ASSERT_TRUE(NWB::Log::BuildMessagePayload(source, payload));

    constexpr usize s_PrefixBytes = sizeof(tchar);
    NWB::Log::LogBytes shifted(testArena.arena);
    shifted.resize(s_PrefixBytes + payload.size());
    NWB_MEMCPY(shifted.data() + s_PrefixBytes, payload.size(), payload.data(), payload.size());
    NWB::Log::MessageType parsed = NWB::Log::MakeMessageType(testArena.arena);
    TStringView error;
    ASSERT_TRUE(NWB::Log::ParseMessagePayload(testArena.arena, shifted.data() + s_PrefixBytes, payload.size(), parsed, error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(TStringView(Get<2u>(parsed)), message);

    shifted.back() = 1u;
    EXPECT_FALSE(NWB::Log::ParseMessagePayload(testArena.arena, shifted.data() + s_PrefixBytes, payload.size(), parsed, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(Get<2u>(parsed).empty());
}

TEST_F(LoggerServerCrash, CrashUploadAuthorizationMatchesBearerToken){
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches(AStringView(), AStringView()));
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches(AStringView(), "bad"));
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches(s_SECRET_TOKEN, "Bearer secret-token"));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SECRET_TOKEN, AStringView()));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SECRET_TOKEN, s_SECRET_TOKEN.data()));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SECRET_TOKEN, "Bearer wrong"));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SECRET_TOKEN, "Bearer secret-token "));

    const char headerBytes[] = "Bearer secret-token trailing";
    const AStringView header(headerBytes, LengthOf("Bearer secret-token") - 1u);
    EXPECT_TRUE(NWB::Log::CrashUploadAuthorizationMatches(s_SECRET_TOKEN, header));
    EXPECT_FALSE(NWB::Log::CrashUploadAuthorizationMatches(s_SECRET_TOKEN, header.substr(0u, header.size() - 1u)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_LOGSERVER_TEST_NOINLINE


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

