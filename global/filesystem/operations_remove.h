// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        while(true){
            const TStringView fileName(data.cFileName);
            if(fileName != NWB_TEXT(".") && fileName != NWB_TEXT("..")){
                const Path<ArenaT> child = path / fileName;
                removedCount += RemoveAllImpl(child, outError);
                if(outError){
                    CloseDirectory(findHandle, outError);
                    return removedCount;
                }
            }

            if(FindNextFile(findHandle, &data))
                continue;
            CaptureDirectoryIterationError(outError);
            break;
        }
        CloseDirectory(findHandle, outError);
        if(outError)
            return removedCount;
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

    while(true){
        errno = 0;
        dirent* entry = readdir(directory);
        if(entry == nullptr){
            CaptureDirectoryIterationError(outError);
            break;
        }

        const AStringView name(entry->d_name);
        if(name == "." || name == "..")
            continue;

        const Path<ArenaT> child = path / name;
        removedCount += RemoveAllImpl(child, outError);
        if(outError){
            CloseDirectory(directory, outError);
            return removedCount;
        }
    }
    CloseDirectory(directory, outError);
    if(outError)
        return removedCount;

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
    stageDirectoryName.reserve(stageToken.size() + GlobalFilesystemDetail::s_StageDirectoryNameExtraCharacters);
    stageDirectoryName += GlobalFilesystemDetail::s_StagedDirectoryPrefix;
    stageDirectoryName += stageToken;
    stageDirectoryName += GlobalFilesystemDetail::s_StageDirectorySuffix;
    output.stageDirectory = stageBaseDirectory / stageDirectoryName;

    AString<TempArenaT> backupDirectoryName{tempArena};
    backupDirectoryName.reserve(stageToken.size() + GlobalFilesystemDetail::s_BackupDirectoryNameExtraCharacters);
    backupDirectoryName += GlobalFilesystemDetail::s_StagedDirectoryPrefix;
    backupDirectoryName += stageToken;
    backupDirectoryName += GlobalFilesystemDetail::s_BackupDirectorySuffix;
    output.backupDirectory = stageBaseDirectory / backupDirectoryName;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

