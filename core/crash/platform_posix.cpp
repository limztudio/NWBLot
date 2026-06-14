// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"

#include <exception>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <sys/types.h>
#include <sys/wait.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_posix{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// CrashRequest is fixed-size and large; keep the alternate stack comfortably above the request snapshot size.
inline constexpr usize s_SignalStackSize = 256u * 1024u;
inline constexpr int s_AndroidEmergencyFileMode = 0644;
inline constexpr int s_FirstNonStdioFileDescriptor = STDERR_FILENO + 1;
inline constexpr int s_HandlerExecFailureExitCode = 127;
inline constexpr usize s_AArch64FramePointerRegisterIndex = 29u;
inline constexpr usize s_PipeFileDescriptorCount = 2u;
inline constexpr usize s_PipeReadEndIndex = 0u;
inline constexpr usize s_PipeWriteEndIndex = 1u;
inline constexpr usize s_MaxFramePointerWalkBytes = 8u * 1024u * 1024u;

struct FramePointerRecord{
    const FramePointerRecord* previous = nullptr;
    const void* returnAddress = nullptr;
};


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

[[nodiscard]] static bool __hidden_is_aligned_pointer(const u64 address)noexcept{
    return (address & (sizeof(void*) - 1u)) == 0u;
}

static void __hidden_append_callstack_frame(Detail::CrashDumpRequestOptions& options, const u64 address)noexcept{
    if(address == 0u || options.callstackFrameCount >= Detail::s_MaxCallstackFrames)
        return;
    if(options.callstackFrameCount != 0u && options.callstackFrames[options.callstackFrameCount - 1u] == address)
        return;

    options.callstackFrames[options.callstackFrameCount++] = address;
}

static void __hidden_capture_frame_pointer_callstack(
    Detail::CrashDumpRequestOptions& options,
    const u64 instructionPointer,
    const u64 stackPointer,
    const u64 framePointer
)noexcept{
    __hidden_append_callstack_frame(options, instructionPointer);

    if(stackPointer == 0u || framePointer == 0u || !__hidden_is_aligned_pointer(framePointer))
        return;
    if(framePointer < stackPointer || framePointer - stackPointer > s_MaxFramePointerWalkBytes)
        return;

    u64 currentFrame = framePointer;
    while(options.callstackFrameCount < Detail::s_MaxCallstackFrames){
        const auto* const frame = reinterpret_cast<const FramePointerRecord*>(static_cast<usize>(currentFrame));
        const u64 nextFrame = static_cast<u64>(reinterpret_cast<usize>(frame->previous));
        const u64 returnAddress = static_cast<u64>(reinterpret_cast<usize>(frame->returnAddress));
        __hidden_append_callstack_frame(options, returnAddress);

        if(nextFrame <= currentFrame || !__hidden_is_aligned_pointer(nextFrame))
            break;
        if(nextFrame < stackPointer || nextFrame - stackPointer > s_MaxFramePointerWalkBytes)
            break;

        currentFrame = nextFrame;
    }
}

static void __hidden_capture_signal_context(Detail::CrashDumpRequestOptions& options, const siginfo_t* signalInfo, const void* signalContext)noexcept{
    if(signalInfo)
        options.faultAddress = static_cast<u64>(reinterpret_cast<usize>(signalInfo->si_addr));

    const ucontext_t* context = static_cast<const ucontext_t*>(signalContext);
    if(!context)
        return;

#if defined(__x86_64__) && defined(REG_RIP) && defined(REG_RSP) && defined(REG_RBP)
    options.instructionPointer = static_cast<u64>(context->uc_mcontext.gregs[REG_RIP]);
    options.stackPointer = static_cast<u64>(context->uc_mcontext.gregs[REG_RSP]);
    options.framePointer = static_cast<u64>(context->uc_mcontext.gregs[REG_RBP]);
#elif defined(__aarch64__)
    options.instructionPointer = static_cast<u64>(context->uc_mcontext.pc);
    options.stackPointer = static_cast<u64>(context->uc_mcontext.sp);
    options.framePointer = static_cast<u64>(context->uc_mcontext.regs[s_AArch64FramePointerRegisterIndex]);
#endif

    __hidden_capture_frame_pointer_callstack(options, options.instructionPointer, options.stackPointer, options.framePointer);
}

static void __hidden_signal_handler(const int signalNumber, siginfo_t* signalInfo, void* signalContext)noexcept{
    Detail::CrashDumpRequestOptions options;
    __hidden_capture_signal_context(options, signalInfo, signalContext);

    Detail::NotifyCrashHandler(Detail::CrashReasonKind::PosixSignal, static_cast<u32>(signalNumber), options);

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
    static u8 s_signalStack[s_SignalStackSize] = {};

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
    AppendFixedBuffer(emergencyPath, "/");
    AppendFixedBuffer(emergencyPath, PackageNames::s_AndroidEmergencyRequestFileName);
    Detail::g_State.emergencyWriteFd = open(emergencyPath, O_CREAT | O_APPEND | O_WRONLY, s_AndroidEmergencyFileMode);
}
#endif

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
[[nodiscard]] static bool __hidden_move_fd_above_stdio(int& inOutFd)noexcept{
    if(inOutFd > STDERR_FILENO)
        return true;

    const int duplicatedFd = fcntl(inOutFd, F_DUPFD, s_FirstNonStdioFileDescriptor);
    if(duplicatedFd < 0)
        return false;

    close(inOutFd);
    inOutFd = duplicatedFd;
    return true;
}

static void __hidden_redirect_stdio_to_null()noexcept{
    const int nullFd = open("/dev/null", O_RDWR);
    if(nullFd < 0)
        return;

    static_cast<void>(dup2(nullFd, STDIN_FILENO));
    static_cast<void>(dup2(nullFd, STDOUT_FILENO));
    static_cast<void>(dup2(nullFd, STDERR_FILENO));
    if(nullFd > STDERR_FILENO)
        close(nullFd);
}

static void __hidden_silence_child_process(int& requestReadFd)noexcept{
    if(!__hidden_move_fd_above_stdio(requestReadFd))
        return;

    static_cast<void>(setsid());
    __hidden_redirect_stdio_to_null();
}

static void __hidden_wait_for_child_process(const pid_t pid)noexcept{
    if(pid <= 0)
        return;

    int status = 0;
    while(waitpid(pid, &status, 0) < 0){
        if(errno == EINTR)
            continue;
        break;
    }
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CaptureManualDumpContext(CrashDumpRequestOptions& outOptions, ManualDumpContextStorage& storage)noexcept{
    static_cast<void>(storage);

#if defined(__x86_64__)
    void* stackPointer = nullptr;
    __asm__ volatile("mov %%rsp, %0" : "=r"(stackPointer));
    outOptions.stackPointer = static_cast<u64>(reinterpret_cast<usize>(stackPointer));
    outOptions.framePointer = static_cast<u64>(reinterpret_cast<usize>(__builtin_frame_address(0)));
    outOptions.instructionPointer = static_cast<u64>(reinterpret_cast<usize>(__builtin_return_address(0)));
    __hidden_crash_posix::__hidden_capture_frame_pointer_callstack(
        outOptions,
        outOptions.instructionPointer,
        outOptions.stackPointer,
        outOptions.framePointer
    );
#elif defined(__aarch64__)
    void* stackPointer = nullptr;
    __asm__ volatile("mov %0, sp" : "=r"(stackPointer));
    outOptions.stackPointer = static_cast<u64>(reinterpret_cast<usize>(stackPointer));
    outOptions.framePointer = static_cast<u64>(reinterpret_cast<usize>(__builtin_frame_address(0)));
    outOptions.instructionPointer = static_cast<u64>(reinterpret_cast<usize>(__builtin_return_address(0)));
    __hidden_crash_posix::__hidden_capture_frame_pointer_callstack(
        outOptions,
        outOptions.instructionPointer,
        outOptions.stackPointer,
        outOptions.framePointer
    );
#endif
}

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

void NotifyCrashHandler(const CrashReasonKind::Enum reasonKind, const u32 reasonCode, const CrashDumpRequestOptions& options)noexcept{
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
    if(access(handlerExecutablePath.c_str(), X_OK) != 0)
        return false;

    int pipeFds[__hidden_crash_posix::s_PipeFileDescriptorCount] = { -1, -1 };
    if(pipe(pipeFds) != 0)
        return false;

    const pid_t pid = fork();
    if(pid < 0){
        close(pipeFds[__hidden_crash_posix::s_PipeReadEndIndex]);
        close(pipeFds[__hidden_crash_posix::s_PipeWriteEndIndex]);
        return false;
    }

    if(pid == 0){
        close(pipeFds[__hidden_crash_posix::s_PipeWriteEndIndex]);

        __hidden_crash_posix::__hidden_silence_child_process(pipeFds[__hidden_crash_posix::s_PipeReadEndIndex]);

        char fdText[s_HandlerArgumentTextCapacity] = {};
        AppendUnsignedToFixedBuffer(fdText, static_cast<u64>(pipeFds[__hidden_crash_posix::s_PipeReadEndIndex]));
        execl(handlerExecutablePath.c_str(), handlerExecutablePath.c_str(), s_RequestFdArgument, fdText, nullptr);
        _exit(__hidden_crash_posix::s_HandlerExecFailureExitCode);
    }

    close(pipeFds[__hidden_crash_posix::s_PipeReadEndIndex]);
    g_State.requestWriteFd = pipeFds[__hidden_crash_posix::s_PipeWriteEndIndex];
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
    const pid_t handlerPid = g_State.handlerPid;
    if(g_State.requestWriteFd >= 0){
        close(g_State.requestWriteFd);
        g_State.requestWriteFd = -1;
    }
    __hidden_crash_posix::__hidden_wait_for_child_process(handlerPid);
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

