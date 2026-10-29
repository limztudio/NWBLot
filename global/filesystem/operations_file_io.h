// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<u64, ErrorCode> FileSize(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
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


template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> ResolveAbsolutePath(ArenaT& arena, const Path<ArenaT>& baseDirectory, const AStringView relativeOrAbsolute){
    if(relativeOrAbsolute.empty())
        return MakeUnexpected(std::make_error_code(std::errc::invalid_argument));
    Path<ArenaT> candidate(arena, relativeOrAbsolute);
    if(!candidate.isAbsolute())
        candidate = baseDirectory / candidate;
    const auto absolutePath = AbsolutePath(candidate);
    if(!absolutePath)
        return MakeUnexpected(absolutePath.error());
    return LexicallyNormal(*absolutePath);
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
        return MakeUnexpected(std::make_error_code(std::errc::io_error));
    }
    const StreamOffset fileSizeAtEnd = stream.tellg();
    if(fileSizeAtEnd < 0)
        return MakeUnexpected(std::make_error_code(std::errc::io_error));
    const u64 fileSize = static_cast<u64>(fileSizeAtEnd);
    if(fileSize > static_cast<u64>(Limit<usize>::s_Max) || !CanRepresentStreamSize(fileSize))
        return MakeUnexpected(std::make_error_code(std::errc::value_too_large));
    stream.seekg(0, InputFileStream::beg);
    if(!stream.good())
        return MakeUnexpected(std::make_error_code(std::errc::io_error));
    inOutData.resize(static_cast<usize>(fileSize));
    if(fileSize == 0u)
        return {};
    stream.read(MutableReadBuffer(inOutData), static_cast<StreamSize>(fileSize));
    if(stream.good() || (stream.eof() && stream.gcount() == static_cast<StreamSize>(fileSize)))
        return {};
    inOutData.clear();
    return MakeUnexpected(std::make_error_code(std::errc::io_error));
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
    u8 buffer[s_VerifyChunkSize];
    const u8* const expectedBytes = static_cast<const u8*>(expected);
    for(usize offset = 0u; offset < count;){
        const usize remaining = count - offset;
        const usize chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if(!ReadStreamExact(stream, NotNull<void*>(buffer), chunk) || NWB_MEMCMP(buffer, expectedBytes + offset, chunk) != 0)
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
[[nodiscard]] inline Expected<void, ErrorCode> StageNewBinaryFileDurable(const Path<ArenaT>& path, const void* const data, const usize count, b8& outCreated)noexcept{
    outCreated = false;
#if defined(NWB_PLATFORM_WINDOWS)
    if(static_cast<u64>(count) > static_cast<u64>(Limit<i64>::s_Max))
        return MakeUnexpected(std::make_error_code(std::errc::value_too_large));
    const HANDLE handle = CreateFile(path.c_str(), GENERIC_WRITE, 0u, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(handle == INVALID_HANDLE_VALUE)
        return MakeUnexpected(LastSystemError());
    outCreated = true;
    ErrorCode error;
    const u8* cursor = static_cast<const u8*>(data);
    usize remaining = count;
    while(remaining > 0u){
        const DWORD requestSize = remaining > static_cast<usize>(Limit<DWORD>::s_Max) ? Limit<DWORD>::s_Max : static_cast<DWORD>(remaining);
        DWORD transferred = 0u;
        if(!WriteFile(handle, cursor, requestSize, &transferred, nullptr)){
            error = LastSystemError();
            break;
        }
        if(transferred == 0u){
            error = std::make_error_code(std::errc::io_error);
            break;
        }
        cursor += transferred;
        remaining -= transferred;
    }
    if(!error && !FlushFileBuffers(handle))
        error = LastSystemError();
    if(!CloseHandle(handle) && !error)
        error = LastSystemError();
#else
    if(static_cast<u64>(count) > static_cast<u64>(Limit<off_t>::s_Max))
        return MakeUnexpected(std::make_error_code(std::errc::value_too_large));
    constexpr u32 s_NewFileMode = 0666u;
    const int descriptor = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, s_NewFileMode);
    if(descriptor < 0)
        return MakeUnexpected(LastSystemError());
    outCreated = true;
    ErrorCode error;
    const u8* cursor = static_cast<const u8*>(data);
    usize remaining = count;
    while(remaining > 0u){
        const usize requestSize = remaining > static_cast<usize>(Limit<ssize_t>::s_Max) ? static_cast<usize>(Limit<ssize_t>::s_Max) : remaining;
        const ssize_t transferred = write(descriptor, cursor, requestSize);
        if(transferred < 0){
            if(errno == EINTR)
                continue;
            error = LastSystemError();
            break;
        }
        if(transferred == 0){
            error = std::make_error_code(std::errc::io_error);
            break;
        }
        cursor += static_cast<usize>(transferred);
        remaining -= static_cast<usize>(transferred);
    }
    if(!error && fsync(descriptor) != 0)
        error = LastSystemError();
    if(close(descriptor) != 0 && !error)
        error = LastSystemError();
#endif
    if(error)
        return MakeUnexpected(error);
    return {};
}

template<typename ArenaT>
[[nodiscard]] inline Expected<void, ErrorCode> ResizeFile(const Path<ArenaT>& path, const u64 byteCount)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(byteCount > static_cast<u64>(Limit<LONGLONG>::s_Max))
        return MakeUnexpected(std::make_error_code(std::errc::value_too_large));
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
        return MakeUnexpected(std::make_error_code(std::errc::value_too_large));
    if(::truncate(path.c_str(), static_cast<off_t>(byteCount)) != 0)
        return MakeUnexpected(LastSystemError());
    return {};
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

