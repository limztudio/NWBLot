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
        return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
    return (static_cast<u64>(data.nFileSizeHigh) << GlobalFilesystemDetail::s_FileSizeHighPartShiftBits) | static_cast<u64>(data.nFileSizeLow);
#else
    struct stat pathStat = {};
    if(stat(path.c_str(), &pathStat) != 0)
        return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
    return static_cast<u64>(pathStat.st_size);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> ResolveAbsolutePath(
    ArenaT& arena,
    const Path<ArenaT>& baseDirectory,
    const AStringView relativeOrAbsolute
){
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
[[nodiscard]] inline char* MutableReadBuffer(Container& outData)noexcept{
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
[[nodiscard]] inline Expected<void, ErrorCode> ReadTextFile(const Path<ArenaT>& path, StringT& inOutText)
    requires requires(StringT& text, usize size){ text.resize(size); text.data(); text.clear(); }
{
    return GlobalFilesystemDetail::ReadWholeBinaryFile(path, inOutText);
}

template<typename ArenaT>
[[nodiscard]] inline bool WriteTextFile(const Path<ArenaT>& path, const AStringView content){
    if(!GlobalFilesystemDetail::CanRepresentStreamSize(static_cast<u64>(content.size())))
        return false;

    GlobalFilesystemDetail::OutputFileStream stream(
        path,
        GlobalFilesystemDetail::OutputFileStream::binary | GlobalFilesystemDetail::OutputFileStream::trunc
    );
    if(!stream.is_open())
        return false;

    if(!content.empty())
        stream.write(content.data(), static_cast<GlobalFilesystemDetail::StreamSize>(content.size()));
    stream.close();
    return stream.good();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT, typename Container>
[[nodiscard]] inline Expected<void, ErrorCode> ReadBinaryFile(const Path<ArenaT>& path, Container& inOutBytes){
    return GlobalFilesystemDetail::ReadWholeBinaryFile(path, inOutBytes);
}

template<typename ArenaT, typename Container>
[[nodiscard]] inline bool WriteBinaryFile(const Path<ArenaT>& path, const Container& bytes){
    if(!GlobalFilesystemDetail::CanRepresentStreamSize(static_cast<u64>(bytes.size())))
        return false;

    GlobalFilesystemDetail::OutputFileStream stream(
        path,
        GlobalFilesystemDetail::OutputFileStream::binary | GlobalFilesystemDetail::OutputFileStream::trunc
    );
    if(!stream.is_open())
        return false;

    if(!bytes.empty()){
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<GlobalFilesystemDetail::StreamSize>(bytes.size())
        );
    }
    stream.close();
    return stream.good();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

