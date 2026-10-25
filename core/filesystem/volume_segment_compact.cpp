// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_file_system.h"
#include "volume_storage_detail.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>

#include <global/error_code.h>
#include <global/limit.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

