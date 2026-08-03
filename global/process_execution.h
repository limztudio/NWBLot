// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "filesystem.h"
#include "platform.h"
#include "text_utils.h"
#include "type.h"

#include <cerrno>
#include <climits>
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


namespace ProcessExecutionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
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
#endif

#if defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
inline constexpr int s_ProcessSignalExitCodeBase = 128;
inline constexpr int s_ExecFailureExitCode = 127;
inline constexpr mode_t s_RedirectedOutputFileMode = 0666;

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
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
inline constexpr usize s_DefaultCaptureProcessOutputTimeoutMilliseconds = 3000u;
inline constexpr u64 s_MillisecondsPerSecond = 1000u;
inline constexpr u64 s_NanosecondsPerMillisecond = 1000000u;
inline constexpr int s_ProcessPollSleepMilliseconds = 1;
inline constexpr usize s_DefaultCaptureOutputMaxBytes = 8192u;
inline constexpr usize s_CaptureReadBufferBytes = 256u;

inline void CloseFileDescriptor(int& fd)noexcept{
    if(fd < 0)
        return;

    const int closeResult = ::close(fd);
    fd = -1;
    if(closeResult != 0)
        return;
}

[[nodiscard]] inline u64 MonotonicMilliseconds()noexcept{
    timespec time = {};
    if(::clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return 0u;

    return static_cast<u64>(time.tv_sec) * s_MillisecondsPerSecond + static_cast<u64>(time.tv_nsec / s_NanosecondsPerMillisecond);
}

[[nodiscard]] inline bool TimeoutExpired(const u64 deadlineMilliseconds)noexcept{
    return deadlineMilliseconds != 0u && MonotonicMilliseconds() >= deadlineMilliseconds;
}

[[nodiscard]] inline int RemainingTimeoutMilliseconds(const u64 deadlineMilliseconds)noexcept{
    if(deadlineMilliseconds == 0u)
        return -1;

    const u64 now = MonotonicMilliseconds();
    if(now >= deadlineMilliseconds)
        return 0;

    const u64 remaining = deadlineMilliseconds - now;
    return remaining > static_cast<u64>(INT_MAX)
        ? INT_MAX
        : static_cast<int>(remaining)
    ;
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

[[nodiscard]] inline bool WaitForProcessSuccess(const pid_t childPid, const u64 deadlineMilliseconds)noexcept{
    int status = 0;
    for(;;){
        const pid_t waitedPid = ::waitpid(childPid, &status, WNOHANG);
        if(waitedPid == childPid)
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if(waitedPid < 0 && errno != EINTR)
            return false;
        if(TimeoutExpired(deadlineMilliseconds)){
            KillAndReapProcess(childPid);
            return false;
        }

        const int pollResult = ::poll(nullptr, 0, s_ProcessPollSleepMilliseconds);
        if(pollResult < 0 && errno != EINTR){
            KillAndReapProcess(childPid);
            return false;
        }
    }
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline bool ExecutableAvailableInPath(ArenaT& arena, const AStringView searchPath, const AStringView executableName){
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

        const AStringView directoryText(searchPath.data() + begin, cursor - begin);
        Path<ArenaT> candidate(arena);
        if(directoryText.empty())
            candidate = ".";
        else{
            AString<ArenaT> directory(arena);
            directory.assign(directoryText.data(), directoryText.size());
            candidate = Path<ArenaT>(arena, AStringView(directory.data(), directory.size()));
        }

        AString<ArenaT> executable(arena);
        executable.assign(executableName.data(), executableName.size());
        candidate /= executable.c_str();

#if defined(NWB_PLATFORM_WINDOWS)
        if(PathIsRegularFile(candidate))
            return true;
#else
        const AString<ArenaT> candidateText = PathToString<char>(arena, candidate);
        if(::access(candidateText.c_str(), X_OK) == 0)
            return true;
#endif

        if(cursor >= searchPath.size())
            break;
        ++cursor;
    }

    return false;
}

template<typename ArenaT>
[[nodiscard]] inline int RunProcessRedirectedToFile(
    ArenaT& arena,
    const char* const* argv,
    const char* const outputPath,
    bool* const outExitCodeQueryFailed = nullptr
){
    if(outExitCodeQueryFailed)
        *outExitCodeQueryFailed = false;
    if(!argv || !argv[0] || !outputPath)
        return -1;

#if defined(NWB_PLATFORM_WINDOWS)
    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE outputHandle = CreateFileA(
        outputPath,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &securityAttributes,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if(outputHandle == INVALID_HANDLE_VALUE)
        return -1;

    AString<ArenaT> commandLine{arena};
    for(const char* const* argument = argv; *argument; ++argument)
        ProcessExecutionDetail::AppendWindowsCommandLineArgument(commandLine, AStringView(*argument));

    STARTUPINFOA startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = outputHandle;
    startupInfo.hStdError = outputHandle;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION processInfo = {};
    const BOOL created = CreateProcessA(
        argv[0],
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo
    );
    CloseHandle(outputHandle);
    if(!created)
        return -1;

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1u;
    if(!GetExitCodeProcess(processInfo.hProcess, &exitCode) && outExitCodeQueryFailed)
        *outExitCodeQueryFailed = true;
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return static_cast<int>(exitCode);
#elif defined(NWB_PLATFORM_LINUX) || defined(NWB_PLATFORM_ANDROID)
    static_cast<void>(arena);

    const int outputFileDescriptor = ::open(
        outputPath,
        O_CREAT | O_TRUNC | O_WRONLY,
        ProcessExecutionDetail::s_RedirectedOutputFileMode
    );
    if(outputFileDescriptor < 0)
        return -1;

    const pid_t childPid = ::fork();
    if(childPid < 0){
        const int closeResult = ::close(outputFileDescriptor);
        if(closeResult != 0)
            return -1;
        return -1;
    }

    if(childPid == 0){
        if(::dup2(outputFileDescriptor, STDOUT_FILENO) < 0)
            _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
        if(::dup2(outputFileDescriptor, STDERR_FILENO) < 0)
            _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
        if(::close(outputFileDescriptor) != 0)
            _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
        ::execvp(argv[0], const_cast<char* const*>(argv));
        _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
    }

    const int closeParentResult = ::close(outputFileDescriptor);
    const int exitCode = ProcessExecutionDetail::WaitForProcessExitCode(childPid);
    if(closeParentResult != 0)
        return -1;
    return exitCode;
#else
    static_cast<void>(arena);
    return -1;
#endif
}

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
template<typename StringT>
[[nodiscard]] inline bool CaptureProcessOutput(
    StringT& outOutput,
    const char* const* argv,
    const usize maxOutputBytes = ProcessExecutionDetail::s_DefaultCaptureOutputMaxBytes,
    const usize readBufferBytes = ProcessExecutionDetail::s_CaptureReadBufferBytes,
    const usize timeoutMilliseconds = ProcessExecutionDetail::s_DefaultCaptureProcessOutputTimeoutMilliseconds
){
    outOutput.clear();

    if(!argv || !argv[0] || readBufferBytes == 0u)
        return false;

    int pipeFds[2] = { -1, -1 };
    if(::pipe(pipeFds) != 0)
        return false;

    const pid_t childPid = ::fork();
    if(childPid < 0){
        ProcessExecutionDetail::CloseFileDescriptor(pipeFds[0]);
        ProcessExecutionDetail::CloseFileDescriptor(pipeFds[1]);
        return false;
    }

    if(childPid == 0){
        const int devNull = ::open("/dev/null", O_WRONLY);
        if(devNull >= 0){
            if(::dup2(devNull, STDERR_FILENO) < 0)
                _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
            if(devNull != STDERR_FILENO){
                if(::close(devNull) != 0)
                    _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
            }
        }

        if(::dup2(pipeFds[1], STDOUT_FILENO) < 0)
            _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
        if(::close(pipeFds[0]) != 0)
            _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
        if(::close(pipeFds[1]) != 0)
            _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
        ::execvp(argv[0], const_cast<char* const*>(argv));
        _exit(ProcessExecutionDetail::s_ExecFailureExitCode);
    }

    ProcessExecutionDetail::CloseFileDescriptor(pipeFds[1]);
    const u64 timeoutStartMilliseconds = ProcessExecutionDetail::MonotonicMilliseconds();
    const u64 deadlineMilliseconds = timeoutMilliseconds == 0u || timeoutStartMilliseconds == 0u
        ? 0u
        : timeoutStartMilliseconds + static_cast<u64>(timeoutMilliseconds)
    ;

    const int flags = ::fcntl(pipeFds[0], F_GETFL, 0);
    if(flags >= 0){
        if(::fcntl(pipeFds[0], F_SETFL, flags | O_NONBLOCK) < 0){
            ProcessExecutionDetail::CloseFileDescriptor(pipeFds[0]);
            ProcessExecutionDetail::KillAndReapProcess(childPid);
            return false;
        }
    }

    char buffer[ProcessExecutionDetail::s_CaptureReadBufferBytes] = {};
    const usize effectiveReadBufferBytes = readBufferBytes < sizeof(buffer)
        ? readBufferBytes
        : sizeof(buffer)
    ;
    bool readSucceeded = true;
    bool outputTruncated = false;
    bool timedOut = false;
    for(;;){
        if(ProcessExecutionDetail::TimeoutExpired(deadlineMilliseconds)){
            timedOut = true;
            break;
        }

        pollfd pipePoll = {};
        pipePoll.fd = pipeFds[0];
        pipePoll.events = POLLIN | POLLHUP | POLLERR;

        const int pollResult = ::poll(&pipePoll, 1, ProcessExecutionDetail::RemainingTimeoutMilliseconds(deadlineMilliseconds));
        if(pollResult == 0){
            timedOut = true;
            break;
        }
        if(pollResult < 0){
            if(errno == EINTR)
                continue;
            readSucceeded = false;
            break;
        }

        const ssize_t readBytes = ::read(pipeFds[0], buffer, effectiveReadBufferBytes);
        if(readBytes > 0){
            const usize length = static_cast<usize>(readBytes);
            const usize remainingOutputBytes = outOutput.size() < maxOutputBytes
                ? maxOutputBytes - outOutput.size()
                : 0u
            ;
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
        break;
    }

    ProcessExecutionDetail::CloseFileDescriptor(pipeFds[0]);
    if(timedOut){
        ProcessExecutionDetail::KillAndReapProcess(childPid);
        return false;
    }

    const bool processSucceeded = ProcessExecutionDetail::WaitForProcessSuccess(childPid, deadlineMilliseconds);
    return readSucceeded
        && !outputTruncated
        && processSucceeded
        && !outOutput.empty()
    ;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

