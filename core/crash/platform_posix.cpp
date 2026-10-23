// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"

#include <global/algorithm.h>
#include <global/blocking_io.h>
#include <global/process_execution.h>
#include <global/scope_exit.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <execinfo.h>
#include <sys/socket.h>
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
inline constexpr u64 s_HandlerExitWaitMilliseconds = 3000u;
inline constexpr usize s_AArch64FramePointerRegisterIndex = 29u;
inline constexpr usize s_MaxFramePointerWalkBytes = 8u * 1024u * 1024u;

alignas(MaxAlign) static u8 g_SignalStack[s_SignalStackSize] = {};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct FramePointerRecord{
    const FramePointerRecord* previous = nullptr;
    const void* returnAddress = nullptr;
};


[[nodiscard]] static CrashDetail::PlatformFailure NativeFailure(const CrashDetail::PlatformOperation::Enum operation, const int code)noexcept{
    return { operation, CrashDetail::PlatformErrorDomain::Posix, static_cast<u64>(code) };
}

[[nodiscard]] static Expected<void, CrashDetail::PlatformFailure> ReleaseOwnedDescriptor(int& descriptor)noexcept{
    const int owned = descriptor;
    descriptor = -1;
    if(owned < 0)
        return {};
    // Linux/Android release the descriptor even on EINTR: retrying could close a reused descriptor.
    if(close(owned) != 0)
        return MakeUnexpected(NativeFailure(CrashDetail::PlatformOperation::CloseDescriptor, errno));
    return {};
}

static void CleanupOwnedDescriptor(int& descriptor, CrashDetail::PlatformCleanupObservations& observations)noexcept{
    const auto released = ReleaseOwnedDescriptor(descriptor);
    if(!released)
        observations.record(released.error());
}

static void AppendCallstackFrame(CrashDetail::CrashDumpRequestOptions& options, const u64 address)noexcept{
    if(address == 0u || options.callstackFrameCount >= CrashDetail::s_MaxCallstackFrames)
        return;
    if(options.callstackFrameCount != 0u && options.callstackFrames[options.callstackFrameCount - 1u] == address)
        return;

    options.callstackFrames[options.callstackFrameCount++] = address;
}

static void CaptureFramePointerCallstack(CrashDetail::CrashDumpRequestOptions& options, const u64 instructionPointer, const u64 stackPointer, const u64 framePointer)noexcept{
    AppendCallstackFrame(options, instructionPointer);

    if(stackPointer == 0u || framePointer == 0u || !IsAligned(framePointer, static_cast<u64>(sizeof(void*))))
        return;
    if(framePointer < stackPointer || framePointer - stackPointer > s_MaxFramePointerWalkBytes)
        return;

    u64 currentFrame = framePointer;
    while(options.callstackFrameCount < CrashDetail::s_MaxCallstackFrames){
        const auto* const frame = reinterpret_cast<const FramePointerRecord*>(static_cast<usize>(currentFrame));
        const u64 nextFrame = static_cast<u64>(reinterpret_cast<usize>(frame->previous));
        const u64 returnAddress = static_cast<u64>(reinterpret_cast<usize>(frame->returnAddress));
        AppendCallstackFrame(options, returnAddress);

        if(nextFrame <= currentFrame || !IsAligned(nextFrame, static_cast<u64>(sizeof(void*))))
            break;
        if(nextFrame < stackPointer || nextFrame - stackPointer > s_MaxFramePointerWalkBytes)
            break;

        currentFrame = nextFrame;
    }
}

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
// backtrace() unwinds via .eh_frame without frame pointers; warmed up at install so the crash-time call stays allocation-free.
// Pre-2.35 glibc may take the loader lock (dl_iterate_phdr); newer glibc uses lock-free __dl_find_object.
// When the trigger IP is known, capture starts there (robust to inlining); otherwise only that IP is emitted.
// Returns false when nothing unwound so the caller falls back to the frame-pointer walk.
[[nodiscard]] static bool CaptureEhFrameCallstack(CrashDetail::CrashDumpRequestOptions& options, const u64 alignmentInstructionPointer)noexcept{
    void* unwoundAddresses[CrashDetail::s_MaxCallstackFrames];
    const int unwoundFrameCount = backtrace(unwoundAddresses, static_cast<int>(CrashDetail::s_MaxCallstackFrames));
    if(unwoundFrameCount <= 0)
        return false;

    if(alignmentInstructionPointer != 0u){
        for(int i = 0; i < unwoundFrameCount; ++i){
            if(static_cast<u64>(reinterpret_cast<usize>(unwoundAddresses[i])) != alignmentInstructionPointer)
                continue;

            for(usize frame = static_cast<usize>(i); frame < static_cast<usize>(unwoundFrameCount); ++frame)
                AppendCallstackFrame(options, static_cast<u64>(reinterpret_cast<usize>(unwoundAddresses[frame])));
            return options.callstackFrameCount != 0u;
        }

        AppendCallstackFrame(options, alignmentInstructionPointer);
        return true;
    }

    for(int i = 0; i < unwoundFrameCount; ++i)
        AppendCallstackFrame(options, static_cast<u64>(reinterpret_cast<usize>(unwoundAddresses[i])));
    return options.callstackFrameCount != 0u;
}

static void WarmUpUnwinder()noexcept{
    void* warmUpAddresses[CrashDetail::s_MaxCallstackFrames];
    const int warmedFrameCount = backtrace(warmUpAddresses, static_cast<int>(CrashDetail::s_MaxCallstackFrames));
    if(warmedFrameCount <= 0)
        return;
}
#endif

static void CaptureCurrentCallstack(CrashDetail::CrashDumpRequestOptions& outOptions, const void* const stackPointer, const void* const framePointer, const void* const instructionPointer)noexcept{
    outOptions.stackPointer = static_cast<u64>(reinterpret_cast<usize>(stackPointer));
    outOptions.framePointer = static_cast<u64>(reinterpret_cast<usize>(framePointer));
    outOptions.instructionPointer = static_cast<u64>(reinterpret_cast<usize>(instructionPointer));
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    const u64 alignmentInstructionPointer = outOptions.triggerInstructionPointer != 0u ? outOptions.triggerInstructionPointer : outOptions.instructionPointer;
    if(CaptureEhFrameCallstack(outOptions, alignmentInstructionPointer)){
        // backtrace already trimmed crash-handler-internal frames, so the downstream skip must not drop user frames.
        outOptions.callstackFramesToSkip = 0u;
        return;
    }
#endif
    CaptureFramePointerCallstack(outOptions, outOptions.instructionPointer, outOptions.stackPointer, outOptions.framePointer);
}

static void CaptureSignalContext(CrashDetail::CrashDumpRequestOptions& options, const siginfo_t* signalInfo, const void* signalContext)noexcept{
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

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    // backtrace() crosses the signal trampoline into the interrupted frame, so the fault IP aligns the trace even in frame-pointer-omitting final builds.
    if(CaptureEhFrameCallstack(options, options.instructionPointer))
        return;
#endif
    CaptureFramePointerCallstack(options, options.instructionPointer, options.stackPointer, options.framePointer);
}

static void SignalHandler(const int signalNumber, siginfo_t* signalInfo, void* signalContext)noexcept{
    if(!CrashDetail::TryConsumeSuppressedPlatformCrashCapture()){
        CrashDetail::CrashDumpRequestOptions options;
        CaptureSignalContext(options, signalInfo, signalContext);
        CrashDetail::NotifyCrashHandler(CrashDetail::CrashReasonKind::PosixSignal, static_cast<u32>(signalNumber), options);
    }

    struct sigaction action = {};
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    sigaction(signalNumber, &action, nullptr);
    raise(signalNumber);
}

[[noreturn]] static void TerminateHandler()noexcept{
    CrashDetail::CrashDumpRequestOptions options;
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    // std::terminate runs in a normal thread context, so unwind a callstack directly instead of relying on the follow-up SIGABRT to carry it.
    if(!CaptureEhFrameCallstack(options, 0u))
        options.callstackFrameCount = 0u;
#endif
    CrashDetail::NotifyCrashHandler(CrashDetail::CrashReasonKind::Terminate, 0u, options);
    abort();
}

[[nodiscard]] static Expected<void, CrashDetail::PlatformFailure> InstallSignalHandlers()noexcept{
    stack_t stack = {};
    stack.ss_sp = g_SignalStack;
    stack.ss_size = sizeof(g_SignalStack);
    if(sigaltstack(&stack, &CrashDetail::g_State.previousSignalStack) != 0)
        return MakeUnexpected(NativeFailure(CrashDetail::PlatformOperation::InstallSignalStack, errno));
    CrashDetail::g_State.signalStackInstalled = true;
    CrashDetail::g_State.signalStackOwner = QueryCurrentThreadId();

    struct sigaction action = {};
    action.sa_sigaction = SignalHandler;
    if(sigemptyset(&action.sa_mask) != 0)
        return MakeUnexpected(NativeFailure(CrashDetail::PlatformOperation::InstallSignalAction, errno));
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    constexpr int s_Signals[] = {
        SIGSEGV, SIGABRT, SIGFPE, SIGILL,
#if defined(SIGBUS)
        SIGBUS,
#endif
#if defined(SIGTRAP)
        SIGTRAP,
#endif
    };
    for(usize index = 0u; index < sizeof(s_Signals) / sizeof(s_Signals[0u]); ++index){
        if(sigaction(s_Signals[index], &action, &CrashDetail::g_State.previousSignalActions[index]) != 0)
            return MakeUnexpected(NativeFailure(CrashDetail::PlatformOperation::InstallSignalAction, errno));
        CrashDetail::g_State.installedSignals[index] = s_Signals[index];
        CrashDetail::g_State.signalActionInstalled[index] = true;
    }
    return {};
}

#if defined(NWB_PLATFORM_ANDROID)
[[nodiscard]] static Expected<void, CrashDetail::PlatformFailure> OpenAndroidEmergencyFile()noexcept{
    const AStringView directory(CrashDetail::g_State.spoolDirectoryText);
    const AStringView filename(PackageNames::s_AndroidEmergencyRequestFileName);
    char emergencyPath[CrashDetail::s_MaxPathText] = {};
    if(directory.empty() || directory.size() + 1u + filename.size() >= sizeof(emergencyPath))
        return MakeUnexpected(NativeFailure(CrashDetail::PlatformOperation::OpenEmergencyFile, ENAMETOOLONG));
    CopyFixedBuffer(emergencyPath, directory);
    AppendFixedBuffer(emergencyPath, "/");
    AppendFixedBuffer(emergencyPath, filename);
    const int descriptor = open(emergencyPath, O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, s_AndroidEmergencyFileMode);
    if(descriptor < 0)
        return MakeUnexpected(NativeFailure(CrashDetail::PlatformOperation::OpenEmergencyFile, errno));
    CrashDetail::g_State.emergencyWriteFd = descriptor;
    return {};
}
#endif

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
[[nodiscard]] static bool MoveFdAboveStdio(int& descriptor)noexcept{
    if(descriptor > STDERR_FILENO)
        return true;
    const int duplicated = fcntl(descriptor, F_DUPFD, s_FirstNonStdioFileDescriptor);
    if(duplicated < 0)
        return false;
    const auto released = ReleaseOwnedDescriptor(descriptor);
    descriptor = duplicated;
    if(!released){
        errno = static_cast<int>(released.error().code);
        return false;
    }
    return true;
}

[[nodiscard]] static bool SetCloseOnExec(const int descriptor)noexcept{
    const int flags = fcntl(descriptor, F_GETFD, 0);
    return flags >= 0 && fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
}

[[nodiscard]] static bool SendAllSocketNoSigpipe(const int descriptor, const void* const data, const usize byteCount)noexcept{
    return TransferAllPosix(static_cast<const u8*>(data), byteCount, [descriptor](const u8* const cursor, const usize remaining)noexcept{
        return send(descriptor, cursor, remaining, MSG_NOSIGNAL);
    }
    );
}

[[noreturn]] static void FailChild(const int startupDescriptor, const int error)noexcept{
    const auto* cursor = reinterpret_cast<const u8*>(&error);
    usize remaining = sizeof(error);
    while(remaining != 0u){
        const ssize_t written = write(startupDescriptor, cursor, remaining);
        if(written < 0 && errno == EINTR)
            continue;
        if(written <= 0)
            _exit(s_HandlerExecFailureExitCode);
        cursor += static_cast<usize>(written);
        remaining -= static_cast<usize>(written);
    }
    _exit(s_HandlerExecFailureExitCode);
}

[[nodiscard]] static bool SilenceChildProcess(int& requestRead, int& ackWrite, int& startupWrite)noexcept{
    if(!MoveFdAboveStdio(requestRead) || !MoveFdAboveStdio(ackWrite) || !MoveFdAboveStdio(startupWrite) || !SetCloseOnExec(startupWrite))
        return false;
    if(setsid() < 0)
        return false;
    constexpr AStringView s_NullDevicePath = "/dev/null";

    int nullDescriptor = open(s_NullDevicePath.data(), O_RDWR);
    if(nullDescriptor < 0)
        return false;
    if(dup2(nullDescriptor, STDIN_FILENO) < 0 || dup2(nullDescriptor, STDOUT_FILENO) < 0 || dup2(nullDescriptor, STDERR_FILENO) < 0)
        return false;
    if(nullDescriptor > STDERR_FILENO){
        const auto released = ReleaseOwnedDescriptor(nullDescriptor);
        if(!released){
            errno = static_cast<int>(released.error().code);
            return false;
        }
    }
    return true;
}

static void DrainPendingAcks(const int ackReadFd)noexcept{
    if(ackReadFd < 0)
        return;

    for(;;){
        pollfd pollFd = {};
        pollFd.fd = ackReadFd;
        pollFd.events = POLLIN;

        const int pollResult = poll(&pollFd, 1u, 0);
        if(pollResult <= 0)
            return;
        if((pollFd.revents & POLLIN) == 0)
            return;

        CrashDetail::CrashAck ignoredAck;
        if(!ReadAllFileDescriptor(ackReadFd, &ignoredAck, sizeof(ignoredAck)))
            return;
    }
}

[[nodiscard]] static CrashDetail::CrashDumpTransportStatus::Enum WaitForAck(const int ackReadFd, const CrashDetail::CrashRequest& request, const u32 waitMilliseconds)noexcept{
    if(ackReadFd < 0 || waitMilliseconds == 0u)
        return CrashDetail::CrashDumpTransportStatus::Sent;

    const auto now = ProcessExecutionDetail::MonotonicMilliseconds();
    if(!now || static_cast<u64>(waitMilliseconds) > Limit<u64>::s_Max - *now)
        return CrashDetail::CrashDumpTransportStatus::Failed;
    const u64 deadline = *now + waitMilliseconds;

    for(;;){
        pollfd pollFd = {};
        pollFd.fd = ackReadFd;
        pollFd.events = POLLIN;

        const auto remainingTimeout = ProcessExecutionDetail::RemainingTimeoutMilliseconds(deadline);
        if(!remainingTimeout)
            return CrashDetail::CrashDumpTransportStatus::Failed;
        const int pollResult = poll(&pollFd, 1u, *remainingTimeout);
        if(pollResult == 0)
            return CrashDetail::CrashDumpTransportStatus::TimedOut;
        if(pollResult < 0){
            if(errno == EINTR)
                continue;
            return CrashDetail::CrashDumpTransportStatus::Failed;
        }

        if((pollFd.revents & POLLIN) == 0){
            if((pollFd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                return CrashDetail::CrashDumpTransportStatus::Failed;
            continue;
        }

        CrashDetail::CrashAck ack;
        if(!ReadAllFileDescriptor(ackReadFd, &ack, sizeof(ack)))
            return CrashDetail::CrashDumpTransportStatus::Failed;
        if(CrashDetail::CrashAckMatchesRequest(ack, request))
            return ack.packageWritten ? CrashDetail::CrashDumpTransportStatus::PackageWritten : CrashDetail::CrashDumpTransportStatus::PackageWriteFailed;
        const auto expired = ProcessExecutionDetail::TimeoutExpired(deadline);
        if(!expired)
            return CrashDetail::CrashDumpTransportStatus::Failed;
        if(*expired)
            return CrashDetail::CrashDumpTransportStatus::TimedOut;
    }
}

static void WaitForOwnedChild(pid_t& child, const bool terminateImmediately, CrashDetail::PlatformCleanupObservations& observations)noexcept{
    if(child <= 0)
        return;
    constexpr u32 s_WaitPollCount = 300u;
    constexpr int s_WaitPollMilliseconds = 10;
    constexpr u32 s_WaitPhaseCount = 2u;
    bool terminated = terminateImmediately;
    for(u32 phase = 0u; phase < s_WaitPhaseCount; ++phase){
        if(phase != 0u || terminateImmediately){
            if(kill(child, SIGKILL) != 0 && errno != ESRCH){
                observations.record(NativeFailure(CrashDetail::PlatformOperation::KillChild, errno));
                return;
            }
            terminated = true;
        }
        for(u32 attempt = 0u; attempt < s_WaitPollCount; ++attempt){
            int status = 0;
            const pid_t waited = waitpid(child, &status, WNOHANG);
            if(waited == child){
                child = -1;
                if(!terminated && WIFEXITED(status) && WEXITSTATUS(status) != 0)
                    observations.record({ CrashDetail::PlatformOperation::WaitChild, CrashDetail::PlatformErrorDomain::ExitCode, static_cast<u64>(WEXITSTATUS(status)) });
                else if(!terminated && WIFSIGNALED(status))
                    observations.record({ CrashDetail::PlatformOperation::WaitChild, CrashDetail::PlatformErrorDomain::Signal, static_cast<u64>(WTERMSIG(status)) });
                return;
            }
            if(waited < 0 && errno != EINTR){
                const int error = errno;
                if(error == ECHILD)
                    child = -1;
                observations.record(NativeFailure(CrashDetail::PlatformOperation::WaitChild, error));
                return;
            }
            if(poll(nullptr, 0u, s_WaitPollMilliseconds) < 0 && errno != EINTR){
                observations.record(NativeFailure(CrashDetail::PlatformOperation::WaitChild, errno));
                break;
            }
        }
        observations.record(NativeFailure(CrashDetail::PlatformOperation::WaitChild, ETIMEDOUT));
        if(terminated)
            return;
    }
}

#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CaptureManualDumpContext(CrashDumpRequestOptions& outOptions, ManualDumpContextStorage& storage)noexcept{
    static_cast<void>(storage);

#if defined(__x86_64__) || defined(__aarch64__)
    void* stackPointer = nullptr;
#if defined(__x86_64__)
    __asm__ volatile("mov %%rsp, %0" : "=r"(stackPointer));
#else
    __asm__ volatile("mov %0, sp" : "=r"(stackPointer));
#endif
    __hidden_crash_posix::CaptureCurrentCallstack(outOptions, stackPointer, __builtin_frame_address(0), __builtin_return_address(0));
#endif
}

CrashDumpTransportStatus::Enum RequestCrashHandler(const CrashRequest& request, const u32 waitMilliseconds)noexcept{
    static_cast<void>(waitMilliseconds);

    // Re-entrancy guard + writer serialization, matching Win32: a fault inside the transport bails, and one faulting thread owns the channel.
    // Async-safe (atomic CAS, no Futex).
    static thread_local bool s_InTransport = false;
    if(s_InTransport)
        return CrashDumpTransportStatus::Failed;
    s_InTransport = true;

    u32 expected = 0u;
    if(!g_State.transportInFlight.compare_exchange_strong(expected, 1u, MemoryOrder::acq_rel, MemoryOrder::acquire)){
        s_InTransport = false;
        return CrashDumpTransportStatus::Sent;
    }

    CrashDumpTransportStatus::Enum status = CrashDumpTransportStatus::Failed;
#if defined(NWB_PLATFORM_ANDROID)
    if(g_State.emergencyWriteFd >= 0)
        status = WriteAllFileDescriptor(g_State.emergencyWriteFd, &request, sizeof(request)) ? CrashDumpTransportStatus::Sent : CrashDumpTransportStatus::Failed;
#elif defined(NWB_PLATFORM_LINUX)
    if(g_State.requestWriteFd >= 0){
        __hidden_crash_posix::DrainPendingAcks(g_State.ackReadFd);
        if(!__hidden_crash_posix::SendAllSocketNoSigpipe(g_State.requestWriteFd, &request, sizeof(request)))
            status = CrashDumpTransportStatus::Failed;
        else
            status = __hidden_crash_posix::WaitForAck(g_State.ackReadFd, request, waitMilliseconds);
    }
#endif

    g_State.transportInFlight.store(0u, MemoryOrder::release);
    s_InTransport = false;
    return status;
}

template<typename ArenaT>
Expected<void, PlatformFailure> StartDesktopHandler(const ::Path<ArenaT>& handlerExecutablePath, Alloc::ScratchArena& scratchArena, PlatformCleanupObservations& observations)noexcept{
    static_cast<void>(scratchArena);
#if defined(NWB_PLATFORM_ANDROID)
    static_cast<void>(handlerExecutablePath);
    static_cast<void>(observations);
    return {};
#elif defined(NWB_PLATFORM_LINUX)
    using namespace __hidden_crash_posix;
    if(g_State.handlerStarted)
        return {};
    if(handlerExecutablePath.empty())
        return MakeUnexpected(NativeFailure(PlatformOperation::ExecHandler, EINVAL));
    if(access(handlerExecutablePath.c_str(), X_OK) != 0)
        return MakeUnexpected(NativeFailure(PlatformOperation::ExecHandler, errno));
    int request[2u] = { -1, -1 };
    int ack[2u] = { -1, -1 };
    int startup[2u] = { -1, -1 };
    pid_t child = -1;
    ScopeExit rollback([&]()noexcept{
        for(int& descriptor : request)
            CleanupOwnedDescriptor(descriptor, observations);
        for(int& descriptor : ack)
            CleanupOwnedDescriptor(descriptor, observations);
        for(int& descriptor : startup)
            CleanupOwnedDescriptor(descriptor, observations);
        WaitForOwnedChild(child, true, observations);
        if(child > 0){
            g_State.handlerPid = child;
            g_State.platformRestorePending = true;
        }
    }
    );
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, request) != 0)
        return MakeUnexpected(NativeFailure(PlatformOperation::CreateRequestChannel, errno));
    if(pipe(ack) != 0)
        return MakeUnexpected(NativeFailure(PlatformOperation::CreateAckChannel, errno));
    if(pipe(startup) != 0)
        return MakeUnexpected(NativeFailure(PlatformOperation::CreateStartupChannel, errno));
    if(!SetCloseOnExec(request[0u]) || !SetCloseOnExec(ack[0u]) || !SetCloseOnExec(startup[0u]) || !SetCloseOnExec(startup[1u]))
        return MakeUnexpected(NativeFailure(PlatformOperation::SetDescriptorFlags, errno));
    child = fork();
    if(child < 0)
        return MakeUnexpected(NativeFailure(PlatformOperation::CreateProcess, errno));
    if(child == 0){
        for(int* descriptor : { &request[0u], &ack[0u], &startup[0u] }){
            const auto released = ReleaseOwnedDescriptor(*descriptor);
            if(!released)
                FailChild(startup[1u], static_cast<int>(released.error().code));
        }
        if(!SilenceChildProcess(request[1u], ack[1u], startup[1u]))
            FailChild(startup[1u], errno);
        char requestText[s_HandlerArgumentTextCapacity] = {};
        char ackText[s_HandlerArgumentTextCapacity] = {};
        AppendUnsignedToFixedBuffer(requestText, static_cast<u64>(request[1u]));
        AppendUnsignedToFixedBuffer(ackText, static_cast<u64>(ack[1u]));
        execl(handlerExecutablePath.c_str(), handlerExecutablePath.c_str(), s_RequestFdArgument.data(), requestText, s_AckFdArgument.data(), ackText, nullptr);
        FailChild(startup[1u], errno);
    }
    for(int* descriptor : { &request[1u], &ack[1u], &startup[1u] }){
        const auto released = ReleaseOwnedDescriptor(*descriptor);
        if(!released)
            return MakeUnexpected(released.error());
    }
    const auto started = ProcessExecutionDetail::MonotonicMilliseconds();
    if(!started || s_HandlerExitWaitMilliseconds > Limit<u64>::s_Max - *started)
        return MakeUnexpected(NativeFailure(PlatformOperation::ExecHandler, !started ? static_cast<int>(started.error()) : EOVERFLOW));
    const u64 deadline = *started + s_HandlerExitWaitMilliseconds;
    int childError = 0;
    usize received = 0u;
    for(;;){
        pollfd ready = { startup[0u], POLLIN, 0 };
        const auto remaining = ProcessExecutionDetail::RemainingTimeoutMilliseconds(deadline);
        if(!remaining)
            return MakeUnexpected(NativeFailure(PlatformOperation::ExecHandler, static_cast<int>(remaining.error())));
        const int polled = poll(&ready, 1u, *remaining);
        if(polled < 0 && errno == EINTR)
            continue;
        if(polled <= 0)
            return MakeUnexpected(NativeFailure(PlatformOperation::ExecHandler, polled == 0 ? ETIMEDOUT : errno));
        if((ready.revents & POLLNVAL) != 0)
            return MakeUnexpected(NativeFailure(PlatformOperation::ExecHandler, EBADF));
        const ssize_t count = read(startup[0u], reinterpret_cast<u8*>(&childError) + received, sizeof(childError) - received);
        if(count < 0 && errno == EINTR)
            continue;
        if(count < 0)
            return MakeUnexpected(NativeFailure(PlatformOperation::ExecHandler, errno));
        if(count == 0){
            if(received != 0u)
                return MakeUnexpected(NativeFailure(PlatformOperation::ExecHandler, EIO));
            break;
        }
        received += static_cast<usize>(count);
        if(received == sizeof(childError))
            return MakeUnexpected(NativeFailure(PlatformOperation::ExecHandler, childError));
    }
    const auto startupReleased = ReleaseOwnedDescriptor(startup[0u]);
    if(!startupReleased)
        return MakeUnexpected(startupReleased.error());
    int childStatus = 0;
    const pid_t earlyExit = waitpid(child, &childStatus, WNOHANG);
    if(earlyExit == child){
        child = -1;
        if(WIFSIGNALED(childStatus))
            return MakeUnexpected(PlatformFailure{ PlatformOperation::ExecHandler, PlatformErrorDomain::Signal, static_cast<u64>(WTERMSIG(childStatus)) });
        return MakeUnexpected(PlatformFailure{ PlatformOperation::ExecHandler, PlatformErrorDomain::ExitCode, WIFEXITED(childStatus) ? static_cast<u64>(WEXITSTATUS(childStatus)) : 0u });
    }
    if(earlyExit < 0){
        const int error = errno;
        if(error == ECHILD)
            child = -1;
        return MakeUnexpected(NativeFailure(PlatformOperation::WaitChild, error));
    }
    g_State.requestWriteFd = request[0u];
    g_State.ackReadFd = ack[0u];
    g_State.handlerPid = child;
    g_State.handlerStarted = true;
    rollback.release();
    return {};
#else
    static_cast<void>(handlerExecutablePath);
    static_cast<void>(observations);
    return MakeUnexpected(PlatformFailure{ PlatformOperation::CreateProcess, PlatformErrorDomain::Contract, 1u });
#endif
}

template Expected<void, PlatformFailure> StartDesktopHandler(const ::Path<Alloc::ScratchArena>& handlerExecutablePath, Alloc::ScratchArena& scratchArena, PlatformCleanupObservations& observations)noexcept;

Expected<void, PlatformFailure> InstallPlatformHandlers()noexcept{
#if defined(NWB_PLATFORM_ANDROID)
    const auto emergency = __hidden_crash_posix::OpenAndroidEmergencyFile();
    if(!emergency)
        return MakeUnexpected(emergency.error());
#elif defined(NWB_PLATFORM_LINUX)
    __hidden_crash_posix::WarmUpUnwinder();
#endif
    const auto signals = __hidden_crash_posix::InstallSignalHandlers();
    if(!signals)
        return MakeUnexpected(signals.error());
    g_State.previousTerminateHandler = ExchangeTerminateHandler(__hidden_crash_posix::TerminateHandler);
    g_State.terminateHandlerInstalled = true;
    return {};
}

void UninstallPlatformResources(PlatformCleanupObservations& observations)noexcept{
    using namespace __hidden_crash_posix;
#if defined(NWB_PLATFORM_ANDROID)
    CleanupOwnedDescriptor(g_State.emergencyWriteFd, observations);
#elif defined(NWB_PLATFORM_LINUX)
    CleanupOwnedDescriptor(g_State.requestWriteFd, observations);
    WaitForOwnedChild(g_State.handlerPid, false, observations);
    CleanupOwnedDescriptor(g_State.ackReadFd, observations);
#endif
    g_State.handlerStarted = false;
    if(g_State.terminateHandlerInstalled){
        if(QueryTerminateHandler() == TerminateHandler)
            ExchangeTerminateHandler(g_State.previousTerminateHandler);
        g_State.terminateHandlerInstalled = false;
        g_State.previousTerminateHandler = nullptr;
    }
    bool pending = false;
    for(usize remaining = s_MaxCrashSignals; remaining != 0u; --remaining){
        const usize index = remaining - 1u;
        if(!g_State.signalActionInstalled[index])
            continue;
        struct sigaction current = {};
        if(sigaction(g_State.installedSignals[index], nullptr, &current) != 0){
            observations.record(NativeFailure(PlatformOperation::RestoreSignalAction, errno));
            pending = true;
            continue;
        }
        if((current.sa_flags & SA_SIGINFO) != 0 && current.sa_sigaction == SignalHandler){
            if(sigaction(g_State.installedSignals[index], &g_State.previousSignalActions[index], nullptr) != 0){
                observations.record(NativeFailure(PlatformOperation::RestoreSignalAction, errno));
                pending = true;
                continue;
            }
        }
        g_State.signalActionInstalled[index] = false;
    }
    if(g_State.signalStackInstalled){
        stack_t current = {};
        if(g_State.signalStackOwner != QueryCurrentThreadId()){
            observations.record(NativeFailure(PlatformOperation::RestoreSignalStack, EXDEV));
            pending = true;
        }
        else if(sigaltstack(nullptr, &current) != 0){
            observations.record(NativeFailure(PlatformOperation::RestoreSignalStack, errno));
            pending = true;
        }
        else if((current.ss_flags & SS_DISABLE) == 0 && current.ss_sp == g_SignalStack){
            if(sigaltstack(&g_State.previousSignalStack, nullptr) != 0){
                observations.record(NativeFailure(PlatformOperation::RestoreSignalStack, errno));
                pending = true;
            }
            else
                g_State.signalStackInstalled = false;
        }
        else
            g_State.signalStackInstalled = false;
    }
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
    pending = pending || g_State.handlerPid > 0;
#endif
    g_State.platformRestorePending = pending;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

