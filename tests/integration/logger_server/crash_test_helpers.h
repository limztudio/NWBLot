// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once

#include <tests/common/crash_test_paths.h>

#include <logger/server/crash_ingest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerServerCrash{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashTestText = AString<Core::Alloc::GlobalArena>;
using CrashTestBytes = Vector<u8, Core::Alloc::GlobalArena>;
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


using CrashTestPaths::CrashRootDirectory;
using CrashTestPaths::RemoveTestArtifacts;
using CrashTestPaths::SpoolDirectory;
using CrashTestPaths::TestCaseDirectory;
using CrashTestPaths::TestRootDirectory;

[[nodiscard]] CrashTestPath StorageDirectory(Core::Alloc::GlobalArena& arena, AStringView testGroup);
[[nodiscard]] CrashTestPath ArchiveInputDirectory(Core::Alloc::GlobalArena& arena, AStringView testGroup);
[[nodiscard]] CrashTestPath ArchiveFileName(Core::Alloc::GlobalArena& arena, AStringView stem);
[[nodiscard]] CrashTestPath ArchivePath(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem);
[[nodiscard]] CrashTestPath ExtractedPackageDirectory(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem);
[[nodiscard]] CrashTestPath RawArchivePath(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem);
[[nodiscard]] CrashTestPath InvalidArchivePath(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem);
void AppendArchiveFile(CrashTestText& archive, AStringView relativePath, AStringView content);
void BeginArchiveWithManifest(
    CrashTestText& archive,
    Core::Alloc::GlobalArena& arena,
    AStringView crashId,
    AStringView platform,
    AStringView event,
    AStringView reasonKind,
    u64 reasonCode,
    ManifestEventField::Enum eventField = ManifestEventField::Include,
    const ManifestTriggerFields& trigger = ManifestTriggerFields{}
);
[[nodiscard]] CrashTestText BuildManifest(
    Core::Alloc::GlobalArena& arena,
    AStringView crashId,
    AStringView platform,
    AStringView event,
    AStringView reasonKind,
    u64 reasonCode,
    ManifestEventField::Enum eventField = ManifestEventField::Include,
    const ManifestTriggerFields& trigger = ManifestTriggerFields{}
);
[[nodiscard]] bool WriteArchive(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem, AStringView archive);
[[nodiscard]] bool WriteArchiveBytes(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem, Span<const u8> archive);
[[nodiscard]] Expected<CrashTestText, ErrorCode> ReadServerSymbolication(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem);
[[nodiscard]] Log::CrashIngestConfig MakeIngestConfig(Core::Alloc::GlobalArena& arena, AStringView testGroup);
[[nodiscard]] Expected<CrashTestPath, ErrorCode> WaitForTriggerPackage(Core::Alloc::GlobalArena& arena, const CrashTestPath& pendingDirectory, AStringView category, AStringView expression, AStringView message, AStringView file);
void BuildLinuxCrashArchive(CrashTestText& archive, Core::Alloc::GlobalArena& arena, AStringView crashId);
[[nodiscard]] bool Contains(AStringView text, AStringView needle)noexcept;
[[nodiscard]] bool ContainsMessage(TStringView text, TStringView needle)noexcept;
void PreserveObservedReport(Core::Alloc::GlobalArena& arena, AStringView report, AStringView suffix);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

