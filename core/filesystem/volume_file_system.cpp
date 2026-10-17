// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_file_system.h"
#include "volume_storage_detail.h"
#include "volume_staging_detail.h"
#include "arena_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/filesystem/volume_naming.h>

#include <global/limit.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VolumeFileSystem::VolumeFileSystem(Alloc::GlobalArena& arena)
    : m_mountDirectory(arena)
    , m_arena(arena)
    , m_segmentPaths(m_arena)
    , m_files(0, Hasher<Name>(), EqualTo<Name>(), m_arena)
{}

bool VolumeFileSystem::mount(const VolumeMountDesc& desc){
    Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_LoadMetadataScratch);

    ScopedLock lock(m_mutex);
    if(m_mounted && m_writable && !flushMetadataLocked(scratchArena))
        return false;
    unmountLocked();

    if(!ValidVolumeName(desc.volumeName.view())){
        FilesystemVolumeDetail::LogFailure(desc.volumeName.view(), FilesystemVolumeDetail::s_VolumeOpMount, "invalid volume name");
        return false;
    }

    m_volumeName = desc.volumeName;
    m_mountDirectory = desc.mountDirectory.empty() ? Path(m_mountDirectory.arena(), ".") : desc.mountDirectory;
    m_usage = desc.usage;
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
        if(NWB_MEMCMP(discoveredHeader.magic, FilesystemVolumeDetail::s_VolumeMagic, sizeof(discoveredHeader.magic)) != 0){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMount, "magic mismatch");
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
            const u64 segmentFileSize = fileSizeResult.value_or(0u);
            if(!fileSizeResult || segmentFileSize == 0){
                if(!fileSizeResult)
                    FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMountFileSize, segmentPath, fileSizeResult.error());
                else
                    FilesystemVolumeDetail::LogFailureWithPath(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMountFileSize, segmentPath, FilesystemVolumeDetail::s_VolumeDetailSegmentSizeZero);
                unmountLocked();
                return false;
            }

            const bool isLastSegment = segmentIndex + 1u == segmentPathCount;
            if(!isLastSegment && segmentFileSize != m_segmentSize){
                NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: segment '{}' has size {}, expected {}"), StringConvert(m_volumeName), StringConvert(segmentPath.native()), segmentFileSize, m_segmentSize);
                unmountLocked();
                return false;
            }
            if(isLastSegment && segmentFileSize > m_segmentSize){
                NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: final segment '{}' has size {}, exceeding logical segment size {}"), StringConvert(m_volumeName), StringConvert(segmentPath.native()), segmentFileSize, m_segmentSize);
                unmountLocked();
                return false;
            }
        }

        const auto firstFileSizeResult = FileSize(m_segmentPaths[0]);
        const u64 firstSegmentFileSize = firstFileSizeResult.value_or(0u);
        if(!firstFileSizeResult || firstSegmentFileSize < sizeof(FilesystemVolumeDetail::VolumeHeaderDisk)){
            if(!firstFileSizeResult)
                FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMountFileSize, m_segmentPaths[0], firstFileSizeResult.error());
            else
                FilesystemVolumeDetail::LogFailureWithPath(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMountFileSize, m_segmentPaths[0], "segment is smaller than the volume header");
            unmountLocked();
            return false;
        }

        if(desc.segmentSize != 0 && desc.segmentSize != m_segmentSize){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: requested segment size {} does not match volume size {}"), StringConvert(m_volumeName), desc.segmentSize, m_segmentSize);
            unmountLocked();
            return false;
        }

        if(!loadMetadataLocked(scratchArena)){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMount, "metadata load failed");
            unmountLocked();
            return false;
        }

        if(desc.metadataSize != 0 && desc.metadataSize != m_metadataBytes){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: requested metadata size {} does not match volume metadata size {}"), StringConvert(m_volumeName), desc.metadataSize, m_metadataBytes);
            unmountLocked();
            return false;
        }
    }

    if(m_maxSegments != 0 && m_segmentPaths.size() > m_maxSegments){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): mount failed: discovered {} segments, maxSegments is {}"), StringConvert(m_volumeName), m_segmentPaths.size(), m_maxSegments);
        unmountLocked();
        return false;
    }

    m_mounted = true;
    return true;
}

bool VolumeFileSystem::unmount(){
    ScopedLock lock(m_mutex);
    if(m_mounted && m_writable){
        Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_SaveMetadataScratch);
        if(!flushMetadataLocked(scratchArena))
            return false;
    }
    unmountLocked();
    return true;
}

bool VolumeFileSystem::mounted()const noexcept{
    NothrowScopedLock lock(m_mutex);
    return m_mounted;
}

bool VolumeFileSystem::writable()const noexcept{
    NothrowScopedLock lock(m_mutex);
    return m_mounted && m_writable;
}

AStringView VolumeFileSystem::volumeName()const noexcept{
    NothrowScopedLock lock(m_mutex);
    return m_volumeName.view();
}

Path VolumeFileSystem::mountDirectory()const{
    ScopedLock lock(m_mutex);
    return m_mountDirectory;
}

u64 VolumeFileSystem::segmentSize()const noexcept{
    NothrowScopedLock lock(m_mutex);
    return m_segmentSize;
}

u64 VolumeFileSystem::metadataSize()const noexcept{
    NothrowScopedLock lock(m_mutex);
    return m_metadataBytes;
}

usize VolumeFileSystem::segmentCount()const noexcept{
    NothrowScopedLock lock(m_mutex);
    return m_segmentPaths.size();
}


u64 VolumeFileSystem::fileCount()const noexcept{
    NothrowScopedLock lock(m_mutex);
    return static_cast<u64>(m_files.size());
}

Expected<u64> VolumeFileSystem::usedBytes()const noexcept{
    NothrowScopedLock lock(m_mutex);

    u64 totalUsedBytes = 0;
    for(const auto& [_, record] : m_files){
        const auto totalUsedBytesResult = ::AddNoOverflow(totalUsedBytes, record.size);
        if(!totalUsedBytesResult)
            return MakeUnexpected(totalUsedBytesResult.error());
        totalUsedBytes = *totalUsedBytesResult;
    }

    return totalUsedBytes;
}

Expected<u64> VolumeFileSystem::wastedBytes()const noexcept{
    NothrowScopedLock lock(m_mutex);
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


bool VolumeFileSystem::writeFile(const Name& virtualPath, const void* data, const usize bytes){
    ScopedLock lock(m_mutex);
    return writeFileLocked(virtualPath, data, bytes, MetadataFlushMode::Immediate);
}

bool VolumeFileSystem::writeFileDeferred(const Name& virtualPath, const void* data, const usize bytes){
    ScopedLock lock(m_mutex);
    return writeFileLocked(virtualPath, data, bytes, MetadataFlushMode::Deferred);
}

bool VolumeFileSystem::flush(){
    return compact(true);
}


void VolumeFileSystem::reserveFileCapacity(const usize fileCount){
    ScopedLock lock(m_mutex);
    if(fileCount > m_files.size())
        m_files.reserve(fileCount);
}

Expected<usize> VolumeFileSystem::readFile(const Name& virtualPath, const u64 offset, void* data, const usize bytes)const{
    ScopedLock lock(m_mutex);
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

bool VolumeFileSystem::seekFile(FileCursor& cursor, const i64 offset, const FileSeekOrigin::Enum origin)const noexcept(!NWB_OCCUR_WARNING){
    NothrowScopedLock lock(m_mutex);
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

    u64 destination = 0;
    if(offset < 0){
        const u64 distance = static_cast<u64>(-(offset + 1)) + 1u;
        if(distance > base)
            return false;
        destination = base - distance;
    }
    else{
        const u64 distance = static_cast<u64>(offset);
        if(distance > Limit<u64>::s_Max - base)
            return false;
        destination = base + distance;
    }
    if(destination > record->size)
        return false;
    cursor.offset = destination;
    return true;
}

bool VolumeFileSystem::removeFile(const Name& virtualPath){
    ScopedLock lock(m_mutex);
    if(!m_mounted || !m_writable){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpRemoveFile, "filesystem is not mounted in writable mode");
        return false;
    }
    if(!virtualPath){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpRemoveFile, "virtual path is invalid");
        return false;
    }
    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpRemoveFile, "file was not found");
        return false;
    }

    Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_SaveMetadataScratch);
    if(flushMetadataLocked(scratchArena, &virtualPath)){
        m_files.erase(itr);
        return true;
    }
    FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpRemoveFile, "failed to flush metadata before erase");
    return false;
}

bool VolumeFileSystem::fileExists(const Name& virtualPath)const noexcept{
    NothrowScopedLock lock(m_mutex);
    if(!m_mounted)
        return false;
    if(!virtualPath)
        return false;
    return m_files.find(virtualPath) != m_files.end();
}

Expected<u64> VolumeFileSystem::fileSize(const Name& virtualPath)const noexcept(!NWB_OCCUR_WARNING){
    NothrowScopedLock lock(m_mutex);
    if(!m_mounted){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpFileSize, "filesystem is not mounted");
        return MakeUnexpected(Failure{});
    }
    if(!virtualPath){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpFileSize, "virtual path is invalid");
        return MakeUnexpected(Failure{});
    }

    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpFileSize, "file was not found");
        return MakeUnexpected(Failure{});
    }

    return itr.value().size;
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
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpCompact, "filesystem is not mounted in writable mode");
        return false;
    }

    struct FileLayout{
        Name path;
        NotNull<FileRecord*> record;
        u64 sourceOffset = 0u;
        u64 destinationOffset = 0u;
        u64 size = 0u;
    };

    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_CompactScratch);
    Vector<FileLayout, Core::Alloc::ScratchArena> layouts(scratchArena);
    layouts.reserve(m_files.size());
    for(auto entry = m_files.begin(); entry != m_files.end(); ++entry){
        FileRecord& record = entry.value();
        const auto endOffset = AddNoOverflow(record.offset, record.size);
        if(!endOffset || record.offset < m_metadataBytes || *endOffset > m_nextFreeOffset){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpCompact, "invalid source file layout");
            return false;
        }
        layouts.push_back(FileLayout{ entry.key(), NotNull<FileRecord*>(&record), record.offset, 0u, record.size });
    }
    Sort(layouts.begin(), layouts.end(), [](const FileLayout& lhs, const FileLayout& rhs)noexcept{ return lhs.sourceOffset < rhs.sourceOffset; });

    u64 compactedWriteOffset = m_metadataBytes;
    u64 previousSourceEnd = m_metadataBytes;
    bool movesPayload = false;
    for(auto& layout : layouts){
        if(layout.sourceOffset < previousSourceEnd){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpCompact, "file layout overlap detected");
            return false;
        }
        layout.destinationOffset = compactedWriteOffset;
        movesPayload |= layout.size != 0u && layout.destinationOffset != layout.sourceOffset;
        const auto sourceEnd = AddNoOverflow(layout.sourceOffset, layout.size);
        const auto destinationEnd = AddNoOverflow(compactedWriteOffset, layout.size);
        if(!sourceEnd || !destinationEnd){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpCompact, "compaction offset overflow");
            return false;
        }
        previousSourceEnd = *sourceEnd;
        compactedWriteOffset = *destinationEnd;
    }
    if(!movesPayload){
        const u64 previousNextFreeOffset = m_nextFreeOffset;
        ScopeExit restoreRecords([this, &layouts, previousNextFreeOffset]()noexcept{
            for(const auto& layout : layouts)
                *layout.record = FileRecord{ layout.sourceOffset, layout.size };
            m_nextFreeOffset = previousNextFreeOffset;
        });
        for(const auto& layout : layouts)
            *layout.record = FileRecord{ layout.destinationOffset, layout.size };
        m_nextFreeOffset = compactedWriteOffset;
        if(!flushMetadataLocked(scratchArena))
            return false;
        restoreRecords.release();
        return !shrinkSegments || trimSegmentsForNextFreeOffsetLocked();
    }

    const auto originalPhysicalCapacity = computePhysicalCapacityLocked();
    if(!originalPhysicalCapacity)
        return false;
    const u64 copyChunkBytes = Min(FilesystemVolumeDetail::s_VolumeMoveChunkBytes, m_segmentSize);
    if(copyChunkBytes == 0u || !CanRepresentU64<usize>(copyChunkBytes)){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpCompact, "invalid compaction chunk size");
        return false;
    }

    const StagedDirectoryPaths stagedPaths = FilesystemVolumeStagingDetail::BuildStagedVolumePaths(m_mountDirectory, m_volumeName.view(), scratchArena);
    if(!EnsureEmptyStagedDirectory(stagedPaths.stageDirectory, FilesystemVolumeStagingDetail::s_VolumePublishLogPrefix, "stage directory"))
        return false;
    StagedDirectoryCleanupGuard stageCleanup(stagedPaths.stageDirectory, FilesystemVolumeStagingDetail::s_VolumePublishLogPrefix);
    if(!RemoveStagedDirectoryIfPresent(stagedPaths.backupDirectory, FilesystemVolumeStagingDetail::s_VolumePublishLogPrefix, "backup directory"))
        return false;
    Vector<u8, Core::Alloc::ScratchArena> copyBuffer(static_cast<usize>(copyChunkBytes), 0u, scratchArena);
    usize stagedSegmentCount = 0u;
    {
        VolumeFileSystem stagedVolume(m_arena);
        VolumeMountDesc stagedDesc(m_arena);
        stagedDesc.volumeName = m_volumeName;
        stagedDesc.mountDirectory = stagedPaths.stageDirectory;
        stagedDesc.segmentSize = m_segmentSize;
        stagedDesc.metadataSize = m_metadataBytes;
        stagedDesc.maxSegments = m_maxSegments;
        stagedDesc.createIfMissing = true;
        stagedDesc.usage = VolumeUsage::CookWrite;
        if(!stagedVolume.mount(stagedDesc))
            return false;
        const u64 requiredBytes = shrinkSegments ? compactedWriteOffset : *originalPhysicalCapacity;
        if(!stagedVolume.ensureCapacityLocked(requiredBytes))
            return false;
        stagedVolume.m_files.reserve(layouts.size());
        for(const auto& layout : layouts){
            if(!stagedVolume.m_files.emplace(layout.path, FileRecord{ layout.destinationOffset, layout.size }).second){
                FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpCompact, "duplicate compacted file identity");
                return false;
            }
            u64 copiedBytes = 0u;
            while(copiedBytes < layout.size){
                const u64 bytes = Min(copyChunkBytes, layout.size - copiedBytes);
                if(!readBytesLocked(layout.sourceOffset + copiedBytes, copyBuffer.data(), bytes))
                    return false;
                if(!stagedVolume.writeBytesLocked(layout.destinationOffset + copiedBytes, copyBuffer.data(), bytes))
                    return false;
                copiedBytes += bytes;
            }
        }
        stagedVolume.m_nextFreeOffset = requiredBytes;
        if(!stagedVolume.trimSegmentsForNextFreeOffsetLocked())
            return false;
        stagedVolume.m_nextFreeOffset = compactedWriteOffset;
        stagedSegmentCount = stagedVolume.m_segmentPaths.size();
        if(!stagedVolume.unmount())
            return false;
    }

    struct CompactPublication{
        Vector<FileLayout, Core::Alloc::ScratchArena>& layouts;
        SegmentPathVector& segmentPaths;
        u64& nextFreeOffset;
        u64 compactedWriteOffset;
        usize segmentCount;
    };
    CompactPublication publication{ layouts, m_segmentPaths, m_nextFreeOffset, compactedWriteOffset, stagedSegmentCount };
    const FilesystemVolumeStagingDetail::VolumePublicationObserver observer{
        .context = &publication,
        .commit = [](void* context)noexcept{
            auto& state = *static_cast<CompactPublication*>(context);
            for(const auto& layout : state.layouts)
                *layout.record = FileRecord{ layout.destinationOffset, layout.size };
            state.nextFreeOffset = state.compactedWriteOffset;
            while(state.segmentPaths.size() > state.segmentCount)
                state.segmentPaths.pop_back();
        },
    };
    if(!FilesystemVolumeStagingDetail::PromoteStagedVolume(stagedPaths, m_mountDirectory, m_volumeName.view(), stagedSegmentCount, scratchArena, observer))
        return false;
    stageCleanup.dismiss();
    return true;
}


bool VolumeFileSystem::writeFileLocked(const Name& virtualPath, const void* data, const usize bytes, const MetadataFlushMode::Enum flushMode){
    if(!m_mounted || !m_writable){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, "filesystem is not mounted in writable mode");
        return false;
    }
    if(!virtualPath){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, "virtual path is invalid");
        return false;
    }
    if(bytes != 0 && data == nullptr){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, "data pointer is null while byte count is non-zero");
        return false;
    }

    const auto itrFind = m_files.find(virtualPath);
    const bool existed = itrFind != m_files.end();

    u64 fileCountAfterWrite = static_cast<u64>(m_files.size());
    if(!existed){
        if(fileCountAfterWrite == Limit<u64>::s_Max){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, "file count overflow");
            return false;
        }
        ++fileCountAfterWrite;
    }
    if(!canFitMetadataForFileCountLocked(fileCountAfterWrite)){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): writeFile failed: metadata area is full for file count {}"), StringConvert(m_volumeName), fileCountAfterWrite);
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

    Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_SaveMetadataScratch);
    if(flushMetadataLocked(scratchArena)){
        rollback.release();
        return true;
    }
    FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteFile, "failed to flush metadata after payload write");
    return false;
}

bool VolumeFileSystem::scanSegmentsLocked(){

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

Expected<VolumeFileSystem::FileRecord> VolumeFileSystem::readFileRecordLocked(const Name& virtualPath)const noexcept(!NWB_OCCUR_WARNING){
    if(!m_mounted){
        FilesystemVolumeDetail::LogFailure(m_volumeName.view(), FilesystemVolumeDetail::s_VolumeOpReadFile, "filesystem is not mounted");
        return MakeUnexpected(Failure{});
    }
    if(!virtualPath){
        FilesystemVolumeDetail::LogFailure(m_volumeName.view(), FilesystemVolumeDetail::s_VolumeOpReadFile, "virtual path is invalid");
        return MakeUnexpected(Failure{});
    }
    const auto itr = m_files.find(virtualPath);
    if(itr == m_files.end()){
        FilesystemVolumeDetail::LogFailure(m_volumeName.view(), FilesystemVolumeDetail::s_VolumeOpReadFile, "file was not found");
        return MakeUnexpected(Failure{});
    }

    return itr.value();
}

void VolumeFileSystem::unmountLocked()noexcept{
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
    return MakeVolumeSegmentPath(m_mountDirectory, m_volumeName.view(), segmentIndex);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

