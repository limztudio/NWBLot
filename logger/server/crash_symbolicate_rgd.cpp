// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <core/crash/package_names.h>

#include <global/environment.h>
#include <global/process_execution.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_symbolicate{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;

// AMD Radeon GPU Detective (rgd) is an EXTERNAL tool that captures a .rgd crash dump on a TDR; the engine never
// emits one, so this decode is best-effort: it runs only when a .rgd is present in the package and rgd is
// resolvable, and it never fails the surrounding ingest (ProcessCrashUpload rejects on a thrown exception).
// Cross-platform: the Radeon Developer Tool Suite ships rgd for both Windows (rgd.exe) and Linux (rgd).
inline constexpr usize s_RgdMaxOutputBytes = 512u * 1024u;       // rgd analysis text (system info + marker tree) can be large
inline constexpr usize s_RgdReadChunkBytes = 4096u;
inline constexpr u32 s_RgdTimeoutMilliseconds = 30000u;          // rgd parsing of a big dump is slow
inline constexpr const char* s_RgdToolSuiteEnvVar = "RadeonDeveloperToolSuite";
inline constexpr const char* s_RgdParseArgument = "--parse";
#if defined(NWB_PLATFORM_WINDOWS)
inline constexpr const char* s_RgdExecutableName = "rgd.exe";
inline constexpr u32 s_RgdPollIntervalMilliseconds = 5u;
#else
inline constexpr const char* s_RgdExecutableName = "rgd";
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)

// Append a CreateProcessW-compatible quoted argument. Operates on UTF-8 bytes; the quoting metacharacters
// (space, quote, backslash) are ASCII, so this is correct for UTF-8 multibyte paths.
static void AppendQuotedArgument(AString<LogArena>& commandLine, const AStringView arg){
    if(!commandLine.empty())
        commandLine.push_back(' ');

    commandLine.push_back('"');
    usize i = 0u;
    const usize length = arg.size();
    while(i < length){
        usize backslashes = 0u;
        while(i < length && arg[i] == '\\'){
            ++i;
            ++backslashes;
        }

        if(i == length){
            // Trailing backslashes precede the closing quote: double them.
            for(usize n = 0u; n < backslashes * 2u; ++n)
                commandLine.push_back('\\');
            break;
        }

        if(arg[i] == '"'){
            // Backslashes before a literal quote: double them, then escape the quote.
            for(usize n = 0u; n < backslashes * 2u + 1u; ++n)
                commandLine.push_back('\\');
            commandLine.push_back('"');
            ++i;
        }
        else{
            for(usize n = 0u; n < backslashes; ++n)
                commandLine.push_back('\\');
            commandLine.push_back(arg[i]);
            ++i;
        }
    }
    commandLine.push_back('"');
}

// Spawn argv capturing stdout+stderr into outOutput, bounded by s_RgdMaxOutputBytes and s_RgdTimeoutMilliseconds.
// Always reaps the child (never orphans rgd.exe). Returns true if the process ran, did not time out, and produced
// non-empty output. Never throws.
[[nodiscard]] static bool RunRgdCapture(LogArena& arena, const char* const* argv, CrashReportText& outOutput){
    outOutput.clear();
    if(!argv || !argv[0])
        return false;

    AString<LogArena> commandLine(arena);
    for(usize i = 0u; argv[i] != nullptr; ++i)
        AppendQuotedArgument(commandLine, AStringView(argv[i]));

    // CreateProcessW needs a mutable wide command line.
    const int wideLength = ::MultiByteToWideChar(CP_UTF8, 0, commandLine.data(), static_cast<int>(commandLine.size()), nullptr, 0);
    if(wideLength <= 0)
        return false;
    Vector<wchar_t, LogArena> wideCommandLine(arena);
    wideCommandLine.resize(static_cast<usize>(wideLength) + 1u, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, commandLine.data(), static_cast<int>(commandLine.size()), wideCommandLine.data(), wideLength);
    wideCommandLine[static_cast<usize>(wideLength)] = L'\0';

    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE readHandle = nullptr;
    HANDLE writeHandle = nullptr;
    if(!::CreatePipe(&readHandle, &writeHandle, &securityAttributes, 0u))
        return false;
    // The read end stays in this process and must not be inherited by the child.
    ::SetHandleInformation(readHandle, HANDLE_FLAG_INHERIT, 0u);

    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writeHandle;
    startupInfo.hStdError = writeHandle;
    startupInfo.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION processInfo = {};
    const BOOL created = ::CreateProcessW(nullptr, wideCommandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startupInfo, &processInfo);
    // The parent never writes to the child; closing the write end lets our reads see EOF when the child exits.
    ::CloseHandle(writeHandle);
    if(!created){
        ::CloseHandle(readHandle);
        return false;
    }

    const ULONGLONG deadline = ::GetTickCount64() + s_RgdTimeoutMilliseconds;
    char chunk[s_RgdReadChunkBytes];
    bool timedOut = false;
    bool hitCap = false;
    for(;;){
        DWORD available = 0u;
        if(!::PeekNamedPipe(readHandle, nullptr, 0u, nullptr, &available, nullptr))
            break; // write end closed (child exited) or pipe error

        if(available == 0u){
            if(::WaitForSingleObject(processInfo.hProcess, 0u) == WAIT_OBJECT_0){
                // Child exited; re-peek once for a final burst, else we are done.
                if(!::PeekNamedPipe(readHandle, nullptr, 0u, nullptr, &available, nullptr) || available == 0u)
                    break;
            }
            else{
                if(::GetTickCount64() >= deadline){
                    timedOut = true;
                    break;
                }
                ::Sleep(s_RgdPollIntervalMilliseconds);
                continue;
            }
        }

        const DWORD toRead = available < static_cast<DWORD>(sizeof(chunk)) ? available : static_cast<DWORD>(sizeof(chunk));
        DWORD bytesRead = 0u;
        if(!::ReadFile(readHandle, chunk, toRead, &bytesRead, nullptr) || bytesRead == 0u)
            break;

        const usize length = static_cast<usize>(bytesRead);
        const usize remaining = outOutput.size() < s_RgdMaxOutputBytes ? s_RgdMaxOutputBytes - outOutput.size() : 0u;
        if(length <= remaining)
            outOutput.append(chunk, length);
        else{
            if(remaining != 0u)
                outOutput.append(chunk, remaining);
            hitCap = true; // captured all we want; the child is no longer needed
            break;
        }
    }

    ::CloseHandle(readHandle);

    // Always reap the child so an external rgd.exe is never orphaned by the long-lived ingest worker. On a clean
    // read-loop exit the child is already exiting (its stdout pipe is closed), so a bounded wait suffices; on the
    // cap/timeout paths — or if a "clean" child lingers past the wait — escalate to a (non-ignorable)
    // TerminateProcess then wait unconditionally. Mirrors the Linux CaptureProcessOutput always-reap guarantee.
    bool escalate = timedOut || hitCap;
    if(!escalate && ::WaitForSingleObject(processInfo.hProcess, s_RgdTimeoutMilliseconds) != WAIT_OBJECT_0)
        escalate = true;
    if(escalate){
        ::TerminateProcess(processInfo.hProcess, 1u);
        ::WaitForSingleObject(processInfo.hProcess, INFINITE);
    }
    ::CloseHandle(processInfo.hThread);
    ::CloseHandle(processInfo.hProcess);

    return !timedOut && !outOutput.empty();
}

#else

// Linux/Unix: reuse the shared fork/exec capture (handles non-blocking pipe drain, timeout, and always-reaps the
// child on timeout) — the Windows half above is the only platform that lacked a CaptureProcessOutput.
[[nodiscard]] static bool RunRgdCapture(LogArena& arena, const char* const* argv, CrashReportText& outOutput){
    static_cast<void>(arena);
    return CaptureProcessOutput(outOutput, argv, s_RgdMaxOutputBytes, s_RgdReadChunkBytes, s_RgdTimeoutMilliseconds);
}

#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Resolve the rgd executable: the RadeonDeveloperToolSuite env var (install root) first, then PATH. Cross-platform
// (PathIsRegularFile / ExecutableAvailableInPath / ReadEnvironmentVariable handle the platform differences).
[[nodiscard]] static bool ResolveRgdExecutable(LogArena& arena, AString<LogArena>& outExecutable){
    outExecutable.clear();

    AString<LogArena> toolSuiteRoot(arena);
    if(ReadEnvironmentVariable(s_RgdToolSuiteEnvVar, toolSuiteRoot) && !toolSuiteRoot.empty()){
        const Path candidate = Path(arena, AStringView(toolSuiteRoot.data(), toolSuiteRoot.size())) / s_RgdExecutableName;
        if(PathIsRegularFile(candidate)){
            outExecutable = PathToString<char>(arena, candidate);
            return true;
        }
    }

    AString<LogArena> pathValue(arena);
    if(ReadEnvironmentVariable("PATH", pathValue) && !pathValue.empty()){
        if(ExecutableAvailableInPath(arena, AStringView(pathValue.data(), pathValue.size()), AStringView(s_RgdExecutableName))){
            // On the search path: the spawner resolves the bare executable name (CreateProcessW / execvp).
            outExecutable = s_RgdExecutableName;
            return true;
        }
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendRadeonGpuDetectiveSummary(LogArena& arena, const Path& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport){
    static_cast<void>(config);

    const Path rgdCapture = packageDirectory / CrashNames::s_GpuDetectiveCaptureFileName;
    if(!PathIsRegularFile(rgdCapture))
        return; // no Radeon GPU Detective capture in this package: nothing to decode (silent skip).

    AString<LogArena> rgdExecutable(arena);
    if(!ResolveRgdExecutable(arena, rgdExecutable)){
        outReport += "\n[gpu_detective]\nstatus=skipped\ndetail=rgd not found (set RadeonDeveloperToolSuite or PATH)\n";
        return;
    }

    const AString<LogArena> capturePath = PathToString<char>(arena, rgdCapture);
    const char* const argv[] = { rgdExecutable.c_str(), s_RgdParseArgument, capturePath.c_str(), nullptr };

    CrashReportText decoded(arena);
    if(!RunRgdCapture(arena, argv, decoded)){
        outReport += "\n[gpu_detective]\nstatus=decode_failed\n";
        return;
    }

    outReport += "\n[gpu_detective]\n";
    outReport.append(decoded.data(), decoded.size());
    if(decoded[decoded.size() - 1u] != '\n')
        outReport.push_back('\n');
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
