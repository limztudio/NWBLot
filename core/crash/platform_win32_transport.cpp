// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "platform_win32_transport.h"

#include <global/blocking_io.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void WindowsCrashTransport::DrainPendingAcks(const HANDLE ackReadHandle)noexcept{
    if(ackReadHandle == INVALID_HANDLE_VALUE)
        return;

    for(;;){
        DWORD availableBytes = 0u;
        if(!PeekNamedPipe(ackReadHandle, nullptr, 0u, nullptr, &availableBytes, nullptr))
            return;
        if(availableBytes < sizeof(CrashDetail::CrashAck))
            return;

        CrashDetail::CrashAck ignoredAck;
        if(!ReadAllWin32Handle(ackReadHandle, &ignoredAck, sizeof(ignoredAck)))
            return;
    }
}


CrashDumpTransportStatus::Enum WindowsCrashTransport::Request(CrashState& state, const CrashRequest& request, const u32 waitMilliseconds)noexcept{
    if(state.requestWriteHandle == INVALID_HANDLE_VALUE)
        return CrashDumpTransportStatus::Failed;

    // Re-entrancy guard: a fault inside the transport (e.g. the SEH filter faulting) must not re-enter or self-deadlock.
    static thread_local b8 s_InTransport = false;
    if(s_InTransport)
        return CrashDumpTransportStatus::Failed;
    s_InTransport = true;

    // One writer owns the request pipe and ack event; other requests are not sent.
    // One async-safe CAS, never blocks.
    u32 expected = 0u;
    if(!state.transportInFlight.compare_exchange_strong(expected, 1u, MemoryOrder::acq_rel, MemoryOrder::acquire)){
        s_InTransport = false;
        return CrashDumpTransportStatus::Failed;
    }

    CrashDumpTransportStatus::Enum status = CrashDumpTransportStatus::Failed;
    do{
        DrainPendingAcks(state.ackReadHandle);

        if(state.crashHandledEvent)
            ResetEvent(state.crashHandledEvent);

        if(!WriteAllWin32Handle(state.requestWriteHandle, &request, sizeof(request))){
            status = CrashDumpTransportStatus::Failed;
            break;
        }

        if(!state.crashHandledEvent || state.ackReadHandle == INVALID_HANDLE_VALUE || waitMilliseconds == 0u){
            status = CrashDumpTransportStatus::Sent;
            break;
        }

        // Wake on the ack event OR the handler process dying, so a dead/wedged handler never costs the full timeout.
        // Falls back to a single-object wait if the handler process handle is unavailable.
        Array<HANDLE, s_CrashWaitHandleCount> waitHandles = { state.crashHandledEvent, state.handlerProcessInfo.hProcess };
        const DWORD waitCount = state.handlerProcessInfo.hProcess ? s_CrashWaitHandleCount : 1u;
        const DWORD waitResult = WaitForMultipleObjects(waitCount, waitHandles.data(), FALSE, waitMilliseconds);
        if(waitResult == WAIT_OBJECT_0){
            CrashAck ack;
            if(!ReadAllWin32Handle(state.ackReadHandle, &ack, sizeof(ack))){
                status = CrashDumpTransportStatus::Failed;
                break;
            }
            if(!CrashProtocol::CrashAckMatchesRequest(ack, request)){
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

    state.transportInFlight.store(0u, MemoryOrder::release);
    s_InTransport = false;
    return status;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

