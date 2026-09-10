// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "filesystem.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VolumeFileSystem final : public IFilesystem, NoCopy{
private:
    struct MetadataFlushMode{
        enum Enum : u8{
            Deferred = 0u,
            Immediate = 1u,
        };
    };

    struct FileRecord{
        u64 offset = 0;
        u64 size = 0;
    };

    using SegmentPathVector = Vector<Path, Alloc::GlobalArena>;

    using FileMap = HashMap<Name, FileRecord, Hasher<Name>, EqualTo<Name>, Alloc::GlobalArena>;


public:
    explicit VolumeFileSystem(Alloc::GlobalArena& arena);
    virtual ~VolumeFileSystem()override;


public:
    virtual bool mount(const VolumeMountDesc& desc)override;
    virtual bool unmount()override;

    [[nodiscard]] virtual bool mounted()const override;
    [[nodiscard]] virtual bool writable()const override;

    [[nodiscard]] AStringView volumeName()const;
    [[nodiscard]] Path mountDirectory()const;

    [[nodiscard]] u64 segmentSize()const;
    [[nodiscard]] u64 metadataSize()const;
    [[nodiscard]] usize segmentCount()const;
    [[nodiscard]] virtual u64 fileCount()const override;
    [[nodiscard]] u64 usedBytes()const;
    [[nodiscard]] u64 wastedBytes()const;


public:
    using IFilesystem::readFile;
    using IFilesystem::writeFile;
    using IFilesystem::writeFileDeferred;


public:
    virtual bool writeFile(const Name& virtualPath, const void* data, usize bytes)override;
    virtual bool writeFileDeferred(const Name& virtualPath, const void* data, usize bytes)override;
    virtual bool flush()override;
    virtual void reserveFileCapacity(usize fileCount)override;

    virtual bool readFile(const Name& virtualPath, u64 offset, void* data, usize bytes, usize& outBytesRead)const override;
    virtual bool seekFile(FileCursor& cursor, i64 offset, FileSeekOrigin::Enum origin)const override;
    virtual bool removeFile(const Name& virtualPath)override;
    virtual bool fileExists(const Name& virtualPath)const override;
    virtual bool fileSize(const Name& virtualPath, u64& outSize)const override;
    virtual Vector<Name, VolumeArena> listFiles()const override;
    bool compact(bool shrinkSegments = true);


private:
    bool writeFileLocked(const Name& virtualPath, const void* data, usize bytes, MetadataFlushMode::Enum flushMode);
    bool scanSegmentsLocked();

    bool createSegmentLocked(usize segmentIndex);
    bool ensureCapacityLocked(u64 requiredBytes);
    bool computeLogicalCapacityLocked(u64& outCapacityBytes)const;

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

    u64 m_segmentSize = 0;
    u64 m_metadataBytes = 0;
    u64 m_nextFreeOffset = 0;
    usize m_maxSegments = 0;

    Alloc::GlobalArena& m_arena;
    SegmentPathVector m_segmentPaths;
    FileMap m_files;

    VolumeUsage::Enum m_usage = VolumeUsage::RuntimeReadOnly;
    bool m_mounted = false;
    bool m_writable = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

