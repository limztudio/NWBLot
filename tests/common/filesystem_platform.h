// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/filesystem/path.h>
#include <global/termination.h>

#if defined(FOUNDATION_PLATFORM_WINDOWS)
#include <windows.h>
#else
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests{

template<typename P>
[[nodiscard]] i32 WindowsSymlink(const P& link, const P& target, const bool directory){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    const DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE | (directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);
    return CreateSymbolicLinkW(link.c_str(), target.c_str(), flags) ? 0 : static_cast<i32>(GetLastError());
#else
    (void)link; (void)target; (void)directory;
    return -1;
#endif
}

class HeldFile final : NoCopy{
public:
    template<typename P>
    explicit HeldFile(const P& path){
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        m_handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
        (void)path;
#endif
    }

    ~HeldFile(){ if(open() && !close()) TerminateInvariant(); }

    [[nodiscard]] b8 open()const noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        return m_handle != INVALID_HANDLE_VALUE;
#else
        return false;
#endif
    }

    [[nodiscard]] b8 close()noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS)
        if(!CloseHandle(m_handle))
            return false;
        m_handle = INVALID_HANDLE_VALUE;
        return true;
#else
        return false;
#endif
    }

private:
#if defined(FOUNDATION_PLATFORM_WINDOWS)
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#endif
};

#if defined(FOUNDATION_PLATFORM_WINDOWS)
inline constexpr i32 s_NoLinkPrivilege = ERROR_PRIVILEGE_NOT_HELD;
inline constexpr i32 s_FileSharingViolation = ERROR_SHARING_VIOLATION;
inline constexpr i32 s_LinkLoop = -1;
inline constexpr usize s_PathMax = 0u;
#else
inline constexpr i32 s_NoLinkPrivilege = -1;
inline constexpr i32 s_FileSharingViolation = -1;
inline constexpr i32 s_LinkLoop = ELOOP;
inline constexpr usize s_PathMax = PATH_MAX;
#endif

template<typename P>
[[nodiscard]] i32 OpenDirectoryFd(const P& path){
#if !defined(FOUNDATION_PLATFORM_WINDOWS)
    return ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
#else
    (void)path;
    return -1;
#endif
}

[[nodiscard]] inline i32 MakeDirectoryAt(i32 parent, const char* child){
#if !defined(FOUNDATION_PLATFORM_WINDOWS)
    return ::mkdirat(parent, child, 0700);
#else
    (void)parent; (void)child;
    return -1;
#endif
}

[[nodiscard]] inline i32 OpenChildDirectory(i32 parent, const char* child){
#if !defined(FOUNDATION_PLATFORM_WINDOWS)
    return ::openat(parent, child, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
#else
    (void)parent; (void)child;
    return -1;
#endif
}

[[nodiscard]] inline i32 CreateChildFile(i32 parent, const char* child){
#if !defined(FOUNDATION_PLATFORM_WINDOWS)
    return ::openat(parent, child, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
#else
    (void)parent; (void)child;
    return -1;
#endif
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

