// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"
#include "../atomic.h"
#include "../diagnostics.h"
#include "../numeric_text.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SizedFileCreateFailure{
    ErrorCode code;
    b8 created;
};

template<typename ArenaT>
[[nodiscard]] inline Expected<void, SizedFileCreateFailure> CreateSizedFileExclusive(const Path<ArenaT>& path, const u64 size)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    if(size > static_cast<u64>(Limit<LONGLONG>::s_Max))
        return MakeUnexpected(SizedFileCreateFailure{ MakeErrorCode(ErrorCondition::value_too_large), false });
    const HANDLE handle = CreateFile(path.c_str(), GENERIC_WRITE, 0u, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(handle == INVALID_HANDLE_VALUE)
        return MakeUnexpected(SizedFileCreateFailure{ LastSystemError(), false });
    ErrorCode error;
    LARGE_INTEGER end{};
    end.QuadPart = static_cast<LONGLONG>(size);
    if(!SetFilePointerEx(handle, end, nullptr, FILE_BEGIN) || !SetEndOfFile(handle))
        error = LastSystemError();
    if(!CloseHandle(handle) && !error)
        error = LastSystemError();
#else
    if(size > static_cast<u64>(Limit<off_t>::s_Max))
        return MakeUnexpected(SizedFileCreateFailure{ MakeErrorCode(ErrorCondition::value_too_large), false });
    constexpr u32 s_NewSegmentMode = 0666u;
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, s_NewSegmentMode);
    if(descriptor < 0)
        return MakeUnexpected(SizedFileCreateFailure{ LastSystemError(), false });
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
        return MakeUnexpected(SizedFileCreateFailure{ error, true });
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<u64, ErrorCode> FileSize(const Path<ArenaT>& path)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if(!GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &data))
        return MakeUnexpected(LastSystemError());
    return (static_cast<u64>(data.nFileSizeHigh) << GlobalFilesystemDetail::s_FileSizeHighPartShiftBits) | static_cast<u64>(data.nFileSizeLow);
#else
    struct stat pathStat = {};
    if(stat(path.c_str(), &pathStat) != 0)
        return MakeUnexpected(LastSystemError());
    return static_cast<u64>(pathStat.st_size);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> ResolveAbsoluteCandidate(const Path<ArenaT>& baseDirectory, Path<ArenaT>& candidate){
    if(!candidate.isAbsolute())
        candidate = baseDirectory / candidate;
    const auto absolutePath = AbsolutePath(candidate);
    if(!absolutePath)
        return MakeUnexpected(absolutePath.error());
    return LexicallyNormal(*absolutePath);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> ResolveAbsolutePath(ArenaT& arena, const Path<ArenaT>& baseDirectory, const Path<ArenaT>& relativeOrAbsolute){
    if(relativeOrAbsolute.empty())
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    Path<ArenaT> candidate(arena, relativeOrAbsolute);
    return GlobalFilesystemDetail::ResolveAbsoluteCandidate(baseDirectory, candidate);
}

template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> ResolveAbsolutePath(ArenaT& arena, const Path<ArenaT>& baseDirectory, const AStringView relativeOrAbsolute){
    if(relativeOrAbsolute.empty())
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    Path<ArenaT> candidate(arena, relativeOrAbsolute);
    return GlobalFilesystemDetail::ResolveAbsoluteCandidate(baseDirectory, candidate);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container>
[[nodiscard]] inline char* MutableReadBuffer(Container& outData)noexcept(noexcept(reinterpret_cast<char*>(outData.data()))){
    return reinterpret_cast<char*>(outData.data());
}

template<typename ArenaT, typename Container>
[[nodiscard]] inline Expected<void, ErrorCode> ReadWholeBinaryFile(const Path<ArenaT>& path, Container& inOutData){
    inOutData.clear();
    InputFileStream stream(path, InputFileStream::binary | InputFileStream::ate);
    if(!stream.is_open()){
        const auto size = FileSize(path);
        if(!size)
            return MakeUnexpected(size.error());
        return MakeUnexpected(MakeErrorCode(ErrorCondition::io_error));
    }
    const StreamOffset fileSizeAtEnd = stream.tellg();
    if(fileSizeAtEnd < 0)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::io_error));
    const u64 fileSize = static_cast<u64>(fileSizeAtEnd);
    if(fileSize > static_cast<u64>(Limit<usize>::s_Max) || !CanRepresentStreamSize(fileSize))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
    stream.seekg(0, InputFileStream::beg);
    if(!stream.good())
        return MakeUnexpected(MakeErrorCode(ErrorCondition::io_error));
    inOutData.resize(static_cast<usize>(fileSize));
    if(fileSize == 0u)
        return {};
    stream.read(MutableReadBuffer(inOutData), static_cast<StreamSize>(fileSize));
    if(stream.good() || (stream.eof() && stream.gcount() == static_cast<StreamSize>(fileSize)))
        return {};
    inOutData.clear();
    return MakeUnexpected(MakeErrorCode(ErrorCondition::io_error));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT, typename StringT>
[[nodiscard]] inline Expected<void, ErrorCode> ReadTextFile(const Path<ArenaT>& path, StringT& inOutText) requires requires(StringT& text, usize size){ text.resize(size); text.data(); text.clear(); }
{
    return GlobalFilesystemDetail::ReadWholeBinaryFile(path, inOutText);
}

template<typename ArenaT>
[[nodiscard]] inline b8 WriteTextFile(const Path<ArenaT>& path, const AStringView content){
    if(!CanRepresentStreamSize(static_cast<u64>(content.size())))
        return false;

    OutputFileStream stream(path, OutputFileStream::binary | OutputFileStream::trunc);
    if(!stream.is_open())
        return false;

    if(!content.empty())
        stream.write(content.data(), static_cast<StreamSize>(content.size()));
    stream.close();
    return stream.good();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 ReadStreamExact(InputFileStream& stream, const NotNull<void*> destination, const usize byteCount){
    if(!CanRepresentStreamSize(static_cast<u64>(byteCount)))
        return false;
    stream.read(reinterpret_cast<char*>(destination.get()), static_cast<StreamSize>(byteCount));
    return stream.gcount() == static_cast<StreamSize>(byteCount) && !stream.bad();
}

[[nodiscard]] inline b8 VerifyStreamAtEnd(InputFileStream& stream){
    char extraByte = 0;
    stream.read(&extraByte, 1);
    return stream.gcount() == 0 && stream.eof() && !stream.bad();
}

template<typename ArenaT>
[[nodiscard]] inline b8 VerifyBinaryFileBytes(const Path<ArenaT>& path, const void* const expected, const usize count){
    InputFileStream stream(path, InputFileStream::binary);
    if(!stream.is_open())
        return false;
    constexpr usize s_VerifyChunkSize = 4096u;
    Array<u8, s_VerifyChunkSize> buffer = {};
    const u8* const expectedBytes = static_cast<const u8*>(expected);
    for(usize offset = 0u; offset < count;){
        const usize remaining = count - offset;
        const usize chunk = remaining < buffer.size() ? remaining : buffer.size();
        if(!ReadStreamExact(stream, NotNull<void*>(buffer.data()), chunk) || FOUNDATION_MEMCMP(buffer.data(), expectedBytes + offset, chunk) != 0)
            return false;
        offset += chunk;
    }
    return VerifyStreamAtEnd(stream);
}

template<typename ArenaT, typename Container>
[[nodiscard]] inline Expected<void, ErrorCode> ReadBinaryFile(const Path<ArenaT>& path, Container& inOutBytes){
    return GlobalFilesystemDetail::ReadWholeBinaryFile(path, inOutBytes);
}

template<typename ArenaT, typename Container>
[[nodiscard]] inline b8 WriteBinaryFile(const Path<ArenaT>& path, const Container& bytes){
    if(!CanRepresentStreamSize(static_cast<u64>(bytes.size())))
        return false;

    OutputFileStream stream(path, OutputFileStream::binary | OutputFileStream::trunc);
    if(!stream.is_open())
        return false;

    if(!bytes.empty()){
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<StreamSize>(bytes.size()));
    }
    stream.close();
    return stream.good();
}

template<typename ArenaT>
[[nodiscard]] inline Expected<void, ErrorCode> ResizeFile(const Path<ArenaT>& path, const u64 byteCount)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    if(byteCount > static_cast<u64>(Limit<LONGLONG>::s_Max))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
    const HANDLE file = CreateFile(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(file == INVALID_HANDLE_VALUE)
        return MakeUnexpected(LastSystemError());

    LARGE_INTEGER offset{};
    offset.QuadPart = static_cast<LONGLONG>(byteCount);
    const b8 seekSucceeded = SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != 0;
    const b8 resizeSucceeded = seekSucceeded && SetEndOfFile(file) != 0;
    const ErrorCode errorCode = resizeSucceeded ? ErrorCode{} : LastSystemError();
    const b8 closeSucceeded = CloseHandle(file) != 0;
    const ErrorCode closeError = closeSucceeded ? ErrorCode{} : LastSystemError();
    if(errorCode)
        return MakeUnexpected(errorCode);
    if(closeError)
        return MakeUnexpected(closeError);
    return {};
#else
    if(!::CanRepresentU64<off_t>(byteCount))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
    if(::truncate(path.c_str(), static_cast<off_t>(byteCount)) != 0)
        return MakeUnexpected(LastSystemError());
    return {};
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

