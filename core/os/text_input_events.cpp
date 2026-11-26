// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"
#include "text_input_text.h"

#include <global/atomic_identity.h>
#include <global/scope_exit.h>
#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextInputAdmission::Enum QueuedTextInputService::emitCommit(const TextInputSessionToken token, const AStringView text){
    return admitEvent(token, TextInputEventKind::Commit, text, 0u, 0u, 0u, 0u, true);
}

TextInputAdmission::Enum QueuedTextInputService::emitPreedit(const TextInputSessionToken token, const AStringView text, const usize anchorByte, const usize caretByte, const b8 caretVisible, const Span<const TextInputPreeditClause> clauses){
    return admitEvent(token, TextInputEventKind::Preedit, text, anchorByte, caretByte, 0u, 0u, caretVisible, TextInputDeletionBasis::Caret, clauses);
}

TextInputAdmission::Enum QueuedTextInputService::emitDeleteSurrounding(const TextInputSessionToken token, const usize beforeBytes, const usize afterBytes, const u64 revision, const TextInputDeletionBasis::Enum basis){
    if(!isOwnerThread())
        return TextInputAdmission::WrongThread;
    if(!token.valid() || token != m_activeToken)
        return TextInputAdmission::InvalidSession;
    if(revision != m_surroundingRevision)
        return TextInputAdmission::InvalidRange;
    if(basis >= TextInputDeletionBasis::kCount)
        return TextInputAdmission::InvalidRange;
    const usize start = basis == TextInputDeletionBasis::Selection ? Min(m_anchorByte, m_caretByte) : m_caretByte;
    const usize end = basis == TextInputDeletionBasis::Selection ? Max(m_anchorByte, m_caretByte) : m_caretByte;
    if(beforeBytes > start || afterBytes > m_surrounding.size() - end)
        return TextInputAdmission::InvalidRange;
    if(!IsUtf8ScalarBoundary(m_surrounding, start - beforeBytes) || !IsUtf8ScalarBoundary(m_surrounding, end + afterBytes))
        return TextInputAdmission::InvalidRange;
    return admitEvent(token, TextInputEventKind::DeleteSurrounding, {}, 0u, 0u, beforeBytes, afterBytes, true, basis);
}

b8 QueuedTextInputService::cancelSession(const TextInputSessionToken token, const TextInputCancelReason::Enum reason){
    if(!isOwnerThread() || !token.valid() || token != m_activeToken)
        return false;
    const u64 revision = m_surroundingRevision;
    invalidateSession();
    clearEvents();
    m_cancelledToken = token;
    TextInputEvent& event = m_events.empty() ? m_events.emplace_back(m_arena) : m_events[0u];
    event.text.clear();
    event.preeditClauses.clear();
    event.token = token;
    event.sequence = m_nextSequence;
    AdvanceNonWrappingRevision(m_nextSequence);
    event.surroundingRevision = revision;
    event.kind = TextInputEventKind::Cancelled;
    event.anchorByte = 0u;
    event.caretByte = 0u;
    event.deleteBeforeBytes = 0u;
    event.deleteAfterBytes = 0u;
    event.caretVisible = true;
    event.cancelReason = reason;
    event.deletionBasis = TextInputDeletionBasis::Caret;
    m_eventHead = 0u;
    m_eventCount = 1u;
    const b8 wasTransitioning = m_transitioning;
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
    const b8 caretVisible,
    const TextInputDeletionBasis::Enum basis,
    const Span<const TextInputPreeditClause> clauses
){
    if(!isOwnerThread())
        return TextInputAdmission::WrongThread;
    if(!token.valid() || token != m_activeToken)
        return TextInputAdmission::InvalidSession;
    const TextInputAdmission::Enum validation = ValidateTextInputUtf8(text, s_TextInputMaxEventTextBytes);
    if(validation != TextInputAdmission::Accepted)
        return validation;
    if(kind == TextInputEventKind::Preedit){
        if(!IsUtf8ScalarBoundary(text, anchorByte) || !IsUtf8ScalarBoundary(text, caretByte))
            return TextInputAdmission::InvalidRange;
        usize prevClauseEnd = 0u;
        for(const TextInputPreeditClause& clause : clauses){
            if(clause.beginByte >= clause.endByte || clause.endByte > text.size() || clause.beginByte < prevClauseEnd)
                return TextInputAdmission::InvalidRange;
            if(!IsUtf8ScalarBoundary(text, clause.beginByte) || !IsUtf8ScalarBoundary(text, clause.endByte))
                return TextInputAdmission::InvalidRange;
            if(static_cast<u8>(clause.style) >= static_cast<u8>(TextInputPreeditClauseStyle::kCount))
                return TextInputAdmission::InvalidRange;
            prevClauseEnd = clause.endByte;
        }
    }
    else if(kind == TextInputEventKind::Commit && text.empty())
        return TextInputAdmission::Accepted;

    const usize backIdx = m_eventCount > 0u ? (m_eventHead + m_eventCount - 1u) % s_TextInputMaxEvents : 0u;
    const b8 replace = kind == TextInputEventKind::Preedit && m_eventCount > 0u && m_events[backIdx].kind == TextInputEventKind::Preedit;
    const usize oldBytes = replace ? m_events[backIdx].text.size() : 0u;
    if((!replace && m_eventCount >= s_TextInputMaxEvents) || text.size() > s_TextInputMaxQueuedTextBytes - (m_queuedTextBytes - oldBytes)){
        if(!cancelSession(token, TextInputCancelReason::Overflow))
            TerminateInvariant();
        return TextInputAdmission::QueueFull;
    }
    const usize targetIdx = replace ? backIdx : (m_eventHead + m_eventCount) % s_TextInputMaxEvents;
    const b8 pushedNewSlot = !replace && targetIdx == m_events.size();
    TextInputEvent& event = pushedNewSlot ? m_events.emplace_back(m_arena) : m_events[targetIdx];
    ScopeExit rollback([this, &event, replace, pushedNewSlot]()noexcept{
        if(pushedNewSlot)
            m_events.pop_back();
        else if(!replace){
            event.text.clear();
            event.preeditClauses.clear();
        }
    });
    if(kind == TextInputEventKind::Preedit && !clauses.empty())
        event.preeditClauses.reserve(clauses.size());
    event.text.assign(text.data(), text.size());
    event.preeditClauses.clear();
    if(kind == TextInputEventKind::Preedit){
        for(const TextInputPreeditClause& clause : clauses)
            event.preeditClauses.push_back(clause);
    }
    event.token = token;
    event.sequence = m_nextSequence;
    AdvanceNonWrappingRevision(m_nextSequence);
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
    if(!replace)
        ++m_eventCount;
    rollback.release();
    return TextInputAdmission::Accepted;
}

void QueuedTextInputService::clearEvents()noexcept{
    for(usize offset = 0u; offset < m_eventCount; ++offset){
        TextInputEvent& entry = m_events[(m_eventHead + offset) % s_TextInputMaxEvents];
        entry.text.clear();
        entry.preeditClauses.clear();
    }
    m_eventHead = 0u;
    m_eventCount = 0u;
    m_queuedTextBytes = 0u;
}

void QueuedTextInputService::invalidateSession()noexcept{
    m_activeToken = {};
    m_surrounding.clear();
    m_anchorByte = 0u;
    m_caretByte = 0u;
    m_surroundingRevision = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
