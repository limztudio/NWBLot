// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InputRouter::routePointer(const InputEvent& event, InputRoutingResult& result, const HitTarget* hit){
    if(event.type == InputEventType::PointerMove){
        if(m_primaryDown)
            updatePointerGesture(event.position, false);
        result.pointerConsumed |= m_primaryDown ? m_pointerSequenceConsumed : m_secondaryDown ? m_secondarySequenceConsumed : hasPopup() || hit != nullptr;
        return;
    }
    if(event.type == InputEventType::PrimaryDown){
        if(m_primaryDown){
            result.pointerConsumed |= m_pointerSequenceConsumed;
            return;
        }
        advanceHoverActivity();
        m_primaryDown = true;
        m_pointerSubmitArmed = false;
        m_pointerSequenceConsumed = m_secondaryDown ? m_secondarySequenceConsumed : hasPopup() || hit != nullptr;
        result.pointerConsumed |= m_pointerSequenceConsumed;
        if(!m_pointerSequenceConsumed){
            clearFocus();
            return;
        }
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
        m_pointerSubmitArmed = hit != nullptr && hit->activatable;
        const HitTarget* focused = hit != nullptr && hit->owner.valid() ? controlHost(*hit) : hit;
        const WidgetId nextFocus = focused != nullptr && focused->focusable ? focused->id : WidgetId{};
        if(m_focus != nextFocus || (nextFocus.valid() && (m_focusDeclaration != focused->declarationGeneration || m_focusControl != focused->control)))
            retireCommandFocus();
        m_focus = focused != nullptr && focused->focusable ? focused->id : WidgetId{};
        m_focusDeclaration = m_focus.valid() ? focused->declarationGeneration : 0u;
        m_focusControl = m_focus.valid() ? focused->control : ControlToken{};
        if(hit != nullptr && hit->pointerGesture)
            appendPointerGesture(*hit, result);
        return;
    }
    result.pointerConsumed |= m_primaryDown ? m_pointerSequenceConsumed : m_secondaryDown ? m_secondarySequenceConsumed : hit != nullptr;
    if(m_primaryDown && m_pointerSequenceConsumed && m_pointerSubmitArmed && hit != nullptr && hit->id == m_capture && hit->declarationGeneration == m_captureDeclaration && hit->activatable && hit->popup == m_capturePopup && hit->control == m_captureControl){
        const HitTarget* host = hit->owner.valid() ? controlHost(*hit) : nullptr;
        if(host != nullptr)
            appendControlAction(*host, *hit, ControlActionKind::Activate, 0.0, result);
        else
            appendActivation(*hit, InputActionSource::Pointer, event.source, result);
    }
    if(m_primaryDown)
        updatePointerGesture(event.position, true);
    m_primaryDown = false;
    m_pointerSubmitArmed = false;
    m_pointerSequenceConsumed = false;
    m_capture = {};
    m_capturePopup = {};
    m_captureDeclaration = 0u;
    m_captureControl = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

