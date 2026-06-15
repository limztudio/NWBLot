// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_helpers.h"

#include <tests/filesystem_helpers.h>

#include <core/crash/package_names.h>
#include <global/filesystem/directory_iterator.h>
#include <global/filesystem/operations.h>
#include <global/thread.h>
#include <logger/server/crash_paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{
namespace LoggerServerCrash{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = Core::Crash::PackageNames;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashTestPath CrashRootDirectory(Core::Alloc::GlobalArena& arena){
    CrashTestPath executableDirectory(arena);
    if(GetExecutableDirectory(executableDirectory))
        return executableDirectory / CrashNames::s_DefaultRootDirectoryName;

    return CrashTestPath(arena, CrashNames::s_DefaultRootDirectoryName);
}

CrashTestPath TestRootDirectory(Core::Alloc::GlobalArena& arena){
    return CrashRootDirectory(arena) / "test_storage";
}

CrashTestPath TestCaseDirectory(Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return TestRootDirectory(arena) / testGroup;
}

CrashTestPath StorageDirectory(Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return TestCaseDirectory(arena, testGroup) / "storage";
}

CrashTestPath SpoolDirectory(Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return TestCaseDirectory(arena, testGroup) / "spool";
}

CrashTestPath ArchiveInputDirectory(Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return TestCaseDirectory(arena, testGroup) / "input";
}

CrashTestPath ArchiveFileName(Core::Alloc::GlobalArena& arena, const AStringView stem){
    CrashTestPath fileName(arena, stem);
    fileName.replace_extension(Log::s_CrashUploadArchiveFileExtension);
    return fileName;
}

CrashTestPath ArchivePath(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return ArchiveInputDirectory(arena, testGroup) / ArchiveFileName(arena, stem);
}

CrashTestPath ExtractedPackageDirectory(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return StorageDirectory(arena, testGroup) / Log::s_CrashExtractedDirectoryName / stem;
}

CrashTestPath RawArchivePath(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return StorageDirectory(arena, testGroup) / Log::s_CrashRawDirectoryName / ArchiveFileName(arena, stem);
}

CrashTestPath InvalidArchivePath(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return StorageDirectory(arena, testGroup) / Log::s_CrashInvalidDirectoryName / ArchiveFileName(arena, stem);
}

void RemoveTestArtifacts(Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    ErrorCode error;
    static_cast<void>(RemoveAllIfExists(TestCaseDirectory(arena, testGroup), error));
}

void AppendArchiveFile(CrashTestText& archive, const AStringView relativePath, const AStringView content){
    char sizeBuffer[32] = {};
    archive += CrashNames::s_ArchiveFileHeaderPrefix;
    archive += relativePath;
    archive += " ";
    archive += FormatDecimal(content.size(), sizeBuffer);
    archive += "\n";
    archive += content;
    archive += CrashNames::s_ArchiveEntryEndText;
}

CrashTestText BuildManifest(
    Core::Alloc::GlobalArena& arena,
    const AStringView crashId,
    const AStringView platform,
    const AStringView event,
    const AStringView reasonKind,
    const u64 reasonCode
){
    CrashTestText manifest(arena);
    char reasonCodeBuffer[32] = {};
    manifest += "{\n";
    manifest += "  \"format\": \"";
    manifest += CrashNames::s_ManifestFormatValue;
    manifest += "\",\n";
    manifest += "  \"crash_id\": \"";
    manifest += crashId;
    manifest += "\",\n  \"application\": \"logger_server_test\",\n";
    manifest += "  \"version\": \"1\",\n";
    manifest += "  \"build_id\": \"unit-test\",\n";
    manifest += "  \"abi\": \"test-abi\",\n";
    manifest += "  \"platform\": \"";
    manifest += platform;
    manifest += "\",\n  \"reason_kind\": \"";
    manifest += reasonKind;
    manifest += "\",\n  \"reason_code\": ";
    manifest += FormatDecimal(static_cast<usize>(reasonCode), reasonCodeBuffer);
    manifest += ",\n  \"process_id\": 1,\n";
    manifest += "  \"thread_id\": 7,\n";
    manifest += "  \"has_exception_context\": false,\n";
    manifest += "  \"fault_address\": 0,\n";
    manifest += "  \"instruction_pointer\": 0,\n";
    manifest += "  \"stack_pointer\": 0,\n";
    manifest += "  \"frame_pointer\": 0,\n";
    manifest += "  \"event\": \"";
    manifest += event;
    manifest += "\",\n";
    manifest += "  \"trigger_category\": \"\",\n";
    manifest += "  \"trigger_expression\": \"\",\n";
    manifest += "  \"trigger_message\": \"\",\n";
    manifest += "  \"trigger_file\": \"\",\n";
    manifest += "  \"trigger_line\": 0,\n";
    manifest += "  \"dump_detail_mode\": \"small\",\n";
    manifest += "  \"gpu_dumps_enabled\": false,\n";
    manifest += "  \"artifact_strategy\": \"unit_test\",\n";
    manifest += "  \"handler_lifetime\": \"unit_test\"\n";
    manifest += "}\n";
    return manifest;
}

bool WriteArchive(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const CrashTestText& archive){
    ErrorCode error;
    static_cast<void>(EnsureDirectories(ArchiveInputDirectory(arena, testGroup), error));
    if(error)
        return false;

    return WriteTextFile(ArchivePath(arena, testGroup, stem), AStringView(archive.data(), archive.size()));
}

static void AppendArchiveBytes(CrashTestBytes& archive, const void* const bytes, const usize byteCount){
    const auto* const begin = static_cast<const u8*>(bytes);
    archive.insert(archive.end(), begin, begin + byteCount);
}

static void AppendArchiveText(CrashTestBytes& archive, const AStringView text){
    AppendArchiveBytes(archive, text.data(), text.size());
}

static void AppendArchiveText(CrashTestBytes& archive, const char* const text){
    if(text)
        AppendArchiveText(archive, AStringView(text));
}

static void AppendArchiveUnsigned(CrashTestBytes& archive, const u64 value){
    char buffer[32] = {};
    AppendArchiveText(archive, FormatDecimal(static_cast<usize>(value), buffer));
}

bool WriteArchiveBytes(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const CrashTestBytes& archive){
    ErrorCode error;
    static_cast<void>(EnsureDirectories(ArchiveInputDirectory(arena, testGroup), error));
    if(error)
        return false;

    return WriteBinaryFile(ArchivePath(arena, testGroup, stem), archive);
}

bool BuildArchiveFromPackageDirectory(Core::Alloc::GlobalArena& arena, const CrashTestPath& packageDirectory, CrashTestBytes& outArchive){
    outArchive.clear();
    AppendArchiveText(outArchive, CrashNames::s_ArchiveHeaderText);

    ErrorCode error;
    RecursiveDirectoryIterator directory(packageDirectory, error);
    if(error)
        return false;

    bool wroteFile = false;
    for(const auto& entry : directory){
        ErrorCode entryError;
        if(!entry.is_regular_file(entryError) || entryError)
            continue;

        CrashTestBytes fileBytes(arena);
        if(!ReadBinaryFile(entry.path(), fileBytes, entryError))
            return false;

        const CrashTestText relativePath = PathToGenericString<char>(arena, entry.path().lexically_relative(packageDirectory));
        AppendArchiveText(outArchive, CrashNames::s_ArchiveFileHeaderPrefix);
        AppendArchiveText(outArchive, AStringView(relativePath.data(), relativePath.size()));
        AppendArchiveText(outArchive, " ");
        AppendArchiveUnsigned(outArchive, fileBytes.size());
        AppendArchiveText(outArchive, "\n");
        if(!fileBytes.empty())
            AppendArchiveBytes(outArchive, fileBytes.data(), fileBytes.size());
        AppendArchiveText(outArchive, CrashNames::s_ArchiveEntryEndText);
        wroteFile = true;
    }

    return wroteFile;
}

bool ReadServerSymbolication(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, CrashTestText& outReport){
    return ReadTextFile(ExtractedPackageDirectory(arena, testGroup, stem) / Log::s_ServerSymbolicationFileName, outReport);
}

Log::CrashIngestConfig MakeIngestConfig(Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    Log::CrashIngestConfig config(arena);
    config.storageDirectory = StorageDirectory(arena, testGroup);
    return config;
}

static bool TriggerPackageContains(
    const CrashTestPath& packageDirectory,
    const AStringView category,
    const AStringView expression,
    const AStringView message,
    const AStringView file
){
    const CrashTestPath triggerPath = packageDirectory / CrashNames::s_TriggerFileName;
    if(!category.empty()){
        CrashTestText needle(triggerPath.arena());
        needle += "category=";
        needle += category;
        if(!TextFileContains(triggerPath, AStringView(needle.data(), needle.size())))
            return false;
    }
    if(!expression.empty()){
        CrashTestText needle(triggerPath.arena());
        needle += "expression=";
        needle += expression;
        if(!TextFileContains(triggerPath, AStringView(needle.data(), needle.size())))
            return false;
    }
    if(!message.empty()){
        CrashTestText needle(triggerPath.arena());
        needle += "message=";
        needle += message;
        if(!TextFileContains(triggerPath, AStringView(needle.data(), needle.size())))
            return false;
    }
    if(!file.empty() && !TextFileContains(triggerPath, file))
        return false;

    return true;
}

static bool FindTriggerPackage(
    Core::Alloc::GlobalArena& arena,
    const CrashTestPath& pendingDirectory,
    const AStringView category,
    const AStringView expression,
    const AStringView message,
    const AStringView file,
    CrashTestPath& outPackageDirectory
){
    ErrorCode error;
    DirectoryIterator directory(pendingDirectory, error);
    if(error)
        return false;

    for(const auto& entry : directory){
        ErrorCode entryError;
        if(!IsDirectory(entry.path(), entryError) || entryError)
            continue;
        if(!TriggerPackageContains(entry.path(), category, expression, message, file))
            continue;

        outPackageDirectory = entry.path();
        return true;
    }

    static_cast<void>(arena);
    return false;
}

bool WaitForTriggerPackage(
    Core::Alloc::GlobalArena& arena,
    const CrashTestPath& pendingDirectory,
    const AStringView category,
    const AStringView expression,
    const AStringView message,
    const AStringView file,
    CrashTestPath& outPackageDirectory
){
    constexpr u32 s_TimeoutMilliseconds = 3000u;
    constexpr u32 s_PollMilliseconds = 10u;
    for(u32 elapsedMilliseconds = 0u; elapsedMilliseconds <= s_TimeoutMilliseconds; elapsedMilliseconds += s_PollMilliseconds){
        if(
            PathIsDirectory(pendingDirectory)
            && FindTriggerPackage(arena, pendingDirectory, category, expression, message, file, outPackageDirectory)
        )
            return true;
        SleepMS(s_PollMilliseconds);
    }
    return false;
}

void BuildLinuxCrashArchive(Core::Alloc::GlobalArena& arena, CrashTestText& archive, const AStringView crashId){
    archive += CrashNames::s_ArchiveHeaderText;
    const CrashTestText manifest = BuildManifest(arena, crashId, "linux", "crash", "signal", 11u);
    AppendArchiveFile(archive, CrashNames::s_ManifestFileName, AStringView(manifest.data(), manifest.size()));
    AppendArchiveFile(archive, CrashNames::s_CpuContextFileName, "fault_address=0\ninstruction_pointer=4198964\nstack_pointer=0\nframe_pointer=0\n");
    AppendArchiveFile(archive, CrashNames::s_CallstackFileName, "#0 0x0000000000401234\n#1 0x0000000000401240\n");
    AppendArchiveFile(archive, CrashNames::s_ProcMapsFileName, "00400000-00452000 r-xp 00000000 08:01 123 /tmp/nwb_loader\n");
}

bool Contains(const CrashTestText& text, const AStringView needle){
    return AStringView(text.data(), text.size()).find(needle) != AStringView::npos;
}

bool ContainsMessage(const Log::LogString& text, const TStringView needle){
    return TStringView(text.data(), text.size()).find(needle) != TStringView::npos;
}

static CrashTestPath ObservedReportPath(Core::Alloc::GlobalArena& arena, const CrashTestPath& basePath, const AStringView suffix){
    if(suffix.empty())
        return basePath;

    CrashTestText fileName(arena);
    const CrashTestText stem = PathToString<char>(arena, basePath.stem());
    const CrashTestText extension = PathToString<char>(arena, basePath.extension());
    fileName += AStringView(stem.data(), stem.size());
    fileName += ".";
    fileName += suffix;
    fileName += AStringView(extension.data(), extension.size());
    return basePath.parent_path() / AStringView(fileName.data(), fileName.size());
}

void PreserveObservedReport(TestContext& context, Core::Alloc::GlobalArena& arena, const CrashTestText& report, const AStringView suffix){
    const char* const outputPathText = NWB_GETENV("NWB_LOGSERVER_CRASH_TEST_OBSERVE_PATH");
    if(!outputPathText || outputPathText[0] == 0)
        return;

    const CrashTestPath baseOutputPath(arena, AStringView(outputPathText));
    const CrashTestPath outputPath = ObservedReportPath(arena, baseOutputPath, suffix);
    ErrorCode error;
    static_cast<void>(EnsureDirectories(outputPath.parent_path(), error));
    NWB_TEST_CHECK(context, !error);
    NWB_TEST_CHECK(context, WriteTextFile(outputPath, AStringView(report.data(), report.size())));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

