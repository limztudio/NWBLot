// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FileRemovalFailure{
    ErrorCode code;
    u64 removedCount;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<u64, FileRemovalFailure> RemoveAllImpl(const Path<ArenaT>& path){
#if defined(NWB_PLATFORM_WINDOWS)
    const auto attributes = FileAttributes(path);
    if(!attributes)
        return MakeUnexpected(FileRemovalFailure{ attributes.error(), 0u });
    if(*attributes == INVALID_FILE_ATTRIBUTES)
        return 0u;
    if((*attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u){
        const auto removed = RemoveFile(path);
        if(!removed)
            return MakeUnexpected(FileRemovalFailure{ removed.error(), 0u });
        return *removed ? 1u : 0u;
    }
    u64 removedCount = 0u;
    const Path<ArenaT> pattern = path / NWB_TEXT("*");
    WIN32_FIND_DATA data = {};
    const HANDLE findHandle = FindFirstFile(pattern.c_str(), &data);
    if(findHandle != INVALID_HANDLE_VALUE){
        ErrorCode iterationError;
        ScopeExit close([&]()noexcept{ CloseDirectory(findHandle, iterationError); });
        for(;;){
            const TStringView fileName(data.cFileName);
            if(fileName != NWB_TEXT(".") && fileName != NWB_TEXT("..")){
                const auto removed = RemoveAllImpl(path / fileName);
                if(!removed)
                    return MakeUnexpected(FileRemovalFailure{ removed.error().code, removedCount + removed.error().removedCount });
                removedCount += *removed;
            }
            if(FindNextFile(findHandle, &data))
                continue;
            CaptureDirectoryIterationError(iterationError);
            break;
        }
        CloseDirectory(findHandle, iterationError);
        close.release();
        if(iterationError)
            return MakeUnexpected(FileRemovalFailure{ iterationError, removedCount });
    }
    if(RemoveDirectory(path.c_str()))
        return removedCount + 1u;
    return MakeUnexpected(FileRemovalFailure{ LastSystemError(), removedCount });
#else
    const auto pathStat = LStatPath(path);
    if(!pathStat){
        if(IsMissingPathError(pathStat.error()))
            return 0u;
        return MakeUnexpected(FileRemovalFailure{ pathStat.error(), 0u });
    }
    if(!S_ISDIR(pathStat->st_mode)){
        if(std::remove(path.c_str()) == 0)
            return 1u;
        return MakeUnexpected(FileRemovalFailure{ LastSystemError(), 0u });
    }
    DIR* const directory = opendir(path.c_str());
    if(directory == nullptr)
        return MakeUnexpected(FileRemovalFailure{ LastSystemError(), 0u });
    ErrorCode iterationError;
    ScopeExit close([&]()noexcept{ CloseDirectory(directory, iterationError); });
    u64 removedCount = 0u;
    for(;;){
        errno = 0;
        dirent* const entry = readdir(directory);
        if(entry == nullptr){
            CaptureDirectoryIterationError(iterationError);
            break;
        }
        const AStringView name(entry->d_name);
        if(GlobalFilesystemPathDetail::IsDot(name) || GlobalFilesystemPathDetail::IsDotDot(name))
            continue;
        const auto removed = RemoveAllImpl(path / name);
        if(!removed)
            return MakeUnexpected(FileRemovalFailure{ removed.error().code, removedCount + removed.error().removedCount });
        removedCount += *removed;
    }
    CloseDirectory(directory, iterationError);
    close.release();
    if(iterationError)
        return MakeUnexpected(FileRemovalFailure{ iterationError, removedCount });
    if(rmdir(path.c_str()) != 0)
        return MakeUnexpected(FileRemovalFailure{ LastSystemError(), removedCount });
    return removedCount + 1u;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<u64, FileRemovalFailure> RemoveAll(const Path<ArenaT>& path){
    return GlobalFilesystemDetail::RemoveAllImpl(path);
}

template<typename ArenaT>
[[nodiscard]] inline Expected<void, ErrorCode> RenamePath(const Path<ArenaT>& from, const Path<ArenaT>& to)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    if(MoveFileEx(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING))
#else
    if(std::rename(from.c_str(), to.c_str()) == 0)
#endif
        return {};
    return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<void, ErrorCode> RemoveAllIfExists(const Path<ArenaT>& path){
    const auto exists = FileExists(path);
    if(!exists)
        return MakeUnexpected(exists.error());
    if(!*exists)
        return {};
    const auto removedCount = RemoveAll(path);
    if(!removedCount)
        return MakeUnexpected(removedCount.error().code);
    if(*removedCount != 0u)
        return {};
    const auto stillExists = FileExists(path);
    if(!stillExists)
        return MakeUnexpected(stillExists.error());
    if(*stillExists)
        return MakeUnexpected(std::make_error_code(std::errc::io_error));
    return {};
}

template<typename ArenaT>
[[nodiscard]] inline Expected<void, ErrorCode> EnsureEmptyDirectory(const Path<ArenaT>& path){
    const auto removed = RemoveAllIfExists(path);
    if(!removed)
        return MakeUnexpected(removed.error());
    return EnsureDirectories(path);
}

template<typename ArenaT>
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> MovePathToDirectory(
    const Path<ArenaT>& sourcePath,
    const Path<ArenaT>& destinationDirectory
){
    const auto ensured = EnsureDirectories(destinationDirectory);
    if(!ensured)
        return MakeUnexpected(ensured.error());
    const Path<ArenaT> destination = destinationDirectory / sourcePath.filename();
    const auto removed = RemoveAllIfExists(destination);
    if(!removed)
        return MakeUnexpected(removed.error());
    const auto renamed = RenamePath(sourcePath, destination);
    if(!renamed)
        return MakeUnexpected(renamed.error());
    return destination;
}

template<typename TempArenaT, typename PathArenaT>
[[nodiscard]] inline StagedDirectoryPaths<PathArenaT> BuildStagedDirectoryPaths(TempArenaT& tempArena, const Path<PathArenaT>& outputDirectory, const AStringView stageToken){
    PathArenaT& pathArena = outputDirectory.arena();
    const Path<PathArenaT> outputParentDirectory = outputDirectory.parentPath();
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

