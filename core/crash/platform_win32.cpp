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

    Detail::NotifyCrashHandler(Detail::CrashReasonKind::WindowsException, exceptionCode);

    if(Detail::g_State.previousExceptionFilter)
        return Detail::g_State.previousExceptionFilter(exceptionInfo);

    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] static void __hidden_terminate_handler(){
    Detail::NotifyCrashHandler(Detail::CrashReasonKind::Terminate, 0u);
    std::abort();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Detail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RequestCrashHandler(const CrashReasonKind::Enum reasonKind, const u32 reasonCode, const u32 waitMilliseconds)noexcept{
    if(g_State.requestWriteHandle == INVALID_HANDLE_VALUE)
        return false;

    CrashRequest request;
    SnapshotCrashState(request, reasonKind, reasonCode);

    if(g_State.crashHandledEvent)
        ResetEvent(g_State.crashHandledEvent);

    if(!__hidden_crash_win32::__hidden_write_all(g_State.requestWriteHandle, &request, sizeof(request)))
        return false;

    if(g_State.crashHandledEvent && waitMilliseconds > 0u)
        return WaitForSingleObject(g_State.crashHandledEvent, waitMilliseconds) == WAIT_OBJECT_0;

    return true;
}

void NotifyCrashHandler(const CrashReasonKind::Enum reasonKind, const u32 reasonCode)noexcept{
    static_cast<void>(RequestCrashHandler(reasonKind, reasonCode, 3000u));
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

