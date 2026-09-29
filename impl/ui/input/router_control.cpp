// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::validControlTargets()const{
    for(const auto& target : m_stagedTargets){
        if(
            (!target.control.empty() && !target.control.valid())
            || !IsFinite(target.scrollStep) || target.scrollStep < 0.0
            || !IsFinite(target.gestureMaximum) || target.gestureMaximum < 0.0 || target.pageRows == 0u
            || ((target.navigable || target.scrollable) && (!target.control.valid() || !target.focusable || target.owner.valid()))
            || (target.focusOnCommit && (!target.focusable || target.owner.valid()))
        )
            return false;
        if(!target.owner.valid()){
            if(target.ownerDeclarationGeneration != 0u)
                return false;
            continue;
        }
        if(
            target.owner == target.id || target.ownerDeclarationGeneration == 0u || !target.control.valid()
            || target.focusable || target.navigable || target.scrollable || target.focusOnCommit
        )
            return false;
        usize begin = 0u;
        usize end = m_stagedLookup.size();
        while(begin < end){
            const usize middle = begin + (end - begin) / 2u;
            if(m_stagedLookup[middle].value < target.owner.value)
                begin = middle + 1u;
            else
                end = middle;
        }
        if(begin == m_stagedLookup.size() || m_stagedLookup[begin].value != target.owner.value)
            return false;
        const auto& host = m_stagedTargets[m_stagedLookup[begin].index];
        if(
            !host.enabled || !host.focusable || host.owner.valid()
            || host.declarationGeneration != target.ownerDeclarationGeneration
            || host.control != target.control || host.popup != target.popup || host.layer != target.layer
        )
            return false;
    }
    return true;
}

const HitTarget* InputRouter::controlHost(const HitTarget& target)const{
    const HitTarget* host = target.owner.valid() ? findTarget(target.owner, target.ownerDeclarationGeneration) : &target;
    if(
        host == nullptr || !host->enabled || !host->focusable || host->owner.valid() || !host->control.valid()
        || host->control != target.control || host->popup != target.popup || !allowedByPopup(*host)
        || Min(host->rectangle.x + host->rectangle.width, host->clip.x + host->clip.width) <= Max(host->rectangle.x, host->clip.x)
        || Min(host->rectangle.y + host->rectangle.height, host->clip.y + host->clip.height) <= Max(host->rectangle.y, host->clip.y)
    )
        return nullptr;
    return host;
}

bool InputRouter::currentControlAction(const ControlAction& action)const{
    const HitTarget* host = findTarget(action.id.target, action.id.declarationGeneration);
    if(
        host == nullptr || !isInteractive(*host) || !host->focusable || host->owner.valid()
        || !action.control.valid() || host->control != action.control || host->popup != action.popup
    )
        return false;
    if(action.kind == ControlActionKind::Wheel)
        return host->scrollable;
    if(action.kind != ControlActionKind::Activate){
        if(!host->navigable)
            return false;
        if(action.source == host->id)
            return true;
        const HitTarget* source = findTarget(action.source, action.sourceDeclarationGeneration);
        return
            source != nullptr && isInteractive(*source) && source->control == action.sourceControl
            && keyboardHost(*source) == host
        ;
    }
    const HitTarget* source = findTarget(action.source);
    return
        source != nullptr && isInteractive(*source) && source->activatable
        && source->owner == host->id && source->ownerDeclarationGeneration == host->declarationGeneration
        && source->control == action.control && source->popup == action.popup && source->value == action.value
    ;
}

bool InputRouter::consumeControlAction(
    const WidgetId host, const u64 declarationGeneration, const ControlToken& token, ControlAction& action){
    const HitTarget* target = findTarget(host, declarationGeneration);
    if(
        declarationGeneration == 0u || !token.valid() || target == nullptr || !isInteractive(*target)
        || target->control != token || target->owner.valid() || !target->focusable
    )
        return false;
    for(usize index = 0u; index < m_controlActions.size(); ++index){
        const auto& candidate = m_controlActions[index];
        if(
            candidate.id.target != host || candidate.id.declarationGeneration != declarationGeneration
            || candidate.control != token || !currentControlAction(candidate)
        )
            continue;
        action = candidate;
        m_controlActions.erase(m_controlActions.begin() + static_cast<isize>(index));
        return true;
    }
    return false;
}

void InputRouter::fenceControl(const WidgetId host, const u64 declarationGeneration, const ControlToken& token){
    const HitTarget* accepted = findTarget(host);
    if(accepted == nullptr || (accepted->declarationGeneration == declarationGeneration && accepted->control == token))
        return;
    for(auto& target : m_targets){
        if(target.id == host || target.owner == host)
            target.enabled = false;
    }
    reconcileTargets();
}

void InputRouter::reconcileControlActions(){
    for(usize index = 0u; index < m_controlActions.size();){
        if(!currentControlAction(m_controlActions[index]))
            m_controlActions.erase(m_controlActions.begin() + static_cast<isize>(index));
        else
            ++index;
    }
    // Losing an initial-down owner is terminal for that held key even if its token later returns.
    for(auto& owner : m_controlKeyOwners){
        if(!currentControlKeyOwner(owner))
            owner = {};
    }
}

void InputRouter::appendControlAction(
    const HitTarget& host, const HitTarget& source, const ControlActionKind::Enum kind, const f64 delta,
    InputRoutingResult& result){
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
    m_controlActions.push_back(action);
    ++m_nextActionSequence;
}

void InputRouter::routeWheel(const InputEvent& event, InputRoutingResult& result){
    m_pointer = event.position;
    m_pointerKnown = true;
    updateHover();
    const HitTarget* hit = findTarget(m_hover);
    result.pointerConsumed |= wantsPointer();
    if(
        (m_primaryDown && !m_pointerSequenceConsumed) || (m_secondaryDown && !m_secondarySequenceConsumed)
        || hit == nullptr || event.scrollY == 0.0
    )
        return;
    const HitTarget* host = controlHost(*hit);
    if(host != nullptr && host->scrollable)
        appendControlAction(*host, *hit, ControlActionKind::Wheel, event.scrollY, result);
}

bool InputRouter::routeControlKey(
    const InputEvent& event, const HitTarget& host, const HitTarget& source, const bool alreadyPressed,
    InputRoutingResult& result){
    ControlActionKind::Enum kind;
    switch(event.key){
    case InputKey::Up: kind = ControlActionKind::Up; break;
    case InputKey::Down: kind = ControlActionKind::Down; break;
    case InputKey::PageUp: kind = ControlActionKind::PageUp; break;
    case InputKey::PageDown: kind = ControlActionKind::PageDown; break;
    case InputKey::Home: kind = ControlActionKind::Home; break;
    case InputKey::End: kind = ControlActionKind::End; break;
    case InputKey::Enter:
    case InputKey::Space: kind = ControlActionKind::Submit; break;
    default: return false;
    }
    auto& owner = m_controlKeyOwners[static_cast<usize>(event.key) - 1u];
    if(!alreadyPressed && !event.repeat)
        owner = { host.id, host.declarationGeneration, host.popup, host.control,
            source.id, source.declarationGeneration, source.control };
    if(
        owner.host == host.id && owner.declarationGeneration == host.declarationGeneration
        && owner.popup == host.popup && owner.control == host.control
        && owner.source == source.id && owner.sourceDeclarationGeneration == source.declarationGeneration
        && owner.sourceControl == source.control
        && (kind != ControlActionKind::Submit || (!alreadyPressed && !event.repeat))
    )
        appendControlAction(host, source, kind, 0.0, result);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

