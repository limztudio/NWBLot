// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::setBindings(const InputKeyBinding* bindings, const usize count){
    return m_bindings.set(bindings, count);
}

void InputRouter::restoreDefaultBindings(){
    m_bindings.restoreDefaults();
}

bool InputRouter::wantsKeyboard()const noexcept{
    return hasPopup() || m_focus.valid() || FindIf(m_commandSources.begin(), m_commandSources.end(), [](const CommandSource& source)noexcept{
        return source.consumed;
    }) != m_commandSources.end();
}

bool InputRouter::ownsKey(const i32 key)const noexcept{
    return InputBindings::ValidKey(key) && ownsSource({ 0u, static_cast<u64>(key) + 1u });
}

bool InputRouter::ownsSource(const InputSource& source)const noexcept{
    const CommandSource* held = findCommandSource(source);
    return held && held->consumed;
}

bool InputRouter::canEditCommand(const InputEvent& event)const{
    if(event.type != InputEventType::CommandDown || !event.edit)
        return false;
    const CommandSource* source = findCommandSource(event.source);
    const HitTarget* focused = findTarget(m_focus, m_focusDeclaration);
    if(
        !source || !source->consumed || !focused || !focused->textEditable || !isInteractive(*focused)
        || source->focusOwner.target != focused->id || source->focusOwner.declarationGeneration != focused->declarationGeneration
        || source->focusOwner.popup != focused->popup || source->focusOwner.control != focused->control
    )
        return false;
    return !source->delegated || ((event.command == InputCommand::Accept || event.command == InputCommand::Submit)
        && currentControlKeyOwner(source->controlOwner));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::resolveSourceEvent(const InputEvent& event, InputEvent& resolved){
    resolved = event;
    const bool physical = event.type == InputEventType::KeyDown || event.type == InputEventType::KeyUp;
    const bool command = event.type == InputEventType::CommandDown || event.type == InputEventType::CommandUp;
    if(!physical && !command){
        if(event.type == InputEventType::FocusLost)
            m_boundSources.clear();
        return true;
    }
    if(physical){
        if(!InputBindings::ValidKey(event.key))
            return false;
        resolved.type = event.type == InputEventType::KeyDown ? InputEventType::CommandDown : InputEventType::CommandUp;
        resolved.source = { 0u, static_cast<u64>(event.key) + 1u };
    }
    else if(!event.source.valid() || event.source.device == 0u || event.command > InputCommand::ContextMenu)
        return false;
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
            return false;
        if(physical){
            const i32 modifiers = (event.shift ? Core::InputModifier::Shift : 0)
                | (event.control ? Core::InputModifier::Control : 0) | (event.alt ? Core::InputModifier::Alt : 0)
                | (event.super ? Core::InputModifier::Super : 0);
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
    return true;
}

InputRouter::CommandSource* InputRouter::findCommandSource(const InputSource& source)noexcept{
    const auto found = FindIf(m_commandSources.begin(), m_commandSources.end(), [&source](const CommandSource& current)noexcept{
        return current.source == source;
    });
    return found == m_commandSources.end() ? nullptr : &*found;
}

const InputRouter::CommandSource* InputRouter::findCommandSource(const InputSource& source)const noexcept{
    const auto found = FindIf(m_commandSources.begin(), m_commandSources.end(), [&source](const CommandSource& current)noexcept{
        return current.source == source;
    });
    return found == m_commandSources.end() ? nullptr : &*found;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

