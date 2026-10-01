// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InputRouter::routeKeyboard(const InputEvent& event, InputRoutingResult& result){
    const u32 keyMask = 1u << (static_cast<u8>(event.key) - 1u);
    if(event.type == InputEventType::KeyUp){
        result.keyboardConsumed |= (m_consumedKeys & keyMask) != 0u;
        m_pressedKeys &= ~keyMask;
        m_consumedKeys &= ~keyMask;
        m_controlKeyOwners[static_cast<usize>(event.key) - 1u] = {};
        if(event.key == InputKey::Menu || event.key == InputKey::F10)
            m_contextMenuKeyOwners[event.key == InputKey::Menu ? 0u : 1u] = {};
        return;
    }
    const bool alreadyPressed = (m_pressedKeys & keyMask) != 0u;
    m_pressedKeys |= keyMask;
    bool consumed = (m_consumedKeys & keyMask) != 0u;
    if(alreadyPressed && !consumed && (event.key == InputKey::Menu || event.key == InputKey::F10))
        return;
    const HitTarget* focused = findTarget(m_focus, m_focusDeclaration);
    if(routeContextMenuKey(event, focused, alreadyPressed, result))
        consumed = true;
    else if(event.key == InputKey::Tab)
        consumed |= moveFocusOnTab(event.shift) || hasPopup();
    else if(event.key == InputKey::Escape && !(focused && focused->textEditable)){
        if(hasPopup()){
            const bool dismissed = !event.repeat && !alreadyPressed && dismissPopup(PopupDismissReason::Escape);
            if(dismissed)
                updateHover();
            consumed = true;
        }
        else{
            consumed |= m_focus.valid() || m_capture.valid() || m_pointerSequenceConsumed;
            for(usize index = 0u; index < m_pointerGestures.size(); ++index){
                if(m_pointerGestures[index].gesture.id.sequence == m_activeGestureSequence){
                    m_pointerGestures.erase(m_pointerGestures.begin() + static_cast<isize>(index));
                    break;
                }
            }
            m_activeGestureSequence = 0u;
            m_focus = {};
            m_capture = {};
            m_capturePopup = {};
            m_focusDeclaration = 0u;
            m_captureDeclaration = 0u;
            m_captureControl = {};
            m_focusControl = {};
        }
    }
    else{
        consumed |= hasPopup() || focused != nullptr;
        const bool delegatedKey = event.key == InputKey::Up || event.key == InputKey::Down
            || event.key == InputKey::PageUp || event.key == InputKey::PageDown || event.key == InputKey::Enter;
        const HitTarget* keyboardOwner = focused != nullptr && delegatedKey ? keyboardHost(*focused) : nullptr;
        const HitTarget* control = keyboardOwner != nullptr ? keyboardOwner : focused;
        const bool controlHandled = control != nullptr && focused != nullptr && control->navigable
            && routeControlKey(event, *control, *focused, alreadyPressed, result);
        if(
            !controlHandled && focused != nullptr && focused->activatable && !event.repeat && !alreadyPressed
            && (event.key == InputKey::Enter || event.key == InputKey::Space)
        )
            appendActivation(*focused, InputActionSource::Keyboard, result);
    }
    if(consumed){
        m_consumedKeys |= keyMask;
        result.keyboardConsumed = true;
    }
}

bool InputRouter::moveFocus(const bool reverse){
    if(m_targets.empty())
        return false;
    usize current = m_targets.size();
    for(usize index = 0u; index < m_targets.size(); ++index){
        if(m_targets[index].id == m_focus){
            current = index;
            break;
        }
    }
    usize candidate = current;
    for(usize visited = 0u; visited < m_targets.size(); ++visited){
        if(reverse)
            candidate = candidate == 0u || candidate == m_targets.size() ? m_targets.size() - 1u : candidate - 1u;
        else
            candidate = candidate >= m_targets.size() - 1u ? 0u : candidate + 1u;
        const HitTarget& target = m_targets[candidate];
        if(target.focusable && isInteractive(target)){
            m_focus = target.id;
            m_focusDeclaration = target.declarationGeneration;
            m_focusControl = target.control;
            return true;
        }
    }
    return false;
}

bool InputRouter::moveFocusOnTab(const bool reverse){
    if(m_popups.empty() || !m_popups.back().scope.dismissTab)
        return moveFocus(reverse);
    const PopupRecord& top = m_popups.back();
    if(top.closing)
        return true;
    usize current = m_targets.size();
    for(usize index = 0u; index < m_targets.size(); ++index){
        if(m_targets[index].id == m_focus && m_targets[index].declarationGeneration == m_focusDeclaration){
            current = index;
            break;
        }
    }
    const PopupToken token = top.scope.token;
    if(reverse){
        for(usize index = current; index > 0u; --index){
            const HitTarget& target = m_targets[index - 1u];
            if(target.popup != token || !target.focusable || !isInteractive(target))
                continue;
            m_focus = target.id;
            m_focusDeclaration = target.declarationGeneration;
            m_focusControl = target.control;
            return true;
        }
    }
    else{
        for(usize index = current == m_targets.size() ? 0u : current + 1u; index < m_targets.size(); ++index){
            const HitTarget& target = m_targets[index];
            if(target.popup != token || !target.focusable || !isInteractive(target))
                continue;
            m_focus = target.id;
            m_focusDeclaration = target.declarationGeneration;
            m_focusControl = target.control;
            return true;
        }
    }
    const PopupToken parent = top.scope.parent;
    usize anchor = m_targets.size();
    for(usize index = 0u; index < m_targets.size(); ++index){
        const HitTarget& target = m_targets[index];
        if(target.id == top.scope.tabAnchor && target.declarationGeneration == top.scope.tabAnchorDeclarationGeneration
            && target.popup == parent){
            anchor = index;
            break;
        }
    }
    WidgetId nextFocus;
    u64 nextDeclaration = 0u;
    for(usize offset = 1u; offset <= m_targets.size(); ++offset){
        const usize index = anchor == m_targets.size()
            ? (reverse ? m_targets.size() - offset : offset - 1u)
            : (reverse ? (anchor + m_targets.size() - offset) % m_targets.size()
                : (anchor + offset) % m_targets.size());
        const HitTarget& target = m_targets[index];
        if(
            target.popup != parent || !target.enabled || !target.focusable || target.owner.valid()
            || Min(target.rectangle.x + target.rectangle.width, target.clip.x + target.clip.width)
                <= Max(target.rectangle.x, target.clip.x)
            || Min(target.rectangle.y + target.rectangle.height, target.clip.y + target.clip.height)
                <= Max(target.rectangle.y, target.clip.y)
        )
            continue;
        nextFocus = target.id;
        nextDeclaration = target.declarationGeneration;
        break;
    }
    if(!dismissPopup(PopupDismissReason::Tab))
        return true;
    PopupRecord& closing = m_popups.back();
    closing.restoreFocus = nextFocus;
    closing.restoreDeclaration = nextDeclaration;
    closing.restorePopup = parent;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

