// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_file_system.h"
#include "volume_storage_detail.h"
#include "arena_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <global/limit.h>
#include <global/simplemath.h>

#include <cerrno>
#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<void, ErrorCode> ResizeFile(const Path& path, const u64 byteCount){
#if defined(NWB_PLATFORM_WINDOWS)
    if(byteCount > static_cast<u64>(Limit<LONGLONG>::s_Max))
        return MakeUnexpected(std::make_error_code(std::errc::value_too_large));
    HANDLE file = CreateFile(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(file == INVALID_HANDLE_VALUE)
        return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());

    LARGE_INTEGER offset{};
    offset.QuadPart = static_cast<LONGLONG>(byteCount);
    const bool seekSucceeded = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != 0;
    const bool resizeSucceeded = seekSucceeded && SetEndOfFile(file) != 0;
    const ErrorCode errorCode = resizeSucceeded ? ErrorCode{} : GlobalFilesystemDetail::LastSystemError();
    CloseHandle(file);
    if(errorCode)
        return MakeUnexpected(errorCode);
    return {};
#else
    if(!CanRepresentU64<off_t>(byteCount))
        return MakeUnexpected(std::make_error_code(std::errc::value_too_large));
    if(::truncate(path.c_str(), static_cast<off_t>(byteCount)) != 0)
        return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
    return {};
#endif
}

bool VolumeFileSystem::moveBytesLocked(const u64 destinationOffset, const u64 sourceOffset, const u64 byteCount){
    if(byteCount == 0 || destinationOffset == sourceOffset)
        return true;
    if(m_segmentSize == 0){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMoveBytes, FilesystemVolumeDetail::s_VolumeDetailSegmentSizeZero);
        return false;
    }

    u64 sourceEndOffset = 0;
    const auto sourceEndOffsetResult = FilesystemVolumeDetail::AddNoOverflow(sourceOffset, byteCount);
    if(!sourceEndOffsetResult){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMoveBytes, "source range overflow");
        return false;
    }
    sourceEndOffset = *sourceEndOffsetResult;

    const auto capacity = computeLogicalCapacityLocked();
    if(!capacity){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMoveBytes, "capacity overflow");
        return false;
    }
    const u64 capacityBytes = *capacity;
    if(sourceEndOffset > capacityBytes){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): moveBytes failed: source range [{}..{}) exceeds capacity {}")
            , StringConvert(m_volumeName)
            , sourceOffset
            , sourceEndOffset
            , capacityBytes
        );
        return false;
    }

    u64 destinationEndOffset = 0;
    const auto destinationEndOffsetResult = FilesystemVolumeDetail::AddNoOverflow(destinationOffset, byteCount);
    if(!destinationEndOffsetResult){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMoveBytes, "destination range overflow");
        return false;
    }
    destinationEndOffset = *destinationEndOffsetResult;
    if(!ensureCapacityLocked(destinationEndOffset)){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMoveBytes, "failed to ensure destination capacity");
        return false;
    }

    const u64 moveChunkBytes = Min(FilesystemVolumeDetail::s_VolumeMoveChunkBytes, m_segmentSize);
    if(moveChunkBytes == 0 || moveChunkBytes > static_cast<u64>(Limit<usize>::s_Max)){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpMoveBytes, "invalid move chunk size");
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(FilesystemArenaScope::s_MoveBytesScratch);
    Vector<u8, Core::Alloc::ScratchArena> moveBuffer(
        static_cast<usize>(moveChunkBytes),
        0,
        scratchArena
    );

    if(destinationOffset < sourceOffset){
        u64 movedBytes = 0;
        while(movedBytes < byteCount){
            const u64 pendingBytes = byteCount - movedBytes;
            const u64 copyBytes = Min(pendingBytes, moveChunkBytes);

            const u64 readOffset = sourceOffset + movedBytes;
            const u64 writeOffset = destinationOffset + movedBytes;
            if(!readBytesLocked(readOffset, moveBuffer.data(), copyBytes))
                return false;
            if(!writeBytesLocked(writeOffset, moveBuffer.data(), copyBytes))
                return false;

            movedBytes += copyBytes;
        }
        return true;
    }

    u64 remainingBytes = byteCount;
    while(remainingBytes > 0){
        const u64 copyBytes = Min(remainingBytes, moveChunkBytes);
        const u64 chunkBegin = remainingBytes - copyBytes;

        const u64 readOffset = sourceOffset + chunkBegin;
        const u64 writeOffset = destinationOffset + chunkBegin;
        if(!readBytesLocked(readOffset, moveBuffer.data(), copyBytes))
            return false;
        if(!writeBytesLocked(writeOffset, moveBuffer.data(), copyBytes))
            return false;

        remainingBytes -= copyBytes;
    }

    return true;
}

bool VolumeFileSystem::trimSegmentsForNextFreeOffsetLocked(){

    if(!m_writable || m_segmentSize == 0){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpTrimSegments, FilesystemVolumeDetail::s_VolumeDetailNotWritableOrSizeZero);
        return false;
    }
    if(m_segmentPaths.empty()){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpTrimSegments, "no segments are mounted");
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
        const Path removePath = m_segmentPaths.back();
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

