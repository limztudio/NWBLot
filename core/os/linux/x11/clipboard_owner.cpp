// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_service.h"

#include <X11/Xatom.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void X11ClipboardService::answerSelection(const XSelectionRequestEvent& event){
    OwnedSelection* const selection = ownedSelection(event.selection);
    const Atom destination = event.property ? event.property : event.target;
    bool converted = false;
    const bool currentOwner = selection && selection->owned && XGetSelectionOwner(&m_display, event.selection) == m_ownerWindow;
    const bool validTime = selection && (event.time == CurrentTime || static_cast<i32>(static_cast<u32>(event.time) - static_cast<u32>(selection->timestamp)) >= 0);
    if(currentOwner && validTime && destination){
        if(event.target == m_multipleAtom && event.property){
            const auto property = ReadX11Property(m_display, event.requestor, event.property, false, 256u * 4u);
            if(
                property.has_value()
                && (property->type == m_atomPairAtom || property->type == XA_ATOM)
                && property->format == 32
                && property->count <= 256u
                && (property->count % 2u) == 0u
                && property->remaining == 0u
            ){
                Array<Atom, 256u> pairs{};
                if(property->count)
                    NWB_MEMCPY(pairs.data(), pairs.size() * sizeof(Atom), property->bytes, property->count * sizeof(Atom));
                for(usize index = 0u; index < property->count; index += 2u){
                    bool usable = pairs[index] != m_multipleAtom && pairs[index + 1u] && pairs[index + 1u] != event.property;
                    for(usize previous = 0u; previous < index; previous += 2u){
                        if(pairs[previous + 1u] == pairs[index + 1u])
                            usable = false;
                    }
                    if(!usable || !convertTarget(event, pairs[index], pairs[index + 1u], *selection))
                        pairs[index + 1u] = None;
                }
                X11CheckedOperation checked(m_display);
                XChangeProperty(
                    &m_display, event.requestor, event.property, m_atomPairAtom, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(pairs.data()), static_cast<int>(property->count)
                );
                converted = checked.succeeded();
            }
        }
        else if(event.target != m_multipleAtom)
            converted = convertTarget(event, event.target, destination, *selection);
    }
    XEvent response{};
    response.xselection.type = SelectionNotify;
    response.xselection.display = &m_display;
    response.xselection.requestor = event.requestor;
    response.xselection.selection = event.selection;
    response.xselection.target = event.target;
    response.xselection.property = converted ? destination : None;
    response.xselection.time = event.time;
    X11CheckedOperation checked(m_display);
    const int sent = XSendEvent(&m_display, event.requestor, False, 0, &response);
    const bool delivered = checked.succeeded() && sent != 0;
    if(!delivered){
        for(usize index = m_outgoing.size(); index > 0u; --index){
            if(m_outgoing[index - 1u].requestor == event.requestor)
                m_outgoing[index - 1u].deadline = TimerNow();
        }
    }
    XFlush(&m_display);
}

bool X11ClipboardService::convertTarget(
    const XSelectionRequestEvent& event,
    const Atom target,
    const Atom property,
    OwnedSelection& selection
){
    if(target == m_utf8Atom || target == m_utf8MimeAtom || target == m_textAtom)
        return sendText(event.requestor, property, target == m_textAtom ? m_utf8Atom : target, selection.utf8);
    if(target == XA_STRING)
        return selection.latin1Available && sendText(event.requestor, property, XA_STRING, selection.latin1);
    X11CheckedOperation checked(m_display);
    if(target == m_targetsAtom){
        Array<Atom, 7u> targets{ m_targetsAtom, m_timestampAtom, m_multipleAtom, m_utf8Atom, m_utf8MimeAtom, m_textAtom, XA_STRING };
        const int count = selection.latin1Available ? 7 : 6;
        XChangeProperty(
            &m_display, event.requestor, property, XA_ATOM, 32, PropModeReplace,
            reinterpret_cast<const unsigned char*>(targets.data()), count
        );
    }
    else if(target == m_timestampAtom){
        const unsigned long timestamp = selection.timestamp;
        XChangeProperty(
            &m_display, event.requestor, property, XA_INTEGER, 32, PropModeReplace,
            reinterpret_cast<const unsigned char*>(&timestamp), 1
        );
    }
    else
        return false;
    return checked.succeeded();
}

bool X11ClipboardService::sendText(const Window requestor, const Atom property, const Atom type, const AStringView text){
    for(const OutgoingTransfer& active : m_outgoing){
        if(active.requestor == requestor && active.property == property)
            return false;
    }
    if(text.size() <= m_chunkBytes){
        X11CheckedOperation checked(m_display);
        XChangeProperty(
            &m_display, requestor, property, type, 8, PropModeReplace,
            reinterpret_cast<const unsigned char*>(text.data()), static_cast<int>(text.size())
        );
        return checked.succeeded();
    }
    if(m_outgoing.size() == s_MaxTransfers)
        return false;
    OutgoingTransfer transfer(m_arena);
    transfer.text.assign(text.data(), text.size());
    transfer.requestor = requestor;
    transfer.property = property;
    transfer.type = type;
    transfer.deadline = TimerAddMS(TimerNow(), s_TransferTimeoutMs);
    bool succeeded = false;
    bool changedMask = false;
    long restoreMask = 0;
    {
        X11CheckedOperation checked(m_display);
        XWindowAttributes attributes{};
        const bool exists = XGetWindowAttributes(&m_display, requestor, &attributes) != 0;
        if(exists){
            restoreMask = attributes.your_event_mask;
            transfer.previousEventMask = attributes.your_event_mask;
            for(const OutgoingTransfer& active : m_outgoing){
                if(active.requestor == requestor)
                    transfer.previousEventMask = active.previousEventMask;
            }
            XSelectInput(&m_display, requestor, attributes.your_event_mask | PropertyChangeMask | StructureNotifyMask);
            changedMask = true;
            const unsigned long lowerBound = text.size();
            XChangeProperty(
                &m_display, requestor, property, m_incrementalAtom, 32, PropModeReplace,
                reinterpret_cast<const unsigned char*>(&lowerBound), 1
            );
        }
        succeeded = checked.succeeded() && exists;
    }
    if(!succeeded){
        if(changedMask){
            X11CheckedOperation rollback(m_display);
            XSelectInput(&m_display, requestor, restoreMask);
            if(!rollback.succeeded())
                return false;
        }
        return false;
    }
    m_outgoing.push_back(Move(transfer));
    return true;
}

void X11ClipboardService::sendChunk(const usize index){
    OutgoingTransfer& transfer = m_outgoing[index];
    const usize count = Min(m_chunkBytes, transfer.text.size() - transfer.offset);
    bool succeeded = false;
    {
        X11CheckedOperation checked(m_display);
        XChangeProperty(
            &m_display, transfer.requestor, transfer.property, transfer.type, 8, PropModeReplace,
            reinterpret_cast<const unsigned char*>(transfer.text.data() + transfer.offset), static_cast<int>(count)
        );
        succeeded = checked.succeeded();
    }
    if(!succeeded || count == 0u)
        releaseTransfer(index, succeeded);
    else
        transfer.offset += count;
    XFlush(&m_display);
}

void X11ClipboardService::releaseTransfer(const usize index, const bool requestorAlive){
    const Window requestor = m_outgoing[index].requestor;
    const long previousMask = m_outgoing[index].previousEventMask;
    m_outgoing.erase(m_outgoing.begin() + static_cast<isize>(index));
    if(!requestorAlive)
        return;
    for(const OutgoingTransfer& active : m_outgoing){
        if(active.requestor == requestor)
            return;
    }
    X11CheckedOperation checked(m_display);
    XSelectInput(&m_display, requestor, previousMask);
    const bool restored = checked.succeeded();
    if(!restored)
        return;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

