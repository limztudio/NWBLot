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


namespace __hidden_filesystem{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_ErrnoMessageBufferBytes = 256u;


static bool ToStreamOff(const u64 value, GlobalFilesystemDetail::StreamOffset& out){
    if(!CanRepresentU64<GlobalFilesystemDetail::StreamOffset>(value))
        return false;
    out = static_cast<GlobalFilesystemDetail::StreamOffset>(value);
    return true;
}

static bool ToStreamSize(const u64 value, GlobalFilesystemDetail::StreamSize& out){
    if(!CanRepresentU64<GlobalFilesystemDetail::StreamSize>(value))
        return false;
    out = static_cast<GlobalFilesystemDetail::StreamSize>(value);
    return true;
}

static bool ResizeFile(const Path& path, const u64 byteCount, ErrorCode& outError){
#if defined(NWB_PLATFORM_WINDOWS)
    if(byteCount > static_cast<u64>(Limit<LONGLONG>::s_Max)){
        outError = std::make_error_code(std::errc::value_too_large);
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
        outError = std::make_error_code(std::errc::value_too_large);
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

ACompactString LastErrnoMessage(){
    const i32 errorNumber = errno;
    if(errorNumber == 0)
        return ACompactString("none");

    char errorText[s_ErrnoMessageBufferBytes] = {};
    if(NWB_STRERROR(errorText, sizeof(errorText), errorNumber) != 0)
        return ACompactString("unknown");

    ACompactString output(errorText);
    output += " (";
    char numberText[TextDetail::s_DecimalTextBufferBytes] = {};
    output += FormatDecimal(errorNumber, numberText);
    output += ")";
    return output;
}

void LogFailure(AStringView volumeName, AStringView operation, AStringView detail){
    NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed: {}")
        , StringConvert(volumeName)
        , StringConvert(operation)
        , StringConvert(detail)
    );
}

void LogFailureWithPath(AStringView volumeName, AStringView operation, const Path& path, AStringView detail){
    NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed on '{}': {}")
        , StringConvert(volumeName)
        , StringConvert(operation)
        , StringConvert(path.string())
        , StringConvert(detail)
    );
}

void LogFailureWithFsError(AStringView volumeName, AStringView operation, const Path& path, const ErrorCode& errorCode){
    NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed on '{}': [{}] {}")
        , StringConvert(volumeName)
        , StringConvert(operation)
        , StringConvert(path.string())
        , errorCode.value()
        , StringConvert(errorCode.message())
    );
}

bool ReadVolumeHeaderFromSegment(
    const AStringView volumeName,
    const Path& segmentPath,
    VolumeHeaderDisk& outHeader
){
    outHeader = {};

    GlobalFilesystemDetail::InputFileStream stream(
        segmentPath,
        GlobalFilesystemDetail::InputFileStream::binary
    );
    if(!stream.is_open()){
        LogFailureWithPath(volumeName, "mount:open_header", segmentPath, LastErrnoMessage());
        return false;
    }

    stream.read(
        reinterpret_cast<char*>(&outHeader),
        static_cast<GlobalFilesystemDetail::StreamSize>(sizeof(outHeader))
    );
    if(stream.good())
        return true;
    if(stream.eof() && stream.gcount() == static_cast<GlobalFilesystemDetail::StreamSize>(sizeof(outHeader)))
        return true;

    LogFailureWithPath(volumeName, "mount:read_header", segmentPath, LastErrnoMessage());
    return false;
}

template<typename SegmentPaths, typename ChunkFunc>
static bool ForEachSegmentChunk(
    const AStringView volumeName,
    const AStringView operation,
    const SegmentPaths& segmentPaths,
    const u64 segmentSize,
    const u64 offset,
    const u64 byteCount,
    ChunkFunc&& chunkFunc){
    if(segmentSize == 0){
        LogFailure(volumeName, operation, "segment size is zero");
        return false;
    }

    u64 endOffset = 0;
    if(!AddNoOverflow(offset, byteCount, endOffset)){
        LogFailure(volumeName, operation, "offset overflow");
        return false;
    }
    if(static_cast<u64>(segmentPaths.size()) > Limit<u64>::s_Max / segmentSize){
        LogFailure(volumeName, operation, "capacity overflow");
        return false;
    }
    const u64 capacityBytes = static_cast<u64>(segmentPaths.size()) * segmentSize;
    if(endOffset > capacityBytes){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed: range [{}..{}) exceeds capacity {}")
            , StringConvert(volumeName)
            , StringConvert(operation)
            , offset
            , endOffset
            , capacityBytes
        );
        return false;
    }

    u64 currentOffset = offset;
    u64 remainingBytes = byteCount;
    while(remainingBytes > 0){
        const usize segmentIndex = static_cast<usize>(currentOffset / segmentSize);
        const u64 segmentOffset = currentOffset % segmentSize;
        const u64 chunkBytes = Min(remainingBytes, segmentSize - segmentOffset);

        GlobalFilesystemDetail::StreamOffset streamOffset = 0;
        GlobalFilesystemDetail::StreamSize streamChunkSize = 0;
        if(!ToStreamOff(segmentOffset, streamOffset)){
            LogFailure(volumeName, operation, "segment offset cannot be represented as stream offset");
            return false;
        }
        if(!ToStreamSize(chunkBytes, streamChunkSize)){
            LogFailure(volumeName, operation, "chunk size cannot be represented as stream size");
            return false;
        }

        if(!chunkFunc(segmentIndex, streamOffset, streamChunkSize, chunkBytes))
            return false;

        currentOffset += chunkBytes;
        remainingBytes -= chunkBytes;
    }

    return true;
}

template<typename SegmentPaths, typename Stream, typename SeekStream, typename TransferStream>
static bool TransferSegmentChunk(
    const AStringView volumeName,
    const SegmentPaths& segmentPaths,
    const usize segmentIndex,
    const GlobalFilesystemDetail::StreamOffset streamOffset,
    const AStringView openOperation,
    const AStringView seekOperation,
    Stream& stream,
    SeekStream&& seekStream,
    TransferStream&& transferStream){
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

template<typename SegmentPaths, typename Bytes, typename TransferChunk>
static bool TransferVolumeBytes(
    const AStringView volumeName,
    const AStringView operation,
    const SegmentPaths& segmentPaths,
    const u64 segmentSize,
    const u64 offset,
    const u64 byteCount,
    Bytes& bytes,
    TransferChunk&& transferChunk
){
    return ForEachSegmentChunk(
        volumeName,
        operation,
        segmentPaths,
        segmentSize,
        offset,
        byteCount,
        [&](
            const usize segmentIndex,
            const GlobalFilesystemDetail::StreamOffset streamOffset,
            const GlobalFilesystemDetail::StreamSize streamChunkSize,
            const u64 chunkBytes
        ){
            return transferChunk(
                volumeName,
                segmentPaths,
                segmentIndex,
                streamOffset,
                streamChunkSize,
                chunkBytes,
                bytes
            );
        }
    );
}

template<typename SegmentPaths>
static bool ReadSegmentBytes(
    const AStringView volumeName,
    const SegmentPaths& segmentPaths,
    const usize segmentIndex,
    const GlobalFilesystemDetail::StreamOffset streamOffset,
    const GlobalFilesystemDetail::StreamSize streamChunkSize,
    const u64 chunkBytes,
    u8*& outputBytes){
    GlobalFilesystemDetail::InputFileStream stream(
        segmentPaths[segmentIndex],
        GlobalFilesystemDetail::InputFileStream::binary
    );
    return TransferSegmentChunk(
        volumeName,
        segmentPaths,
        segmentIndex,
        streamOffset,
        "readBytes:open",
        "readBytes:seek",
        stream,
        [](auto& stream, const GlobalFilesystemDetail::StreamOffset offset){ stream.seekg(offset); },
        [&](auto& stream){
            stream.read(reinterpret_cast<char*>(outputBytes), streamChunkSize);
            if(stream.gcount() != streamChunkSize){
                NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): readBytes failed on '{}': requested {} bytes, received {} bytes, errno {}")
                    , StringConvert(volumeName)
                    , StringConvert(segmentPaths[segmentIndex].string())
                    , static_cast<i64>(streamChunkSize)
                    , static_cast<i64>(stream.gcount())
                    , StringConvert(LastErrnoMessage())
                );
                return false;
            }

            outputBytes += chunkBytes;
            return true;
        }
    );
}

template<typename SegmentPaths>
static bool WriteSegmentBytes(
    const AStringView volumeName,
    const SegmentPaths& segmentPaths,
    const usize segmentIndex,
    const GlobalFilesystemDetail::StreamOffset streamOffset,
    const GlobalFilesystemDetail::StreamSize streamChunkSize,
    const u64 chunkBytes,
    const u8*& inputBytes){
    GlobalFilesystemDetail::FileStream stream(
        segmentPaths[segmentIndex],
        GlobalFilesystemDetail::FileStream::binary
            | GlobalFilesystemDetail::FileStream::in
            | GlobalFilesystemDetail::FileStream::out
    );
    return TransferSegmentChunk(
        volumeName,
        segmentPaths,
        segmentIndex,
        streamOffset,
        "writeBytes:open",
        "writeBytes:seek",
        stream,
        [](auto& stream, const GlobalFilesystemDetail::StreamOffset offset){ stream.seekp(offset); },
        [&](auto& stream){
            stream.write(reinterpret_cast<const char*>(inputBytes), streamChunkSize);
            if(!stream.good()){
                NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): writeBytes failed on '{}': attempted {} bytes, errno {}")
                    , StringConvert(volumeName)
                    , StringConvert(segmentPaths[segmentIndex].string())
                    , static_cast<i64>(streamChunkSize)
                    , StringConvert(LastErrnoMessage())
                );
                return false;
            }

            inputBytes += chunkBytes;
            return true;
        }
    );
}

struct ReadSegmentBytesOp{
    template<typename SegmentPaths>
    bool operator()(
        const AStringView volumeName,
        const SegmentPaths& segmentPaths,
        const usize segmentIndex,
        const GlobalFilesystemDetail::StreamOffset streamOffset,
        const GlobalFilesystemDetail::StreamSize streamChunkSize,
        const u64 chunkBytes,
        u8*& outputBytes
    )const{
        return ReadSegmentBytes(
            volumeName,
            segmentPaths,
            segmentIndex,
            streamOffset,
            streamChunkSize,
            chunkBytes,
            outputBytes
        );
    }
};

struct WriteSegmentBytesOp{
    template<typename SegmentPaths>
    bool operator()(
        const AStringView volumeName,
        const SegmentPaths& segmentPaths,
        const usize segmentIndex,
        const GlobalFilesystemDetail::StreamOffset streamOffset,
        const GlobalFilesystemDetail::StreamSize streamChunkSize,
        const u64 chunkBytes,
        const u8*& inputBytes
    )const{
        return WriteSegmentBytes(
            volumeName,
            segmentPaths,
            segmentIndex,
            streamOffset,
            streamChunkSize,
            chunkBytes,
            inputBytes
        );
    }
};




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool VolumeFileSystem::createSegmentLocked(const usize segmentIndex){
    if(m_maxSegments != 0 && segmentIndex >= m_maxSegments){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): createSegment failed: segment index {} exceeds maxSegments {}")
            , StringConvert(m_volumeName)
            , segmentIndex
            , m_maxSegments
        );
        return false;
    }
    if(m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "createSegment", "segment size is zero");
        return false;
    }

    const Path path = segmentPath(segmentIndex);
    GlobalFilesystemDetail::OutputFileStream stream(
        path,
        GlobalFilesystemDetail::OutputFileStream::binary | GlobalFilesystemDetail::OutputFileStream::trunc
    );
    if(!stream.is_open()){
        __hidden_filesystem::LogFailureWithPath(
            m_volumeName,
            "createSegment:open",
            path,
            __hidden_filesystem::LastErrnoMessage()
        );
        return false;
    }

    GlobalFilesystemDetail::StreamOffset streamOffset = 0;
    if(!__hidden_filesystem::ToStreamOff(m_segmentSize - 1, streamOffset)){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): createSegment failed: segment size {} cannot be represented as stream offset")
            , StringConvert(m_volumeName)
            , m_segmentSize
        );
        return false;
    }

    stream.seekp(streamOffset);
    if(!stream.good()){
        __hidden_filesystem::LogFailureWithPath(
            m_volumeName,
            "createSegment:seek",
            path,
            __hidden_filesystem::LastErrnoMessage()
        );
        return false;
    }

    char zero = 0;
    stream.write(&zero, 1);
    if(!stream.good()){
        __hidden_filesystem::LogFailureWithPath(
            m_volumeName,
            "createSegment:write",
            path,
            __hidden_filesystem::LastErrnoMessage()
        );
        return false;
    }

    if(segmentIndex == m_segmentPaths.size())
        m_segmentPaths.push_back(path);
    else if(segmentIndex < m_segmentPaths.size())
        m_segmentPaths[segmentIndex] = path;
    else{
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): createSegment failed: segment index {} is non-contiguous (segment count {})")
            , StringConvert(m_volumeName)
            , segmentIndex
            , m_segmentPaths.size()
        );
        return false;
    }

    return true;
}

bool VolumeFileSystem::ensureCapacityLocked(const u64 requiredBytes){
    if(m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "ensureCapacity", "segment size is zero");
        return false;
    }

    for(;;){
        const u64 segmentCount = static_cast<u64>(m_segmentPaths.size());
        if(segmentCount > Limit<u64>::s_Max / m_segmentSize){
            __hidden_filesystem::LogFailure(m_volumeName, "ensureCapacity", "capacity overflow while computing current volume size");
            return false;
        }

        const u64 capacity = segmentCount * m_segmentSize;
        if(requiredBytes <= capacity)
            return true;

        if(!m_writable){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): ensureCapacity failed: required {} bytes, current capacity {} bytes, filesystem is read-only")
                , StringConvert(m_volumeName)
                , requiredBytes
                , capacity
            );
            return false;
        }
        if(!createSegmentLocked(m_segmentPaths.size()))
            return false;
    }
}

bool VolumeFileSystem::computePhysicalCapacityLocked(u64& outCapacityBytes)const{
    outCapacityBytes = 0;

    if(m_segmentPaths.empty() || m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "physicalCapacity", "no mounted segments or segment size is zero");
        return false;
    }

    const usize fullSegmentCount = m_segmentPaths.size() - 1u;
    if(static_cast<u64>(fullSegmentCount) > Limit<u64>::s_Max / m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "physicalCapacity", "segment capacity overflow");
        return false;
    }

    ErrorCode errorCode;
    const u64 lastSegmentBytes = FileSize(m_segmentPaths.back(), errorCode);
    if(errorCode){
        __hidden_filesystem::LogFailureWithFsError(m_volumeName, "physicalCapacity:file_size", m_segmentPaths.back(), errorCode);
        return false;
    }
    if(lastSegmentBytes == 0 || lastSegmentBytes > m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "physicalCapacity", "final segment size is outside logical bounds");
        return false;
    }

    const u64 fullSegmentBytes = static_cast<u64>(fullSegmentCount) * m_segmentSize;
    if(__hidden_filesystem::AddNoOverflow(fullSegmentBytes, lastSegmentBytes, outCapacityBytes))
        return true;

    __hidden_filesystem::LogFailure(m_volumeName, "physicalCapacity", "capacity overflow while adding final segment");
    return false;
}

bool VolumeFileSystem::readBytesLocked(const u64 offset, void* data, const u64 byteCount)const{
    if(byteCount == 0)
        return true;
    if(data == nullptr){
        __hidden_filesystem::LogFailure(m_volumeName, "readBytes", "invalid arguments");
        return false;
    }

    u8* outputBytes = static_cast<u8*>(data);
    return __hidden_filesystem::TransferVolumeBytes(
        m_volumeName,
        "readBytes",
        m_segmentPaths,
        m_segmentSize,
        offset,
        byteCount,
        outputBytes,
        __hidden_filesystem::ReadSegmentBytesOp{}
    );
}

bool VolumeFileSystem::writeBytesLocked(const u64 offset, const void* data, const u64 byteCount){
    if(byteCount == 0)
        return true;
    if(data == nullptr){
        __hidden_filesystem::LogFailure(m_volumeName, "writeBytes", "invalid arguments");
        return false;
    }

    u64 endOffset = 0;
    if(!__hidden_filesystem::AddNoOverflow(offset, byteCount, endOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "writeBytes", "offset overflow");
        return false;
    }
    if(!ensureCapacityLocked(endOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "writeBytes", "insufficient capacity");
        return false;
    }

    const u8* inputBytes = static_cast<const u8*>(data);
    return __hidden_filesystem::TransferVolumeBytes(
        m_volumeName,
        "writeBytes",
        m_segmentPaths,
        m_segmentSize,
        offset,
        byteCount,
        inputBytes,
        __hidden_filesystem::WriteSegmentBytesOp{}
    );
}

bool VolumeFileSystem::moveBytesLocked(const u64 destinationOffset, const u64 sourceOffset, const u64 byteCount){
    if(byteCount == 0 || destinationOffset == sourceOffset)
        return true;
    if(m_segmentSize == 0){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "segment size is zero");
        return false;
    }

    u64 sourceEndOffset = 0;
    if(!__hidden_filesystem::AddNoOverflow(sourceOffset, byteCount, sourceEndOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "source range overflow");
        return false;
    }

    if(static_cast<u64>(m_segmentPaths.size()) > Limit<u64>::s_Max / m_segmentSize){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "capacity overflow");
        return false;
    }
    const u64 capacityBytes = static_cast<u64>(m_segmentPaths.size()) * m_segmentSize;
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
    if(!__hidden_filesystem::AddNoOverflow(destinationOffset, byteCount, destinationEndOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "destination range overflow");
        return false;
    }
    if(!ensureCapacityLocked(destinationEndOffset)){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "failed to ensure destination capacity");
        return false;
    }

    const u64 moveChunkBytes = Min(__hidden_filesystem::s_VolumeMoveChunkBytes, m_segmentSize);
    if(moveChunkBytes == 0 || moveChunkBytes > static_cast<u64>(Limit<usize>::s_Max)){
        __hidden_filesystem::LogFailure(m_volumeName, "moveBytes", "invalid move chunk size");
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
        __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "filesystem is not writable or segment size is zero");
        return false;
    }
    if(m_segmentPaths.empty()){
        __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "no segments are mounted");
        return false;
    }

    const u64 requiredBytes = Max(m_nextFreeOffset, m_metadataBytes);
    u64 requiredSegments = DivideUp(requiredBytes, m_segmentSize);
    if(requiredSegments == 0)
        requiredSegments = 1;
    if(requiredSegments > static_cast<u64>(m_segmentPaths.size())){
        __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "required segment count exceeds mounted segment count");
        return false;
    }
    if(requiredSegments > static_cast<u64>(Limit<usize>::s_Max)){
        __hidden_filesystem::LogFailure(m_volumeName, "trimSegments", "required segment count exceeds usize range");
        return false;
    }

    while(m_segmentPaths.size() > static_cast<usize>(requiredSegments)){
        const Path removePath = m_segmentPaths.back();
        if(!RemoveFile(removePath, errorCode)){
            if(errorCode){
                __hidden_filesystem::LogFailureWithFsError(m_volumeName, "trimSegments:remove", removePath, errorCode);
            }
            else{
                __hidden_filesystem::LogFailureWithPath(m_volumeName, "trimSegments:remove", removePath, "segment was not present");
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
        __hidden_filesystem::LogFailureWithFsError(m_volumeName, "trimSegments:file_size", lastSegmentPath, errorCode);
        return false;
    }
    if(currentLastSegmentBytes == requiredLastSegmentBytes)
        return true;

    if(!__hidden_filesystem::ResizeFile(lastSegmentPath, requiredLastSegmentBytes, errorCode)){
        __hidden_filesystem::LogFailureWithFsError(m_volumeName, "trimSegments:resize", lastSegmentPath, errorCode);
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
