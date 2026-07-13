
#include "internal.h"

#include <global/blocking_io.h>

#include <cstdlib>
#include <cstring>
#include <exception>

#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_win32{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline constexpr usize s_HandlerCommandLineReserveSlack = 160u;
inline constexpr DWORD s_HandlerExitWaitMilliseconds = 3000u;


[[nodiscard]] static bool __hidden_ack_matches_request(
    const Detail::CrashAck& ack,
    const Detail::CrashRequest& request
)noexcept{
    return ack.magic == Detail::s_AckMagic
        && ack.version == Detail::s_RequestVersion
        && AStringView(ack.crashId) == AStringView(request.crashId)
    ;
}

static void __hidden_drain_pending_acks(const HANDLE ackReadHandle)noexcept{
    if(ackReadHandle == INVALID_HANDLE_VALUE)
        return;

    for(;;){
        DWORD availableBytes = 0u;
        if(!PeekNamedPipe(ackReadHandle, nullptr, 0u, nullptr, &availableBytes, nullptr))
            return;
        if(availableBytes < sizeof(Detail::CrashAck))
            return;

        Detail::CrashAck ignoredAck;
        if(!ReadAllWin32Handle(ackReadHandle, &ignoredAck, sizeof(ignoredAck)))
            return;
    }
}

static LONG WINAPI __hidden_unhandled_exception_filter(EXCEPTION_POINTERS* exceptionInfo){
    const u32 exceptionCode = exceptionInfo && exceptionInfo->ExceptionRecord
        ? static_cast<u32>(exceptionInfo->ExceptionRecord->ExceptionCode)
        : 0u
    ;

    Detail::CrashDumpRequestOptions options;
    options.exceptionPointers = static_cast<u64>(reinterpret_cast<usize>(exceptionInfo));
    if(exceptionInfo && exceptionInfo->ExceptionRecord)
        options.instructionPointer = static_cast<u64>(reinterpret_cast<usize>(exceptionInfo->ExceptionRecord->ExceptionAddress));

    if(!Detail::TryConsumeSuppressedPlatformCrashCapture())
        Detail::NotifyCrashHandler(Detail::CrashReasonKind::WindowsException, exceptionCode, options);

    if(Detail::g_State.previousExceptionFilter)
        return Detail::g_State.previousExceptionFilter(exceptionInfo);

    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] static void __hidden_terminate_handler(){
    Detail::NotifyCrashHandler(Detail::CrashReasonKind::Terminate, 0u);
    std::abort();
}

static u64 __hidden_context_instruction_pointer(const CONTEXT& context)noexcept{
#if defined(_M_X64) || defined(__x86_64__)
    return static_cast<u64>(context.Rip);
#elif defined(_M_IX86) || defined(__i386__)
    return static_cast<u64>(context.Eip);
#elif defined(_M_ARM64) || defined(__aarch64__)
    return static_cast<u64>(context.Pc);
#else
    return 0u;
#endif
}

static u64 __hidden_context_stack_pointer(const CONTEXT& context)noexcept{
#if defined(_M_X64) || defined(__x86_64__)
    return static_cast<u64>(context.Rsp);
#elif defined(_M_IX86) || defined(__i386__)
    return static_cast<u64>(context.Esp);
#elif defined(_M_ARM64) || defined(__aarch64__)
    return static_cast<u64>(context.Sp);
#else
    return 0u;
#endif
}

static u64 __hidden_context_frame_pointer(const CONTEXT& context)noexcept{
#if defined(_M_X64) || defined(__x86_64__)
    return static_cast<u64>(context.Rbp);
#elif defined(_M_IX86) || defined(__i386__)
    return static_cast<u64>(context.Ebp);
#elif defined(_M_ARM64) || defined(__aarch64__)
    return static_cast<u64>(context.Fp);
#else
    return 0u;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CaptureManualDumpContext(CrashDumpRequestOptions& outOptions, ManualDumpContextStorage& storage)noexcept{
    RtlCaptureContext(&storage.context);

    storage.exceptionRecord = EXCEPTION_RECORD{};
    storage.exceptionRecord.ExceptionCode = s_ManualDumpExceptionCode;
    storage.exceptionRecord.ExceptionAddress = reinterpret_cast<void*>(static_cast<usize>(
        __hidden_crash_win32::__hidden_context_instruction_pointer(storage.context)
    ));
    storage.exceptionPointers.ExceptionRecord = &storage.exceptionRecord;
    storage.exceptionPointers.ContextRecord = &storage.context;

    outOptions.exceptionPointers = static_cast<u64>(reinterpret_cast<usize>(&storage.exceptionPointers));
    outOptions.instructionPointer = __hidden_crash_win32::__hidden_context_instruction_pointer(storage.context);
    outOptions.stackPointer = __hidden_crash_win32::__hidden_context_stack_pointer(storage.context);
    outOptions.framePointer = __hidden_crash_win32::__hidden_context_frame_pointer(storage.context);

    // Capture the full call stack here, in-process, where every module is loaded and unwinding is reliable
    // (mirrors the POSIX frame-pointer walk). The log server then only has to resolve these addresses to
    // symbols against the minidump's module map -- it does NOT have to unwind the minidump server-side, which
    // needs module unwind data (.pdata) the server may not have, and which produced only the capture frame.
    // The leading capture-internal frames are dropped downstream via options.callstackFramesToSkip.
    void* backTrace[s_MaxCallstackFrames] = {};
    const USHORT capturedFrames = RtlCaptureStackBackTrace(0u, static_cast<ULONG>(s_MaxCallstackFrames), backTrace, nullptr);
    outOptions.callstackFrameCount = 0u;
    for(USHORT i = 0u; i < capturedFrames; ++i){
        const u64 address = static_cast<u64>(reinterpret_cast<usize>(backTrace[i]));
        if(address != 0u)
            outOptions.callstackFrames[outOptions.callstackFrameCount++] = address;
    }
}

CrashDumpTransportStatus::Enum RequestCrashHandler(const CrashRequest& request, const u32 waitMilliseconds)noexcept{
    if(g_State.requestWriteHandle == INVALID_HANDLE_VALUE)
        return CrashDumpTransportStatus::Failed;

    // Re-entrancy guard: a fault while already inside the transport (e.g. the SEH filter itself faulting)
    // must not re-enter and corrupt the in-flight request or self-deadlock on the channel.
    static thread_local bool s_inTransport = false;
    if(s_inTransport)
        return CrashDumpTransportStatus::Failed;
    s_inTransport = true;

    // Serialize concurrent writers: if two threads fault at once, only one owns the single request pipe + ack
    // event; the other bails (its sibling's report is still written). One async-safe CAS, never blocks.
    u32 expected = 0u;
    if(!g_State.transportInFlight.compare_exchange_strong(expected, 1u, MemoryOrder::acq_rel, MemoryOrder::acquire)){
        s_inTransport = false;
        return CrashDumpTransportStatus::Sent;
    }

    CrashDumpTransportStatus::Enum status = CrashDumpTransportStatus::Failed;
    do{
        __hidden_crash_win32::__hidden_drain_pending_acks(g_State.ackReadHandle);

        if(g_State.crashHandledEvent)
            ResetEvent(g_State.crashHandledEvent);

        if(!WriteAllWin32Handle(g_State.requestWriteHandle, &request, sizeof(request))){
            status = CrashDumpTransportStatus::Failed;
            break;
        }

        if(!g_State.crashHandledEvent || g_State.ackReadHandle == INVALID_HANDLE_VALUE || waitMilliseconds == 0u){
            status = CrashDumpTransportStatus::Sent;
            break;
        }

        // Wake on the ack event OR the handler process dying, so a dead/wedged handler never costs the full
        // timeout. Falls back to a single-object wait if the handler process handle is unavailable.
        HANDLE waitHandles[2] = { g_State.crashHandledEvent, g_State.handlerProcessInfo.hProcess };
        const DWORD waitCount = g_State.handlerProcessInfo.hProcess ? 2u : 1u;
        const DWORD waitResult = WaitForMultipleObjects(waitCount, waitHandles, FALSE, waitMilliseconds);
        if(waitResult == WAIT_OBJECT_0){
            CrashAck ack;
            if(!ReadAllWin32Handle(g_State.ackReadHandle, &ack, sizeof(ack))){
                status = CrashDumpTransportStatus::Failed;
                break;
            }
            if(!__hidden_crash_win32::__hidden_ack_matches_request(ack, request)){
                status = CrashDumpTransportStatus::Failed;
                break;
            }
            status = ack.packageWritten
                ? CrashDumpTransportStatus::PackageWritten
                : CrashDumpTransportStatus::PackageWriteFailed
            ;
            break;
        }
        if(waitResult == WAIT_TIMEOUT){
            status = CrashDumpTransportStatus::TimedOut;
            break;
        }
        // WAIT_OBJECT_0 + 1 (handler process exited) or WAIT_FAILED: treat as a failed transport.
        status = CrashDumpTransportStatus::Failed;
    }while(false);

    g_State.transportInFlight.store(0u, MemoryOrder::release);
    s_inTransport = false;
    return status;
}

template<typename ArenaT>
bool StartDesktopHandler(const ::Path<ArenaT>& handlerExecutablePath){
    if(g_State.handlerStarted)
        return true;
    if(handlerExecutablePath.empty())
        return false;

    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    HANDLE requestReadHandle = INVALID_HANDLE_VALUE;
    HANDLE requestWriteHandle = INVALID_HANDLE_VALUE;
    // Size the request pipe for a whole CrashRequest so a single write never blocks waiting for the handler to
    // drain (the POD exceeds the 64 KB default), which combined with the handler-death wakeup bounds the writer.
    if(!CreatePipe(&requestReadHandle, &requestWriteHandle, &securityAttributes, static_cast<DWORD>(sizeof(CrashRequest))))
        return false;

    if(!SetHandleInformation(requestWriteHandle, HANDLE_FLAG_INHERIT, 0u)){
        CloseHandle(requestReadHandle);
        CloseHandle(requestWriteHandle);
        return false;
    }

    HANDLE ackReadHandle = INVALID_HANDLE_VALUE;
    HANDLE ackWriteHandle = INVALID_HANDLE_VALUE;
    if(!CreatePipe(&ackReadHandle, &ackWriteHandle, &securityAttributes, 0u)){
        CloseHandle(requestReadHandle);
        CloseHandle(requestWriteHandle);
        return false;
    }

    if(!SetHandleInformation(ackReadHandle, HANDLE_FLAG_INHERIT, 0u)){
        CloseHandle(requestReadHandle);
        CloseHandle(requestWriteHandle);
        CloseHandle(ackReadHandle);
        CloseHandle(ackWriteHandle);
        return false;
    }

    HANDLE ackEvent = CreateEventW(&securityAttributes, TRUE, FALSE, nullptr);
    if(!ackEvent){
        CloseHandle(requestReadHandle);
        CloseHandle(requestWriteHandle);
        CloseHandle(ackReadHandle);
        CloseHandle(ackWriteHandle);
        return false;
    }

    char requestHandleText[s_HandlerArgumentTextCapacity] = {};
    char ackHandleText[s_HandlerArgumentTextCapacity] = {};
    char ackEventText[s_HandlerArgumentTextCapacity] = {};
    AppendUnsignedToFixedBuffer(requestHandleText, reinterpret_cast<uintptr_t>(requestReadHandle));
    AppendUnsignedToFixedBuffer(ackHandleText, reinterpret_cast<uintptr_t>(ackWriteHandle));
    AppendUnsignedToFixedBuffer(ackEventText, reinterpret_cast<uintptr_t>(ackEvent));

    TString<ArenaT> commandLine(handlerExecutablePath.arena());
    commandLine.reserve(handlerExecutablePath.size() + __hidden_crash_win32::s_HandlerCommandLineReserveSlack);
    commandLine += NWB_TEXT("\"");
    commandLine += handlerExecutablePath.native();
    commandLine += NWB_TEXT("\" ");
    commandLine += s_RequestHandleArgument;
    commandLine += NWB_TEXT(" ");
    commandLine += StringConvert(handlerExecutablePath.arena(), AStringView(requestHandleText));
    commandLine += NWB_TEXT(" ");
    commandLine += s_AckHandleArgument;
    commandLine += NWB_TEXT(" ");
    commandLine += StringConvert(handlerExecutablePath.arena(), AStringView(ackHandleText));
    commandLine += NWB_TEXT(" ");
    commandLine += s_AckEventArgument;
    commandLine += NWB_TEXT(" ");
    commandLine += StringConvert(handlerExecutablePath.arena(), AStringView(ackEventText));

    STARTUPINFO startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION processInfo = {};
    const DWORD creationFlags = CREATE_NO_WINDOW | DETACHED_PROCESS;
    const BOOL created = CreateProcess(
        handlerExecutablePath.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        creationFlags,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo
    );

    CloseHandle(requestReadHandle);
    CloseHandle(ackWriteHandle);
    SetHandleInformation(ackEvent, HANDLE_FLAG_INHERIT, 0u);

    if(!created){
        CloseHandle(requestWriteHandle);
        CloseHandle(ackReadHandle);
        CloseHandle(ackEvent);
        return false;
    }

    g_State.requestWriteHandle = requestWriteHandle;
    g_State.ackReadHandle = ackReadHandle;
    g_State.crashHandledEvent = ackEvent;
    g_State.handlerProcessInfo = processInfo;
    g_State.handlerStarted = true;
    return true;
}

template bool StartDesktopHandler(const ::Path<Alloc::PersistentArena>& handlerExecutablePath);

void InstallPlatformHandlers(){
    g_State.previousExceptionFilter = SetUnhandledExceptionFilter(__hidden_crash_win32::__hidden_unhandled_exception_filter);
    std::set_terminate(__hidden_crash_win32::__hidden_terminate_handler);
}

void UninstallPlatformResources(){
    if(g_State.requestWriteHandle != INVALID_HANDLE_VALUE){
        CloseHandle(g_State.requestWriteHandle);
        g_State.requestWriteHandle = INVALID_HANDLE_VALUE;
    }

    if(g_State.ackReadHandle != INVALID_HANDLE_VALUE){
        CloseHandle(g_State.ackReadHandle);
        g_State.ackReadHandle = INVALID_HANDLE_VALUE;
    }

    if(g_State.handlerProcessInfo.hProcess)
        WaitForSingleObject(g_State.handlerProcessInfo.hProcess, __hidden_crash_win32::s_HandlerExitWaitMilliseconds);

    if(g_State.crashHandledEvent){
        CloseHandle(g_State.crashHandledEvent);
        g_State.crashHandledEvent = nullptr;
    }

    if(g_State.handlerProcessInfo.hThread){
        CloseHandle(g_State.handlerProcessInfo.hThread);
        g_State.handlerProcessInfo.hThread = nullptr;
    }

    if(g_State.handlerProcessInfo.hProcess){
        CloseHandle(g_State.handlerProcessInfo.hProcess);
        g_State.handlerProcessInfo.hProcess = nullptr;
    }

    SetUnhandledExceptionFilter(g_State.previousExceptionFilter);
    g_State.previousExceptionFilter = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

