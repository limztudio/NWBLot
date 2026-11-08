// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"
#include "posix_callstack_capture.h"

#include <global/algorithm.h>
#include <global/blocking_io.h>
#include <global/process_capture.h>
#include <global/process_execution.h>
#include <global/scope_exit.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <global/posix/descriptor.h>
#include <global/posix/socket_io.h>
#endif

#include <fcntl.h>
#include <poll.h>
#include <ucontext.h>
#include <unistd.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
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

alignas(MaxAlign) static Array<u8, s_SignalStackSize> g_SignalStack = {};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

[[nodiscard]] static Expected<void, CrashDetail::PlatformFailure> InstallSignalHandlers()noexcept{
    stack_t stack = {};
    stack.ss_sp = g_SignalStack.data();
    stack.ss_size = sizeof(g_SignalStack);
    if(sigaltstack(&stack, &CrashDetail::g_State.previousSignalStack) != 0)
        return MakeUnexpected(NativeFailure(CrashDetail::PlatformOperation::InstallSignalStack, errno));
    CrashDetail::g_State.signalStackInstalled = true;
    CrashDetail::g_State.signalStackOwner = QueryCurrentThreadId();

    struct sigaction action = {};
    action.sa_sigaction = PosixCallstackCapture::SignalHandler;
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
    const AStringView directory(CrashDetail::g_State.spoolDirectoryText.data());
    const AStringView filename(PackageNames::s_AndroidEmergencyRequestFileName);
    Array<char, CrashDetail::s_MaxPathText> emergencyPath = {};
    if(directory.empty() || directory.size() + 1u + filename.size() >= sizeof(emergencyPath))
        return MakeUnexpected(NativeFailure(CrashDetail::PlatformOperation::OpenEmergencyFile, ENAMETOOLONG));
    CopyFixedBuffer(emergencyPath, directory);
    AppendFixedBuffer(emergencyPath, "/");
    AppendFixedBuffer(emergencyPath, filename);
    const int descriptor = open(emergencyPath.data(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC, s_AndroidEmergencyFileMode);
    if(descriptor < 0)
        return MakeUnexpected(NativeFailure(CrashDetail::PlatformOperation::OpenEmergencyFile, errno));
    CrashDetail::g_State.emergencyWriteFd = descriptor;
    return {};
}
#endif

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
[[noreturn]] static void FailChild(const int startupDescriptor, const int error)noexcept{
    if(!WriteAllFileDescriptor(startupDescriptor, &error, sizeof(error)))
        _exit(s_HandlerExecFailureExitCode);
    _exit(s_HandlerExecFailureExitCode);
}

[[nodiscard]] static b8 SilenceChildProcess(int& requestRead, int& ackWrite, int& startupWrite)noexcept{
    if(!MoveFdAboveStdio(requestRead, s_FirstNonStdioFileDescriptor) || !MoveFdAboveStdio(ackWrite, s_FirstNonStdioFileDescriptor) || !MoveFdAboveStdio(startupWrite, s_FirstNonStdioFileDescriptor) || !SetCloseOnExec(startupWrite))
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
        if(CrashDetail::CrashProtocol::CrashAckMatchesRequest(ack, request))
            return ack.packageWritten ? CrashDetail::CrashDumpTransportStatus::PackageWritten : CrashDetail::CrashDumpTransportStatus::PackageWriteFailed;
        const auto expired = ProcessExecutionDetail::TimeoutExpired(deadline);
        if(!expired)
            return CrashDetail::CrashDumpTransportStatus::Failed;
        if(*expired)
            return CrashDetail::CrashDumpTransportStatus::TimedOut;
    }
}

static void WaitForOwnedChild(const b8 terminateImmediately, pid_t& child, CrashDetail::PlatformCleanupObservations& observations)noexcept{
    if(child <= 0)
        return;
    constexpr u32 s_WaitPollCount = 300u;
    constexpr int s_WaitPollMilliseconds = 10;
    constexpr u32 s_WaitPhaseCount = 2u;
    b8 terminated = terminateImmediately;
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


void CaptureManualDumpContext([[maybe_unused]] CrashDumpRequestOptions& outOptions, [[maybe_unused]] ManualDumpContextStorage& storage)noexcept{
#if defined(__x86_64__) || defined(__aarch64__)
    void* stackPointer = nullptr;
#if defined(__x86_64__)
    __asm__ volatile("mov %%rsp, %0" : "=r"(stackPointer));
#else
    __asm__ volatile("mov %0, sp" : "=r"(stackPointer));
#endif
    PosixCallstackCapture::CaptureCurrent(outOptions, stackPointer, __builtin_frame_address(0), __builtin_return_address(0));
#endif
}

CrashDumpTransportStatus::Enum RequestCrashHandler(const CrashRequest& request, [[maybe_unused]] const u32 waitMilliseconds)noexcept{
    // Re-entrancy guard + writer serialization, matching Win32: a fault inside the transport bails, and one faulting thread owns the channel.
    // Async-safe (atomic CAS, no Futex).
    static thread_local b8 s_InTransport = false;
    if(s_InTransport)
        return CrashDumpTransportStatus::Failed;
    s_InTransport = true;

    u32 expected = 0u;
    if(!g_State.transportInFlight.compare_exchange_strong(expected, 1u, MemoryOrder::acq_rel, MemoryOrder::acquire)){
        s_InTransport = false;
        return CrashDumpTransportStatus::Failed;
    }

    CrashDumpTransportStatus::Enum status = CrashDumpTransportStatus::Failed;
#if defined(NWB_PLATFORM_ANDROID)
    if(g_State.emergencyWriteFd >= 0)
        status = WriteAllFileDescriptor(g_State.emergencyWriteFd, &request, sizeof(request)) ? CrashDumpTransportStatus::Sent : CrashDumpTransportStatus::Failed;
#elif defined(NWB_PLATFORM_LINUX)
    if(g_State.requestWriteFd >= 0){
        __hidden_crash_posix::DrainPendingAcks(g_State.ackReadFd);
        if(!SendAllSocketNoSigpipe(g_State.requestWriteFd, &request, sizeof(request)))
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
Expected<void, PlatformFailure> StartDesktopHandler([[maybe_unused]] const ::Path<ArenaT>& handlerExecutablePath, [[maybe_unused]] Alloc::ScratchArena& scratchArena, [[maybe_unused]] PlatformCleanupObservations& observations)noexcept{
#if defined(NWB_PLATFORM_ANDROID)
    return {};
#elif defined(NWB_PLATFORM_LINUX)
    using namespace __hidden_crash_posix;
    if(g_State.handlerStarted)
        return {};
    if(handlerExecutablePath.empty())
        return MakeUnexpected(NativeFailure(PlatformOperation::ExecHandler, EINVAL));
    if(access(handlerExecutablePath.c_str(), X_OK) != 0)
        return MakeUnexpected(NativeFailure(PlatformOperation::ExecHandler, errno));
    Array<int, 2u> request = { -1, -1 };
    Array<int, 2u> ack = { -1, -1 };
    Array<int, 2u> startup = { -1, -1 };
    pid_t child = -1;
    ScopeExit rollback([&]()noexcept{
        for(int& descriptor : request)
            CleanupOwnedDescriptor(descriptor, observations);
        for(int& descriptor : ack)
            CleanupOwnedDescriptor(descriptor, observations);
        for(int& descriptor : startup)
            CleanupOwnedDescriptor(descriptor, observations);
        WaitForOwnedChild(true, child, observations);
        if(child > 0){
            g_State.handlerPid = child;
            g_State.platformRestorePending = true;
        }
    });
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, request.data()) != 0)
        return MakeUnexpected(NativeFailure(PlatformOperation::CreateRequestChannel, errno));
    if(pipe(ack.data()) != 0)
        return MakeUnexpected(NativeFailure(PlatformOperation::CreateAckChannel, errno));
    if(pipe(startup.data()) != 0)
        return MakeUnexpected(NativeFailure(PlatformOperation::CreateStartupChannel, errno));
    if(!SetCloseOnExec(request[0u]) || !SetCloseOnExec(ack[0u]) || !SetCloseOnExec(startup[0u]) || !SetCloseOnExec(startup[1u]))
        return MakeUnexpected(NativeFailure(PlatformOperation::SetDescriptorFlags, errno));
    child = fork();
    if(child < 0)
        return MakeUnexpected(NativeFailure(PlatformOperation::CreateProcess, errno));
    if(child == 0){
        for(const NotNull<int*> descriptor : { MakeNotNull(&request[0u]), MakeNotNull(&ack[0u]), MakeNotNull(&startup[0u]) }){
            const auto released = ReleaseOwnedDescriptor(*descriptor);
            if(!released)
                FailChild(startup[1u], static_cast<int>(released.error().code));
        }
        if(!SilenceChildProcess(request[1u], ack[1u], startup[1u]))
            FailChild(startup[1u], errno);
        Array<char, s_HandlerArgumentTextCapacity> requestText = {};
        Array<char, s_HandlerArgumentTextCapacity> ackText = {};
        AppendUnsignedToFixedBuffer(requestText, static_cast<u64>(request[1u]));
        AppendUnsignedToFixedBuffer(ackText, static_cast<u64>(ack[1u]));
        execl(handlerExecutablePath.c_str(), handlerExecutablePath.c_str(), s_RequestFdArgument.data(), requestText.data(), s_AckFdArgument.data(), ackText.data(), nullptr);
        FailChild(startup[1u], errno);
    }
    for(const NotNull<int*> descriptor : { MakeNotNull(&request[1u]), MakeNotNull(&ack[1u]), MakeNotNull(&startup[1u]) }){
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
    PosixCallstackCapture::WarmUp();
#endif
    const auto signals = __hidden_crash_posix::InstallSignalHandlers();
    if(!signals)
        return MakeUnexpected(signals.error());
    g_State.previousTerminateHandler = ExchangeTerminateHandler(PosixCallstackCapture::Terminate);
    g_State.terminateHandlerInstalled = true;
    return {};
}

void UninstallPlatformResources(PlatformCleanupObservations& observations)noexcept{
    using namespace __hidden_crash_posix;
#if defined(NWB_PLATFORM_ANDROID)
    CleanupOwnedDescriptor(g_State.emergencyWriteFd, observations);
#elif defined(NWB_PLATFORM_LINUX)
    CleanupOwnedDescriptor(g_State.requestWriteFd, observations);
    WaitForOwnedChild(false, g_State.handlerPid, observations);
    CleanupOwnedDescriptor(g_State.ackReadFd, observations);
#endif
    g_State.handlerStarted = false;
    if(g_State.terminateHandlerInstalled){
        if(QueryTerminateHandler() == PosixCallstackCapture::Terminate)
            ExchangeTerminateHandler(g_State.previousTerminateHandler);
        g_State.terminateHandlerInstalled = false;
        g_State.previousTerminateHandler = nullptr;
    }
    b8 pending = false;
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
        if((current.sa_flags & SA_SIGINFO) != 0 && current.sa_sigaction == PosixCallstackCapture::SignalHandler){
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
        else if((current.ss_flags & SS_DISABLE) == 0 && current.ss_sp == g_SignalStack.data()){
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

