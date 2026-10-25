// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_file_system.h"
#include "volume_storage_detail.h"
#include "volume_staging_detail.h"
#include "arena_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>

#include <global/error_code.h>
#include <global/limit.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool VolumeFileSystem::compact(const bool shrinkSegments){
    ScopedLock lock(m_mutex);
    if(!m_mounted || !m_writable){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpCompact, FilesystemVolumeDetail::s_VolumeDetailNotMountedWritable);
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
    if(!EnsureEmptyStagedDirectory(stagedPaths.stageDirectory, FilesystemVolumeStagingDetail::s_VolumePublishLogPrefix, s_StageDirectoryLabel))
        return false;
    StagedDirectoryCleanupGuard stageCleanup(stagedPaths.stageDirectory, FilesystemVolumeStagingDetail::s_VolumePublishLogPrefix);
    if(!RemoveStagedDirectoryIfPresent(stagedPaths.backupDirectory, FilesystemVolumeStagingDetail::s_VolumePublishLogPrefix, s_BackupDirectoryLabel))
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
            auto& state = *MakeNotNull(static_cast<CompactPublication*>(context));
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

bool VolumeFileSystem::trimSegmentsForNextFreeOffsetLocked(){

    if(!m_writable || m_segmentSize == 0){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpTrimSegments, FilesystemVolumeDetail::s_VolumeDetailNotWritableOrSizeZero);
        return false;
    }
    if(m_segmentPaths.empty()){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpTrimSegments, FilesystemVolumeDetail::s_VolumeDetailNoSegmentsMounted);
        return false;
    }

    const u64 requiredBytes = Max(m_nextFreeOffset, m_metadataBytes);
    u64 requiredSegments = DivideUp(requiredBytes, m_segmentSize);
    if(requiredSegments == 0)
        requiredSegments = 1;
    if(requiredSegments > static_cast<u64>(m_segmentPaths.size())){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpTrimSegments, "required segment count exceeds mounted segment count");
        return false;
    }
    if(requiredSegments > static_cast<u64>(Limit<usize>::s_Max)){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpTrimSegments, "required segment count exceeds usize range");
        return false;
    }

    while(m_segmentPaths.size() > static_cast<usize>(requiredSegments)){
        const Path& removePath = m_segmentPaths.back();
        const auto removed = RemoveFile(removePath);
        if(!removed || !*removed){
            if(!removed){
                FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpTrimSegmentsRemove, removePath, removed.error());
            }
            else{
                FilesystemVolumeDetail::LogFailureWithPath(m_volumeName, FilesystemVolumeDetail::s_VolumeOpTrimSegmentsRemove, removePath, "segment was not present");
            }
            return false;
        }
        m_segmentPaths.pop_back();
    }

    u64 requiredLastSegmentBytes = requiredBytes % m_segmentSize;
    if(requiredLastSegmentBytes == 0)
        requiredLastSegmentBytes = m_segmentSize;

    const Path& lastSegmentPath = m_segmentPaths.back();
    const auto currentLastSegmentBytes = FileSize(lastSegmentPath);
    if(!currentLastSegmentBytes){
        FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpTrimSegmentsFileSize, lastSegmentPath, currentLastSegmentBytes.error());
        return false;
    }
    if(*currentLastSegmentBytes == requiredLastSegmentBytes)
        return true;

    const auto resized = ResizeFile(lastSegmentPath, requiredLastSegmentBytes);
    if(!resized){
        FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpTrimSegmentsResize, lastSegmentPath, resized.error());
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

