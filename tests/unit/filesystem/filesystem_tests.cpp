// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/filesystem/filesystem.h>
#include <core/filesystem/volume_file_system.h>
#include <core/filesystem/volume_staging.h>
#include <tests/common/test_context.h>
#include <tests/common/capturing_logger.h>

#include <global/filesystem.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_filesystem_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Filesystem;
using Path = NWB::Path;

inline constexpr Name s_TestFile("project/tests/payload");
inline constexpr Name s_TestArena("tests/filesystem");

class MemoryFilesystem final : public IFilesystem{
public:
    explicit MemoryFilesystem(NWB::Core::Alloc::GlobalArena& arena)
        : m_bytes(arena)
    {}


public:
    virtual bool mountVolume(const VolumeMountDesc& desc)override{
        m_mounted = true;
        m_writable = desc.usage != VolumeUsage::RuntimeReadOnly;
        return true;
    }
    virtual bool unmountVolume()override{ m_mounted = false; return true; }
    virtual bool mounted()const override{ return m_mounted; }
    virtual bool writable()const override{ return m_mounted && m_writable; }

    virtual bool readFile(const Name& path, const u64 offset, void* data, const usize bytes, usize& outBytesRead)const override{
        outBytesRead = 0;
        if(!fileExists(path) || offset > m_bytes.size() || (bytes != 0 && !data))
            return false;
        outBytesRead = Min(bytes, m_bytes.size() - static_cast<usize>(offset));
        auto* destination = static_cast<u8*>(data);
        for(usize i = 0; i < outBytesRead; ++i)
            destination[i] = m_bytes[static_cast<usize>(offset) + i];
        return true;
    }
    virtual bool seekFile(FileCursor& cursor, const i64 offset, const FileSeekOrigin::Enum origin)const override{
        if(cursor.filesystem != this || !fileExists(cursor.virtualPath))
            return false;
        const u64 base = origin == FileSeekOrigin::End ? m_bytes.size() : origin == FileSeekOrigin::Current ? cursor.offset : 0u;
        if(origin > FileSeekOrigin::End || base > static_cast<u64>(Limit<i64>::s_Max))
            return false;
        if(offset > 0 && static_cast<u64>(offset) > static_cast<u64>(Limit<i64>::s_Max) - base)
            return false;
        const i64 destination = static_cast<i64>(base) + offset;
        if(destination < 0 || static_cast<u64>(destination) > m_bytes.size())
            return false;
        cursor.offset = static_cast<u64>(destination);
        return true;
    }
    virtual bool writeFile(const Name& path, const void* data, const usize bytes)override{
        if(!writable() || !path || (bytes != 0 && !data))
            return false;
        m_bytes.resize(bytes);
        const auto* source = static_cast<const u8*>(data);
        for(usize i = 0; i < bytes; ++i)
            m_bytes[i] = source[i];
        m_path = path;
        return true;
    }
    virtual bool writeFileDeferred(const Name& path, const void* data, const usize bytes)override{
        return writeFile(path, data, bytes);
    }
    virtual bool flush()override{ return mounted(); }
    virtual bool removeFile(const Name& path)override{
        if(!writable() || !fileExists(path))
            return false;
        m_path = NAME_NONE;
        m_bytes.clear();
        return true;
    }
    virtual bool fileExists(const Name& path)const override{ return mounted() && path && m_path == path; }
    virtual bool fileSize(const Name& path, u64& outSize)const override{
        outSize = 0;
        if(!fileExists(path))
            return false;
        outSize = m_bytes.size();
        return true;
    }
    virtual u64 fileCount()const override{ return mounted() && m_path ? 1u : 0u; }
    virtual void reserveFileCapacity([[maybe_unused]] const usize fileCount)override{}
    virtual Vector<Name, VolumeArena> listFiles()const override{
        Vector<Name, VolumeArena> names(m_bytes.get_allocator().arena());
        if(fileCount())
            names.push_back(m_path);
        return names;
    }


private:
    VolumeBytes m_bytes;
    Name m_path;
    bool m_mounted = false;
    bool m_writable = false;
};

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
        ErrorCode error;
        ASSERT_TRUE(RemoveAllIfExists(m_directory, error));
        ASSERT_TRUE(m_desc.volumeName.assign("test"));
        m_desc.mountDirectory = m_directory;
        m_desc.segmentSize = 4096;
        m_desc.metadataSize = 512;
        m_desc.createIfMissing = true;
        m_desc.usage = VolumeUsage::CookWrite;
    }
    virtual void TearDown()override{
        ErrorCode error;
        EXPECT_TRUE(RemoveAllIfExists(m_directory, error));
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

    FileCursor cursor;
    ASSERT_TRUE(filesystem->openFile(s_TestFile, cursor));
    ASSERT_TRUE(filesystem->seekFile(cursor, 3570, FileSeekOrigin::Begin));
    Array<u8, 64> buffer{};
    usize bytesRead = 0;
    ASSERT_TRUE(filesystem->readFile(cursor, buffer.data(), buffer.size(), bytesRead));
    ASSERT_EQ(bytesRead, buffer.size());
    EXPECT_EQ(NWB_MEMCMP(buffer.data(), payload.data() + 3570, bytesRead), 0);
    EXPECT_EQ(cursor.offset, 3634u);
    ASSERT_TRUE(filesystem->seekFile(cursor, -10, FileSeekOrigin::End));
    ASSERT_TRUE(filesystem->readFile(cursor, buffer.data(), buffer.size(), bytesRead));
    EXPECT_EQ(bytesRead, 10u);
    EXPECT_EQ(NWB_MEMCMP(buffer.data(), payload.data() + 9990, bytesRead), 0);
    ASSERT_TRUE(filesystem->readFile(cursor, buffer.data(), buffer.size(), bytesRead));
    EXPECT_EQ(bytesRead, 0u);
    EXPECT_EQ(cursor.offset, 10000u);
    EXPECT_FALSE(filesystem->seekFile(cursor, 1, FileSeekOrigin::Current));
    EXPECT_FALSE(filesystem->seekFile(cursor, -10001, FileSeekOrigin::End));
    EXPECT_FALSE(filesystem->seekFile(cursor, Limit<i64>::s_Min, FileSeekOrigin::Current));
    EXPECT_FALSE(filesystem->seekFile(cursor, Limit<i64>::s_Max, FileSeekOrigin::Current));
    EXPECT_FALSE(filesystem->seekFile(cursor, 0, static_cast<FileSeekOrigin::Enum>(255)));
    EXPECT_EQ(cursor.offset, 10000u);
    filesystem->closeFile(cursor);
    EXPECT_FALSE(filesystem->readFile(cursor, buffer.data(), buffer.size(), bytesRead));
    ASSERT_TRUE(filesystem->unmountVolume());
}

TEST_F(FilesystemVolumeTest, DeferredWritesSurviveUnmountAndReadOnlyMount){
    auto filesystem = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystem);
    Array<u8, 4> payload{ 1u, 3u, 5u, 7u };
    ASSERT_TRUE(filesystem->writeFileDeferred(s_TestFile, payload));
    payload[0] = 99u;
    ASSERT_TRUE(filesystem->unmountVolume());
    m_desc.createIfMissing = false;
    m_desc.usage = VolumeUsage::RuntimeReadOnly;
    ASSERT_TRUE(filesystem->mountVolume(m_desc));
    VolumeBytes loaded(m_arena);
    ASSERT_TRUE(filesystem->readFile(s_TestFile, loaded));
    ASSERT_EQ(loaded.size(), 4u);
    EXPECT_EQ(loaded[0], 1u);
    EXPECT_FALSE(filesystem->writeFile(s_TestFile, payload));
    EXPECT_FALSE(filesystem->removeFile(s_TestFile));
    ASSERT_TRUE(filesystem->unmountVolume());
}

TEST_F(FilesystemVolumeTest, RemountFlushesPendingMetadata){
    auto filesystem = CreateFilesystem(m_arena, m_desc);
    ASSERT_TRUE(filesystem);
    const Array<u8, 3> payload{ 4u, 5u, 6u };
    ASSERT_TRUE(filesystem->writeFileDeferred(s_TestFile, payload));
    m_desc.createIfMissing = false;
    m_desc.usage = VolumeUsage::RuntimeReadOnly;
    ASSERT_TRUE(filesystem->mountVolume(m_desc));
    VolumeBytes loaded(m_arena);
    ASSERT_TRUE(filesystem->readFile(s_TestFile, loaded));
    ASSERT_EQ(loaded.size(), payload.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), payload.data(), loaded.size()), 0);
    ASSERT_TRUE(filesystem->unmountVolume());
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
    EXPECT_FALSE(filesystem->writeFile(NAME_NONE, payload));
    usize bytesRead = 27;
    EXPECT_FALSE(filesystem->readFile(s_TestFile, 1, nullptr, 0, bytesRead));
    EXPECT_EQ(bytesRead, 0u);
    ASSERT_TRUE(filesystem->removeFile(s_TestFile));
    EXPECT_EQ(filesystem->fileCount(), 0u);
    EXPECT_FALSE(filesystem->readFile(s_TestFile, loaded));
    EXPECT_TRUE(loaded.empty());
    ASSERT_TRUE(filesystem->unmountVolume());
}

TEST(FilesystemFactory, UsesCapturedProjectBackendWithoutNativeVolumeFiles){
    NWB::Core::Alloc::GlobalArena arena(s_TestArena);
    VolumeMountDesc desc(arena);
    ASSERT_TRUE(desc.volumeName.assign("downloaded_assets"));
    desc.usage = VolumeUsage::RuntimeReadWrite;
    usize factoryCalls = 0;
    FilesystemFactory factory = [&factoryCalls](NWB::Core::Alloc::GlobalArena& objectArena, const VolumeMountDesc& mount){
        ++factoryCalls;
        EXPECT_EQ(mount.volumeName.view(), AStringView("downloaded_assets"));
        return MakeUnique<MemoryFilesystem>(objectArena);
    };
    auto filesystem = CreateFilesystem(arena, desc, factory);
    ASSERT_TRUE(filesystem);
    EXPECT_EQ(factoryCalls, 1u);
    const Array<u8, 4> payload{ 9u, 8u, 7u, 6u };
    ASSERT_TRUE(filesystem->writeFile(s_TestFile, payload));
    VolumeBytes loaded(arena);
    ASSERT_TRUE(filesystem->readFile(s_TestFile, loaded));
    ASSERT_EQ(loaded.size(), payload.size());
    EXPECT_EQ(NWB_MEMCMP(loaded.data(), payload.data(), loaded.size()), 0);
    FileCursor cursor;
    ASSERT_TRUE(filesystem->openFile(s_TestFile, cursor));
    ASSERT_TRUE(filesystem->seekFile(cursor, -2, FileSeekOrigin::End));
    Array<u8, 4> tail{};
    usize bytesRead = 0;
    ASSERT_TRUE(filesystem->readFile(cursor, tail.data(), tail.size(), bytesRead));
    EXPECT_EQ(bytesRead, 2u);
    EXPECT_EQ(tail[0], 7u);
    EXPECT_EQ(tail[1], 6u);
    EXPECT_TRUE(filesystem->flush());
    EXPECT_TRUE(filesystem->unmountVolume());
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

