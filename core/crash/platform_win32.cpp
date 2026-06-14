// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"

#include <cstdlib>
#include <cstring>
#include <exception>

#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_win32{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool __hidden_write_all(const HANDLE handle, const void* const data, const usize byteCount)noexcept{
    const u8* cursor = static_cast<const u8*>(data);
    usize remaining = byteCount;
    while(remaining > 0u){
        DWORD bytesWritten = 0u;
        const DWORD requestSize = remaining > static_cast<usize>(Limit<DWORD>::s_Max)
            ? Limit<DWORD>::s_Max
            : static_cast<DWORD>(remaining)
        ;
        if(!WriteFile(handle, cursor, requestSize, &bytesWritten, nullptr) || bytesWritten == 0u)
            return false;

        cursor += bytesWritten;
        remaining -= bytesWritten;
    }
    return true;
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
    storage.exceptionRecord.ExceptionCode = 0xE0425742u;
    storage.exceptionRecord.ExceptionAddress = reinterpret_cast<void*>(static_cast<usize>(
        __hidden_crash_win32::__hidden_context_instruction_pointer(storage.context)
    ));
    storage.exceptionPointers.ExceptionRecord = &storage.exceptionRecord;
    storage.exceptionPointers.ContextRecord = &storage.context;

    outOptions.exceptionPointers = static_cast<u64>(reinterpret_cast<usize>(&storage.exceptionPointers));
    outOptions.instructionPointer = __hidden_crash_win32::__hidden_context_instruction_pointer(storage.context);
    outOptions.stackPointer = __hidden_crash_win32::__hidden_context_stack_pointer(storage.context);
    outOptions.framePointer = __hidden_crash_win32::__hidden_context_frame_pointer(storage.context);
}

CrashDumpTransportStatus::Enum RequestCrashHandler(const CrashRequest& request, const u32 waitMilliseconds)noexcept{
    if(g_State.requestWriteHandle == INVALID_HANDLE_VALUE)
        return CrashDumpTransportStatus::Failed;

    if(g_State.crashHandledEvent)
        ResetEvent(g_State.crashHandledEvent);

    if(!__hidden_crash_win32::__hidden_write_all(g_State.requestWriteHandle, &request, sizeof(request)))
        return CrashDumpTransportStatus::Failed;

    if(!g_State.crashHandledEvent || waitMilliseconds == 0u)
        return CrashDumpTransportStatus::Sent;

    const DWORD waitResult = WaitForSingleObject(g_State.crashHandledEvent, waitMilliseconds);
    if(waitResult == WAIT_OBJECT_0)
        return CrashDumpTransportStatus::Acknowledged;
    if(waitResult == WAIT_TIMEOUT)
        return CrashDumpTransportStatus::TimedOut;

    return CrashDumpTransportStatus::Failed;
}

void NotifyCrashHandler(const CrashReasonKind::Enum reasonKind, const u32 reasonCode, const CrashDumpRequestOptions& options)noexcept{
    CrashDumpRequestOptions requestOptions = options;
    requestOptions.waitMilliseconds = 3000u;
    static_cast<void>(RequestCrashDump(reasonKind, reasonCode, requestOptions));
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
    if(!CreatePipe(&requestReadHandle, &requestWriteHandle, &securityAttributes, 0u))
        return false;

    SetHandleInformation(requestWriteHandle, HANDLE_FLAG_INHERIT, 0u);

    HANDLE ackEvent = CreateEventW(&securityAttributes, TRUE, FALSE, nullptr);
    if(!ackEvent){
        CloseHandle(requestReadHandle);
        CloseHandle(requestWriteHandle);
        return false;
    }

    char requestHandleText[32] = {};
    char ackEventText[32] = {};
    AppendUnsignedToFixedBuffer(requestHandleText, reinterpret_cast<uintptr_t>(requestReadHandle));
    AppendUnsignedToFixedBuffer(ackEventText, reinterpret_cast<uintptr_t>(ackEvent));

    TString<ArenaT> commandLine(handlerExecutablePath.arena());
    commandLine.reserve(handlerExecutablePath.size() + 96u);
    commandLine += NWB_TEXT("\"");
    commandLine += handlerExecutablePath.native();
    commandLine += NWB_TEXT("\" --request-handle ");
    commandLine += StringConvert(handlerExecutablePath.arena(), AStringView(requestHandleText));
    commandLine += NWB_TEXT(" --ack-event ");
    commandLine += StringConvert(handlerExecutablePath.arena(), AStringView(ackEventText));

    STARTUPINFO startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo = {};
    const BOOL created = CreateProcess(
        handlerExecutablePath.c_str(),
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

    CloseHandle(requestReadHandle);

    if(!created){
        CloseHandle(requestWriteHandle);
        CloseHandle(ackEvent);
        return false;
    }

    g_State.requestWriteHandle = requestWriteHandle;
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

