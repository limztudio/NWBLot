// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/test_context.h>

#include <logger/server/crash_ingest.h>
#include <logger/server/crash_symbolicate.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_server_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using TestArena = NWB::Tests::TestArena<struct LoggerServerCrashTestsTag>;
using CrashTestText = AString<NWB::Core::Alloc::GlobalArena>;
using CrashTestPath = Path<NWB::Core::Alloc::GlobalArena>;

#define NWB_LOGSERVER_TEST_CHECK NWB_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static CrashTestPath CrashRootDirectory(NWB::Core::Alloc::GlobalArena& arena){
    CrashTestPath executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / "crashes";

    return CrashTestPath(arena, "crashes");
}

[[nodiscard]] static CrashTestPath ArchiveInputDirectory(NWB::Core::Alloc::GlobalArena& arena){
    return CrashRootDirectory(arena) / "test_input";
}

[[nodiscard]] static CrashTestPath ArchivePath(NWB::Core::Alloc::GlobalArena& arena, const AStringView stem){
    CrashTestPath fileName(arena, stem);
    fileName.replace_extension("nwbcrashpkg");
    return ArchiveInputDirectory(arena) / fileName;
}

[[nodiscard]] static CrashTestPath ExtractedPackageDirectory(NWB::Core::Alloc::GlobalArena& arena, const AStringView stem){
    return CrashRootDirectory(arena) / "packages" / stem;
}

[[nodiscard]] static CrashTestPath RawArchivePath(NWB::Core::Alloc::GlobalArena& arena, const AStringView stem){
    return CrashRootDirectory(arena) / "raw" / CrashTestPath(arena, stem).replace_extension("nwbcrashpkg");
}

[[nodiscard]] static CrashTestPath InvalidArchivePath(NWB::Core::Alloc::GlobalArena& arena, const AStringView stem){
    return CrashRootDirectory(arena) / "invalid" / CrashTestPath(arena, stem).replace_extension("nwbcrashpkg");
}

static void RemoveTestArtifacts(NWB::Core::Alloc::GlobalArena& arena, const AStringView stem){
    ErrorCode error;
    static_cast<void>(RemoveAllIfExists(ArchivePath(arena, stem), error));
    error.clear();
    static_cast<void>(RemoveAllIfExists(ExtractedPackageDirectory(arena, stem), error));
    error.clear();
    static_cast<void>(RemoveAllIfExists(RawArchivePath(arena, stem), error));
    error.clear();
    static_cast<void>(RemoveAllIfExists(InvalidArchivePath(arena, stem), error));
}

static void AppendArchiveFile(CrashTestText& archive, const AStringView relativePath, const AStringView content){
    char sizeBuffer[32] = {};
    archive += "FILE ";
    archive += relativePath;
    archive += " ";
    archive += FormatDecimal(content.size(), sizeBuffer);
    archive += "\n";
    archive += content;
    archive += "\nEND\n";
}

[[nodiscard]] static CrashTestText BuildManifest(NWB::Core::Alloc::GlobalArena& arena, const AStringView crashId, const AStringView platform){
    CrashTestText manifest(arena);
    manifest += "{\n";
    manifest += "  \"format\": \"nwb-crash-package-v1\",\n";
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

[[nodiscard]] static bool WriteArchive(NWB::Core::Alloc::GlobalArena& arena, const AStringView stem, const CrashTestText& archive){
    ErrorCode error;
    static_cast<void>(EnsureDirectories(ArchiveInputDirectory(arena), error));
    if(error)
        return false;

    return WriteTextFile(ArchivePath(arena, stem), AStringView(archive.data(), archive.size()));
}

[[nodiscard]] static bool ReadServerSymbolication(NWB::Core::Alloc::GlobalArena& arena, const AStringView stem, CrashTestText& outReport){
    return ReadTextFile(ExtractedPackageDirectory(arena, stem) / "server_symbolication.txt", outReport);
}

[[nodiscard]] static bool Contains(const CrashTestText& text, const AStringView needle){
    return AStringView(text.data(), text.size()).find(needle) != AStringView::npos;
}

[[nodiscard]] static bool ContainsMessage(const NWB::Log::LogString& text, const TStringView needle){
    return TStringView(text.data(), text.size()).find(needle) != TStringView::npos;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestLinuxCrashPackageMapsInstructionPointer(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Stem("logger_server_linux_crash_test");
    RemoveTestArtifacts(arena, s_Stem);

    CrashTestText archive(arena);
    archive += "NWBCRASHPKG 1\n";
    const CrashTestText manifest = BuildManifest(arena, "linux-test", "linux");
    AppendArchiveFile(archive, "manifest.json", AStringView(manifest.data(), manifest.size()));
    AppendArchiveFile(archive, "cpu_context.txt", "fault_address=0\ninstruction_pointer=4198964\nstack_pointer=0\nframe_pointer=0\n");
    AppendArchiveFile(archive, "proc_maps.txt", "00400000-00452000 r-xp 00000000 08:01 123 /tmp/nwb_loader\n");

    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Stem, archive));

    NWB::Log::CrashSymbolicationConfig config(arena);
    config.symbolStoreDirectory = CrashRootDirectory(arena) / "test_symbols";
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);
    NWB_LOGSERVER_TEST_CHECK(context, result.type == NWB::Log::Type::EssentialInfo);
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("module_relative_ip=0x0000000000001234")));

    CrashTestText report(arena);
    NWB_LOGSERVER_TEST_CHECK(context, ReadServerSymbolication(arena, s_Stem, report));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=linux"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "symbol_store="));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "instruction_pointer_module=/tmp/nwb_loader"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "module_relative_ip=0x0000000000001234"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "core_artifact=missing"));

    RemoveTestArtifacts(arena, s_Stem);
}

static void TestAndroidCrashPackageCopiesTombstoneFrames(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Stem("logger_server_android_crash_test");
    RemoveTestArtifacts(arena, s_Stem);

    CrashTestText archive(arena);
    archive += "NWBCRASHPKG 1\n";
    const CrashTestText manifest = BuildManifest(arena, "android-test", "android");
    AppendArchiveFile(archive, "manifest.json", AStringView(manifest.data(), manifest.size()));
    AppendArchiveFile(
        archive,
        "android_tombstone.txt",
        "backtrace:\n      #00 pc 0000000000012344  /data/app/lib/arm64/libnwb.so (CrashHere+16)\n      #01 pc 0000000000012450  /data/app/lib/arm64/libnwb.so (Caller+12)\n"
    );

    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Stem, archive));

    NWB::Log::CrashSymbolicationConfig config(arena);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, result.accepted);
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("status=tombstone_parsed")));
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("[tombstone_callstack]")));
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("#00 pc 0000000000012344")));

    CrashTestText report(arena);
    NWB_LOGSERVER_TEST_CHECK(context, ReadServerSymbolication(arena, s_Stem, report));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "platform=android"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "android_tombstone=present"));
    NWB_LOGSERVER_TEST_CHECK(context, Contains(report, "#01 pc 0000000000012450"));

    RemoveTestArtifacts(arena, s_Stem);
}

static void TestInvalidCrashPackageIsRejected(TestContext& context){
    TestArena testArena;
    auto& arena = testArena.arena;
    constexpr AStringView s_Stem("logger_server_invalid_crash_test");
    RemoveTestArtifacts(arena, s_Stem);

    CrashTestText archive(arena);
    archive += "NWBCRASHPKG 0\n";
    NWB_LOGSERVER_TEST_CHECK(context, WriteArchive(arena, s_Stem, archive));

    NWB::Log::CrashSymbolicationConfig config(arena);
    const NWB::Log::CrashIngestResult result = NWB::Log::ProcessCrashUpload(arena, ArchivePath(arena, s_Stem), config);

    NWB_LOGSERVER_TEST_CHECK(context, !result.accepted);
    NWB_LOGSERVER_TEST_CHECK(context, result.type == NWB::Log::Type::Error);
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("Crash upload rejected")));
    NWB_LOGSERVER_TEST_CHECK(context, ContainsMessage(result.message, NWB_TEXT("invalid crash archive header")));

    ErrorCode error;
    NWB_LOGSERVER_TEST_CHECK(context, IsRegularFile(InvalidArchivePath(arena, s_Stem), error) && !error);

    RemoveTestArtifacts(arena, s_Stem);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_LOGSERVER_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("logserver crash", [](NWB::Tests::TestContext& context){
    __hidden_logger_server_tests::TestLinuxCrashPackageMapsInstructionPointer(context);
    __hidden_logger_server_tests::TestAndroidCrashPackageCopiesTombstoneFrames(context);
    __hidden_logger_server_tests::TestInvalidCrashPackageIsRejected(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

