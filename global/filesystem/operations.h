// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <cerrno>
#include <cstdio>
#include <fstream>

#include "../platform.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "../basic_string.h"
#include "../generic.h"
#include "../limit.h"
#include "../type.h"
#include "path.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using InputFileStream = std::ifstream;
using OutputFileStream = std::ofstream;
using FileStream = std::fstream;
using StreamOffset = std::streamoff;
using StreamSize = std::streamsize;

inline constexpr usize s_InitialPathBufferCapacity = 256u;


[[nodiscard]] inline bool CanRepresentStreamSize(const u64 byteCount)noexcept{
    return byteCount <= static_cast<u64>(Limit<StreamSize>::s_Max);
}

inline void ClearError(ErrorCode& outError)noexcept{
    outError.clear();
}

#if defined(NWB_PLATFORM_WINDOWS)
inline void SetLastSystemError(ErrorCode& outError)noexcept{
    outError = ErrorCode(static_cast<i32>(GetLastError()), std::system_category());
}
#else
inline void SetLastSystemError(ErrorCode& outError)noexcept{
    outError = ErrorCode(errno, std::generic_category());
}
#endif

inline void SetMissingPathError(ErrorCode& outError)noexcept{
    outError = std::make_error_code(std::errc::no_such_file_or_directory);
}

inline void SetUnsupportedError(ErrorCode& outError)noexcept{
    outError = std::make_error_code(std::errc::function_not_supported);
}

template<typename ArenaT>
[[nodiscard]] inline bool IsRootComponent(const Path<ArenaT>& path)noexcept{
    return path.size() != 0u && path.size() == GlobalFilesystemPathDetail::RootDirectoryLength(path.native());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using BasicOutputFileStream = std::basic_ofstream<T>;
using InputFileStream = GlobalFilesystemDetail::InputFileStream;
using OutputFileStream = GlobalFilesystemDetail::OutputFileStream;
using FileStream = GlobalFilesystemDetail::FileStream;
using StreamOffset = GlobalFilesystemDetail::StreamOffset;
using StreamSize = GlobalFilesystemDetail::StreamSize;

using FileOpenMode = std::ios_base::openmode;

inline constexpr FileOpenMode s_FileOpenWrite = std::ios::out;
inline constexpr FileOpenMode s_FileOpenAppend = std::ios::app;
inline constexpr FileOpenMode s_FileOpenBinary = std::ios::binary;
inline constexpr FileOpenMode s_FileOpenTruncate = std::ios::trunc;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
struct StagedDirectoryPaths{
    explicit StagedDirectoryPaths(ArenaT& arena)
        : stageDirectory(arena)
        , backupDirectory(arena)
    {}

    Path<ArenaT> stageDirectory;
    Path<ArenaT> backupDirectory;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool ReadSymlink(const Path<ArenaT>& path, Path<ArenaT>& outPath, ErrorCode& outError)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    static_cast<void>(path);
    static_cast<void>(outPath);
    GlobalFilesystemDetail::SetUnsupportedError(outError);
    return false;
#else
    TString<ArenaT> buffer(outPath.arena());
    usize capacity = GlobalFilesystemDetail::s_InitialPathBufferCapacity;
    for(;;){
        buffer.resize(capacity);
        const ssize_t copiedBytes = readlink(path.c_str(), buffer.data(), buffer.size());
        if(copiedBytes < 0){
            GlobalFilesystemDetail::SetLastSystemError(outError);
            return false;
        }
        if(static_cast<usize>(copiedBytes) < buffer.size()){
            outPath = TStringView(buffer.data(), static_cast<usize>(copiedBytes));
            GlobalFilesystemDetail::ClearError(outError);
            return true;
        }
        capacity *= 2u;
    }
#endif
}

template<typename ArenaT>
[[nodiscard]] inline bool GetCurrentPath(Path<ArenaT>& outPath, ErrorCode& outError)noexcept{
    TString<ArenaT> buffer(outPath.arena());
#if defined(NWB_PLATFORM_WINDOWS)
    DWORD capacity = MAX_PATH;
    for(;;){
        buffer.resize(static_cast<usize>(capacity));
        const DWORD copiedLength = GetCurrentDirectory(capacity, buffer.data());
        if(copiedLength == 0u){
            GlobalFilesystemDetail::SetLastSystemError(outError);
            return false;
        }
        if(copiedLength < capacity){
            outPath = TStringView(buffer.data(), static_cast<usize>(copiedLength));
            GlobalFilesystemDetail::ClearError(outError);
            return true;
        }
        capacity = copiedLength + 1u;
    }
#else
    usize capacity = GlobalFilesystemDetail::s_InitialPathBufferCapacity;
    for(;;){
        buffer.resize(capacity);
        if(getcwd(buffer.data(), buffer.size()) != nullptr){
            outPath = TStringView(buffer.data(), std::char_traits<tchar>::length(buffer.data()));
            GlobalFilesystemDetail::ClearError(outError);
            return true;
        }
        if(errno != ERANGE){
            GlobalFilesystemDetail::SetLastSystemError(outError);
            return false;
        }
        capacity *= 2u;
    }
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Path<ArenaT> LexicallyNormal(const Path<ArenaT>& path)noexcept{
    return path.lexically_normal();
}

template<typename ArenaT>
[[nodiscard]] inline bool PathHasDirectoryAncestor(const Path<ArenaT>& normalizedPath, const Path<ArenaT>& normalizedDirectory){
    if(normalizedPath.empty() || normalizedDirectory.empty())
        return false;

    for(Path<ArenaT> parent = normalizedPath.parent_path(); !parent.empty();){
        if(parent == normalizedDirectory)
            return true;

        const Path<ArenaT> nextParent = parent.parent_path();
        if(nextParent == parent)
            break;
        parent = nextParent;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool GetExecutablePath(Path<ArenaT>& outPath){
#if defined(NWB_PLATFORM_WINDOWS)
    constexpr usize s_MaxPathLength = 4096;
    tchar executablePathBuffer[s_MaxPathLength] = {};
    const DWORD copiedLength = GetModuleFileName(nullptr, executablePathBuffer, static_cast<DWORD>(s_MaxPathLength));
    if(copiedLength == 0 || copiedLength >= static_cast<DWORD>(s_MaxPathLength))
        return false;

    outPath = TStringView(executablePathBuffer, static_cast<usize>(copiedLength));
    return true;
#elif defined(NWB_PLATFORM_LINUX)
    ErrorCode errorCode;
    if(!ReadSymlink(Path<ArenaT>(outPath.arena(), "/proc/self/exe"), outPath, errorCode) || outPath.empty())
        return false;

    outPath = LexicallyNormal(outPath);
    return true;
#else
    ErrorCode errorCode;
    return GetCurrentPath(outPath, errorCode);
#endif
}

template<typename ArenaT>
[[nodiscard]] inline bool GetExecutableDirectory(Path<ArenaT>& outDirectory){
    Path<ArenaT> executablePath(outDirectory.arena());
    if(!GetExecutablePath(executablePath))
        return false;

    outDirectory = executablePath.parent_path();
    return !outDirectory.empty();
}

template<typename ArenaT>
[[nodiscard]] inline bool GetExecutableName(Path<ArenaT>& outName){
    Path<ArenaT> executablePath(outName.arena());
    if(!GetExecutablePath(executablePath))
        return false;

    outName = executablePath.stem();
    return !outName.empty();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
template<typename ArenaT>
[[nodiscard]] inline DWORD FileAttributes(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
    const DWORD attributes = GetFileAttributes(path.c_str());
    if(attributes == INVALID_FILE_ATTRIBUTES){
        const DWORD error = GetLastError();
        if(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND){
            ClearError(outError);
            return INVALID_FILE_ATTRIBUTES;
        }
        SetLastSystemError(outError);
        return INVALID_FILE_ATTRIBUTES;
    }

    ClearError(outError);
    return attributes;
}
#else
template<typename ArenaT>
[[nodiscard]] inline bool StatPath(const Path<ArenaT>& path, struct stat& outStat, ErrorCode& outError)noexcept{
    if(stat(path.c_str(), &outStat) == 0){
        ClearError(outError);
        return true;
    }

    if(errno == ENOENT || errno == ENOTDIR){
        ClearError(outError);
        return false;
    }

    SetLastSystemError(outError);
    return false;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool FileExists(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    return GlobalFilesystemDetail::FileAttributes(path, outError) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat pathStat;
    return GlobalFilesystemDetail::StatPath(path, pathStat, outError);
#endif
}

template<typename ArenaT>
[[nodiscard]] inline bool IsDirectory(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const DWORD attributes = GlobalFilesystemDetail::FileAttributes(path, outError);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
#else
    struct stat pathStat;
    return GlobalFilesystemDetail::StatPath(path, pathStat, outError) && S_ISDIR(pathStat.st_mode);
#endif
}

template<typename ArenaT>
[[nodiscard]] inline bool IsRegularFile(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const DWORD attributes = GlobalFilesystemDetail::FileAttributes(path, outError);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
#else
    struct stat pathStat;
    return GlobalFilesystemDetail::StatPath(path, pathStat, outError) && S_ISREG(pathStat.st_mode);
#endif
}

[[nodiscard]] inline bool IsMissingPathError(const ErrorCode& error)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    return error.category() == std::system_category()
        && (
            error.value() == ERROR_FILE_NOT_FOUND
            || error.value() == ERROR_PATH_NOT_FOUND
            || error.value() == ERROR_DIRECTORY
        )
    ;
#else
    return error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory;
#endif
}

template<typename ArenaT>
[[nodiscard]] inline bool PathExists(const Path<ArenaT>& path)noexcept{
    ErrorCode error;
    return FileExists(path, error) && !error;
}

template<typename ArenaT>
[[nodiscard]] inline bool PathIsDirectory(const Path<ArenaT>& path)noexcept{
    ErrorCode error;
    return IsDirectory(path, error) && !error;
}

template<typename ArenaT>
[[nodiscard]] inline bool PathIsRegularFile(const Path<ArenaT>& path)noexcept{
    ErrorCode error;
    return IsRegularFile(path, error) && !error;
}

template<typename ArenaT>
[[nodiscard]] inline bool PathIsMissing(const Path<ArenaT>& path)noexcept{
    ErrorCode error;
    return !FileExists(path, error) && !error;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Path<ArenaT> AbsolutePath(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
    if(path.is_absolute()){
        GlobalFilesystemDetail::ClearError(outError);
        return path.lexically_normal();
    }

    Path<ArenaT> currentPath(path.arena());
    if(!GetCurrentPath(currentPath, outError))
        return Path<ArenaT>(path.arena());

    GlobalFilesystemDetail::ClearError(outError);
    return (currentPath / path).lexically_normal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool CreateDirectorySingle(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(CreateDirectory(path.c_str(), nullptr)){
        ClearError(outError);
        return true;
    }

    if(GetLastError() == ERROR_ALREADY_EXISTS){
        ClearError(outError);
        return false;
    }
#else
    if(mkdir(path.c_str(), 0777) == 0){
        ClearError(outError);
        return true;
    }

    if(errno == EEXIST){
        ClearError(outError);
        return false;
    }
#endif

    SetLastSystemError(outError);
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool CreateDirectories(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
    outError.clear();
    if(path.empty())
        return false;

    bool createdAny = false;
    Path<ArenaT> current(path.arena());
    const Path<ArenaT> normalized = path.lexically_normal();

    for(const Path<ArenaT> component : normalized){
        if(GlobalFilesystemDetail::IsRootComponent(component)){
            current = component;
            continue;
        }

        current = current.empty() ? component : current / component;
        if(IsDirectory(current, outError))
            continue;
        if(outError)
            return false;

        const bool created = GlobalFilesystemDetail::CreateDirectorySingle(current, outError);
        if(outError)
            return false;
        createdAny = createdAny || created;
    }

    return createdAny;
}

template<typename ArenaT>
[[nodiscard]] inline bool EnsureDirectories(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
    if(CreateDirectories(path, outError))
        return true;

    return !outError;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool RemoveFile(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(DeleteFile(path.c_str())){
        GlobalFilesystemDetail::ClearError(outError);
        return true;
    }

    const DWORD error = GetLastError();
    if(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND){
        GlobalFilesystemDetail::ClearError(outError);
        return false;
    }
#else
    if(std::remove(path.c_str()) == 0){
        GlobalFilesystemDetail::ClearError(outError);
        return true;
    }

    if(errno == ENOENT || errno == ENOTDIR){
        GlobalFilesystemDetail::ClearError(outError);
        return false;
    }
#endif

    GlobalFilesystemDetail::SetLastSystemError(outError);
    return false;
}

namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline u64 RemoveAllImpl(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const DWORD attributes = FileAttributes(path, outError);
    if(outError)
        return 0u;
    if(attributes == INVALID_FILE_ATTRIBUTES)
        return 0u;

    if((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u){
        return RemoveFile(path, outError) ? 1u : 0u;
    }

    u64 removedCount = 0u;
    Path<ArenaT> pattern = path / NWB_TEXT("*");
    WIN32_FIND_DATA data = {};
    HANDLE findHandle = FindFirstFile(pattern.c_str(), &data);
    if(findHandle != INVALID_HANDLE_VALUE){
        do{
            const TStringView fileName(data.cFileName);
            if(fileName == NWB_TEXT(".") || fileName == NWB_TEXT(".."))
                continue;

            const Path<ArenaT> child = path / fileName;
            removedCount += RemoveAllImpl(child, outError);
            if(outError){
                FindClose(findHandle);
                return removedCount;
            }
        }while(FindNextFile(findHandle, &data));
        FindClose(findHandle);
    }

    if(RemoveDirectory(path.c_str())){
        ClearError(outError);
        return removedCount + 1u;
    }

    SetLastSystemError(outError);
    return removedCount;
#else
    struct stat pathStat;
    if(lstat(path.c_str(), &pathStat) != 0){
        if(errno == ENOENT || errno == ENOTDIR){
            ClearError(outError);
            return 0u;
        }
        SetLastSystemError(outError);
        return 0u;
    }

    if(!S_ISDIR(pathStat.st_mode)){
        if(std::remove(path.c_str()) == 0){
            ClearError(outError);
            return 1u;
        }
        SetLastSystemError(outError);
        return 0u;
    }

    u64 removedCount = 0u;
    DIR* directory = opendir(path.c_str());
    if(directory == nullptr){
        SetLastSystemError(outError);
        return 0u;
    }

    for(dirent* entry = readdir(directory); entry != nullptr; entry = readdir(directory)){
        const AStringView name(entry->d_name);
        if(name == "." || name == "..")
            continue;

        const Path<ArenaT> child = path / name;
        removedCount += RemoveAllImpl(child, outError);
        if(outError){
            closedir(directory);
            return removedCount;
        }
    }
    closedir(directory);

    if(rmdir(path.c_str()) != 0){
        SetLastSystemError(outError);
        return removedCount;
    }

    ClearError(outError);
    return removedCount + 1u;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline u64 RemoveAll(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
    return GlobalFilesystemDetail::RemoveAllImpl(path, outError);
}

template<typename ArenaT>
[[nodiscard]] inline bool RenamePath(const Path<ArenaT>& from, const Path<ArenaT>& to, ErrorCode& outError)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(MoveFileEx(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)){
#else
    if(std::rename(from.c_str(), to.c_str()) == 0){
#endif
        GlobalFilesystemDetail::ClearError(outError);
        return true;
    }

    GlobalFilesystemDetail::SetLastSystemError(outError);
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool RemoveAllIfExists(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
    outError.clear();

    const bool exists = FileExists(path, outError);
    if(outError || !exists)
        return !outError;

    outError.clear();
    const u64 removedCount = RemoveAll(path, outError);
    if(outError)
        return false;
    if(removedCount != 0u)
        return true;

    const bool stillExists = FileExists(path, outError);
    return !outError && !stillExists;
}

template<typename ArenaT>
[[nodiscard]] inline bool EnsureEmptyDirectory(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
    if(!RemoveAllIfExists(path, outError))
        return false;

    outError.clear();
    return EnsureDirectories(path, outError);
}

template<typename ArenaT>
[[nodiscard]] inline bool MovePathToDirectory(const Path<ArenaT>& sourcePath, const Path<ArenaT>& destinationDirectory, Path<ArenaT>& outPath){
    outPath.clear();

    ErrorCode error;
    if(!EnsureDirectories(destinationDirectory, error))
        return false;

    const Path<ArenaT> destination = destinationDirectory / sourcePath.filename();
    error.clear();
    if(!RemoveAllIfExists(destination, error))
        return false;

    error.clear();
    if(!RenamePath(sourcePath, destination, error))
        return false;

    outPath = destination;
    return true;
}

template<typename ArenaT>
[[nodiscard]] inline bool MovePathToDirectory(const Path<ArenaT>& sourcePath, const Path<ArenaT>& destinationDirectory){
    Path<ArenaT> movedPath(sourcePath.arena());
    return MovePathToDirectory(sourcePath, destinationDirectory, movedPath);
}

template<typename TempArenaT, typename PathArenaT>
[[nodiscard]] inline StagedDirectoryPaths<PathArenaT> BuildStagedDirectoryPaths(TempArenaT& tempArena, const Path<PathArenaT>& outputDirectory, const AStringView stageToken){
    PathArenaT& pathArena = outputDirectory.arena();
    const Path<PathArenaT> outputParentDirectory = outputDirectory.parent_path();
    const Path<PathArenaT> stageBaseDirectory = outputParentDirectory.empty() ? outputDirectory : outputParentDirectory;

    StagedDirectoryPaths<PathArenaT> output(pathArena);

    AString<TempArenaT> stageDirectoryName{tempArena};
    stageDirectoryName.reserve(stageToken.size() + 7u);
    stageDirectoryName += '.';
    stageDirectoryName += stageToken;
    stageDirectoryName += "_stage";
    output.stageDirectory = stageBaseDirectory / stageDirectoryName;

    AString<TempArenaT> backupDirectoryName{tempArena};
    backupDirectoryName.reserve(stageToken.size() + 8u);
    backupDirectoryName += '.';
    backupDirectoryName += stageToken;
    backupDirectoryName += "_backup";
    output.backupDirectory = stageBaseDirectory / backupDirectoryName;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline u64 FileSize(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if(!GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &data)){
        GlobalFilesystemDetail::SetLastSystemError(outError);
        return 0u;
    }

    GlobalFilesystemDetail::ClearError(outError);
    return (static_cast<u64>(data.nFileSizeHigh) << 32u) | static_cast<u64>(data.nFileSizeLow);
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

