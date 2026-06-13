// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"

#include <exception>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <sys/types.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_posix{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool __hidden_write_all_fd(const int fd, const void* const data, const usize byteCount)noexcept{
    const u8* cursor = static_cast<const u8*>(data);
    usize remaining = byteCount;
    while(remaining > 0u){
        const ssize_t bytesWritten = write(fd, cursor, remaining);
        if(bytesWritten < 0){
            if(errno == EINTR)
                continue;
            return false;
        }
        if(bytesWritten == 0)
            return false;

        cursor += static_cast<usize>(bytesWritten);
        remaining -= static_cast<usize>(bytesWritten);
    }
    return true;
}

static void __hidden_signal_handler(const int signalNumber, siginfo_t*, void*)noexcept{
    Detail::NotifyCrashHandler(Detail::CrashReasonKind::PosixSignal, static_cast<u32>(signalNumber));

    struct sigaction action = {};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(signalNumber, &action, nullptr);
    raise(signalNumber);
}

[[noreturn]] static void __hidden_terminate_handler(){
    Detail::NotifyCrashHandler(Detail::CrashReasonKind::Terminate, 0u);
    abort();
}

static void __hidden_install_signal_handlers(){
    static u8 s_signalStack[SIGSTKSZ * 4u] = {};

    stack_t stack = {};
    stack.ss_sp = s_signalStack;
    stack.ss_size = sizeof(s_signalStack);
    stack.ss_flags = 0;
    sigaltstack(&stack, &Detail::g_State.previousSignalStack);

    struct sigaction action = {};
    action.sa_sigaction = __hidden_signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;

    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);
    sigaction(SIGFPE, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
#if defined(SIGBUS)
    sigaction(SIGBUS, &action, nullptr);
#endif
#if defined(SIGTRAP)
    sigaction(SIGTRAP, &action, nullptr);
#endif
}

#if defined(NWB_PLATFORM_ANDROID)
static void __hidden_open_android_emergency_file(){
    if(Detail::g_State.spoolDirectoryText[0] == 0)
        return;

    char emergencyPath[Detail::s_MaxPathText] = {};
    CopyFixedBuffer(emergencyPath, Detail::g_State.spoolDirectoryText);
    AppendFixedBuffer(emergencyPath, "/last_android_native_crash_request.bin");
    Detail::g_State.emergencyWriteFd = open(emergencyPath, O_CREAT | O_APPEND | O_WRONLY, 0644);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashDumpTransportStatus::Enum RequestCrashHandler(const CrashRequest& request, const u32 waitMilliseconds)noexcept{
    static_cast<void>(waitMilliseconds);

#if defined(NWB_PLATFORM_ANDROID)
    if(g_State.emergencyWriteFd >= 0)
        return __hidden_crash_posix::__hidden_write_all_fd(g_State.emergencyWriteFd, &request, sizeof(request))
            ? CrashDumpTransportStatus::Sent
            : CrashDumpTransportStatus::Failed
        ;
#elif defined(NWB_PLATFORM_LINUX)
    if(g_State.requestWriteFd >= 0)
        return __hidden_crash_posix::__hidden_write_all_fd(g_State.requestWriteFd, &request, sizeof(request))
            ? CrashDumpTransportStatus::Sent
            : CrashDumpTransportStatus::Failed
        ;
#endif

    return CrashDumpTransportStatus::Failed;
}

void NotifyCrashHandler(const CrashReasonKind::Enum reasonKind, const u32 reasonCode)noexcept{
    CrashDumpRequestOptions options;
    static_cast<void>(RequestCrashDump(reasonKind, reasonCode, options));
}

template<typename ArenaT>
bool StartDesktopHandler(const ::Path<ArenaT>& handlerExecutablePath){
#if defined(NWB_PLATFORM_ANDROID)
    static_cast<void>(handlerExecutablePath);
    return true;
#elif defined(NWB_PLATFORM_LINUX)
    if(g_State.handlerStarted)
        return true;
    if(handlerExecutablePath.empty())
        return false;

    int pipeFds[2] = { -1, -1 };
    if(pipe(pipeFds) != 0)
        return false;

    const pid_t pid = fork();
    if(pid < 0){
        close(pipeFds[0]);
        close(pipeFds[1]);
        return false;
    }

    if(pid == 0){
        close(pipeFds[1]);

        char fdText[32] = {};
        AppendUnsignedToFixedBuffer(fdText, static_cast<u64>(pipeFds[0]));
        execl(handlerExecutablePath.c_str(), handlerExecutablePath.c_str(), "--request-fd", fdText, nullptr);
        _exit(127);
    }

    close(pipeFds[0]);
    g_State.requestWriteFd = pipeFds[1];
    g_State.handlerPid = pid;
    g_State.handlerStarted = true;
    return true;
#else
    return false;
#endif
}

template bool StartDesktopHandler(const ::Path<Alloc::PersistentArena>& handlerExecutablePath);

void InstallPlatformHandlers(){
#if defined(NWB_PLATFORM_ANDROID)
    __hidden_crash_posix::__hidden_open_android_emergency_file();
#endif

    __hidden_crash_posix::__hidden_install_signal_handlers();
    std::set_terminate(__hidden_crash_posix::__hidden_terminate_handler);
}

void UninstallPlatformResources(){
#if defined(NWB_PLATFORM_ANDROID)
    if(g_State.emergencyWriteFd >= 0){
        close(g_State.emergencyWriteFd);
        g_State.emergencyWriteFd = -1;
    }
#elif defined(NWB_PLATFORM_LINUX)
    if(g_State.requestWriteFd >= 0){
        close(g_State.requestWriteFd);
        g_State.requestWriteFd = -1;
    }
    g_State.handlerPid = -1;
#endif

    if(g_State.previousSignalStack.ss_sp)
        sigaltstack(&g_State.previousSignalStack, nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

