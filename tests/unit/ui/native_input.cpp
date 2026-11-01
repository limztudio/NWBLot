// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "native_input.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const NWB::Impl::Ui::HitTarget* FindHitTarget(const Span<const NWB::Impl::Ui::HitTarget> targets, const NWB::Impl::Ui::WidgetId widget)noexcept{
    for(const auto& target : targets){
        if(target.id == widget)
            return &target;
    }
    return nullptr;
}

NWB::Impl::Ui::InputEvent NativeInput(const NWB::Impl::Ui::InputEvent& event)noexcept{
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

NWB::Impl::Ui::InputEvent Pointer(const NWB::Impl::Ui::InputEventType::Enum type, const NWB::Impl::Ui::Point& position)noexcept{
    NWB::Impl::Ui::InputEvent event;
    event.type = type;
    event.position = position;
    return event;
}

NWB::Impl::Ui::PointerInput Pointer(const NWB::Impl::Ui::PointerPhase::Enum phase, const NWB::Impl::Ui::InputSource& source, const NWB::Impl::Ui::Point& position, const b8 hoverCapable, const NWB::Impl::Ui::PointerIntent::Enum intent)noexcept{
    NWB::Impl::Ui::PointerInput input;
    input.source = source;
    input.phase = phase;
    input.intent = intent;
    input.position = position;
    input.hoverCapable = hoverCapable;
    return input;
}

NWB::Impl::Ui::InputEvent Key(const NWB::Impl::Ui::InputEventType::Enum type, const NWB::Core::Key::Enum key, const b8 repeat, const b8 shift)noexcept{
    NWB::Impl::Ui::InputEvent event;
    event.type = type;
    event.key = key;
    event.shift = shift;
    event.repeat = repeat;
    return event;
}

NWB::Impl::Ui::InputEvent Key(const NWB::Core::Key::Enum key, const b8 shift, const b8 control, const b8 alt, const b8 repeat)noexcept{
    NWB::Impl::Ui::InputEvent event;
    event.type = NWB::Impl::Ui::InputEventType::KeyDown;
    event.key = key;
    event.shift = shift;
    event.control = control;
    event.alt = alt;
    event.repeat = repeat;
    return event;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::Impl::Ui::InputRoutingResult InputTestFixtureBase::send(const NWB::Impl::Ui::InputEvent& event){
    EXPECT_TRUE(inputRouter().queue(NativeInput(event)));
    return inputRouter().process();
}

NWB::Impl::Ui::InputRoutingResult InputTestFixtureBase::send(const NWB::Impl::Ui::PointerInput& input){
    EXPECT_TRUE(inputRouter().queuePointer(input));
    return inputRouter().process();
}

void InputTestFixtureBase::click(const NWB::Impl::Ui::Point& position){
    EXPECT_TRUE(send(Pointer(NWB::Impl::Ui::InputEventType::PrimaryDown, position)).pointerConsumed);
    EXPECT_TRUE(send(Pointer(NWB::Impl::Ui::InputEventType::PrimaryUp, position)).pointerConsumed);
}

void InputTestFixtureBase::key(const NWB::Core::Key::Enum value, const b8 shift){
    EXPECT_TRUE(send(Key(NWB::Impl::Ui::InputEventType::KeyDown, value, false, shift)).commandConsumed);
    EXPECT_TRUE(send(Key(NWB::Impl::Ui::InputEventType::KeyUp, value, false, shift)).commandConsumed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

