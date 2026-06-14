// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/test_context.h>

#include <core/crash/package_names.h>
#include <logger/server/crash_auth.h>
#include <logger/server/crash_ingest.h>
#include <logger/server/crash_paths.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <cstdlib>
#include <dlfcn.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_server_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using TestArena = NWB::Tests::TestArena<struct LoggerServerCrashTestsTag>;
using CrashTestText = AString<NWB::Core::Alloc::GlobalArena>;
using CrashTestPath = Path<NWB::Core::Alloc::GlobalArena>;
namespace CrashNames = NWB::Core::Crash::PackageNames;

#define NWB_LOGSERVER_TEST_CHECK NWB_TEST_CHECK


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

[[nodiscard]] static CrashTestPath StorageDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return TestCaseDirectory(arena, testGroup) / "storage";
}

[[nodiscard]] static CrashTestPath ArchiveInputDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return TestCaseDirectory(arena, testGroup) / "input";
}

[[nodiscard]] static CrashTestPath ArchiveFileName(NWB::Core::Alloc::GlobalArena& arena, const AStringView stem){
    CrashTestPath fileName(arena, stem);
    fileName.replace_extension(NWB::Log::s_CrashUploadArchiveFileExtension);
    return fileName;
}

[[nodiscard]] static CrashTestPath ArchivePath(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return ArchiveInputDirectory(arena, testGroup) / ArchiveFileName(arena, stem);
}

[[nodiscard]] static CrashTestPath ExtractedPackageDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return StorageDirectory(arena, testGroup) / NWB::Log::s_CrashExtractedDirectoryName / stem;
}

[[nodiscard]] static CrashTestPath RawArchivePath(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return StorageDirectory(arena, testGroup) / NWB::Log::s_CrashRawDirectoryName / ArchiveFileName(arena, stem);
}

[[nodiscard]] static CrashTestPath InvalidArchivePath(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return StorageDirectory(arena, testGroup) / NWB::Log::s_CrashInvalidDirectoryName / ArchiveFileName(arena, stem);
}

static void RemoveTestArtifacts(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    ErrorCode error;
    static_cast<void>(RemoveAllIfExists(TestCaseDirectory(arena, testGroup), error));
}

static void AppendArchiveFile(CrashTestText& archive, const AStringView relativePath, const AStringView content){
    char sizeBuffer[32] = {};
    archive += CrashNames::s_ArchiveFileHeaderPrefix;
    archive += relativePath;
    archive += " ";
    archive += FormatDecimal(content.size(), sizeBuffer);
    archive += "\n";
    archive += content;
    archive += CrashNames::s_ArchiveEntryEndText;
}

[[nodiscard]] static CrashTestText BuildManifest(NWB::Core::Alloc::GlobalArena& arena, const AStringView crashId, const AStringView platform){
    CrashTestText manifest(arena);
    manifest += "{\n";
    manifest += "  \"format\": \"";
    manifest += CrashNames::s_ManifestFormatValue;
    manifest += "\",\n";
    manifest += "  \"crash_id\": \"";
    manifest += crashId;
    manifest += "\",\n";
    manifest += "  \"platform\": \"";
    manifest += platform;
    manifest += "\",\n";
    manifest += "  \"reason_kind\": \"manual\",\n";
    manifest += "  \"artifact_strategy\": \"unit_test\",\n";
    manifest += "  \"thread_id\": 7\n";
    manifest += "}\n";
    return manifest;
}

[[nodiscard]] static bool WriteArchive(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const CrashTestText& archive){
    ErrorCode error;
    static_cast<void>(EnsureDirectories(ArchiveInputDirectory(arena, testGroup), error));
    if(error)
        return false;

    return WriteTextFile(ArchivePath(arena, testGroup, stem), AStringView(archive.data(), archive.size()));
}

[[nodiscard]] static bool ReadServerSymbolication(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, CrashTestText& outReport){
    return ReadTextFile(ExtractedPackageDirectory(arena, testGroup, stem) / NWB::Log::s_ServerSymbolicationFileName, outReport);
}

[[nodiscard]] static NWB::Log::CrashIngestConfig MakeIngestConfig(NWB::Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    NWB::Log::CrashIngestConfig config(arena);
    config.storageDirectory = StorageDirectory(arena, testGroup);
    return config;
}

[[nodiscard]] static bool DirectoryExists(const CrashTestPath& path){
    ErrorCode error;
    return IsDirectory(path, error) && !error;
}

[[nodiscard]] static bool RegularFileExists(const CrashTestPath& path){
    ErrorCode error;
    return IsRegularFile(path, error) && !error;
}

[[nodiscard]] static bool PathIsMissing(const CrashTestPath& path){
    ErrorCode error;
    return !FileExists(path, error) && !error;
}

static void BuildLinuxCrashArchive(NWB::Core::Alloc::GlobalArena& arena, CrashTestText& archive, const AStringView crashId){
    archive += CrashNames::s_ArchiveHeaderText;
    const CrashTestText manifest = BuildManifest(arena, crashId, "linux");
    AppendArchiveFile(archive, CrashNames::s_ManifestFileName, AStringView(manifest.data(), manifest.size()));
    AppendArchiveFile(archive, CrashNames::s_CpuContextFileName, "fault_address=0\ninstruction_pointer=4198964\nstack_pointer=0\nframe_pointer=0\n");
    AppendArchiveFile(archive, CrashNames::s_CallstackFileName, "#0 0x0000000000401234\n#1 0x0000000000401240\n");
    AppendArchiveFile(archive, CrashNames::s_ProcMapsFileName, "00400000-00452000 r-xp 00000000 08:01 123 /tmp/nwb_loader\n");
}

[[nodiscard]] static bool Contains(const CrashTestText& text, const AStringView needle){
    return AStringView(text.data(), text.size()).find(needle) != AStringView::npos;
}

[[nodiscard]] static bool ContainsMessage(const NWB::Log::LogString& text, const TStringView needle){
    return TStringView(text.data(), text.size()).find(needle) != TStringView::npos;
}

static void PreserveObservedReport(TestContext& context, NWB::Core::Alloc::GlobalArena& arena, const CrashTestText& report){
    const char* const outputPathText = NWB_GETENV("NWB_LOGSERVER_CRASH_TEST_OBSERVE_PATH");
    if(!outputPathText || outputPathText[0] == 0)
        return;

    const CrashTestPath outputPath(arena, AStringView(outputPathText));
    ErrorCode error;
    static_cast<void>(EnsureDirectories(outputPath.parent_path(), error));
    NWB_LOGSERVER_TEST_CHECK(context, !error);
    NWB_LOGSERVER_TEST_CHECK(context, WriteTextFile(outputPath, AStringView(report.data(), report.size())));
}

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
[[gnu::noinline]] static u64 LinuxCrashSymbolicationProbe(){
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

[[nodiscard]] static bool LinuxExternalSymbolizerAvailable(){
    return std::system("command -v llvm-symbolizer >/dev/null 2>&1 || command -v addr2line >/dev/null 2>&1") == 0;
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
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "status=callstack_captured"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "symbol_store="));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "symbol_store_status=missing"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "instruction_pointer_module=/tmp/nwb_loader"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "module_relative_ip=0x0000000000001234"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "[callstack]"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "#0 0x0000000000401234 /tmp/nwb_loader+0x0000000000001234"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "#1 0x0000000000401240 /tmp/nwb_loader+0x0000000000001240"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "core_artifact=missing"));

    PreserveObservedReport(context, arena, report);

    RemoveTestArtifacts(arena, s_Group);
}

static void TestLinuxCrashPackageSymbolicatesSelfFrame(TestContext& context){
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    if(!LinuxExternalSymbolizerAvailable())
        return;

    TestArena testArena;
    auto& arena = testArena.arena;
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

    PreserveObservedReport(context, arena, report);

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
    const CrashTestText manifest = BuildManifest(arena, "windows-missing-dump-test", "windows");
    AppendArchiveFile(archive, CrashNames::s_ManifestFileName, AStringView(manifest.data(), manifest.size()));

    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Group, s_Stem, archive));

    NWB::Log::CrashIngestConfig config = MakeIngestConfig(arena, s_Group);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Group, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);

    CrashTestText report(arena);
    NWB_LOGSERVER_TEST_CHECK(context, ReadServerSymbolication(arena, s_Group, s_Stem, report));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=windows"));
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
    NWB_LOGSERVER_TEST_CHECK(context, DirectoryExists(ExtractedPackageDirectory(arena, s_Group, s_Stem1)));
    NWB_LOGSERVER_TEST_CHECK(context, RegularFileExists(RawArchivePath(arena, s_Group, s_Stem1)));
    NWB_LOGSERVER_TEST_CHECK(context, DirectoryExists(ExtractedPackageDirectory(arena, s_Group, s_Stem2)));
    NWB_LOGSERVER_TEST_CHECK(context, RegularFileExists(RawArchivePath(arena, s_Group, s_Stem2)));

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
    NWB_LOGSERVER_TEST_CHECK(context, DirectoryExists(ExtractedPackageDirectory(arena, s_Group, s_Stem)));
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
    NWB_LOGSERVER_TEST_CHECK(context, RegularFileExists(InvalidArchivePath(arena, s_Group, s_Stem1)));

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("logserver crash", [](NWB::Tests::TestContext& context){
    __hidden_logger_server_tests::TestLinuxCrashPackageMapsInstructionPointer(context);
    __hidden_logger_server_tests::TestLinuxCrashPackageSymbolicatesSelfFrame(context);
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

