// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/input/input.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline const NWB::Impl::Ui::HitTarget* FindHitTarget(const NWB::Impl::Ui::InputVector<NWB::Impl::Ui::HitTarget>& targets, const NWB::Impl::Ui::WidgetId widget)noexcept{
    for(const auto& target : targets){
        if(target.id == widget)
            return &target;
    }
    return nullptr;
}

// Existing fixtures exercise the native cursor adapter; neutral providers use queuePointer directly.
[[nodiscard]] inline NWB::Impl::Ui::InputEvent NativeInput(const NWB::Impl::Ui::InputEvent& event)noexcept{
    using namespace NWB::Impl::Ui;
    InputEvent resolved = event;
    switch(event.type){
    case InputEventType::PointerMove:
    case InputEventType::PrimaryDown:
    case InputEventType::PrimaryUp:
    case InputEventType::SecondaryDown:
    case InputEventType::SecondaryUp:
    case InputEventType::PointerWheel:
    case InputEventType::PointerLeave:
    case InputEventType::PointerCaptureLost:
        resolved.source = { 0u, 1u };
        resolved.hoverCapable = true;
        resolved.extend = event.shift;
        break;
    default:
        break;
    }
    return resolved;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

