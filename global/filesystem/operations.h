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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "../array.h"
#include "../basic_string.h"
#include "../generic.h"
#include "../expected.h"
#include "../error_code.h"
#include "../scope_exit.h"
#include "../limit.h"
#include "../not_null.h"
#include "../type.h"
#include "path.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using BasicOutputFileStream = std::basic_ofstream<T>;
using InputFileStream = std::ifstream;
using OutputFileStream = std::ofstream;
using FileStream = std::fstream;
using StreamOffset = std::streamoff;
using StreamSize = std::streamsize;

using FileOpenMode = std::ios_base::openmode;

inline constexpr FileOpenMode s_FileOpenWrite = std::ios::out;
inline constexpr FileOpenMode s_FileOpenAppend = std::ios::app;
inline constexpr FileOpenMode s_FileOpenBinary = std::ios::binary;
inline constexpr FileOpenMode s_FileOpenTruncate = std::ios::trunc;

using FileFormatFlags = std::ios_base::fmtflags;

inline constexpr FileFormatFlags s_FileFormatFixed = std::ios::fixed;
inline constexpr FileFormatFlags s_FileFormatFloatField = std::ios::floatfield;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 CanRepresentStreamSize(const u64 byteCount)noexcept{
    return ::CanRepresentU64<StreamSize>(byteCount);
}

[[nodiscard]] inline ErrorCode LastSystemError()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    return ErrorCode(static_cast<i32>(GetLastError()), SystemCategory());
#else
    return ErrorCode(errno, GenericCategory());
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
inline void CloseDirectory(const HANDLE findHandle, ErrorCode& outError)noexcept{
    if(!FindClose(findHandle) && !outError)
        outError = LastSystemError();
}

inline void CaptureDirectoryIterationError(ErrorCode& outError)noexcept{
    if(GetLastError() != ERROR_NO_MORE_FILES && !outError)
        outError = LastSystemError();
}
#else
inline void CloseDirectory(const NotNull<DIR*> directory, ErrorCode& outError)noexcept{
    if(closedir(directory.get()) != 0 && !outError)
        outError = LastSystemError();
}

inline void CaptureDirectoryIterationError(ErrorCode& outError)noexcept{
    if(errno != 0 && !outError)
        outError = LastSystemError();
}
#endif

template<typename ArenaT>
[[nodiscard]] inline b8 IsRootComponent(const Path<ArenaT>& path)noexcept{
    const usize pathSize = path.size();
    return pathSize != 0u && pathSize == GlobalFilesystemPathDetail::RootDirectoryLength(path.native());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
struct StagedDirectoryPaths{
    Path<ArenaT> stageDirectory;
    Path<ArenaT> backupDirectory;


public:
    explicit StagedDirectoryPaths(ArenaT& arena)noexcept(noexcept(decltype(stageDirectory)(arena)) && noexcept(decltype(backupDirectory)(arena)))
        : stageDirectory(arena)
        , backupDirectory(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> ReadSymlink([[maybe_unused]] const Path<ArenaT>& path){
#if defined(NWB_PLATFORM_WINDOWS)
    return MakeUnexpected(MakeErrorCode(ErrorCondition::function_not_supported));
#else
    TString<ArenaT> buffer(path.arena());
    usize capacity = GlobalFilesystemDetail::s_InitialPathBufferCapacity;
    for(;;){
        buffer.resize(capacity);
        const ssize_t copiedBytes = readlink(path.c_str(), buffer.data(), buffer.size());
        if(copiedBytes < 0)
            return MakeUnexpected(LastSystemError());
        if(static_cast<usize>(copiedBytes) < buffer.size())
            return Path<ArenaT>(path.arena(), TStringView(buffer.data(), static_cast<usize>(copiedBytes)));
        if(capacity > Limit<usize>::s_Max / 2u)
            return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
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
            return MakeUnexpected(LastSystemError());
        if(copiedLength < capacity)
            return Path<ArenaT>(arena, TStringView(buffer.data(), static_cast<usize>(copiedLength)));
        if(copiedLength == Limit<DWORD>::s_Max)
            return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
        capacity = copiedLength + 1u;
    }
#else
    usize capacity = GlobalFilesystemDetail::s_InitialPathBufferCapacity;
    for(;;){
        buffer.resize(capacity);
        if(getcwd(buffer.data(), buffer.size()) != nullptr)
            return Path<ArenaT>(arena, TStringView(buffer.data(), CharTraits<tchar>::length(buffer.data())));
        if(errno != ERANGE)
            return MakeUnexpected(LastSystemError());
        if(capacity > Limit<usize>::s_Max / 2u)
            return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
        capacity *= 2u;
    }
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Path<ArenaT> LexicallyNormal(const Path<ArenaT>& path){
    return path.lexicallyNormal();
}

template<typename ArenaT>
[[nodiscard]] inline b8 PathHasDirectoryAncestor(const Path<ArenaT>& normalizedPath, const Path<ArenaT>& normalizedDirectory){
    if(normalizedPath.empty() || normalizedDirectory.empty())
        return false;

    for(Path<ArenaT> parent = normalizedPath.parentPath(); !parent.empty();){
        if(parent == normalizedDirectory)
            return true;

        if(parent.parentPathView() == parent.native())
            break;
        parent = Move(parent).parentPath();
    }

    return false;
}

template<typename ArenaT>
[[nodiscard]] inline usize PathDepth(const Path<ArenaT>& path)noexcept{
    usize depth = 0u;
    for(auto it = path.begin(); it != path.end(); ++it)
        ++depth;
    return depth;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> GetExecutablePath(ArenaT& arena){
#if defined(NWB_PLATFORM_WINDOWS)
    constexpr usize s_MaxPathLength = 4096;
    Array<tchar, s_MaxPathLength> executablePathBuffer = {};
    const DWORD copiedLength = GetModuleFileName(nullptr, executablePathBuffer.data(), static_cast<DWORD>(s_MaxPathLength));
    if(copiedLength == 0u)
        return MakeUnexpected(LastSystemError());
    if(copiedLength >= static_cast<DWORD>(s_MaxPathLength))
        return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
    return Path<ArenaT>(arena, TStringView(executablePathBuffer.data(), static_cast<usize>(copiedLength)));
#elif defined(NWB_PLATFORM_LINUX)
    const auto path = ReadSymlink(Path<ArenaT>(arena, "/proc/self/exe"));
    if(!path)
        return MakeUnexpected(path.error());
    if(path->empty())
        return MakeUnexpected(MakeErrorCode(ErrorCondition::no_such_file_or_directory));
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
        return MakeUnexpected(MakeErrorCode(ErrorCondition::no_such_file_or_directory));
    return result;
}

template<typename ArenaT>
[[nodiscard]] inline Path<ArenaT> ResolveDefaultStorageDirectory(ArenaT& arena, const AStringView defaultDirectoryName){
    const auto executableDirectory = GetExecutableDirectory(arena);
    if(executableDirectory)
        return *executableDirectory / defaultDirectoryName;
    return Path<ArenaT>(arena, defaultDirectoryName);
}

template<typename ArenaT, typename SourceArenaT>
[[nodiscard]] inline Path<ArenaT> ResolveStorageDirectory(ArenaT& arena, const Path<SourceArenaT>& configuredStorageDirectory, const AStringView defaultDirectoryName){
    if(!configuredStorageDirectory.empty())
        return Path<ArenaT>(arena, configuredStorageDirectory);
    return ResolveDefaultStorageDirectory(arena, defaultDirectoryName);
}

template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> GetExecutableName(ArenaT& arena){
    const auto executablePath = GetExecutablePath(arena);
    if(!executablePath)
        return MakeUnexpected(executablePath.error());
    Path<ArenaT> result = executablePath->stem();
    if(result.empty())
        return MakeUnexpected(MakeErrorCode(ErrorCondition::no_such_file_or_directory));
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
    return MakeUnexpected(ErrorCode(static_cast<i32>(error), SystemCategory()));
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
[[nodiscard]] inline Expected<b8, ErrorCode> FileExists(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const auto attributes = GlobalFilesystemDetail::FileAttributes(path);
    if(!attributes)
        return MakeUnexpected(attributes.error());
    return *attributes != INVALID_FILE_ATTRIBUTES;
#else
    const auto pathStat = GlobalFilesystemDetail::StatPath(path);
    if(!pathStat){
        if(pathStat.error() == ErrorCondition::no_such_file_or_directory || pathStat.error() == ErrorCondition::not_a_directory)
            return false;
        return MakeUnexpected(pathStat.error());
    }
    return true;
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<b8, ErrorCode> FileExistsNoFollow(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const auto attributes = GlobalFilesystemDetail::FileAttributes(path);
    if(!attributes)
        return MakeUnexpected(attributes.error());
    return *attributes != INVALID_FILE_ATTRIBUTES;
#else
    const auto pathStat = GlobalFilesystemDetail::LStatPath(path);
    if(!pathStat){
        if(pathStat.error() == ErrorCondition::no_such_file_or_directory || pathStat.error() == ErrorCondition::not_a_directory)
            return false;
        return MakeUnexpected(pathStat.error());
    }
    return true;
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<b8, ErrorCode> IsDirectory(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const auto attributes = GlobalFilesystemDetail::FileAttributes(path);
    if(!attributes)
        return MakeUnexpected(attributes.error());
    return *attributes != INVALID_FILE_ATTRIBUTES && (*attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
#else
    const auto pathStat = GlobalFilesystemDetail::StatPath(path);
    if(!pathStat){
        if(pathStat.error() == ErrorCondition::no_such_file_or_directory || pathStat.error() == ErrorCondition::not_a_directory)
            return false;
        return MakeUnexpected(pathStat.error());
    }
    return S_ISDIR(pathStat->st_mode);
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<b8, ErrorCode> IsDirectoryNoFollow(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const auto attributes = GlobalFilesystemDetail::FileAttributes(path);
    if(!attributes)
        return MakeUnexpected(attributes.error());
    return *attributes != INVALID_FILE_ATTRIBUTES && (*attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u && (*attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0u;
#else
    const auto pathStat = GlobalFilesystemDetail::LStatPath(path);
    if(!pathStat){
        if(pathStat.error() == ErrorCondition::no_such_file_or_directory || pathStat.error() == ErrorCondition::not_a_directory)
            return false;
        return MakeUnexpected(pathStat.error());
    }
    return S_ISDIR(pathStat->st_mode);
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<b8, ErrorCode> IsRegularFile(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    const auto attributes = GlobalFilesystemDetail::FileAttributes(path);
    if(!attributes)
        return MakeUnexpected(attributes.error());
    return *attributes != INVALID_FILE_ATTRIBUTES && (*attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u;
#else
    const auto pathStat = GlobalFilesystemDetail::StatPath(path);
    if(!pathStat){
        if(pathStat.error() == ErrorCondition::no_such_file_or_directory || pathStat.error() == ErrorCondition::not_a_directory)
            return false;
        return MakeUnexpected(pathStat.error());
    }
    return S_ISREG(pathStat->st_mode);
#endif
}

[[nodiscard]] inline b8 IsMissingPathError(const ErrorCode& error)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    return error.category() == SystemCategory() && (error.value() == ERROR_FILE_NOT_FOUND || error.value() == ERROR_PATH_NOT_FOUND || error.value() == ERROR_DIRECTORY);
#else
    return error == ErrorCondition::no_such_file_or_directory || error == ErrorCondition::not_a_directory;
#endif
}

[[nodiscard]] inline b8 IsMissingOrInvalidPathLookupError(const ErrorCode& error)noexcept{
    if(IsMissingPathError(error))
        return true;
#if defined(NWB_PLATFORM_WINDOWS)
    if(error.category() != SystemCategory())
        return false;
    switch(error.value()){
    case ERROR_INVALID_NAME:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_PATHNAME:
    case ERROR_CANT_RESOLVE_FILENAME:
        return true;
    default:
        return false;
    }
#else
    return error == ErrorCondition::too_many_symbolic_link_levels;
#endif
}

template<typename ArenaT>
[[nodiscard]] inline Expected<b8, ErrorCode> AreSameExistingPath(const Path<ArenaT>& lhs, const Path<ArenaT>& rhs)noexcept{
    if(lhs.empty() || rhs.empty())
        return false;
#if defined(NWB_PLATFORM_WINDOWS)
    const HANDLE lhsHandle = CreateFile(
        lhs.c_str(),
        0u,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
    if(lhsHandle == INVALID_HANDLE_VALUE){
        const ErrorCode lhsError = LastSystemError();
        if(IsMissingOrInvalidPathLookupError(lhsError))
            return false;
        return MakeUnexpected(lhsError);
    }
    const HANDLE rhsHandle = CreateFile(
        rhs.c_str(),
        0u,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr
    );
    if(rhsHandle == INVALID_HANDLE_VALUE){
        const ErrorCode rhsError = LastSystemError();
        const b8 lhsClosed = CloseHandle(lhsHandle) != 0;
        const ErrorCode closeError = lhsClosed ? ErrorCode{} : LastSystemError();
        if(IsMissingOrInvalidPathLookupError(rhsError)){
            if(closeError)
                return MakeUnexpected(closeError);
            return false;
        }
        return MakeUnexpected(rhsError);
    }

    b8 same = false;
    ErrorCode queryError;
    FILE_ID_INFO lhsId = {};
    FILE_ID_INFO rhsId = {};
    if(
        GetFileInformationByHandleEx(lhsHandle, FileIdInfo, &lhsId, sizeof(lhsId))
        && GetFileInformationByHandleEx(rhsHandle, FileIdInfo, &rhsId, sizeof(rhsId))
    ){
        same = lhsId.VolumeSerialNumber == rhsId.VolumeSerialNumber
            && NWB_MEMCMP(&lhsId.FileId, &rhsId.FileId, sizeof(lhsId.FileId)) == 0;
    }
    else{
        BY_HANDLE_FILE_INFORMATION lhsInfo = {};
        BY_HANDLE_FILE_INFORMATION rhsInfo = {};
        if(GetFileInformationByHandle(lhsHandle, &lhsInfo) && GetFileInformationByHandle(rhsHandle, &rhsInfo)){
            same = lhsInfo.dwVolumeSerialNumber == rhsInfo.dwVolumeSerialNumber
                && lhsInfo.nFileIndexHigh == rhsInfo.nFileIndexHigh
                && lhsInfo.nFileIndexLow == rhsInfo.nFileIndexLow;
        }
        else
            queryError = LastSystemError();
    }

    const b8 rhsClosed = CloseHandle(rhsHandle) != 0;
    const ErrorCode rhsCloseError = rhsClosed ? ErrorCode{} : LastSystemError();
    const b8 lhsClosed = CloseHandle(lhsHandle) != 0;
    const ErrorCode lhsCloseError = lhsClosed ? ErrorCode{} : LastSystemError();
    if(queryError)
        return MakeUnexpected(queryError);
    if(rhsCloseError)
        return MakeUnexpected(rhsCloseError);
    if(lhsCloseError)
        return MakeUnexpected(lhsCloseError);
    return same;
#else
    const auto lhsStat = GlobalFilesystemDetail::StatPath(lhs);
    if(!lhsStat){
        if(IsMissingOrInvalidPathLookupError(lhsStat.error()))
            return false;
        return MakeUnexpected(lhsStat.error());
    }
    const auto rhsStat = GlobalFilesystemDetail::StatPath(rhs);
    if(!rhsStat){
        if(IsMissingOrInvalidPathLookupError(rhsStat.error()))
            return false;
        return MakeUnexpected(rhsStat.error());
    }
    return lhsStat->st_dev == rhsStat->st_dev && lhsStat->st_ino == rhsStat->st_ino;
#endif
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
[[nodiscard]] inline Expected<b8, ErrorCode> CreateDirectorySingle(const Path<ArenaT>& path)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(CreateDirectory(path.c_str(), nullptr))
        return true;
    const ErrorCode createError = LastSystemError();
    if(createError.value() != ERROR_ALREADY_EXISTS)
        return MakeUnexpected(createError);
#else
    if(mkdir(path.c_str(), static_cast<mode_t>(s_CreateDirectoryPermissionMask)) == 0)
        return true;
    const ErrorCode createError = LastSystemError();
    if(createError != ErrorCondition::file_exists)
        return MakeUnexpected(createError);
#endif
    const auto directory = IsDirectory(path);
    if(!directory)
        return MakeUnexpected(directory.error());
    if(!*directory)
        return MakeUnexpected(createError);
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<b8, ErrorCode> CreateDirectories(const Path<ArenaT>& path){
    if(path.empty())
        return false;
    b8 createdAny = false;
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
[[nodiscard]] inline Expected<b8, ErrorCode> RemoveFile(const Path<ArenaT>& path)noexcept{
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
    return MakeUnexpected(LastSystemError());
}

template<typename ArenaT>
[[nodiscard]] inline Expected<void, ErrorCode> RenamePath(const Path<ArenaT>& from, const Path<ArenaT>& to)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(MoveFileEx(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING))
#else
    if(std::rename(from.c_str(), to.c_str()) == 0)
#endif
        return {};
    return MakeUnexpected(LastSystemError());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

