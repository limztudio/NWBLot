// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"
#include "../not_null.h"

#if !defined(NWB_PLATFORM_WINDOWS)
#include <fcntl.h>
#include <limits.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemCleanupDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline usize NamedPathEnd(const BasicStringView<CharT> path)noexcept(IsArithmetic_V<CharT>){
    usize end = path.size();
    while(end != 0u && GlobalFilesystemPathDetail::IsSeparator(path[end - 1u]))
        --end;
    return end;
}

template<typename CharT>
[[nodiscard]] inline bool HasNamedLeaf(const BasicStringView<CharT> path, const usize end)noexcept(IsArithmetic_V<CharT>){
    usize begin = end;
    while(begin != 0u && !GlobalFilesystemPathDetail::IsSeparator(path[begin - 1u]))
        --begin;
    const usize rootNameEnd = GlobalFilesystemPathDetail::RootNameLength(path);
    if(begin < rootNameEnd)
        begin = rootNameEnd;
    const BasicStringView<CharT> leaf(path.data() + begin, end - begin);
    return !leaf.empty() && !GlobalFilesystemPathDetail::IsDot(leaf) && !GlobalFilesystemPathDetail::IsDotDot(leaf);
}

#if defined(NWB_PLATFORM_WINDOWS)
inline constexpr usize s_PathCapacity = 32768u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ErrorCode WindowsError(const DWORD error)noexcept{
    return ErrorCode(static_cast<i32>(error), std::system_category());
}

[[nodiscard]] inline bool IsMissing(const DWORD error)noexcept{
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

[[nodiscard]] inline usize WindowsRootLength(const WStringView path)noexcept{
    usize begin = 0u;
    if(path.size() >= 4u && path[0] == L'\\' && path[1] == L'\\' && (path[2] == L'?' || path[2] == L'.') && path[3] == L'\\'){
        if(path.size() >= 8u && (path[4] == L'U' || path[4] == L'u') && (path[5] == L'N' || path[5] == L'n') && (path[6] == L'C' || path[6] == L'c') && path[7] == L'\\')
            begin = 8u;
        else if(path.size() >= 6u && path[5] == L':')
            return path.size() >= 7u && GlobalFilesystemPathDetail::IsSeparator(path[6]) ? 7u : 6u;
        else
            return path.size();
    }
    else if(path.size() >= 2u && GlobalFilesystemPathDetail::IsSeparator(path[0]) && GlobalFilesystemPathDetail::IsSeparator(path[1]))
        begin = 2u;
    else
        return GlobalFilesystemPathDetail::RootDirectoryLength(path);

    usize component = 0u;
    for(usize cursor = begin; cursor < path.size(); ++cursor){
        if(GlobalFilesystemPathDetail::IsSeparator(path[cursor])){
            if(++component == 2u)
                return cursor + 1u;
        }
    }
    return path.size();
}

[[nodiscard]] inline Expected<void, ErrorCode> RemoveWindowsTree(wchar (&path)[s_PathCapacity], const usize rootEnd)noexcept{
    usize end = rootEnd;
    for(;;){
        const DWORD attributes = GetFileAttributesW(path);
        if(attributes == INVALID_FILE_ATTRIBUTES){
            const DWORD error = GetLastError();
            if(!IsMissing(error))
                return MakeUnexpected(WindowsError(error));
        }
        else if((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0u || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u){
            const BOOL removed = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u ? RemoveDirectoryW(path) : DeleteFileW(path);
            if(!removed){
                const DWORD error = GetLastError();
                if(!IsMissing(error))
                    return MakeUnexpected(WindowsError(error));
            }
        }
        else{
            if(end > s_PathCapacity - 3u)
                return MakeUnexpected(WindowsError(ERROR_FILENAME_EXCED_RANGE));
            path[end] = L'\\';
            path[end + 1u] = L'*';
            path[end + 2u] = 0;
            WIN32_FIND_DATAW data = {};
            const HANDLE findHandle = FindFirstFileW(path, &data);
            path[end] = 0;
            bool hasChild = false;
            ErrorCode iterationError;
            if(findHandle != INVALID_HANDLE_VALUE){
                for(;;){
                    const WStringView name(data.cFileName);
                    if(!GlobalFilesystemPathDetail::IsDot(name) && !GlobalFilesystemPathDetail::IsDotDot(name)){
                        if(name.size() > s_PathCapacity - end - 2u)
                            iterationError = WindowsError(ERROR_FILENAME_EXCED_RANGE);
                        else{
                            path[end] = L'\\';
                            for(usize index = 0u; index < name.size(); ++index)
                                path[end + 1u + index] = name[index];
                            end += name.size() + 1u;
                            path[end] = 0;
                            hasChild = true;
                        }
                        break;
                    }
                    if(FindNextFileW(findHandle, &data))
                        continue;
                    const DWORD error = GetLastError();
                    if(error != ERROR_NO_MORE_FILES)
                        iterationError = WindowsError(error);
                    break;
                }
                if(!FindClose(findHandle) && !iterationError)
                    iterationError = GlobalFilesystemDetail::LastSystemError();
            }
            else{
                const DWORD error = GetLastError();
                if(!IsMissing(error) && error != ERROR_NO_MORE_FILES)
                    iterationError = WindowsError(error);
            }
            if(iterationError)
                return MakeUnexpected(iterationError);
            if(hasChild)
                continue;
            if(!RemoveDirectoryW(path)){
                const DWORD error = GetLastError();
                if(!IsMissing(error))
                    return MakeUnexpected(WindowsError(error));
            }
        }
        if(end == rootEnd)
            return {};
        while(end > rootEnd && path[end - 1u] != L'\\')
            --end;
        path[--end] = 0;
    }
}
#else


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool IsMissing(const int error)noexcept{
    return error == ENOENT || error == ENOTDIR;
}

[[nodiscard]] inline ErrorCode RemoveAt(const int parent, const NotNull<const char*> name)noexcept{
    struct stat status = {};
    if(fstatat(parent, name.get(), &status, AT_SYMLINK_NOFOLLOW) != 0){
        const int error = errno;
        return IsMissing(error) ? ErrorCode{} : ErrorCode(error, std::generic_category());
    }
    if(!S_ISDIR(status.st_mode)){
        if(unlinkat(parent, name.get(), 0) == 0 || errno == ENOENT)
            return {};
        return GlobalFilesystemDetail::LastSystemError();
    }
    const int descriptor = openat(parent, name.get(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if(descriptor < 0){
        const int error = errno;
        return error == ENOENT ? ErrorCode{} : ErrorCode(error, std::generic_category());
    }
    DIR* const directory = fdopendir(descriptor);
    if(directory == nullptr){
        ErrorCode error = GlobalFilesystemDetail::LastSystemError();
        if(close(descriptor) != 0 && !error)
            error = GlobalFilesystemDetail::LastSystemError();
        return error;
    }
    ErrorCode error;
    for(;;){
        errno = 0;
        const dirent* const entry = readdir(directory);
        if(entry == nullptr){
            if(errno != 0)
                error = GlobalFilesystemDetail::LastSystemError();
            break;
        }
        const AStringView child(entry->d_name);
        if(GlobalFilesystemPathDetail::IsDot(child) || GlobalFilesystemPathDetail::IsDotDot(child))
            continue;
        error = RemoveAt(descriptor, MakeNotNull<const char*>(entry->d_name));
        if(error)
            break;
    }
    if(closedir(directory) != 0 && !error)
        error = GlobalFilesystemDetail::LastSystemError();
    if(error)
        return error;
    if(unlinkat(parent, name.get(), AT_REMOVEDIR) == 0 || errno == ENOENT)
        return {};
    return GlobalFilesystemDetail::LastSystemError();
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Unwind cleanup uses no C++ dynamic storage or allocating path operations; native/libc resource failures remain explicit errors.
// Removes named trees and links without traversing child links; missing paths succeed, and roots or a final dot/dotdot are rejected.
// Windows uses one 32K UTF-16 stack buffer and iterative traversal; POSIX uses relative descriptors and stack frames proportional to depth.
// Windows accepts DOS/UNC paths, including their extended spellings; other device namespaces are rejected without opening them.
// Native failures can leave a partially removed tree; callers must own the tree and exclude concurrent renames/reparse replacement.
template<typename ArenaT>
[[nodiscard]] inline Expected<void, ErrorCode> RemoveAllForCleanup(const Path<ArenaT>& path)noexcept{
    const auto text = path.native();
    if(text.empty())
        return {};
    const usize namedEnd = GlobalFilesystemCleanupDetail::NamedPathEnd(text);
#if defined(NWB_PLATFORM_WINDOWS)
    if(text.find(L'\0') != decltype(text)::npos || !GlobalFilesystemCleanupDetail::HasNamedLeaf(text, namedEnd) || namedEnd <= GlobalFilesystemCleanupDetail::WindowsRootLength(text))
        return MakeUnexpected(GlobalFilesystemCleanupDetail::WindowsError(ERROR_INVALID_NAME));
    wchar buffer[GlobalFilesystemCleanupDetail::s_PathCapacity];
    const DWORD absoluteSize = GetFullPathNameW(path.c_str(), static_cast<DWORD>(GlobalFilesystemCleanupDetail::s_PathCapacity), buffer, nullptr);
    if(absoluteSize == 0u)
        return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
    if(absoluteSize >= GlobalFilesystemCleanupDetail::s_PathCapacity)
        return MakeUnexpected(GlobalFilesystemCleanupDetail::WindowsError(ERROR_FILENAME_EXCED_RANGE));
    usize end = GlobalFilesystemCleanupDetail::NamedPathEnd(WStringView(buffer, absoluteSize));
    if(end <= GlobalFilesystemCleanupDetail::WindowsRootLength(WStringView(buffer, absoluteSize)))
        return MakeUnexpected(GlobalFilesystemCleanupDetail::WindowsError(ERROR_INVALID_NAME));
    buffer[end] = 0;
    if(end >= 4u && buffer[0] == L'\\' && buffer[1] == L'\\' && buffer[2] == L'.' && buffer[3] == L'\\')
        buffer[2] = L'?';
    if(!(end >= 4u && buffer[0] == L'\\' && buffer[1] == L'\\' && buffer[2] == L'?' && buffer[3] == L'\\')){
        const bool unc = end >= 2u && buffer[0] == L'\\' && buffer[1] == L'\\';
        const usize prefix = unc ? 6u : 4u;
        if(end > GlobalFilesystemCleanupDetail::s_PathCapacity - prefix - 1u)
            return MakeUnexpected(GlobalFilesystemCleanupDetail::WindowsError(ERROR_FILENAME_EXCED_RANGE));
        for(usize index = end + 1u; index > (unc ? 2u : 0u); --index)
            buffer[index - 1u + prefix] = buffer[index - 1u];
        buffer[0] = L'\\'; buffer[1] = L'\\'; buffer[2] = L'?'; buffer[3] = L'\\';
        if(unc){
            buffer[4] = L'U'; buffer[5] = L'N'; buffer[6] = L'C'; buffer[7] = L'\\';
        }
        end += prefix;
    }
    return GlobalFilesystemCleanupDetail::RemoveWindowsTree(buffer, end);
#else
    if(text.find('\0') != decltype(text)::npos || !GlobalFilesystemCleanupDetail::HasNamedLeaf(text, namedEnd))
        return MakeUnexpected(ErrorCode(EINVAL, std::generic_category()));
    int parent = open(text[0] == '/' ? "/" : ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if(parent < 0)
        return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
    ErrorCode error;
    usize begin = text[0] == '/' ? 1u : 0u;
    while(begin < namedEnd){
        while(begin < namedEnd && text[begin] == '/')
            ++begin;
        usize end = begin;
        while(end < namedEnd && text[end] != '/')
            ++end;
        if(end - begin > NAME_MAX){
            error = ErrorCode(ENAMETOOLONG, std::generic_category());
            break;
        }
        char component[NAME_MAX + 1u];
        for(usize index = begin; index < end; ++index)
            component[index - begin] = text[index];
        component[end - begin] = 0;
        if(end == namedEnd){
            error = GlobalFilesystemCleanupDetail::RemoveAt(parent, MakeNotNull<const char*>(component));
            break;
        }
        const int next = openat(parent, component, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if(next < 0){
            const int nativeError = errno;
            if(!GlobalFilesystemCleanupDetail::IsMissing(nativeError))
                error = ErrorCode(nativeError, std::generic_category());
            break;
        }
        if(close(parent) != 0){
            error = GlobalFilesystemDetail::LastSystemError();
            if(close(next) != 0 && !error)
                error = GlobalFilesystemDetail::LastSystemError();
            parent = -1;
            break;
        }
        parent = next;
        begin = end + 1u;
    }
    if(parent >= 0 && close(parent) != 0 && !error)
        error = GlobalFilesystemDetail::LastSystemError();
    if(error)
        return MakeUnexpected(error);
    return {};
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

