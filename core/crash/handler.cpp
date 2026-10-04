// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "handler.h"
#include "package_internal.h"

#include <global/basic_string.h>

#include <cstdlib>

#if defined(GLB_PLATFORM_WINDOWS)
#include <global/blocking_io.h>
#if defined(_MSC_VER) && defined(GLB_DEBUG)
#include <crtdbg.h>
#endif
#include <windows.h>
#elif defined(GLB_PLATFORM_LINUX)
#include <global/blocking_io.h>
#include <signal.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_handler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u64 s_DecimalRadix = 10u;
inline constexpr char s_DecimalDigitFirst = '0';
inline constexpr char s_DecimalDigitLast = '9';
inline constexpr int s_ProcessSuccessExitCode = 0;
inline constexpr int s_ProcessFailureExitCode = -1;


[[nodiscard]] static u64 __hidden_parse_u64(const TStringView text)noexcept{
    u64 value = 0u;
    for(const tchar ch : text){
        if(ch < static_cast<tchar>(s_DecimalDigitFirst) || ch > static_cast<tchar>(s_DecimalDigitLast))
            break;
        value = (value * s_DecimalRadix) + static_cast<u64>(ch - static_cast<tchar>(s_DecimalDigitFirst));
    }
    return value;
}

static Detail::CrashAck __hidden_make_ack(const Detail::CrashRequest& request, const bool packageWritten)noexcept{
    Detail::CrashAck ack;
    ack.packageWritten = packageWritten ? 1u : 0u;
    CopyFixedBuffer(ack.crashId, request.crashId);
    return ack;
}

static void __hidden_silence_process()noexcept{
#if defined(GLB_PLATFORM_WINDOWS)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#if defined(_MSC_VER) && defined(GLB_DEBUG)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_WARN, 0);
    _CrtSetReportMode(_CRT_ERROR, 0);
    _CrtSetReportMode(_CRT_ASSERT, 0);
#endif
    if(HWND consoleWindow = GetConsoleWindow())
        ShowWindow(consoleWindow, SW_HIDE);
#elif defined(GLB_PLATFORM_LINUX)
    signal(SIGPIPE, SIG_IGN);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunCrashHandlerProcess(const isize argc, tchar** argv){
    __hidden_crash_handler::__hidden_silence_process();
    Detail::InitializeDumpArena();

#if defined(GLB_PLATFORM_WINDOWS)
    HANDLE requestReadHandle = INVALID_HANDLE_VALUE;
    HANDLE ackWriteHandle = INVALID_HANDLE_VALUE;
    HANDLE ackEvent = nullptr;

    for(isize i = 1; i + 1 < argc; ++i){
        if(SafeStringView(argv[i]) == Detail::s_RequestHandleArgument)
            requestReadHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(__hidden_crash_handler::__hidden_parse_u64(SafeStringView(argv[++i]))));
        else if(SafeStringView(argv[i]) == Detail::s_AckHandleArgument)
            ackWriteHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(__hidden_crash_handler::__hidden_parse_u64(SafeStringView(argv[++i]))));
        else if(SafeStringView(argv[i]) == Detail::s_AckEventArgument)
            ackEvent = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(__hidden_crash_handler::__hidden_parse_u64(SafeStringView(argv[++i]))));
    }

    if(requestReadHandle == INVALID_HANDLE_VALUE)
        return __hidden_crash_handler::s_ProcessFailureExitCode;

    for(;;){
        Detail::CrashRequest request;
        if(!ReadAllWin32Handle(requestReadHandle, &request, sizeof(request)))
            break;
        // Defensive: reject a corrupt/interleaved request (writer serialization should prevent this).
        if(request.magic != Detail::s_RequestMagic || request.version != Detail::s_RequestVersion)
            break;

        const bool packageWritten = Detail::WriteCrashPackage(request);
        if(ackWriteHandle != INVALID_HANDLE_VALUE){
            const Detail::CrashAck ack = __hidden_crash_handler::__hidden_make_ack(request, packageWritten);
            if(!WriteAllWin32Handle(ackWriteHandle, &ack, sizeof(ack)))
                break;
        }
        if(ackEvent)
            SetEvent(ackEvent);
        if(packageWritten){
            if(!Detail::FlushCrashReportsForRequest(request))
                continue;
        }
    }

    return __hidden_crash_handler::s_ProcessSuccessExitCode;
#elif defined(GLB_PLATFORM_LINUX)
    int requestReadFd = -1;
    int ackWriteFd = -1;
    for(isize i = 1; i + 1 < argc; ++i){
        if(SafeStringView(argv[i]) == Detail::s_RequestFdArgument)
            requestReadFd = static_cast<int>(__hidden_crash_handler::__hidden_parse_u64(SafeStringView(argv[++i])));
        else if(SafeStringView(argv[i]) == Detail::s_AckFdArgument)
            ackWriteFd = static_cast<int>(__hidden_crash_handler::__hidden_parse_u64(SafeStringView(argv[++i])));
    }

    if(requestReadFd < 0)
        return __hidden_crash_handler::s_ProcessFailureExitCode;

    for(;;){
        Detail::CrashRequest request;
        if(!ReadAllFileDescriptor(requestReadFd, &request, sizeof(request)))
            break;
        // Defensive: reject a corrupt/interleaved request (writer serialization should prevent this).
        if(request.magic != Detail::s_RequestMagic || request.version != Detail::s_RequestVersion)
            break;

        const bool packageWritten = Detail::WriteCrashPackage(request);
        if(ackWriteFd >= 0){
            const Detail::CrashAck ack = __hidden_crash_handler::__hidden_make_ack(request, packageWritten);
            if(!WriteAllFileDescriptor(ackWriteFd, &ack, sizeof(ack)))
                break;
        }
        if(packageWritten){
            if(!Detail::FlushCrashReportsForRequest(request))
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

