// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/integration/crash/crash_test_paths.h>

#include <logger/server/crash_ingest.h>
#include <logger/server/crash_paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerServerCrash{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashTestText = AString<Core::Alloc::GlobalArena>;
using CrashTestPath = ::Path<Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ManifestEventField{
    enum Enum : u8{
        Include,
        Omit,
    };
};

struct ManifestTriggerFields{
    AStringView category;
    AStringView expression;
    AStringView message;
    AStringView file;
    u64 line = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline CrashTestPath StorageDirectory(Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return CrashTestPaths::TestCaseDirectory(arena, testGroup) / "storage";
}
[[nodiscard]] inline CrashTestPath ArchiveInputDirectory(Core::Alloc::GlobalArena& arena, const AStringView testGroup){
    return CrashTestPaths::TestCaseDirectory(arena, testGroup) / "input";
}
[[nodiscard]] CrashTestPath ArchiveFileName(Core::Alloc::GlobalArena& arena, AStringView stem);
[[nodiscard]] inline CrashTestPath ArchivePath(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return ArchiveInputDirectory(arena, testGroup) / ArchiveFileName(arena, stem);
}
[[nodiscard]] inline CrashTestPath ExtractedPackageDirectory(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return StorageDirectory(arena, testGroup) / Log::s_CrashExtractedDirectoryName / stem;
}
[[nodiscard]] inline CrashTestPath RawArchivePath(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return StorageDirectory(arena, testGroup) / Log::s_CrashRawDirectoryName / ArchiveFileName(arena, stem);
}
[[nodiscard]] inline CrashTestPath InvalidArchivePath(Core::Alloc::GlobalArena& arena, const AStringView testGroup, const AStringView stem){
    return StorageDirectory(arena, testGroup) / Log::s_CrashInvalidDirectoryName / ArchiveFileName(arena, stem);
}
void AppendArchiveFile(AStringView relativePath, AStringView content, CrashTestText& archive);
void BeginArchiveWithManifest(
    AStringView crashId,
    AStringView platform,
    AStringView event,
    AStringView reasonKind,
    u64 reasonCode,
    ManifestEventField::Enum eventField,
    const ManifestTriggerFields& trigger,
    Core::Alloc::GlobalArena& arena,
    CrashTestText& archive
);
[[nodiscard]] CrashTestText BuildManifest(
    AStringView crashId,
    AStringView platform,
    AStringView event,
    AStringView reasonKind,
    u64 reasonCode,
    ManifestEventField::Enum eventField,
    const ManifestTriggerFields& trigger,
    Core::Alloc::GlobalArena& arena
);
[[nodiscard]] b8 WriteArchive(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem, AStringView archive);
[[nodiscard]] b8 WriteArchiveBytes(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem, Span<const u8> archive);
[[nodiscard]] Expected<CrashTestText, ErrorCode> ReadServerSymbolication(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem);
[[nodiscard]] Log::CrashIngestConfig MakeIngestConfig(Core::Alloc::GlobalArena& arena, AStringView testGroup);
[[nodiscard]] Expected<CrashTestPath, ErrorCode> WaitForTriggerPackage(Core::Alloc::GlobalArena& arena, const CrashTestPath& pendingDirectory, AStringView category, AStringView expression, AStringView message, AStringView file);
void BuildLinuxCrashArchive(AStringView crashId, Core::Alloc::GlobalArena& arena, CrashTestText& archive);
void PreserveObservedReport(Core::Alloc::GlobalArena& arena, AStringView report, AStringView suffix);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

