// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"
#include "text_input_text.h"

#include <global/atomic.h>
#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_text_input_service{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static u64 AllocateServiceIdentity(){
    static Atomic<u64> s_NextIdentity{ 1u };
    const u64 identity = s_NextIdentity.fetch_add(1u, MemoryOrder::relaxed);
    if(identity == 0u)
        TerminateInvariant();
    return identity;
}

static TextInputAdmission::Enum ValidateSurrounding(const AStringView text, const usize anchorByte, const usize caretByte){
    const TextInputAdmission::Enum status = ValidateTextInputUtf8(text, s_TextInputMaxSurroundingBytes);
    if(status != TextInputAdmission::Accepted)
        return status;
    if(!IsTextInputUtf8Boundary(text, anchorByte) || !IsTextInputUtf8Boundary(text, caretByte))
        return TextInputAdmission::InvalidRange;
    return TextInputAdmission::Accepted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


QueuedTextInputService::QueuedTextInputService(Alloc::GlobalArena& arena)
    : m_arena(arena)
    , m_surrounding(arena)
    , m_events(arena)
    , m_ownerThread(QueryCurrentThreadId())
    , m_serviceIdentity(__hidden_text_input_service::AllocateServiceIdentity())
{
    m_events.reserve(s_TextInputMaxEvents);
}

QueuedTextInputService::~QueuedTextInputService(){
    GLOBAL_ASSERT(isOwnerThread());
}

TextInputSessionToken QueuedTextInputService::activeSession()const noexcept{
    return isOwnerThread() ? m_activeToken : TextInputSessionToken{};
}

u64 QueuedTextInputService::surroundingRevision(const TextInputSessionToken token)const noexcept{
    return isOwnerThread() && token.valid() && token == m_activeToken ? m_surroundingRevision : 0u;
}

TextInputBeginResult QueuedTextInputService::begin(const TextInputSessionDesc& desc){
    if(!isOwnerThread())
        return { .token = {}, .admission = TextInputAdmission::WrongThread };
    if(m_transitioning || m_activeToken.valid())
        return { .token = {}, .admission = TextInputAdmission::Busy };
    if(!capabilities().commit)
        return { .token = {}, .admission = TextInputAdmission::Unsupported };
    if(!m_focused)
        return { .token = {}, .admission = TextInputAdmission::Unavailable };
    if(!IsTextInputCaretRectValid(desc.caret))
        return { .token = {}, .admission = TextInputAdmission::InvalidRange };
    const TextInputAdmission::Enum validation = __hidden_text_input_service::ValidateSurrounding(
        desc.surrounding, desc.anchorByte, desc.caretByte
    );
    if(validation != TextInputAdmission::Accepted)
        return { .token = {}, .admission = validation };
    if(m_nextGeneration == Limit<u64>::s_Max)
        TerminateInvariant();

    m_transitioning = true;
    ScopeExit finishTransition([this]()noexcept{ m_transitioning = false; });
    clearEvents();
    m_cancelledToken = {};
    m_activeToken = { .service = m_serviceIdentity, .generation = m_nextGeneration };
    ++m_nextGeneration;
    m_caret = desc.caret;
    m_surrounding.assign(desc.surrounding.data(), desc.surrounding.size());
    m_anchorByte = desc.anchorByte;
    m_caretByte = desc.caretByte;
    m_surroundingRevision = 1u;
    const TextInputSessionToken token = m_activeToken;
    const TextInputSessionDesc copiedDesc{ m_caret, m_surrounding, m_anchorByte, m_caretByte };
    const TextInputAdmission::Enum admission = startNativeSession(token, copiedDesc);
    if(admission != TextInputAdmission::Accepted){
        invalidateSession();
        clearEvents();
        endNativeSession(token);
        return { .token = {}, .admission = admission };
    }
    if(m_activeToken != token)
        return { .token = {}, .admission = TextInputAdmission::Unavailable };
    return { .token = token, .admission = TextInputAdmission::Accepted };
}

bool QueuedTextInputService::end(const TextInputSessionToken token){
    if(!isOwnerThread() || m_transitioning || !token.valid())
        return false;
    if(token != m_activeToken && token != m_cancelledToken)
        return false;
    const bool active = token == m_activeToken;
    m_transitioning = true;
    ScopeExit finishTransition([this]()noexcept{ m_transitioning = false; });
    invalidateSession();
    m_cancelledToken = {};
    clearEvents();
    if(active)
        endNativeSession(token);
    return true;
}

TextInputAdmission::Enum QueuedTextInputService::updateCaret(const TextInputSessionToken token, const TextInputRect caret){
    if(!isOwnerThread())
        return TextInputAdmission::WrongThread;
    if(m_transitioning || !token.valid() || token != m_activeToken)
        return TextInputAdmission::InvalidSession;
    if(!IsTextInputCaretRectValid(caret))
        return TextInputAdmission::InvalidRange;
    m_transitioning = true;
    ScopeExit finishTransition([this]()noexcept{ m_transitioning = false; });
    const TextInputAdmission::Enum admission = updateNativeCaret(caret);
    if(admission != TextInputAdmission::Accepted)
        return admission;
    if(token != m_activeToken)
        return TextInputAdmission::NativeFailure;
    m_caret = caret;
    return TextInputAdmission::Accepted;
}

TextInputAdmission::Enum QueuedTextInputService::updateSurrounding(
    const TextInputSessionToken token,
    const AStringView text,
    const usize anchorByte,
    const usize caretByte,
    const TextInputChangeCause::Enum cause){
    if(!isOwnerThread())
        return TextInputAdmission::WrongThread;
    if(m_transitioning || !token.valid() || token != m_activeToken)
        return TextInputAdmission::InvalidSession;
    if(cause != TextInputChangeCause::InputMethod && cause != TextInputChangeCause::Other)
        return TextInputAdmission::InvalidRange;
    const TextInputAdmission::Enum validation = __hidden_text_input_service::ValidateSurrounding(text, anchorByte, caretByte);
    if(validation != TextInputAdmission::Accepted)
        return validation;
    if(m_surroundingRevision == Limit<u64>::s_Max)
        TerminateInvariant();
    m_transitioning = true;
    ScopeExit finishTransition([this]()noexcept{ m_transitioning = false; });
    m_surrounding.assign(text.data(), text.size());
    m_anchorByte = anchorByte;
    m_caretByte = caretByte;
    ++m_surroundingRevision;
    updateNativeSurrounding(m_surrounding, m_anchorByte, m_caretByte, m_surroundingRevision, cause);
    return token == m_activeToken ? TextInputAdmission::Accepted : TextInputAdmission::NativeFailure;
}

TextInputPollResult::Enum QueuedTextInputService::poll(const TextInputSessionToken token, TextInputEvent& event){
    if(!isOwnerThread())
        return TextInputPollResult::WrongThread;
    if(!token.valid() || (token != m_activeToken && token != m_cancelledToken))
        return TextInputPollResult::InvalidSession;
    if(m_events.empty())
        return TextInputPollResult::Pending;
    const TextInputEvent& next = m_events.front();
    event.text.assign(next.text.data(), next.text.size());
    event.token = next.token;
    event.sequence = next.sequence;
    event.surroundingRevision = next.surroundingRevision;
    event.kind = next.kind;
    event.anchorByte = next.anchorByte;
    event.caretByte = next.caretByte;
    event.deleteBeforeBytes = next.deleteBeforeBytes;
    event.deleteAfterBytes = next.deleteAfterBytes;
    event.caretVisible = next.caretVisible;
    event.cancelReason = next.cancelReason;
    event.deletionBasis = next.deletionBasis;
    m_queuedTextBytes -= next.text.size();
    const bool cancelled = next.kind == TextInputEventKind::Cancelled;
    m_events.erase(m_events.begin());
    if(cancelled)
        m_cancelledToken = {};
    return TextInputPollResult::Event;
}

bool QueuedTextInputService::setFocused(const bool focused){
    if(!isOwnerThread())
        return false;
    m_focused = focused;
    if(!focused && m_activeToken.valid())
        return cancelSession(m_activeToken, TextInputCancelReason::FocusLost);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextInputAdmission::Enum QueuedTextInputService::startNativeSession(TextInputSessionToken, const TextInputSessionDesc&){
    return TextInputAdmission::Accepted;
}

void QueuedTextInputService::endNativeSession(TextInputSessionToken){}

TextInputAdmission::Enum QueuedTextInputService::updateNativeCaret(TextInputRect){
    return TextInputAdmission::Accepted;
}

void QueuedTextInputService::updateNativeSurrounding(AStringView, usize, usize, u64, TextInputChangeCause::Enum){}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

