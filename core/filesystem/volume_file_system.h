// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "volume_types.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VolumeFileSystem : NoCopy{
private:
    struct FileRecord{
        u64 offset = 0;
        u64 size = 0;
    };

    using SegmentPathVector = Vector<Path, Alloc::GlobalArena>;

    using FileMap = HashMap<Name, FileRecord, Hasher<Name>, EqualTo<Name>, Alloc::GlobalArena>;


public:
    explicit VolumeFileSystem(Alloc::GlobalArena& arena);
    ~VolumeFileSystem();


public:
    bool mount(const VolumeMountDesc& desc);
    void unmount();

    [[nodiscard]] bool mounted()const;
    [[nodiscard]] bool writable()const;

    [[nodiscard]] AStringView volumeName()const;
    [[nodiscard]] Path mountDirectory()const;

    [[nodiscard]] u64 segmentSize()const;
    [[nodiscard]] u64 metadataSize()const;
    [[nodiscard]] usize segmentCount()const;
    [[nodiscard]] u64 fileCount()const;
    [[nodiscard]] u64 usedBytes()const;
    [[nodiscard]] u64 wastedBytes()const;


public:
    bool writeFile(const Name& virtualPath, const void* data, usize bytes);
    bool writeFileDeferred(const Name& virtualPath, const void* data, usize bytes);

    template<typename ByteContainer>
    bool writeFile(const Name& virtualPath, const ByteContainer& data){
        return writeFile(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    template<typename ByteContainer>
    bool writeFileDeferred(const Name& virtualPath, const ByteContainer& data){
        return writeFileDeferred(virtualPath, data.empty() ? nullptr : data.data(), data.size());
    }

    bool flushMetadata();
    void reserveFileCapacity(usize fileCount);

    template<typename ByteContainer>
    bool readFile(const Name& virtualPath, ByteContainer& outData)const;
    bool removeFile(const Name& virtualPath);

    bool fileExists(const Name& virtualPath)const;
    bool fileSize(const Name& virtualPath, u64& outSize)const;

    Vector<Name, VolumeArena> listFiles()const;
    bool compact(bool shrinkSegments = true);


private:
    struct MetadataFlushMode{
        enum Enum : u8{
            Deferred = 0u,
            Immediate = 1u,
        };
    };

    bool writeFileLocked(const Name& virtualPath, const void* data, usize bytes, MetadataFlushMode::Enum flushMode);
    bool scanSegmentsLocked();

    bool createSegmentLocked(usize segmentIndex);
    bool ensureCapacityLocked(u64 requiredBytes);

    bool loadMetadataLocked();
    bool flushMetadataLocked();
    bool canFitMetadataForFileCountLocked(u64 fileCount)const;
    bool readFileRecordLocked(const Name& virtualPath, FileRecord& outRecord)const;
    bool computePhysicalCapacityLocked(u64& outCapacityBytes)const;

    bool readBytesLocked(u64 offset, void* data, u64 byteCount)const;
    bool writeBytesLocked(u64 offset, const void* data, u64 byteCount);
    bool moveBytesLocked(u64 destinationOffset, u64 sourceOffset, u64 byteCount);
    bool trimSegmentsForNextFreeOffsetLocked();

    void unmountLocked();
    Path segmentPath(usize segmentIndex)const;


private:
    Path m_mountDirectory;
    ACompactString m_volumeName;

    mutable Futex m_mutex;
    VolumeUsage::Enum m_usage = VolumeUsage::RuntimeReadOnly;
    bool m_mounted = false;
    bool m_writable = false;

    u64 m_segmentSize = 0;
    u64 m_metadataBytes = 0;
    u64 m_nextFreeOffset = 0;
    usize m_maxSegments = 0;

    Alloc::GlobalArena& m_arena;
    SegmentPathVector m_segmentPaths;
    FileMap m_files;
};


template<typename ByteContainer>
bool VolumeFileSystem::readFile(const Name& virtualPath, ByteContainer& outData)const{
    ScopedLock lock(m_mutex);
    outData.clear();

    FileRecord record;
    if(!readFileRecordLocked(virtualPath, record))
        return false;

    if(record.size > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): readFile failed: file size {} exceeds runtime buffer limit {}")
            , StringConvert(m_volumeName.view())
            , record.size
            , static_cast<u64>(Limit<usize>::s_Max)
        );
        return false;
    }

    outData.resize(static_cast<usize>(record.size));
    if(record.size == 0)
        return true;

    if(readBytesLocked(record.offset, outData.data(), record.size))
        return true;

    NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): readFile failed: payload read failed"), StringConvert(m_volumeName.view()));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
