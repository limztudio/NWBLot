// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_helpers.h"

#include <tests/filesystem_helpers.h>
#include <tests/test_context.h>

#include <core/crash/module.h>
#include <core/crash/package_names.h>
#include <core/common/log.h>
#include <global/assert.h>
#include <global/filesystem/operations.h>
#include <global/process_execution.h>
#include <logger/server/crash_auth.h>
#include <logger/server/crash_ingest.h>
#include <logger/server/crash_paths.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <cstdlib>
#include <dlfcn.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_server_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using TestArena = NWB::Tests::TestArena<struct LoggerServerCrashTestsTag>;
using NWB::Tests::WaitForDirectory;
using namespace NWB::Tests::LoggerServerCrash;
namespace CrashNames = NWB::Core::Crash::PackageNames;

#define NWB_LOGSERVER_TEST_CHECK NWB_TEST_CHECK
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
    const char* const pathText = std::getenv("PATH");
    if(!pathText || pathText[0] == 0)
        return false;

    const AStringView searchPath(pathText);
    return ExecutableAvailableInPath(arena, searchPath, "llvm-symbolizer")
        || ExecutableAvailableInPath(arena, searchPath, "addr2line")
    ;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestLinuxCrashPackageMapsInstructionPointer(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_crash_test");
    constexpr AStringView s_Stem("linux_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BuildLinuxCrashArchive(arena, archive, "linux-test");

    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem, archive));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    config.symbolication.symbolStoreDirectory = StorageDirectory(arena, s_Group) / "test_symbols";
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);
    NWB_LOGSERVER_TEST_CHECK(context, result.type == NWB::Log::Type::EssentialInfo);
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("module_relative_ip=0x0000000000001234")));

    CrashTestText report(arena);
    NWB_LOGSERVER_TEST_CHECK(context, ReadServerSymbolication(arena, s_Group, s_Stem, report));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=linux"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "[event]"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "event=crash"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "exception=SIGSEGV (11)"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "status=callstack_captured"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "symbol_store="));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "symbol_store_status=missing"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "instruction_pointer_module=/tmp/nwb_loader"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "module_relative_ip=0x0000000000001234"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "[callstack]"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "#0 0x0000000000401234 /tmp/nwb_loader+0x0000000000001234"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "#1 0x0000000000401240 /tmp/nwb_loader+0x0000000000001240"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "core_artifact=missing"));

    PreserveObservedReport(context, arena, report, "linux_maps");

    RemoveTestArtifacts(arena, s_Group);
}

static void TestLinuxCrashPackageSymbolicatesSelfFrame(TestContext& context){
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
    NWB_LOGSERVER_TEST_CHECK(context, BuildSelfProcMapLineForAddress(arena, frameAddress, procMapLine, expectedSymbolicationOffset));

    CrashTestText archive(arena);
    archive += CrashNames::s_ArchiveHeaderText;
    const CrashTestText manifest = BuildManifest(arena, "linux-symbolized-test", "linux");
    AppendArchiveFile(archive, CrashNames::s_ManifestFileName, AStringView(manifest.data(), manifest.size()));

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

    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem, archive));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);

    CrashTestText report(arena);
    NWB_LOGSERVER_TEST_CHECK(context, ReadServerSymbolication(arena, s_Group, s_Stem, report));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=linux"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "module frames are symbolized with DWARF"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "LinuxCrashSymbolicationProbe") || Contains(report, "tests/logger_server/tests.cpp"));
    CrashTestText expectedSymbolicationIp(arena);
    expectedSymbolicationIp += "symbolication_relative_ip=";
    AppendHexAddressText(arena, expectedSymbolicationIp, expectedSymbolicationOffset);
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, AStringView(expectedSymbolicationIp.data(), expectedSymbolicationIp.size())));

    PreserveObservedReport(context, arena, report, "linux_symbolicated");

    RemoveTestArtifacts(arena, s_Group);
#else
    static_cast<void>(context);
#endif
}

static void TestLinuxAssertCrashProducesObservableLoggerReport(TestContext& context){
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_assert_observe_test");
    constexpr AStringView s_Stem("linux_assert_observe_001");
    RemoveTestArtifacts(arena, s_Group);
    const AStringView expectedAssertCategory(LinuxObservableAssertCategory());

    const CrashTestPath spoolDirectory = SpoolDirectory(arena, s_Group);
    const pid_t childPid = fork();
    NWB_LOGSERVER_TEST_CHECK(context, childPid >= 0);
    if(childPid == 0){
        NWB::Core::Alloc::PersistentArena installArena(
            NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u),
            "NWB::Tests::LoggerServer::AssertChildInstallArena"
        );
        NWB::Core::Crash::CrashConfigT<NWB::Core::Alloc::PersistentArena> config(installArena);
        config.applicationName = AStringView("logserver_crash_tests");
        config.version = AStringView("1");
        config.buildId = AStringView("linux-assert-observe-test");
        config.spoolDirectory = spoolDirectory;

        if(!NWB::Core::Crash::InstallCrashHandler(installArena, config))
            _exit(121);

        LinuxForceAssertFalseForCrashObservation();
        _exit(122);
    }

    int status = 0;
    while(waitpid(childPid, &status, 0) < 0){
        if(errno == EINTR)
            continue;
        break;
    }

    NWB_LOGSERVER_TEST_CHECK(context, WIFSIGNALED(status));
    NWB_LOGSERVER_TEST_CHECK(context, WTERMSIG(status) == SIGABRT);

    const CrashTestPath pendingDirectory = spoolDirectory / CrashNames::s_PendingDirectoryName;
    NWB_LOGSERVER_TEST_CHECK(context, WaitForDirectory(pendingDirectory, 3000u));

    CrashTestPath assertPackageDirectory(arena);
    NWB_LOGSERVER_TEST_CHECK(
        context,
        WaitForTriggerPackage(
            arena,
            pendingDirectory,
            "assert",
            expectedAssertCategory,
            "false",
            AStringView(),
            "tests/logger_server/tests.cpp",
            assertPackageDirectory
        )
    );

    CrashTestBytes archive(arena);
    NWB_LOGSERVER_TEST_CHECK(context, BuildArchiveFromPackageDirectory(arena, assertPackageDirectory, archive));
    NWB_LOGSERVER_TEST_CHECK(context, WriteArchiveBytes(arena, s_Group, s_Stem, archive));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem), config);
    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);

    CrashTestText report(arena);
    NWB_LOGSERVER_TEST_CHECK(context, ReadServerSymbolication(arena, s_Group, s_Stem, report));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=linux"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "reason=manual"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "[event]"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "event=assert"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "expression=false"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "status=callstack_captured"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "[callstack]"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "[trigger]"));
    CrashTestText expectedCategoryLine(arena);
    expectedCategoryLine += "category=";
    expectedCategoryLine += expectedAssertCategory;
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, AStringView(expectedCategoryLine.data(), expectedCategoryLine.size())));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "tests/logger_server/tests.cpp"));
    if(LinuxExternalSymbolizerAvailable(arena)){
        NWB_LOGSERVER_TEST_CHECK(
            context,
            Contains(report, "LinuxForceAssertFalseForCrashObservation") || Contains(report, "tests/logger_server/tests.cpp")
        );
    }

    PreserveObservedReport(context, arena, report, "linux_assert");

    RemoveTestArtifacts(arena, s_Group);
#else
    static_cast<void>(context);
#endif
}

NWB_LOGSERVER_TEST_NOINLINE static void TestRecoverableErrorDiagnosticProducesObservableLoggerReport(TestContext& context){
#if defined(NWB_PLATFORM_WINDOWS) || (defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID))
    TestArena testArena;
    auto& arena = testArena.arena;
    NWB::Core::Alloc::PersistentArena installArena(
        NWB::Core::Alloc::PersistentArena::StructureAlignedSize(64u * 1024u),
        "NWB::Tests::LoggerServer::RecoverableErrorInstallArena"
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
    NWB_LOGSERVER_TEST_CHECK(context, installed);

    bool continuedAfterError = false;
    if(installed){
        CaptureDiagnosticEvent(DiagnosticEventRecord{
            .event = DiagnosticEventName::s_Error,
            .category = NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError,
            .message = s_ErrorMessage.data(),
            .file = "tests/logger_server/tests.cpp",
            .line = __LINE__,
        });
        continuedAfterError = true;
    }

    const CrashTestPath pendingDirectory = spoolDirectory / CrashNames::s_PendingDirectoryName;
    CrashTestPath errorPackageDirectory(arena);
    NWB_LOGSERVER_TEST_CHECK(
        context,
        WaitForTriggerPackage(
            arena,
            pendingDirectory,
            "error",
            NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategoryError,
            AStringView(),
            s_ErrorMessage,
            "tests/logger_server/tests.cpp",
            errorPackageDirectory
        )
    );
    NWB_LOGSERVER_TEST_CHECK(context, continuedAfterError);
    NWB::Core::Crash::UninstallCrashHandler();

    CrashTestBytes archive(arena);
    NWB_LOGSERVER_TEST_CHECK(context, BuildArchiveFromPackageDirectory(arena, errorPackageDirectory, archive));
    NWB_LOGSERVER_TEST_CHECK(context, WriteArchiveBytes(arena, s_Group, s_Stem, archive));

    NWB::Log::CrashIngestConfig ingestConfig = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem), ingestConfig);
    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);

    CrashTestText report(arena);
    NWB_LOGSERVER_TEST_CHECK(context, ReadServerSymbolication(arena, s_Group, s_Stem, report));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "[event]"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "event=error"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "category=logger_Error"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, s_ErrorMessage));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "tests/logger_server/tests.cpp"));
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=linux"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "status=callstack_captured"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "[callstack]"));
#elif defined(NWB_PLATFORM_WINDOWS)
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=windows"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "resolver=windows_pdb_minidump"));
#endif

    PreserveObservedReport(context, arena, report, "recoverable_error");

    RemoveTestArtifacts(arena, s_Group);
#else
    static_cast<void>(context);
#endif
}

static void TestAndroidCrashPackageCopiesTombstoneFrames(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_android_crash_test");
    constexpr AStringView s_Stem("android_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    archive += CrashNames::s_ArchiveHeaderText;
    const CrashTestText manifest = BuildManifest(arena, "android-test", "android");
    AppendArchiveFile(archive, CrashNames::s_ManifestFileName, AStringView(manifest.data(), manifest.size()));
    AppendArchiveFile(
        archive,
        CrashNames::s_AndroidTombstoneFileName,
        "backtrace:\n      #00 pc 0000000000012344  /data/app/lib/arm64/libnwb.so (CrashHere+16)\n      #01 pc 0000000000012450  /data/app/lib/arm64/libnwb.so (Caller+12)\n"
    );

    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem, archive));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("status=tombstone_parsed")));
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("[tombstone_callstack]")));
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("#00 pc 0000000000012344")));

    CrashTestText report(arena);
    NWB_LOGSERVER_TEST_CHECK(context, ReadServerSymbolication(arena, s_Group, s_Stem, report));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=android"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "android_tombstone=present"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "#01 pc 0000000000012450"));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestLinuxCrashPackageReportsMissingProcMaps(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_missing_maps_test");
    constexpr AStringView s_Stem("linux_missing_maps_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    archive += CrashNames::s_ArchiveHeaderText;
    const CrashTestText manifest = BuildManifest(arena, "linux-missing-maps-test", "linux");
    AppendArchiveFile(archive, CrashNames::s_ManifestFileName, AStringView(manifest.data(), manifest.size()));
    AppendArchiveFile(archive, CrashNames::s_CpuContextFileName, "instruction_pointer=4198964\n");

    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem, archive));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);

    CrashTestText report(arena);
    NWB_LOGSERVER_TEST_CHECK(context, ReadServerSymbolication(arena, s_Group, s_Stem, report));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=linux"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "proc_maps=missing"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "proc maps missing for module lookup"));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestLinuxCrashPackageReportsUnmappedInstructionPointer(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_linux_unmapped_ip_test");
    constexpr AStringView s_Stem("linux_unmapped_ip_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    archive += CrashNames::s_ArchiveHeaderText;
    const CrashTestText manifest = BuildManifest(arena, "linux-unmapped-ip-test", "linux");
    AppendArchiveFile(archive, CrashNames::s_ManifestFileName, AStringView(manifest.data(), manifest.size()));
    AppendArchiveFile(archive, CrashNames::s_CpuContextFileName, "instruction_pointer=7340032\n");
    AppendArchiveFile(archive, CrashNames::s_ProcMapsFileName, "00400000-00452000 r-xp 00000000 08:01 123 /tmp/nwb_loader\n");

    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem, archive));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);

    CrashTestText report(arena);
    NWB_LOGSERVER_TEST_CHECK(context, ReadServerSymbolication(arena, s_Group, s_Stem, report));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=linux"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "proc_maps=present"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "instruction pointer was not found in proc maps"));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestAndroidCrashPackageReportsTombstoneWithoutFrames(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_android_no_frames_test");
    constexpr AStringView s_Stem("android_no_frames_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    archive += CrashNames::s_ArchiveHeaderText;
    const CrashTestText manifest = BuildManifest(arena, "android-no-frames-test", "android");
    AppendArchiveFile(archive, CrashNames::s_ManifestFileName, AStringView(manifest.data(), manifest.size()));
    AppendArchiveFile(archive, CrashNames::s_AndroidTombstoneFileName, "pid: 7, tid: 7, name: nwb\nbacktrace:\n");

    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem, archive));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);

    CrashTestText report(arena);
    NWB_LOGSERVER_TEST_CHECK(context, ReadServerSymbolication(arena, s_Group, s_Stem, report));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=android"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "status=not_decoded"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "android_tombstone=present"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "no native frame lines were recognized"));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestWindowsCrashPackageReportsMissingMinidump(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_windows_missing_dump_test");
    constexpr AStringView s_Stem("windows_missing_dump_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    archive += CrashNames::s_ArchiveHeaderText;
    const CrashTestText manifest = BuildManifest(arena, "windows-missing-dump-test", "windows", "windows_exception", 0xC0000005u);
    AppendArchiveFile(archive, CrashNames::s_ManifestFileName, AStringView(manifest.data(), manifest.size()));

    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem, archive));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);

    CrashTestText report(arena);
    NWB_LOGSERVER_TEST_CHECK(context, ReadServerSymbolication(arena, s_Group, s_Stem, report));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=windows"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "[event]"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "event=crash"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "exception=access_violation 0x00000000c0000005"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "resolver=windows_pdb_minidump"));
#if defined(NWB_PLATFORM_WINDOWS)
    CrashTestText missingDumpMessage(arena);
    missingDumpMessage += CrashNames::s_ProcessDumpFileName;
    missingDumpMessage += " is missing or unreadable";
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, AStringView(missingDumpMessage.data(), missingDumpMessage.size())));
#else
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "only available on Windows logserver builds"));
#endif

    RemoveTestArtifacts(arena, s_Group);
}

static void TestInvalidCrashPackageIsRejected(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_invalid_crash_test");
    constexpr AStringView s_Stem("invalid_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    archive += "NWBCRASHPKG 0\n";
    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem, archive));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, !result.accepted);
    NWB_LOGSERVER_TEST_CHECK(context, result.type == NWB::Log::Type::Error);
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("Crash upload rejected")));
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("invalid crash archive header")));

    ErrorCode error;
    NWB_LOGSERVER_TEST_CHECK(context, IsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem), error) && !error);

    RemoveTestArtifacts(arena, s_Group);
}

static void TestCrashRetentionPrunesOldestAcceptedUploads(TestContext& context){
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
        NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem0, archive));
        const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem0), config);
        NWB_LOGSERVER_TEST_CHECK(context, result.accepted);
    }
    {
        CrashTestText archive(arena);
        BuildLinuxCrashArchive(arena, archive, s_Stem1);
        NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem1, archive));
        const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem1), config);
        NWB_LOGSERVER_TEST_CHECK(context, result.accepted);
    }
    {
        CrashTestText archive(arena);
        BuildLinuxCrashArchive(arena, archive, s_Stem2);
        NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem2, archive));
        const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem2), config);
        NWB_LOGSERVER_TEST_CHECK(context, result.accepted);
    }

    NWB_LOGSERVER_TEST_CHECK(context, PathIsMissing(ExtractedPackageDirectory(arena, s_Group, s_Stem0)));
    NWB_LOGSERVER_TEST_CHECK(context, PathIsMissing(RawArchivePath(arena, s_Group, s_Stem0)));
    NWB_LOGSERVER_TEST_CHECK(context, PathIsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem1)));
    NWB_LOGSERVER_TEST_CHECK(context, PathIsRegularFile(RawArchivePath(arena, s_Group, s_Stem1)));
    NWB_LOGSERVER_TEST_CHECK(context, PathIsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem2)));
    NWB_LOGSERVER_TEST_CHECK(context, PathIsRegularFile(RawArchivePath(arena, s_Group, s_Stem2)));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestAcceptedCrashWarnsWhenRawArchiveCannotBeRetained(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Group("logger_server_raw_archive_failed_test");
    constexpr AStringView s_Stem("raw_blocked_001");
    RemoveTestArtifacts(arena, s_Group);

    CrashTestText archive(arena);
    BuildLinuxCrashArchive(arena, archive, s_Stem);
    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem, archive));

    ErrorCode error;
    static_cast<void>(EnsureDirectories(StorageDirectory(arena, s_Group), error));
    NWB_LOGSERVER_TEST_CHECK(context, !error);
    NWB_LOGSERVER_TEST_CHECK(context, WriteTextFile(StorageDirectory(arena, s_Group) / NWB::Log::s_CrashRawDirectoryName, AStringView("blocked")));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);
    NWB_LOGSERVER_TEST_CHECK(context, result.type == NWB::Log::Type::Warning);
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("raw upload archive could not be retained")));
    NWB_LOGSERVER_TEST_CHECK(context, PathIsDirectory(ExtractedPackageDirectory(arena, s_Group, s_Stem)));
    NWB_LOGSERVER_TEST_CHECK(context, PathIsMissing(ArchivePath(arena, s_Group, s_Stem)));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestCrashRetentionPrunesOldestInvalidUploads(TestContext& context){
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
        archive += "NWBCRASHPKG 0\n";
        NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem0, archive));
        const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem0), config);
        NWB_LOGSERVER_TEST_CHECK(context, !result.accepted);
    }
    {
        CrashTestText archive(arena);
        archive += "NWBCRASHPKG 0\n";
        NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem1, archive));
        const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem1), config);
        NWB_LOGSERVER_TEST_CHECK(context, !result.accepted);
    }

    NWB_LOGSERVER_TEST_CHECK(context, PathIsMissing(InvalidArchivePath(arena, s_Group, s_Stem0)));
    NWB_LOGSERVER_TEST_CHECK(context, PathIsRegularFile(InvalidArchivePath(arena, s_Group, s_Stem1)));

    RemoveTestArtifacts(arena, s_Group);
}

static void TestCrashUploadAuthorizationMatchesBearerToken(TestContext& context){
    NWB_LOGSERVER_TEST_CHECK(context, NWB::Log::CrashUploadAuthorizationMatches(AStringView(), nullptr));
    NWB_LOGSERVER_TEST_CHECK(context, NWB::Log::CrashUploadAuthorizationMatches(AStringView(), "bad"));
    NWB_LOGSERVER_TEST_CHECK(context, NWB::Log::CrashUploadAuthorizationMatches("secret-token", "Bearer secret-token"));
    NWB_LOGSERVER_TEST_CHECK(context, !NWB::Log::CrashUploadAuthorizationMatches("secret-token", nullptr));
    NWB_LOGSERVER_TEST_CHECK(context, !NWB::Log::CrashUploadAuthorizationMatches("secret-token", "secret-token"));
    NWB_LOGSERVER_TEST_CHECK(context, !NWB::Log::CrashUploadAuthorizationMatches("secret-token", "Bearer wrong"));
    NWB_LOGSERVER_TEST_CHECK(context, !NWB::Log::CrashUploadAuthorizationMatches("secret-token", "Bearer secret-token "));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_LOGSERVER_TEST_CHECK
#undef NWB_LOGSERVER_TEST_NOINLINE


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("logserver crash", [](NWB::Tests::TestContext& context){
    __hidden_logger_server_tests::TestLinuxCrashPackageMapsInstructionPointer(context);
    __hidden_logger_server_tests::TestLinuxCrashPackageSymbolicatesSelfFrame(context);
    __hidden_logger_server_tests::TestLinuxAssertCrashProducesObservableLoggerReport(context);
    __hidden_logger_server_tests::TestRecoverableErrorDiagnosticProducesObservableLoggerReport(context);
    __hidden_logger_server_tests::TestAndroidCrashPackageCopiesTombstoneFrames(context);
    __hidden_logger_server_tests::TestLinuxCrashPackageReportsMissingProcMaps(context);
    __hidden_logger_server_tests::TestLinuxCrashPackageReportsUnmappedInstructionPointer(context);
    __hidden_logger_server_tests::TestAndroidCrashPackageReportsTombstoneWithoutFrames(context);
    __hidden_logger_server_tests::TestWindowsCrashPackageReportsMissingMinidump(context);
    __hidden_logger_server_tests::TestInvalidCrashPackageIsRejected(context);
    __hidden_logger_server_tests::TestCrashRetentionPrunesOldestAcceptedUploads(context);
    __hidden_logger_server_tests::TestAcceptedCrashWarnsWhenRawArchiveCannotBeRetained(context);
    __hidden_logger_server_tests::TestCrashRetentionPrunesOldestInvalidUploads(context);
    __hidden_logger_server_tests::TestCrashUploadAuthorizationMatchesBearerToken(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

