// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_file_system.h"
#include "volume_segment_transfer.h"
#include "volume_storage_detail.h"
#include "arena_names.h"

#include <global/overflow.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>

#if !defined(FOUNDATION_PLATFORM_WINDOWS)
#include <sys/mman.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VolumeFileSystem::~VolumeFileSystem()noexcept{
    closeReadMappingsLocked();
}

b8 VolumeFileSystem::mount(const VolumeMountDesc& desc){
    Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_LoadMetadataScratch);

    ExclusiveLockGuard lock(m_mutex);
    if(m_mounted && m_writable && !flushMetadataLocked(scratchArena))
        return false;
    unmountLocked();

    if(!ValidVolumeName(desc.volumeName.view())){
        FilesystemVolumeDetail::LogFailure(desc.volumeName.view(), FilesystemVolumeDetail::s_VolumeOpMount, FilesystemVolumeDetail::s_VolumeDetailInvalidVolumeName);
        return false;
    }

    m_volumeName = desc.volumeName;
    m_mountDirectory = desc.mountDirectory.empty() ? Path(m_mountDirectory.arena(), ".") : desc.mountDirectory;
    m_writable = (desc.usage != VolumeUsage::RuntimeReadOnly);
    m_maxSegments = desc.maxSegments;

    const auto mountExists = FileExists(m_mountDirectory);
    if(!mountExists){
        FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMountExists, m_mountDirectory, mountExists.error());
        return false;
    }
    if(!*mountExists){
        if(!desc.createIfMissing || !m_writable){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMount, "mount directory does not exist and creation is disabled");
            return false;
        }

        if(const auto directories = EnsureDirectories(m_mountDirectory); !directories){
            FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMountCreateDirectories, m_mountDirectory, directories.error());
            return false;
        }
    }
    else if(const auto directory = IsDirectory(m_mountDirectory); !directory || !*directory){
        if(!directory)
            FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMountIsDirectory, m_mountDirectory, directory.error());
        else
            FilesystemVolumeDetail::LogFailureWithPath(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMountIsDirectory, m_mountDirectory, "path is not a directory");
        return false;
    }

    if(!scanSegmentsLocked()){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMount, "segment scan failed");
        unmountLocked();
        return false;
    }

    if(m_segmentPaths.empty()){
        if(!desc.createIfMissing || !m_writable || desc.segmentSize == 0){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMount, "volume does not exist and creation parameters are invalid");
            unmountLocked();
            return false;
        }

        m_segmentSize = desc.segmentSize;
        m_metadataBytes = desc.metadataSize == 0 ? FilesystemVolumeDetail::DefaultMetadataBytes(m_segmentSize) : desc.metadataSize;

        if(m_metadataBytes <= sizeof(FilesystemVolumeDetail::VolumeHeaderDisk) || m_metadataBytes >= m_segmentSize){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMount, "metadata size is outside valid segment bounds");
            unmountLocked();
            return false;
        }

        m_nextFreeOffset = m_metadataBytes;

        if(!createSegmentLocked(0)){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMount, "failed to create first segment");
            unmountLocked();
            return false;
        }
        if(!flushMetadataLocked(scratchArena)){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMount, "failed to write initial metadata");
            unmountLocked();
            return false;
        }
    }
    else{
        const auto decodedHeader = FilesystemVolumeDetail::ReadVolumeHeaderFromSegment(m_volumeName, m_segmentPaths[0]);
        if(!decodedHeader){
            unmountLocked();
            return false;
        }
        const auto& discoveredHeader = *decodedHeader;
        if(discoveredHeader.magic != FilesystemVolumeDetail::s_VolumeMagic){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMount, FilesystemVolumeDetail::s_VolumeDetailMagicMismatch);
            unmountLocked();
            return false;
        }
        if(discoveredHeader.segmentSize == 0){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMount, FilesystemVolumeDetail::s_VolumeDetailSegmentSizeZero);
            unmountLocked();
            return false;
        }
        m_segmentSize = discoveredHeader.segmentSize;

        const usize segmentPathCount = m_segmentPaths.size();
        for(usize segmentIndex = 0u; segmentIndex < segmentPathCount; ++segmentIndex){
            const Path& segmentPath = m_segmentPaths[segmentIndex];
            const auto fileSizeResult = FileSize(segmentPath);
            if(!fileSizeResult){
                FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMountFileSize, segmentPath, fileSizeResult.error());
                unmountLocked();
                return false;
            }
            const u64 segmentFileSize = *fileSizeResult;
            if(segmentFileSize == 0){
                FilesystemVolumeDetail::LogFailureWithPath(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMountFileSize, segmentPath, FilesystemVolumeDetail::s_VolumeDetailSegmentSizeZero);
                unmountLocked();
                return false;
            }

            const b8 isLastSegment = segmentIndex + 1u == segmentPathCount;
            if(!isLastSegment && segmentFileSize != m_segmentSize){
                NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): mount failed: segment '{}' has size {}, expected {}"), ConvertString(m_volumeName), ConvertString(segmentPath.native()), segmentFileSize, m_segmentSize);
                unmountLocked();
                return false;
            }
            if(isLastSegment && segmentFileSize > m_segmentSize){
                NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): mount failed: final segment '{}' has size {}, exceeding logical segment size {}"), ConvertString(m_volumeName), ConvertString(segmentPath.native()), segmentFileSize, m_segmentSize);
                unmountLocked();
                return false;
            }
        }

        const auto firstFileSizeResult = FileSize(m_segmentPaths[0]);
        if(!firstFileSizeResult){
            FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMountFileSize, m_segmentPaths[0], firstFileSizeResult.error());
            unmountLocked();
            return false;
        }
        if(*firstFileSizeResult < sizeof(FilesystemVolumeDetail::VolumeHeaderDisk)){
            FilesystemVolumeDetail::LogFailureWithPath(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMountFileSize, m_segmentPaths[0], "segment is smaller than the volume header");
            unmountLocked();
            return false;
        }

        if(desc.segmentSize != 0 && desc.segmentSize != m_segmentSize){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): mount failed: requested segment size {} does not match volume size {}"), ConvertString(m_volumeName), desc.segmentSize, m_segmentSize);
            unmountLocked();
            return false;
        }

        if(!loadMetadataLocked(scratchArena)){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMount, "metadata load failed");
            unmountLocked();
            return false;
        }

        if(desc.metadataSize != 0 && desc.metadataSize != m_metadataBytes){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): mount failed: requested metadata size {} does not match volume metadata size {}"), ConvertString(m_volumeName), desc.metadataSize, m_metadataBytes);
            unmountLocked();
            return false;
        }
    }

    if(m_maxSegments != 0 && m_segmentPaths.size() > m_maxSegments){
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): mount failed: discovered {} segments, maxSegments is {}"), ConvertString(m_volumeName), m_segmentPaths.size(), m_maxSegments);
        unmountLocked();
        return false;
    }

    if(!m_writable)
        openReadMappingsLocked();

    m_mounted = true;
    return true;
}

b8 VolumeFileSystem::unmount(){
    ExclusiveLockGuard lock(m_mutex);
    if(m_mounted && m_writable){
        Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_SaveMetadataScratch);
        if(!flushMetadataLocked(scratchArena))
            return false;
    }
    unmountLocked();
    return true;
}

b8 VolumeFileSystem::mounted()const noexcept{
    SharedLockGuard lock(m_mutex);
    return m_mounted;
}

b8 VolumeFileSystem::writable()const noexcept{
    SharedLockGuard lock(m_mutex);
    return m_mounted && m_writable;
}

usize VolumeFileSystem::segmentCount()const noexcept{
    SharedLockGuard lock(m_mutex);
    return m_segmentPaths.size();
}

u64 VolumeFileSystem::fileCount()const noexcept{
    SharedLockGuard lock(m_mutex);
    return static_cast<u64>(m_files.size());
}

Expected<u64> VolumeFileSystem::wastedBytes()const noexcept{
    SharedLockGuard lock(m_mutex);
    if(m_nextFreeOffset <= m_metadataBytes)
        return 0;

    u64 totalUsedBytes = 0;
    for(const auto& [_, record] : m_files){
        const auto totalUsedBytesResult = ::AddNoOverflow(totalUsedBytes, record.size);
        if(!totalUsedBytesResult)
            return MakeUnexpected(totalUsedBytesResult.error());
        totalUsedBytes = *totalUsedBytesResult;
    }

    const u64 payloadSpan = m_nextFreeOffset - m_metadataBytes;
    if(totalUsedBytes >= payloadSpan)
        return 0;

    return payloadSpan - totalUsedBytes;
}


b8 VolumeFileSystem::writeFile(const Name& virtualPath, const void* data, const usize bytes){
    ExclusiveLockGuard lock(m_mutex);
    Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_SaveMetadataScratch);
    FilesystemVolumeDetail::SegmentTransferCache writeCache(scratchArena, true);
    m_activeWriteCache = AddressOf(writeCache);
    ScopeExit clearCache([this, &writeCache]()noexcept{
        m_activeWriteCache = nullptr;
        writeCache.closeAll();
    });
    return writeFileLocked(virtualPath, data, bytes, MetadataFlushMode::Immediate, AddressOf(scratchArena));
}

b8 VolumeFileSystem::writeFileDeferred(const Name& virtualPath, const void* data, const usize bytes){
    ExclusiveLockGuard lock(m_mutex);
    return writeFileLocked(virtualPath, data, bytes, MetadataFlushMode::Deferred);
}

b8 VolumeFileSystem::writeFilesDeferred(const DeferredWriteItem* const items, const usize count){
    if(count == 0u)
        return true;
    if(!items){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, FilesystemVolumeDetail::s_VolumeDetailInvalidArguments);
        return false;
    }

    ExclusiveLockGuard lock(m_mutex);
    if(!m_mounted || !m_writable){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, FilesystemVolumeDetail::s_VolumeDetailNotMountedWritable);
        return false;
    }

    if(count <= Limit<usize>::s_Max - m_files.size())
        m_files.reserve(m_files.size() + count);

    Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_SaveMetadataScratch);
    FilesystemVolumeDetail::SegmentTransferCache writeCache(scratchArena, true);
    m_activeWriteCache = AddressOf(writeCache);
    ScopeExit clearCache([this, &writeCache]()noexcept{
        m_activeWriteCache = nullptr;
        writeCache.closeAll();
    });

    for(usize i = 0u; i < count; ++i){
        if(!writeFileLocked(items[i].virtualPath, items[i].data, items[i].bytes, MetadataFlushMode::Deferred))
            return false;
    }
    return true;
}

void VolumeFileSystem::reserveFileCapacity(const usize fileCount){
    ExclusiveLockGuard lock(m_mutex);
    if(fileCount > m_files.size())
        m_files.reserve(fileCount);
}

Expected<usize> VolumeFileSystem::readFile(const Name& virtualPath, const u64 offset, void* data, const usize bytes)const{
    SharedLockGuard lock(m_mutex);
    const auto record = readFileRecordLocked(virtualPath);
    if(!record)
        return MakeUnexpected(record.error());
    if(offset > record->size || (bytes != 0 && data == nullptr)){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpReadFile, "invalid read offset or buffer");
        return MakeUnexpected(Failure{});
    }
    const u64 readSize = Min<u64>(static_cast<u64>(bytes), record->size - offset);
    if(readSize != 0 && !readBytesLocked(record->offset + offset, data, readSize))
        return MakeUnexpected(Failure{});
    return static_cast<usize>(readSize);
}

Expected<usize> VolumeFileSystem::readWholeFile(const Name& virtualPath, void* const context, u8* (* const prepareBuffer)(void* context, usize size))const{
    if(!prepareBuffer)
        return MakeUnexpected(Failure{});
    SharedLockGuard lock(m_mutex);
    const auto record = readFileRecordLocked(virtualPath);
    if(!record)
        return MakeUnexpected(record.error());
    if(record->size > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Filesystem: file exceeds the runtime buffer limit"));
        return MakeUnexpected(Failure{});
    }
    const usize byteCount = static_cast<usize>(record->size);
    u8* const destination = prepareBuffer(context, byteCount);
    if(byteCount != 0u && !destination)
        return MakeUnexpected(Failure{});
    if(byteCount != 0u && !readBytesLocked(record->offset, destination, record->size))
        return MakeUnexpected(Failure{});
    return byteCount;
}

b8 VolumeFileSystem::seekFile(FileCursor& cursor, const i64 offset, const FileSeekOrigin::Enum origin)const noexcept(!FOUNDATION_OCCUR_WARNING){
    SharedLockGuard lock(m_mutex);
    if(cursor.filesystem != this)
        return false;
    const auto record = readFileRecordLocked(cursor.virtualPath);
    if(!record)
        return false;

    u64 base = 0;
    switch(origin){
    case FileSeekOrigin::Begin: break;
    case FileSeekOrigin::Current: base = cursor.offset; break;
    case FileSeekOrigin::End: base = record->size; break;
    default: return false;
    }

    const auto destination = AddSignedOffset(base, offset);
    if(!destination || *destination > record->size)
        return false;
    cursor.offset = *destination;
    return true;
}

b8 VolumeFileSystem::removeFile(const Name& virtualPath){
    ExclusiveLockGuard lock(m_mutex);
    if(!m_mounted || !m_writable){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpRemoveFile, FilesystemVolumeDetail::s_VolumeDetailNotMountedWritable);
        return false;
    }
    if(!virtualPath){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpRemoveFile, FilesystemVolumeDetail::s_VolumeDetailInvalidVirtualPath);
        return false;
    }
    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpRemoveFile, FilesystemVolumeDetail::s_VolumeDetailFileNotFound);
        return false;
    }

    Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_SaveMetadataScratch);
    if(flushMetadataLocked(scratchArena, virtualPath)){
        m_files.erase(itr);
        return true;
    }
    FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpRemoveFile, "failed to flush metadata before erase");
    return false;
}

b8 VolumeFileSystem::fileExists(const Name& virtualPath)const noexcept{
    SharedLockGuard lock(m_mutex);
    return m_mounted && virtualPath && m_files.find(virtualPath) != m_files.end();
}

Expected<u64> VolumeFileSystem::fileSize(const Name& virtualPath)const noexcept(!FOUNDATION_OCCUR_WARNING){
    SharedLockGuard lock(m_mutex);
    if(!m_mounted){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpFileSize, FilesystemVolumeDetail::s_VolumeDetailNotMounted);
        return MakeUnexpected(Failure{});
    }
    if(!virtualPath){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpFileSize, FilesystemVolumeDetail::s_VolumeDetailInvalidVirtualPath);
        return MakeUnexpected(Failure{});
    }

    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpFileSize, FilesystemVolumeDetail::s_VolumeDetailFileNotFound);
        return MakeUnexpected(Failure{});
    }

    return itr.value().size;
}


b8 VolumeFileSystem::writeFileLocked(const Name& virtualPath, const void* data, const usize bytes, const MetadataFlushMode::Enum flushMode, Alloc::ScratchArena* const flushScratchArena){
    if(!m_mounted || !m_writable){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, FilesystemVolumeDetail::s_VolumeDetailNotMountedWritable);
        return false;
    }
    if(!virtualPath){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, FilesystemVolumeDetail::s_VolumeDetailInvalidVirtualPath);
        return false;
    }
    if(bytes != 0 && data == nullptr){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, "data pointer is null while byte count is non-zero");
        return false;
    }

    const auto itrFind = m_files.find(virtualPath);
    const b8 existed = itrFind != m_files.end();

    u64 fileCountAfterWrite = static_cast<u64>(m_files.size());
    if(!existed){
        if(fileCountAfterWrite == Limit<u64>::s_Max){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, "file count overflow");
            return false;
        }
        ++fileCountAfterWrite;
    }
    if(!canFitMetadataForFileCountLocked(fileCountAfterWrite)){
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): writeFile failed: metadata area is full for file count {}"), ConvertString(m_volumeName), fileCountAfterWrite);
        return false;
    }

    const u64 byteCount = static_cast<u64>(bytes);
    u64 newFileEnd = 0;
    const auto newFileEndResult = ::AddNoOverflow(m_nextFreeOffset, byteCount);
    if(!newFileEndResult){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, "offset overflow while reserving payload bytes");
        return false;
    }
    newFileEnd = *newFileEndResult;
    if(!ensureCapacityLocked(newFileEnd))
        return false;

    const u64 writeOffset = m_nextFreeOffset;
    if(byteCount > 0 && !writeBytesLocked(writeOffset, data, byteCount))
        return false;

    FileRecord previousRecord;
    if(existed)
        previousRecord = itrFind.value();
    const u64 previousNextFreeOffset = m_nextFreeOffset;

    const auto inserted = m_files.insert_or_assign(virtualPath, FileRecord{ writeOffset, byteCount });
    m_nextFreeOffset = newFileEnd;
    ScopeExit rollback([&]()noexcept{
        if(existed)
            inserted.first.value() = previousRecord;
        else
            m_files.erase(inserted.first);
        m_nextFreeOffset = previousNextFreeOffset;
    });

    if(flushMode == MetadataFlushMode::Deferred){
        rollback.release();
        return true;
    }

    b8 flushed = false;
    if(flushScratchArena)
        flushed = flushMetadataLocked(*flushScratchArena);
    else{
        Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_SaveMetadataScratch);
        flushed = flushMetadataLocked(scratchArena);
    }
    if(flushed){
        rollback.release();
        return true;
    }
    FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, "failed to flush metadata after payload write");
    return false;
}

b8 VolumeFileSystem::scanSegmentsLocked(){
    m_segmentPaths.clear();

    for(usize segmentIndex = 0u;; ++segmentIndex){
        const Path hashedSegmentPath = MakeVolumeSegmentPath(m_mountDirectory, m_volumeName.view(), segmentIndex);

        const auto exists = FileExists(hashedSegmentPath);
        if(!exists){
            FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpScanSegmentsExists, hashedSegmentPath, exists.error());
            return false;
        }
        if(!*exists)
            break;

        if(const auto regularFile = IsRegularFile(hashedSegmentPath); !regularFile || !*regularFile){
            if(!regularFile)
                FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpScanSegmentsIsRegularFile, hashedSegmentPath, regularFile.error());
            else
                FilesystemVolumeDetail::LogFailureWithPath(m_volumeName, FilesystemVolumeDetail::s_VolumeOpScanSegmentsIsRegularFile, hashedSegmentPath, "segment path is not a regular file");
            return false;
        }

        m_segmentPaths.push_back(hashedSegmentPath);

        if(segmentIndex == Limit<usize>::s_Max){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpScanSegments, "segment index overflow");
            return false;
        }
    }

    return true;
}

Expected<VolumeFileSystem::FileRecord> VolumeFileSystem::readFileRecordLocked(const Name& virtualPath)const noexcept(!FOUNDATION_OCCUR_WARNING){
    if(!m_mounted){
        FilesystemVolumeDetail::LogFailure(m_volumeName.view(), FilesystemVolumeDetail::s_VolumeOpReadFile, FilesystemVolumeDetail::s_VolumeDetailNotMounted);
        return MakeUnexpected(Failure{});
    }
    if(!virtualPath){
        FilesystemVolumeDetail::LogFailure(m_volumeName.view(), FilesystemVolumeDetail::s_VolumeOpReadFile, FilesystemVolumeDetail::s_VolumeDetailInvalidVirtualPath);
        return MakeUnexpected(Failure{});
    }
    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        FilesystemVolumeDetail::LogFailure(m_volumeName.view(), FilesystemVolumeDetail::s_VolumeOpReadFile, FilesystemVolumeDetail::s_VolumeDetailFileNotFound);
        return MakeUnexpected(Failure{});
    }

    return itr.value();
}

void VolumeFileSystem::unmountLocked()noexcept{
    closeReadMappingsLocked();

    m_mounted = false;
    m_writable = false;

    m_volumeName.clear();
    m_mountDirectory.clear();

    m_segmentSize = 0;
    m_metadataBytes = 0;
    m_nextFreeOffset = 0;
    m_maxSegments = 0;

    m_segmentPaths.clear();
    m_files.clear();
}

void VolumeFileSystem::openReadMappingsLocked()noexcept{
    closeReadMappingsLocked();
    if(m_segmentPaths.empty())
        return;

    m_readMappings.resize(m_segmentPaths.size());
    for(usize index = 0u; index < m_segmentPaths.size(); ++index){
        ReadOnlySegmentMapping& entry = m_readMappings[index];
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        const HANDLE fileHandle = CreateFileW(
            m_segmentPaths[index].c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
            nullptr
        );
        if(fileHandle == INVALID_HANDLE_VALUE)
            continue;
        LARGE_INTEGER fileSize{};
        if(!GetFileSizeEx(fileHandle, &fileSize) || fileSize.QuadPart <= 0 || !CanRepresentU64<usize>(static_cast<u64>(fileSize.QuadPart))){
            CloseHandle(fileHandle);
            continue;
        }
        const HANDLE mappingHandle = CreateFileMappingW(fileHandle, nullptr, PAGE_READONLY, 0u, 0u, nullptr);
        if(!mappingHandle){
            CloseHandle(fileHandle);
            continue;
        }
        const void* const mappedView = MapViewOfFile(mappingHandle, FILE_MAP_READ, 0u, 0u, 0u);
        if(!mappedView){
            CloseHandle(mappingHandle);
            CloseHandle(fileHandle);
            continue;
        }
        entry.fileHandle = fileHandle;
        entry.mappingHandle = mappingHandle;
        entry.mappedView = static_cast<const u8*>(mappedView);
        entry.mappedBytes = static_cast<u64>(fileSize.QuadPart);
#else
        const int fd = ::open(m_segmentPaths[index].c_str(), O_RDONLY | O_CLOEXEC);
        if(fd < 0)
            continue;
        struct stat status{};
        if(::fstat(fd, &status) != 0 || status.st_size <= 0 || !CanRepresentU64<usize>(static_cast<u64>(status.st_size))){
            ::close(fd);
            continue;
        }
        const usize mapBytes = static_cast<usize>(status.st_size);
        void* const mappedView = ::mmap(nullptr, mapBytes, PROT_READ, MAP_PRIVATE, fd, 0);
        if(mappedView == MAP_FAILED){
            ::close(fd);
            continue;
        }
        entry.fileDescriptor = fd;
        entry.mappedView = static_cast<const u8*>(mappedView);
        entry.mappedBytes = static_cast<u64>(status.st_size);
#endif
    }
}

void VolumeFileSystem::closeReadMappingsLocked()noexcept{
    for(ReadOnlySegmentMapping& entry : m_readMappings){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        if(entry.mappedView){
            UnmapViewOfFile(entry.mappedView);
            entry.mappedView = nullptr;
        }
        if(entry.mappingHandle){
            CloseHandle(static_cast<HANDLE>(entry.mappingHandle));
            entry.mappingHandle = nullptr;
        }
        if(entry.fileHandle){
            CloseHandle(static_cast<HANDLE>(entry.fileHandle));
            entry.fileHandle = nullptr;
        }
#else
        if(entry.mappedView){
            ::munmap(const_cast<u8*>(entry.mappedView), static_cast<usize>(entry.mappedBytes));
            entry.mappedView = nullptr;
        }
        if(entry.fileDescriptor >= 0){
            ::close(entry.fileDescriptor);
            entry.fileDescriptor = -1;
        }
#endif
        entry.mappedBytes = 0u;
    }
    m_readMappings.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

