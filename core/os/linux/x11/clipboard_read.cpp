// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_service.h"

#include <X11/Xatom.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void X11ClipboardService::requestConversion(){
    bool succeeded = false;
    {
        X11CheckedOperation checked(m_display);
        XDeleteProperty(&m_display, m_operationWindow, m_propertyAtom);
        XConvertSelection(&m_display, m_operationSelection, m_operationTarget, m_propertyAtom, m_operationWindow, m_operationTimestamp);
        succeeded = checked.succeeded();
    }
    if(!succeeded){
        finishOperation(ClipboardStatus::NativeFailure);
        return;
    }
    m_phase = X11ClipboardPhase::Notify;
    XFlush(&m_display);
}

void X11ClipboardService::receiveSelection(const XSelectionEvent& event){
    if(
        m_phase != X11ClipboardPhase::Notify
        || event.selection != m_operationSelection
        || event.target != m_operationTarget
        || event.time != m_operationTimestamp
    )
        return;
    if(!event.property){
        if(m_operationTarget == m_utf8Atom){
            m_operationTarget = XA_STRING;
            requestConversion();
        }
        else
            finishOperation(ClipboardStatus::Unavailable);
        return;
    }
    if(event.property != m_propertyAtom){
        finishOperation(ClipboardStatus::NativeFailure);
        return;
    }
    X11Property property;
    if(!ReadX11Property(m_display, m_operationWindow, m_propertyAtom, false, s_ClipboardMaxTextBytes, property)){
        finishOperation(ClipboardStatus::NativeFailure);
        return;
    }
    if(property.type == m_incrementalAtom){
        if(property.format != 32 || property.count != 1u || property.remaining || !property.bytes){
            finishOperation(ClipboardStatus::InvalidText);
            return;
        }
        const unsigned long lowerBound = *reinterpret_cast<const unsigned long*>(property.bytes);
        if(lowerBound > s_ClipboardMaxTextBytes){
            finishOperation(ClipboardStatus::TooLarge);
            return;
        }
        m_phase = X11ClipboardPhase::Incremental;
        XDeleteProperty(&m_display, m_operationWindow, m_propertyAtom);
        XFlush(&m_display);
        return;
    }
    if(property.type != m_operationTarget || property.format != 8){
        finishOperation(ClipboardStatus::InvalidText);
        return;
    }
    if(property.remaining){
        finishOperation(ClipboardStatus::TooLarge);
        return;
    }
    const AStringView bytes = property.count ? AStringView(reinterpret_cast<const char*>(property.bytes), property.count) : AStringView{};
    const ClipboardStatus::Enum status = m_received.appendBytes(bytes);
    if(status != ClipboardStatus::Success)
        finishOperation(status);
    else
        finishRead();
}

void X11ClipboardService::receiveChunk(){
    X11Property property;
    if(!ReadX11Property(m_display, m_operationWindow, m_propertyAtom, true, s_ClipboardMaxTextBytes, property)){
        finishOperation(ClipboardStatus::NativeFailure);
        return;
    }
    if(property.type != m_operationTarget || property.format != 8){
        finishOperation(ClipboardStatus::InvalidText);
        return;
    }
    if(property.remaining){
        finishOperation(ClipboardStatus::TooLarge);
        return;
    }
    if(property.count == 0u){
        finishRead();
        return;
    }
    const ClipboardStatus::Enum status = m_received.appendBytes(AStringView(reinterpret_cast<const char*>(property.bytes), property.count));
    if(status != ClipboardStatus::Success)
        finishOperation(status);
    else
        XFlush(&m_display);
}

void X11ClipboardService::finishRead(){
    if(m_operationTarget == XA_STRING){
        const ClipboardStatus::Enum status = DecodeClipboardLatin1(m_received.text(), m_decoded);
        finishOperation(status, m_decoded);
    }
    else
        finishOperation(ClipboardStatus::Success, m_received.text());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

