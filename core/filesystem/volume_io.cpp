// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_file_system.h"
#include "volume_storage_detail.h"
#include "arena_names.h"

#include <global/simplemath.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FilesystemVolumeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr StringView s_NoMountLabel = "none";
static constexpr StringView s_UnknownMountLabel = "unknown";
constexpr usize s_ErrnoMessageBufferBytes = 256u;

struct SegmentCreateFailure{
    ErrorCode code;
    b8 created;
};

static Expected<void, SegmentCreateFailure> CreateNewSegment(const Path& path, const u64 size)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(size > static_cast<u64>(Limit<LONGLONG>::s_Max))
        return MakeUnexpected(SegmentCreateFailure{ MakeErrorCode(ErrorCondition::value_too_large), false });
    const HANDLE handle = CreateFile(path.c_str(), GENERIC_WRITE, 0u, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(handle == INVALID_HANDLE_VALUE)
        return MakeUnexpected(SegmentCreateFailure{ LastSystemError(), false });
    ErrorCode error;
    LARGE_INTEGER end{};
    end.QuadPart = static_cast<LONGLONG>(size);
    if(!SetFilePointerEx(handle, end, nullptr, FILE_BEGIN) || !SetEndOfFile(handle))
        error = LastSystemError();
    if(!CloseHandle(handle) && !error)
        error = LastSystemError();
#else
    if(size > static_cast<u64>(Limit<off_t>::s_Max))
        return MakeUnexpected(SegmentCreateFailure{ MakeErrorCode(ErrorCondition::value_too_large), false });
    constexpr u32 s_NewSegmentMode = 0666u;
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, s_NewSegmentMode);
    if(descriptor < 0)
        return MakeUnexpected(SegmentCreateFailure{ LastSystemError(), false });
    ErrorCode error;
    while(ftruncate(descriptor, static_cast<off_t>(size)) != 0){
        if(errno != EINTR){
            error = LastSystemError();
            break;
        }
    }
    if(close(descriptor) != 0 && !error)
        error = LastSystemError();
#endif
    if(error)
        return MakeUnexpected(SegmentCreateFailure{ error, true });
    return {};
}

static Expected<void, ErrorCode> GrowLastSegment(const Path& path, const u64 size, const u64 minimumBytes)noexcept{
    if(minimumBytes > size)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::io_error));
#if defined(NWB_PLATFORM_WINDOWS)
    if(size > static_cast<u64>(Limit<LONGLONG>::s_Max))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
    const HANDLE handle = CreateFile(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(handle == INVALID_HANDLE_VALUE)
        return MakeUnexpected(LastSystemError());
    ErrorCode error;
    LARGE_INTEGER current{};
    if(!GetFileSizeEx(handle, &current))
        error = LastSystemError();
    else if(current.QuadPart <= 0 || static_cast<u64>(current.QuadPart) < minimumBytes || static_cast<u64>(current.QuadPart) > size)
        error = MakeErrorCode(ErrorCondition::io_error);
    else if(static_cast<u64>(current.QuadPart) < size){
        LARGE_INTEGER end{};
        end.QuadPart = static_cast<LONGLONG>(size);
        if(!SetFilePointerEx(handle, end, nullptr, FILE_BEGIN) || !SetEndOfFile(handle))
            error = LastSystemError();
    }
    if(!CloseHandle(handle) && !error)
        error = LastSystemError();
#else
    if(size > static_cast<u64>(Limit<off_t>::s_Max))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
    const int descriptor = open(path.c_str(), O_RDWR | O_CLOEXEC);
    if(descriptor < 0)
        return MakeUnexpected(LastSystemError());
    ErrorCode error;
    struct stat status{};
    if(fstat(descriptor, &status) != 0)
        error = LastSystemError();
    else if(!S_ISREG(status.st_mode) || status.st_size <= 0 || static_cast<u64>(status.st_size) < minimumBytes || static_cast<u64>(status.st_size) > size)
        error = MakeErrorCode(ErrorCondition::io_error);
    else if(static_cast<u64>(status.st_size) < size){
        while(ftruncate(descriptor, static_cast<off_t>(size)) != 0){
            if(errno != EINTR){
                error = LastSystemError();
                break;
            }
        }
    }
    if(close(descriptor) != 0 && !error)
        error = LastSystemError();
#endif
    if(error)
        return MakeUnexpected(error);
    return {};
}


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

ACompactString LastErrnoMessage()noexcept{
    const i32 errorNumber = errno;
    if(errorNumber == 0)
        return ACompactString(s_NoMountLabel);

    Array<char, s_ErrnoMessageBufferBytes> errorText{};
    if(NWB_STRERROR(errorText.data(), errorText.size(), errorNumber) != 0)
        return ACompactString(s_UnknownMountLabel);

    ACompactString output(errorText.data());
    output += " (";
    Array<char, s_DecimalTextBufferBytes> numberText{};
    output += FormatDecimal(errorNumber, numberText);
    output += ")";
    return output;
}

void LogFailure(AStringView volumeName, AStringView operation, AStringView detail)noexcept(!NWB_OCCUR_WARNING){
    NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed: {}"), ConvertString(volumeName), ConvertString(operation), ConvertString(detail));
}

void LogFailureWithPath(AStringView volumeName, AStringView operation, const Path& path, AStringView detail)noexcept(!NWB_OCCUR_WARNING){
    NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed on '{}': {}"), ConvertString(volumeName), ConvertString(operation), ConvertString(path.native()), ConvertString(detail));
}

void LogFailureWithFsError(AStringView volumeName, AStringView operation, const Path& path, const ErrorCode& errorCode)noexcept(!NWB_OCCUR_WARNING){
    NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): {} failed on '{}': [{}] {}"), ConvertString(volumeName), ConvertString(operation), ConvertString(path.native()), errorCode.value(), ConvertString(errorCode.message()));
}

Expected<VolumeHeaderDisk> ReadVolumeHeaderFromSegment(const AStringView volumeName, const Path& segmentPath){
    VolumeHeaderDisk header;

    InputFileStream stream(segmentPath, InputFileStream::binary);
    if(!stream.is_open()){
        LogFailureWithPath(volumeName, FilesystemVolumeDetail::s_VolumeOpMountOpenHeader, segmentPath, LastErrnoMessage());
        return MakeUnexpected(Failure{});
    }

    stream.read(reinterpret_cast<char*>(&header), static_cast<StreamSize>(sizeof(header)));
    if(stream.good())
        return header;
    if(stream.eof() && stream.gcount() == static_cast<StreamSize>(sizeof(header)))
        return header;

    LogFailureWithPath(volumeName, FilesystemVolumeDetail::s_VolumeOpMountReadHeader, segmentPath, LastErrnoMessage());
    return MakeUnexpected(Failure{});
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 VolumeFileSystem::createSegmentLocked(const usize segmentIndex){
    if(m_maxSegments != 0 && segmentIndex >= m_maxSegments){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): createSegment failed: segment index {} exceeds maxSegments {}"), ConvertString(m_volumeName), segmentIndex, m_maxSegments);
        return false;
    }
    if(m_segmentSize == 0){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpCreateSegment, FilesystemVolumeDetail::s_VolumeDetailSegmentSizeZero);
        return false;
    }

    if(segmentIndex != m_segmentPaths.size() || segmentIndex == Limit<usize>::s_Max){
        NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): createSegment failed: segment index {} is non-contiguous (segment count {})"), ConvertString(m_volumeName), segmentIndex, m_segmentPaths.size());
        return false;
    }
    m_segmentPaths.reserve(segmentIndex + 1u);
    Path path = segmentPath(segmentIndex);
    const auto created = FilesystemVolumeDetail::CreateNewSegment(path, m_segmentSize);
    if(!created){
        NWB_LOGGER_ERROR(NWB_TEXT("Filesystem('{}'): failed to create segment '{}': {}{}"), ConvertString(m_volumeName), PathToString<tchar>(path), ConvertString(created.error().code.message()), created.error().created ? NWB_TEXT("; partial segment retained") : NWB_TEXT(""));
        return false;
    }
    m_segmentPaths.push_back(Move(path));
    return true;
}

b8 VolumeFileSystem::ensureCapacityLocked(const u64 requiredBytes){
    if(m_segmentSize == 0){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpEnsureCapacity, FilesystemVolumeDetail::s_VolumeDetailSegmentSizeZero);
        return false;
    }

    for(;;){
        const auto capacityResult = computeLogicalCapacityLocked();
        if(!capacityResult){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpEnsureCapacity, "capacity overflow while computing current volume size");
            return false;
        }
        const u64 capacity = *capacityResult;

        if(requiredBytes <= capacity)
            return true;

        if(!m_writable){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): ensureCapacity failed: required {} bytes, current capacity {} bytes, filesystem is read-only"), ConvertString(m_volumeName), requiredBytes, capacity);
            return false;
        }
        if(m_maxSegments != 0u && m_segmentPaths.size() >= m_maxSegments){
            NWB_LOGGER_WARNING(NWB_TEXT("Filesystem('{}'): ensureCapacity failed: maxSegments {} reached"), ConvertString(m_volumeName), m_maxSegments);
            return false;
        }
        if(m_segmentPaths.empty()){
            FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpEnsureCapacity, FilesystemVolumeDetail::s_VolumeDetailNoSegmentsMounted);
            return false;
        }
        const Path& previousLast = m_segmentPaths.back();
        const u64 previousSegmentsCapacity = capacity - m_segmentSize;
        const u64 minimumLastBytes = m_nextFreeOffset > previousSegmentsCapacity ? m_nextFreeOffset - previousSegmentsCapacity : 0u;
        const auto grown = FilesystemVolumeDetail::GrowLastSegment(previousLast, m_segmentSize, minimumLastBytes);
        if(!grown){
            FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpEnsureCapacity, previousLast, grown.error());
            return false;
        }
        if(!createSegmentLocked(m_segmentPaths.size()))
            return false;
    }
}

Expected<u64> VolumeFileSystem::computeLogicalCapacityLocked()const noexcept{
    if(m_segmentSize == 0)
        return MakeUnexpected(Failure{});

    const u64 segmentCount = static_cast<u64>(m_segmentPaths.size());
    if(segmentCount > Limit<u64>::s_Max / m_segmentSize)
        return MakeUnexpected(Failure{});

    return segmentCount * m_segmentSize;
}

Expected<u64, ErrorCode> VolumeFileSystem::computePhysicalCapacityLocked()const noexcept(!NWB_OCCUR_WARNING){
    if(m_segmentPaths.empty() || m_segmentSize == 0){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpPhysicalCapacity, FilesystemVolumeDetail::s_VolumeDetailNoMountedSegmentsOrSizeZero);
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    }

    const usize fullSegmentCount = m_segmentPaths.size() - 1u;
    if(static_cast<u64>(fullSegmentCount) > Limit<u64>::s_Max / m_segmentSize){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpPhysicalCapacity, FilesystemVolumeDetail::s_VolumeDetailSegmentCapacityOverflow);
        return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
    }

    const auto lastSegmentBytes = FileSize(m_segmentPaths.back());
    if(!lastSegmentBytes){
        const ErrorCode& errorCode = lastSegmentBytes.error();
        FilesystemVolumeDetail::LogFailureWithFsError(m_volumeName, FilesystemVolumeDetail::s_VolumeOpPhysicalCapacityFileSize, m_segmentPaths.back(), errorCode);
        return MakeUnexpected(errorCode);
    }
    if(*lastSegmentBytes == 0 || *lastSegmentBytes > m_segmentSize){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpPhysicalCapacity, "final segment size is outside logical bounds");
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    }

    const u64 fullSegmentBytes = static_cast<u64>(fullSegmentCount) * m_segmentSize;
    const auto capacityBytes = ::AddNoOverflow(fullSegmentBytes, *lastSegmentBytes);
    if(capacityBytes)
        return *capacityBytes;

    FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpPhysicalCapacity, "capacity overflow while adding final segment");
    return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
}

b8 VolumeFileSystem::readBytesLocked(const u64 offset, void* data, const u64 byteCount)const{
    if(byteCount == 0)
        return true;
    if(data == nullptr){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpReadBytes, FilesystemVolumeDetail::s_VolumeDetailInvalidArguments);
        return false;
    }

    u8* outputBytes = static_cast<u8*>(data);
    return FilesystemVolumeDetail::TransferVolumeBytes(m_volumeName, FilesystemVolumeDetail::s_VolumeOpReadBytes, m_segmentPaths, m_segmentSize, offset, byteCount, FilesystemVolumeDetail::ReadSegmentBytesOp{}, outputBytes);
}

b8 VolumeFileSystem::writeBytesLocked(const u64 offset, const void* data, const u64 byteCount){
    if(byteCount == 0)
        return true;
    if(data == nullptr){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteBytes, FilesystemVolumeDetail::s_VolumeDetailInvalidArguments);
        return false;
    }

    u64 endOffset = 0;
    const auto endOffsetResult = ::AddNoOverflow(offset, byteCount);
    if(!endOffsetResult){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteBytes, FilesystemVolumeDetail::s_VolumeDetailOffsetOverflow);
        return false;
    }
    endOffset = *endOffsetResult;
    if(!ensureCapacityLocked(endOffset)){
        FilesystemVolumeDetail::LogFailure(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteBytes, "insufficient capacity");
        return false;
    }

    const u8* inputBytes = static_cast<const u8*>(data);
    return FilesystemVolumeDetail::TransferVolumeBytes(m_volumeName, FilesystemVolumeDetail::s_VolumeOpWriteBytes, m_segmentPaths, m_segmentSize, offset, byteCount, FilesystemVolumeDetail::WriteSegmentBytesOp{}, inputBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FILESYSTEM_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

