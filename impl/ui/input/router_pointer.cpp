// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InputRouter::routePointer(const InputEvent& event, InputRoutingResult& result){
    m_pointer = event.position;
    m_pointerKnown = true;
    updateHover();
    const HitTarget* hit = findTarget(m_hover);
    if(event.type == InputEventType::PointerMove){
        if(m_primaryDown)
            updatePointerGesture(event.position, false);
        result.pointerConsumed |= m_primaryDown ? m_pointerSequenceConsumed : hasPopup() || hit != nullptr;
        return;
    }
    if(event.type == InputEventType::PrimaryDown){
        if(m_primaryDown){
            result.pointerConsumed |= m_pointerSequenceConsumed;
            return;
        }
        m_primaryDown = true;
        m_pointerSequenceConsumed = hasPopup() || hit != nullptr;
        result.pointerConsumed |= m_pointerSequenceConsumed;
        if(hasPopup() && hit == nullptr){
            const bool dismissed = dismissPopup(PopupDismissReason::OutsideClick);
            if(dismissed)
                updateHover();
            m_capture = {};
            m_capturePopup = {};
            m_captureDeclaration = 0u;
            m_captureControl = {};
            return;
        }
        m_capture = hit == nullptr ? WidgetId{} : hit->id;
        m_captureDeclaration = hit == nullptr ? 0u : hit->declarationGeneration;
        m_capturePopup = hit == nullptr ? PopupToken{} : hit->popup;
        m_captureControl = hit == nullptr ? ControlToken{} : hit->control;
        const HitTarget* focused = hit != nullptr && hit->owner.valid() ? controlHost(*hit) : hit;
        m_focus = focused != nullptr && focused->focusable ? focused->id : WidgetId{};
        m_focusDeclaration = m_focus.valid() ? focused->declarationGeneration : 0u;
        m_focusControl = m_focus.valid() ? focused->control : ControlToken{};
        if(hit != nullptr && hit->pointerGesture)
            appendPointerGesture(*hit, result);
        return;
    }
    result.pointerConsumed |= m_primaryDown ? m_pointerSequenceConsumed : hit != nullptr;
    if(
        m_primaryDown && m_pointerSequenceConsumed && hit != nullptr
        && hit->id == m_capture && hit->declarationGeneration == m_captureDeclaration && hit->activatable
        && hit->popup == m_capturePopup && hit->control == m_captureControl
    ){
        const HitTarget* host = hit->owner.valid() ? controlHost(*hit) : nullptr;
        if(host != nullptr)
            appendControlAction(*host, *hit, ControlActionKind::Activate, 0.0, result);
        else
            appendActivation(*hit, InputActionSource::Pointer, result);
    }
    if(m_primaryDown)
        updatePointerGesture(event.position, true);
    m_primaryDown = false;
    m_pointerSequenceConsumed = false;
    m_capture = {};
    m_capturePopup = {};
    m_captureDeclaration = 0u;
    m_captureControl = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

