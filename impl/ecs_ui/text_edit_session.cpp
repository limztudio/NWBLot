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
    , m_expectedText(arena)
    , m_expectedPreedit(arena)
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
    captureModel(model);
    m_publishedText = m_expectedText;
    m_publishedAnchor = model.anchor();
    m_publishedCaret = model.caret();
    m_publishedModelRevision = model.revision();
    m_surroundingRevision = m_service.surroundingRevision(m_token);
    return Core::TextInputAdmission::Accepted;
}

Core::TextInputAdmission::Enum UiTextEditSession::refresh(
    const UiTextEditOwner& owner, const Ui::EditModel& model, const Core::TextInputRect caret){
    if(!m_service.isOwnerThread())
        return Core::TextInputAdmission::WrongThread;
    if(!m_token.valid() || !(m_owner == owner) || !matchesModel(model))
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
    if(!(m_owner == owner) || !matchesModel(model)){
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
        captureModel(model);
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
    if(matchesModel(model))
        model.cancelComposition();
    return release();
}


bool UiTextEditSession::matchesModel(const Ui::EditModel& model)const{
    const auto composition = model.composition();
    return
        model.revision() == m_expectedRevision && model.text() == AStringView(m_expectedText)
        && model.anchor() == m_expectedAnchor && model.caret() == m_expectedCaret
        && composition.active == m_expectedComposition.active && composition.text == AStringView(m_expectedPreedit)
        && composition.anchor == m_expectedComposition.anchor && composition.caret == m_expectedComposition.caret
        && composition.replacementStart == m_expectedComposition.replacementStart
        && composition.replacementEnd == m_expectedComposition.replacementEnd
    ;
}

bool UiTextEditSession::matchesPublishedModel(const Ui::EditModel& model)const{
    return
        model.revision() == m_publishedModelRevision && model.text() == AStringView(m_publishedText)
        && model.anchor() == m_publishedAnchor && model.caret() == m_publishedCaret
    ;
}

void UiTextEditSession::captureModel(const Ui::EditModel& model){
    m_expectedText.assign(model.text().data(), model.text().size());
    m_expectedAnchor = model.anchor();
    m_expectedCaret = model.caret();
    m_expectedRevision = model.revision();
    m_expectedComposition = model.composition();
    m_expectedPreedit.assign(m_expectedComposition.text.data(), m_expectedComposition.text.size());
    m_expectedComposition.text = {};
}

Core::TextInputAdmission::Enum UiTextEditSession::publishSurrounding(const Ui::EditModel& model){
    const auto admission = m_service.updateSurrounding(
        m_token, model.text(), model.anchor(), model.caret(), Core::TextInputChangeCause::InputMethod
    );
    if(admission != Core::TextInputAdmission::Accepted)
        return admission;
    m_publishedText.assign(model.text().data(), model.text().size());
    m_publishedAnchor = model.anchor();
    m_publishedCaret = model.caret();
    m_publishedModelRevision = model.revision();
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
    m_expectedText.clear();
    m_expectedPreedit.clear();
    m_publishedText.clear();
    m_expectedComposition = {};
    m_preeditCaretVisible = true;
    return true;
}

UiTextEditStatus::Enum UiTextEditSession::applyEvent(Ui::EditModel& model){
    switch(m_event.kind){
    case Core::TextInputEventKind::Commit:
        m_preeditCaretVisible = true;
        return (model.composition().active ? model.commitComposition(m_event.text) : model.replaceSelection(m_event.text))
            ? UiTextEditStatus::Applied : UiTextEditStatus::ModelRejected;
    case Core::TextInputEventKind::Preedit:
        m_preeditCaretVisible = m_event.caretVisible;
        if(m_event.text.empty()){
            model.cancelComposition();
            return UiTextEditStatus::Applied;
        }
        if(!model.composition().active && !model.beginComposition())
            return UiTextEditStatus::ModelRejected;
        return model.updateComposition(m_event.text, m_event.anchorByte, m_event.caretByte)
            ? UiTextEditStatus::Applied : UiTextEditStatus::ModelRejected;
    case Core::TextInputEventKind::DeleteSurrounding: {
        if(m_event.surroundingRevision != m_surroundingRevision || !matchesPublishedModel(model))
            return UiTextEditStatus::StaleSurrounding;
        if(model.hasSelection() && (m_event.deleteBeforeBytes != 0u || m_event.deleteAfterBytes != 0u))
            return UiTextEditStatus::ModelRejected;
        const usize caret = model.caret();
        if(m_event.deleteBeforeBytes > caret || m_event.deleteAfterBytes > model.text().size() - caret)
            return UiTextEditStatus::InvalidEvent;
        return model.eraseSurrounding(m_event.deleteBeforeBytes, m_event.deleteAfterBytes)
            ? UiTextEditStatus::Applied : UiTextEditStatus::ModelRejected;
    }
    case Core::TextInputEventKind::Cancelled:
        return UiTextEditStatus::Cancelled;
    default:
        return UiTextEditStatus::InvalidEvent;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

