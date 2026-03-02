// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <filesystem>
#include <fstream>

#include "generic.h"
#include "type.h"
#include "limit.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Path = std::filesystem::path;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool FileExists(const Path& path, ErrorCode& errorCode)noexcept{
    return std::filesystem::exists(path, errorCode);
}

[[nodiscard]] inline Path AbsolutePath(const Path& path, ErrorCode& errorCode){
    return std::filesystem::absolute(path, errorCode);
}

inline bool CreateDirectories(const Path& path, ErrorCode& errorCode)noexcept{
    return std::filesystem::create_directories(path, errorCode);
}


[[nodiscard]] inline bool ResolveAbsolutePath(const Path& baseDirectory, const AStringView relativeOrAbsolute, Path& outPath){
    if(relativeOrAbsolute.empty())
        return false;

    Path candidate(relativeOrAbsolute);
    if(!candidate.is_absolute())
        candidate = baseDirectory / candidate;

    ErrorCode errorCode;
    const Path absolutePath = AbsolutePath(candidate, errorCode);
    if(errorCode)
        return false;

    outPath = absolutePath.lexically_normal();
    return true;
}


[[nodiscard]] inline bool ReadTextFile(const Path& path, AString& outText){
    std::ifstream stream(path, std::ifstream::binary);
    if(!stream.is_open())
        return false;

    std::ostringstream ss;
    ss << stream.rdbuf();
    outText = ss.str();
    return stream.good() || stream.eof();
}


template<typename Container>
[[nodiscard]] inline bool ReadBinaryFile(const Path& path, Container& outBytes){
    outBytes.clear();

    ErrorCode errorCode;
    const u64 fileSize = std::filesystem::file_size(path, errorCode);
    if(errorCode)
        return false;

    if(fileSize > static_cast<u64>(Limit<usize>::s_Max))
        return false;

    std::ifstream stream(path, std::ifstream::binary);
    if(!stream.is_open())
        return false;

    outBytes.resize(static_cast<usize>(fileSize));
    if(fileSize == 0)
        return true;

    stream.read(reinterpret_cast<char*>(outBytes.data()), static_cast<std::streamsize>(fileSize));
    if(stream.good())
        return true;

    return stream.eof() && stream.gcount() == static_cast<std::streamsize>(fileSize);
}

template<typename Container, typename PodType>
inline void AppendPOD(Container& outBinary, const PodType& value){
    const usize beginOffset = outBinary.size();
    outBinary.resize(beginOffset + sizeof(PodType));
    NWB_MEMCPY(outBinary.data() + beginOffset, sizeof(PodType), &value, sizeof(PodType));
}

template<typename Container, typename PodType>
[[nodiscard]] inline bool ReadPOD(const Container& binary, usize& inOutOffset, PodType& outValue){
    if(inOutOffset > binary.size())
        return false;
    if(binary.size() - inOutOffset < sizeof(PodType))
        return false;

    NWB_MEMCPY(&outValue, sizeof(PodType), binary.data() + inOutOffset, sizeof(PodType));
    inOutOffset += sizeof(PodType);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

