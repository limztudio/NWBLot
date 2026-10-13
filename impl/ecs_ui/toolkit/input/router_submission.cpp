// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::currentCommandFocus(const CommandSource& source)const noexcept{
    const auto& owner = source.focusOwner;
    const HitTarget* focused = findTarget(owner.target, owner.declarationGeneration);
    return
        m_windowFocused && source.consumed && owner.target.valid() && m_focus == owner.target && m_focusDeclaration == owner.declarationGeneration
        && focused != nullptr && focused->focusable && isInteractive(*focused) && focused->popup == owner.popup && focused->control == owner.control
    ;
}

bool InputRouter::canSubmitCommand(const InputEvent& event)const noexcept{
    if(event.type != InputEventType::CommandUp || !event.edit || (event.command != InputCommand::Accept && event.command != InputCommand::Submit))
        return false;
    const CommandSource* source = findCommandSource(event.source);
    if(source == nullptr || !source->submitArmed || !currentCommandFocus(*source))
        return false;
    const HitTarget* focused = findTarget(source->focusOwner.target, source->focusOwner.declarationGeneration);
    return focused->textEditable && (!source->delegated || currentControlCommandOwner(*source));
}

void InputRouter::cancelSubmitCommands(const WidgetId id)noexcept{
    for(CommandSource& source : m_commandSources){
        if(source.focusOwner.target == id)
            source.submitArmed = false;
    }
}

bool InputRouter::pressed(const WidgetId id)const noexcept{
    if(m_primaryDown && m_pointerSubmitArmed && m_capture == id && hitTest(m_interactionPosition) == id)
        return true;
    for(const CommandSource& source : m_commandSources){
        if(source.submitArmed && source.focusOwner.target == id && currentCommandFocus(source))
            return true;
    }
    return false;
}

void InputRouter::releaseCommand(const InputEvent& event, const CommandSource& source, InputRoutingResult& result){
    if(!currentCommandFocus(source))
        return;
    if(source.contextArmed && event.command == InputCommand::ContextMenu && currentContextMenuOwner(source.focusOwner)){
        const HitTarget* target = findTarget(source.focusOwner.target, source.focusOwner.declarationGeneration);
        appendContextMenu(*target, source.contextPosition, InputActionSource::Command, event.source, result);
        return;
    }
    if(!source.submitArmed || (event.command != InputCommand::Accept && event.command != InputCommand::Submit && event.command != InputCommand::Activate))
        return;
    const HitTarget* focused = findTarget(source.focusOwner.target, source.focusOwner.declarationGeneration);
    if(source.controlOwner.host.valid()){
        if(currentControlCommandOwner(source)){
            const HitTarget* host = findTarget(source.controlOwner.host, source.controlOwner.declarationGeneration);
            appendControlAction(*host, *focused, ControlActionKind::Submit, 0.0, result);
        }
    }
    else if(focused->activatable)
        appendActivation(*focused, InputActionSource::Command, event.source, result);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

