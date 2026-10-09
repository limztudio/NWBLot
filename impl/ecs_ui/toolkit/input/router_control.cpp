// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"
#include "../rect_math.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::validControlTargets()const{
    for(const auto& target : m_stagedTargets){
        if(
            (!target.control.empty() && !target.control.valid())
            || !IsFinite(target.scrollStep) || target.scrollStep < 0.0
            || !IsFinite(target.scrollStepX) || target.scrollStepX < 0.0
            || !IsFinite(target.gestureMaximum) || target.gestureMaximum < 0.0 || target.pageRows == 0u
            || !IsFinite(target.gestureMaximumX) || target.gestureMaximumX < 0.0
            || ((target.navigable || target.scrollable) && (!target.control.valid() || !target.focusable || target.owner.valid()))
            || (target.horizontalNavigation && !target.navigable)
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
            || target.focusable || target.navigable || target.scrollable || target.focusOnCommit || target.horizontalNavigation
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
    )
        return nullptr;
    const SIMDVector visibleBounds = IntersectRectBoundsValue(
        VectorSet(host->rectangle.x, host->rectangle.y, host->rectangle.width, host->rectangle.height), VectorSet(host->clip.x, host->clip.y, host->clip.width, host->clip.height)
    );
    if((VectorMoveMask(VectorLessOrEqual(VectorSwizzle<2, 3, 2, 3>(visibleBounds), VectorSwizzle<0, 1, 0, 1>(visibleBounds))) & VectorComponentMask::s_XY) != 0u)
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
        if(
            !host->navigable
            || ((action.kind == ControlActionKind::Left || action.kind == ControlActionKind::Right) && !host->horizontalNavigation)
        )
            return false;
        if(action.source == host->id)
            return true;
        const HitTarget* source = findTarget(action.source, action.sourceDeclarationGeneration);
        return
            source != nullptr && isInteractive(*source) && source->control == action.sourceControl
            && keyboardHost(*source) == host
        ;
    }
    const HitTarget* source = findTarget(action.source, action.sourceDeclarationGeneration);
    return
        source != nullptr && isInteractive(*source) && source->activatable
        && source->owner == host->id && source->ownerDeclarationGeneration == host->declarationGeneration
        && source->control == action.control && source->popup == action.popup && source->value == action.value
    ;
}

Expected<ControlAction> InputRouter::consumeControlAction(
    const WidgetId host, const u64 declarationGeneration, const ControlToken& token
){
    const HitTarget* target = findTarget(host, declarationGeneration);
    if(
        declarationGeneration == 0u || !token.valid() || target == nullptr || !isInteractive(*target)
        || target->control != token || target->owner.valid() || !target->focusable
    )
        return MakeUnexpected(Failure{});
    for(usize index = 0u; index < m_controlActions.size(); ++index){
        const auto& candidate = m_controlActions[index];
        if(
            candidate.id.target != host || candidate.id.declarationGeneration != declarationGeneration
            || candidate.control != token || !currentControlAction(candidate)
        )
            continue;
        const ControlAction action = candidate;
        m_controlActions.erase(m_controlActions.begin() + static_cast<isize>(index));
        return action;
    }
    return MakeUnexpected(Failure{});
}

void InputRouter::fenceControl(const WidgetId host, const u64 declarationGeneration, const ControlToken& token){
    const HitTarget* accepted = findTarget(host);
    if(accepted == nullptr || (accepted->declarationGeneration == declarationGeneration && accepted->control == token))
        return;
    const bool preserveText = accepted->declarationGeneration == declarationGeneration
        && accepted->textEditable && !accepted->owner.valid() && (accepted->scrollable || accepted->control.empty());
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

void InputRouter::reconcileControlActions(){
    for(usize index = 0u; index < m_controlActions.size();){
        if(!currentControlAction(m_controlActions[index]))
            m_controlActions.erase(m_controlActions.begin() + static_cast<isize>(index));
        else
            ++index;
    }
    // Losing an initial-down owner is terminal for that held key even if its token later returns.
    for(auto& source : m_commandSources){
        if(!currentControlKeyOwner(source.controlOwner))
            source.controlOwner = {};
        const HitTarget* focused = findTarget(source.focusOwner.target, source.focusOwner.declarationGeneration);
        if(
            !focused || !focused->textEditable || !isInteractive(*focused)
            || focused->popup != source.focusOwner.popup || focused->control != source.focusOwner.control
        )
            source.focusOwner = {};
    }
}

void InputRouter::appendControlAction(
    const HitTarget& host, const HitTarget& source, const ControlActionKind::Enum kind, const f64 delta,
    InputRoutingResult& result, const f64 deltaX
){
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

void InputRouter::routeWheel(const InputEvent& event, InputRoutingResult& result){
    m_pointer = event.position;
    m_pointerKnown = true;
    updateHover();
    const HitTarget* hit = findTarget(m_hover);
    result.pointerConsumed |= wantsPointer();
    if(
        (m_primaryDown && !m_pointerSequenceConsumed) || (m_secondaryDown && !m_secondarySequenceConsumed)
        || hit == nullptr || (event.scrollX == 0.0 && event.scrollY == 0.0)
    )
        return;
    const HitTarget* host = controlHost(*hit);
    if(host != nullptr && host->scrollable && (event.scrollY != 0.0 || host->scrollStepX > 0.0))
        appendControlAction(*host, *hit, ControlActionKind::Wheel, event.scrollY, result, event.scrollX);
}

bool InputRouter::routeControlKey(
    const InputEvent& event, const HitTarget& host, const HitTarget& source, ControlKeyOwner& owner, const bool alreadyPressed,
    InputRoutingResult& result
){
    ControlActionKind::Enum kind;
    switch(event.command){
    case InputCommand::Left:
    case InputCommand::WordLeft:
        if(!host.horizontalNavigation)
            return false;
        kind = ControlActionKind::Left;
        break;
    case InputCommand::Right:
    case InputCommand::WordRight:
        if(!host.horizontalNavigation)
            return false;
        kind = ControlActionKind::Right;
        break;
    case InputCommand::Up: kind = ControlActionKind::Up; break;
    case InputCommand::Down: kind = ControlActionKind::Down; break;
    case InputCommand::PageUp: kind = ControlActionKind::PageUp; break;
    case InputCommand::PageDown: kind = ControlActionKind::PageDown; break;
    case InputCommand::DocumentHome:
    case InputCommand::Home: kind = ControlActionKind::Home; break;
    case InputCommand::DocumentEnd:
    case InputCommand::End: kind = ControlActionKind::End; break;
    case InputCommand::Accept:
    case InputCommand::Submit:
    case InputCommand::Activate: kind = ControlActionKind::Submit; break;
    default: return false;
    }
    if(!alreadyPressed && !event.repeat)
        owner = { host.id, host.declarationGeneration, host.popup, host.control,
            source.id, source.declarationGeneration, source.control,
            kind == ControlActionKind::Left || kind == ControlActionKind::Right };
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

