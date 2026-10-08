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
#include "../expected.h"
#include "../scope_exit.h"
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
inline constexpr usize s_FileSizeHighPartShiftBits = sizeof(u32) * 8u;
inline constexpr u32 s_CreateDirectoryPermissionMask = 0777u;
inline constexpr char s_StagedDirectoryPrefix = '.';
inline constexpr AStringView s_StageDirectorySuffix = "_stage";
inline constexpr AStringView s_BackupDirectorySuffix = "_backup";
inline constexpr usize s_StageDirectorySuffixLength = s_StageDirectorySuffix.size();
inline constexpr usize s_BackupDirectorySuffixLength = s_BackupDirectorySuffix.size();
inline constexpr usize s_StageDirectoryNameExtraCharacters = 1u + s_StageDirectorySuffixLength;
inline constexpr usize s_BackupDirectoryNameExtraCharacters = 1u + s_BackupDirectorySuffixLength;


[[nodiscard]] inline bool CanRepresentStreamSize(const u64 byteCount)noexcept{
    return ::CanRepresentU64<StreamSize>(byteCount);
}

[[nodiscard]] inline ErrorCode LastSystemError()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    return ErrorCode(static_cast<i32>(GetLastError()), std::system_category());
#else
    return ErrorCode(errno, std::generic_category());
#endif
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

inline void SetUnsupportedError(ErrorCode& outError)noexcept{
    outError = std::make_error_code(std::errc::function_not_supported);
}

inline void SetValueTooLargeError(ErrorCode& outError)noexcept{
    outError = std::make_error_code(std::errc::value_too_large);
}

inline void SetIOError(ErrorCode& outError)noexcept{
    outError = std::make_error_code(std::errc::io_error);
}

template<typename ArenaT>
[[nodiscard]] inline bool IsRootComponent(const Path<ArenaT>& path)noexcept{
    const usize pathSize = path.size();
    return pathSize != 0u && pathSize == GlobalFilesystemPathDetail::RootDirectoryLength(path.native());
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
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> ReadSymlink(const Path<ArenaT>& path){
#if defined(NWB_PLATFORM_WINDOWS)
    static_cast<void>(path);
    return MakeUnexpected(std::make_error_code(std::errc::function_not_supported));
#else
    TString<ArenaT> buffer(path.arena());
    usize capacity = GlobalFilesystemDetail::s_InitialPathBufferCapacity;
    for(;;){
        buffer.resize(capacity);
        const ssize_t copiedBytes = readlink(path.c_str(), buffer.data(), buffer.size());
        if(copiedBytes < 0)
            return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
        if(static_cast<usize>(copiedBytes) < buffer.size())
            return Path<ArenaT>(path.arena(), TStringView(buffer.data(), static_cast<usize>(copiedBytes)));
        if(capacity > Limit<usize>::s_Max / 2u)
            return MakeUnexpected(std::make_error_code(std::errc::value_too_large));
        capacity *= 2u;
    }
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> GetCurrentPath(ArenaT& arena){
    TString<ArenaT> buffer(arena);
#if defined(NWB_PLATFORM_WINDOWS)
    DWORD capacity = MAX_PATH;
    for(;;){
        buffer.resize(static_cast<usize>(capacity));
        const DWORD copiedLength = GetCurrentDirectory(capacity, buffer.data());
        if(copiedLength == 0u)
            return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
        if(copiedLength < capacity)
            return Path<ArenaT>(arena, TStringView(buffer.data(), static_cast<usize>(copiedLength)));
        if(copiedLength == Limit<DWORD>::s_Max)
            return MakeUnexpected(std::make_error_code(std::errc::value_too_large));
        capacity = copiedLength + 1u;
    }
#else
    usize capacity = GlobalFilesystemDetail::s_InitialPathBufferCapacity;
    for(;;){
        buffer.resize(capacity);
        if(getcwd(buffer.data(), buffer.size()) != nullptr)
            return Path<ArenaT>(arena, TStringView(buffer.data(), std::char_traits<tchar>::length(buffer.data())));
        if(errno != ERANGE)
            return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
        if(capacity > Limit<usize>::s_Max / 2u)
            return MakeUnexpected(std::make_error_code(std::errc::value_too_large));
        capacity *= 2u;
    }
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Path<ArenaT> LexicallyNormal(const Path<ArenaT>& path)noexcept{
    return path.lexicallyNormal();
}

template<typename ArenaT>
[[nodiscard]] inline bool PathHasDirectoryAncestor(const Path<ArenaT>& normalizedPath, const Path<ArenaT>& normalizedDirectory){
    if(normalizedPath.empty() || normalizedDirectory.empty())
        return false;

    for(Path<ArenaT> parent = normalizedPath.parentPath(); !parent.empty();){
        if(parent == normalizedDirectory)
            return true;

        const Path<ArenaT> nextParent = parent.parentPath();
        if(nextParent == parent)
            break;
        parent = nextParent;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> GetExecutablePath(ArenaT& arena){
#if defined(NWB_PLATFORM_WINDOWS)
    constexpr usize s_MaxPathLength = 4096;
    tchar executablePathBuffer[s_MaxPathLength] = {};
    const DWORD copiedLength = GetModuleFileName(nullptr, executablePathBuffer, static_cast<DWORD>(s_MaxPathLength));
    if(copiedLength == 0u)
        return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
    if(copiedLength >= static_cast<DWORD>(s_MaxPathLength))
        return MakeUnexpected(std::make_error_code(std::errc::value_too_large));
    return Path<ArenaT>(arena, TStringView(executablePathBuffer, static_cast<usize>(copiedLength)));
#elif defined(NWB_PLATFORM_LINUX)
    const auto path = ReadSymlink(Path<ArenaT>(arena, "/proc/self/exe"));
    if(!path)
        return MakeUnexpected(path.error());
    if(path->empty())
        return MakeUnexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    return LexicallyNormal(*path);
#else
    return GetCurrentPath(arena);
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> GetExecutableDirectory(ArenaT& arena){
    const auto executablePath = GetExecutablePath(arena);
    if(!executablePath)
        return MakeUnexpected(executablePath.error());
    Path<ArenaT> result = executablePath->parentPath();
    if(result.empty())
        return MakeUnexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    return result;
}

template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> GetExecutableName(ArenaT& arena){
    const auto executablePath = GetExecutablePath(arena);
    if(!executablePath)
        return MakeUnexpected(executablePath.error());
    Path<ArenaT> result = executablePath->stem();
    if(result.empty())
        return MakeUnexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
template<typename ArenaT>
[[nodiscard]] inline Expected<DWORD, ErrorCode> FileAttributes(const Path<ArenaT>& path)noexcept{
    const DWORD attributes = GetFileAttributes(path.c_str());
    if(attributes != INVALID_FILE_ATTRIBUTES)
        return attributes;
    const DWORD error = GetLastError();
    if(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return INVALID_FILE_ATTRIBUTES;
    return MakeUnexpected(ErrorCode(static_cast<i32>(error), std::system_category()));
}
#else
template<typename ArenaT>
[[nodiscard]] inline Expected<struct stat, ErrorCode> StatPath(const Path<ArenaT>& path)noexcept{
    struct stat value = {};
    if(stat(path.c_str(), &value) == 0)
        return value;
    return MakeUnexpected(LastSystemError());
}

template<typename ArenaT>
[[nodiscard]] inline Expected<struct stat, ErrorCode> LStatPath(const Path<ArenaT>& path)noexcept{
    struct stat value = {};
    if(lstat(path.c_str(), &value) == 0)
        return value;
    return MakeUnexpected(LastSystemError());
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> FileExists(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const auto attributes = GlobalFilesystemDetail::FileAttributes(path);
    if(!attributes)
        return MakeUnexpected(attributes.error());
    return *attributes != INVALID_FILE_ATTRIBUTES;
#else
    const auto pathStat = GlobalFilesystemDetail::StatPath(path);
    if(!pathStat){
        if(pathStat.error() == std::errc::no_such_file_or_directory || pathStat.error() == std::errc::not_a_directory)
            return false;
        return MakeUnexpected(pathStat.error());
    }
    return true;
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> FileExistsNoFollow(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const auto attributes = GlobalFilesystemDetail::FileAttributes(path);
    if(!attributes)
        return MakeUnexpected(attributes.error());
    return *attributes != INVALID_FILE_ATTRIBUTES;
#else
    const auto pathStat = GlobalFilesystemDetail::LStatPath(path);
    if(!pathStat){
        if(pathStat.error() == std::errc::no_such_file_or_directory || pathStat.error() == std::errc::not_a_directory)
            return false;
        return MakeUnexpected(pathStat.error());
    }
    return true;
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> IsDirectory(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const auto attributes = GlobalFilesystemDetail::FileAttributes(path);
    if(!attributes)
        return MakeUnexpected(attributes.error());
    return *attributes != INVALID_FILE_ATTRIBUTES && (*attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
#else
    const auto pathStat = GlobalFilesystemDetail::StatPath(path);
    if(!pathStat){
        if(pathStat.error() == std::errc::no_such_file_or_directory || pathStat.error() == std::errc::not_a_directory)
            return false;
        return MakeUnexpected(pathStat.error());
    }
    return S_ISDIR(pathStat->st_mode);
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> IsDirectoryNoFollow(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const auto attributes = GlobalFilesystemDetail::FileAttributes(path);
    if(!attributes)
        return MakeUnexpected(attributes.error());
    return *attributes != INVALID_FILE_ATTRIBUTES && (*attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u && (*attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0u;
#else
    const auto pathStat = GlobalFilesystemDetail::LStatPath(path);
    if(!pathStat){
        if(pathStat.error() == std::errc::no_such_file_or_directory || pathStat.error() == std::errc::not_a_directory)
            return false;
        return MakeUnexpected(pathStat.error());
    }
    return S_ISDIR(pathStat->st_mode);
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> IsRegularFile(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const auto attributes = GlobalFilesystemDetail::FileAttributes(path);
    if(!attributes)
        return MakeUnexpected(attributes.error());
    return *attributes != INVALID_FILE_ATTRIBUTES && (*attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
#else
    const auto pathStat = GlobalFilesystemDetail::StatPath(path);
    if(!pathStat){
        if(pathStat.error() == std::errc::no_such_file_or_directory || pathStat.error() == std::errc::not_a_directory)
            return false;
        return MakeUnexpected(pathStat.error());
    }
    return S_ISREG(pathStat->st_mode);
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
[[nodiscard]] inline bool PathIsDirectory(const Path<ArenaT>& path)noexcept{
    const auto result = IsDirectory(path);
    return result && *result;
}

template<typename ArenaT>
[[nodiscard]] inline bool PathIsRegularFile(const Path<ArenaT>& path)noexcept{
    const auto result = IsRegularFile(path);
    return result && *result;
}

template<typename ArenaT>
[[nodiscard]] inline bool PathIsMissing(const Path<ArenaT>& path)noexcept{
    const auto result = FileExists(path);
    return result && !*result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> AbsolutePath(const Path<ArenaT>& path){
    if(path.isAbsolute())
        return path.lexicallyNormal();
    const auto currentPath = GetCurrentPath(path.arena());
    if(!currentPath)
        return MakeUnexpected(currentPath.error());
    return (*currentPath / path).lexicallyNormal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> CreateDirectorySingle(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(CreateDirectory(path.c_str(), nullptr))
        return true;
    if(GetLastError() == ERROR_ALREADY_EXISTS)
        return false;
#else
    if(mkdir(path.c_str(), static_cast<mode_t>(s_CreateDirectoryPermissionMask)) == 0)
        return true;
    if(errno == EEXIST)
        return false;
#endif
    return MakeUnexpected(LastSystemError());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> CreateDirectories(const Path<ArenaT>& path){
    if(path.empty())
        return false;
    bool createdAny = false;
    Path<ArenaT> current(path.arena());
    const Path<ArenaT> normalized = path.lexicallyNormal();
    for(const Path<ArenaT> component : normalized){
        if(GlobalFilesystemDetail::IsRootComponent(component)){
            current = component;
            continue;
        }
        current = current.empty() ? component : current / component;
        const auto directory = IsDirectory(current);
        if(!directory)
            return MakeUnexpected(directory.error());
        if(*directory)
            continue;
        const auto created = GlobalFilesystemDetail::CreateDirectorySingle(current);
        if(!created)
            return MakeUnexpected(created.error());
        createdAny = createdAny || *created;
    }
    return createdAny;
}

template<typename ArenaT>
[[nodiscard]] inline Expected<void, ErrorCode> EnsureDirectories(const Path<ArenaT>& path){
    const auto created = CreateDirectories(path);
    if(!created)
        return MakeUnexpected(created.error());
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> RemoveFile(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(DeleteFile(path.c_str()))
        return true;
    const DWORD error = GetLastError();
    if(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return false;
#else
    if(std::remove(path.c_str()) == 0)
        return true;
    if(errno == ENOENT || errno == ENOTDIR)
        return false;
#endif
    return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "operations_file_io.h"
#include "operations_remove.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

