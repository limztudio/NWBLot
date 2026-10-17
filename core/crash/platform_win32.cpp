// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "internal.h"

#include <global/blocking_io.h>
#include <global/scope_exit.h>
#include <global/termination.h>

#include <cstring>
#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_win32{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr DWORD s_HandlerExitWaitMilliseconds = 3000u;
inline constexpr DWORD s_CrashWaitHandleCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Detail::PlatformFailure NativeFailure(const Detail::PlatformOperation::Enum operation, const DWORD code)noexcept{
    return { operation, Detail::PlatformErrorDomain::Windows, static_cast<u64>(code) };
}

[[nodiscard]] static Expected<void, Detail::PlatformFailure> ReleaseOwnedHandle(HANDLE& handle, const HANDLE emptyValue = nullptr)noexcept{
    const HANDLE owned = handle;
    handle = emptyValue;
    if(!owned || owned == INVALID_HANDLE_VALUE)
        return {};
    if(!CloseHandle(owned))
        return MakeUnexpected(NativeFailure(Detail::PlatformOperation::CloseHandle, GetLastError()));
    return {};
}

static void CleanupOwnedHandle(HANDLE& handle, Detail::PlatformCleanupObservations& observations, const HANDLE emptyValue = nullptr)noexcept{
    const auto released = ReleaseOwnedHandle(handle, emptyValue);
    if(!released)
        observations.record(released.error());
}

static void WaitForOwnedProcess(const HANDLE process, const bool terminateImmediately, Detail::PlatformCleanupObservations& observations)noexcept{
    if(!process || process == INVALID_HANDLE_VALUE)
        return;
    DWORD waited = WaitForSingleObject(process, terminateImmediately ? 0u : s_HandlerExitWaitMilliseconds);
    bool terminated = false;
    if(!terminateImmediately && waited != WAIT_OBJECT_0){
        // Preserve detached upload completion after the existing bounded normal shutdown wait.
        observations.record(NativeFailure(Detail::PlatformOperation::WaitProcess, waited == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT));
        return;
    }
    if(waited != WAIT_OBJECT_0){
        if(waited != WAIT_TIMEOUT)
            observations.record(NativeFailure(Detail::PlatformOperation::WaitProcess, waited == WAIT_FAILED ? GetLastError() : ERROR_INVALID_DATA));
        if(!TerminateProcess(process, ERROR_CANCELLED)){
            const DWORD error = GetLastError();
            waited = WaitForSingleObject(process, 0u);
            if(waited != WAIT_OBJECT_0)
                observations.record(NativeFailure(Detail::PlatformOperation::TerminateProcess, error));
        }
        else{
            terminated = true;
            waited = WaitForSingleObject(process, s_HandlerExitWaitMilliseconds);
        }
        if(waited != WAIT_OBJECT_0){
            observations.record(NativeFailure(Detail::PlatformOperation::WaitProcess, waited == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT));
            return;
        }
    }
    DWORD exitCode = 0u;
    if(!GetExitCodeProcess(process, &exitCode))
        observations.record(NativeFailure(Detail::PlatformOperation::WaitProcess, GetLastError()));
    else if(exitCode != 0u && !terminated)
        observations.record({ Detail::PlatformOperation::WaitProcess, Detail::PlatformErrorDomain::ExitCode, static_cast<u64>(exitCode) });
}

static void DrainPendingAcks(const HANDLE ackReadHandle)noexcept{
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

static LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionInfo){
    const u32 exceptionCode = exceptionInfo && exceptionInfo->ExceptionRecord ? static_cast<u32>(exceptionInfo->ExceptionRecord->ExceptionCode) : 0u;

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

[[noreturn]] static void TerminateHandler()noexcept{
    Detail::NotifyCrashHandler(Detail::CrashReasonKind::Terminate, 0u);
    TerminateInvariant();
}

static u64 ContextInstructionPointer(const CONTEXT& context)noexcept{
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

static u64 ContextStackPointer(const CONTEXT& context)noexcept{
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

static u64 ContextFramePointer(const CONTEXT& context)noexcept{
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
    storage.exceptionRecord.ExceptionAddress = reinterpret_cast<void*>(static_cast<usize>(__hidden_crash_win32::ContextInstructionPointer(storage.context)));
    storage.exceptionPointers.ExceptionRecord = &storage.exceptionRecord;
    storage.exceptionPointers.ContextRecord = &storage.context;

    outOptions.exceptionPointers = 0u;
    outOptions.manualContext = &storage;
    outOptions.instructionPointer = __hidden_crash_win32::ContextInstructionPointer(storage.context);
    outOptions.stackPointer = __hidden_crash_win32::ContextStackPointer(storage.context);
    outOptions.framePointer = __hidden_crash_win32::ContextFramePointer(storage.context);

    // Capture in-process (all modules loaded); server only resolves symbols, never unwinds (no .pdata needed).
    // Leading capture frames dropped via options.callstackFramesToSkip.
    void* backTrace[s_MaxCallstackFrames];
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

    // Re-entrancy guard: a fault while already inside the transport (e.g. the SEH filter itself faulting) must not re-enter and corrupt the in-flight request or self-deadlock on the channel.
    static thread_local bool s_inTransport = false;
    if(s_inTransport)
        return CrashDumpTransportStatus::Failed;
    s_inTransport = true;

    // Serialize concurrent writers: if two threads fault at once, only one owns the single request pipe + ack event; the other bails (its sibling's report is still written).
    // One async-safe CAS, never blocks.
    u32 expected = 0u;
    if(!g_State.transportInFlight.compare_exchange_strong(expected, 1u, MemoryOrder::acq_rel, MemoryOrder::acquire)){
        s_inTransport = false;
        return CrashDumpTransportStatus::Sent;
    }

    CrashDumpTransportStatus::Enum status = CrashDumpTransportStatus::Failed;
    do{
        __hidden_crash_win32::DrainPendingAcks(g_State.ackReadHandle);

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

        // Wake on the ack event OR the handler process dying, so a dead/wedged handler never costs the full timeout.
        // Falls back to a single-object wait if the handler process handle is unavailable.
        HANDLE waitHandles[__hidden_crash_win32::s_CrashWaitHandleCount] = { g_State.crashHandledEvent, g_State.handlerProcessInfo.hProcess };
        const DWORD waitCount = g_State.handlerProcessInfo.hProcess ? __hidden_crash_win32::s_CrashWaitHandleCount : 1u;
        const DWORD waitResult = WaitForMultipleObjects(waitCount, waitHandles, FALSE, waitMilliseconds);
        if(waitResult == WAIT_OBJECT_0){
            CrashAck ack;
            if(!ReadAllWin32Handle(g_State.ackReadHandle, &ack, sizeof(ack))){
                status = CrashDumpTransportStatus::Failed;
                break;
            }
            if(!CrashAckMatchesRequest(ack, request)){
                status = CrashDumpTransportStatus::Failed;
                break;
            }
            status = ack.packageWritten ? CrashDumpTransportStatus::PackageWritten : CrashDumpTransportStatus::PackageWriteFailed;
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
Expected<void, PlatformFailure> StartDesktopHandler(const ::Path<ArenaT>& handlerExecutablePath, Alloc::ScratchArena& scratchArena, PlatformCleanupObservations& observations){
    if(g_State.handlerStarted)
        return {};
    constexpr usize s_MaxCommandCharacters = 32766u;
    constexpr usize s_HandleTextCharacters = 20u;
    constexpr usize s_ArgumentCharacters = s_RequestHandleArgument.size() + s_AckHandleArgument.size() + s_AckEventArgument.size() + 3u * s_HandleTextCharacters + 8u;
    if(handlerExecutablePath.empty())
        return MakeUnexpected(__hidden_crash_win32::NativeFailure(PlatformOperation::PrepareCommand, ERROR_INVALID_PARAMETER));
    if(handlerExecutablePath.size() > s_MaxCommandCharacters - s_ArgumentCharacters)
        return MakeUnexpected(__hidden_crash_win32::NativeFailure(PlatformOperation::PrepareCommand, ERROR_FILENAME_EXCED_RANGE));

    // Allocate all command and attribute storage before the first native handle exists.
    TString<Alloc::ScratchArena> commandLine(scratchArena);
    commandLine.resize(handlerExecutablePath.size() + s_ArgumentCharacters);
    SIZE_T attributeListBytes = 0u;
    const BOOL sizeQueried = InitializeProcThreadAttributeList(nullptr, 1u, 0u, &attributeListBytes);
    const DWORD sizeError = sizeQueried ? ERROR_SUCCESS : GetLastError();
    if(sizeQueried || sizeError != ERROR_INSUFFICIENT_BUFFER || attributeListBytes == 0u)
        return MakeUnexpected(__hidden_crash_win32::NativeFailure(PlatformOperation::PrepareAttributes, sizeError == ERROR_SUCCESS ? ERROR_INVALID_DATA : sizeError));
    Vector<MaxAlign, Alloc::ScratchArena> attributeStorage((static_cast<usize>(attributeListBytes) - 1u) / sizeof(MaxAlign) + 1u, scratchArena);
    auto* const attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
    if(!InitializeProcThreadAttributeList(attributeList, 1u, 0u, &attributeListBytes))
        return MakeUnexpected(__hidden_crash_win32::NativeFailure(PlatformOperation::PrepareAttributes, GetLastError()));
    ScopeExit releaseAttributes([&]()noexcept{ DeleteProcThreadAttributeList(attributeList); });

    HANDLE requestReadHandle = nullptr;
    HANDLE requestWriteHandle = nullptr;
    HANDLE ackReadHandle = nullptr;
    HANDLE ackWriteHandle = nullptr;
    HANDLE ackEvent = nullptr;
    PROCESS_INFORMATION processInfo = {};
    ScopeExit releaseHandles([&]()noexcept{
        __hidden_crash_win32::CleanupOwnedHandle(requestReadHandle, observations);
        __hidden_crash_win32::CleanupOwnedHandle(requestWriteHandle, observations);
        __hidden_crash_win32::CleanupOwnedHandle(ackReadHandle, observations);
        __hidden_crash_win32::CleanupOwnedHandle(ackWriteHandle, observations);
        __hidden_crash_win32::CleanupOwnedHandle(ackEvent, observations);
        __hidden_crash_win32::WaitForOwnedProcess(processInfo.hProcess, true, observations);
        __hidden_crash_win32::CleanupOwnedHandle(processInfo.hThread, observations);
        __hidden_crash_win32::CleanupOwnedHandle(processInfo.hProcess, observations);
    }
    );
    SECURITY_ATTRIBUTES securityAttributes = {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;
    // The request pipe admits a whole fixed CrashRequest without waiting for a child drain.
    if(!CreatePipe(&requestReadHandle, &requestWriteHandle, &securityAttributes, static_cast<DWORD>(sizeof(CrashRequest))))
        return MakeUnexpected(__hidden_crash_win32::NativeFailure(PlatformOperation::CreateRequestChannel, GetLastError()));
    if(!SetHandleInformation(requestWriteHandle, HANDLE_FLAG_INHERIT, 0u))
        return MakeUnexpected(__hidden_crash_win32::NativeFailure(PlatformOperation::SetInheritance, GetLastError()));
    if(!CreatePipe(&ackReadHandle, &ackWriteHandle, &securityAttributes, 0u))
        return MakeUnexpected(__hidden_crash_win32::NativeFailure(PlatformOperation::CreateAckChannel, GetLastError()));
    if(!SetHandleInformation(ackReadHandle, HANDLE_FLAG_INHERIT, 0u))
        return MakeUnexpected(__hidden_crash_win32::NativeFailure(PlatformOperation::SetInheritance, GetLastError()));
    ackEvent = CreateEventW(&securityAttributes, TRUE, FALSE, nullptr);
    if(!ackEvent)
        return MakeUnexpected(__hidden_crash_win32::NativeFailure(PlatformOperation::CreateAckEvent, GetLastError()));

    usize cursor = 0u;
    const auto append = [&](const TStringView text)noexcept{
        for(const tchar value : text)
            commandLine[cursor++] = value;
    };
    const auto appendHandle = [&](const HANDLE handle)noexcept{
        char text[s_HandlerArgumentTextCapacity] = {};
        AppendUnsignedToFixedBuffer(text, static_cast<u64>(reinterpret_cast<usize>(handle)));
        for(const char value : AStringView(text))
            commandLine[cursor++] = static_cast<tchar>(value);
    };
    append(NWB_TEXT("\""));
    append(handlerExecutablePath.native());
    append(NWB_TEXT("\" "));
    append(s_RequestHandleArgument);
    append(NWB_TEXT(" "));
    appendHandle(requestReadHandle);
    append(NWB_TEXT(" "));
    append(s_AckHandleArgument);
    append(NWB_TEXT(" "));
    appendHandle(ackWriteHandle);
    append(NWB_TEXT(" "));
    append(s_AckEventArgument);
    append(NWB_TEXT(" "));
    appendHandle(ackEvent);
    commandLine[cursor] = 0;

    HANDLE inheritedHandles[] = { requestReadHandle, ackWriteHandle, ackEvent };
    if(!UpdateProcThreadAttribute(attributeList, 0u, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritedHandles, sizeof(inheritedHandles), nullptr, nullptr))
        return MakeUnexpected(__hidden_crash_win32::NativeFailure(PlatformOperation::PrepareAttributes, GetLastError()));
    STARTUPINFOEX startupInfo = {};
    startupInfo.StartupInfo.cb = sizeof(startupInfo);
    startupInfo.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.StartupInfo.wShowWindow = SW_HIDE;
    startupInfo.lpAttributeList = attributeList;
    constexpr DWORD s_CreationFlags = CREATE_NO_WINDOW | DETACHED_PROCESS | EXTENDED_STARTUPINFO_PRESENT;
    if(!CreateProcess(handlerExecutablePath.c_str(), commandLine.data(), nullptr, nullptr, TRUE, s_CreationFlags, nullptr, nullptr, &startupInfo.StartupInfo, &processInfo))
        return MakeUnexpected(__hidden_crash_win32::NativeFailure(PlatformOperation::CreateProcess, GetLastError()));
    if(!SetHandleInformation(ackEvent, HANDLE_FLAG_INHERIT, 0u))
        return MakeUnexpected(__hidden_crash_win32::NativeFailure(PlatformOperation::SetInheritance, GetLastError()));
    const auto requestReleased = __hidden_crash_win32::ReleaseOwnedHandle(requestReadHandle);
    if(!requestReleased)
        return MakeUnexpected(requestReleased.error());
    const auto ackReleased = __hidden_crash_win32::ReleaseOwnedHandle(ackWriteHandle);
    if(!ackReleased)
        return MakeUnexpected(ackReleased.error());

    g_State.requestWriteHandle = requestWriteHandle;
    g_State.ackReadHandle = ackReadHandle;
    g_State.crashHandledEvent = ackEvent;
    g_State.handlerProcessInfo = processInfo;
    g_State.handlerStarted = true;
    releaseHandles.release();
    return {};
}

template Expected<void, PlatformFailure> StartDesktopHandler(const ::Path<Alloc::ScratchArena>& handlerExecutablePath, Alloc::ScratchArena& scratchArena, PlatformCleanupObservations& observations);

Expected<void, PlatformFailure> InstallPlatformHandlers()noexcept{
    g_State.previousExceptionFilter = SetUnhandledExceptionFilter(__hidden_crash_win32::UnhandledExceptionFilter);
    g_State.exceptionFilterInstalled = true;
    g_State.previousTerminateHandler = ExchangeTerminateHandler(__hidden_crash_win32::TerminateHandler);
    g_State.terminateHandlerInstalled = true;
    return {};
}

void UninstallPlatformResources(PlatformCleanupObservations& observations)noexcept{
    __hidden_crash_win32::CleanupOwnedHandle(g_State.requestWriteHandle, observations, INVALID_HANDLE_VALUE);
    __hidden_crash_win32::CleanupOwnedHandle(g_State.ackReadHandle, observations, INVALID_HANDLE_VALUE);
    __hidden_crash_win32::WaitForOwnedProcess(g_State.handlerProcessInfo.hProcess, false, observations);
    __hidden_crash_win32::CleanupOwnedHandle(g_State.crashHandledEvent, observations);
    __hidden_crash_win32::CleanupOwnedHandle(g_State.handlerProcessInfo.hThread, observations);
    __hidden_crash_win32::CleanupOwnedHandle(g_State.handlerProcessInfo.hProcess, observations);
    g_State.handlerStarted = false;
    if(g_State.exceptionFilterInstalled){
        const auto replaced = SetUnhandledExceptionFilter(g_State.previousExceptionFilter);
        if(replaced != __hidden_crash_win32::UnhandledExceptionFilter){
            const auto restored = SetUnhandledExceptionFilter(replaced);
            if(restored != g_State.previousExceptionFilter)
                observations.record({ PlatformOperation::RestoreExceptionFilter, PlatformErrorDomain::Contract, 1u });
        }
        g_State.exceptionFilterInstalled = false;
        g_State.previousExceptionFilter = nullptr;
    }
    if(g_State.terminateHandlerInstalled){
        if(QueryTerminateHandler() == __hidden_crash_win32::TerminateHandler)
            ExchangeTerminateHandler(g_State.previousTerminateHandler);
        g_State.terminateHandlerInstalled = false;
        g_State.previousTerminateHandler = nullptr;
    }
    g_State.platformRestorePending = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

