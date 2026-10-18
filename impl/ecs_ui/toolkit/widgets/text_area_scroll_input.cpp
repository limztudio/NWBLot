// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simd/result.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_text_area_scroll_input{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<SIMDVectorDoubleResult> Step(const SIMDVectorDouble offset, const SIMDVectorDouble delta, const SIMDVectorDouble step, const SIMDVectorDouble maximum)noexcept{
    if(!IsFinite(VectorDoubleGetX(offset)) || !IsFinite(VectorDoubleGetX(delta)) || !IsFinite(VectorDoubleGetX(step)) || VectorDoubleGetX(step) < 0.0 || !IsFinite(VectorDoubleGetX(maximum)) || VectorDoubleGetX(maximum) < 0.0)
        return MakeUnexpected(Failure{});
    SIMDVectorDouble next = VectorDoubleSelect(VectorDoubleZero(), VectorDoubleMin(maximum, offset), VectorDoubleGreater(offset, VectorDoubleZero()));
    if(VectorDoubleGetX(step) > 0.0 && VectorDoubleGetX(delta) > 0.0){
        const SIMDVectorDouble distanceInSteps = VectorDoubleDivide(VectorDoubleSubtract(maximum, next), step);
        next = VectorDoubleGetIntX(VectorDoubleGreaterOrEqual(delta, distanceInSteps)) != 0u ? maximum : VectorDoubleAdd(next, VectorDoubleMultiply(delta, step));
    }
    else if(VectorDoubleGetX(step) > 0.0 && VectorDoubleGetX(delta) < 0.0){
        const SIMDVectorDouble distanceInSteps = VectorDoubleDivide(next, step);
        next = VectorDoubleGetIntX(VectorDoubleGreaterOrEqual(VectorDoubleNegate(delta), distanceInSteps)) != 0u ? VectorDoubleZero() : VectorDoubleAdd(next, VectorDoubleMultiply(delta, step));
    }
    if(!IsFinite(VectorDoubleGetX(next)) || VectorDoubleGetX(next) > static_cast<f64>(Limit<f32>::s_Max))
        return MakeUnexpected(Failure{});
    return SIMDVectorDoubleResult{ next };
}

[[nodiscard]] static SIMDMatrixDouble LoadDragValues(const PointerGesture& gesture, const bool horizontal)noexcept{
    return SIMDMatrixDouble{ .v = {
        VectorDoubleSubtract(VectorDoubleSet(horizontal ? gesture.targetRectangle.x : gesture.targetRectangle.y, horizontal ? gesture.position.x : gesture.position.y, 0.0, 0.0), VectorDoubleSet(horizontal ? gesture.referenceRectangle.x : gesture.referenceRectangle.y, horizontal ? gesture.origin.x : gesture.origin.y, 0.0, 0.0)),
        VectorDoubleSubtract(VectorDoubleReplicate(horizontal ? gesture.referenceRectangle.width : gesture.referenceRectangle.height), VectorDoubleReplicate(horizontal ? gesture.targetRectangle.width : gesture.targetRectangle.height)),
        VectorDoubleReplicate(gesture.maximum),
        VectorDoubleZero()
    } };
}

[[nodiscard]] static Expected<SIMDVectorDoubleResult> Drag(const SIMDVectorDouble startDelta, const SIMDVectorDouble travel, const SIMDVectorDouble maximum)noexcept{
    if(!IsFinite(VectorDoubleGetX(travel)) || VectorDoubleGetX(travel) <= 0.0 || !IsFinite(VectorDoubleGetX(startDelta)) || !IsFinite(VectorDoubleGetY(startDelta)) || !IsFinite(VectorDoubleGetX(maximum)) || VectorDoubleGetX(maximum) < 0.0)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble normalized = VectorDoubleDivide(VectorDoubleAdd(VectorDoubleSplatX(startDelta), VectorDoubleSplatY(startDelta)), travel);
    const SIMDVectorDouble clamped = VectorDoubleSelect(VectorDoubleZero(), VectorDoubleMin(VectorDoubleSplatOne(), normalized), VectorDoubleGreater(normalized, VectorDoubleZero()));
    const SIMDVectorDouble next = VectorDoubleMultiply(clamped, maximum);
    if(!IsFinite(VectorDoubleGetX(next)) || VectorDoubleGetX(next) > static_cast<f64>(Limit<f32>::s_Max))
        return MakeUnexpected(Failure{});
    return SIMDVectorDoubleResult{ next };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::applyTextAreaScrollInput(const Item& item, const TextAreaFrame& frame, const ControlToken& token){
    TextAreaState& state = *frame.state;
    const bool enabled = item.editOptions.enabled;
    const auto horizontalIdResult = MakeWidgetId(item.state.id, "scroll.x.thumb");
    if(!horizontalIdResult)
        return false;
    const WidgetId horizontalId = *horizontalIdResult;
    const auto verticalIdResult = MakeWidgetId(item.state.id, "scroll.y.thumb");
    if(!verticalIdResult)
        return false;
    const WidgetId verticalId = *verticalIdResult;
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
            const SIMDMatrixDouble values = __hidden_text_area_scroll_input::LoadDragValues(*horizontal, true);
            const auto next = __hidden_text_area_scroll_input::Drag(values.v[0], values.v[1], values.v[2]);
            if(!next)
                return false;
            scroll.x = static_cast<f32>(VectorDoubleGetX(next->value));
            manual = true;
            horizontal = m_context.takePartPointerGesture(item.state, horizontalId, enabled);
            haveHorizontal = horizontal && horizontal->control == token;
        }
        else if(haveVertical && verticalSequence <= actionSequence){
            const SIMDMatrixDouble values = __hidden_text_area_scroll_input::LoadDragValues(*vertical, false);
            const auto next = __hidden_text_area_scroll_input::Drag(values.v[0], values.v[1], values.v[2]);
            if(!next)
                return false;
            scroll.y = static_cast<f32>(VectorDoubleGetX(next->value));
            manual = true;
            vertical = m_context.takePartPointerGesture(item.state, verticalId, enabled);
            haveVertical = vertical && vertical->control == token;
        }
        else{
            Point next = scroll;
            if(action->kind == ControlActionKind::Wheel){
                const auto x = __hidden_text_area_scroll_input::Step(VectorDoubleReplicate(scroll.x), VectorDoubleReplicate(action->deltaX), VectorDoubleReplicate(action->stepX), VectorDoubleReplicate(action->maximumX));
                if(!x)
                    return false;
                const f32 nextX = static_cast<f32>(VectorDoubleGetX(x->value));
                const auto y = __hidden_text_area_scroll_input::Step(VectorDoubleReplicate(scroll.y), VectorDoubleNegate(VectorDoubleReplicate(action->delta)), VectorDoubleReplicate(action->step), VectorDoubleReplicate(action->maximum));
                if(!y)
                    return false;
                next = { nextX, static_cast<f32>(VectorDoubleGetX(y->value)) };
                manual = true;
            }
            else if(action->kind == ControlActionKind::Activate && action->value >= 1u && action->value <= 4u){
                const ScrollViewportPlacement& accepted = state.scrollbars();
                const bool x = action->value <= 2u;
                const f64 direction = action->value == 1u || action->value == 3u ? -1.0 : 1.0;
                const ScrollbarPlacement& bar = x ? accepted.horizontal : accepted.vertical;
                const auto stepped = __hidden_text_area_scroll_input::Step(VectorDoubleReplicate(x ? scroll.x : scroll.y), VectorDoubleReplicate(direction), VectorDoubleReplicate(bar.viewportExtent), VectorDoubleReplicate(bar.maximum));
                if(!stepped)
                    return false;
                (x ? next.x : next.y) = static_cast<f32>(VectorDoubleGetX(stepped->value));
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

