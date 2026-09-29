// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"
#include "text_input_text.h"

#include <global/scope_exit.h>
#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextInputAdmission::Enum QueuedTextInputService::emitCommit(const TextInputSessionToken token, const AStringView text){
    return admitEvent(token, TextInputEventKind::Commit, text, 0u, 0u, 0u, 0u, true);
}

TextInputAdmission::Enum QueuedTextInputService::emitPreedit(
    const TextInputSessionToken token,
    const AStringView text,
    const usize anchorByte,
    const usize caretByte,
    const bool caretVisible){
    return admitEvent(token, TextInputEventKind::Preedit, text, anchorByte, caretByte, 0u, 0u, caretVisible);
}

TextInputAdmission::Enum QueuedTextInputService::emitDeleteSurrounding(
    const TextInputSessionToken token,
    const usize beforeBytes,
    const usize afterBytes,
    const u64 revision,
    const TextInputDeletionBasis::Enum basis){
    if(!isOwnerThread())
        return TextInputAdmission::WrongThread;
    if(!token.valid() || token != m_activeToken)
        return TextInputAdmission::InvalidSession;
    if(revision != 0u && revision != m_surroundingRevision)
        return TextInputAdmission::InvalidRange;
    if(basis >= TextInputDeletionBasis::kCount)
        return TextInputAdmission::InvalidRange;
    const usize start = basis == TextInputDeletionBasis::Selection ? Min(m_anchorByte, m_caretByte) : m_caretByte;
    const usize end = basis == TextInputDeletionBasis::Selection ? Max(m_anchorByte, m_caretByte) : m_caretByte;
    if(beforeBytes > start || afterBytes > m_surrounding.size() - end)
        return TextInputAdmission::InvalidRange;
    if(
        !IsTextInputUtf8Boundary(m_surrounding, start - beforeBytes)
        || !IsTextInputUtf8Boundary(m_surrounding, end + afterBytes)
    )
        return TextInputAdmission::InvalidRange;
    return admitEvent(token, TextInputEventKind::DeleteSurrounding, {}, 0u, 0u, beforeBytes, afterBytes, true, basis);
}

bool QueuedTextInputService::cancelSession(const TextInputSessionToken token, const TextInputCancelReason::Enum reason){
    if(!isOwnerThread() || !token.valid() || token != m_activeToken)
        return false;
    const u64 revision = m_surroundingRevision;
    invalidateSession();
    clearEvents();
    m_cancelledToken = token;
    TextInputEvent& event = m_events.emplace_back(m_arena);
    event.token = token;
    if(m_nextSequence == Limit<u64>::s_Max)
        TerminateInvariant();
    event.sequence = m_nextSequence;
    ++m_nextSequence;
    event.surroundingRevision = revision;
    event.kind = TextInputEventKind::Cancelled;
    event.cancelReason = reason;
    const bool wasTransitioning = m_transitioning;
    m_transitioning = true;
    ScopeExit finishTransition([this, wasTransitioning]()noexcept{ m_transitioning = wasTransitioning; });
    endNativeSession(token);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextInputAdmission::Enum QueuedTextInputService::admitEvent(
    const TextInputSessionToken token,
    const TextInputEventKind::Enum kind,
    const AStringView text,
    const usize anchorByte,
    const usize caretByte,
    const usize beforeBytes,
    const usize afterBytes,
    const bool caretVisible,
    const TextInputDeletionBasis::Enum basis){
    if(!isOwnerThread())
        return TextInputAdmission::WrongThread;
    if(!token.valid() || token != m_activeToken)
        return TextInputAdmission::InvalidSession;
    const TextInputAdmission::Enum validation = ValidateTextInputUtf8(text, s_TextInputMaxEventTextBytes);
    if(validation != TextInputAdmission::Accepted)
        return validation;
    if(kind == TextInputEventKind::Preedit){
        if(!IsTextInputUtf8Boundary(text, anchorByte) || !IsTextInputUtf8Boundary(text, caretByte))
            return TextInputAdmission::InvalidRange;
    }
    else if(kind == TextInputEventKind::Commit && text.empty())
        return TextInputAdmission::Accepted;

    // Only consecutive preedit updates may coalesce; a commit or deletion is an ordering barrier.
    const bool replace = kind == TextInputEventKind::Preedit && !m_events.empty()
        && m_events.back().kind == TextInputEventKind::Preedit
    ;
    const usize oldBytes = replace ? m_events.back().text.size() : 0u;
    if(
        (!replace && m_events.size() >= s_TextInputMaxEvents)
        || text.size() > s_TextInputMaxQueuedTextBytes - (m_queuedTextBytes - oldBytes)
    ){
        if(!cancelSession(token, TextInputCancelReason::Overflow))
            TerminateInvariant();
        return TextInputAdmission::QueueFull;
    }
    TextInputEvent& event = replace ? m_events.back() : m_events.emplace_back(m_arena);
    event.text.assign(text.data(), text.size());
    event.token = token;
    if(m_nextSequence == Limit<u64>::s_Max)
        TerminateInvariant();
    event.sequence = m_nextSequence;
    ++m_nextSequence;
    event.surroundingRevision = m_surroundingRevision;
    event.kind = kind;
    event.anchorByte = anchorByte;
    event.caretByte = caretByte;
    event.deleteBeforeBytes = beforeBytes;
    event.deleteAfterBytes = afterBytes;
    event.caretVisible = caretVisible;
    event.cancelReason = TextInputCancelReason::NativeCancelled;
    event.deletionBasis = basis;
    m_queuedTextBytes = m_queuedTextBytes - oldBytes + text.size();
    return TextInputAdmission::Accepted;
}

void QueuedTextInputService::clearEvents(){
    m_events.clear();
    m_queuedTextBytes = 0u;
}

void QueuedTextInputService::invalidateSession(){
    m_activeToken = {};
    m_surrounding.clear();
    m_anchorByte = 0u;
    m_caretByte = 0u;
    m_surroundingRevision = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

