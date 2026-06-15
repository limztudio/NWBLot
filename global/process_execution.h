// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "filesystem.h"
#include "platform.h"
#include "text_utils.h"
#include "type.h"

#include <cerrno>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <fcntl.h>
#include <sys/wait.h>
#endif
#if !defined(NWB_PLATFORM_WINDOWS)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ProcessExecutionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
inline void CloseFileDescriptor(int& fd)noexcept{
    if(fd < 0)
        return;

    static_cast<void>(::close(fd));
    fd = -1;
}

[[nodiscard]] inline bool WaitForProcessSuccess(const pid_t childPid)noexcept{
    int status = 0;
    pid_t waitedPid = -1;
    do{
        waitedPid = ::waitpid(childPid, &status, 0);
    }while(waitedPid < 0 && errno == EINTR);

    return waitedPid == childPid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
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
[[nodiscard]] inline bool CaptureProcessOutput(StringT& outOutput, const char* const* argv, const usize maxOutputBytes = 8192u, const usize readBufferBytes = 256u){
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
            static_cast<void>(::dup2(devNull, STDERR_FILENO));
            if(devNull != STDERR_FILENO)
                static_cast<void>(::close(devNull));
        }

        static_cast<void>(::dup2(pipeFds[1], STDOUT_FILENO));
        static_cast<void>(::close(pipeFds[0]));
        static_cast<void>(::close(pipeFds[1]));
        ::execvp(argv[0], const_cast<char* const*>(argv));
        _exit(127);
    }

    ProcessExecutionDetail::CloseFileDescriptor(pipeFds[1]);

    char buffer[256] = {};
    const usize effectiveReadBufferBytes = readBufferBytes < sizeof(buffer)
        ? readBufferBytes
        : sizeof(buffer)
    ;
    bool readSucceeded = true;
    bool outputTruncated = false;
    for(;;){
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

        readSucceeded = false;
        break;
    }

    ProcessExecutionDetail::CloseFileDescriptor(pipeFds[0]);
    return readSucceeded && !outputTruncated && ProcessExecutionDetail::WaitForProcessSuccess(childPid) && !outOutput.empty();
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

