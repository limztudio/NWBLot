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
    virtual b8 mount(const VolumeMountDesc& desc)override;
    virtual b8 unmount()override;

    [[nodiscard]] virtual b8 mounted()const noexcept override;
    [[nodiscard]] virtual b8 writable()const noexcept override;

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
    virtual b8 writeFile(const Name& virtualPath, const void* data, usize bytes)override;
    virtual b8 writeFileDeferred(const Name& virtualPath, const void* data, usize bytes)override;
    virtual b8 flush()override;
    virtual void reserveFileCapacity(usize fileCount)override;

    [[nodiscard]] virtual Expected<usize> readFile(const Name& virtualPath, u64 offset, void* data, usize bytes)const override;
    virtual b8 seekFile(FileCursor& cursor, i64 offset, FileSeekOrigin::Enum origin)const noexcept(!NWB_OCCUR_WARNING)override;
    virtual b8 removeFile(const Name& virtualPath)override;
    virtual b8 fileExists(const Name& virtualPath)const noexcept override;
    [[nodiscard]] virtual Expected<u64> fileSize(const Name& virtualPath)const noexcept(!NWB_OCCUR_WARNING)override;
    virtual Vector<Name, VolumeArena> listFiles()const override;
    b8 compact(b8 shrinkSegments = true);


private:
    b8 writeFileLocked(const Name& virtualPath, const void* data, usize bytes, MetadataFlushMode::Enum flushMode);
    b8 scanSegmentsLocked();

    b8 createSegmentLocked(usize segmentIndex);
    b8 ensureCapacityLocked(u64 requiredBytes);
    Expected<u64> computeLogicalCapacityLocked()const noexcept;

    b8 loadMetadataLocked(Alloc::ScratchArena& scratchArena);
    b8 flushMetadataLocked(Alloc::ScratchArena& scratchArena, const Name* excludedPath = nullptr);
    b8 canFitMetadataForFileCountLocked(u64 fileCount)const noexcept;
    [[nodiscard]] Expected<FileRecord> readFileRecordLocked(const Name& virtualPath)const noexcept(!NWB_OCCUR_WARNING);
    Expected<u64, ErrorCode> computePhysicalCapacityLocked()const noexcept(!NWB_OCCUR_WARNING);

    b8 readBytesLocked(u64 offset, void* data, u64 byteCount)const;
    b8 writeBytesLocked(u64 offset, const void* data, u64 byteCount);
    b8 trimSegmentsForNextFreeOffsetLocked();

    void unmountLocked()noexcept;
    Path segmentPath(usize segmentIndex)const;


private:
    Path m_mountDirectory;
    ACompactString m_volumeName;


    u64 m_segmentSize = 0;
    u64 m_metadataBytes = 0;
    u64 m_nextFreeOffset = 0;
    usize m_maxSegments = 0;

    Alloc::GlobalArena& m_arena;
    SegmentPathVector m_segmentPaths;
    FileMap m_files;
    mutable Futex m_mutex;

    VolumeUsage::Enum m_usage = VolumeUsage::RuntimeReadOnly;
    b8 m_mounted = false;
    b8 m_writable = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

