// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "process_execution.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ProcessCaptureFailureKind{
    enum Enum : u8{
        InvalidArguments,
        PipeFailed,
        SpawnFailed,
        ClockFailed,
        PipeFlagsFailed,
        PollFailed,
        ReadFailed,
        TimedOut,
        OutputTruncated,
        ChildWaitFailed,
        ChildExitedUnsuccessfully,
        ChildTerminated,
    };
};

struct ProcessCaptureFailure{
    ProcessCaptureFailureKind::Enum kind = ProcessCaptureFailureKind::InvalidArguments;
    b8 outputTruncated = false;
    ErrorCondition nativeError{};
    ErrorCondition completionError{};
    i32 exitCode = -1;
    i32 terminationSignal = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ProcessExecutionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_DefaultCaptureProcessOutputTimeoutMilliseconds = 3000u;
inline constexpr u64 s_MillisecondsPerSecond = 1000u;
inline constexpr u64 s_NanosecondsPerMillisecond = 1000000u;
inline constexpr int s_ProcessPollSleepMilliseconds = 1;
inline constexpr usize s_DefaultCaptureOutputMaxBytes = 8192u;
inline constexpr usize s_CaptureReadBufferBytes = 256u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void CloseFileDescriptor(int& fd)noexcept{
    if(fd < 0)
        return;

    const int closeResult = ::close(fd);
    fd = -1;
    if(closeResult != 0)
        return;
}

// ErrorCondition retains native POSIX errno without allocation or category initialization in crash-handler callers.
[[nodiscard]] inline Expected<u64, ErrorCondition> MonotonicMilliseconds()noexcept{
    timespec time = {};
    if(::clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return MakeUnexpected(static_cast<ErrorCondition>(errno));

    const u64 fractionalMilliseconds = static_cast<u64>(time.tv_nsec / s_NanosecondsPerMillisecond);
    if(static_cast<u64>(time.tv_sec) > (Limit<u64>::s_Max - fractionalMilliseconds) / s_MillisecondsPerSecond)
        return MakeUnexpected(ErrorCondition::value_too_large);
    return static_cast<u64>(time.tv_sec) * s_MillisecondsPerSecond + fractionalMilliseconds;
}

[[nodiscard]] inline Expected<b8, ErrorCondition> TimeoutExpired(const u64 deadlineMilliseconds)noexcept{
    if(deadlineMilliseconds == 0u)
        return false;
    const auto now = MonotonicMilliseconds();
    if(!now)
        return MakeUnexpected(now.error());
    return *now >= deadlineMilliseconds;
}

[[nodiscard]] inline Expected<int, ErrorCondition> RemainingTimeoutMilliseconds(const u64 deadlineMilliseconds)noexcept{
    if(deadlineMilliseconds == 0u)
        return -1;

    const auto now = MonotonicMilliseconds();
    if(!now)
        return MakeUnexpected(now.error());
    if(*now >= deadlineMilliseconds)
        return 0;

    const u64 remaining = deadlineMilliseconds - *now;
    return remaining > static_cast<u64>(INT_MAX) ? INT_MAX : static_cast<int>(remaining);
}

inline void KillAndReapProcess(const pid_t childPid)noexcept{
    if(childPid <= 0)
        return;

    const int killResult = ::kill(childPid, SIGKILL);
    if(killResult != 0 && errno != ESRCH)
        return;

    int status = 0;
    while(::waitpid(childPid, &status, 0) < 0 && errno == EINTR){
    }
}

[[nodiscard]] inline Expected<void, ProcessCaptureFailure> WaitForProcessSuccess(const pid_t childPid, const u64 deadlineMilliseconds)noexcept{
    int status = 0;
    for(;;){
        const pid_t waitedPid = ::waitpid(childPid, &status, WNOHANG);
        if(waitedPid == childPid){
            if(WIFEXITED(status)){
                const i32 exitCode = WEXITSTATUS(status);
                if(exitCode == 0)
                    return {};
                return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::ChildExitedUnsuccessfully, .exitCode = exitCode, });
            }
            return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::ChildTerminated, .terminationSignal = WIFSIGNALED(status) ? WTERMSIG(status) : 0, });
        }
        if(waitedPid < 0 && errno != EINTR)
            return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::ChildWaitFailed, .nativeError = static_cast<ErrorCondition>(errno), });
        const auto expired = TimeoutExpired(deadlineMilliseconds);
        if(!expired){
            const ErrorCondition error = expired.error();
            KillAndReapProcess(childPid);
            return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::ClockFailed, .nativeError = error, });
        }
        if(*expired){
            KillAndReapProcess(childPid);
            return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::TimedOut, .nativeError = ErrorCondition::timed_out, });
        }

        const int pollResult = ::poll(nullptr, 0, s_ProcessPollSleepMilliseconds);
        if(pollResult < 0 && errno != EINTR){
            const ErrorCondition error = static_cast<ErrorCondition>(errno);
            KillAndReapProcess(childPid);
            return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::PollFailed, .nativeError = error, });
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT, typename StringT>
[[nodiscard]] inline Expected<void, ProcessCaptureFailure> CaptureProcessOutput(
    StringT& outOutput,
    ArenaT& arena,
    const Span<const AStringView> arguments,
    const usize maxOutputBytes = ProcessExecutionDetail::s_DefaultCaptureOutputMaxBytes,
    const usize readBufferBytes = ProcessExecutionDetail::s_CaptureReadBufferBytes,
    const usize timeoutMilliseconds = ProcessExecutionDetail::s_DefaultCaptureProcessOutputTimeoutMilliseconds
){
    ProcessExecutionDetail::NativeProcessArguments<ArenaT> nativeArguments(arena);
    const b8 validArguments = nativeArguments.build(arguments);
    outOutput.clear();
    if(!validArguments || readBufferBytes == 0u)
        return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::InvalidArguments, .nativeError = ErrorCondition::invalid_argument, });
    const auto& argv = nativeArguments.argv;

    int pipeFds[2] = { -1, -1 };
    if(::pipe(pipeFds) != 0)
        return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::PipeFailed, .nativeError = static_cast<ErrorCondition>(errno), });
    ScopeExit closePipe([&]()noexcept{
        ProcessExecutionDetail::CloseFileDescriptor(pipeFds[0]);
        ProcessExecutionDetail::CloseFileDescriptor(pipeFds[1]);
    });

    const pid_t childPid = ::fork();
    if(childPid < 0)
        return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::SpawnFailed, .nativeError = static_cast<ErrorCondition>(errno), });

    if(childPid == 0){
        if(::close(pipeFds[0]) != 0)
            _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
        if(::dup2(pipeFds[1], STDOUT_FILENO) < 0)
            _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
        if(pipeFds[1] != STDOUT_FILENO && ::close(pipeFds[1]) != 0)
            _exit(ProcessExecutionDetail::s_ExecFailureExitCode);

        const int devNull = ::open("/dev/null", O_WRONLY);
        if(devNull >= 0){
            if(::dup2(devNull, STDERR_FILENO) < 0)
                _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
            if(devNull != STDERR_FILENO){
                if(::close(devNull) != 0)
                    _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
            }
        }

        ::execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
    }

    ScopeExit reapChild([&]()noexcept{ ProcessExecutionDetail::KillAndReapProcess(childPid); });

    ProcessExecutionDetail::CloseFileDescriptor(pipeFds[1]);
    const auto timeoutStartMilliseconds = timeoutMilliseconds == 0u ? Expected<u64, ErrorCondition>(0u) : ProcessExecutionDetail::MonotonicMilliseconds();
    if(!timeoutStartMilliseconds)
        return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::ClockFailed, .nativeError = timeoutStartMilliseconds.error(), });
    if(static_cast<u64>(timeoutMilliseconds) > Limit<u64>::s_Max - *timeoutStartMilliseconds)
        return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::InvalidArguments, .nativeError = ErrorCondition::value_too_large, });
    const u64 deadlineMilliseconds = timeoutMilliseconds == 0u ? 0u : *timeoutStartMilliseconds + static_cast<u64>(timeoutMilliseconds);

    const int flags = ::fcntl(pipeFds[0], F_GETFL, 0);
    if(flags < 0 || ::fcntl(pipeFds[0], F_SETFL, flags | O_NONBLOCK) < 0)
        return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::PipeFlagsFailed, .nativeError = static_cast<ErrorCondition>(errno), });

    char buffer[ProcessExecutionDetail::s_CaptureReadBufferBytes];
    const usize effectiveReadBufferBytes = readBufferBytes < sizeof(buffer) ? readBufferBytes : sizeof(buffer);
    b8 readSucceeded = true;
    ProcessCaptureFailure readFailure;
    b8 outputTruncated = false;
    b8 timedOut = false;
    for(;;){
        const auto expired = ProcessExecutionDetail::TimeoutExpired(deadlineMilliseconds);
        if(!expired)
            return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::ClockFailed, .outputTruncated = outputTruncated, .nativeError = expired.error(), });
        if(*expired){
            timedOut = true;
            break;
        }

        pollfd pipePoll = {};
        pipePoll.fd = pipeFds[0];
        pipePoll.events = POLLIN | POLLHUP | POLLERR;

        const auto remainingTimeout = ProcessExecutionDetail::RemainingTimeoutMilliseconds(deadlineMilliseconds);
        if(!remainingTimeout)
            return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::ClockFailed, .outputTruncated = outputTruncated, .nativeError = remainingTimeout.error(), });
        const int pollResult = ::poll(&pipePoll, 1, *remainingTimeout);
        if(pollResult == 0){
            timedOut = true;
            break;
        }
        if(pollResult < 0){
            if(errno == EINTR)
                continue;
            readSucceeded = false;
            readFailure = ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::PollFailed, .nativeError = static_cast<ErrorCondition>(errno), };
            break;
        }

        const ssize_t readBytes = ::read(pipeFds[0], buffer, effectiveReadBufferBytes);
        if(readBytes > 0){
            const usize length = static_cast<usize>(readBytes);
            const usize remainingOutputBytes = outOutput.size() < maxOutputBytes ? maxOutputBytes - outOutput.size() : 0u;
            if(length <= remainingOutputBytes)
                outOutput.append(buffer, length);
            else{
                if(remainingOutputBytes != 0u)
                    outOutput.append(buffer, remainingOutputBytes);
                outputTruncated = true;
            }
            continue;
        }

        if(readBytes == 0)
            break;

        if(errno == EINTR)
            continue;
        if(errno == EAGAIN || errno == EWOULDBLOCK){
            if((pipePoll.revents & (POLLHUP | POLLERR)) != 0)
                break;
            continue;
        }

        readSucceeded = false;
        readFailure = ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::ReadFailed, .nativeError = static_cast<ErrorCondition>(errno), };
        break;
    }

    ProcessExecutionDetail::CloseFileDescriptor(pipeFds[0]);
    if(timedOut)
        return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::TimedOut, .outputTruncated = outputTruncated, .nativeError = ErrorCondition::timed_out, });

    reapChild.release();
    const auto processSucceeded = ProcessExecutionDetail::WaitForProcessSuccess(childPid, deadlineMilliseconds);
    if(!readSucceeded){
        readFailure.outputTruncated = outputTruncated;
        if(!processSucceeded){
            readFailure.completionError = processSucceeded.error().nativeError;
            readFailure.exitCode = processSucceeded.error().exitCode;
            readFailure.terminationSignal = processSucceeded.error().terminationSignal;
        }
        return MakeUnexpected(readFailure);
    }
    if(!processSucceeded){
        ProcessCaptureFailure failure = processSucceeded.error();
        failure.outputTruncated = outputTruncated;
        return MakeUnexpected(failure);
    }
    if(outputTruncated)
        return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::OutputTruncated, .outputTruncated = true, .nativeError = ErrorCondition::value_too_large, });
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

