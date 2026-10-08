// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_test_helpers.h"

#include <gtest/gtest.h>

#include <core/crash/package_internal.h>
#include <core/crash/package_names.h>
#include <global/environment.h>
#include <global/filesystem/directory_iterator.h>
#include <global/filesystem/operations.h>
#include <global/thread.h>
#include <logger/server/crash_paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerServerCrash{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = Core::Crash::PackageNames;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashTestPath CrashRootDirectory(Core::Alloc::GlobalArena& arena){
    const auto executableDirectory = GetExecutableDirectory(arena);
    if(executableDirectory)
        return *executableDirectory / CrashNames::s_DefaultRootDirectoryName;

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
    fileName.replaceExtension(Log::s_CrashUploadArchiveFileExtension);
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
    EXPECT_TRUE(RemoveAllIfExists(TestCaseDirectory(arena, testGroup)));
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

void BeginArchiveWithManifest(
    Core::Alloc::GlobalArena& arena,
    CrashTestText& archive,
    const AStringView crashId,
    const AStringView platform,
    const AStringView event,
    const AStringView reasonKind,
    const u64 reasonCode,
    const ManifestEventField::Enum eventField,
    const ManifestTriggerFields& trigger
){
    archive += CrashNames::s_ArchiveHeaderText;
    const CrashTestText manifest = BuildManifest(arena, crashId, platform, event, reasonKind, reasonCode, eventField, trigger);
    AppendArchiveFile(archive, CrashNames::s_ManifestFileName, AStringView(manifest.data(), manifest.size()));
}

CrashTestText BuildManifest(
    Core::Alloc::GlobalArena& arena,
    const AStringView crashId,
    const AStringView platform,
    const AStringView event,
    const AStringView reasonKind,
    const u64 reasonCode,
    const ManifestEventField::Enum eventField,
    const ManifestTriggerFields& trigger
){
    CrashTestText manifest(arena);
    char reasonCodeBuffer[32] = {};
    char triggerLineBuffer[32] = {};
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
    if(eventField == ManifestEventField::Include){
        manifest += "  \"event\": \"";
        manifest += event;
        manifest += "\",\n";
    }
    manifest += "  \"trigger_category\": \"";
    manifest += trigger.category;
    manifest += "\",\n";
    manifest += "  \"trigger_expression\": \"";
    manifest += trigger.expression;
    manifest += "\",\n";
    manifest += "  \"trigger_message\": \"";
    manifest += trigger.message;
    manifest += "\",\n";
    manifest += "  \"trigger_file\": \"";
    manifest += trigger.file;
    manifest += "\",\n";
    manifest += "  \"trigger_line\": ";
    manifest += FormatDecimal(static_cast<usize>(trigger.line), triggerLineBuffer);
    manifest += ",\n";
    manifest += "  \"dump_detail_mode\": \"small\",\n";
    manifest += "  \"artifact_strategy\": \"unit_test\",\n";
    manifest += "  \"handler_lifetime\": \"unit_test\"\n";
    manifest += "}\n";
    return manifest;
}

bool WriteArchive(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const CrashTestText& archive){
    if(!EnsureDirectories(ArchiveInputDirectory(arena, testGroup)))
        return false;

    return WriteTextFile(ArchivePath(arena, testGroup, stem), AStringView(archive.data(), archive.size()));
}

bool WriteArchiveBytes(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem, const CrashTestBytes& archive){
    if(!EnsureDirectories(ArchiveInputDirectory(arena, testGroup)))
        return false;

    return WriteBinaryFile(ArchivePath(arena, testGroup, stem), archive);
}

Expected<CrashTestBytes> BuildArchiveFromPackageDirectory(Core::Alloc::GlobalArena& arena, const CrashTestPath& packageDirectory){
    return Core::Crash::Detail::BuildPackageArchive(arena, packageDirectory);
}

Expected<CrashTestText, ErrorCode> ReadServerSymbolication(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    CrashTestText report(arena);
    const auto result = ReadTextFile(ExtractedPackageDirectory(arena, testGroup, stem) / Log::s_ServerSymbolicationFileName, report);
    if(!result)
        return MakeUnexpected(result.error());
    return report;
}

Log::CrashIngestConfig MakeIngestConfig(Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    Log::CrashIngestConfig config(arena);
    config.storageDirectory = StorageDirectory(arena, testGroup);
    return config;
}

static bool ManifestContainsStringValue(Core::Alloc::GlobalArena& arena, const CrashTestText& manifest, const AStringView key, const AStringView value){
    if(value.empty())
        return true;

    CrashTestText needle(arena);
    needle += "\"";
    needle += key;
    needle += "\": \"";
    needle += value;
    needle += "\"";
    return Contains(manifest, AStringView(needle.data(), needle.size()));
}

static bool TriggerPackageContains(
    Core::Alloc::GlobalArena& arena,
    const CrashTestPath& packageDirectory,
    const AStringView category,
    const AStringView expression,
    const AStringView message,
    const AStringView file
){
    CrashTestText manifest(arena);
    if(!ReadTextFile(packageDirectory / CrashNames::s_ManifestFileName, manifest))
        return false;

    if(!ManifestContainsStringValue(arena, manifest, CrashNames::s_ManifestTriggerCategoryKey, category))
        return false;
    if(!ManifestContainsStringValue(arena, manifest, CrashNames::s_ManifestTriggerExpressionKey, expression))
        return false;
    if(!ManifestContainsStringValue(arena, manifest, CrashNames::s_ManifestTriggerMessageKey, message))
        return false;
    if(!file.empty() && !Contains(manifest, file))
        return false;

    return true;
}

static Expected<CrashTestPath> FindTriggerPackage(
    Core::Alloc::GlobalArena& arena,
    const CrashTestPath& pendingDirectory,
    const AStringView category,
    const AStringView expression,
    const AStringView message,
    const AStringView file
){
    const auto directory = DirectoryIterator<Core::Alloc::GlobalArena>::Create(pendingDirectory);
    if(!directory)
        return MakeUnexpected(Failure{});

    for(const auto& entry : *directory){
        const auto isDirectory = IsDirectory(entry.path());
        if(!isDirectory || !*isDirectory)
            continue;
        if(!TriggerPackageContains(arena, entry.path(), category, expression, message, file))
            continue;
        return entry.path();
    }
    return MakeUnexpected(Failure{});
}

Expected<CrashTestPath> WaitForTriggerPackage(
    Core::Alloc::GlobalArena& arena,
    const CrashTestPath& pendingDirectory,
    const AStringView category,
    const AStringView expression,
    const AStringView message,
    const AStringView file
){
    constexpr u32 s_TimeoutMilliseconds = 3000u;
    constexpr u32 s_PollMilliseconds = 10u;
    for(u32 elapsedMilliseconds = 0u; elapsedMilliseconds <= s_TimeoutMilliseconds; elapsedMilliseconds += s_PollMilliseconds){
        if(PathIsDirectory(pendingDirectory)){
            auto packageDirectory = FindTriggerPackage(arena, pendingDirectory, category, expression, message, file);
            if(packageDirectory)
                return Move(*packageDirectory);
        }
        SleepMS(s_PollMilliseconds);
    }
    return MakeUnexpected(Failure{});
}

void BuildLinuxCrashArchive(Core::Alloc::GlobalArena& arena, CrashTestText& archive, const AStringView crashId){
    BeginArchiveWithManifest(arena, archive, crashId, "linux", "crash", "signal", 11u);
    AppendArchiveFile(archive, CrashNames::s_CpuContextFileName, "fault_address=0\ninstruction_pointer=4198964\nstack_pointer=0\nframe_pointer=0\n");
    AppendArchiveFile(archive, CrashNames::s_CallstackFileName, "#0 0x0000000000401234\n#1 0x0000000000401240\n");
    AppendArchiveFile(archive, CrashNames::s_ProcMapsFileName, "00400000-00452000 r-xp 00000000 08:01 123 /tmp/nwb_loader\n");
}

bool Contains(const CrashTestText& text, const AStringView needle)noexcept{
    return AStringView(text.data(), text.size()).find(needle) != AStringView::npos;
}

bool ContainsMessage(const Log::LogString& text, const TStringView needle)noexcept{
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
    return basePath.parentPath() / AStringView(fileName.data(), fileName.size());
}

void PreserveObservedReport(Core::Alloc::GlobalArena& arena, const CrashTestText& report, const AStringView suffix){
    const auto outputPathText = ReadEnvironmentVariable(arena, "NWB_LOGSERVER_CRASH_TEST_OBSERVE_PATH");
    if(!outputPathText || outputPathText->empty())
        return;

    const CrashTestPath baseOutputPath(arena, AStringView(outputPathText->data(), outputPathText->size()));
    const CrashTestPath outputPath = ObservedReportPath(arena, baseOutputPath, suffix);
    EXPECT_TRUE(EnsureDirectories(outputPath.parentPath()));
    EXPECT_TRUE(WriteTextFile(outputPath, AStringView(report.data(), report.size())));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

