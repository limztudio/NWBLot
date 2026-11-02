// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InputRouter::cancelDeviceInput(const u64 device)noexcept{
    if(m_activePointerSource.valid() && m_activePointerSource.device == device)
        terminatePointerSource(m_activePointerSource);
    for(usize index = m_pointerSources.size(); index > 0u; --index){
        if(m_pointerSources[index - 1u].source.device == device)
            m_pointerSources.erase(m_pointerSources.begin() + static_cast<isize>(index - 1u));
    }
    for(usize index = m_pointerBindings.size(); index > 0u; --index){
        if(m_pointerBindings[index - 1u].source.device == device)
            m_pointerBindings.erase(m_pointerBindings.begin() + static_cast<isize>(index - 1u));
    }
    if(m_hoverPointerSource.valid() && m_hoverPointerSource.device == device){
        advanceHoverActivity();
        clearHover();
        m_pointerKnown = false;
        m_hoverPointerSource = {};
    }
    for(usize index = m_commandSources.size(); index > 0u; --index){
        if(m_commandSources[index - 1u].source.device == device)
            m_commandSources.erase(m_commandSources.begin() + static_cast<isize>(index - 1u));
    }
    for(usize index = m_boundSources.size(); index > 0u; --index){
        if(m_boundSources[index - 1u].source.device == device)
            m_boundSources.erase(m_boundSources.begin() + static_cast<isize>(index - 1u));
    }
    for(usize index = m_events.size(); index > 0u; --index){
        const InputEvent& event = m_events[index - 1u];
        if(event.source.valid() && event.source.device == device)
            m_events.erase(m_events.begin() + static_cast<isize>(index - 1u));
    }
}

b8 InputRouter::canEditCommand(const InputEvent& event)const noexcept{
    if(event.type != InputEventType::CommandDown || !event.edit)
        return false;
    const CommandSource* source = findCommandSource(event.source);
    if(source == nullptr || !currentCommandFocus(*source))
        return false;
    const HitTarget* focused = findTarget(source->focusOwner.target, source->focusOwner.declarationGeneration);
    if(!focused->textEditable)
        return false;
    return !source->delegated || ((event.command == InputCommand::Accept || event.command == InputCommand::Submit) && currentControlCommandOwner(*source));
}

b8 InputRouter::canSubmitCommand(const InputEvent& event)const noexcept{
    if(event.type != InputEventType::CommandUp || !event.edit || (event.command != InputCommand::Accept && event.command != InputCommand::Submit))
        return false;
    const CommandSource* source = findCommandSource(event.source);
    if(source == nullptr || !source->submitArmed || !currentCommandFocus(*source))
        return false;
    const HitTarget& focused = *MakeNotNull(findTarget(source->focusOwner.target, source->focusOwner.declarationGeneration));
    return focused.textEditable && (!source->delegated || currentControlCommandOwner(*source));
}

void InputRouter::cancelSubmitCommands(const WidgetId id)noexcept{
    for(CommandSource& source : m_commandSources){
        if(source.focusOwner.target == id)
            source.submitArmed = false;
    }
}

b8 InputRouter::pressed(const WidgetId id)const noexcept{
    if(m_primaryDown && m_pointerSubmitArmed && m_capture == id && hitTest(m_interactionPosition) == id)
        return true;
    for(const CommandSource& source : m_commandSources){
        if(source.submitArmed && source.focusOwner.target == id && currentCommandFocus(source))
            return true;
    }
    return false;
}

Expected<InputEvent> InputRouter::resolveSourceEvent(const InputEvent& event){
    InputEvent resolved = event;
    const b8 physical = event.type == InputEventType::KeyDown || event.type == InputEventType::KeyUp;
    const b8 command = event.type == InputEventType::CommandDown || event.type == InputEventType::CommandUp;
    if(!physical && !command)
        return resolved;
    if(physical){
        if(!InputBindings::ValidKey(event.key))
            return MakeUnexpected(Failure{});
        resolved.type = event.type == InputEventType::KeyDown ? InputEventType::CommandDown : InputEventType::CommandUp;
        resolved.source = { 0u, static_cast<u64>(event.key) + 1u };
    }
    else if(!event.source.valid() || event.command > InputCommand::ContextMenu)
        return MakeUnexpected(Failure{});
    auto held = FindIf(m_boundSources.begin(), m_boundSources.end(), [&resolved](const BoundSource& source)noexcept{
        return source.source == resolved.source;
    });
    if(held != m_boundSources.end()){
        resolved.command = held->intent.command;
        resolved.extend = held->intent.extend;
        resolved.edit = held->intent.edit;
        resolved.allowText = held->intent.allowText;
        if(resolved.type == InputEventType::CommandUp)
            m_boundSources.erase(held);
        else
            resolved.repeat = true;
    }
    else if(resolved.type == InputEventType::CommandDown){
        if(m_boundSources.size() == s_InputMaxSources)
            return MakeUnexpected(Failure{});
        if(physical){
            const i32 modifiers = (event.shift ? Core::InputModifier::Shift : 0) | (event.control ? Core::InputModifier::Control : 0) | (event.alt ? Core::InputModifier::Alt : 0) | (event.super ? Core::InputModifier::Super : 0);
            const InputCommandIntent intent = m_bindings.resolve(event.key, modifiers);
            resolved.command = intent.command;
            resolved.extend = intent.extend;
            resolved.edit = intent.edit;
            resolved.allowText = intent.allowText;
        }
        m_boundSources.push_back({ resolved.source, { resolved.command, resolved.extend, resolved.edit, resolved.allowText } });
    }
    else{
        resolved.command = InputCommand::None;
        resolved.extend = false;
        resolved.edit = false;
        resolved.allowText = false;
    }
    if(resolved.type == InputEventType::CommandUp)
        resolved.repeat = false;
    return resolved;
}

void InputRouter::releaseCommand(const InputEvent& event, const CommandSource& source, InputRoutingResult& result){
    if(!currentCommandFocus(source))
        return;
    if(source.contextArmed && event.command == InputCommand::ContextMenu && currentContextMenuOwner(source.focusOwner)){
        const HitTarget& target = *MakeNotNull(findTarget(source.focusOwner.target, source.focusOwner.declarationGeneration));
        appendContextMenu(target, source.contextPosition, InputActionSource::Command, event.source, result);
        return;
    }
    if(!source.submitArmed || (event.command != InputCommand::Accept && event.command != InputCommand::Submit && event.command != InputCommand::Activate))
        return;
    const HitTarget& focused = *MakeNotNull(findTarget(source.focusOwner.target, source.focusOwner.declarationGeneration));
    if(source.controlOwner.host.valid()){
        if(currentControlCommandOwner(source)){
            const HitTarget& host = *MakeNotNull(findTarget(source.controlOwner.host, source.controlOwner.declarationGeneration));
            appendControlAction(host, focused, ControlActionKind::Submit, 0.0, result);
        }
    }
    else if(focused.activatable)
        appendActivation(focused, InputActionSource::Command, event.source, result);
}

void InputRouter::cancelCommandPresses()noexcept{
    for(CommandSource& source : m_commandSources){
        source.submitArmed = false;
        source.contextArmed = false;
    }
}

void InputRouter::retireCommandFocus()noexcept{
    for(CommandSource& source : m_commandSources){
        source.submitArmed = false;
        source.contextArmed = false;
        source.focusOwner = {};
        source.controlOwner = {};
    }
}

void InputRouter::reconcileCommandPresses()noexcept{
    for(CommandSource& source : m_commandSources){
        if(!currentCommandFocus(source)){
            source.submitArmed = false;
            source.contextArmed = false;
        }
    }
}

void InputRouter::routeCommand(const InputEvent& event, InputRoutingResult& result){
    CommandSource* source = findCommandSource(event.source);
    if(event.type == InputEventType::CommandUp){
        if(source){
            result.commandConsumed |= source->consumed;
            releaseCommand(event, *source, result);
            m_commandSources.erase(m_commandSources.begin() + (source - m_commandSources.data()));
        }
        return;
    }
    const b8 alreadyPressed = source != nullptr;
    if(!source){
        if(m_commandSources.size() == s_InputMaxSources){
            result.activationOverflow = true;
            return;
        }
        CommandSource admitted;
        admitted.source = event.source;
        m_commandSources.push_back(admitted);
        source = &m_commandSources.back();
        const HitTarget* focused = findTarget(m_focus, m_focusDeclaration);
        if(m_windowFocused && !event.repeat && focused && isInteractive(*focused))
            source->focusOwner = { focused->id, focused->declarationGeneration, focused->popup, focused->control };
    }
    // Blurred and orphan repeat sequences remain inert until released and pressed again.
    result.commandConsumed |= source->consumed;
    if(!m_windowFocused || (!alreadyPressed && event.repeat))
        return;
    b8 consumed = source->consumed;
    if(alreadyPressed && !consumed)
        return;
    const HitTarget* focused = findTarget(m_focus, m_focusDeclaration);
    if(routeContextMenuCommand(event, focused, alreadyPressed, *source))
        consumed = true;
    else if(event.command == InputCommand::FocusNext || event.command == InputCommand::FocusPrevious)
        consumed |= moveFocusForTraversal(event.command == InputCommand::FocusPrevious) || hasPopup();
    else if(event.command == InputCommand::Cancel){
        consumed |= wantsCommands() || m_capture.valid() || m_pointerSequenceConsumed || m_secondarySequenceConsumed || (!m_pointerSources.empty() && wantsPointer());
        if(!event.repeat && !alreadyPressed){
            cancelCommandPresses();
            cancelPointerPress();
        }
        if(hasPopup() && !(focused && focused->textEditable)){
            const b8 dismissed = !event.repeat && !alreadyPressed && dismissPopup(PopupDismissReason::Cancel);
            if(dismissed)
                updateHover();
            consumed = true;
        }
        else if(!event.repeat && !alreadyPressed && !(focused && focused->textEditable)){
            clearFocus();
        }
    }
    else{
        consumed |= hasPopup() || focused != nullptr;
        const b8 delegatedCommand = event.command == InputCommand::Up || event.command == InputCommand::Down
            || event.command == InputCommand::PageUp || event.command == InputCommand::PageDown
            || event.command == InputCommand::Accept || event.command == InputCommand::Submit;
        const HitTarget* commandOwner = focused != nullptr && delegatedCommand ? commandHost(*focused) : nullptr;
        const HitTarget* control = commandOwner != nullptr ? commandOwner : focused;
        const b8 submitCommand = event.command == InputCommand::Accept || event.command == InputCommand::Submit || event.command == InputCommand::Activate;
        b8 controlHandled = false;
        if(control != nullptr && focused != nullptr && control->navigable){
            if(submitCommand){
                controlHandled = true;
                if(!event.repeat && !alreadyPressed)
                    source->controlOwner = { control->id, control->declarationGeneration, control->popup, control->control, false };
            }
            else
                controlHandled = routeControlNavigation(event, *control, *focused, alreadyPressed, *source, result);
        }
        source->delegated |= controlHandled && commandOwner != nullptr;
        if(submitCommand && focused != nullptr && !event.repeat && !alreadyPressed && (controlHandled || focused->activatable || focused->textEditable))
            source->submitArmed = true;
    }
    if(consumed){
        source->consumed = true;
        result.commandConsumed = true;
    }
}

void InputRouter::assignFocus(const HitTarget& target)noexcept{
    if(m_focus != target.id || m_focusDeclaration != target.declarationGeneration || m_focusControl != target.control)
        retireCommandFocus();
    m_focus = target.id;
    m_focusDeclaration = target.declarationGeneration;
    m_focusControl = target.control;
}

b8 InputRouter::moveFocus(const b8 reverse)noexcept{
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
            assignFocus(target);
            return true;
        }
    }
    return false;
}

b8 InputRouter::moveFocusForTraversal(const b8 reverse){
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
            assignFocus(target);
            return true;
        }
    }
    else{
        for(usize index = current == m_targets.size() ? 0u : current + 1u; index < m_targets.size(); ++index){
            const HitTarget& target = m_targets[index];
            if(target.popup != token || !target.focusable || !isInteractive(target))
                continue;
            assignFocus(target);
            return true;
        }
    }
    const PopupToken parent = top.scope.parent;
    usize anchor = m_targets.size();
    for(usize index = 0u; index < m_targets.size(); ++index){
        const HitTarget& target = m_targets[index];
        if(target.id == top.scope.focusAnchor && target.declarationGeneration == top.scope.focusAnchorDeclarationGeneration && target.popup == parent){
            anchor = index;
            break;
        }
    }
    WidgetId nextFocus;
    u64 nextDeclaration = 0u;
    for(usize offset = 1u; offset <= m_targets.size(); ++offset){
        const usize index = anchor == m_targets.size() ? (reverse ? m_targets.size() - offset : offset - 1u) : (reverse ? (anchor + m_targets.size() - offset) % m_targets.size() : (anchor + offset) % m_targets.size());
        const HitTarget& target = m_targets[index];
        if(target.popup != parent || !target.enabled || !target.focusable || target.owner.valid() || !HasVisibleBounds(target))
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

