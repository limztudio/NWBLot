// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_text_area_scroll_input{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<f32> Step(const f64 offset, const f64 delta, const f64 step, const f64 maximum)noexcept{
    if(!IsFinite(offset) || !IsFinite(delta) || !IsFinite(step) || step < 0.0 || !IsFinite(maximum) || maximum < 0.0)
        return MakeUnexpected(Failure{});
    f64 next = offset > 0.0 ? Min(maximum, offset) : 0.0;
    if(step > 0.0 && delta > 0.0){
        const f64 distanceInSteps = (maximum - next) / step;
        next = delta >= distanceInSteps ? maximum : next + (delta * step);
    }
    else if(step > 0.0 && delta < 0.0){
        const f64 distanceInSteps = next / step;
        next = (-delta) >= distanceInSteps ? 0.0 : next + (delta * step);
    }
    if(!IsFinite(next) || next > static_cast<f64>(Limit<f32>::s_Max))
        return MakeUnexpected(Failure{});
    return static_cast<f32>(next);
}

[[nodiscard]] static Expected<f32> Drag(const PointerGesture& gesture, const b8 horizontal)noexcept{
    const f64 currentTarget = horizontal ? gesture.targetRectangle.x : gesture.targetRectangle.y;
    const f64 currentPointer = horizontal ? gesture.position.x : gesture.position.y;
    const f64 referenceTarget = horizontal ? gesture.referenceRectangle.x : gesture.referenceRectangle.y;
    const f64 referencePointer = horizontal ? gesture.origin.x : gesture.origin.y;
    const f64 startDelta = currentTarget - referenceTarget;
    const f64 pointerDelta = currentPointer - referencePointer;
    const f64 travel = static_cast<f64>(horizontal ? gesture.referenceRectangle.width : gesture.referenceRectangle.height) - static_cast<f64>(horizontal ? gesture.targetRectangle.width : gesture.targetRectangle.height);
    const f64 maximum = gesture.maximum;
    if(!IsFinite(travel) || travel <= 0.0 || !IsFinite(startDelta) || !IsFinite(pointerDelta) || !IsFinite(maximum) || maximum < 0.0)
        return MakeUnexpected(Failure{});
    const f64 normalized = (startDelta + pointerDelta) / travel;
    const f64 clamped = normalized > 0.0 ? Min(1.0, normalized) : 0.0;
    const f64 next = clamped * maximum;
    if(!IsFinite(next) || next > static_cast<f64>(Limit<f32>::s_Max))
        return MakeUnexpected(Failure{});
    return static_cast<f32>(next);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::applyTextAreaScrollInput(const Item& item, const TextAreaFrame& frame, const ControlToken& token){
    TextAreaState& state = *frame.state;
    const b8 enabled = item.editOptions.enabled;
    WidgetId horizontalId;
    if(!TryMakeWidgetId(item.state.id, s_WidgetPartScrollHorizontalThumb, horizontalId))
        return false;
    WidgetId verticalId;
    if(!TryMakeWidgetId(item.state.id, s_WidgetPartScrollVerticalThumb, verticalId))
        return false;
    auto horizontal = m_context.takePartPointerGesture(item.state, horizontalId, enabled);
    b8 haveHorizontal = horizontal && horizontal->control == token;
    auto vertical = m_context.takePartPointerGesture(item.state, verticalId, enabled);
    b8 haveVertical = vertical && vertical->control == token;
    auto action = m_context.takeControlAction(item.state, enabled, token);
    b8 haveAction = action.has_value();
    Point scroll = state.scroll();
    b8 manual = false;
    while(haveHorizontal || haveVertical || haveAction){
        const u64 horizontalSequence = haveHorizontal ? horizontal->updateSequence : Limit<u64>::s_Max;
        const u64 verticalSequence = haveVertical ? vertical->updateSequence : Limit<u64>::s_Max;
        const u64 actionSequence = haveAction ? action->id.sequence : Limit<u64>::s_Max;
        if(haveHorizontal && horizontalSequence <= verticalSequence && horizontalSequence <= actionSequence){
            const auto next = __hidden_text_area_scroll_input::Drag(*horizontal, true);
            if(!next)
                return false;
            scroll.x = *next;
            manual = true;
            horizontal = m_context.takePartPointerGesture(item.state, horizontalId, enabled);
            haveHorizontal = horizontal && horizontal->control == token;
        }
        else if(haveVertical && verticalSequence <= actionSequence){
            const auto next = __hidden_text_area_scroll_input::Drag(*vertical, false);
            if(!next)
                return false;
            scroll.y = *next;
            manual = true;
            vertical = m_context.takePartPointerGesture(item.state, verticalId, enabled);
            haveVertical = vertical && vertical->control == token;
        }
        else{
            Point next = scroll;
            if(action->kind == ControlActionKind::Wheel){
                const auto x = __hidden_text_area_scroll_input::Step(scroll.x, action->deltaX, action->stepX, action->maximumX);
                if(!x)
                    return false;
                const auto y = __hidden_text_area_scroll_input::Step(scroll.y, -action->delta, action->step, action->maximum);
                if(!y)
                    return false;
                next = { *x, *y };
                manual = true;
            }
            else if(action->kind == ControlActionKind::Activate && action->value >= s_TextAreaScrollHorizontalBefore && action->value <= s_TextAreaScrollVerticalAfter){
                const ScrollViewportPlacement& accepted = state.scrollbars();
                const b8 x = action->value <= s_TextAreaScrollHorizontalAfter;
                const f64 direction = action->value == s_TextAreaScrollHorizontalBefore || action->value == s_TextAreaScrollVerticalBefore ? -1.0 : 1.0;
                const ScrollbarPlacement& bar = x ? accepted.horizontal : accepted.vertical;
                const auto stepped = __hidden_text_area_scroll_input::Step(x ? scroll.x : scroll.y, direction, bar.viewportExtent, bar.maximum);
                if(!stepped)
                    return false;
                (x ? next.x : next.y) = *stepped;
                manual = true;
            }
            scroll = next;
            action = m_context.takeControlAction(item.state, enabled, token);
            haveAction = action.has_value();
        }
    }
    if(manual){
        state.m_visual.scroll = scroll.x;
        state.m_scrollY = scroll.y;
        state.m_revealCaret = false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

