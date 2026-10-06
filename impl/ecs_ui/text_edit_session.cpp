// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_edit_session.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiTextEditSession::UiTextEditSession(Core::Alloc::GlobalArena& arena, Core::ITextInputService& service)
    : m_service(service)
    , m_event(arena)
    , m_expectedModel(arena)
    , m_publishedText(arena)
{}

UiTextEditSession::~UiTextEditSession(){
    if(!release())
        TerminateInvariant();
}


Core::TextInputAdmission::Enum UiTextEditSession::begin(
    const UiTextEditOwner& owner, Ui::EditModel& model, const Core::TextInputRect caret){
    if(!m_service.isOwnerThread())
        return Core::TextInputAdmission::WrongThread;
    if(!owner.valid() || model.composition().active)
        return Core::TextInputAdmission::InvalidSession;
    if(m_token.valid())
        return Core::TextInputAdmission::Busy;
    const Core::TextInputSessionDesc desc{ caret, model.text(), model.anchor(), model.caret() };
    const auto result = m_service.begin(desc);
    if(result.admission != Core::TextInputAdmission::Accepted)
        return result.admission;
    m_token = result.token;
    m_owner = owner;
    m_lastSequence = 0u;
    m_expectedModel.capture(model);
    m_publishedText = m_expectedModel.m_expectedText;
    m_publishedAnchor = model.anchor();
    m_publishedCaret = model.caret();
    m_publishedModelRevision = model.revision();
    m_publishedExternalRevision = model.externalRevision();
    m_publishedSelectionGeneration = model.selectionGeneration();
    m_surroundingRevision = m_service.surroundingRevision(m_token);
    return Core::TextInputAdmission::Accepted;
}

Core::TextInputAdmission::Enum UiTextEditSession::refresh(
    const UiTextEditOwner& owner, const Ui::EditModel& model, const Core::TextInputRect caret){
    if(!m_service.isOwnerThread())
        return Core::TextInputAdmission::WrongThread;
    if(!m_token.valid() || !(m_owner == owner) || !m_expectedModel.matches(model))
        return Core::TextInputAdmission::InvalidSession;
    const auto admission = m_service.updateCaret(m_token, caret);
    if(admission != Core::TextInputAdmission::Accepted)
        return admission;
    if(matchesPublishedModel(model))
        return Core::TextInputAdmission::Accepted;
    return publishSurrounding(model);
}

UiTextEditResult UiTextEditSession::drain(const UiTextEditOwner& owner, Ui::EditModel& model){
    UiTextEditResult result;
    if(!m_service.isOwnerThread()){
        result.status = UiTextEditStatus::WrongThread;
        return result;
    }
    if(!m_token.valid())
        return result;
    if(!(m_owner == owner) || !m_expectedModel.matches(model)){
        result.status = m_owner == owner ? UiTextEditStatus::StaleModel : UiTextEditStatus::StaleOwner;
        if(!release())
            result.status = UiTextEditStatus::NativeFailure;
        return result;
    }
    const auto initialRevision = model.revision();
    for(usize index = 0u; index <= Core::s_TextInputMaxEvents; ++index){
        const auto poll = m_service.poll(m_token, m_event);
        if(poll == Core::TextInputPollResult::Pending)
            break;
        if(poll != Core::TextInputPollResult::Event){
            result.status = poll == Core::TextInputPollResult::WrongThread
                ? UiTextEditStatus::WrongThread : UiTextEditStatus::Cancelled;
            model.cancelComposition();
            if(!release())
                result.status = UiTextEditStatus::NativeFailure;
            break;
        }
        if(m_event.token != m_token || m_event.sequence <= m_lastSequence || index == Core::s_TextInputMaxEvents){
            result.status = UiTextEditStatus::InvalidEvent;
        }
        else{
            m_lastSequence = m_event.sequence;
            result.status = applyEvent(model);
        }
        if(result.status != UiTextEditStatus::Applied){
            model.cancelComposition();
            if(!release())
                result.status = UiTextEditStatus::NativeFailure;
            break;
        }
        ++result.eventsApplied;
        m_expectedModel.capture(model);
    }
    result.textChanged = model.revision() != initialRevision;
    if(m_token.valid() && !matchesPublishedModel(model)){
        if(publishSurrounding(model) != Core::TextInputAdmission::Accepted){
            model.cancelComposition();
            result.status = UiTextEditStatus::NativeFailure;
            if(!release())
                TerminateInvariant();
        }
    }
    return result;
}

bool UiTextEditSession::end(const UiTextEditOwner& owner, Ui::EditModel& model){
    if(!m_service.isOwnerThread() || !m_token.valid() || !(m_owner == owner))
        return false;
    if(m_expectedModel.matches(model))
        model.cancelComposition();
    return release();
}


bool UiTextEditSession::matchesPublishedModel(const Ui::EditModel& model)const noexcept{
    return
        model.revision() == m_publishedModelRevision && model.externalRevision() == m_publishedExternalRevision
        && model.selectionGeneration() == m_publishedSelectionGeneration
        && model.text() == AStringView(m_publishedText)
        && model.anchor() == m_publishedAnchor && model.caret() == m_publishedCaret
    ;
}

Core::TextInputAdmission::Enum UiTextEditSession::publishSurrounding(
    const Ui::EditModel& model, const Core::TextInputChangeCause::Enum cause){
    const auto admission = m_service.updateSurrounding(
        m_token, model.text(), model.anchor(), model.caret(), cause
    );
    if(admission != Core::TextInputAdmission::Accepted)
        return admission;
    m_publishedText.assign(model.text().data(), model.text().size());
    m_publishedAnchor = model.anchor();
    m_publishedCaret = model.caret();
    m_publishedModelRevision = model.revision();
    m_publishedExternalRevision = model.externalRevision();
    m_publishedSelectionGeneration = model.selectionGeneration();
    m_surroundingRevision = m_service.surroundingRevision(m_token);
    return Core::TextInputAdmission::Accepted;
}

bool UiTextEditSession::release(){
    if(!m_token.valid())
        return true;
    if(!m_service.isOwnerThread())
        return false;
    if(!m_service.end(m_token) && m_service.activeSession() == m_token)
        return false;
    m_token = {};
    m_owner = {};
    m_expectedModel.clear();
    m_publishedText.clear();
    m_preeditCaretVisible = true;
    return true;
}

UiTextEditStatus::Enum UiTextEditSession::applyEvent(Ui::EditModel& model){
    if(m_event.kind == Core::TextInputEventKind::Commit)
        m_preeditCaretVisible = true;
    else if(m_event.kind == Core::TextInputEventKind::Preedit)
        m_preeditCaretVisible = m_event.caretVisible;
    return ApplyUiTextEditEvent(model, m_event, m_surroundingRevision, matchesPublishedModel(model));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

