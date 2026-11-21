// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/alloc/scratch.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FilesystemVolumeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SegmentTransferCache final : NoCopy{
public:
    SegmentTransferCache(Alloc::ScratchArena& scratchArena, b8 writable)noexcept;
    ~SegmentTransferCache()noexcept;


public:
    void closeAll()noexcept;
    [[nodiscard]] void* acquireWindowsHandle(AStringView volumeName, const Path& segmentPath, usize segmentIndex, AStringView openOperation);
    [[nodiscard]] int acquirePosixFd(AStringView volumeName, const Path& segmentPath, usize segmentIndex, AStringView openOperation);


private:
    Vector<usize, Alloc::ScratchArena> m_nativeHandles;
    b8 m_writable = false;
};


class VolumeSegmentTransfer final{
public:
    [[nodiscard]] static b8 Read(AStringView volumeName, Span<const Path> segmentPaths, u64 segmentSize, u64 offset, u64 byteCount, u8* outputBytes);
    [[nodiscard]] static b8 Write(AStringView volumeName, Span<const Path> segmentPaths, u64 segmentSize, u64 offset, u64 byteCount, const u8* inputBytes);
    [[nodiscard]] static b8 ReadCached(AStringView volumeName, Span<const Path> segmentPaths, u64 segmentSize, u64 offset, u64 byteCount, u8* outputBytes, SegmentTransferCache& cache);
    [[nodiscard]] static b8 WriteCached(AStringView volumeName, Span<const Path> segmentPaths, u64 segmentSize, u64 offset, u64 byteCount, const u8* inputBytes, SegmentTransferCache& cache);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

