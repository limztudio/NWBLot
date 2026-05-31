// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <filesystem>
#include <fstream>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif

#include "basic_string.h"
#include "generic.h"
#include "limit.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using InputFileStream = std::ifstream;
using OutputFileStream = std::ofstream;
using FileStream = std::fstream;
using StreamOffset = std::streamoff;
using StreamSize = std::streamsize;


[[nodiscard]] inline bool CanRepresentStreamSize(const u64 byteCount)noexcept{
    return byteCount <= static_cast<u64>(Limit<StreamSize>::s_Max);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Path = std::filesystem::path;

template<typename T>
using BasicOutputFileStream = std::basic_ofstream<T>;
using FileOpenMode = std::ios_base::openmode;

inline constexpr FileOpenMode s_FileOpenWrite = std::ios::out;
inline constexpr FileOpenMode s_FileOpenAppend = std::ios::app;
inline constexpr FileOpenMode s_FileOpenBinary = std::ios::binary;
inline constexpr FileOpenMode s_FileOpenTruncate = std::ios::trunc;

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

[[nodiscard]] inline bool PathHasDirectoryAncestor(const Path& normalizedPath, const Path& normalizedDirectory){
    if(normalizedPath.empty() || normalizedDirectory.empty())
        return false;

    for(Path parent = normalizedPath.parent_path(); !parent.empty();){
        if(parent == normalizedDirectory)
            return true;

        const Path nextParent = parent.parent_path();
        if(nextParent == parent)
            break;
        parent = nextParent;
    }

    return false;
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
    return error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Path AbsolutePath(const Path& path, ErrorCode& outError)noexcept{
    return std::filesystem::absolute(path, outError);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool CreateDirectories(const Path& path, ErrorCode& outError)noexcept{
    return std::filesystem::create_directories(path, outError);
}

[[nodiscard]] inline bool EnsureDirectories(const Path& path, ErrorCode& outError)noexcept{
    static_cast<void>(CreateDirectories(path, outError));
    return !outError;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool RemoveFile(const Path& path, ErrorCode& outError)noexcept{
    return std::filesystem::remove(path, outError);
}

[[nodiscard]] inline u64 RemoveAll(const Path& path, ErrorCode& outError)noexcept{
    return static_cast<u64>(std::filesystem::remove_all(path, outError));
}

[[nodiscard]] inline bool RenamePath(const Path& from, const Path& to, ErrorCode& outError)noexcept{
    std::filesystem::rename(from, to, outError);
    return !outError;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool RemoveAllIfExists(const Path& path, ErrorCode& outError)noexcept{
    outError.clear();

    const bool exists = FileExists(path, outError);
    if(outError || !exists)
        return !outError;

    outError.clear();
    static_cast<void>(RemoveAll(path, outError));
    return !outError;
}

[[nodiscard]] inline bool EnsureEmptyDirectory(const Path& path, ErrorCode& outError)noexcept{
    if(!RemoveAllIfExists(path, outError))
        return false;

    outError.clear();
    return EnsureDirectories(path, outError);
}

template<typename ArenaT>
[[nodiscard]] inline StagedDirectoryPaths BuildStagedDirectoryPaths(ArenaT& arena, const Path& outputDirectory, const AStringView stageToken){
    const Path outputParentDirectory = outputDirectory.parent_path();
    const Path stageBaseDirectory = outputParentDirectory.empty() ? outputDirectory : outputParentDirectory;

    StagedDirectoryPaths output;

    AString<ArenaT> stageDirectoryName{arena};
    stageDirectoryName.reserve(stageToken.size() + 7u);
    stageDirectoryName += '.';
    stageDirectoryName += stageToken;
    stageDirectoryName += "_stage";
    output.stageDirectory = stageBaseDirectory / stageDirectoryName;

    AString<ArenaT> backupDirectoryName{arena};
    backupDirectoryName.reserve(stageToken.size() + 8u);
    backupDirectoryName += '.';
    backupDirectoryName += stageToken;
    backupDirectoryName += "_backup";
    output.backupDirectory = stageBaseDirectory / backupDirectoryName;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u64 FileSize(const Path& path, ErrorCode& outError)noexcept{
    return std::filesystem::file_size(path, outError);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ResolveAbsolutePath(
    const Path& baseDirectory,
    const AStringView relativeOrAbsolute,
    Path& outPath,
    ErrorCode& outError
){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container>
[[nodiscard]] inline char* MutableReadBuffer(Container& outData)noexcept{
    return reinterpret_cast<char*>(outData.data());
}

template<typename Container>
[[nodiscard]] inline bool ReadWholeBinaryFile(const Path& path, Container& outData, ErrorCode& outError){
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


template<typename StringT>
[[nodiscard]] inline bool ReadTextFile(const Path& path, StringT& outText)
    requires requires(StringT& text, usize size){ text.resize(size); text.data(); text.clear(); }
{
    ErrorCode errorCode;
    return GlobalFilesystemDetail::ReadWholeBinaryFile(path, outText, errorCode);
}

[[nodiscard]] inline bool WriteTextFile(const Path& path, const AStringView content){
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


template<typename Container>
[[nodiscard]] inline bool ReadBinaryFile(const Path& path, Container& outBytes, ErrorCode& outError){
    return GlobalFilesystemDetail::ReadWholeBinaryFile(path, outBytes, outError);
}

template<typename Container>
[[nodiscard]] inline bool WriteBinaryFile(const Path& path, const Container& bytes){
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

