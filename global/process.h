
#pragma once


#include "platform.h"
#include "type.h"
#include "basic_string.h"

#include <cstdlib>

#if defined(NWB_PLATFORM_WINDOWS)
#include <processthreadsapi.h>
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
#include <sys/syscall.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline int RunSystemCommand(const char* command){
    return std::system(command);
}

template<typename ArenaT>
[[nodiscard]] inline int RunSystemCommand(const AString<ArenaT>& command){
#if defined(NWB_PLATFORM_WINDOWS)
    AString<ArenaT> systemCommand("\"", command.get_allocator());
    systemCommand += command;
    systemCommand += '"';
    return RunSystemCommand(systemCommand.c_str());
#else
    return RunSystemCommand(command.c_str());
#endif
}

[[nodiscard]] inline u32 CurrentProcessId()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    return static_cast<u32>(GetCurrentProcessId());
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
    return static_cast<u32>(getpid());
#else
    return 0u;
#endif
}

[[nodiscard]] inline u32 CurrentThreadId()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    return static_cast<u32>(GetCurrentThreadId());
#elif defined(SYS_gettid)
    return static_cast<u32>(syscall(SYS_gettid));
#else
    return 0u;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

