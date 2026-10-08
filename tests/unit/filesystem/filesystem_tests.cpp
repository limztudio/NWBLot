// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/filesystem/factory.h>
#include <core/filesystem/volume_file_system.h>
#include <core/filesystem/volume_staging.h>
#include <core/filesystem/volume_storage_detail.h>
#include <tests/common/test_context.h>
#include <tests/common/capturing_logger.h>

#include <global/filesystem.h>
#include <global/binary.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_filesystem_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Filesystem;
using Path = NWB::Path;

inline constexpr Name s_TestFile("project/tests/payload");
inline constexpr Name s_TestArena("tests/filesystem");

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
    void benchmarkMetadataFlush(const usize fileCount, const usize iterations){
        m_desc.metadataSize = fileCount == 1u ? 512u : 512u * 1024u;
        m_desc.segmentSize = m_desc.metadataSize * s_ExpectedDualCount;
        auto filesystem = CreateFilesystem(m_arena, m_desc);
        ASSERT_TRUE(filesystem);
        filesystem->reserveFileCapacity(fileCount);
        for(usize index = 0u; index < fileCount; ++index){
            char number[TextDetail::s_DecimalTextBufferBytes] = {};
            const Name path(FormatDecimal(index, number));
            ASSERT_TRUE(filesystem->writeFileDeferred(path, nullptr, 0u));
        }
        ASSERT_TRUE(filesystem->flush());
        const Timer begin = TimerNow();
        for(usize iteration = 0u; iteration < iterations; ++iteration)
            ASSERT_TRUE(filesystem->flush());
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        char durationText[TextDetail::s_DecimalTextBufferBytes] = {};
        const AStringView formattedDuration = FormatDecimal(nanoseconds, durationText);
        durationText[formattedDuration.size()] = '\0';
        RecordProperty("flush_ns", durationText);
        RecordProperty("file_count", static_cast<int>(fileCount));
        RecordProperty("iterations", static_cast<int>(iterations));
        ASSERT_TRUE(filesystem->unmount());
        m_desc.createIfMissing = false;
        m_desc.usage = VolumeUsage::RuntimeReadOnly;
        ASSERT_TRUE(filesystem->mount(m_desc));
        EXPECT_EQ(filesystem->fileCount(), fileCount);
        ASSERT_TRUE(filesystem->unmount());
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
    auto filesystem = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystem);
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
    auto filesystem = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystem);
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

TEST_F(FilesystemVolumeTest, RemountFlushesPendingMetadata){
    auto filesystem = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystem);
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
    auto filesystem = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystem);
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

TEST_F(FilesystemVolumeTest, MetadataImagePreservesCompleteHashesAndClearsRemovedRecords){
    using FilesystemVolumeDetail::VolumeHeaderDisk;
    using FilesystemVolumeDetail::VolumeIndexEntryDisk;
    auto filesystem = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystem);
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

TEST_F(FilesystemVolumeTest, DISABLED_MetadataFlushBenchmarkSingleFile){
    benchmarkMetadataFlush(1u, 1024u);
}

TEST_F(FilesystemVolumeTest, DISABLED_MetadataFlushBenchmark4096Files){
    benchmarkMetadataFlush(4096u, 128u);
}

TEST(FilesystemFactory, PropagatesFactoryAndMountFailures){
    NWB::Tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
    NWB::Core::Alloc::GlobalArena arena(s_TestArena);
    VolumeMountDesc desc(arena);
    const FilesystemFactory emptyFactory = []([[maybe_unused]] NWB::Core::Alloc::GlobalArena& objectArena, [[maybe_unused]] const VolumeMountDesc& mount){
        return UniquePtr<IFilesystem>();
    };
    EXPECT_FALSE(CreateFilesystem(arena, desc, emptyFactory));
    EXPECT_FALSE(CreateFilesystem(arena, desc));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

