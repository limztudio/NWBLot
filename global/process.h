// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "platform.h"
#include "type.h"
#include "basic_string.h"

#include <cstdlib>

#if defined(GLB_PLATFORM_WINDOWS)
#include <processthreadsapi.h>
#elif defined(GLB_PLATFORM_LINUX) || defined(GLB_PLATFORM_ANDROID)
#include <sys/syscall.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline int RunSystemCommand(ArenaT& arena, const AStringView command){
    if(command.find(char{}) != AStringView::npos)
        return -1;
    const AString<ArenaT> nativeCommand(command, arena);
    return std::system(nativeCommand.c_str());
}

template<typename ArenaT>
[[nodiscard]] inline int RunSystemCommand(const AString<ArenaT>& command){
#if defined(GLB_PLATFORM_WINDOWS)
    AString<ArenaT> systemCommand("\"", command.get_allocator());
    systemCommand += command;
    systemCommand += '"';
    return std::system(systemCommand.c_str());
#else
    return std::system(command.c_str());
#endif
}

[[nodiscard]] inline u32 CurrentProcessId()noexcept{
#if defined(GLB_PLATFORM_WINDOWS)
    return static_cast<u32>(GetCurrentProcessId());
#elif defined(GLB_PLATFORM_LINUX) || defined(GLB_PLATFORM_ANDROID)
    return static_cast<u32>(getpid());
#else
    return 0u;
#endif
}

[[nodiscard]] inline u32 CurrentThreadId()noexcept{
#if defined(GLB_PLATFORM_WINDOWS)
    return static_cast<u32>(GetCurrentThreadId());
#elif defined(SYS_gettid)
    return static_cast<u32>(syscall(SYS_gettid));
#else
    return 0u;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

