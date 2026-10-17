// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <chrono>
#include <stop_token>
#include <thread>
#include <latch>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "type.h"
#include "platform.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
#include <sys/syscall.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using StopToken = std::stop_token;
using Latch = std::latch;

using Thread = std::thread;
using JoiningThread = std::jthread;
using ThreadId = std::thread::id;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SleepMS(u32 duration)noexcept{
    std::this_thread::sleep_for(std::chrono::milliseconds(duration));
}


[[nodiscard]] inline ThreadId QueryCurrentThreadId()noexcept{ return std::this_thread::get_id(); }

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

