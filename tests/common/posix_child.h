// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/assert.h>
#include <global/diagnostics.h>
#include <global/type.h>
#include <csignal>
#include <cstdlib>

#if defined(FOUNDATION_PLATFORM_LINUX)
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#endif

#if !defined(FOUNDATION_PLATFORM_WINDOWS)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::PosixChild{

struct WaitResult{
    b8 collected = false;
    b8 signaled = false;
    i32 signal = 0;
    b8 exited = false;
    i32 exitCode = 0;
};

[[nodiscard]] inline i32 Fork()noexcept{
#if defined(FOUNDATION_PLATFORM_LINUX)
    return ::fork();
#else
    return -1;
#endif
}

[[nodiscard]] inline WaitResult Wait(const i32 child)noexcept{
    WaitResult result;
#if defined(FOUNDATION_PLATFORM_LINUX)
    int status = 0;
    pid_t observed;
    do{
        observed = ::waitpid(child, &status, 0);
    }while(observed < 0 && errno == EINTR);
    if(observed == child){
        result.collected = true;
        result.signaled = WIFSIGNALED(status);
        result.exited = WIFEXITED(status);
        if(result.signaled)
            result.signal = WTERMSIG(status);
        if(result.exited)
            result.exitCode = WEXITSTATUS(status);
    }
#else
    (void)child;
#endif
    return result;
}

[[nodiscard]] inline b8 CloseDescriptor(const i32 descriptor)noexcept{
#if !defined(FOUNDATION_PLATFORM_WINDOWS)
    return ::close(descriptor) == 0;
#else
    (void)descriptor;
    return false;
#endif
}

inline void SilenceConsole()noexcept{
#if defined(FOUNDATION_PLATFORM_LINUX)
    const int nullFd = ::open("/dev/null", O_WRONLY);
    if(nullFd < 0)
        return;
    if(::dup2(nullFd, STDOUT_FILENO) < 0 || ::dup2(nullFd, STDERR_FILENO) < 0){
        if(nullFd > STDERR_FILENO)
            ::close(nullFd);
        return;
    }
    if(nullFd > STDERR_FILENO)
        ::close(nullFd);
#endif
}

#if FOUNDATION_OCCUR_ASSERT
inline constexpr auto s_AssertCategory = DiagnosticEventCategory::s_Assert;
#define NWB_TEST_OBSERVABLE_ASSERT(expression) FOUNDATION_ASSERT(expression)
#else
inline constexpr auto s_AssertCategory = DiagnosticEventCategory::s_FatalAssert;
#define NWB_TEST_OBSERVABLE_ASSERT(expression) FOUNDATION_FATAL_ASSERT(expression)
#endif

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

