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


static bool ResizeFile(const Path& path, const u64 byteCount, ErrorCode& outError){
#if defined(NWB_PLATFORM_WINDOWS)
    if(byteCount > static_cast<u64>(Limit<LONGLONG>::s_Max)){
        GlobalFilesystemDetail::SetValueTooLargeError(outError);
        return false;
    }

    HANDLE file = CreateFile(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if(file == INVALID_HANDLE_VALUE){
        GlobalFilesystemDetail::SetLastSystemError(outError);
        return false;
    }

    LARGE_INTEGER offset{};
    offset.QuadPart = static_cast<LONGLONG>(byteCount);
    const bool seekSucceeded = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != 0;
    const bool resizeSucceeded = seekSucceeded && SetEndOfFile(file) != 0;
    if(resizeSucceeded)
        GlobalFilesystemDetail::ClearError(outError);
    else
        GlobalFilesystemDetail::SetLastSystemError(outError);
    CloseHandle(file);
    return resizeSucceeded;
#else
    if(!CanRepresentU64<off_t>(byteCount)){
        GlobalFilesystemDetail::SetValueTooLargeError(outError);
        return false;
    }

    if(::truncate(path.c_str(), static_cast<off_t>(byteCount)) == 0){
        GlobalFilesystemDetail::ClearError(outError);
        return true;
    }

    GlobalFilesystemDetail::SetLastSystemError(outError);
    return false;
#endif
}

bool VolumeFileSystem::moveBytesLocked(const u64 destinationOffset, const u64 sourceOffset, const u64 byteCount){
    if(byteCount == 0 || destinationOffset == sourceOffset)
        return true;
    if(m_segmentSize == 0){
        FilesystemVolumeDetail::LogFailure(m_volumeName, "moveBytes", "segment size is zero");
        return false;
    }

    u64 sourceEndOffset = 0;
    if(!FilesystemVolumeDetail::AddNoOverflow(sourceOffset, byteCount, sourceEndOffset)){
        FilesystemVolumeDetail::LogFailure(m_volumeName, "moveBytes", "source range overflow");
        return false;
    }

    u64 capacityBytes = 0;
    if(!computeLogicalCapacityLocked(capacityBytes)){
        FilesystemVolumeDetail::LogFailure(m_volumeName, "moveBytes", "capacity overflow");
        return false;
    }
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
    if(!FilesystemVolumeDetail::AddNoOverflow(destinationOffset, byteCount, destinationEndOffset)){
        FilesystemVolumeDetail::LogFailure(m_volumeName, "moveBytes", "destination range overflow");
        return false;
    }
    if(!ensureCapacityLocked(destinationEndOffset)){
        FilesystemVolumeDetail::LogFailure(m_volumeName, "moveBytes", "failed to ensure destination capacity");
        return false;
    }

    const u64 moveChunkBytes = Min(FilesystemVolumeDetail::s_VolumeMoveChunkBytes, m_segmentSize);
    if(moveChunkBytes == 0 || moveChunkBytes > static_cast<u64>(Limit<usize>::s_Max)){
        FilesystemVolumeDetail::LogFailure(m_volumeName, "moveBytes", "invalid move chunk size");
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
    ErrorCode errorCode;

    if(!m_writable || m_segmentSize == 0){
        FilesystemVolumeDetail::LogFailure(m_volumeName, "trimSegments", "filesystem is not writable or segment size is zero");
        return false;
    }
    if(m_segmentPaths.empty()){
        FilesystemVolumeDetail::LogFailure(m_volumeName, "trimSegments", "no segments are mounted");
        return false;
    }

    const u64 requiredBytes = Max(m_nextFreeOffset, m_metadataBytes);
    u64 requiredSegments = DivideUp(requiredBytes, m_segmentSize);
    if(requiredSegments == 0)
        requiredSegments = 1;
    if(requiredSegments > static_cast<u64>(m_segmentPaths.size())){
        FilesystemVolumeDetail::LogFailure(m_volumeName, "trimSegments", "required segment count exceeds mounted segment count");
        return false;
    }
    if(requiredSegments > static_cast<u64>(Limit<usize>::s_Max)){
        FilesystemVolumeDetail::LogFailure(m_volumeName, "trimSegments", "required segment count exceeds usize range");
        return false;
    }

    while(m_segmentPaths.size() > static_cast<usize>(requiredSegments)){
        const Path removePath = m_segmentPaths.back();
        if(!RemoveFile(removePath, errorCode)){
            if(errorCode){
                FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, "trimSegments:remove", removePath, errorCode);
            }
            else{
                FilesystemVolumeDetail::LogFailureWithPath(m_volumeName, "trimSegments:remove", removePath, "segment was not present");
            }
            return false;
        }
        m_segmentPaths.pop_back();
    }

    u64 requiredLastSegmentBytes = requiredBytes % m_segmentSize;
    if(requiredLastSegmentBytes == 0)
        requiredLastSegmentBytes = m_segmentSize;

    const Path& lastSegmentPath = m_segmentPaths.back();
    const u64 currentLastSegmentBytes = FileSize(lastSegmentPath, errorCode);
    if(errorCode){
        FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, "trimSegments:file_size", lastSegmentPath, errorCode);
        return false;
    }
    if(currentLastSegmentBytes == requiredLastSegmentBytes)
        return true;

    if(!ResizeFile(lastSegmentPath, requiredLastSegmentBytes, errorCode)){
        FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, "trimSegments:resize", lastSegmentPath, errorCode);
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

