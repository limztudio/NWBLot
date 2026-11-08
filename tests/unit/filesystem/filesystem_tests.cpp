// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_test_fixture.h"

#include <core/filesystem/factory.h>
#include <core/filesystem/volume_file_system.h>
#include <core/filesystem/volume_staging.h>
#include <core/filesystem/volume_naming.h>
#include <core/filesystem/volume_storage_detail.h>

#include <tests/common/test_context.h>
#include <tests/common/capturing_logger.h>

#include <global/filesystem.h>
#include <global/binary.h>
#include <global/filesystem/cleanup.h>
#include <global/scope_exit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_filesystem_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_MountedDirectory = "mounted";
constexpr AStringView s_RelocatedDirectory = "relocated";
constexpr AStringView s_HeldFileSuffix = ".held";



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Filesystem;
using Path = NWB::Path;

inline constexpr Name s_TestFile("project/tests/payload");
inline constexpr Name s_TestArena("tests/filesystem");

using NWB::Tests::FilesystemTest::BlockingRestoreLogger;
using NWB::Tests::FilesystemTest::FilesystemVolumeTest;
using NWB::Tests::FilesystemTest::ThrowingPublishLogger;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(FilesystemVolumeTest, SegmentProbePreservesInspectionErrors){
    const auto missing = VolumeSegmentExists(m_directory, m_desc.volumeName.view());
    ASSERT_TRUE(missing);
    EXPECT_FALSE(*missing);

#if defined(NWB_PLATFORM_WINDOWS)
    const Path invalidPath(m_arena, "invalid|volume");
#else
    TString<NWB::Core::Alloc::GlobalArena> longName(m_arena);
    longName.resize(40000u, static_cast<tchar>('a'));
    const Path invalidPath(m_arena, longName);
#endif
    const auto inspection = IsDirectory(invalidPath);
    ASSERT_FALSE(inspection);
    const auto failedProbe = VolumeSegmentExists(invalidPath, m_desc.volumeName.view());
    ASSERT_FALSE(failedProbe);
    EXPECT_EQ(failedProbe.error(), inspection.error());

    auto filesystem = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystem);
    const auto present = VolumeSegmentExists(m_directory, m_desc.volumeName.view());
    ASSERT_TRUE(present);
    EXPECT_TRUE(*present);
    ASSERT_TRUE((*filesystem)->unmount());
}

TEST_F(FilesystemVolumeTest, SegmentCreatedAfterMountIsNotTruncated){
    VolumeFileSystem filesystem(m_arena);
    ASSERT_TRUE(filesystem.mount(m_desc));
    const Path unexpected = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 1u);
    const Array<u8, 4u> sentinel{ 7u, 8u, 9u, 10u };
    ASSERT_TRUE(WriteBinaryFile(unexpected, sentinel));
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> payload(5000u, 5u, scratchArena);
    EXPECT_FALSE(filesystem.writeFile(s_TestFile, payload));
    Vector<u8, NWB::Core::Alloc::ScratchArena> loaded(scratchArena);
    ASSERT_TRUE(ReadBinaryFile(unexpected, loaded));
    ASSERT_EQ(loaded.size(), sentinel.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), sentinel.data(), sentinel.size()), 0);
    EXPECT_FALSE(filesystem.fileExists(s_TestFile));
    ASSERT_TRUE(filesystem.unmount());
}

TEST_F(FilesystemVolumeTest, FailedMultiSegmentGrowthKeepsTrimmedVolumeMountable){
    constexpr Name s_Large("project/tests/large_growth");
    VolumeFileSystem filesystem(m_arena);
    ASSERT_TRUE(filesystem.mount(m_desc));
    const Array<u8, 3u> payload{ 1u, 2u, 3u };
    ASSERT_TRUE(filesystem.writeFile(s_TestFile, payload));
    ASSERT_TRUE(filesystem.flush());
    const Path first = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 0u);
    const auto trimmedSize = FileSize(first);
    ASSERT_TRUE(trimmedSize);
    ASSERT_LT(*trimmedSize, m_desc.segmentSize);
    const Path collision = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 2u);
    const Array<u8, 2u> sentinel{ 7u, 8u };
    ASSERT_TRUE(WriteBinaryFile(collision, sentinel));
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> large(9000u, 5u, scratchArena);
    EXPECT_FALSE(filesystem.writeFile(s_Large, large));
    Vector<u8, NWB::Core::Alloc::ScratchArena> loaded(scratchArena);
    ASSERT_TRUE(ReadBinaryFile(collision, loaded));
    ASSERT_EQ(loaded.size(), sentinel.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), sentinel.data(), sentinel.size()), 0);
    ASSERT_TRUE(RemoveFile(collision));
    ASSERT_TRUE(filesystem.unmount());
    ASSERT_TRUE(filesystem.mount(m_desc));
    ASSERT_TRUE(filesystem.readFile(s_TestFile, loaded));
    ASSERT_EQ(loaded.size(), payload.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), payload.data(), payload.size()), 0);
    EXPECT_FALSE(filesystem.fileExists(s_Large));
    ASSERT_TRUE(filesystem.unmount());
}

TEST_F(FilesystemVolumeTest, GrowthDoesNotPadOverExternallyTruncatedLiveData){
    VolumeFileSystem filesystem(m_arena);
    ASSERT_TRUE(filesystem.mount(m_desc));
    const Array<u8, 8u> payload{ 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u };
    ASSERT_TRUE(filesystem.writeFile(s_TestFile, payload));
    const Path first = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 0u);
    const u64 truncatedSize = m_desc.metadataSize + 1u;
    ASSERT_TRUE(ResizeFile(first, truncatedSize));
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> large(5000u, 5u, scratchArena);
    EXPECT_FALSE(filesystem.writeFile(Name("project/tests/new_growth"), large));
    const auto size = FileSize(first);
    ASSERT_TRUE(size);
    EXPECT_EQ(*size, truncatedSize);
    ASSERT_TRUE(filesystem.unmount());
}

TEST_F(FilesystemVolumeTest, GrowthAtSegmentLimitDoesNotExpandTrimmedFile){
    m_desc.maxSegments = 1u;
    VolumeFileSystem filesystem(m_arena);
    ASSERT_TRUE(filesystem.mount(m_desc));
    const Array<u8, 3u> payload{ 1u, 2u, 3u };
    ASSERT_TRUE(filesystem.writeFile(s_TestFile, payload));
    ASSERT_TRUE(filesystem.flush());
    const Path first = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 0u);
    const auto before = FileSize(first);
    ASSERT_TRUE(before);
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> large(5000u, 5u, scratchArena);
    EXPECT_FALSE(filesystem.writeFile(Name("project/tests/limited_growth"), large));
    const auto after = FileSize(first);
    ASSERT_TRUE(after);
    EXPECT_EQ(*after, *before);
    ASSERT_TRUE(filesystem.unmount());
}

TEST_F(FilesystemVolumeTest, ReadsAcrossSegmentsAndSeeksAtBoundaries){
    auto filesystemResult = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystemResult);
    auto filesystem = Move(*filesystemResult);
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> payload(scratchArena);
    payload.resize(10000);
    for(usize i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<u8>(i % 251u);
    ASSERT_TRUE(filesystem->writeFileDeferred(s_TestFile, payload));
    ASSERT_TRUE(filesystem->flush());

    const auto opened = filesystem->openFile(s_TestFile);
    ASSERT_TRUE(opened);
    FileCursor cursor = *opened;
    ASSERT_TRUE(filesystem->seekFile(cursor, 3570, FileSeekOrigin::Begin));
    Array<u8, 64> buffer{};
    auto bytesRead = filesystem->readFile(cursor, buffer.data(), buffer.size());
    ASSERT_TRUE(bytesRead);
    ASSERT_EQ(*bytesRead, buffer.size());
    EXPECT_EQ(NWB_MEMCMP(buffer.data(), payload.data() + 3570, *bytesRead), 0);
    EXPECT_EQ(cursor.offset, 3634u);
    ASSERT_TRUE(filesystem->seekFile(cursor, -10, FileSeekOrigin::End));
    bytesRead = filesystem->readFile(cursor, buffer.data(), buffer.size());
    ASSERT_TRUE(bytesRead);
    EXPECT_EQ(*bytesRead, 10u);
    EXPECT_EQ(NWB_MEMCMP(buffer.data(), payload.data() + 9990, *bytesRead), 0);
    bytesRead = filesystem->readFile(cursor, buffer.data(), buffer.size());
    ASSERT_TRUE(bytesRead);
    EXPECT_EQ(*bytesRead, 0u);
    EXPECT_EQ(cursor.offset, 10000u);
    EXPECT_FALSE(filesystem->seekFile(cursor, 1, FileSeekOrigin::Current));
    EXPECT_FALSE(filesystem->seekFile(cursor, -10001, FileSeekOrigin::End));
    EXPECT_FALSE(filesystem->seekFile(cursor, Limit<i64>::s_Min, FileSeekOrigin::Current));
    EXPECT_FALSE(filesystem->seekFile(cursor, Limit<i64>::s_Max, FileSeekOrigin::Current));
    EXPECT_FALSE(filesystem->seekFile(cursor, 0, static_cast<FileSeekOrigin::Enum>(255)));
    EXPECT_EQ(cursor.offset, 10000u);
    filesystem->closeFile(cursor);
    EXPECT_FALSE(filesystem->readFile(cursor, buffer.data(), buffer.size()));
    ASSERT_TRUE(filesystem->unmount());
}

TEST_F(FilesystemVolumeTest, DeferredWritesSurviveUnmountAndReadOnlyMount){
    auto filesystemResult = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystemResult);
    auto filesystem = Move(*filesystemResult);
    Array<u8, 4> payload{ 1u, 3u, 5u, 7u };
    ASSERT_TRUE(filesystem->writeFileDeferred(s_TestFile, payload));
    payload[0] = 99u;
    ASSERT_TRUE(filesystem->unmount());
    m_desc.createIfMissing = false;
    m_desc.usage = VolumeUsage::RuntimeReadOnly;
    ASSERT_TRUE(filesystem->mount(m_desc));
    VolumeBytes loaded(m_arena);
    ASSERT_TRUE(filesystem->readFile(s_TestFile, loaded));
    ASSERT_EQ(loaded.size(), 4u);
    EXPECT_EQ(loaded[0], 1u);
    EXPECT_FALSE(filesystem->writeFile(s_TestFile, payload));
    EXPECT_FALSE(filesystem->removeFile(s_TestFile));
    ASSERT_TRUE(filesystem->unmount());
}

TEST_F(FilesystemVolumeTest, FailedUnmountRetainsPendingWritesForExplicitRetry){
    m_desc.mountDirectory = m_directory / s_MountedDirectory;
    const Path relocated = m_directory / s_RelocatedDirectory;
    auto filesystemResult = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystemResult);
    auto filesystem = Move(*filesystemResult);
    Array<u8, 4u> payload{ 1u, 3u, 5u, 7u };
    ASSERT_TRUE(filesystem->writeFileDeferred(s_TestFile, payload));
    payload[0u] = 99u;

    ASSERT_TRUE(RenamePath(m_desc.mountDirectory, relocated));
    EXPECT_FALSE(filesystem->unmount());
    EXPECT_TRUE(filesystem->mounted());
    EXPECT_TRUE(filesystem->writable());
    EXPECT_TRUE(filesystem->fileExists(s_TestFile));
    ASSERT_TRUE(RenamePath(relocated, m_desc.mountDirectory));
    ASSERT_TRUE(filesystem->unmount());

    m_desc.createIfMissing = false;
    m_desc.usage = VolumeUsage::RuntimeReadOnly;
    ASSERT_TRUE(filesystem->mount(m_desc));
    VolumeBytes loaded(m_arena);
    ASSERT_TRUE(filesystem->readFile(s_TestFile, loaded));
    ASSERT_EQ(loaded.size(), payload.size());
    EXPECT_EQ(loaded[0u], 1u);
    EXPECT_EQ(loaded[1u], 3u);
    EXPECT_EQ(loaded[2u], 5u);
    EXPECT_EQ(loaded[3u], 7u);
    ASSERT_TRUE(filesystem->unmount());
}

TEST_F(FilesystemVolumeTest, ExplicitFlushSurvivesResourceOnlyDestructionWithoutPersistenceRetry){
    static_assert(IsNothrowDestructible_V<VolumeFileSystem>);
    m_desc.mountDirectory = m_directory / s_MountedDirectory;
    const Path relocated = m_directory / s_RelocatedDirectory;
    auto filesystemResult = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystemResult);
    auto filesystem = Move(*filesystemResult);
    const Array<u8, 3u> payload{ 4u, 5u, 6u };
    ASSERT_TRUE(filesystem->writeFileDeferred(s_TestFile, payload));
    ASSERT_TRUE(filesystem->flush());

    ASSERT_TRUE(RenamePath(m_desc.mountDirectory, relocated));
    const u32 messageCount = m_logger.messageCount();
    filesystem.reset();
    EXPECT_EQ(m_logger.messageCount(), messageCount);
    ASSERT_TRUE(RenamePath(relocated, m_desc.mountDirectory));

    m_desc.createIfMissing = false;
    m_desc.usage = VolumeUsage::RuntimeReadOnly;
    auto reopened = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(reopened);
    VolumeBytes loaded(m_arena);
    ASSERT_TRUE((*reopened)->readFile(s_TestFile, loaded));
    ASSERT_EQ(loaded.size(), payload.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), payload.data(), loaded.size()), 0);
    ASSERT_TRUE((*reopened)->unmount());
}

TEST_F(FilesystemVolumeTest, RemountFlushesPendingMetadata){
    auto filesystemResult = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystemResult);
    auto filesystem = Move(*filesystemResult);
    const Array<u8, 3> payload{ 4u, 5u, 6u };
    ASSERT_TRUE(filesystem->writeFileDeferred(s_TestFile, payload));
    m_desc.createIfMissing = false;
    m_desc.usage = VolumeUsage::RuntimeReadOnly;
    ASSERT_TRUE(filesystem->mount(m_desc));
    VolumeBytes loaded(m_arena);
    ASSERT_TRUE(filesystem->readFile(s_TestFile, loaded));
    ASSERT_EQ(loaded.size(), payload.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), payload.data(), loaded.size()), 0);
    ASSERT_TRUE(filesystem->unmount());
}

TEST_F(FilesystemVolumeTest, ReplacesRemovesAndReadsEmptyFiles){
    auto filesystemResult = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystemResult);
    auto filesystem = Move(*filesystemResult);
    const Array<u8, 3> payload{ 4u, 5u, 6u };
    ASSERT_TRUE(filesystem->writeFile(s_TestFile, payload));
    ASSERT_TRUE(filesystem->writeFile(s_TestFile, nullptr, 0));
    VolumeBytes loaded(m_arena);
    ASSERT_TRUE(filesystem->readFile(s_TestFile, loaded));
    EXPECT_TRUE(loaded.empty());
    EXPECT_EQ(filesystem->fileCount(), 1u);
    EXPECT_FALSE(filesystem->writeFile(s_TestFile, nullptr, 1));
    EXPECT_FALSE(filesystem->writeFile(s_NameNone, payload));
    EXPECT_FALSE(filesystem->readFile(s_TestFile, 1, nullptr, 0));
    ASSERT_TRUE(filesystem->removeFile(s_TestFile));
    EXPECT_EQ(filesystem->fileCount(), 0u);
    EXPECT_FALSE(filesystem->readFile(s_TestFile, loaded));
    EXPECT_TRUE(loaded.empty());
    ASSERT_TRUE(filesystem->unmount());
}

TEST_F(FilesystemVolumeTest, CompactionPreservesExistingRecoveryDirectories){
    constexpr Name s_Gap("project/tests/recovery_gap");
    VolumeFileSystem filesystem(m_arena);
    ASSERT_TRUE(filesystem.mount(m_desc));
    const Array<u8, 4u> payload{ 1u, 2u, 3u, 4u };
    ASSERT_TRUE(filesystem.writeFile(s_Gap, payload));
    ASSERT_TRUE(filesystem.writeFile(s_TestFile, payload));
    ASSERT_TRUE(filesystem.removeFile(s_Gap));
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    const auto paths = FilesystemVolumeStagingDetail::BuildStagedVolumePaths(m_directory, m_desc.volumeName.view(), scratchArena);
    ASSERT_TRUE(EnsureDirectories(paths.backupDirectory));
    ASSERT_TRUE(EnsureDirectories(paths.stageDirectory));
    const Path backupEvidence = paths.backupDirectory / "recovery";
    const Path stageEvidence = paths.stageDirectory / "recovery";
    ASSERT_TRUE(WriteBinaryFile(backupEvidence, payload));
    ASSERT_TRUE(WriteBinaryFile(stageEvidence, payload));
    EXPECT_FALSE(filesystem.compact(false));
    Vector<u8, NWB::Core::Alloc::ScratchArena> loaded(scratchArena);
    ASSERT_TRUE(ReadBinaryFile(backupEvidence, loaded));
    ASSERT_EQ(loaded.size(), payload.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), payload.data(), payload.size()), 0);
    ASSERT_TRUE(ReadBinaryFile(stageEvidence, loaded));
    ASSERT_EQ(loaded.size(), payload.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), payload.data(), payload.size()), 0);
    ASSERT_TRUE(filesystem.unmount());
}

#if defined(NWB_PLATFORM_WINDOWS)
TEST_F(FilesystemVolumeTest, IncompleteCompactionRollbackUnmountsTheVolume){
    VolumeFileSystem filesystem(m_arena);
    ASSERT_TRUE(filesystem.mount(m_desc));
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> abandoned(5000u, 19u, scratchArena);
    const Array<u8, 3u> payload{ 4u, 5u, 6u };
    ASSERT_TRUE(filesystem.writeFile(s_TestFile, abandoned));
    ASSERT_TRUE(filesystem.writeFile(s_TestFile, payload));
    ASSERT_GE(filesystem.segmentCount(), 2u);
    const Path first = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 0u);
    const Path second = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 1u);
    Vector<u8, NWB::Core::Alloc::ScratchArena> original(scratchArena);
    ASSERT_TRUE(ReadBinaryFile(first, original));
    const auto paths = FilesystemVolumeStagingDetail::BuildStagedVolumePaths(m_directory, m_desc.volumeName.view(), scratchArena);
    const Path backup = MakeVolumeSegmentPath(paths.backupDirectory, m_desc.volumeName.view(), 0u);
    const HANDLE held = CreateFile(second.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(held, INVALID_HANDLE_VALUE);
    ScopeExit closeHeld([held]()noexcept{ CloseHandle(held); });
    BlockingRestoreLogger logger(m_logger.arena(), first);
    {
        NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
        EXPECT_FALSE(filesystem.compact(false));
    }
    EXPECT_TRUE(logger.blocked());
    EXPECT_FALSE(filesystem.mounted());
    EXPECT_FALSE(filesystem.fileExists(s_TestFile));
    Vector<u8, NWB::Core::Alloc::ScratchArena> loaded(scratchArena);
    ASSERT_TRUE(ReadBinaryFile(backup, loaded));
    ASSERT_EQ(loaded.size(), original.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), original.data(), original.size()), 0);
    EXPECT_FALSE(filesystem.readFile(s_TestFile, loaded));
    EXPECT_TRUE(filesystem.unmount());
}
#endif

TEST_F(FilesystemVolumeTest, FailedCompactionPreservesOverlappingSourcePayloadBytes){
    constexpr Name s_First("project/tests/compact_first");
    constexpr Name s_Gap("project/tests/compact_gap");
    constexpr Name s_Target("project/tests/compact_target");
    VolumeFileSystem filesystem(m_arena);
    ASSERT_TRUE(filesystem.mount(m_desc));
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> first(static_cast<usize>(m_desc.segmentSize - m_desc.metadataSize), 31u, scratchArena);
    const Array<u8, 128u> gap{};
    Array<u8, 256u> payload{};
    for(usize index = 0u; index < payload.size(); ++index)
        payload[index] = static_cast<u8>(index);
    ASSERT_TRUE(filesystem.writeFile(s_First, first));
    ASSERT_TRUE(filesystem.writeFile(s_Gap, gap));
    ASSERT_TRUE(filesystem.writeFile(s_Target, payload));
    ASSERT_TRUE(filesystem.removeFile(s_Gap));
    const ::Path<NWB::Core::Alloc::ScratchArena> directory(scratchArena, m_directory);
    const auto metadataSegment = MakeVolumeSegmentPath(directory, m_desc.volumeName.view(), 0u);
    auto backupSegment = metadataSegment;
    backupSegment += s_HeldFileSuffix;
    b8 restored = false;
    {
        ASSERT_TRUE(RenamePath(metadataSegment, backupSegment));
        ScopeExit restore([&]()noexcept{
            const auto removed = RemoveAllForCleanup(metadataSegment);
            if(removed)
                restored = static_cast<b8>(RenamePath(backupSegment, metadataSegment));
        });
        ASSERT_TRUE(EnsureDirectories(metadataSegment));
        EXPECT_FALSE(filesystem.compact(false));
    }
    ASSERT_TRUE(restored);
    Vector<u8, NWB::Core::Alloc::ScratchArena> loaded(scratchArena);
    ASSERT_TRUE(filesystem.readFile(s_Target, loaded));
    ASSERT_EQ(loaded.size(), payload.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), payload.data(), payload.size()), 0);
    ASSERT_TRUE(filesystem.unmount());
}

#if NWB_OCCUR_WARNING
TEST_F(FilesystemVolumeTest, ImmediateMutationUnwindPreservesIndexedPayloadsAndMembership){
    constexpr Name s_First("project/tests/mutation_first");
    constexpr Name s_New("project/tests/mutation_new");
    VolumeFileSystem filesystem(m_arena);
    ASSERT_TRUE(filesystem.mount(m_desc));
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> first(static_cast<usize>(m_desc.segmentSize - m_desc.metadataSize), 31u, scratchArena);
    const Array<u8, 4u> payload{ 1u, 2u, 3u, 4u };
    const Array<u8, 4u> replacement{ 8u, 7u, 6u, 5u };
    ASSERT_TRUE(filesystem.writeFile(s_First, first));
    ASSERT_TRUE(filesystem.writeFile(s_TestFile, payload));
    const ::Path<NWB::Core::Alloc::ScratchArena> directory(scratchArena, m_directory);
    const auto metadataSegment = MakeVolumeSegmentPath(directory, m_desc.volumeName.view(), 0u);
    auto backupSegment = metadataSegment;
    backupSegment += s_HeldFileSuffix;
    Vector<u8, NWB::Core::Alloc::ScratchArena> loaded(scratchArena);
    for(u32 operation = 0u; operation < 3u; ++operation){
        b8 restored = false;
        {
            ASSERT_TRUE(RenamePath(metadataSegment, backupSegment));
            ScopeExit restore([&]()noexcept{
                const auto removed = RemoveAllForCleanup(metadataSegment);
                if(removed)
                    restored = static_cast<b8>(RenamePath(backupSegment, metadataSegment));
            });
            ASSERT_TRUE(EnsureDirectories(metadataSegment));
            ThrowingPublishLogger throwingLogger(m_logger.arena());
            NWB::Core::Common::LoggerRegistrationGuard throwingLoggerGuard(throwingLogger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
            if(operation == 0u)
                EXPECT_THROW({ const b8 written = filesystem.writeFile(s_TestFile, replacement); EXPECT_FALSE(written); }, i32);
            else if(operation == 1u)
                EXPECT_THROW({ const b8 written = filesystem.writeFile(s_New, replacement); EXPECT_FALSE(written); }, i32);
            else
                EXPECT_THROW({ const b8 removed = filesystem.removeFile(s_TestFile); EXPECT_FALSE(removed); }, i32);
        }
        ASSERT_TRUE(restored);
        EXPECT_EQ(filesystem.fileCount(), 2u);
        EXPECT_TRUE(filesystem.fileExists(s_TestFile));
        EXPECT_FALSE(filesystem.fileExists(s_New));
        ASSERT_TRUE(filesystem.readFile(s_TestFile, loaded));
        ASSERT_EQ(loaded.size(), payload.size());
        EXPECT_EQ(NWB_MEMCMP(loaded.data(), payload.data(), payload.size()), 0);
    }
    ASSERT_TRUE(filesystem.unmount());
}
#endif

TEST_F(FilesystemVolumeTest, CompactionPreservesCapacityUntilRequestedTrimAndRetainsCursorIdentity){
    VolumeFileSystem filesystem(m_arena);
    ASSERT_TRUE(filesystem.mount(m_desc));
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> abandoned(5000u, 19u, scratchArena);
    const Array<u8, 3u> payload{ 4u, 5u, 6u };
    ASSERT_TRUE(filesystem.writeFile(s_TestFile, abandoned));
    ASSERT_TRUE(filesystem.writeFile(s_TestFile, payload));
    const usize originalSegments = filesystem.segmentCount();
    ASSERT_GT(originalSegments, 1u);
    const auto opened = filesystem.openFile(s_TestFile);
    ASSERT_TRUE(opened);
    FileCursor cursor = *opened;
    ASSERT_TRUE(filesystem.compact(false));
    EXPECT_EQ(filesystem.segmentCount(), originalSegments);
    ASSERT_TRUE(filesystem.wastedBytes());
    EXPECT_EQ(*filesystem.wastedBytes(), 0u);
    Array<u8, 3u> loaded{};
    const auto read = filesystem.readFile(cursor, loaded.data(), loaded.size());
    ASSERT_TRUE(read);
    EXPECT_EQ(*read, payload.size());
    EXPECT_EQ(loaded, payload);
    ASSERT_TRUE(filesystem.compact(true));
    EXPECT_EQ(filesystem.segmentCount(), 1u);
    const ::Path<NWB::Core::Alloc::ScratchArena> directory(scratchArena, m_directory);
    const auto firstSegment = MakeVolumeSegmentPath(directory, m_desc.volumeName.view(), 0u);
    const auto lastSegmentBytes = FileSize(firstSegment);
    ASSERT_TRUE(lastSegmentBytes);
    EXPECT_EQ(*lastSegmentBytes, m_desc.metadataSize + payload.size());
    ASSERT_TRUE(filesystem.unmount());
}

TEST_F(FilesystemVolumeTest, RejectsInvalidPathHashFromMetadataIndex){
    using FilesystemVolumeDetail::VolumeHeaderDisk;
    using FilesystemVolumeDetail::VolumeIndexEntryDisk;
    auto filesystemResult = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystemResult);
    auto filesystem = Move(*filesystemResult);
    const Array<u8, 3u> payload{ 4u, 5u, 6u };
    ASSERT_TRUE(filesystem->writeFile(s_TestFile, payload));
    ASSERT_EQ(filesystem->fileCount(), 1u);
    ASSERT_TRUE(filesystem->unmount());

    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    const ::Path<NWB::Core::Alloc::ScratchArena> directory(scratchArena, m_directory);
    const auto segmentPath = MakeVolumeSegmentPath(directory, m_desc.volumeName.view(), 0u);
    Vector<u8, NWB::Core::Alloc::ScratchArena> bytes(scratchArena);
    ASSERT_TRUE(ReadBinaryFile(segmentPath, bytes));
    ASSERT_GE(bytes.size(), sizeof(VolumeHeaderDisk) + sizeof(VolumeIndexEntryDisk));
    VolumeIndexEntryDisk entry{};
    NWB_MEMCPY(&entry, sizeof(entry), bytes.data() + sizeof(VolumeHeaderDisk), sizeof(entry));
    ASSERT_TRUE(Name(entry.hash));
    entry.hash = NameHash{};
    NWB_MEMCPY(bytes.data() + sizeof(VolumeHeaderDisk), sizeof(entry), &entry, sizeof(entry));
    ASSERT_TRUE(WriteBinaryFile(segmentPath, bytes));

    m_desc.createIfMissing = false;
    m_desc.usage = VolumeUsage::RuntimeReadOnly;
    EXPECT_FALSE(CreateFilesystem(m_arena, m_desc));
}

TEST_F(FilesystemVolumeTest, MetadataImagePreservesCompleteHashesAndClearsRemovedRecords){
    using FilesystemVolumeDetail::VolumeHeaderDisk;
    using FilesystemVolumeDetail::VolumeIndexEntryDisk;
    auto filesystemResult = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystemResult);
    auto filesystem = Move(*filesystemResult);
    Array<NameHash, 3u> hashes{};
    for(NameHash& hash : hashes)
        hash.qwords[0u] = 7u;
    hashes[0u].qwords[s_NameHashLaneCount - 1u] = 30u;
    hashes[1u].qwords[s_NameHashLaneCount - 1u] = 10u;
    hashes[2u].qwords[s_NameHashLaneCount - 1u] = 20u;
    const Array<u8, 3u> payload{ 4u, 5u, 6u };
    ASSERT_TRUE(filesystem->writeFileDeferred(Name(hashes[0u]), payload));
    ASSERT_TRUE(filesystem->writeFileDeferred(Name(hashes[2u]), payload));
    ASSERT_TRUE(filesystem->writeFileDeferred(Name(hashes[1u]), nullptr, 0u));
    ASSERT_TRUE(filesystem->flush());

    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> expected(scratchArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> actual(scratchArena);
    expected.reserve(static_cast<usize>(m_desc.segmentSize));
    actual.reserve(static_cast<usize>(m_desc.segmentSize));
    const Array<VolumeIndexEntryDisk, 3u> records{
        VolumeIndexEntryDisk{ hashes[1u], 518u, 0u },
        VolumeIndexEntryDisk{ hashes[2u], 515u, 3u },
        VolumeIndexEntryDisk{ hashes[0u], 512u, 3u }
    };
    const Path segmentPath = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 0u);
    const auto verifyImage = [&](const usize firstRecord){
        VolumeHeaderDisk header{};
        header.magic = FilesystemVolumeDetail::s_VolumeMagic;
        header.segmentSize = m_desc.segmentSize;
        header.metadataBytes = m_desc.metadataSize;
        header.fileCount = records.size() - firstRecord;
        header.indexBytes = header.fileCount * sizeof(VolumeIndexEntryDisk);
        header.nextFreeOffset = 518u;
        expected.clear();
        AppendPOD(expected, header);
        for(usize index = firstRecord; index < records.size(); ++index)
            AppendPOD(expected, records[index]);
        expected.resize(static_cast<usize>(m_desc.metadataSize), 0u);
        expected.insert(expected.end(), payload.begin(), payload.end());
        expected.insert(expected.end(), payload.begin(), payload.end());
        ASSERT_TRUE(ReadBinaryFile(segmentPath, actual));
        ASSERT_EQ(actual.size(), expected.size());
        EXPECT_EQ(NWB_MEMCMP(actual.data(), expected.data(), expected.size()), 0);
    };
    verifyImage(0u);
    ASSERT_TRUE(filesystem->unmount());
    m_desc.createIfMissing = false;
    m_desc.usage = VolumeUsage::RuntimeReadOnly;
    ASSERT_TRUE(filesystem->mount(m_desc));
    for(usize index = 0u; index < hashes.size(); ++index){
        VolumeBytes loaded(m_arena);
        ASSERT_TRUE(filesystem->readFile(Name(hashes[index]), loaded));
        if(index == 1u)
            EXPECT_TRUE(loaded.empty());
        else{
            ASSERT_EQ(loaded.size(), payload.size());
            EXPECT_EQ(NWB_MEMCMP(loaded.data(), payload.data(), payload.size()), 0);
        }
    }
    ASSERT_TRUE(filesystem->unmount());
    m_desc.usage = VolumeUsage::CookWrite;
    ASSERT_TRUE(filesystem->mount(m_desc));
    for(usize index = 0u; index < records.size(); ++index){
        ASSERT_TRUE(filesystem->removeFile(Name(records[index].hash)));
        verifyImage(index + 1u);
    }
    ASSERT_TRUE(filesystem->unmount());
}

TEST(FilesystemFactory, PropagatesFactoryAndMountFailures){
    NWB::Tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
    NWB::Core::Alloc::GlobalArena arena(s_TestArena);
    VolumeMountDesc desc(arena);
    const FilesystemFactory emptyFactory = []([[maybe_unused]] NWB::Core::Alloc::GlobalArena& objectArena, [[maybe_unused]] const VolumeMountDesc& mount)noexcept{
        return Expected<UniquePtr<IFilesystem>>(UniquePtr<IFilesystem>());
    };
    EXPECT_FALSE(CreateFilesystem(arena, desc, emptyFactory));
    const FilesystemFactory failedFactory = []([[maybe_unused]] NWB::Core::Alloc::GlobalArena& objectArena, [[maybe_unused]] const VolumeMountDesc& mount)noexcept -> Expected<UniquePtr<IFilesystem>>{
        return MakeUnexpected(Failure{});
    };
    EXPECT_FALSE(CreateFilesystem(arena, desc, failedFactory));
    EXPECT_FALSE(CreateFilesystem(arena, desc));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


TEST(Filesystem, VolumeNamesRejectEmptySeparatorsAndWhitespace){
    EXPECT_FALSE(NWB::Core::Filesystem::ValidVolumeName(""));
    EXPECT_FALSE(NWB::Core::Filesystem::ValidVolumeName("graphics/cache"));
    EXPECT_FALSE(NWB::Core::Filesystem::ValidVolumeName("graphics cache"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

