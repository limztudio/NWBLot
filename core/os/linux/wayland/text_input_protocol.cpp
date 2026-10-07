// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_OS_WITH_TEXT_INPUT_V3)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void WaylandTextInputService::commitState(const TextInputSessionToken token, const u64 revision){
    zwp_text_input_v3_commit(m_input);
    ++m_commitSerial;
    m_serials.record(m_commitSerial, token, revision);
}

void WaylandTextInputService::sendCaret(const TextInputRect caret){
    const TextInputRect logical = WaylandTextInputRectForPixels(caret, m_bufferScale);
    zwp_text_input_v3_set_cursor_rectangle(m_input, logical.x, logical.y, logical.width, logical.height);
}

void WaylandTextInputService::sendSurrounding(
    const AStringView text,
    const usize anchorByte,
    const usize caretByte,
    const u64 revision){
    const WaylandTextInputSurrounding slice = m_wireState.update(text, anchorByte, caretByte, revision);
    if(!slice.available)
        return;
    m_wireSurrounding.assign(slice.text.data(), slice.text.size());
    zwp_text_input_v3_set_surrounding_text(
        m_input, m_wireSurrounding.c_str(), static_cast<i32>(slice.caretByte), static_cast<i32>(slice.anchorByte)
    );
}

void WaylandTextInputService::receiveDone(const u32 serial){
    const TextInputSessionToken token = activeSession();
    const WaylandTextInputProvenance provenance = m_serials.resolve(serial, token);
    if(!m_enabled || !m_entered || !m_keyboardFocused || provenance.token != token || !token.valid()){
        clearPending();
        return;
    }
    // A delayed done from this session still delivers text; only its native-state feedback must wait.
    m_waitingForCurrentSerial = serial != m_commitSerial;
    if(m_pendingInvalid){
        clearPending();
        nativeFailure();
        return;
    }
    TextInputAdmission::Enum status = TextInputAdmission::Accepted;
    if(m_hasPreedit || m_hasCommit || m_hasDelete)
        status = emitPreedit(token, {}, 0u, 0u);
    if(status == TextInputAdmission::Accepted && m_hasDelete){
        if(CanApplyWaylandTextInputDeletion(
            provenance, surroundingRevision(), m_wireState, surroundingAnchorByte(), surroundingCaretByte(),
            m_pendingBefore, m_pendingAfter
        )){
            status = emitDeleteSurrounding(
                token, m_pendingBefore, m_pendingAfter, provenance.revision, TextInputDeletionBasis::Selection
            );
        }
        else{
            // Refuse stale or unsent surrounding ranges before applying later native batch edits.
            clearPending();
            if(!cancelSession(token, TextInputCancelReason::NativeCancelled))
                NWB_LOGGER_WARNING(NWB_TEXT("Wayland text input: stale surrounding batch could not cancel current session"));
            return;
        }
    }
    if(status == TextInputAdmission::Accepted && m_hasCommit)
        status = emitCommit(token, m_pendingCommit);
    if(status == TextInputAdmission::Accepted && m_hasPreedit){
        const bool visible = m_pendingBegin != -1 || m_pendingEnd != -1;
        const usize begin = visible ? static_cast<usize>(m_pendingBegin) : 0u;
        const usize end = visible ? static_cast<usize>(m_pendingEnd) : 0u;
        status = emitPreedit(token, m_pendingPreedit, begin, end, visible);
    }
    clearPending();
    if(status != TextInputAdmission::Accepted && status != TextInputAdmission::InvalidSession){
        nativeFailure();
        return;
    }
    if(!m_waitingForCurrentSerial && m_deferredState.pending() && token == activeSession()){
        const TextInputChangeCause::Enum cause = m_deferredState.cause();
        m_deferredState.clear();
        sendCaret(caretRect());
        sendSurrounding(surroundingText(), surroundingAnchorByte(), surroundingCaretByte(), surroundingRevision());
        const u32 nativeCause = cause == TextInputChangeCause::InputMethod
            ? ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD : ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER;
        zwp_text_input_v3_set_text_change_cause(m_input, nativeCause);
        commitState(token, m_wireState.revision());
        if(!flush())
            nativeFailure();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void WaylandTextInputService::OnRegistryGlobal(
    void* const data,
    wl_registry* const registry,
    const u32 name,
    const char* const interfaceName,
    const u32 version){
    auto& service = *static_cast<WaylandTextInputService*>(data);
    if(AStringView(interfaceName) != zwp_text_input_manager_v3_interface.name || service.m_manager || version == 0u)
        return;
    // Protocol v1 commits cursor-rectangle updates through its explicit commit request.
    service.m_manager = static_cast<zwp_text_input_manager_v3*>(wl_registry_bind(
        registry, name, &zwp_text_input_manager_v3_interface, 1u
    ));
    service.m_managerName = name;
    if(service.m_seat){
        wl_seat* const seat = service.m_seat;
        const u32 seatName = service.m_seatName;
        service.m_seat = nullptr;
        service.attachSeat(seat, seatName);
    }
}

void WaylandTextInputService::OnRegistryRemove(void* const data, wl_registry*, const u32 name){
    auto& service = *static_cast<WaylandTextInputService*>(data);
    if(name == service.m_seatName){
        service.attachSeat(nullptr, 0u);
        return;
    }
    if(name != service.m_managerName)
        return;
    const TextInputSessionToken token = service.activeSession();
    if(token.valid() && !service.cancelSession(token, TextInputCancelReason::NativeCancelled))
        NWB_LOGGER_WARNING(NWB_TEXT("Wayland text input: manager removal could not cancel current session"));
    service.releaseDevice();
    zwp_text_input_manager_v3_destroy(service.m_manager);
    service.m_manager = nullptr;
    service.m_managerName = 0u;
}

void WaylandTextInputService::OnEnter(void* const data, zwp_text_input_v3* const input, wl_surface* const surface){
    auto& service = *static_cast<WaylandTextInputService*>(data);
    if(input != service.m_input)
        return;
    service.m_entered = surface == &service.m_surface;
    service.clearPending();
}

void WaylandTextInputService::OnLeave(void* const data, zwp_text_input_v3* const input, wl_surface* const surface){
    auto& service = *static_cast<WaylandTextInputService*>(data);
    if(input != service.m_input || surface != &service.m_surface)
        return;
    service.m_entered = false;
    service.m_enabled = false;
    service.clearPending();
    const TextInputSessionToken token = service.activeSession();
    if(token.valid() && !service.cancelSession(token, TextInputCancelReason::FocusLost))
        NWB_LOGGER_WARNING(NWB_TEXT("Wayland text input: protocol focus loss could not cancel current session"));
}

void WaylandTextInputService::OnPreedit(
    void* const data,
    zwp_text_input_v3* const input,
    const char* const text,
    const i32 begin,
    const i32 end){
    auto& service = *static_cast<WaylandTextInputService*>(data);
    if(input != service.m_input || !service.m_enabled)
        return;
    const AStringView value = text ? AStringView(text) : AStringView{};
    const bool hidden = begin == -1 && end == -1;
    const bool valid = ValidateTextInputUtf8(value, s_TextInputMaxEventTextBytes) == TextInputAdmission::Accepted;
    const bool range = hidden || (begin >= 0 && end >= 0
        && IsTextInputUtf8Boundary(value, static_cast<usize>(begin))
        && IsTextInputUtf8Boundary(value, static_cast<usize>(end)));
    service.m_pendingInvalid |= !valid || !range;
    if(valid && range)
        service.m_pendingPreedit.assign(value.data(), value.size());
    service.m_pendingBegin = begin;
    service.m_pendingEnd = end;
    service.m_hasPreedit = true;
}

void WaylandTextInputService::OnCommit(void* const data, zwp_text_input_v3* const input, const char* const text){
    auto& service = *static_cast<WaylandTextInputService*>(data);
    if(input != service.m_input || !service.m_enabled)
        return;
    const AStringView value = text ? AStringView(text) : AStringView{};
    const bool valid = ValidateTextInputUtf8(value, s_TextInputMaxEventTextBytes) == TextInputAdmission::Accepted;
    service.m_pendingInvalid |= !valid;
    if(valid)
        service.m_pendingCommit.assign(value.data(), value.size());
    service.m_hasCommit = true;
}

void WaylandTextInputService::OnDelete(
    void* const data,
    zwp_text_input_v3* const input,
    const u32 before,
    const u32 after){
    auto& service = *static_cast<WaylandTextInputService*>(data);
    if(input != service.m_input || !service.m_enabled)
        return;
    service.m_pendingBefore = before;
    service.m_pendingAfter = after;
    service.m_hasDelete = true;
}

void WaylandTextInputService::OnDone(void* const data, zwp_text_input_v3* const input, const u32 serial){
    auto& service = *static_cast<WaylandTextInputService*>(data);
    if(input == service.m_input)
        service.receiveDone(serial);
}

#if defined(ZWP_TEXT_INPUT_V3_ACTION_SINCE_VERSION)
void WaylandTextInputService::OnAction(void*, zwp_text_input_v3*, u32, u32){}

void WaylandTextInputService::OnLanguage(void*, zwp_text_input_v3*, const char*){}

void WaylandTextInputService::OnPreeditHint(void*, zwp_text_input_v3*, u32, u32, u32){}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

