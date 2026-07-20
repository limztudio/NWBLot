// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_file_system.h"
#include "volume_storage_detail.h"
#include "arena_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <global/filesystem/volume_naming.h>
#include <global/limit.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VolumeFileSystem::VolumeFileSystem(Alloc::GlobalArena& arena)
    : m_mountDirectory(arena)
    , m_arena(arena)
    , m_segmentPaths(m_arena)
    , m_files(0, Hasher<Name>(), EqualTo<Name>(), m_arena)
{}

VolumeFileSystem::~VolumeFileSystem(){
    unmount();
}


bool VolumeFileSystem::mount(const VolumeMountDesc& desc){
    ErrorCode errorCode;

    ScopedLock lock(m_mutex);
    unmountLocked();

    if(!::ValidVolumeName(desc.volumeName.view())){
        __hidden_filesystem::LogFailure(desc.volumeName.view(), "mount", "invalid volume name");
        return false;
    }

    m_volumeName = desc.volumeName;
    m_mountDirectory = desc.mountDirectory.empty() ? Path(m_mountDirectory.arena(), ".") : desc.mountDirectory;
    m_usage = desc.usage;
    m_writable = (desc.usage != VolumeUsage::RuntimeReadOnly);
    m_maxSegments = desc.maxSegments;

    if(!FileExists(m_mountDirectory, errorCode)){
        if(errorCode){
            __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:exists", m_mountDirectory, errorCode);
            return false;
        }

        if(!desc.createIfMissing || !m_writable){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "mount directory does not exist and creation is disabled");
            return false;
        }

        if(!EnsureDirectories(m_mountDirectory, errorCode)){
            __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:create_directories", m_mountDirectory, errorCode);
            return false;
        }
    }
    else if(!IsDirectory(m_mountDirectory, errorCode)){
        __hidden_filesystem::LogFailureWithPath(m_volumeName, "mount:is_directory", m_mountDirectory, "path is not a directory");
        return false;
    }

    if(!scanSegmentsLocked()){
        __hidden_filesystem::LogFailure(m_volumeName, "mount", "segment scan failed");
        unmountLocked();
        return false;
    }

    if(m_segmentPaths.empty()){
        if(!desc.createIfMissing || !m_writable || desc.segmentSize == 0){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "volume does not exist and creation parameters are invalid");
            unmountLocked();
            return false;
        }

        m_segmentSize = desc.segmentSize;
        m_metadataBytes = desc.metadataSize == 0
            ? __hidden_filesystem::DefaultMetadataBytes(m_segmentSize)
            : desc.metadataSize
        ;

        if(m_metadataBytes <= sizeof(__hidden_filesystem::VolumeHeaderDisk) || m_metadataBytes >= m_segmentSize){
            __hidden_filesystem::LogFailure(
                m_volumeName,
                "mount",
                "metadata size is outside valid segment bounds"
            );
            unmountLocked();
            return false;
        }

        m_nextFreeOffset = m_metadataBytes;

        if(!createSegmentLocked(0)){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "failed to create first segment");
            unmountLocked();
            return false;
        }
        if(!flushMetadataLocked()){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "failed to write initial metadata");
            unmountLocked();
            return false;
        }
    }
    else{
        __hidden_filesystem::VolumeHeaderDisk discoveredHeader{};
        if(!__hidden_filesystem::ReadVolumeHeaderFromSegment(m_volumeName, m_segmentPaths[0], discoveredHeader)){
            unmountLocked();
            return false;
        }
        if(NWB_MEMCMP(discoveredHeader.magic, __hidden_filesystem::s_VolumeMagic, sizeof(discoveredHeader.magic)) != 0){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "magic mismatch");
            unmountLocked();
            return false;
        }
        if(discoveredHeader.segmentSize == 0){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "segment size is zero");
            unmountLocked();
            return false;
        }
        m_segmentSize = discoveredHeader.segmentSize;

        for(usize segmentIndex = 0; segmentIndex < m_segmentPaths.size(); ++segmentIndex){
            const Path& segmentPath = m_segmentPaths[segmentIndex];
            const u64 segmentFileSize = FileSize(segmentPath, errorCode);
            if(errorCode || segmentFileSize == 0){
                if(errorCode)
                    __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:file_size", segmentPath, errorCode);
                else
                    __hidden_filesystem::LogFailureWithPath(m_volumeName, "mount:file_size", segmentPath, "segment size is zero");
                unmountLocked();
                return false;
            }

            const bool isLastSegment = segmentIndex + 1u == m_segmentPaths.size();
            if(!isLastSegment && segmentFileSize != m_segmentSize){
                NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: segment '{}' has size {}, expected {}")
                    , StringConvert(m_volumeName)
                    , StringConvert(segmentPath.string())
                    , segmentFileSize
                    , m_segmentSize
                );
                unmountLocked();
                return false;
            }
            if(isLastSegment && segmentFileSize > m_segmentSize){
                NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: final segment '{}' has size {}, exceeding logical segment size {}")
                    , StringConvert(m_volumeName)
                    , StringConvert(segmentPath.string())
                    , segmentFileSize
                    , m_segmentSize
                );
                unmountLocked();
                return false;
            }
        }

        const u64 firstSegmentFileSize = FileSize(m_segmentPaths[0], errorCode);
        if(errorCode || firstSegmentFileSize < sizeof(__hidden_filesystem::VolumeHeaderDisk)){
            if(errorCode)
                __hidden_filesystem::LogFailureWithFsError(m_volumeName, "mount:file_size", m_segmentPaths[0], errorCode);
            else
                __hidden_filesystem::LogFailureWithPath(m_volumeName, "mount:file_size", m_segmentPaths[0], "segment is smaller than the volume header");
            unmountLocked();
            return false;
        }

        if(desc.segmentSize != 0 && desc.segmentSize != m_segmentSize){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: requested segment size {} does not match volume size {}")
                , StringConvert(m_volumeName)
                , desc.segmentSize
                , m_segmentSize
            );
            unmountLocked();
            return false;
        }

        if(!loadMetadataLocked()){
            __hidden_filesystem::LogFailure(m_volumeName, "mount", "metadata load failed");
            unmountLocked();
            return false;
        }

        if(desc.metadataSize != 0 && desc.metadataSize != m_metadataBytes){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: requested metadata size {} does not match volume metadata size {}")
                , StringConvert(m_volumeName)
                , desc.metadataSize
                , m_metadataBytes
            );
            unmountLocked();
            return false;
        }
    }

    if(m_maxSegments != 0 && m_segmentPaths.size() > m_maxSegments){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: discovered {} segments, maxSegments is {}")
            , StringConvert(m_volumeName)
            , m_segmentPaths.size()
            , m_maxSegments
        );
        unmountLocked();
        return false;
    }

    m_mounted = true;
    return true;
}

void VolumeFileSystem::unmount(){
    ScopedLock lock(m_mutex);
    unmountLocked();
}

bool VolumeFileSystem::mounted()const{
    ScopedLock lock(m_mutex);
    return m_mounted;
}

bool VolumeFileSystem::writable()const{
    ScopedLock lock(m_mutex);
    return m_mounted && m_writable;
}

AStringView VolumeFileSystem::volumeName()const{
    ScopedLock lock(m_mutex);
    return m_volumeName.view();
}

Path VolumeFileSystem::mountDirectory()const{
    ScopedLock lock(m_mutex);
    return m_mountDirectory;
}

u64 VolumeFileSystem::segmentSize()const{
    ScopedLock lock(m_mutex);
    return m_segmentSize;
}

u64 VolumeFileSystem::metadataSize()const{
    ScopedLock lock(m_mutex);
    return m_metadataBytes;
}

usize VolumeFileSystem::segmentCount()const{
    ScopedLock lock(m_mutex);
    return m_segmentPaths.size();
}


u64 VolumeFileSystem::fileCount()const{
    ScopedLock lock(m_mutex);
    return static_cast<u64>(m_files.size());
}

u64 VolumeFileSystem::usedBytes()const{
    ScopedLock lock(m_mutex);

    u64 totalUsedBytes = 0;
    for(const auto& [_, record] : m_files){
        if(!__hidden_filesystem::AddNoOverflow(totalUsedBytes, record.size, totalUsedBytes))
            return Limit<u64>::s_Max;
    }

    return totalUsedBytes;
}

u64 VolumeFileSystem::wastedBytes()const{
    ScopedLock lock(m_mutex);
    if(m_nextFreeOffset <= m_metadataBytes)
        return 0;

    u64 totalUsedBytes = 0;
    for(const auto& [_, record] : m_files){
        if(!__hidden_filesystem::AddNoOverflow(totalUsedBytes, record.size, totalUsedBytes))
            return 0;
    }

    const u64 payloadSpan = m_nextFreeOffset - m_metadataBytes;
    if(totalUsedBytes >= payloadSpan)
        return 0;

    return payloadSpan - totalUsedBytes;
}


bool VolumeFileSystem::writeFileLocked(
    const Name& virtualPath,
    const void* data,
    const usize bytes,
    const MetadataFlushMode::Enum flushMode
){
    if(!m_mounted || !m_writable){
        __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "filesystem is not mounted in writable mode");
        return false;
    }
    if(!virtualPath){
        __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "virtual path is invalid");
        return false;
    }
    if(bytes != 0 && data == nullptr){
        __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "data pointer is null while byte count is non-zero");
        return false;
    }

    const auto itrFind = m_files.find(virtualPath);
    const bool existed = itrFind != m_files.end();

    u64 fileCountAfterWrite = static_cast<u64>(m_files.size());
    if(!existed){
        if(fileCountAfterWrite == Limit<u64>::s_Max){
            __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "file count overflow");
            return false;
        }
        ++fileCountAfterWrite;
    }
    if(!canFitMetadataForFileCountLocked(fileCountAfterWrite)){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): writeFile failed: metadata area is full for file count {}")
            , StringConvert(m_volumeName)
            , fileCountAfterWrite
        );
        return false;
    }

    const u64 byteCount = static_cast<u64>(bytes);
    u64 newFileEnd = 0;
    if(!__hidden_filesystem::AddNoOverflow(m_nextFreeOffset, byteCount, newFileEnd)){
        __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "offset overflow while reserving payload bytes");
        return false;
    }
    if(!ensureCapacityLocked(newFileEnd))
        return false;

    const u64 writeOffset = m_nextFreeOffset;
    if(byteCount > 0 && !writeBytesLocked(writeOffset, data, byteCount))
        return false;

    FileRecord previousRecord;
    if(existed)
        previousRecord = itrFind.value();
    const u64 previousNextFreeOffset = m_nextFreeOffset;

    m_files.insert_or_assign(virtualPath, FileRecord{ writeOffset, byteCount });
    m_nextFreeOffset = newFileEnd;

    if(flushMode == MetadataFlushMode::Deferred)
        return true;

    if(flushMetadataLocked())
        return true;

    if(existed)
        m_files.insert_or_assign(virtualPath, previousRecord);
    else
        m_files.erase(virtualPath);
    m_nextFreeOffset = previousNextFreeOffset;
    __hidden_filesystem::LogFailure(m_volumeName, "writeFile", "failed to flush metadata after payload write");
    return false;
}

bool VolumeFileSystem::writeFile(const Name& virtualPath, const void* data, const usize bytes){
    ScopedLock lock(m_mutex);
    return writeFileLocked(virtualPath, data, bytes, MetadataFlushMode::Immediate);
}

bool VolumeFileSystem::writeFileDeferred(const Name& virtualPath, const void* data, const usize bytes){
    ScopedLock lock(m_mutex);
    return writeFileLocked(virtualPath, data, bytes, MetadataFlushMode::Deferred);
}

bool VolumeFileSystem::flushMetadata(){
    ScopedLock lock(m_mutex);
    if(!m_mounted || !m_writable){
        __hidden_filesystem::LogFailure(m_volumeName, "flushMetadata", "filesystem is not mounted in writable mode");
        return false;
    }

    return flushMetadataLocked();
}

void VolumeFileSystem::reserveFileCapacity(const usize fileCount){
    ScopedLock lock(m_mutex);
    if(fileCount > m_files.size())
        m_files.reserve(fileCount);
}

bool VolumeFileSystem::readFileRecordLocked(const Name& virtualPath, FileRecord& outRecord)const{
    outRecord = {};
    if(!m_mounted){
        __hidden_filesystem::LogFailure(m_volumeName.view(), "readFile", "filesystem is not mounted");
        return false;
    }
    if(!virtualPath){
        __hidden_filesystem::LogFailure(m_volumeName.view(), "readFile", "virtual path is invalid");
        return false;
    }
    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        __hidden_filesystem::LogFailure(m_volumeName.view(), "readFile", "file was not found");
        return false;
    }

    outRecord = itr.value();
    return true;
}

bool VolumeFileSystem::removeFile(const Name& virtualPath){
    ScopedLock lock(m_mutex);
    if(!m_mounted || !m_writable){
        __hidden_filesystem::LogFailure(m_volumeName, "removeFile", "filesystem is not mounted in writable mode");
        return false;
    }
    if(!virtualPath){
        __hidden_filesystem::LogFailure(m_volumeName, "removeFile", "virtual path is invalid");
        return false;
    }
    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        __hidden_filesystem::LogFailure(m_volumeName, "removeFile", "file was not found");
        return false;
    }

    const FileRecord removedRecord = itr.value();
    m_files.erase(itr);
    if(flushMetadataLocked())
        return true;

    m_files.insert_or_assign(virtualPath, removedRecord);
    __hidden_filesystem::LogFailure(m_volumeName, "removeFile", "failed to flush metadata after erase");
    return false;
}

bool VolumeFileSystem::fileExists(const Name& virtualPath)const{
    ScopedLock lock(m_mutex);
    if(!m_mounted)
        return false;
    if(!virtualPath)
        return false;
    return m_files.find(virtualPath) != m_files.end();
}

bool VolumeFileSystem::fileSize(const Name& virtualPath, u64& outSize)const{
    ScopedLock lock(m_mutex);
    if(!m_mounted){
        __hidden_filesystem::LogFailure(m_volumeName, "fileSize", "filesystem is not mounted");
        return false;
    }
    if(!virtualPath){
        __hidden_filesystem::LogFailure(m_volumeName, "fileSize", "virtual path is invalid");
        return false;
    }

    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        __hidden_filesystem::LogFailure(m_volumeName, "fileSize", "file was not found");
        return false;
    }

    outSize = itr.value().size;
    return true;
}

Vector<Name, VolumeArena> VolumeFileSystem::listFiles()const{
    ScopedLock lock(m_mutex);

    Vector<Name, VolumeArena> output{m_arena};
    output.reserve(m_files.size());
    for(const auto& [path, _] : m_files)
        output.push_back(path);

    Sort(output.begin(), output.end());
    return output;
}


bool VolumeFileSystem::compact(const bool shrinkSegments){
    ScopedLock lock(m_mutex);
    if(!m_mounted || !m_writable){
        __hidden_filesystem::LogFailure(m_volumeName, "compact", "filesystem is not mounted in writable mode");
        return false;
    }

    struct FileLayout{
        Name path;
        u64 sourceOffset = 0;
        u64 destinationOffset = 0;
        u64 size = 0;
    };

    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_CompactScratch);
    Vector<FileLayout, Core::Alloc::ScratchArena> layouts{ scratchArena };
    layouts.reserve(m_files.size());

    for(const auto& [path, record] : m_files){
        u64 endOffset = 0;
        if(!__hidden_filesystem::AddNoOverflow(record.offset, record.size, endOffset)){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "offset overflow detected in file layout");
            return false;
        }
        if(record.offset < m_metadataBytes){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "file layout overlaps metadata region");
            return false;
        }
        if(endOffset > m_nextFreeOffset){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "file layout exceeds next-free boundary");
            return false;
        }

        layouts.push_back(FileLayout{ path, record.offset, 0, record.size });
    }

    Sort(
        layouts.begin(),
        layouts.end(),
        [](const FileLayout& lhs, const FileLayout& rhs){
            return lhs.sourceOffset < rhs.sourceOffset;
        }
    );

    u64 compactedWriteOffset = m_metadataBytes;
    u64 previousSourceEnd = m_metadataBytes;
    for(auto& layout : layouts){
        if(layout.sourceOffset < previousSourceEnd){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "file layout overlap detected");
            return false;
        }

        layout.destinationOffset = compactedWriteOffset;

        if(!__hidden_filesystem::AddNoOverflow(layout.sourceOffset, layout.size, previousSourceEnd)){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "source offset overflow while building compaction plan");
            return false;
        }
        if(!__hidden_filesystem::AddNoOverflow(compactedWriteOffset, layout.size, compactedWriteOffset)){
            __hidden_filesystem::LogFailure(m_volumeName, "compact", "destination offset overflow while building compaction plan");
            return false;
        }
    }

    for(const auto& layout : layouts){
        if(layout.size == 0 || layout.destinationOffset == layout.sourceOffset)
            continue;

        if(!moveBytesLocked(layout.destinationOffset, layout.sourceOffset, layout.size))
            return false;
    }

    const FileMap previousFiles = m_files;
    const u64 previousNextFreeOffset = m_nextFreeOffset;

    for(const auto& layout : layouts)
        m_files.insert_or_assign(layout.path, FileRecord{ layout.destinationOffset, layout.size });
    m_nextFreeOffset = compactedWriteOffset;

    if(!flushMetadataLocked()){
        m_files = previousFiles;
        m_nextFreeOffset = previousNextFreeOffset;
        __hidden_filesystem::LogFailure(m_volumeName, "compact", "failed to flush metadata after move");
        return false;
    }

    if(!shrinkSegments)
        return true;

    if(trimSegmentsForNextFreeOffsetLocked())
        return true;

    __hidden_filesystem::LogFailure(m_volumeName, "compact", "failed to trim trailing segments");
    return false;
}


bool VolumeFileSystem::scanSegmentsLocked(){
    ErrorCode errorCode;

    m_segmentPaths.clear();

    for(usize segmentIndex = 0;; ++segmentIndex){
        const Path hashedSegmentPath = m_mountDirectory / ::MakeVolumeSegmentFileName(m_volumeName.view(), segmentIndex).c_str();

        const bool exists = FileExists(hashedSegmentPath, errorCode);
        if(errorCode){
            __hidden_filesystem::LogFailureWithFsError(m_volumeName, "scanSegments:exists", hashedSegmentPath, errorCode);
            return false;
        }
        if(!exists)
            break;

        if(!IsRegularFile(hashedSegmentPath, errorCode)){
            __hidden_filesystem::LogFailureWithPath(m_volumeName, "scanSegments:is_regular_file", hashedSegmentPath, "segment path is not a regular file");
            return false;
        }

        m_segmentPaths.push_back(hashedSegmentPath);

        if(segmentIndex == Limit<usize>::s_Max){
            __hidden_filesystem::LogFailure(m_volumeName, "scanSegments", "segment index overflow");
            return false;
        }
    }

    return true;
}

void VolumeFileSystem::unmountLocked(){
    m_mounted = false;
    m_writable = false;
    m_usage = VolumeUsage::RuntimeReadOnly;

    m_volumeName.clear();
    m_mountDirectory.clear();

    m_segmentSize = 0;
    m_metadataBytes = 0;
    m_nextFreeOffset = 0;
    m_maxSegments = 0;

    m_segmentPaths.clear();
    m_files.clear();
}

Path VolumeFileSystem::segmentPath(const usize segmentIndex)const{
    return m_mountDirectory / ::MakeVolumeSegmentFileName(m_volumeName.view(), segmentIndex).c_str();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
