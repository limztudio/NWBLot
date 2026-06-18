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

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#endif
#if !defined(NWB_PLATFORM_WINDOWS)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ProcessExecutionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
inline constexpr usize s_DefaultCaptureProcessOutputTimeoutMilliseconds = 3000u;

inline void CloseFileDescriptor(int& fd)noexcept{
    if(fd < 0)
        return;

    [[maybe_unused]] const int closeResult = ::close(fd);
    fd = -1;
}

[[nodiscard]] inline u64 MonotonicMilliseconds()noexcept{
    timespec time = {};
    if(::clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        return 0u;

    return static_cast<u64>(time.tv_sec) * 1000u + static_cast<u64>(time.tv_nsec / 1000000L);
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

    [[maybe_unused]] const int killResult = ::kill(childPid, SIGKILL);
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

        [[maybe_unused]] const int pollResult = ::poll(nullptr, 0, 1);
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

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
template<typename StringT>
[[nodiscard]] inline bool CaptureProcessOutput(
    StringT& outOutput,
    const char* const* argv,
    const usize maxOutputBytes = 8192u,
    const usize readBufferBytes = 256u,
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
            [[maybe_unused]] const int dupStderrResult = ::dup2(devNull, STDERR_FILENO);
            if(devNull != STDERR_FILENO){
                [[maybe_unused]] const int closeDevNullResult = ::close(devNull);
            }
        }

        [[maybe_unused]] const int dupStdoutResult = ::dup2(pipeFds[1], STDOUT_FILENO);
        [[maybe_unused]] const int closeReadEndResult = ::close(pipeFds[0]);
        [[maybe_unused]] const int closeWriteEndResult = ::close(pipeFds[1]);
        ::execvp(argv[0], const_cast<char* const*>(argv));
        _exit(127);
    }

    ProcessExecutionDetail::CloseFileDescriptor(pipeFds[1]);
    const u64 timeoutStartMilliseconds = ProcessExecutionDetail::MonotonicMilliseconds();
    const u64 deadlineMilliseconds = timeoutMilliseconds == 0u || timeoutStartMilliseconds == 0u
        ? 0u
        : timeoutStartMilliseconds + static_cast<u64>(timeoutMilliseconds)
    ;

    const int flags = ::fcntl(pipeFds[0], F_GETFL, 0);
    if(flags >= 0){
        [[maybe_unused]] const int setFlagsResult = ::fcntl(pipeFds[0], F_SETFL, flags | O_NONBLOCK);
    }

    char buffer[256] = {};
    const usize effectiveReadBufferBytes = readBufferBytes < sizeof(buffer)
        ? readBufferBytes
        : sizeof(buffer)
    ;
    bool readSucceeded = true;
    bool outputTruncated = false;
    bool timedOut = false;
    for(;;){
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

    return readSucceeded
        && !outputTruncated
        && ProcessExecutionDetail::WaitForProcessSuccess(childPid, deadlineMilliseconds)
        && !outOutput.empty()
    ;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

