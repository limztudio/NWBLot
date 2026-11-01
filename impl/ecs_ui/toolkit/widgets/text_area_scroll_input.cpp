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
    f32 next = 0.0f;
    if(!TryStepScrollbarOffset(offset, delta, step, maximum, next))
        return MakeUnexpected(Failure{});
    return next;
}

[[nodiscard]] static Expected<f32> Drag(const PointerGesture& gesture, const b8 horizontal)noexcept{
    f32 next = 0.0f;
    if(!TryDragScrollbarOffset(gesture, horizontal, next))
        return MakeUnexpected(Failure{});
    return next;
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

