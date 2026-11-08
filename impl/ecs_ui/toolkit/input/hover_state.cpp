// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "hover_state.h"

#include <impl/ecs_ui/toolkit/input/router_pointer_hover.adhd.h>

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InputHoverState::setTarget(const Span<const HitTarget> targets, const Span<const TargetLookup> lookup, const Span<const PopupRecord> popups, const HitTarget* const hovered)noexcept{
    b8 changedOwner = false;
    UpdateRouterHoverTarget(targets, lookup, popups, hovered != nullptr, hovered != nullptr ? *hovered : HitTarget{}, m_identity, m_target, changedOwner);
    if(changedOwner)
        advanceActivity();
}

void InputHoverState::advanceActivity()noexcept{
    if(m_activityGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    ++m_activityGeneration;
}

void InputHoverState::clearTarget()noexcept{
    m_target = {};
    m_identity = {};
}

void InputHoverState::forgetSource()noexcept{
    advanceActivity();
    clearTarget();
    m_known = false;
    m_source = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

