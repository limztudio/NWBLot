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

bool UiLayerSystem::commandInput(const Ui::InputSource source, const Ui::InputCommand::Enum command, const Ui::InputPhase::Enum phase, const bool extend){
    if(!source.valid() || command > Ui::InputCommand::ContextMenu || (command == Ui::InputCommand::None && phase != Ui::InputPhase::Release) || phase > Ui::InputPhase::Repeat)
        return false;
    synchronizeNativeInput();
    Ui::InputEvent event;
    event.type = phase == Ui::InputPhase::Release ? Ui::InputEventType::CommandUp : Ui::InputEventType::CommandDown;
    event.source = source;
    event.command = command;
    event.extend = extend;
    event.edit = true;
    event.repeat = phase == Ui::InputPhase::Repeat;
    bool consumed = false;
    routeInput(event, &consumed);
    return consumed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

