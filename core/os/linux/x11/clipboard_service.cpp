// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_service.h"

#include <core/common/log.h>

#include <X11/Xatom.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


X11ClipboardService::X11ClipboardService(Alloc::GlobalArena& arena, Display& display)
    : QueuedClipboardService(arena)
    , m_arena(arena)
    , m_display(display)
    , m_owned{ OwnedSelection(arena), OwnedSelection(arena) }
    , m_outgoing(arena)
    , m_received(arena)
    , m_pendingWrite(arena)
    , m_decoded(arena)
{
    m_outgoing.reserve(s_MaxTransfers);
}

X11ClipboardService::~X11ClipboardService(){
    releaseOperation();
    while(!m_outgoing.empty())
        releaseTransfer(m_outgoing.size() - 1u, true);
    if(m_ownerWindow){
        X11CheckedOperation operation(m_display);
        XDestroyWindow(&m_display, m_ownerWindow);
        if(!operation.succeeded())
            NWB_LOGGER_WARNING(NWB_TEXT("X11 clipboard: owner window destruction failed"));
    }
}

bool X11ClipboardService::initialize(){
    X11CheckedOperation operation(m_display);
    m_ownerWindow = XCreateSimpleWindow(&m_display, DefaultRootWindow(&m_display), 0, 0, 1, 1, 0, 0, 0);
    if(m_ownerWindow)
        XSelectInput(&m_display, m_ownerWindow, PropertyChangeMask | StructureNotifyMask);
    m_clipboardAtom = XInternAtom(&m_display, "CLIPBOARD", False);
    m_utf8Atom = XInternAtom(&m_display, "UTF8_STRING", False);
    m_targetsAtom = XInternAtom(&m_display, "TARGETS", False);
    m_timestampAtom = XInternAtom(&m_display, "TIMESTAMP", False);
    m_multipleAtom = XInternAtom(&m_display, "MULTIPLE", False);
    m_atomPairAtom = XInternAtom(&m_display, "ATOM_PAIR", False);
    m_textAtom = XInternAtom(&m_display, "TEXT", False);
    m_utf8MimeAtom = XInternAtom(&m_display, "text/plain;charset=utf-8", False);
    m_incrementalAtom = XInternAtom(&m_display, "INCR", False);
    m_propertyAtom = XInternAtom(&m_display, "NWB_CLIPBOARD_READ", False);
    m_timeProbeAtom = XInternAtom(&m_display, "NWB_CLIPBOARD_TIME", False);
    const long requestWords = XMaxRequestSize(&m_display);
    m_chunkBytes = requestWords > 64 ? Min<usize>(65536u, static_cast<usize>(requestWords - 64) * 4u) : 1024u;
    return
        operation.succeeded() && m_ownerWindow && m_clipboardAtom && m_utf8Atom && m_targetsAtom
        && m_timestampAtom && m_multipleAtom && m_atomPairAtom && m_textAtom && m_utf8MimeAtom
        && m_incrementalAtom && m_propertyAtom && m_timeProbeAtom
    ;
}

bool X11ClipboardService::handleEvent(const XEvent& event){
    NWB_ASSERT(isOwnerThread());
    if(event.type == SelectionRequest && event.xselectionrequest.owner == m_ownerWindow){
        answerSelection(event.xselectionrequest);
        return true;
    }
    if(event.type == SelectionClear && event.xselectionclear.window == m_ownerWindow){
        if(OwnedSelection* const selection = ownedSelection(event.xselectionclear.selection)){
            if(
                XGetSelectionOwner(&m_display, event.xselectionclear.selection) == m_ownerWindow
                || static_cast<i32>(static_cast<u32>(event.xselectionclear.time) - static_cast<u32>(selection->timestamp)) < 0
            )
                return true;
            selection->owned = false;
            selection->utf8.clear();
            selection->latin1.clear();
        }
        return true;
    }
    if(event.type == SelectionNotify && event.xselection.requestor == m_operationWindow){
        receiveSelection(event.xselection);
        return true;
    }
    if(event.type == PropertyNotify){
        if(event.xproperty.window == m_operationWindow){
            if(event.xproperty.state == PropertyNewValue && event.xproperty.atom == m_timeProbeAtom && m_phase == X11ClipboardPhase::Timestamp)
                receiveTimestamp(event.xproperty.time);
            else if(event.xproperty.state == PropertyNewValue && event.xproperty.atom == m_propertyAtom && m_phase == X11ClipboardPhase::Incremental)
                receiveChunk();
            return true;
        }
        if(event.xproperty.window == m_ownerWindow)
            return true;
        for(usize index = 0u; index < m_outgoing.size(); ++index){
            if(event.xproperty.window == m_outgoing[index].requestor && event.xproperty.atom == m_outgoing[index].property){
                if(event.xproperty.state == PropertyDelete)
                    sendChunk(index);
                return true;
            }
        }
    }
    if(event.type == DestroyNotify){
        if(event.xdestroywindow.window == m_operationWindow){
            m_operationWindow = 0u;
            finishOperation(ClipboardStatus::NativeFailure);
            return true;
        }
        bool consumed = false;
        for(usize index = m_outgoing.size(); index > 0u; --index){
            if(m_outgoing[index - 1u].requestor == event.xdestroywindow.window){
                releaseTransfer(index - 1u, false);
                consumed = true;
            }
        }
        return consumed || event.xdestroywindow.window == m_ownerWindow;
    }
    return false;
}

ClipboardCapabilities X11ClipboardService::capabilities(const ClipboardChannel::Enum channel)const noexcept{
    const bool available = m_ownerWindow && (channel == ClipboardChannel::Clipboard || channel == ClipboardChannel::PrimarySelection);
    return { available, available };
}

void X11ClipboardService::startNativeRequest(
    const ClipboardRequestToken token,
    const ClipboardOperation::Enum operation,
    const ClipboardChannel::Enum channel,
    const AStringView text){
    m_operationToken = token;
    m_operation = operation;
    m_operationSelection = selectionAtom(channel);
    m_operationTarget = m_utf8Atom;
    m_deadline = TimerAddMS(TimerNow(), s_TransferTimeoutMs);
    m_received.clear();
    m_pendingWrite.clear();
    if(!text.empty())
        m_pendingWrite.assign(text.data(), text.size());
    if(operation == ClipboardOperation::ReadText){
        const Window owner = XGetSelectionOwner(&m_display, m_operationSelection);
        if(!owner){
            finishOperation(ClipboardStatus::Unavailable);
            return;
        }
        if(owner == m_ownerWindow){
            const OwnedSelection* const selection = ownedSelection(m_operationSelection);
            NWB_FATAL_ASSERT(selection);
            finishOperation(ClipboardStatus::Success, selection->utf8);
            return;
        }
    }
    bool succeeded = false;
    {
        X11CheckedOperation checked(m_display);
        m_operationWindow = XCreateSimpleWindow(&m_display, DefaultRootWindow(&m_display), 0, 0, 1, 1, 0, 0, 0);
        if(m_operationWindow){
            XSelectInput(&m_display, m_operationWindow, PropertyChangeMask | StructureNotifyMask);
            const u8 byte = 1u;
            XChangeProperty(&m_display, m_operationWindow, m_timeProbeAtom, XA_INTEGER, 8, PropModeReplace, &byte, 1);
        }
        succeeded = checked.succeeded();
    }
    if(!succeeded || !m_operationWindow){
        finishOperation(ClipboardStatus::NativeFailure);
        return;
    }
    m_phase = X11ClipboardPhase::Timestamp;
    XFlush(&m_display);
}

void X11ClipboardService::cancelNativeRequest(const ClipboardRequestToken token){
    if(m_operationToken == token)
        releaseOperation();
}

void X11ClipboardService::pumpNativeRequests(){
    const Timer now = TimerNow();
    if(m_operationToken.valid() && now >= m_deadline)
        finishOperation(ClipboardStatus::Unavailable);
    for(usize index = m_outgoing.size(); index > 0u; --index){
        if(now >= m_outgoing[index - 1u].deadline)
            releaseTransfer(index - 1u, true);
    }
    XFlush(&m_display);
}

Atom X11ClipboardService::selectionAtom(const ClipboardChannel::Enum channel)const{
    return channel == ClipboardChannel::PrimarySelection ? XA_PRIMARY : m_clipboardAtom;
}

X11ClipboardService::OwnedSelection* X11ClipboardService::ownedSelection(const Atom selection){
    if(selection == m_clipboardAtom)
        return &m_owned[0];
    if(selection == XA_PRIMARY)
        return &m_owned[1];
    return nullptr;
}

void X11ClipboardService::releaseOperation(){
    if(m_operationWindow){
        X11CheckedOperation checked(m_display);
        XDestroyWindow(&m_display, m_operationWindow);
        if(!checked.succeeded())
            NWB_LOGGER_WARNING(NWB_TEXT("X11 clipboard: request window destruction failed"));
    }
    m_operationWindow = 0u;
    m_operationToken = {};
    m_phase = X11ClipboardPhase::Idle;
    m_pendingWrite.clear();
    m_received.clear();
    m_decoded.clear();
}

void X11ClipboardService::finishOperation(const ClipboardStatus::Enum status, const AStringView text){
    if(m_operationToken.valid() && !completeNativeRequest(m_operationToken, status, text))
        NWB_FATAL_ASSERT(false);
    releaseOperation();
}

void X11ClipboardService::receiveTimestamp(const Time timestamp){
    m_operationTimestamp = timestamp;
    if(m_operation == ClipboardOperation::ReadText){
        requestConversion();
        return;
    }
    OwnedSelection* const selection = ownedSelection(m_operationSelection);
    NWB_FATAL_ASSERT(selection);
    bool acquired = false;
    {
        X11CheckedOperation checked(m_display);
        XSetSelectionOwner(&m_display, m_operationSelection, m_ownerWindow, timestamp);
        acquired = XGetSelectionOwner(&m_display, m_operationSelection) == m_ownerWindow && checked.succeeded();
    }
    if(!acquired){
        finishOperation(ClipboardStatus::Unavailable);
        return;
    }
    selection->utf8 = m_pendingWrite;
    selection->latin1Available = EncodeClipboardLatin1(selection->utf8, selection->latin1) == ClipboardStatus::Success;
    selection->timestamp = timestamp;
    selection->owned = true;
    finishOperation(ClipboardStatus::Success);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GlobalUniquePtr<IClipboardService> CreateX11ClipboardService(Alloc::GlobalArena& arena, _XDisplay& display){
    auto service = MakeGlobalUnique<X11ClipboardService>(arena, arena, display);
    if(!service->initialize()){
        NWB_LOGGER_ERROR(NWB_TEXT("X11 clipboard initialization failed"));
        return {};
    }
    return service;
}

bool DispatchX11ClipboardEvent(IClipboardService& service, const _XEvent& event){
    return checked_cast<X11ClipboardService*>(&service)->handleEvent(event);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

