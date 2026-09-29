// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


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
        consumed |= moveFocus(event.shift) || hasPopup();
    else if(event.key == InputKey::Escape && !(focused && focused->textEditable)){
        if(hasPopup()){
            const bool dismissed = !event.repeat && !alreadyPressed && dismissPopup(PopupDismissReason::Escape);
            if(dismissed)
                updateHover();
            consumed = true;
        }
        else{
            consumed |= m_focus.valid() || m_capture.valid() || m_pointerSequenceConsumed;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

