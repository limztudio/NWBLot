// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::currentCommandFocus(const CommandSource& source)const{
    const auto& owner = source.focusOwner;
    const HitTarget* focused = findTarget(owner.target, owner.declarationGeneration);
    return
        m_windowFocused && source.consumed && owner.target.valid() && m_focus == owner.target && m_focusDeclaration == owner.declarationGeneration
        && focused != nullptr && focused->focusable && isInteractive(*focused) && focused->popup == owner.popup && focused->control == owner.control
    ;
}

bool InputRouter::canSubmitCommand(const InputEvent& event)const{
    if(event.type != InputEventType::CommandUp || !event.edit || (event.command != InputCommand::Accept && event.command != InputCommand::Submit))
        return false;
    const CommandSource* source = findCommandSource(event.source);
    if(source == nullptr || !source->submitArmed || !currentCommandFocus(*source))
        return false;
    const HitTarget* focused = findTarget(source->focusOwner.target, source->focusOwner.declarationGeneration);
    return focused->textEditable && (!source->delegated || currentControlKeyOwner(source->controlOwner));
}

void InputRouter::cancelSubmitCommands(const WidgetId id)noexcept{
    for(CommandSource& source : m_commandSources){
        if(source.focusOwner.target == id)
            source.submitArmed = false;
    }
}

bool InputRouter::pressed(const WidgetId id)const{
    if(m_primaryDown && m_pointerSubmitArmed && m_capture == id && m_hover == id)
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
    if(source.contextOwner.target.valid() && event.command == InputCommand::ContextMenu && currentContextMenuOwner(source.contextOwner)){
        const HitTarget* target = findTarget(source.contextOwner.target, source.contextOwner.declarationGeneration);
        appendContextMenu(*target, source.contextPosition, true, result);
        return;
    }
    if(!source.submitArmed || (event.command != InputCommand::Accept && event.command != InputCommand::Submit && event.command != InputCommand::Activate))
        return;
    const HitTarget* focused = findTarget(source.focusOwner.target, source.focusOwner.declarationGeneration);
    if(source.controlOwner.host.valid()){
        if(currentControlKeyOwner(source.controlOwner)){
            const HitTarget* host = findTarget(source.controlOwner.host, source.controlOwner.declarationGeneration);
            appendControlAction(*host, *focused, ControlActionKind::Submit, 0.0, result);
        }
    }
    else if(focused->activatable)
        appendActivation(*focused, event.source.device == 0u ? InputActionSource::Keyboard : InputActionSource::Command, result);
}

void InputRouter::cancelCommandPresses()noexcept{
    for(CommandSource& source : m_commandSources){
        source.submitArmed = false;
        source.contextOwner = {};
    }
}

void InputRouter::retireCommandFocus()noexcept{
    for(CommandSource& source : m_commandSources){
        source.submitArmed = false;
        source.contextOwner = {};
        source.focusOwner = {};
        source.controlOwner = {};
    }
}

void InputRouter::reconcileCommandPresses(){
    for(CommandSource& source : m_commandSources){
        if(!currentCommandFocus(source) || (source.controlOwner.host.valid() && !currentControlKeyOwner(source.controlOwner))){
            source.submitArmed = false;
            source.contextOwner = {};
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

