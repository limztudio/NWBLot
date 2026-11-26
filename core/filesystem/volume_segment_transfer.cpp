// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_segment_transfer.h"
#include "volume_storage_detail.h"

#include <core/common/log.h>

#include <global/error_code.h>
#include <global/filesystem/operations_file_io.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FilesystemVolumeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<StreamOffset> ToStreamOff(const u64 value)noexcept{
    if(!::CanRepresentU64<StreamOffset>(value))
        return MakeUnexpected(Failure{});
    return static_cast<StreamOffset>(value);
}

static Expected<StreamSize> ToStreamSize(const u64 value)noexcept{
    if(!::CanRepresentU64<StreamSize>(value))
        return MakeUnexpected(Failure{});
    return static_cast<StreamSize>(value);
}

template<typename ChunkFunc>
static b8 ForEachSegmentChunk(const AStringView volumeName, const AStringView operation, const Span<const Path> segmentPaths, const u64 segmentSize, const u64 offset, const u64 byteCount, ChunkFunc&& chunkFunc){
    if(segmentSize == 0){
        LogFailure(volumeName, operation, FilesystemVolumeDetail::s_VolumeDetailSegmentSizeZero);
        return false;
    }

    u64 endOffset = 0;
    const auto endOffsetResult = AddNoOverflow(offset, byteCount);
    if(!endOffsetResult){
        LogFailure(volumeName, operation, FilesystemVolumeDetail::s_VolumeDetailOffsetOverflow);
        return false;
    }
    endOffset = *endOffsetResult;
    if(static_cast<u64>(segmentPaths.size()) > Limit<u64>::s_Max / segmentSize){
        LogFailure(volumeName, operation, "capacity overflow");
        return false;
    }
    const u64 capacityBytes = static_cast<u64>(segmentPaths.size()) * segmentSize;
    if(endOffset > capacityBytes){
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): {} failed: range [{}..{}) exceeds capacity {}"), ConvertString(volumeName), ConvertString(operation), offset, endOffset, capacityBytes);
        return false;
    }

    u64 currentOffset = offset;
    u64 remainingBytes = byteCount;
    while(remainingBytes > 0){
        const usize segmentIndex = static_cast<usize>(currentOffset / segmentSize);
        const u64 segmentOffset = currentOffset % segmentSize;
        const u64 chunkBytes = Min(remainingBytes, segmentSize - segmentOffset);

        const auto streamOffset = ToStreamOff(segmentOffset);
        if(!streamOffset){
            LogFailure(volumeName, operation, "segment offset cannot be represented as stream offset");
            return false;
        }
        const auto streamChunkSize = ToStreamSize(chunkBytes);
        if(!streamChunkSize){
            LogFailure(volumeName, operation, "chunk size cannot be represented as stream size");
            return false;
        }

        if(!chunkFunc(segmentIndex, *streamOffset, *streamChunkSize, chunkBytes))
            return false;

        currentOffset += chunkBytes;
        remainingBytes -= chunkBytes;
    }

    return true;
}

template<typename Bytes, typename TransferChunk>
static b8 TransferVolumeBytes(const AStringView volumeName, const AStringView operation, const Span<const Path> segmentPaths, const u64 segmentSize, const u64 offset, const u64 byteCount, TransferChunk&& transferChunk, Bytes& bytes){
    return ForEachSegmentChunk(
        volumeName,
        operation,
        segmentPaths,
        segmentSize,
        offset,
        byteCount,
        [&](const usize segmentIndex, const StreamOffset streamOffset, const StreamSize streamChunkSize, const u64 chunkBytes)noexcept(
            noexcept(transferChunk(volumeName, segmentPaths, segmentIndex, streamOffset, streamChunkSize, chunkBytes, bytes))
            && (
                !IsReference_V<decltype(transferChunk(volumeName, segmentPaths, segmentIndex, streamOffset, streamChunkSize, chunkBytes, bytes))>
                || IsNothrowConvertible_V<
                    decltype(transferChunk(volumeName, segmentPaths, segmentIndex, streamOffset, streamChunkSize, chunkBytes, bytes)),
                    Decay_T<decltype(transferChunk(volumeName, segmentPaths, segmentIndex, streamOffset, streamChunkSize, chunkBytes, bytes))>
                >
            )
        ){
            return transferChunk(volumeName, segmentPaths, segmentIndex, streamOffset, streamChunkSize, chunkBytes, bytes);
        }
    );
}

#if defined(FOUNDATION_PLATFORM_WINDOWS)
[[nodiscard]] static HANDLE OpenSegmentNativeHandle(const Path& path, const b8 writable)noexcept{
    return CreateFile(
        path.c_str(),
        writable ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ,
        writable ? FILE_SHARE_READ : (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE),
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
}

[[nodiscard]] static b8 ReadFromHandle(
    const AStringView volumeName,
    const Span<const Path> segmentPaths,
    const usize segmentIndex,
    const HANDLE handle,
    const StreamOffset streamOffset,
    const StreamSize streamChunkSize,
    const u64 chunkBytes,
    u8*& outputBytes
){
    LARGE_INTEGER seekPos{};
    seekPos.QuadPart = static_cast<LONGLONG>(streamOffset);
    if(!SetFilePointerEx(handle, seekPos, nullptr, FILE_BEGIN)){
        LogFailureWithFsError(volumeName, "readBytes:seek", segmentPaths[segmentIndex], LastSystemError());
        return false;
    }

    u64 remaining = chunkBytes;
    u64 totalTransferred = 0u;
    u8* cursor = outputBytes;
    while(remaining > 0u){
        const DWORD requestBytes = static_cast<DWORD>(Min<u64>(remaining, 0x40000000ull));
        DWORD bytesRead = 0u;
        if(!ReadFile(handle, cursor, requestBytes, &bytesRead, nullptr)){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): readBytes failed on '{}': requested {} bytes, received {} bytes, error {}")
                , ConvertString(volumeName)
                , ConvertString(segmentPaths[segmentIndex].native())
                , static_cast<i64>(streamChunkSize)
                , static_cast<i64>(totalTransferred)
                , ConvertString(LastSystemError().message())
            );
            return false;
        }
        if(bytesRead == 0u){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): readBytes failed on '{}': requested {} bytes, received {} bytes, errno {}")
                , ConvertString(volumeName)
                , ConvertString(segmentPaths[segmentIndex].native())
                , static_cast<i64>(streamChunkSize)
                , static_cast<i64>(totalTransferred)
                , ConvertString(LastErrnoMessage())
            );
            return false;
        }
        cursor += bytesRead;
        totalTransferred += bytesRead;
        remaining -= bytesRead;
    }

    outputBytes += chunkBytes;
    return true;
}

[[nodiscard]] static b8 WriteToHandle(
    const AStringView volumeName,
    const Span<const Path> segmentPaths,
    const usize segmentIndex,
    const HANDLE handle,
    const StreamOffset streamOffset,
    const StreamSize streamChunkSize,
    const u64 chunkBytes,
    const u8*& inputBytes
){
    LARGE_INTEGER seekPos{};
    seekPos.QuadPart = static_cast<LONGLONG>(streamOffset);
    if(!SetFilePointerEx(handle, seekPos, nullptr, FILE_BEGIN)){
        LogFailureWithFsError(volumeName, "writeBytes:seek", segmentPaths[segmentIndex], LastSystemError());
        return false;
    }

    u64 remaining = chunkBytes;
    const u8* cursor = inputBytes;
    while(remaining > 0u){
        const DWORD requestBytes = static_cast<DWORD>(Min<u64>(remaining, 0x40000000ull));
        DWORD bytesWritten = 0u;
        if(!WriteFile(handle, cursor, requestBytes, &bytesWritten, nullptr) || bytesWritten == 0u){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): writeBytes failed on '{}': attempted {} bytes, error {}")
                , ConvertString(volumeName)
                , ConvertString(segmentPaths[segmentIndex].native())
                , static_cast<i64>(streamChunkSize)
                , ConvertString(LastSystemError().message())
            );
            return false;
        }
        cursor += bytesWritten;
        remaining -= bytesWritten;
    }

    inputBytes += chunkBytes;
    return true;
}
#else
[[nodiscard]] static int OpenSegmentPosixFd(const Path& path, const b8 writable)noexcept{
    return ::open(path.c_str(), writable ? (O_RDWR | O_CLOEXEC) : (O_RDONLY | O_CLOEXEC));
}

[[nodiscard]] static b8 ReadFromFd(
    const AStringView volumeName,
    const Span<const Path> segmentPaths,
    const usize segmentIndex,
    const int fd,
    const StreamOffset streamOffset,
    const StreamSize streamChunkSize,
    const u64 chunkBytes,
    u8*& outputBytes
){
    u64 remaining = chunkBytes;
    u64 totalTransferred = 0u;
    off_t fileOffset = static_cast<off_t>(streamOffset);
    u8* cursor = outputBytes;
    while(remaining > 0u){
        const ssize_t received = ::pread(fd, cursor, static_cast<size_t>(remaining), fileOffset);
        if(received < 0){
            if(errno == EINTR)
                continue;
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): readBytes failed on '{}': requested {} bytes, received {} bytes, errno {}")
                , ConvertString(volumeName)
                , ConvertString(segmentPaths[segmentIndex].native())
                , static_cast<i64>(streamChunkSize)
                , static_cast<i64>(totalTransferred)
                , ConvertString(LastErrnoMessage())
            );
            return false;
        }
        if(received == 0){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): readBytes failed on '{}': requested {} bytes, received {} bytes, errno {}")
                , ConvertString(volumeName)
                , ConvertString(segmentPaths[segmentIndex].native())
                , static_cast<i64>(streamChunkSize)
                , static_cast<i64>(totalTransferred)
                , ConvertString(LastErrnoMessage())
            );
            return false;
        }
        cursor += received;
        totalTransferred += static_cast<u64>(received);
        fileOffset += received;
        remaining -= static_cast<u64>(received);
    }

    outputBytes += chunkBytes;
    return true;
}

[[nodiscard]] static b8 WriteToFd(
    const AStringView volumeName,
    const Span<const Path> segmentPaths,
    const usize segmentIndex,
    const int fd,
    const StreamOffset streamOffset,
    const StreamSize streamChunkSize,
    const u64 chunkBytes,
    const u8*& inputBytes
){
    u64 remaining = chunkBytes;
    off_t fileOffset = static_cast<off_t>(streamOffset);
    const u8* cursor = inputBytes;
    while(remaining > 0u){
        const ssize_t written = ::pwrite(fd, cursor, static_cast<size_t>(remaining), fileOffset);
        if(written < 0){
            if(errno == EINTR)
                continue;
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Filesystem('{}'): writeBytes failed on '{}': attempted {} bytes, errno {}")
                , ConvertString(volumeName)
                , ConvertString(segmentPaths[segmentIndex].native())
                , static_cast<i64>(streamChunkSize)
                , ConvertString(LastErrnoMessage())
            );
            return false;
        }
        if(written == 0){
            LogFailureWithPath(volumeName, "writeBytes:write", segmentPaths[segmentIndex], LastErrnoMessage());
            return false;
        }
        cursor += written;
        fileOffset += written;
        remaining -= static_cast<u64>(written);
    }

    inputBytes += chunkBytes;
    return true;
}
#endif

template<typename HandleVector>
static void CloseNativeHandleVector(HandleVector& nativeHandles)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    const usize invalidValue = reinterpret_cast<usize>(INVALID_HANDLE_VALUE);
    for(const usize raw : nativeHandles){
        if(raw != invalidValue && raw != 0u)
            CloseHandle(reinterpret_cast<HANDLE>(raw));
    }
#else
    const usize invalidValue = static_cast<usize>(~0u);
    for(const usize raw : nativeHandles){
        if(raw != invalidValue)
            ::close(static_cast<int>(raw));
    }
#endif
    nativeHandles.clear();
}

#if defined(FOUNDATION_PLATFORM_WINDOWS)
template<typename HandleVector>
[[nodiscard]] static HANDLE AcquireCachedWindowsHandle(
    HandleVector& nativeHandles,
    const b8 writable,
    const AStringView volumeName,
    const Path& segmentPath,
    const usize segmentIndex,
    const AStringView openOperation
){
    const usize invalidValue = reinterpret_cast<usize>(INVALID_HANDLE_VALUE);
    if(segmentIndex >= nativeHandles.size())
        nativeHandles.resize(segmentIndex + 1u, invalidValue);
    if(nativeHandles[segmentIndex] != invalidValue)
        return reinterpret_cast<HANDLE>(nativeHandles[segmentIndex]);

    const HANDLE opened = OpenSegmentNativeHandle(segmentPath, writable);
    if(opened == INVALID_HANDLE_VALUE){
        LogFailureWithFsError(volumeName, openOperation, segmentPath, LastSystemError());
        return nullptr;
    }
    nativeHandles[segmentIndex] = reinterpret_cast<usize>(opened);
    return opened;
}
#else
template<typename HandleVector>
[[nodiscard]] static int AcquireCachedPosixFd(
    HandleVector& nativeHandles,
    const b8 writable,
    const AStringView volumeName,
    const Path& segmentPath,
    const usize segmentIndex,
    const AStringView openOperation
){
    const usize invalidValue = static_cast<usize>(~0u);
    if(segmentIndex >= nativeHandles.size())
        nativeHandles.resize(segmentIndex + 1u, invalidValue);
    if(nativeHandles[segmentIndex] != invalidValue)
        return static_cast<int>(nativeHandles[segmentIndex]);

    const int opened = OpenSegmentPosixFd(segmentPath, writable);
    if(opened < 0){
        LogFailureWithPath(volumeName, openOperation, segmentPath, LastErrnoMessage());
        return -1;
    }
    nativeHandles[segmentIndex] = static_cast<usize>(opened);
    return opened;
}
#endif

SegmentTransferCache::SegmentTransferCache(Alloc::ScratchArena& scratchArena, const b8 writable)noexcept
    : m_nativeHandles(scratchArena)
    , m_writable(writable)
{}

SegmentTransferCache::~SegmentTransferCache()noexcept{
    closeAll();
}

void SegmentTransferCache::closeAll()noexcept{
    CloseNativeHandleVector(m_nativeHandles);
}

void* SegmentTransferCache::acquireWindowsHandle(const AStringView volumeName, const Path& segmentPath, const usize segmentIndex, const AStringView openOperation){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    return AcquireCachedWindowsHandle(m_nativeHandles, m_writable, volumeName, segmentPath, segmentIndex, openOperation);
#else
    (void)volumeName;
    (void)segmentPath;
    (void)segmentIndex;
    (void)openOperation;
    return nullptr;
#endif
}

int SegmentTransferCache::acquirePosixFd(const AStringView volumeName, const Path& segmentPath, const usize segmentIndex, const AStringView openOperation){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    (void)volumeName;
    (void)segmentPath;
    (void)segmentIndex;
    (void)openOperation;
    return -1;
#else
    return AcquireCachedPosixFd(m_nativeHandles, m_writable, volumeName, segmentPath, segmentIndex, openOperation);
#endif
}

static b8 ReadSegmentBytes(
    const AStringView volumeName,
    const Span<const Path> segmentPaths,
    const usize segmentIndex,
    const StreamOffset streamOffset,
    const StreamSize streamChunkSize,
    const u64 chunkBytes,
    u8*& outputBytes
){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    const HANDLE handle = OpenSegmentNativeHandle(segmentPaths[segmentIndex], false);
    if(handle == INVALID_HANDLE_VALUE){
        LogFailureWithFsError(volumeName, "readBytes:open", segmentPaths[segmentIndex], LastSystemError());
        return false;
    }
    const b8 ok = ReadFromHandle(volumeName, segmentPaths, segmentIndex, handle, streamOffset, streamChunkSize, chunkBytes, outputBytes);
    CloseHandle(handle);
    return ok;
#else
    const int fd = OpenSegmentPosixFd(segmentPaths[segmentIndex], false);
    if(fd < 0){
        LogFailureWithPath(volumeName, "readBytes:open", segmentPaths[segmentIndex], LastErrnoMessage());
        return false;
    }
    const b8 ok = ReadFromFd(volumeName, segmentPaths, segmentIndex, fd, streamOffset, streamChunkSize, chunkBytes, outputBytes);
    ::close(fd);
    return ok;
#endif
}

static b8 WriteSegmentBytes(
    const AStringView volumeName,
    const Span<const Path> segmentPaths,
    const usize segmentIndex,
    const StreamOffset streamOffset,
    const StreamSize streamChunkSize,
    const u64 chunkBytes,
    const u8*& inputBytes
){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    const HANDLE handle = OpenSegmentNativeHandle(segmentPaths[segmentIndex], true);
    if(handle == INVALID_HANDLE_VALUE){
        LogFailureWithFsError(volumeName, "writeBytes:open", segmentPaths[segmentIndex], LastSystemError());
        return false;
    }
    const b8 ok = WriteToHandle(volumeName, segmentPaths, segmentIndex, handle, streamOffset, streamChunkSize, chunkBytes, inputBytes);
    if(!CloseHandle(handle) && ok){
        LogFailureWithFsError(volumeName, "writeBytes:close", segmentPaths[segmentIndex], LastSystemError());
        return false;
    }
    return ok;
#else
    const int fd = OpenSegmentPosixFd(segmentPaths[segmentIndex], true);
    if(fd < 0){
        LogFailureWithPath(volumeName, "writeBytes:open", segmentPaths[segmentIndex], LastErrnoMessage());
        return false;
    }
    const b8 ok = WriteToFd(volumeName, segmentPaths, segmentIndex, fd, streamOffset, streamChunkSize, chunkBytes, inputBytes);
    if(::close(fd) != 0 && ok){
        LogFailureWithPath(volumeName, "writeBytes:close", segmentPaths[segmentIndex], LastErrnoMessage());
        return false;
    }
    return ok;
#endif
}

struct ReadSegmentBytesOp{
    b8 operator()(
        const AStringView volumeName,
        const Span<const Path> segmentPaths,
        const usize segmentIndex,
        const StreamOffset streamOffset,
        const StreamSize streamChunkSize,
        const u64 chunkBytes,
        u8*& outputBytes
    )const{
        return ReadSegmentBytes(volumeName, segmentPaths, segmentIndex, streamOffset, streamChunkSize, chunkBytes, outputBytes);
    }
};

struct WriteSegmentBytesOp{
    b8 operator()(
        const AStringView volumeName,
        const Span<const Path> segmentPaths,
        const usize segmentIndex,
        const StreamOffset streamOffset,
        const StreamSize streamChunkSize,
        const u64 chunkBytes,
        const u8*& inputBytes
    )const{
        return WriteSegmentBytes(volumeName, segmentPaths, segmentIndex, streamOffset, streamChunkSize, chunkBytes, inputBytes);
    }
};

b8 VolumeSegmentTransfer::Read(const AStringView volumeName, const Span<const Path> segmentPaths, const u64 segmentSize, const u64 offset, const u64 byteCount, u8* outputBytes){
    return TransferVolumeBytes(volumeName, s_VolumeOpReadBytes, segmentPaths, segmentSize, offset, byteCount, ReadSegmentBytesOp{}, outputBytes);
}

b8 VolumeSegmentTransfer::Write(const AStringView volumeName, const Span<const Path> segmentPaths, const u64 segmentSize, const u64 offset, const u64 byteCount, const u8* inputBytes){
    return TransferVolumeBytes(volumeName, s_VolumeOpWriteBytes, segmentPaths, segmentSize, offset, byteCount, WriteSegmentBytesOp{}, inputBytes);
}

b8 VolumeSegmentTransfer::ReadCached(const AStringView volumeName, const Span<const Path> segmentPaths, const u64 segmentSize, const u64 offset, const u64 byteCount, u8* outputBytes, SegmentTransferCache& cache){
    return TransferVolumeBytes(
        volumeName,
        s_VolumeOpReadBytes,
        segmentPaths,
        segmentSize,
        offset,
        byteCount,
        [&cache](const AStringView volName, const Span<const Path> paths, const usize segmentIndex, const StreamOffset streamOffset, const StreamSize streamChunkSize, const u64 chunkBytes, u8*& outPtr){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
            const HANDLE handle = static_cast<HANDLE>(cache.acquireWindowsHandle(volName, paths[segmentIndex], segmentIndex, "readBytes:open"));
            if(!handle)
                return false;
            return ReadFromHandle(volName, paths, segmentIndex, handle, streamOffset, streamChunkSize, chunkBytes, outPtr);
#else
            const int fd = cache.acquirePosixFd(volName, paths[segmentIndex], segmentIndex, "readBytes:open");
            if(fd < 0)
                return false;
            return ReadFromFd(volName, paths, segmentIndex, fd, streamOffset, streamChunkSize, chunkBytes, outPtr);
#endif
        },
        outputBytes
    );
}

b8 VolumeSegmentTransfer::WriteCached(const AStringView volumeName, const Span<const Path> segmentPaths, const u64 segmentSize, const u64 offset, const u64 byteCount, const u8* inputBytes, SegmentTransferCache& cache){
    return TransferVolumeBytes(
        volumeName,
        s_VolumeOpWriteBytes,
        segmentPaths,
        segmentSize,
        offset,
        byteCount,
        [&cache](const AStringView volName, const Span<const Path> paths, const usize segmentIndex, const StreamOffset streamOffset, const StreamSize streamChunkSize, const u64 chunkBytes, const u8*& inPtr){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
            const HANDLE handle = static_cast<HANDLE>(cache.acquireWindowsHandle(volName, paths[segmentIndex], segmentIndex, "writeBytes:open"));
            if(!handle)
                return false;
            return WriteToHandle(volName, paths, segmentIndex, handle, streamOffset, streamChunkSize, chunkBytes, inPtr);
#else
            const int fd = cache.acquirePosixFd(volName, paths[segmentIndex], segmentIndex, "writeBytes:open");
            if(fd < 0)
                return false;
            return WriteToFd(volName, paths, segmentIndex, fd, streamOffset, streamChunkSize, chunkBytes, inPtr);
#endif
        },
        inputBytes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

