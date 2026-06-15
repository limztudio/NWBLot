// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/test_context.h>

#include <logger/server/crash_ingest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{
namespace LoggerServerCrash{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CrashTestText = AString<Core::Alloc::GlobalArena>;
using CrashTestBytes = Vector<u8, Core::Alloc::GlobalArena>;
using CrashTestPath = ::Path<Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] CrashTestPath CrashRootDirectory(Core::Alloc::GlobalArena& arena);
[[nodiscard]] CrashTestPath TestRootDirectory(Core::Alloc::GlobalArena& arena);
[[nodiscard]] CrashTestPath TestCaseDirectory(Core::Alloc::GlobalArena& arena, AStringView testGroup);
[[nodiscard]] CrashTestPath StorageDirectory(Core::Alloc::GlobalArena& arena, AStringView testGroup);
[[nodiscard]] CrashTestPath SpoolDirectory(Core::Alloc::GlobalArena& arena, AStringView testGroup);
[[nodiscard]] CrashTestPath ArchiveInputDirectory(Core::Alloc::GlobalArena& arena, AStringView testGroup);
[[nodiscard]] CrashTestPath ArchiveFileName(Core::Alloc::GlobalArena& arena, AStringView stem);
[[nodiscard]] CrashTestPath ArchivePath(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem);
[[nodiscard]] CrashTestPath ExtractedPackageDirectory(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem);
[[nodiscard]] CrashTestPath RawArchivePath(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem);
[[nodiscard]] CrashTestPath InvalidArchivePath(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem);

void RemoveTestArtifacts(Core::Alloc::GlobalArena& arena, AStringView testGroup);
void AppendArchiveFile(CrashTestText& archive, AStringView relativePath, AStringView content);
[[nodiscard]] CrashTestText BuildManifest(
    Core::Alloc::GlobalArena& arena,
    AStringView crashId,
    AStringView platform,
    AStringView reasonKind = AStringView("manual"),
    u64 reasonCode = 0u
);
[[nodiscard]] bool WriteArchive(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem, const CrashTestText& archive);
[[nodiscard]] bool WriteArchiveBytes(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem, const CrashTestBytes& archive);
[[nodiscard]] bool BuildArchiveFromPackageDirectory(Core::Alloc::GlobalArena& arena, const CrashTestPath& packageDirectory, CrashTestBytes& outArchive);
[[nodiscard]] bool ReadServerSymbolication(Core::Alloc::GlobalArena& arena, AStringView testGroup, AStringView stem, CrashTestText& outReport);
[[nodiscard]] Log::CrashIngestConfig MakeIngestConfig(Core::Alloc::GlobalArena& arena, AStringView testGroup);
[[nodiscard]] bool WaitForTriggerPackage(
    Core::Alloc::GlobalArena& arena,
    const CrashTestPath& pendingDirectory,
    AStringView category,
    AStringView expression,
    AStringView message,
    AStringView file,
    CrashTestPath& outPackageDirectory
);
void BuildLinuxCrashArchive(Core::Alloc::GlobalArena& arena, CrashTestText& archive, AStringView crashId);
[[nodiscard]] bool Contains(const CrashTestText& text, AStringView needle);
[[nodiscard]] bool ContainsMessage(const Log::LogString& text, TStringView needle);
void PreserveObservedReport(TestContext& context, Core::Alloc::GlobalArena& arena, const CrashTestText& report, AStringView suffix);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
