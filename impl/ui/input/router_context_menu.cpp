// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::currentContextMenuOwner(const ContextMenuOwner& owner)const{
    const HitTarget* target = findTarget(owner.target, owner.declarationGeneration);
    return
        m_windowFocused && owner.declarationGeneration != 0u && target != nullptr && target->contextMenu
        && isInteractive(*target) && target->popup == owner.popup && target->control == owner.control
    ;
}

bool InputRouter::consumeContextMenu(const WidgetId id, const u64 declarationGeneration, ContextMenuAction& action){
    const HitTarget* target = findTarget(id, declarationGeneration);
    if(declarationGeneration == 0u || target == nullptr || !target->contextMenu || !m_windowFocused || !isInteractive(*target))
        return false;
    for(usize index = 0u; index < m_contextMenuActions.size(); ++index){
        const auto& candidate = m_contextMenuActions[index];
        if(
            candidate.id.target != id || candidate.id.declarationGeneration != declarationGeneration
            || candidate.popup != target->popup || candidate.control != target->control
        )
            continue;
        action = candidate;
        m_contextMenuActions.erase(m_contextMenuActions.begin() + static_cast<isize>(index));
        return true;
    }
    return false;
}

void InputRouter::reconcileContextMenus(){
    for(usize index = 0u; index < m_contextMenuActions.size();){
        const auto& action = m_contextMenuActions[index];
        if(!currentContextMenuOwner({ action.id.target, action.id.declarationGeneration, action.popup, action.control }))
            m_contextMenuActions.erase(m_contextMenuActions.begin() + static_cast<isize>(index));
        else
            ++index;
    }
    if(!currentContextMenuOwner(m_secondaryOwner))
        m_secondaryOwner = {};
    for(auto& owner : m_contextMenuKeyOwners){
        if(!currentContextMenuOwner(owner))
            owner = {};
    }
}

void InputRouter::appendContextMenu(
    const HitTarget& target, const Point& position, const bool keyboard, InputRoutingResult& result){
    if(m_contextMenuActions.size() == s_InputMaxContextMenuActions || m_nextActionSequence == 0u){
        result.activationOverflow = true;
        return;
    }
    m_contextMenuActions.push_back({
        { target.id, target.declarationGeneration, m_layoutGeneration, m_nextActionSequence },
        target.popup, target.control, position, keyboard
    });
    ++m_nextActionSequence;
}

bool InputRouter::routeContextMenuKey(
    const InputEvent& event, const HitTarget* focused, const bool alreadyPressed, InputRoutingResult& result){
    const bool trigger = event.key == InputKey::Menu || (event.key == InputKey::F10 && event.shift && !event.control && !event.alt);
    if(!trigger || focused == nullptr || !focused->contextMenu || !m_windowFocused || !isInteractive(*focused))
        return false;
    if(!alreadyPressed && !event.repeat){
        m_contextMenuKeyOwners[event.key == InputKey::Menu ? 0u : 1u] = {
            focused->id, focused->declarationGeneration, focused->popup, focused->control
        };
        appendContextMenu(*focused, { focused->rectangle.x, focused->rectangle.y + focused->rectangle.height }, true, result);
    }
    return true;
}


void InputRouter::routeSecondary(const InputEvent& event, InputRoutingResult& result){
    m_pointer = event.position;
    m_pointerKnown = true;
    updateHover();
    const HitTarget* hit = findTarget(m_hover);
    if(event.type == InputEventType::SecondaryUp){
        result.pointerConsumed |= m_secondaryDown ? m_secondarySequenceConsumed
            : m_primaryDown ? m_pointerSequenceConsumed : hasPopup() || hit != nullptr;
        m_secondaryDown = false;
        m_secondarySequenceConsumed = false;
        m_secondaryOwner = {};
        return;
    }
    if(m_secondaryDown){
        result.pointerConsumed |= m_secondarySequenceConsumed;
        return;
    }
    m_secondaryDown = true;
    m_secondarySequenceConsumed = m_primaryDown ? m_pointerSequenceConsumed : hasPopup() || hit != nullptr;
    result.pointerConsumed |= m_secondarySequenceConsumed;
    if(!m_secondarySequenceConsumed)
        return;
    if(hasPopup() && hit == nullptr){
        const bool dismissed = dismissPopup(PopupDismissReason::OutsideClick);
        if(dismissed)
            updateHover();
        return;
    }
    const HitTarget* anchor = hit != nullptr && !hit->contextMenu && hit->owner.valid() ? controlHost(*hit) : hit;
    if(anchor == nullptr || !anchor->contextMenu || !m_windowFocused)
        return;
    m_secondaryOwner = { anchor->id, anchor->declarationGeneration, anchor->popup, anchor->control };
    appendContextMenu(*anchor, event.position, false, result);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

