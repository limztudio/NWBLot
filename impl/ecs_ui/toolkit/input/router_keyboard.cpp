// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InputRouter::routeCommand(const InputEvent& event, InputRoutingResult& result){
    CommandSource* source = findCommandSource(event.source);
    if(event.type == InputEventType::CommandUp){
        if(source){
            result.keyboardConsumed |= source->consumed;
            m_commandSources.erase(m_commandSources.begin() + (source - m_commandSources.data()));
        }
        return;
    }
    const bool alreadyPressed = source != nullptr;
    if(!source){
        if(m_commandSources.size() == s_InputMaxSources){
            result.activationOverflow = true;
            return;
        }
        m_commandSources.push_back({ event.source });
        source = &m_commandSources.back();
        const HitTarget* focused = findTarget(m_focus, m_focusDeclaration);
        if(m_windowFocused && !event.repeat && focused && focused->textEditable)
            source->focusOwner = { focused->id, focused->declarationGeneration, focused->popup, focused->control };
    }
    // Blurred and orphan repeat sequences remain inert until released and pressed again.
    if(!m_windowFocused || (!alreadyPressed && event.repeat))
        return;
    bool consumed = source->consumed;
    if(alreadyPressed && !consumed)
        return;
    const HitTarget* focused = findTarget(m_focus, m_focusDeclaration);
    if(routeContextMenuKey(event, focused, *source, alreadyPressed, result))
        consumed = true;
    else if(event.command == InputCommand::FocusNext || event.command == InputCommand::FocusPrevious)
        consumed |= moveFocusForTraversal(event.command == InputCommand::FocusPrevious) || hasPopup();
    else if(event.command == InputCommand::Cancel && !(focused && focused->textEditable)){
        if(hasPopup()){
            const bool dismissed = !event.repeat && !alreadyPressed && dismissPopup(PopupDismissReason::Cancel);
            if(dismissed)
                updateHover();
            consumed = true;
        }
        else if(!event.repeat && !alreadyPressed){
            consumed |= m_focus.valid() || m_capture.valid() || m_pointerSequenceConsumed;
            for(usize index = 0u; index < m_pointerGestures.size(); ++index){
                if(m_pointerGestures[index].gesture.id.sequence == m_activeGestureSequence){
                    m_pointerGestures.erase(m_pointerGestures.begin() + static_cast<isize>(index));
                    break;
                }
            }
            m_activeGestureSequence = 0u;
            clearFocus();
            m_capture = {};
            m_capturePopup = {};
            m_captureDeclaration = 0u;
            m_captureControl = {};
        }
    }
    else{
        consumed |= hasPopup() || focused != nullptr;
        const bool delegatedCommand = event.command == InputCommand::Up || event.command == InputCommand::Down
            || event.command == InputCommand::PageUp || event.command == InputCommand::PageDown
            || event.command == InputCommand::Accept || event.command == InputCommand::Submit;
        const HitTarget* keyboardOwner = focused != nullptr && delegatedCommand ? keyboardHost(*focused) : nullptr;
        const HitTarget* control = keyboardOwner != nullptr ? keyboardOwner : focused;
        const bool controlHandled = control != nullptr && focused != nullptr && control->navigable
            && routeControlKey(event, *control, *focused, source->controlOwner, alreadyPressed, result);
        source->delegated |= controlHandled && keyboardOwner != nullptr;
        if(
            !controlHandled && focused != nullptr && focused->activatable && !event.repeat && !alreadyPressed
            && (event.command == InputCommand::Accept || event.command == InputCommand::Submit
                || event.command == InputCommand::Activate)
        )
            appendActivation(*focused, event.source.device == 0u ? InputActionSource::Keyboard : InputActionSource::Command, result);
    }
    if(consumed){
        source->consumed = true;
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

bool InputRouter::moveFocusForTraversal(const bool reverse){
    if(m_popups.empty() || !m_popups.back().scope.dismissFocusTraversal)
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
        if(target.id == top.scope.focusAnchor && target.declarationGeneration == top.scope.focusAnchorDeclarationGeneration
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
    if(!dismissPopup(PopupDismissReason::FocusTraversal))
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

