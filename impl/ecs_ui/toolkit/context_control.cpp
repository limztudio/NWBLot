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

Expected<ControlAction> Context::takeControlAction(
    const WidgetState& state, const bool enabled, const ControlToken& token
){
    if(m_failed || !currentDeclaration(state))
        return MakeUnexpected(Failure{});
    if(!enabled){
        m_input.invalidateTarget(state.id);
        return MakeUnexpected(Failure{});
    }
    const HitTarget* target = m_input.findTarget(state.id);
    if(target && target->popup != m_currentPopup)
        return MakeUnexpected(Failure{});
    return m_input.consumeControlAction(state.id, state.declarationGeneration, token);
}

Expected<PointerGesture> Context::takePartPointerGesture(
    const WidgetState& owner, const WidgetId part, const bool enabled
){
    if(m_failed || !currentDeclaration(owner))
        return MakeUnexpected(Failure{});
    if(!enabled){
        m_input.invalidateTarget(owner.id);
        return MakeUnexpected(Failure{});
    }
    const HitTarget* target = m_input.findTarget(part);
    if(target && (target->popup != m_currentPopup || target->owner != owner.id))
        return MakeUnexpected(Failure{});
    return m_input.consumePointerGesture(part, owner.declarationGeneration);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

