// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"


template<typename ArenaT>
[[nodiscard]] inline u64 FileSize(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if(!GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &data)){
        GlobalFilesystemDetail::SetLastSystemError(outError);
        return 0u;
    }

    GlobalFilesystemDetail::ClearError(outError);
    return (static_cast<u64>(data.nFileSizeHigh) << GlobalFilesystemDetail::s_FileSizeHighPartShiftBits) | static_cast<u64>(data.nFileSizeLow);
#else
    struct stat pathStat;
    if(stat(path.c_str(), &pathStat) != 0){
        GlobalFilesystemDetail::SetLastSystemError(outError);
        return 0u;
    }

    GlobalFilesystemDetail::ClearError(outError);
    return static_cast<u64>(pathStat.st_size);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool ResolveAbsolutePath(
    const Path<ArenaT>& baseDirectory,
    const AStringView relativeOrAbsolute,
    Path<ArenaT>& outPath,
    ErrorCode& outError
){
    if(relativeOrAbsolute.empty())
        return false;

    Path<ArenaT> candidate(outPath.arena(), relativeOrAbsolute);
    if(!candidate.is_absolute())
        candidate = baseDirectory / candidate;

    const Path<ArenaT> absolutePath = AbsolutePath(candidate, outError);
    if(outError)
        return false;

    outPath = LexicallyNormal(absolutePath);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container>
[[nodiscard]] inline char* MutableReadBuffer(Container& outData)noexcept{
    return reinterpret_cast<char*>(outData.data());
}

template<typename ArenaT, typename Container>
[[nodiscard]] inline bool ReadWholeBinaryFile(const Path<ArenaT>& path, Container& outData, ErrorCode& outError){
    outData.clear();

    const u64 fileSize = FileSize(path, outError);
    if(outError)
        return false;

    if(fileSize > static_cast<u64>(Limit<usize>::s_Max))
        return false;
    if(!CanRepresentStreamSize(fileSize))
        return false;

    InputFileStream stream(path, InputFileStream::binary);
    if(!stream.is_open())
        return false;

    outData.resize(static_cast<usize>(fileSize));
    if(fileSize == 0)
        return true;

    stream.read(MutableReadBuffer(outData), static_cast<StreamSize>(fileSize));
    if(stream.good())
        return true;

    if(stream.eof() && stream.gcount() == static_cast<StreamSize>(fileSize))
        return true;

    outData.clear();
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT, typename StringT>
[[nodiscard]] inline bool ReadTextFile(const Path<ArenaT>& path, StringT& outText)
    requires requires(StringT& text, usize size){ text.resize(size); text.data(); text.clear(); }
{
    ErrorCode errorCode;
    return GlobalFilesystemDetail::ReadWholeBinaryFile(path, outText, errorCode);
}

template<typename ArenaT>
[[nodiscard]] inline bool WriteTextFile(const Path<ArenaT>& path, const AStringView content){
    GlobalFilesystemDetail::OutputFileStream stream(
        path,
        GlobalFilesystemDetail::OutputFileStream::binary | GlobalFilesystemDetail::OutputFileStream::trunc
    );
    if(!stream.is_open())
        return false;

    if(!GlobalFilesystemDetail::CanRepresentStreamSize(static_cast<u64>(content.size())))
        return false;

    stream.write(content.data(), static_cast<GlobalFilesystemDetail::StreamSize>(content.size()));
    return stream.good();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT, typename Container>
[[nodiscard]] inline bool ReadBinaryFile(const Path<ArenaT>& path, Container& outBytes, ErrorCode& outError){
    return GlobalFilesystemDetail::ReadWholeBinaryFile(path, outBytes, outError);
}

template<typename ArenaT, typename Container>
[[nodiscard]] inline bool WriteBinaryFile(const Path<ArenaT>& path, const Container& bytes){
    GlobalFilesystemDetail::OutputFileStream stream(
        path,
        GlobalFilesystemDetail::OutputFileStream::binary | GlobalFilesystemDetail::OutputFileStream::trunc
    );
    if(!stream.is_open())
        return false;

    if(!GlobalFilesystemDetail::CanRepresentStreamSize(static_cast<u64>(bytes.size())))
        return false;

    if(!bytes.empty()){
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<GlobalFilesystemDetail::StreamSize>(bytes.size())
        );
    }
    return stream.good();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

