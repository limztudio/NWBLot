// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "handler.h"
#include "package_internal.h"

#include <global/basic_string.h>
#include <global/numeric_text.h>

#include <cstdlib>

#if defined(NWB_PLATFORM_WINDOWS)
#include <global/blocking_io.h>
#if defined(_MSC_VER) && defined(NWB_DEBUG)
#include <crtdbg.h>
#endif
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX)
#include <global/blocking_io.h>

#include <signal.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_handler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr int s_ProcessSuccessExitCode = 0;
inline constexpr int s_ProcessFailureExitCode = -1;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static CrashDetail::CrashAck MakeAck(const CrashDetail::CrashRequest& request, const bool packageWritten)noexcept{
    CrashDetail::CrashAck ack;
    ack.packageWritten = packageWritten ? 1u : 0u;
    CopyFixedBuffer(ack.crashId, request.crashId);
    return ack;
}

[[nodiscard]] static bool SilenceProcess()noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#if defined(_MSC_VER) && defined(NWB_DEBUG)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_WARN, 0);
    _CrtSetReportMode(_CRT_ERROR, 0);
    _CrtSetReportMode(_CRT_ASSERT, 0);
#endif
    if(HWND consoleWindow = GetConsoleWindow())
        ShowWindow(consoleWindow, SW_HIDE);
#elif defined(NWB_PLATFORM_LINUX)
    if(signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        return false;
#endif
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunCrashHandlerProcess(const isize argc, tchar** argv){
    if(!__hidden_crash_handler::SilenceProcess())
        return __hidden_crash_handler::s_ProcessFailureExitCode;
    CrashDetail::InitializeDumpArena();

#if defined(NWB_PLATFORM_WINDOWS)
    HANDLE requestReadHandle = INVALID_HANDLE_VALUE;
    HANDLE ackWriteHandle = INVALID_HANDLE_VALUE;
    HANDLE ackEvent = nullptr;

    for(isize i = 1; i + 1 < argc; ++i){
        if(SafeStringView(argv[i]) == CrashDetail::s_RequestHandleArgument)
            requestReadHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(ParseDecimalPrefixU64(SafeStringView(argv[++i]))));
        else if(SafeStringView(argv[i]) == CrashDetail::s_AckHandleArgument)
            ackWriteHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(ParseDecimalPrefixU64(SafeStringView(argv[++i]))));
        else if(SafeStringView(argv[i]) == CrashDetail::s_AckEventArgument)
            ackEvent = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(ParseDecimalPrefixU64(SafeStringView(argv[++i]))));
    }

    if(requestReadHandle == INVALID_HANDLE_VALUE)
        return __hidden_crash_handler::s_ProcessFailureExitCode;

    for(;;){
        CrashDetail::CrashRequest request;
        if(!ReadAllWin32Handle(requestReadHandle, &request, sizeof(request)))
            break;
        // Defensive: reject a corrupt/interleaved request (writer serialization should prevent this).
        if(!CrashDetail::IsCurrentCrashRequest(request))
            break;

        const bool packageWritten = CrashDetail::WriteCrashPackage(request);
        if(ackWriteHandle != INVALID_HANDLE_VALUE){
            const CrashDetail::CrashAck ack = __hidden_crash_handler::MakeAck(request, packageWritten);
            if(!WriteAllWin32Handle(ackWriteHandle, &ack, sizeof(ack)))
                break;
        }
        if(ackEvent && !SetEvent(ackEvent))
            return __hidden_crash_handler::s_ProcessFailureExitCode;
        if(packageWritten){
            if(!CrashDetail::FlushCrashReportsForRequest(request))
                continue;
        }
    }

    return __hidden_crash_handler::s_ProcessSuccessExitCode;
#elif defined(NWB_PLATFORM_LINUX)
    int requestReadFd = -1;
    int ackWriteFd = -1;
    for(isize i = 1; i + 1 < argc; ++i){
        if(SafeStringView(argv[i]) == CrashDetail::s_RequestFdArgument)
            requestReadFd = static_cast<int>(ParseDecimalPrefixU64(SafeStringView(argv[++i])));
        else if(SafeStringView(argv[i]) == CrashDetail::s_AckFdArgument)
            ackWriteFd = static_cast<int>(ParseDecimalPrefixU64(SafeStringView(argv[++i])));
    }

    if(requestReadFd < 0)
        return __hidden_crash_handler::s_ProcessFailureExitCode;

    for(;;){
        CrashDetail::CrashRequest request;
        if(!ReadAllFileDescriptor(requestReadFd, &request, sizeof(request)))
            break;
        // Defensive: reject a corrupt/interleaved request (writer serialization should prevent this).
        if(!CrashDetail::IsCurrentCrashRequest(request))
            break;

        const bool packageWritten = CrashDetail::WriteCrashPackage(request);
        if(ackWriteFd >= 0){
            const CrashDetail::CrashAck ack = __hidden_crash_handler::MakeAck(request, packageWritten);
            if(!WriteAllFileDescriptor(ackWriteFd, &ack, sizeof(ack)))
                break;
        }
        if(packageWritten){
            if(!CrashDetail::FlushCrashReportsForRequest(request))
                continue;
        }
    }

    return __hidden_crash_handler::s_ProcessSuccessExitCode;
#else
    static_cast<void>(argc);
    static_cast<void>(argv);
    return __hidden_crash_handler::s_ProcessFailureExitCode;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

