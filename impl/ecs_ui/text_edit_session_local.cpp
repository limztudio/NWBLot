// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_edit_session.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::TextInputPollResult::Enum UiTextEditSession::pollOwned(Core::TextInputEvent& event){
    if(!m_service.isOwnerThread())
        return Core::TextInputPollResult::WrongThread;
    if(!m_token.valid())
        return Core::TextInputPollResult::InvalidSession;
    return m_service.poll(m_token, event);
}

Core::TextInputAdmission::Enum UiTextEditSession::adoptLocal(
    const UiTextEditOwner& owner, const Ui::EditModel& model, const Core::TextInputRect caret,
    const Core::TextInputChangeCause::Enum cause
){
    if(!m_service.isOwnerThread())
        return Core::TextInputAdmission::WrongThread;
    if(!m_token.valid() || !(m_owner == owner) || m_service.activeSession() != m_token)
        return Core::TextInputAdmission::InvalidSession;
    const auto admission = m_service.updateCaret(m_token, caret);
    if(admission != Core::TextInputAdmission::Accepted)
        return admission;
    if(!matchesPublishedModel(model)){
        const auto publish = publishSurrounding(model, cause);
        if(publish != Core::TextInputAdmission::Accepted)
            return publish;
    }
    m_expectedModel.capture(model);
    return Core::TextInputAdmission::Accepted;
}

bool UiTextEditSession::cancel(){
    return release();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

