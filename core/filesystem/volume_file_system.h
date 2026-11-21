// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "filesystem.h"

#include <core/alloc/scratch.h>
#include <core/filesystem/volume_naming.h>

#if !defined(FOUNDATION_PLATFORM_WINDOWS)
#include <pthread.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FilesystemVolumeDetail{
    class SegmentTransferCache;
};


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

    struct ReadOnlySegmentMapping{
    #if defined(FOUNDATION_PLATFORM_WINDOWS)
        void* fileHandle = nullptr;
        void* mappingHandle = nullptr;
    #else
        int fileDescriptor = -1;
    #endif
        const u8* mappedView = nullptr;
        u64 mappedBytes = 0u;
    };

#if defined(FOUNDATION_PLATFORM_WINDOWS)
    class VolumeRWLock final : NoCopy{
    public:
        constexpr VolumeRWLock()noexcept = default;
        void lockShared()const noexcept{ AcquireSRWLockShared(&m_lock); }
        void unlockShared()const noexcept{ ReleaseSRWLockShared(&m_lock); }
        void lockExclusive()const noexcept{ AcquireSRWLockExclusive(&m_lock); }
        void unlockExclusive()const noexcept{ ReleaseSRWLockExclusive(&m_lock); }
    private:
        mutable SRWLOCK m_lock = SRWLOCK_INIT;
    };
#else
    class VolumeRWLock final : NoCopy{
    public:
        VolumeRWLock()noexcept{ pthread_rwlock_init(&m_lock, nullptr); }
        ~VolumeRWLock()noexcept{ pthread_rwlock_destroy(&m_lock); }
        void lockShared()const noexcept{ pthread_rwlock_rdlock(&m_lock); }
        void unlockShared()const noexcept{ pthread_rwlock_unlock(&m_lock); }
        void lockExclusive()const noexcept{ pthread_rwlock_wrlock(&m_lock); }
        void unlockExclusive()const noexcept{ pthread_rwlock_unlock(&m_lock); }
    private:
        mutable pthread_rwlock_t m_lock{};
    };
#endif

    class SharedLockGuard final : NoCopy{
    public:
        explicit SharedLockGuard(const VolumeRWLock& lock)noexcept
            : m_lock(lock)
        {
            m_lock.lockShared();
        }
        ~SharedLockGuard()noexcept{ m_lock.unlockShared(); }
    private:
        const VolumeRWLock& m_lock;
    };

    class ExclusiveLockGuard final : NoCopy{
    public:
        explicit ExclusiveLockGuard(const VolumeRWLock& lock)noexcept
            : m_lock(lock)
        {
            m_lock.lockExclusive();
        }
        ~ExclusiveLockGuard()noexcept{ m_lock.unlockExclusive(); }
    private:
        const VolumeRWLock& m_lock;
    };


private:
    using SegmentPathVector = Vector<Path, Alloc::GlobalArena>;
    using ReadMappingVector = Vector<ReadOnlySegmentMapping, Alloc::GlobalArena>;

    using FileMap = HashMap<Name, FileRecord, Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>>;


public:
    explicit VolumeFileSystem(Alloc::GlobalArena& arena)
        : m_mountDirectory(arena)
        , m_arena(arena)
        , m_segmentPaths(m_arena)
        , m_readMappings(m_arena)
        , m_files(0, Hasher<Name>(), EqualTo<Name>(), m_arena)
    {}
    virtual ~VolumeFileSystem()noexcept override;


public:
    virtual b8 mount(const VolumeMountDesc& desc)override;
    virtual b8 unmount()override;

    [[nodiscard]] virtual b8 mounted()const noexcept override;
    [[nodiscard]] virtual b8 writable()const noexcept override;

    [[nodiscard]] usize segmentCount()const noexcept;
    [[nodiscard]] virtual u64 fileCount()const noexcept override;
    [[nodiscard]] Expected<u64> wastedBytes()const noexcept;


public:
    using IFilesystem::readFile;
    using IFilesystem::writeFile;
    using IFilesystem::writeFileDeferred;


public:
    virtual b8 writeFile(const Name& virtualPath, const void* data, usize bytes)override;
    virtual b8 writeFileDeferred(const Name& virtualPath, const void* data, usize bytes)override;
    virtual b8 writeFilesDeferred(const DeferredWriteItem* items, usize count)override;
    virtual b8 flush()override{ return compact(true); }
    virtual void reserveFileCapacity(usize fileCount)override;

    [[nodiscard]] virtual Expected<usize> readFile(const Name& virtualPath, u64 offset, void* data, usize bytes)const override;
    [[nodiscard]] virtual Expected<usize> readWholeFile(const Name& virtualPath, void* context, u8* (*prepareBuffer)(void* context, usize size))const override;
    virtual b8 seekFile(FileCursor& cursor, i64 offset, FileSeekOrigin::Enum origin)const noexcept(!FOUNDATION_OCCUR_WARNING)override;
    virtual b8 removeFile(const Name& virtualPath)override;
    virtual b8 fileExists(const Name& virtualPath)const noexcept override;
    [[nodiscard]] virtual Expected<u64> fileSize(const Name& virtualPath)const noexcept(!FOUNDATION_OCCUR_WARNING)override;
    b8 compact(b8 shrinkSegments = true);


private:
    b8 writeFileLocked(const Name& virtualPath, const void* data, usize bytes, MetadataFlushMode::Enum flushMode, Alloc::ScratchArena* flushScratchArena = nullptr);
    b8 scanSegmentsLocked();

    b8 createSegmentLocked(usize segmentIndex);
    b8 ensureCapacityLocked(u64 requiredBytes);
    Expected<u64> computeLogicalCapacityLocked()const noexcept;

    b8 loadMetadataLocked(Alloc::ScratchArena& scratchArena);
    b8 flushMetadataLocked(Alloc::ScratchArena& scratchArena, const Name& excludedPath = s_NameNone);
    b8 canFitMetadataForFileCountLocked(u64 fileCount)const noexcept;
    [[nodiscard]] Expected<FileRecord> readFileRecordLocked(const Name& virtualPath)const noexcept(!FOUNDATION_OCCUR_WARNING);
    Expected<u64, ErrorCode> computePhysicalCapacityLocked()const noexcept(!FOUNDATION_OCCUR_WARNING);

    b8 readBytesLocked(u64 offset, void* data, u64 byteCount)const;
    b8 writeBytesLocked(u64 offset, const void* data, u64 byteCount);
    b8 trimSegmentsForNextFreeOffsetLocked();
    void openReadMappingsLocked()noexcept;
    void closeReadMappingsLocked()noexcept;

    void unmountLocked()noexcept;
    Path segmentPath(const usize segmentIndex)const{
        return MakeVolumeSegmentPath(m_mountDirectory, m_volumeName.view(), segmentIndex);
    }


private:
    Path m_mountDirectory;
    ACompactString m_volumeName;


    u64 m_segmentSize = 0;
    u64 m_metadataBytes = 0;
    u64 m_nextFreeOffset = 0;
    usize m_maxSegments = 0;

    Alloc::GlobalArena& m_arena;
    SegmentPathVector m_segmentPaths;
    ReadMappingVector m_readMappings;
    FilesystemVolumeDetail::SegmentTransferCache* m_activeWriteCache = nullptr;
    FileMap m_files;
    mutable VolumeRWLock m_mutex;

    b8 m_mounted = false;
    b8 m_writable = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

