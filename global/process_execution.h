// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "containers.h"
#include "filesystem.h"
#include "expected.h"
#include "error_code.h"
#include "limit.h"
#include "overflow.h"
#include "span.h"
#include "scope_exit.h"
#include "platform.h"
#include "text_utils.h"
#include "type.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <ctime>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <poll.h>
#include <signal.h>
#endif
#if !defined(NWB_PLATFORM_WINDOWS)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ProcessExecutionFailure{
    enum Enum : u8{
        InvalidArguments,
        OutputOpenFailed,
        CreateFailed,
        ExitQueryFailed,
        CloseFailed,
        Unsupported,
    };
};


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
#if defined(NWB_PLATFORM_WINDOWS)
    AString<ArenaT> systemCommand("\"", command.get_allocator());
    systemCommand += command;
    systemCommand += '"';
    return std::system(systemCommand.c_str());
#else
    return std::system(command.c_str());
#endif
}


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
    ErrorCondition nativeError{};
    ErrorCondition completionError{};
    i32 exitCode = -1;
    i32 terminationSignal = 0;
    bool outputTruncated = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ProcessExecutionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool AreProcessArgumentsValid(const Span<const AStringView> arguments)noexcept{
    if(arguments.empty() || arguments.front().empty())
        return false;
    for(const AStringView argument : arguments){
        if(argument.find('\0') != AStringView::npos)
            return false;
    }
    return true;
}


template<typename ArenaT>
struct NativeProcessArguments{
    Vector<AString<ArenaT>, ArenaT> text;
    Vector<const char*, ArenaT> argv;
    ArenaT& arena;


public:
    explicit NativeProcessArguments(ArenaT& owner)noexcept(noexcept(decltype(text)(owner)) && noexcept(decltype(argv)(owner)))
        : text(owner)
        , argv(owner)
        , arena(owner)
    {}
    NativeProcessArguments(const NativeProcessArguments&) = default;
    NativeProcessArguments(NativeProcessArguments&&)noexcept = default;
    ~NativeProcessArguments()noexcept{
        // Argument bytes sit above both vector blocks in a scratch arena; retire them before member storage in reverse allocation order.
        while(!text.empty())
            text.pop_back();
    }


public:
    [[nodiscard]] bool build(const Span<const AStringView> arguments){
        if(!AreProcessArgumentsValid(arguments))
            return false;

        text.reserve(arguments.size());
        argv.reserve(arguments.size() + 1u);
        for(const AStringView argument : arguments){
            text.emplace_back(argument, arena);
            argv.push_back(text.back().c_str());
        }
        argv.push_back(nullptr);
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
inline void AppendWindowsCommandLineArgument(StringT& inOutCommand, const AStringView value){
    if(!inOutCommand.empty())
        inOutCommand += ' ';

    bool needsQuotes = value.empty();
    for(const char ch : value){
        if(ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '"'){
            needsQuotes = true;
            break;
        }
    }

    if(!needsQuotes){
        inOutCommand.append(value.data(), value.size());
        return;
    }

    inOutCommand += '"';
    usize backslashCount = 0u;
    for(const char ch : value){
        if(ch == '\\'){
            ++backslashCount;
            continue;
        }

        if(ch == '"'){
            for(usize i = 0u; i < backslashCount * 2u + 1u; ++i)
                inOutCommand += '\\';
            inOutCommand += '"';
            backslashCount = 0u;
            continue;
        }

        for(usize i = 0u; i < backslashCount; ++i)
            inOutCommand += '\\';
        inOutCommand += ch;
        backslashCount = 0u;
    }
    for(usize i = 0u; i < backslashCount * 2u; ++i)
        inOutCommand += '\\';
    inOutCommand += '"';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr int s_ProcessSignalExitCodeBase = 128;
inline constexpr int s_ExecFailureExitCode = 127;
inline constexpr mode_t s_RedirectedOutputFileMode = 0666;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline int WaitForProcessExitCode(const pid_t childPid)noexcept{
    int status = 0;
    pid_t waitedPid = -1;
    do{
        waitedPid = ::waitpid(childPid, &status, 0);
    }while(waitedPid < 0 && errno == EINTR);

    if(waitedPid != childPid)
        return -1;
    if(WIFEXITED(status))
        return WEXITSTATUS(status);
    if(WIFSIGNALED(status))
        return s_ProcessSignalExitCodeBase + WTERMSIG(status);
    return -1;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


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

[[nodiscard]] inline Expected<bool, ErrorCondition> TimeoutExpired(const u64 deadlineMilliseconds)noexcept{
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


template<typename ArenaT>
[[nodiscard]] inline Expected<bool, ErrorCode> ExecutableAvailableInPath(ArenaT& arena, const AStringView searchPath, const AStringView executableName){
    const char separator =
#if defined(NWB_PLATFORM_WINDOWS)
        ';'
#else
        ':'
#endif
    ;

    usize cursor = 0u;
    while(cursor <= searchPath.size()){
        const usize begin = cursor;
        while(cursor < searchPath.size() && searchPath[cursor] != separator)
            ++cursor;

        const AStringView directoryText = searchPath.substr(begin, cursor - begin);
        Path<ArenaT> candidate(arena);
        if(directoryText.empty())
            candidate = ".";
        else
            candidate = Path<ArenaT>(arena, directoryText);

        candidate /= executableName;

#if defined(NWB_PLATFORM_WINDOWS)
        const auto regularCandidate = IsRegularFile(candidate);
        if(!regularCandidate)
            return MakeUnexpected(regularCandidate.error());
        if(*regularCandidate)
            return true;
#else
        const AString<ArenaT> candidateText = PathToString<char>(arena, candidate);
        if(::access(candidateText.c_str(), X_OK) == 0)
            return true;
        const int accessError = errno;
        if(accessError != ENOENT && accessError != ENOTDIR && accessError != EACCES)
            return MakeUnexpected(ErrorCode(accessError, std::generic_category()));
#endif

        if(cursor >= searchPath.size())
            break;
        ++cursor;
    }

    return false;
}

template<typename ArenaT>
[[nodiscard]] inline Expected<int, ProcessExecutionFailure::Enum> RunProcessRedirectedToFile(ArenaT& arena, const Span<const AStringView> arguments, const AStringView outputPath){
    if(outputPath.empty() || outputPath.find('\0') != AStringView::npos)
        return MakeUnexpected(ProcessExecutionFailure::InvalidArguments);
#if defined(NWB_PLATFORM_WINDOWS)
    if(!ProcessExecutionDetail::AreProcessArgumentsValid(arguments))
        return MakeUnexpected(ProcessExecutionFailure::InvalidArguments);
    const AString<ArenaT> nativeExecutable(arguments.front(), arena);
#else
    ProcessExecutionDetail::NativeProcessArguments<ArenaT> nativeArguments(arena);
    if(!nativeArguments.build(arguments))
        return MakeUnexpected(ProcessExecutionFailure::InvalidArguments);
    const auto& argv = nativeArguments.argv;
#endif
    const AString<ArenaT> nativeOutputPath(outputPath, arena);

#if defined(NWB_PLATFORM_WINDOWS)
    AString<ArenaT> commandLine{arena};
    usize commandLineCapacity = arguments.size() - 1u;
    for(const AStringView argument : arguments){
        if(argument.size() > (Limit<usize>::s_Max - 2u) / 2u)
            return MakeUnexpected(ProcessExecutionFailure::InvalidArguments);
        const usize argumentCapacity = argument.size() * 2u + 2u;
        if(AddOverflows<usize>(commandLineCapacity, argumentCapacity))
            return MakeUnexpected(ProcessExecutionFailure::InvalidArguments);
        commandLineCapacity += argumentCapacity;
    }
    if(commandLineCapacity > commandLine.max_size())
        return MakeUnexpected(ProcessExecutionFailure::InvalidArguments);
    // Quoting adds at most two bytes per input byte plus surrounding quotes; reserve once so a scratch arena never retains abandoned growth buffers.
    commandLine.reserve(commandLineCapacity);
    for(const AStringView argument : arguments)
        ProcessExecutionDetail::AppendWindowsCommandLineArgument(commandLine, argument);

    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE outputHandle = CreateFileA(nativeOutputPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &securityAttributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(outputHandle == INVALID_HANDLE_VALUE)
        return MakeUnexpected(ProcessExecutionFailure::OutputOpenFailed);

    STARTUPINFOA startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = outputHandle;
    startupInfo.hStdError = outputHandle;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION processInfo = {};
    const BOOL created = CreateProcessA(nativeExecutable.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo);
    const BOOL outputClosed = CloseHandle(outputHandle);
    if(!created)
        return MakeUnexpected(outputClosed ? ProcessExecutionFailure::CreateFailed : ProcessExecutionFailure::CloseFailed);

    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1u;
    const bool queriedExitCode = waitResult == WAIT_OBJECT_0 && GetExitCodeProcess(processInfo.hProcess, &exitCode);
    const BOOL threadClosed = CloseHandle(processInfo.hThread);
    const BOOL processClosed = CloseHandle(processInfo.hProcess);
    if(!queriedExitCode)
        return MakeUnexpected(ProcessExecutionFailure::ExitQueryFailed);
    if(!outputClosed || !threadClosed || !processClosed)
        return MakeUnexpected(ProcessExecutionFailure::CloseFailed);
    return static_cast<int>(exitCode);
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
    static_cast<void>(arena);

    const int outputFileDescriptor = ::open(nativeOutputPath.c_str(), O_CREAT | O_TRUNC | O_WRONLY, ProcessExecutionDetail::s_RedirectedOutputFileMode);
    if(outputFileDescriptor < 0)
        return MakeUnexpected(ProcessExecutionFailure::OutputOpenFailed);

    const pid_t childPid = ::fork();
    if(childPid < 0){
        const int closeResult = ::close(outputFileDescriptor);
        if(closeResult != 0)
            return MakeUnexpected(ProcessExecutionFailure::CloseFailed);
        return MakeUnexpected(ProcessExecutionFailure::CreateFailed);
    }

    if(childPid == 0){
        if(::dup2(outputFileDescriptor, STDOUT_FILENO) < 0)
            _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
        if(::dup2(outputFileDescriptor, STDERR_FILENO) < 0)
            _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
        if(outputFileDescriptor != STDOUT_FILENO && outputFileDescriptor != STDERR_FILENO && ::close(outputFileDescriptor) != 0)
            _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
        ::execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
    }

    const int closeParentResult = ::close(outputFileDescriptor);
    const int exitCode = ProcessExecutionDetail::WaitForProcessExitCode(childPid);
    if(closeParentResult != 0)
        return MakeUnexpected(ProcessExecutionFailure::CloseFailed);
    if(exitCode < 0)
        return MakeUnexpected(ProcessExecutionFailure::ExitQueryFailed);
    return exitCode;
#else
    static_cast<void>(arena);
    return MakeUnexpected(ProcessExecutionFailure::Unsupported);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT, typename StringT>
[[nodiscard]] inline Expected<void, ProcessCaptureFailure> CaptureProcessOutput(
    ArenaT& arena,
    StringT& outOutput,
    const Span<const AStringView> arguments,
    const usize maxOutputBytes = ProcessExecutionDetail::s_DefaultCaptureOutputMaxBytes,
    const usize readBufferBytes = ProcessExecutionDetail::s_CaptureReadBufferBytes,
    const usize timeoutMilliseconds = ProcessExecutionDetail::s_DefaultCaptureProcessOutputTimeoutMilliseconds
){
    ProcessExecutionDetail::NativeProcessArguments<ArenaT> nativeArguments(arena);
    const bool validArguments = nativeArguments.build(arguments);
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
    }
    );

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
    bool readSucceeded = true;
    ProcessCaptureFailure readFailure;
    bool outputTruncated = false;
    bool timedOut = false;
    for(;;){
        const auto expired = ProcessExecutionDetail::TimeoutExpired(deadlineMilliseconds);
        if(!expired)
            return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::ClockFailed, .nativeError = expired.error(), .outputTruncated = outputTruncated, });
        if(*expired){
            timedOut = true;
            break;
        }

        pollfd pipePoll = {};
        pipePoll.fd = pipeFds[0];
        pipePoll.events = POLLIN | POLLHUP | POLLERR;

        const auto remainingTimeout = ProcessExecutionDetail::RemainingTimeoutMilliseconds(deadlineMilliseconds);
        if(!remainingTimeout)
            return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::ClockFailed, .nativeError = remainingTimeout.error(), .outputTruncated = outputTruncated, });
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
        return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::TimedOut, .nativeError = ErrorCondition::timed_out, .outputTruncated = outputTruncated, });

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
        return MakeUnexpected(ProcessCaptureFailure{ .kind = ProcessCaptureFailureKind::OutputTruncated, .nativeError = ErrorCondition::value_too_large, .outputTruncated = true, });
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

