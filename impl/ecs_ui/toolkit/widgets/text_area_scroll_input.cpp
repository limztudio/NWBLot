// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_text_area_scroll_input{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<f32> Step(const f64 offset, const f64 delta, const f64 step, const f64 maximum)noexcept{
    if(!IsFinite(offset) || !IsFinite(delta) || !IsFinite(step) || step < 0.0 || !IsFinite(maximum) || maximum < 0.0)
        return MakeUnexpected(Failure{});
    f64 next = Clamp(offset, 0.0, maximum);
    if(step > 0.0 && delta > 0.0)
        next = delta >= (maximum - next) / step ? maximum : next + delta * step;
    else if(step > 0.0 && delta < 0.0)
        next = -delta >= next / step ? 0.0 : next + delta * step;
    if(!IsFinite(next) || next > static_cast<f64>(Limit<f32>::s_Max))
        return MakeUnexpected(Failure{});
    return static_cast<f32>(next);
}

[[nodiscard]] static bool Drag(const PointerGesture& gesture, const bool horizontal, Point& scroll)noexcept{
    const f64 track = horizontal ? gesture.referenceRectangle.width : gesture.referenceRectangle.height;
    const f64 thumb = horizontal ? gesture.targetRectangle.width : gesture.targetRectangle.height;
    const f64 start = horizontal ? static_cast<f64>(gesture.targetRectangle.x) - gesture.referenceRectangle.x
        : static_cast<f64>(gesture.targetRectangle.y) - gesture.referenceRectangle.y;
    const f64 delta = horizontal ? static_cast<f64>(gesture.position.x) - gesture.origin.x
        : static_cast<f64>(gesture.position.y) - gesture.origin.y;
    const f64 travel = track - thumb;
    if(!IsFinite(travel) || travel <= 0.0 || !IsFinite(start) || !IsFinite(delta) || !IsFinite(gesture.maximum) || gesture.maximum < 0.0)
        return false;
    const f64 next = Clamp((start + delta) / travel, 0.0, 1.0) * gesture.maximum;
    if(!IsFinite(next) || next > static_cast<f64>(Limit<f32>::s_Max))
        return false;
    if(horizontal)
        scroll.x = static_cast<f32>(next);
    else
        scroll.y = static_cast<f32>(next);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::applyTextAreaScrollInput(const Item& item, const TextAreaFrame& frame, const ControlToken& token){
    TextAreaState& state = *frame.state;
    const bool enabled = item.editOptions.enabled;
    const WidgetId horizontalId = MakeWidgetId(item.state.id, "scroll.x.thumb");
    const WidgetId verticalId = MakeWidgetId(item.state.id, "scroll.y.thumb");
    auto horizontal = m_context.takePartPointerGesture(item.state, horizontalId, enabled);
    bool haveHorizontal = horizontal && horizontal->control == token;
    auto vertical = m_context.takePartPointerGesture(item.state, verticalId, enabled);
    bool haveVertical = vertical && vertical->control == token;
    auto action = m_context.takeControlAction(item.state, enabled, token);
    bool haveAction = action.has_value();
    Point scroll = state.scroll();
    bool manual = false;
    while(haveHorizontal || haveVertical || haveAction){
        const u64 horizontalSequence = haveHorizontal ? horizontal->updateSequence : Limit<u64>::s_Max;
        const u64 verticalSequence = haveVertical ? vertical->updateSequence : Limit<u64>::s_Max;
        const u64 actionSequence = haveAction ? action->id.sequence : Limit<u64>::s_Max;
        if(haveHorizontal && horizontalSequence <= verticalSequence && horizontalSequence <= actionSequence){
            if(!__hidden_text_area_scroll_input::Drag(*horizontal, true, scroll))
                return false;
            manual = true;
            horizontal = m_context.takePartPointerGesture(item.state, horizontalId, enabled);
            haveHorizontal = horizontal && horizontal->control == token;
        }
        else if(haveVertical && verticalSequence <= actionSequence){
            if(!__hidden_text_area_scroll_input::Drag(*vertical, false, scroll))
                return false;
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
            else if(action->kind == ControlActionKind::Activate && action->value >= 1u && action->value <= 4u){
                const ScrollViewportPlacement& accepted = state.scrollbars();
                const bool x = action->value <= 2u;
                const f64 direction = action->value == 1u || action->value == 3u ? -1.0 : 1.0;
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

