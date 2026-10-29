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


// The value is the raw shell status from std::system. A -1 result with errno set means the shell did not run.
template<typename ArenaT>
[[nodiscard]] inline Expected<int, ErrorCondition> RunSystemCommand(ArenaT& arena, const AStringView command){
    if(command.find(char{}) != AStringView::npos)
        return MakeUnexpected(ErrorCondition::invalid_argument);
    const AString<ArenaT> nativeCommand(command, arena);
    errno = 0;
    const int status = std::system(nativeCommand.c_str());
    if(status == -1 && errno != 0)
        return MakeUnexpected(static_cast<ErrorCondition>(errno));
    return status;
}

template<typename ArenaT>
[[nodiscard]] inline Expected<int, ErrorCondition> RunSystemCommand(const AString<ArenaT>& command){
#if defined(NWB_PLATFORM_WINDOWS)
    AString<ArenaT> systemCommand("\"", command.get_allocator());
    systemCommand += command;
    systemCommand += '"';
    errno = 0;
    const int status = std::system(systemCommand.c_str());
#else
    errno = 0;
    const int status = std::system(command.c_str());
#endif
    if(status == -1 && errno != 0)
        return MakeUnexpected(static_cast<ErrorCondition>(errno));
    return status;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ProcessExecutionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 AreProcessArgumentsValid(const Span<const AStringView> arguments)noexcept{
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
    NativeProcessArguments(const NativeProcessArguments&) = delete;
    NativeProcessArguments(NativeProcessArguments&&)noexcept = default;
    ~NativeProcessArguments()noexcept{
        // Argument bytes sit above both vector blocks in a scratch arena; retire them before member storage in reverse allocation order.
        while(!text.empty())
            text.pop_back();
    }


public:
    [[nodiscard]] b8 build(const Span<const AStringView> arguments){
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

    b8 needsQuotes = value.empty();
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


// A child killed by a signal reports 128 plus the signal number.
[[nodiscard]] inline Expected<int, ErrorCondition> WaitForProcessExitCode(const pid_t childPid)noexcept{
    int status = 0;
    pid_t waitedPid = -1;
    do{
        waitedPid = ::waitpid(childPid, &status, 0);
    }while(waitedPid < 0 && errno == EINTR);

    if(waitedPid < 0)
        return MakeUnexpected(static_cast<ErrorCondition>(errno));
    if(waitedPid != childPid)
        return MakeUnexpected(ErrorCondition::no_child_process);
    if(WIFEXITED(status))
        return WEXITSTATUS(status);
    if(WIFSIGNALED(status))
        return s_ProcessSignalExitCodeBase + WTERMSIG(status);
    return MakeUnexpected(ErrorCondition::io_error);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<b8, ErrorCode> ExecutableAvailableInPath(ArenaT& arena, const AStringView searchPath, const AStringView executableName){
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
    const b8 queriedExitCode = waitResult == WAIT_OBJECT_0 && GetExitCodeProcess(processInfo.hProcess, &exitCode);
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
    const Expected<int, ErrorCondition> exitCode = ProcessExecutionDetail::WaitForProcessExitCode(childPid);
    if(closeParentResult != 0)
        return MakeUnexpected(ProcessExecutionFailure::CloseFailed);
    if(!exitCode)
        return MakeUnexpected(ProcessExecutionFailure::ExitQueryFailed);
    return *exitCode;
#else
    static_cast<void>(arena);
    return MakeUnexpected(ProcessExecutionFailure::Unsupported);
#endif
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

