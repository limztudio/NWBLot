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


#if !defined(FOUNDATION_PLATFORM_WINDOWS)
[[nodiscard]] inline Expected<u64, FileRemovalFailure> RemoveAllAt(const int parentDescriptor, const NotNull<const char*> entryName)noexcept;
#endif

template<typename ArenaT>
[[nodiscard]] inline Expected<u64, FileRemovalFailure> RemoveAllImpl(const Path<ArenaT>& path){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
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
    if((*attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u){
        if(RemoveDirectory(path.c_str()))
            return 1u;
        return MakeUnexpected(FileRemovalFailure{ LastSystemError(), 0u });
    }
    u64 removedCount = 0u;
    const Path<ArenaT> pattern = path / FOUNDATION_TEXT("*");
    WIN32_FIND_DATA data = {};
    const HANDLE findHandle = FindFirstFile(pattern.c_str(), &data);
    if(findHandle != INVALID_HANDLE_VALUE){
        ErrorCode iterationError;
        ScopeExit close([&]()noexcept{ CloseDirectory(findHandle, iterationError); });
        for(;;){
            const TStringView fileName(data.cFileName);
            if(fileName != FOUNDATION_TEXT(".") && fileName != FOUNDATION_TEXT("..")){
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
    if(path.empty())
        return 0u;
    return RemoveAllAt(AT_FDCWD, MakeNotNull<const char*>(path.c_str()));
#endif
}

#if !defined(FOUNDATION_PLATFORM_WINDOWS)
[[nodiscard]] inline Expected<u64, FileRemovalFailure> RemoveAllAt(const int parentDescriptor, const NotNull<const char*> entryName)noexcept{
    struct stat entryStat = {};
    if(fstatat(parentDescriptor, entryName.get(), &entryStat, AT_SYMLINK_NOFOLLOW) != 0){
        const ErrorCode statError = LastSystemError();
        if(IsMissingPathError(statError))
            return 0u;
        return MakeUnexpected(FileRemovalFailure{ statError, 0u });
    }
    if(!S_ISDIR(entryStat.st_mode)){
        if(unlinkat(parentDescriptor, entryName.get(), 0) == 0)
            return 1u;
        if(errno == ENOENT || errno == ENOTDIR)
            return 0u;
        return MakeUnexpected(FileRemovalFailure{ LastSystemError(), 0u });
    }
    const int directoryDescriptor = openat(parentDescriptor, entryName.get(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if(directoryDescriptor < 0){
        if(errno == ENOENT)
            return 0u;
        return MakeUnexpected(FileRemovalFailure{ LastSystemError(), 0u });
    }
    DIR* const directory = fdopendir(directoryDescriptor);
    if(directory == nullptr){
        ErrorCode openError = LastSystemError();
        if(close(directoryDescriptor) != 0 && !openError)
            openError = LastSystemError();
        return MakeUnexpected(FileRemovalFailure{ openError, 0u });
    }
    ErrorCode iterationError;
    ScopeExit close([&]()noexcept{ CloseDirectory(MakeNotNull(directory), iterationError); });
    u64 removedCount = 0u;
    for(;;){
        errno = 0;
        dirent* const entry = readdir(directory);
        if(entry == nullptr){
            CaptureDirectoryIterationError(iterationError);
            break;
        }
        const AStringView name(entry->d_name);
        if(IsDot(name) || IsDotDot(name))
            continue;
        const auto removed = RemoveAllAt(directoryDescriptor, MakeNotNull<const char*>(entry->d_name));
        if(!removed)
            return MakeUnexpected(FileRemovalFailure{ removed.error().code, removedCount + removed.error().removedCount });
        removedCount += *removed;
    }
    CloseDirectory(MakeNotNull(directory), iterationError);
    close.release();
    if(iterationError)
        return MakeUnexpected(FileRemovalFailure{ iterationError, removedCount });
    if(unlinkat(parentDescriptor, entryName.get(), AT_REMOVEDIR) != 0){
        if(errno == ENOENT)
            return removedCount;
        return MakeUnexpected(FileRemovalFailure{ LastSystemError(), removedCount });
    }
    return removedCount + 1u;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<u64, FileRemovalFailure> RemoveAll(const Path<ArenaT>& path){
    return GlobalFilesystemDetail::RemoveAllImpl(path);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<void, ErrorCode> RemoveAllIfExists(const Path<ArenaT>& path){
    const auto exists = FileExistsNoFollow(path);
    if(!exists)
        return MakeUnexpected(exists.error());
    if(!*exists)
        return {};
    const auto removedCount = RemoveAll(path);
    if(!removedCount)
        return MakeUnexpected(removedCount.error().code);
    if(*removedCount != 0u)
        return {};
    const auto stillExists = FileExistsNoFollow(path);
    if(!stillExists)
        return MakeUnexpected(stillExists.error());
    if(*stillExists)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::io_error));
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
[[nodiscard]] inline Expected<Path<ArenaT>, ErrorCode> MovePathToDirectory(const Path<ArenaT>& sourcePath, const Path<ArenaT>& destinationDirectory){
    const auto ensured = EnsureDirectories(destinationDirectory);
    if(!ensured)
        return MakeUnexpected(ensured.error());
    const Path<ArenaT> destination = destinationDirectory / sourcePath.filenameView();
    const auto removed = RemoveAllIfExists(destination);
    if(!removed)
        return MakeUnexpected(removed.error());
    const auto renamed = RenamePath(sourcePath, destination);
    if(!renamed)
        return MakeUnexpected(renamed.error());
    return destination;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

