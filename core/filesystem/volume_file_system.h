// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "filesystem.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VolumeFileSystem final : public IFilesystem, NoCopy{
private:
    struct MetadataFlushMode{
        static constexpr u8 s_DeferredBase = 0u;
        enum Enum : u8{
            Deferred = s_DeferredBase,
            Immediate,
        };
    };

    struct FileRecord{
        u64 offset = 0;
        u64 size = 0;
    };


private:
    using SegmentPathVector = Vector<Path, Alloc::GlobalArena>;

    using FileMap = HashMap<Name, FileRecord, Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>>;


public:
    explicit VolumeFileSystem(Alloc::GlobalArena& arena);
    virtual ~VolumeFileSystem()noexcept override = default;


public:
    virtual bool mount(const VolumeMountDesc& desc)override;
    virtual bool unmount()override;

    [[nodiscard]] virtual bool mounted()const noexcept override;
    [[nodiscard]] virtual bool writable()const noexcept override;

    [[nodiscard]] AStringView volumeName()const noexcept;
    [[nodiscard]] Path mountDirectory()const;

    [[nodiscard]] u64 segmentSize()const noexcept;
    [[nodiscard]] u64 metadataSize()const noexcept;
    [[nodiscard]] usize segmentCount()const noexcept;
    [[nodiscard]] virtual u64 fileCount()const noexcept override;
    [[nodiscard]] Expected<u64> usedBytes()const noexcept;
    [[nodiscard]] Expected<u64> wastedBytes()const noexcept;


public:
    using IFilesystem::readFile;
    using IFilesystem::writeFile;
    using IFilesystem::writeFileDeferred;


public:
    virtual bool writeFile(const Name& virtualPath, const void* data, usize bytes)override;
    virtual bool writeFileDeferred(const Name& virtualPath, const void* data, usize bytes)override;
    virtual bool flush()override;
    virtual void reserveFileCapacity(usize fileCount)override;

    [[nodiscard]] virtual Expected<usize> readFile(const Name& virtualPath, u64 offset, void* data, usize bytes)const override;
    virtual bool seekFile(FileCursor& cursor, i64 offset, FileSeekOrigin::Enum origin)const noexcept(!NWB_OCCUR_WARNING)override;
    virtual bool removeFile(const Name& virtualPath)override;
    virtual bool fileExists(const Name& virtualPath)const noexcept override;
    [[nodiscard]] virtual Expected<u64> fileSize(const Name& virtualPath)const noexcept(!NWB_OCCUR_WARNING)override;
    virtual Vector<Name, VolumeArena> listFiles()const override;
    bool compact(bool shrinkSegments = true);


private:
    bool writeFileLocked(const Name& virtualPath, const void* data, usize bytes, MetadataFlushMode::Enum flushMode);
    bool scanSegmentsLocked();

    bool createSegmentLocked(usize segmentIndex);
    bool ensureCapacityLocked(u64 requiredBytes);
    Expected<u64> computeLogicalCapacityLocked()const noexcept;

    bool loadMetadataLocked(Alloc::ScratchArena& scratchArena);
    bool flushMetadataLocked(Alloc::ScratchArena& scratchArena, const Name* excludedPath = nullptr);
    bool canFitMetadataForFileCountLocked(u64 fileCount)const noexcept;
    [[nodiscard]] Expected<FileRecord> readFileRecordLocked(const Name& virtualPath)const noexcept(!NWB_OCCUR_WARNING);
    Expected<u64, ErrorCode> computePhysicalCapacityLocked()const noexcept(!NWB_OCCUR_WARNING);

    bool readBytesLocked(u64 offset, void* data, u64 byteCount)const;
    bool writeBytesLocked(u64 offset, const void* data, u64 byteCount);
    bool trimSegmentsForNextFreeOffsetLocked();

    void unmountLocked()noexcept;
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

