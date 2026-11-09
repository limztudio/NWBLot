// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_segment_transfer.h"
#include "volume_storage_detail.h"

#include <core/common/log.h>

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
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed: range [{}..{}) exceeds capacity {}"), ConvertString(volumeName), ConvertString(operation), offset, endOffset, capacityBytes);
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

template<typename Stream, typename SeekStream, typename TransferStream>
static b8 TransferSegmentChunk(
    const AStringView volumeName,
    const Span<const Path> segmentPaths,
    const usize segmentIndex,
    const StreamOffset streamOffset,
    const AStringView openOperation,
    const AStringView seekOperation,
    SeekStream&& seekStream,
    TransferStream&& transferStream,
    Stream& stream
){
    if(!stream.is_open()){
        LogFailureWithPath(volumeName, openOperation, segmentPaths[segmentIndex], LastErrnoMessage());
        return false;
    }

    seekStream(stream, streamOffset);
    if(!stream.good()){
        LogFailureWithPath(volumeName, seekOperation, segmentPaths[segmentIndex], LastErrnoMessage());
        return false;
    }

    return transferStream(stream);
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

static b8 ReadSegmentBytes(
    const AStringView volumeName,
    const Span<const Path> segmentPaths,
    const usize segmentIndex,
    const StreamOffset streamOffset,
    const StreamSize streamChunkSize,
    const u64 chunkBytes,
    u8*& outputBytes
){
    InputFileStream stream(segmentPaths[segmentIndex], InputFileStream::binary);
    return TransferSegmentChunk(
        volumeName,
        segmentPaths,
        segmentIndex,
        streamOffset,
        "readBytes:open",
        "readBytes:seek",
        [](auto& stream, const StreamOffset offset)noexcept(noexcept(stream.seekg(offset))){ stream.seekg(offset); },
        [&](auto& stream){
            stream.read(reinterpret_cast<char*>(outputBytes), streamChunkSize);
            if(stream.gcount() != streamChunkSize){
                NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): readBytes failed on '{}': requested {} bytes, received {} bytes, errno {}")
                    , ConvertString(volumeName)
                    , ConvertString(segmentPaths[segmentIndex].native())
                    , static_cast<i64>(streamChunkSize)
                    , static_cast<i64>(stream.gcount())
                    , ConvertString(LastErrnoMessage())
                );
                return false;
            }

            outputBytes += chunkBytes;
            return true;
        },
        stream
    );
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
    FileStream stream(segmentPaths[segmentIndex], FileStream::binary | FileStream::in | FileStream::out);
    return TransferSegmentChunk(
        volumeName,
        segmentPaths,
        segmentIndex,
        streamOffset,
        "writeBytes:open",
        "writeBytes:seek",
        [](auto& stream, const StreamOffset offset)noexcept(noexcept(stream.seekp(offset))){ stream.seekp(offset); },
        [&](auto& stream){
            stream.write(reinterpret_cast<const char*>(inputBytes), streamChunkSize);
            if(!stream.good()){
                NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): writeBytes failed on '{}': attempted {} bytes, errno {}")
                    , ConvertString(volumeName)
                    , ConvertString(segmentPaths[segmentIndex].native())
                    , static_cast<i64>(streamChunkSize)
                    , ConvertString(LastErrnoMessage())
                );
                return false;
            }

            stream.close();
            if(!stream.good()){
                LogFailureWithPath(volumeName, "writeBytes:close", segmentPaths[segmentIndex], LastErrnoMessage());
                return false;
            }

            inputBytes += chunkBytes;
            return true;
        },
        stream
    );
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

