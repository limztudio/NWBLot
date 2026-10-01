// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layer_system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiLayerSystem::setInputBindings(const Ui::InputKeyBinding* bindings, const usize count){
    synchronizeNativeInput();
    return m_context.input().setBindings(bindings, count);
}

void UiLayerSystem::restoreDefaultInputBindings(){
    synchronizeNativeInput();
    m_context.input().restoreDefaultBindings();
}

bool UiLayerSystem::commandInput(const Ui::InputSource source, const Ui::InputCommand::Enum command,
    const i32 phase, const bool extend){
    if(source.device == 0u || source.control == 0u || command > Ui::InputCommand::ContextMenu
        || (command == Ui::InputCommand::None && phase != Core::InputAction::Release)
        || phase < Core::InputAction::Release || phase > Core::InputAction::Repeat)
        return false;
    synchronizeNativeInput();
    Ui::InputEvent event;
    event.type = phase == Core::InputAction::Release ? Ui::InputEventType::CommandUp : Ui::InputEventType::CommandDown;
    event.source = source;
    event.command = command;
    event.extend = extend;
    event.edit = true;
    event.repeat = phase == Core::InputAction::Repeat;
    bool consumed = false;
    routeInput(event, &consumed);
    return consumed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

