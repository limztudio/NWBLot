// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
constexpr AStringView s_StageDirectory = "stage";
constexpr AStringView s_BackupDirectory = "backup";

constexpr u32 s_ThirdElementIndex = 2u;
constexpr i32 s_PublishLogFailure = 47;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Filesystem;
using Path = NWB::Path;

inline constexpr Name s_TestFile("project/tests/payload");
inline constexpr Name s_TestArena("tests/filesystem");

class ThrowingPublishLogger final : public NWB::Core::Common::ILogger{
public:
    explicit ThrowingPublishLogger(NWB::Core::Common::LogArena& arena)noexcept
        : m_arena(arena)
    {}


public:
    virtual NWB::Core::Common::LogArena& arena()noexcept override{ return m_arena; }
    virtual void enqueue(NWB::Core::Common::LogString&&, NWB::Core::Common::LogType::Enum)override{ throw s_PublishLogFailure; }
    virtual void enqueue(const NWB::Core::Common::LogString&, NWB::Core::Common::LogType::Enum)override{ throw s_PublishLogFailure; }


private:
    NWB::Core::Common::LogArena& m_arena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FilesystemVolumeTest : public testing::Test{
public:
    FilesystemVolumeTest()
        : m_arena(s_TestArena)
        , m_directory(m_arena, "filesystem_unit_test_volume")
        , m_desc(m_arena)
        , m_loggerGuard(m_logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly)
    {}


protected:
    virtual void SetUp()override{
        ASSERT_TRUE(RemoveAllIfExists(m_directory));
        ASSERT_TRUE(m_desc.volumeName.assign("test"));
        m_desc.mountDirectory = m_directory;
        m_desc.segmentSize = 4096;
        m_desc.metadataSize = 512;
        m_desc.createIfMissing = true;
        m_desc.usage = VolumeUsage::CookWrite;
    }
    virtual void TearDown()override{
        EXPECT_TRUE(RemoveAllIfExists(m_directory));
    }


protected:
    NWB::Core::Alloc::GlobalArena m_arena;
    Path m_directory;
    VolumeMountDesc m_desc;
    NWB::Tests::CapturingLogger m_logger;
    NWB::Core::Common::LoggerRegistrationGuard m_loggerGuard;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    bool restored = false;
    {
        ASSERT_TRUE(RenamePath(metadataSegment, backupSegment));
        ScopeExit restore([&]()noexcept{
            const auto removed = RemoveAllForCleanup(metadataSegment);
            if(removed)
                restored = static_cast<bool>(RenamePath(backupSegment, metadataSegment));
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
        bool restored = false;
        {
            ASSERT_TRUE(RenamePath(metadataSegment, backupSegment));
            ScopeExit restore([&]()noexcept{
                const auto removed = RemoveAllForCleanup(metadataSegment);
                if(removed)
                    restored = static_cast<bool>(RenamePath(backupSegment, metadataSegment));
            });
            ASSERT_TRUE(EnsureDirectories(metadataSegment));
            ThrowingPublishLogger throwingLogger(m_logger.arena());
            NWB::Core::Common::LoggerRegistrationGuard throwingLoggerGuard(throwingLogger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
            if(operation == 0u)
                EXPECT_THROW({ const bool written = filesystem.writeFile(s_TestFile, replacement); EXPECT_FALSE(written); }, i32);
            else if(operation == 1u)
                EXPECT_THROW({ const bool written = filesystem.writeFile(s_New, replacement); EXPECT_FALSE(written); }, i32);
            else
                EXPECT_THROW({ const bool removed = filesystem.removeFile(s_TestFile); EXPECT_FALSE(removed); }, i32);
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
    hashes[s_ThirdElementIndex].qwords[s_NameHashLaneCount - 1u] = 20u;
    const Array<u8, 3u> payload{ 4u, 5u, 6u };
    ASSERT_TRUE(filesystem->writeFileDeferred(Name(hashes[0u]), payload));
    ASSERT_TRUE(filesystem->writeFileDeferred(Name(hashes[s_ThirdElementIndex]), payload));
    ASSERT_TRUE(filesystem->writeFileDeferred(Name(hashes[1u]), nullptr, 0u));
    ASSERT_TRUE(filesystem->flush());

    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> expected(scratchArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> actual(scratchArena);
    expected.reserve(static_cast<usize>(m_desc.segmentSize));
    actual.reserve(static_cast<usize>(m_desc.segmentSize));
    const Array<VolumeIndexEntryDisk, 3u> records{
        VolumeIndexEntryDisk{ hashes[1u], 518u, 0u },
        VolumeIndexEntryDisk{ hashes[s_ThirdElementIndex], 515u, 3u },
        VolumeIndexEntryDisk{ hashes[0u], 512u, 3u }
    };
    const Path segmentPath = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 0u);
    const auto verifyImage = [&](const usize firstRecord){
        VolumeHeaderDisk header{};
        NWB_MEMCPY(header.magic, sizeof(header.magic), FilesystemVolumeDetail::s_VolumeMagic, sizeof(header.magic));
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

TEST_F(FilesystemVolumeTest, FailedLaterSegmentPromotionRestoresPublishedBytes){
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    NWB::Core::Filesystem::StagedDirectoryPaths stagedPaths(m_arena);
    stagedPaths.stageDirectory = m_directory / s_StageDirectory;
    stagedPaths.backupDirectory = m_directory / s_BackupDirectory;
    ASSERT_TRUE(EnsureDirectories(stagedPaths.stageDirectory));
    const Path firstOutput = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 0u);
    const Path secondOutput = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 1u);
    const Path absentOutput = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 2u);
    const Path firstStaged = MakeVolumeSegmentPath(stagedPaths.stageDirectory, m_desc.volumeName.view(), 0u);
    const Array<u8, 3u> firstOld{ 1u, 2u, 3u };
    const Array<u8, 4u> secondOld{ 4u, 5u, 6u, 7u };
    const Array<u8, 2u> replacement{ 8u, 9u };
    ASSERT_TRUE(WriteBinaryFile(firstOutput, firstOld));
    ASSERT_TRUE(WriteBinaryFile(secondOutput, secondOld));
    ASSERT_TRUE(WriteBinaryFile(firstStaged, replacement));

    EXPECT_FALSE(PublishStagedVolume(stagedPaths, m_directory, m_desc.volumeName.view(), 3u, scratchArena));
    EXPECT_TRUE(m_logger.sawErrorContaining(NWB_TEXT("failed to promote staged segment")));
    Vector<u8, NWB::Core::Alloc::ScratchArena> loaded(scratchArena);
    ASSERT_TRUE(ReadBinaryFile(firstOutput, loaded));
    ASSERT_EQ(loaded.size(), firstOld.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), firstOld.data(), firstOld.size()), 0);
    ASSERT_TRUE(ReadBinaryFile(secondOutput, loaded));
    ASSERT_EQ(loaded.size(), secondOld.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), secondOld.data(), secondOld.size()), 0);
    const auto absent = FileExists(absentOutput);
    ASSERT_TRUE(absent);
    EXPECT_FALSE(*absent);
    const auto backup = FileExists(stagedPaths.backupDirectory);
    ASSERT_TRUE(backup);
    EXPECT_FALSE(*backup);
}

TEST_F(FilesystemVolumeTest, ThrowingPromotionDiagnosticRestoresPublishedBytesDuringUnwind){
    NWB::Core::Alloc::ScratchArena scratchArena(s_TestArena);
    NWB::Core::Filesystem::StagedDirectoryPaths stagedPaths(m_arena);
    stagedPaths.stageDirectory = m_directory / s_StageDirectory;
    stagedPaths.backupDirectory = m_directory / s_BackupDirectory;
    ASSERT_TRUE(EnsureDirectories(stagedPaths.stageDirectory));
    const Path firstOutput = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 0u);
    const Path secondOutput = MakeVolumeSegmentPath(m_directory, m_desc.volumeName.view(), 1u);
    const Path firstStaged = MakeVolumeSegmentPath(stagedPaths.stageDirectory, m_desc.volumeName.view(), 0u);
    const Array<u8, 3u> firstOld{ 1u, 2u, 3u };
    const Array<u8, 4u> secondOld{ 4u, 5u, 6u, 7u };
    const Array<u8, 2u> replacement{ 8u, 9u };
    ASSERT_TRUE(WriteBinaryFile(firstOutput, firstOld));
    ASSERT_TRUE(WriteBinaryFile(secondOutput, secondOld));
    ASSERT_TRUE(WriteBinaryFile(firstStaged, replacement));

    {
        ThrowingPublishLogger throwingLogger(m_logger.arena());
        NWB::Core::Common::LoggerRegistrationGuard throwingLoggerGuard(throwingLogger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
        EXPECT_THROW({ const bool published = PublishStagedVolume(stagedPaths, m_directory, m_desc.volumeName.view(), 3u, scratchArena); EXPECT_FALSE(published); }, i32);
    }
    Vector<u8, NWB::Core::Alloc::ScratchArena> loaded(scratchArena);
    ASSERT_TRUE(ReadBinaryFile(firstOutput, loaded));
    ASSERT_EQ(loaded.size(), firstOld.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), firstOld.data(), firstOld.size()), 0);
    ASSERT_TRUE(ReadBinaryFile(secondOutput, loaded));
    ASSERT_EQ(loaded.size(), secondOld.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), secondOld.data(), secondOld.size()), 0);
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

