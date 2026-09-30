// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Context::addPartTarget(const WidgetState& owner, const WidgetId part, HitTarget target){
    if(m_failed || !currentDeclaration(owner) || !part.valid() || part == owner.id || m_targets.size() >= s_InputMaxTargets){
        fail();
        return false;
    }
    target.id = part;
    target.declarationGeneration = owner.declarationGeneration;
    target.owner = owner.id;
    target.ownerDeclarationGeneration = owner.declarationGeneration;
    target.paintOrder = static_cast<u32>(m_targets.size());
    target.popup = m_currentPopup;
    target.layer = m_popupLayer;
    m_targets.push_back({ target, owner.root });
    return true;
}

bool Context::takeControlAction(
    const WidgetState& state, const bool enabled, const ControlToken& token, ControlAction& action){
    if(m_failed || !currentDeclaration(state))
        return false;
    if(!enabled){
        m_input.invalidateTarget(state.id);
        return false;
    }
    for(const auto& target : m_input.targets()){
        if(target.id == state.id && target.popup != m_currentPopup)
            return false;
    }
    return m_input.consumeControlAction(state.id, state.declarationGeneration, token, action);
}

bool Context::takePartPointerGesture(
    const WidgetState& owner, const WidgetId part, const bool enabled, PointerGesture& gesture){
    if(m_failed || !currentDeclaration(owner))
        return false;
    if(!enabled){
        m_input.invalidateTarget(owner.id);
        return false;
    }
    for(const auto& target : m_input.targets()){
        if(target.id == part && (target.popup != m_currentPopup || target.owner != owner.id))
            return false;
    }
    return m_input.consumePointerGesture(part, owner.declarationGeneration, gesture);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

