// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <filesystem>
#include <fstream>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif

#include "basic_string.h"
#include "compact_string.h"
#include "generic.h"
#include "type.h"
#include "limit.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_filesystem{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool CanRepresentStreamSize(const u64 byteCount)noexcept{
    return byteCount <= static_cast<u64>(Limit<std::streamsize>::s_Max);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Path = std::filesystem::path;

using DirectoryIterator = std::filesystem::directory_iterator;
using RecursiveDirectoryIterator = std::filesystem::recursive_directory_iterator;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct StagedDirectoryPaths{
    Path stageDirectory;
    Path backupDirectory;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ReadSymlink(const Path& path, Path& outPath, ErrorCode& outError)noexcept{
    outPath = std::filesystem::read_symlink(path, outError);
    return !outError;
}

[[nodiscard]] inline bool GetCurrentPath(Path& outPath, ErrorCode& outError)noexcept{
    outPath = std::filesystem::current_path(outError);
    return !outError;
}

[[nodiscard]] inline Path LexicallyNormal(const Path& path)noexcept{
    return path.lexically_normal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GetExecutablePath(Path& outPath){
#if defined(NWB_PLATFORM_WINDOWS)
    constexpr usize s_MaxPathLength = 4096;
    wchar executablePathBuffer[s_MaxPathLength] = {};
    const DWORD copiedLength = GetModuleFileNameW(nullptr, executablePathBuffer, static_cast<DWORD>(s_MaxPathLength));
    if(copiedLength == 0 || copiedLength >= static_cast<DWORD>(s_MaxPathLength))
        return false;

    outPath = Path(executablePathBuffer);
    return true;
#elif defined(NWB_PLATFORM_LINUX)
    ErrorCode errorCode;
    if(!ReadSymlink(Path("/proc/self/exe"), outPath, errorCode) || outPath.empty())
        return false;

    outPath = LexicallyNormal(outPath);
    return true;
#else
    ErrorCode errorCode;
    return GetCurrentPath(outPath, errorCode);
#endif
}

[[nodiscard]] inline bool GetExecutableDirectory(Path& outDirectory){
    Path executablePath;
    if(!GetExecutablePath(executablePath))
        return false;

    outDirectory = executablePath.parent_path();
    return !outDirectory.empty();
}

[[nodiscard]] inline bool GetExecutableName(Path& outName){
    Path executablePath;
    if(!GetExecutablePath(executablePath))
        return false;

    outName = executablePath.stem();
    return !outName.empty();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool FileExists(const Path& path, ErrorCode& outError)noexcept{
    return std::filesystem::exists(path, outError);
}

[[nodiscard]] inline bool IsDirectory(const Path& path, ErrorCode& outError)noexcept{
    return std::filesystem::is_directory(path, outError);
}

[[nodiscard]] inline bool IsRegularFile(const Path& path, ErrorCode& outError)noexcept{
    return std::filesystem::is_regular_file(path, outError);
}

[[nodiscard]] inline bool IsMissingPathError(const ErrorCode& error)noexcept{
    return error == std::errc::no_such_file_or_directory
        || error == std::errc::not_a_directory;
}


[[nodiscard]] inline Path AbsolutePath(const Path& path, ErrorCode& outError)noexcept{
    return std::filesystem::absolute(path, outError);
}


[[nodiscard]] inline bool CreateDirectories(const Path& path, ErrorCode& outError)noexcept{
    std::filesystem::create_directories(path, outError);
    return !outError;
}


[[nodiscard]] inline bool RemoveFile(const Path& path, ErrorCode& outError)noexcept{
    std::filesystem::remove(path, outError);
    return !outError;
}

[[nodiscard]] inline bool RemoveAll(const Path& path, ErrorCode& outError)noexcept{
    std::filesystem::remove_all(path, outError);
    return !outError;
}

[[nodiscard]] inline bool RenamePath(const Path& from, const Path& to, ErrorCode& outError)noexcept{
    std::filesystem::rename(from, to, outError);
    return !outError;
}


[[nodiscard]] inline bool RemoveAllIfExists(const Path& path, ErrorCode& outError)noexcept{
    outError.clear();

    const bool exists = FileExists(path, outError);
    if(outError || !exists)
        return !outError;

    outError.clear();
    return RemoveAll(path, outError);
}

[[nodiscard]] inline bool EnsureEmptyDirectory(const Path& path, ErrorCode& outError)noexcept{
    if(!RemoveAllIfExists(path, outError))
        return false;

    outError.clear();
    return CreateDirectories(path, outError);
}

[[nodiscard]] inline StagedDirectoryPaths BuildStagedDirectoryPaths(const Path& outputDirectory, const AStringView stageToken){
    const Path stageBaseDirectory = outputDirectory.parent_path().empty()
        ? outputDirectory
        : outputDirectory.parent_path()
    ;

    StagedDirectoryPaths output;
    output.stageDirectory = stageBaseDirectory / StringFormat(".{}_stage", stageToken);
    output.backupDirectory = stageBaseDirectory / StringFormat(".{}_backup", stageToken);
    return output;
}


[[nodiscard]] inline u64 FileSize(const Path& path, ErrorCode& outError)noexcept{
    return std::filesystem::file_size(path, outError);
}


[[nodiscard]] inline bool ResolveAbsolutePath(const Path& baseDirectory, const AStringView relativeOrAbsolute, Path& outPath, ErrorCode& outError){
    if(relativeOrAbsolute.empty())
        return false;

    Path candidate(relativeOrAbsolute);
    if(!candidate.is_absolute())
        candidate = baseDirectory / candidate;

    const Path absolutePath = AbsolutePath(candidate, outError);
    if(outError)
        return false;

    outPath = LexicallyNormal(absolutePath);
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

[[nodiscard]] inline bool WriteTextFile(const Path& path, const AStringView content){
    std::ofstream stream(path, std::ofstream::binary | std::ofstream::trunc);
    if(!stream.is_open())
        return false;

    if(!__hidden_global_filesystem::CanRepresentStreamSize(static_cast<u64>(content.size())))
        return false;

    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    return stream.good();
}


template<typename Container>
[[nodiscard]] inline bool ReadBinaryFile(const Path& path, Container& outBytes, ErrorCode& outError){
    outBytes.clear();

    const u64 fileSize = FileSize(path, outError);
    if(outError)
        return false;

    if(fileSize > static_cast<u64>(Limit<usize>::s_Max))
        return false;
    if(!__hidden_global_filesystem::CanRepresentStreamSize(fileSize))
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

template<typename Container>
[[nodiscard]] inline bool WriteBinaryFile(const Path& path, const Container& bytes){
    std::ofstream stream(path, std::ofstream::binary | std::ofstream::trunc);
    if(!stream.is_open())
        return false;

    if(!__hidden_global_filesystem::CanRepresentStreamSize(static_cast<u64>(bytes.size())))
        return false;

    if(!bytes.empty())
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return stream.good();
}


template<typename Container, typename PodType>
inline void AppendPOD(Container& outBinary, const PodType& value){
    const usize beginOffset = outBinary.size();
    if(beginOffset > Limit<usize>::s_Max - sizeof(PodType))
        throw RuntimeException("AppendPOD size overflow");

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

template<typename Container>
[[nodiscard]] inline bool AppendString(Container& outBinary, const AStringView text){
    if(text.size() > Limit<u32>::s_Max)
        return false;

    const usize lengthOffset = outBinary.size();
    if(lengthOffset > Limit<usize>::s_Max - sizeof(u32))
        return false;

    const u32 textLength = static_cast<u32>(text.size());
    const usize textOffset = lengthOffset + sizeof(u32);
    if(textOffset > Limit<usize>::s_Max - textLength)
        return false;

    AppendPOD(outBinary, textLength);
    if(textLength == 0)
        return true;

    outBinary.resize(textOffset + textLength);
    NWB_MEMCPY(outBinary.data() + textOffset, textLength, text.data(), textLength);
    return true;
}
template<typename Container>
[[nodiscard]] inline bool AppendString(Container& outBinary, const CompactString& text){
    return AppendString(outBinary, text.view());
}

template<typename Container>
[[nodiscard]] inline bool ReadString(const Container& binary, usize& inOutOffset, AString& outText){
    usize cursor = inOutOffset;
    u32 textLength = 0;
    if(!ReadPOD(binary, cursor, textLength))
        return false;

    if(cursor > binary.size())
        return false;
    if(binary.size() - cursor < textLength)
        return false;

    AString text;
    text.assign(reinterpret_cast<const char*>(binary.data() + cursor), textLength);
    cursor += textLength;

    outText = Move(text);
    inOutOffset = cursor;
    return true;
}
template<typename Container>
[[nodiscard]] inline bool ReadString(const Container& binary, usize& inOutOffset, CompactString& outText){
    usize cursor = inOutOffset;
    AString text;
    if(!ReadString(binary, cursor, text))
        return false;

    CompactString parsedText;
    if(!parsedText.assign(text))
        return false;

    outText = parsedText;
    inOutOffset = cursor;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

