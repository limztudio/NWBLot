// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_edit_session.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiTextEditSession::~UiTextEditSession()noexcept{
    if(!cancel())
        TerminateInvariant();
}

Core::TextInputAdmission::Enum UiTextEditSession::begin(const UiTextEditOwner& owner, const Core::TextInputRect caret, Ui::EditModel& model){
    if(!m_service.isOwnerThread())
        return Core::TextInputAdmission::WrongThread;
    if(!owner.valid() || model.composition().active)
        return Core::TextInputAdmission::InvalidSession;
    if(m_token.valid())
        return Core::TextInputAdmission::Busy;
    const Core::TextInputSessionDesc desc{ caret, model.text(), model.anchor(), model.caret() };
    const auto result = m_service.begin(desc);
    if(!result)
        return result.error();
    m_token = *result;
    m_owner = owner;
    m_lastSequence = 0u;
    m_expectedModel.capture(model);
    m_publishedModel.captureSurrounding(model);
    m_surroundingRevision = m_service.surroundingRevision(m_token);
    return Core::TextInputAdmission::Accepted;
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
        if(!cancel())
            result.status = UiTextEditStatus::NativeFailure;
        return result;
    }
    const auto initialRevision = model.revision();
    for(usize index = 0u; index <= Core::s_TextInputMaxEvents; ++index){
        const auto poll = m_service.poll(m_token, m_event);
        if(poll == Core::TextInputPollResult::Pending)
            break;
        if(poll != Core::TextInputPollResult::Event){
            result.status = poll == Core::TextInputPollResult::WrongThread ? UiTextEditStatus::WrongThread : UiTextEditStatus::Cancelled;
            model.cancelComposition();
            if(!cancel())
                result.status = UiTextEditStatus::NativeFailure;
            break;
        }
        if(m_event.token != m_token || m_event.sequence <= m_lastSequence || index == Core::s_TextInputMaxEvents){
            result.status = UiTextEditStatus::InvalidEvent;
        }
        else{
            m_lastSequence = m_event.sequence;
            result.status = ApplyUiTextEditEvent(model, m_event, m_surroundingRevision, m_publishedModel.matchesSurrounding(model));
        }
        if(result.status != UiTextEditStatus::Applied){
            model.cancelComposition();
            if(!cancel())
                result.status = UiTextEditStatus::NativeFailure;
            break;
        }
        ++result.eventsApplied;
        m_expectedModel.capture(model);
    }
    result.textChanged = model.revision() != initialRevision;
    if(m_token.valid() && !m_publishedModel.matchesSurrounding(model)){
        if(publishSurrounding(model) != Core::TextInputAdmission::Accepted){
            model.cancelComposition();
            result.status = UiTextEditStatus::NativeFailure;
            if(!cancel())
                TerminateInvariant();
        }
    }
    return result;
}

b8 UiTextEditSession::end(const UiTextEditOwner& owner, Ui::EditModel& model){
    if(!m_service.isOwnerThread() || !m_token.valid() || !(m_owner == owner))
        return false;
    if(m_expectedModel.matches(model))
        model.cancelComposition();
    return cancel();
}

Core::TextInputPollResult::Enum UiTextEditSession::pollOwned(Core::TextInputEvent& event){
    if(!m_service.isOwnerThread())
        return Core::TextInputPollResult::WrongThread;
    if(!m_token.valid())
        return Core::TextInputPollResult::InvalidSession;
    return m_service.poll(m_token, event);
}

b8 UiTextEditSession::cancel(){
    if(!m_token.valid())
        return true;
    if(!m_service.isOwnerThread())
        return false;
    if(!m_service.end(m_token) && m_service.activeSession() == m_token)
        return false;
    m_token = {};
    m_owner = {};
    m_expectedModel.clear();
    m_publishedModel.clear();
    return true;
}

Core::TextInputAdmission::Enum UiTextEditSession::syncSession(const UiTextEditOwner& owner, const Ui::EditModel& model, const Core::TextInputRect caret, const Core::TextInputChangeCause::Enum cause, const b8 adoptExpected){
    if(!m_service.isOwnerThread())
        return Core::TextInputAdmission::WrongThread;
    if(!m_token.valid() || !(m_owner == owner) || (adoptExpected ? m_service.activeSession() != m_token : !m_expectedModel.matches(model)))
        return Core::TextInputAdmission::InvalidSession;
    const auto admission = m_service.updateCaret(m_token, caret);
    if(admission != Core::TextInputAdmission::Accepted)
        return admission;
    if(!m_publishedModel.matchesSurrounding(model)){
        const auto publish = publishSurrounding(model, cause);
        if(publish != Core::TextInputAdmission::Accepted)
            return publish;
    }
    if(adoptExpected)
        m_expectedModel.capture(model);
    return Core::TextInputAdmission::Accepted;
}

Core::TextInputAdmission::Enum UiTextEditSession::publishSurrounding(const Ui::EditModel& model, const Core::TextInputChangeCause::Enum cause){
    const auto admission = m_service.updateSurrounding(m_token, model.text(), model.anchor(), model.caret(), cause);
    if(admission != Core::TextInputAdmission::Accepted)
        return admission;
    m_publishedModel.captureSurrounding(model);
    m_surroundingRevision = m_service.surroundingRevision(m_token);
    return Core::TextInputAdmission::Accepted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

