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
        result.pointerConsumed |= m_primaryDown ? m_pointerSequenceConsumed : hit != nullptr;
        return;
    }
    if(event.type == InputEventType::PrimaryDown){
        if(m_primaryDown){
            result.pointerConsumed |= m_pointerSequenceConsumed;
            return;
        }
        m_primaryDown = true;
        m_pointerSequenceConsumed = hit != nullptr;
        result.pointerConsumed |= m_pointerSequenceConsumed;
        m_capture = hit == nullptr ? WidgetId{} : hit->id;
        m_captureDeclaration = hit == nullptr ? 0u : hit->declarationGeneration;
        m_focus = hit != nullptr && hit->focusable ? hit->id : WidgetId{};
        m_focusDeclaration = m_focus.valid() ? hit->declarationGeneration : 0u;
        return;
    }
    result.pointerConsumed |= m_primaryDown ? m_pointerSequenceConsumed : hit != nullptr;
    if(
        m_primaryDown && m_pointerSequenceConsumed && hit != nullptr
        && hit->id == m_capture && hit->declarationGeneration == m_captureDeclaration && hit->activatable
    )
        appendActivation(*hit, InputActionSource::Pointer, result);
    m_primaryDown = false;
    m_pointerSequenceConsumed = false;
    m_capture = {};
    m_captureDeclaration = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

