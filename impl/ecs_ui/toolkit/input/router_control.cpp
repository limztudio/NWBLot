// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ControlAction> InputRouter::consumeControlAction(const WidgetId host, const u64 declarationGeneration, const ControlToken& token)noexcept{
    const HitTarget* target = findTarget(host, declarationGeneration);
    if(declarationGeneration == 0u || !token.valid() || target == nullptr || !isInteractive(*target) || target->control != token || target->owner.valid() || !target->focusable)
        return MakeUnexpected(Failure{});
    for(usize index = 0u; index < m_controlActions.size(); ++index){
        const auto& candidate = m_controlActions[index];
        if(candidate.id.target != host || candidate.id.declarationGeneration != declarationGeneration || candidate.control != token || !currentControlAction(candidate))
            continue;
        const ControlAction action = candidate;
        m_controlActions.erase(m_controlActions.begin() + static_cast<isize>(index));
        return action;
    }
    return MakeUnexpected(Failure{});
}

void InputRouter::fenceControl(const WidgetId host, const u64 declarationGeneration, const ControlToken& token)noexcept{
    const HitTarget* accepted = findTarget(host);
    if(accepted == nullptr || (accepted->declarationGeneration == declarationGeneration && accepted->control == token))
        return;
    const b8 preserveText = accepted->declarationGeneration == declarationGeneration && accepted->textEditable && !accepted->owner.valid() && (accepted->scrollable || accepted->control.empty());
    for(auto& target : m_targets){
        if(target.id == host){
            if(preserveText){
                target.control = {};
                target.scrollable = false;
                target.navigable = false;
                target.horizontalNavigation = false;
            }
            else
                target.enabled = false;
        }
        else if(target.owner == host)
            target.enabled = false;
    }
    reconcileTargets();
}

b8 InputRouter::validControlTargets()const noexcept{
    for(const auto& target : m_stagedTargets){
        if(!HasValidHitTargetControlProperties(target))
            return false;
        if(!target.owner.valid())
            continue;
        const HitTarget* host = FindTargetInTable(m_stagedTargets, m_stagedLookup, target.owner, target.ownerDeclarationGeneration);
        if(!host || !host->enabled || !host->focusable || host->owner.valid() || host->control != target.control || host->popup != target.popup || host->layer != target.layer)
            return false;
    }
    return true;
}

const HitTarget* InputRouter::controlHost(const HitTarget& target)const noexcept{
    const HitTarget* host = target.owner.valid() ? findTarget(target.owner, target.ownerDeclarationGeneration) : &target;
    if(host == nullptr || !host->enabled || !host->focusable || host->owner.valid() || !host->control.valid() || host->control != target.control || host->popup != target.popup || !allowedByPopup(*host) || !HasVisibleBounds(*host))
        return nullptr;
    return host;
}

b8 InputRouter::currentControlAction(const ControlAction& action)const noexcept{
    const HitTarget* host = findTarget(action.id.target, action.id.declarationGeneration);
    if(host == nullptr || !isInteractive(*host) || !host->focusable || host->owner.valid() || !action.control.valid() || host->control != action.control || host->popup != action.popup)
        return false;
    if(action.kind == ControlActionKind::Wheel)
        return host->scrollable;
    if(action.kind != ControlActionKind::Activate){
        if(!host->navigable || ((action.kind == ControlActionKind::Left || action.kind == ControlActionKind::Right) && !host->horizontalNavigation))
            return false;
        if(action.source == host->id)
            return true;
        const HitTarget* source = findTarget(action.source, action.sourceDeclarationGeneration);
        return source != nullptr && isInteractive(*source) && source->control == action.sourceControl && commandHost(*source) == host;
    }
    const HitTarget* source = findTarget(action.source, action.sourceDeclarationGeneration);
    return
        source != nullptr && isInteractive(*source) && source->activatable
        && source->owner == host->id && source->ownerDeclarationGeneration == host->declarationGeneration
        && source->control == action.control && source->popup == action.popup && source->value == action.value
    ;
}

void InputRouter::reconcileControlActions()noexcept{
    for(usize index = 0u; index < m_controlActions.size();){
        if(!currentControlAction(m_controlActions[index]))
            m_controlActions.erase(m_controlActions.begin() + static_cast<isize>(index));
        else
            ++index;
    }
    // Losing an initial-down owner is terminal for that held key even if its token later returns.
    for(auto& source : m_commandSources){
        if(!currentControlCommandOwner(source)){
            if(source.controlOwner.host.valid())
                source.submitArmed = false;
            source.controlOwner = {};
        }
        const HitTarget* focused = findTarget(source.focusOwner.target, source.focusOwner.declarationGeneration);
        if(!focused || !isInteractive(*focused) || focused->popup != source.focusOwner.popup || focused->control != source.focusOwner.control)
            source.focusOwner = {};
    }
}

void InputRouter::appendControlAction(const HitTarget& host, const HitTarget& source, const ControlActionKind::Enum kind, const f64 delta, const f64 deltaX, InputRoutingResult& result){
    if(m_controlActions.size() == s_InputMaxControlActions || m_nextActionSequence == 0u){
        result.activationOverflow = true;
        return;
    }
    ControlAction action;
    action.id = { host.id, host.declarationGeneration, m_layoutGeneration, m_nextActionSequence };
    action.source = source.id;
    action.popup = host.popup;
    action.control = host.control;
    action.kind = kind;
    action.delta = delta;
    action.value = source.value;
    action.step = host.scrollStep;
    action.pageRows = host.pageRows;
    action.maximum = host.gestureMaximum;
    action.sourceDeclarationGeneration = source.declarationGeneration;
    action.sourceControl = source.control;
    action.deltaX = deltaX;
    action.stepX = host.scrollStepX;
    action.maximumX = host.gestureMaximumX;
    m_controlActions.push_back(action);
    ++m_nextActionSequence;
}

b8 InputRouter::routeControlNavigation(const InputEvent& event, const HitTarget& host, const HitTarget& source, const b8 alreadyPressed, CommandSource& held, InputRoutingResult& result){
    ControlActionKind::Enum kind = ControlActionKind::Wheel;
    if(!TryTranslateControlNavigationCommand(event.command, host.horizontalNavigation, kind))
        return false;
    ControlCommandOwner& owner = held.controlOwner;
    if(!alreadyPressed && !event.repeat)
        owner = { host.id, host.declarationGeneration, host.popup, host.control, kind == ControlActionKind::Left || kind == ControlActionKind::Right };
    if(
        owner.host == host.id && owner.declarationGeneration == host.declarationGeneration
        && owner.popup == host.popup && owner.control == host.control
        && held.focusOwner.target == source.id && held.focusOwner.declarationGeneration == source.declarationGeneration
        && held.focusOwner.popup == source.popup && held.focusOwner.control == source.control
    )
        appendControlAction(host, source, kind, 0.0, result);
    return true;
}

void InputRouter::routeWheel(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result){
    result.pointerConsumed |= m_primaryDown ? m_pointerSequenceConsumed : m_secondaryDown ? m_secondarySequenceConsumed : hasPopup() || hit != nullptr;
    if((m_primaryDown && !m_pointerSequenceConsumed) || (m_secondaryDown && !m_secondarySequenceConsumed) || hit == nullptr || (event.scrollX == 0.0 && event.scrollY == 0.0))
        return;
    const HitTarget* host = controlHost(*hit);
    if(host != nullptr && host->scrollable && (event.scrollY != 0.0 || host->scrollStepX > 0.0))
        appendControlAction(*host, *hit, ControlActionKind::Wheel, event.scrollY, event.scrollX, result);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

