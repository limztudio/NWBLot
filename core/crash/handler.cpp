// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "package_internal.h"

#include <cstdlib>

#if defined(NWB_PLATFORM_WINDOWS)
#include <global/blocking_io.h>
#if defined(_MSC_VER) && !defined(NDEBUG)
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


inline constexpr u64 s_DecimalRadix = 10u;
inline constexpr int s_ProcessSuccessExitCode = 0;
inline constexpr int s_ProcessFailureExitCode = -1;


template<typename CharT>
[[nodiscard]] static bool __hidden_arg_equals(const CharT* lhs, const CharT* rhs)noexcept{
    if(!lhs || !rhs)
        return false;

    while(*lhs || *rhs){
        if(*lhs != *rhs)
            return false;
        ++lhs;
        ++rhs;
    }
    return true;
}

template<typename CharT>
[[nodiscard]] static u64 __hidden_parse_u64(const CharT* text)noexcept{
    u64 value = 0u;
    if(!text)
        return value;

    while(*text >= static_cast<CharT>('0') && *text <= static_cast<CharT>('9')){
        value = (value * s_DecimalRadix) + static_cast<u64>(*text - static_cast<CharT>('0'));
        ++text;
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
#if defined(NWB_PLATFORM_WINDOWS)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#if defined(_MSC_VER) && !defined(NDEBUG)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_WARN, 0);
    _CrtSetReportMode(_CRT_ERROR, 0);
    _CrtSetReportMode(_CRT_ASSERT, 0);
#endif
    if(HWND consoleWindow = GetConsoleWindow())
        ShowWindow(consoleWindow, SW_HIDE);
#elif defined(NWB_PLATFORM_LINUX)
    signal(SIGPIPE, SIG_IGN);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int RunCrashHandlerProcess(const isize argc, tchar** argv){
    __hidden_crash_handler::__hidden_silence_process();
    [[maybe_unused]] auto& dumpArena = Detail::DumpArena();

#if defined(NWB_PLATFORM_WINDOWS)
    HANDLE requestReadHandle = INVALID_HANDLE_VALUE;
    HANDLE ackWriteHandle = INVALID_HANDLE_VALUE;
    HANDLE ackEvent = nullptr;

    for(isize i = 1; i + 1 < argc; ++i){
        if(__hidden_crash_handler::__hidden_arg_equals(argv[i], Detail::s_RequestHandleArgument))
            requestReadHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(__hidden_crash_handler::__hidden_parse_u64(argv[++i])));
        else if(__hidden_crash_handler::__hidden_arg_equals(argv[i], Detail::s_AckHandleArgument))
            ackWriteHandle = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(__hidden_crash_handler::__hidden_parse_u64(argv[++i])));
        else if(__hidden_crash_handler::__hidden_arg_equals(argv[i], Detail::s_AckEventArgument))
            ackEvent = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(__hidden_crash_handler::__hidden_parse_u64(argv[++i])));
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
            [[maybe_unused]] const bool ackWritten = WriteAllWin32Handle(ackWriteHandle, &ack, sizeof(ack));
        }
        if(ackEvent)
            SetEvent(ackEvent);
        if(packageWritten){
            [[maybe_unused]] const bool reportsFlushed = Detail::FlushCrashReportsForRequest(request);
        }
    }

    return __hidden_crash_handler::s_ProcessSuccessExitCode;
#elif defined(NWB_PLATFORM_LINUX)
    int requestReadFd = -1;
    int ackWriteFd = -1;
    for(isize i = 1; i + 1 < argc; ++i){
        if(__hidden_crash_handler::__hidden_arg_equals(argv[i], Detail::s_RequestFdArgument))
            requestReadFd = static_cast<int>(__hidden_crash_handler::__hidden_parse_u64(argv[++i]));
        else if(__hidden_crash_handler::__hidden_arg_equals(argv[i], Detail::s_AckFdArgument))
            ackWriteFd = static_cast<int>(__hidden_crash_handler::__hidden_parse_u64(argv[++i]));
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
            [[maybe_unused]] const bool ackWritten = WriteAllFileDescriptor(ackWriteFd, &ack, sizeof(ack));
        }
        if(packageWritten){
            [[maybe_unused]] const bool reportsFlushed = Detail::FlushCrashReportsForRequest(request);
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

