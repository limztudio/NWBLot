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
#include "operations_file_io.h"
#include "operations_remove.h"
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
inline constexpr usize s_FileSizeHighPartShiftBits = sizeof(u32) * 8u;
inline constexpr u32 s_CreateDirectoryPermissionMask = 0777u;
inline constexpr char s_StagedDirectoryPrefix = '.';
inline constexpr char s_StageDirectorySuffix[] = "_stage";
inline constexpr char s_BackupDirectorySuffix[] = "_backup";
inline constexpr usize s_StageDirectorySuffixLength = sizeof(s_StageDirectorySuffix) - 1u;
inline constexpr usize s_BackupDirectorySuffixLength = sizeof(s_BackupDirectorySuffix) - 1u;
inline constexpr usize s_StageDirectoryNameExtraCharacters = 1u + s_StageDirectorySuffixLength;
inline constexpr usize s_BackupDirectoryNameExtraCharacters = 1u + s_BackupDirectorySuffixLength;


[[nodiscard]] inline bool CanRepresentStreamSize(const u64 byteCount)noexcept{
    return ::CanRepresentU64<StreamSize>(byteCount);
}

inline void ClearError(ErrorCode& outError)noexcept{
    outError.clear();
}

#if defined(NWB_PLATFORM_WINDOWS)
inline void SetLastSystemError(ErrorCode& outError)noexcept{
    outError = ErrorCode(static_cast<i32>(GetLastError()), std::system_category());
}

inline void CloseDirectory(const HANDLE findHandle, ErrorCode& outError)noexcept{
    if(!FindClose(findHandle) && !outError)
        SetLastSystemError(outError);
}

inline void CaptureDirectoryIterationError(ErrorCode& outError)noexcept{
    if(GetLastError() != ERROR_NO_MORE_FILES && !outError)
        SetLastSystemError(outError);
}
#else
inline void SetLastSystemError(ErrorCode& outError)noexcept{
    outError = ErrorCode(errno, std::generic_category());
}

inline void CloseDirectory(DIR* const directory, ErrorCode& outError)noexcept{
    if(closedir(directory) != 0 && !outError)
        SetLastSystemError(outError);
}

inline void CaptureDirectoryIterationError(ErrorCode& outError)noexcept{
    if(errno != 0 && !outError)
        SetLastSystemError(outError);
}
#endif

inline void SetMissingPathError(ErrorCode& outError)noexcept{
    outError = std::make_error_code(std::errc::no_such_file_or_directory);
}

inline void SetUnsupportedError(ErrorCode& outError)noexcept{
    outError = std::make_error_code(std::errc::function_not_supported);
}

inline void SetValueTooLargeError(ErrorCode& outError)noexcept{
    outError = std::make_error_code(std::errc::value_too_large);
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

using FileFormatFlags = std::ios_base::fmtflags;

inline constexpr FileFormatFlags s_FileFormatFixed = std::ios::fixed;
inline constexpr FileFormatFlags s_FileFormatFloatField = std::ios::floatfield;


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

template<typename ArenaT>
[[nodiscard]] inline bool LStatPath(const Path<ArenaT>& path, struct stat& outStat, ErrorCode& outError)noexcept{
    if(lstat(path.c_str(), &outStat) == 0){
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
[[nodiscard]] inline bool IsDirectoryNoFollow(const Path<ArenaT>& path, ErrorCode& outError)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const DWORD attributes = GlobalFilesystemDetail::FileAttributes(path, outError);
    return attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u
        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0u
    ;
#else
    struct stat pathStat;
    return GlobalFilesystemDetail::LStatPath(path, pathStat, outError) && S_ISDIR(pathStat.st_mode);
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
    if(mkdir(path.c_str(), static_cast<mode_t>(GlobalFilesystemDetail::s_CreateDirectoryPermissionMask)) == 0){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

