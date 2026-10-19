// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider.h"

#include <impl/ecs_ui/toolkit/layout/validation.h>

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_behavior{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidRange(const f64 minimum, const f64 maximum)noexcept{
    return IsFinite(minimum) && IsFinite(maximum) && minimum <= maximum;
}

[[nodiscard]] static bool SameRect(const Rect& lhs, const Rect& rhs)noexcept{
    return BitCast<u32>(lhs.x) == BitCast<u32>(rhs.x) && BitCast<u32>(lhs.y) == BitCast<u32>(rhs.y) && BitCast<u32>(lhs.width) == BitCast<u32>(rhs.width) && BitCast<u32>(lhs.height) == BitCast<u32>(rhs.height);
}

[[nodiscard]] static bool ValidPlacement(const SliderPlacement& placement)noexcept{
    return
        IsPreciseUiRect(placement.bounds) && IsPreciseUiRect(placement.clip) && IsPreciseUiRect(placement.travelBounds)
        && IsPreciseUiRect(placement.track) && IsPreciseUiRect(placement.centerTravel) && IsPreciseUiRect(placement.thumb)
        && IsFinite(placement.thumbExtent.x) && placement.thumbExtent.x >= 0.0f
        && IsFinite(placement.thumbExtent.y) && placement.thumbExtent.y >= 0.0f
        && placement.thumbExtent.x == placement.thumb.width && placement.thumbExtent.y == placement.thumb.height
        && placement.thumbExtent.x <= placement.travelBounds.width && placement.thumbExtent.y <= placement.travelBounds.height
        && placement.centerTravel.width <= placement.travelBounds.width
        && placement.centerTravel.height <= placement.travelBounds.height
        && placement.track.width <= placement.travelBounds.width && placement.track.height <= placement.travelBounds.height
    ;
}

[[nodiscard]] static bool SamePlacement(const SliderPlacement& lhs, const SliderPlacement& rhs)noexcept{
    return
        SameRect(lhs.bounds, rhs.bounds) && SameRect(lhs.clip, rhs.clip) && SameRect(lhs.travelBounds, rhs.travelBounds)
        && SameRect(lhs.track, rhs.track) && SameRect(lhs.centerTravel, rhs.centerTravel)
        && BitCast<u32>(lhs.thumbExtent.x) == BitCast<u32>(rhs.thumbExtent.x)
        && BitCast<u32>(lhs.thumbExtent.y) == BitCast<u32>(rhs.thumbExtent.y)
    ;
}

[[nodiscard]] static bool SamePress(const InputActionId& lhs, const InputActionId& rhs)noexcept{
    return lhs.target == rhs.target && lhs.declarationGeneration == rhs.declarationGeneration && lhs.layoutGeneration == rhs.layoutGeneration && lhs.sequence == rhs.sequence;
}

[[nodiscard]] static bool ValidGesture(const PointerGesture& gesture)noexcept{
    return
        gesture.id.valid() && gesture.updateSequence != 0u && gesture.state <= PointerGestureState::Completed
        && IsFinite(gesture.origin.x) && IsFinite(gesture.origin.y)
        && IsFinite(gesture.position.x) && IsFinite(gesture.position.y)
        && IsPreciseUiRect(gesture.targetRectangle) && IsPreciseUiRect(gesture.referenceRectangle)
    ;
}

[[nodiscard]] static SIMDVectorDouble ClampValue(const SIMDVectorDouble value, const SIMDVectorDouble minimum, const SIMDVectorDouble maximum)noexcept{
    return VectorDoubleSelect(VectorDoubleSelect(value, maximum, VectorDoubleGreater(value, maximum)), minimum, VectorDoubleLess(value, minimum));
}

[[nodiscard]] static SIMDVectorDouble KeyStep(const SIMDVectorDouble minimum, const SIMDVectorDouble maximum, const SIMDVectorDouble configuredStep)noexcept{
    if(VectorDoubleGetIntX(VectorDoubleGreater(configuredStep, VectorDoubleZero())) != 0u)
        return configuredStep;
    const SIMDVectorDouble span = VectorDoubleSubtract(maximum, minimum);
    if(IsFinite(VectorDoubleGetX(span))){
        const SIMDVectorDouble step = VectorDoubleDivide(span, VectorDoubleReplicate(100.0));
        return VectorDoubleSelect(step, span, VectorDoubleEqual(step, VectorDoubleZero()));
    }
    return VectorDoubleSubtract(VectorDoubleDivide(maximum, VectorDoubleReplicate(100.0)), VectorDoubleDivide(minimum, VectorDoubleReplicate(100.0)));
}

[[nodiscard]] static SIMDVectorDouble StepValue(const SIMDVectorDouble value, const SIMDVectorDouble minimum, const SIMDVectorDouble maximum, const SIMDVectorDouble step, const bool increasing)noexcept{
    const SIMDVectorDouble current = ClampValue(value, minimum, maximum);
    if(increasing){
        const SIMDVectorDouble distance = VectorDoubleSubtract(maximum, current);
        if(VectorDoubleGetIntX(VectorDoubleGreaterOrEqual(step, distance)) != 0u)
            return maximum;
        return ClampValue(VectorDoubleAdd(current, step), minimum, maximum);
    }
    const SIMDVectorDouble distance = VectorDoubleSubtract(current, minimum);
    if(VectorDoubleGetIntX(VectorDoubleGreaterOrEqual(step, distance)) != 0u)
        return minimum;
    return ClampValue(VectorDoubleSubtract(current, step), minimum, maximum);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SliderBehavior::Validate(const SliderOptions& options)noexcept{
    return
        __hidden_ui_slider_behavior::ValidRange(options.minimum, options.maximum)
        && IsFinite(options.keyStep) && options.keyStep >= 0.0
        && options.width.policy <= LayoutSizePolicy::Stretch && IsFinite(options.width.value) && options.width.value >= 0.0f
        && (options.width.policy != LayoutSizePolicy::Stretch || options.width.value > 0.0f)
        && IsFinite(options.height) && options.height >= 0.0f
    ;
}

Expected<f64> SliderBehavior::Normalize(const f64 minimum, const f64 maximum, const f64 value)noexcept{
    using namespace __hidden_ui_slider_behavior;
    if(!ValidRange(minimum, maximum) || !IsFinite(value))
        return MakeUnexpected(Failure{});
    f64 normalized = 0.0;
    if(minimum != maximum && value > minimum){
        if(value >= maximum)
            normalized = 1.0;
        else{
            const SIMDVectorDouble span = VectorDoubleSubtract(VectorDoubleReplicate(maximum), VectorDoubleReplicate(minimum));
            if(IsFinite(VectorDoubleGetX(span)))
                normalized = VectorDoubleGetX(VectorDoubleDivide(VectorDoubleSubtract(VectorDoubleReplicate(value), VectorDoubleReplicate(minimum)), span));
            else{
                const SIMDVectorDouble scaled = VectorDoubleMultiply(VectorDoubleSet(value, minimum, maximum, minimum), VectorDoubleReplicate(0.5));
                normalized = VectorDoubleGetX(VectorDoubleDivide(VectorDoubleSubtract(VectorDoubleSplatX(scaled), VectorDoubleSplatY(scaled)), VectorDoubleSubtract(VectorDoubleSplatZ(scaled), VectorDoubleSplatW(scaled))));
            }
        }
    }
    if(!IsFinite(normalized))
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble normalizedValue = VectorDoubleReplicate(normalized);
    return VectorDoubleGetX(VectorDoubleSelect(VectorDoubleZero(), VectorDoubleMin(VectorDoubleSplatOne(), normalizedValue), VectorDoubleGreater(normalizedValue, VectorDoubleZero())));
}

Expected<f64> SliderBehavior::Interpolate(const f64 minimum, const f64 maximum, const f64 normalized)noexcept{
    using namespace __hidden_ui_slider_behavior;
    if(!ValidRange(minimum, maximum) || !IsFinite(normalized) || normalized < 0.0 || normalized > 1.0)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble lower = VectorDoubleReplicate(minimum);
    const SIMDVectorDouble upper = VectorDoubleReplicate(maximum);
    const SIMDVectorDouble fraction = VectorDoubleReplicate(normalized);
    SIMDVectorDouble value = lower;
    if(normalized == 1.0)
        value = upper;
    else if(normalized > 0.0 && minimum != maximum){
        if(minimum < 0.0 && maximum > 0.0){
            const SIMDVectorDouble lowerWeight = VectorDoubleSubtract(VectorDoubleSplatOne(), fraction);
            value = VectorDoubleAdd(VectorDoubleMultiply(lower, lowerWeight), VectorDoubleMultiply(upper, fraction));
        }
        else{
            const SIMDVectorDouble span = VectorDoubleSubtract(upper, lower);
            value = VectorDoubleAdd(lower, VectorDoubleMultiply(span, fraction));
        }
        value = ClampValue(value, lower, upper);
    }
    const f64 result = VectorDoubleGetX(value);
    if(!IsFinite(result))
        return MakeUnexpected(Failure{});
    return result;
}

bool SliderBehavior::Admit(SliderState& state, const SliderOptions& options, const SliderPlacement& placement)noexcept{
    using namespace __hidden_ui_slider_behavior;
    if(!Validate(options) || !ValidPlacement(placement))
        return false;
    const bool changed = !state.m_admitted || BitCast<u64>(state.m_minimum) != BitCast<u64>(options.minimum)
        || BitCast<u64>(state.m_maximum) != BitCast<u64>(options.maximum)
        || BitCast<u64>(state.m_keyStep) != BitCast<u64>(options.keyStep) || state.m_enabled != options.enabled
        || !SamePlacement(state.m_admission, placement);
    if(!changed)
        return true;
    state.advanceRevision();
    state.advanceAdmission();
    state.m_minimum = options.minimum;
    state.m_maximum = options.maximum;
    state.m_keyStep = options.keyStep;
    state.m_enabled = options.enabled;
    state.m_admission = placement;
    state.m_admitted = true;
    state.m_press = {};
    state.m_pressMoved = false;
    return true;
}

bool SliderBehavior::Apply(SliderState& state, const SliderOptions& options, const ControlAction& action, SliderResult& result)noexcept{
    using namespace __hidden_ui_slider_behavior;
    if(
        !Validate(options) || !state.m_admitted || !action.id.valid() || action.control != state.controlToken()
        || BitCast<u64>(state.m_minimum) != BitCast<u64>(options.minimum)
        || BitCast<u64>(state.m_maximum) != BitCast<u64>(options.maximum)
        || BitCast<u64>(state.m_keyStep) != BitCast<u64>(options.keyStep) || state.m_enabled != options.enabled
        || action.kind > ControlActionKind::Right
    )
        return false;
    SliderResult accepted = result;
    accepted.valid = true;
    if(!options.enabled || options.minimum == options.maximum){
        accepted.dragging = false;
        result = accepted;
        return true;
    }
    const SIMDVectorDouble lower = VectorDoubleReplicate(options.minimum);
    const SIMDVectorDouble upper = VectorDoubleReplicate(options.maximum);
    SIMDVectorDouble step = VectorDoubleZero();
    SIMDVectorDouble nextValue = VectorDoubleReplicate(state.m_value);
    switch(action.kind){
    case ControlActionKind::Left:
    case ControlActionKind::Down:
        step = KeyStep(lower, upper, VectorDoubleReplicate(options.keyStep));
        nextValue = StepValue(nextValue, lower, upper, step, false);
        break;
    case ControlActionKind::Right:
    case ControlActionKind::Up:
        step = KeyStep(lower, upper, VectorDoubleReplicate(options.keyStep));
        nextValue = StepValue(nextValue, lower, upper, step, true);
        break;
    case ControlActionKind::PageDown:
    case ControlActionKind::PageUp:
        step = KeyStep(lower, upper, VectorDoubleReplicate(options.keyStep));
        for(u32 index = 0u; index < 10u; ++index)
            nextValue = StepValue(nextValue, lower, upper, step, action.kind == ControlActionKind::PageUp);
        break;
    case ControlActionKind::Home:
        nextValue = lower;
        break;
    case ControlActionKind::End:
        nextValue = upper;
        break;
    default:
        result = accepted;
        return true;
    }
    const f64 value = VectorDoubleGetX(nextValue);
    if(!IsFinite(value))
        return false;
    accepted.valueChanged = accepted.valueChanged || BitCast<u64>(state.m_value) != BitCast<u64>(value);
    state.advanceRevision();
    state.m_value = value;
    result = accepted;
    return true;
}

bool SliderBehavior::Seek(SliderState& state, const SliderOptions& options, const PointerGesture& gesture, SliderResult& result)noexcept{
    using namespace __hidden_ui_slider_behavior;
    if(
        !Validate(options) || !state.m_admitted || !ValidGesture(gesture) || gesture.control != state.controlToken()
        || BitCast<u64>(state.m_minimum) != BitCast<u64>(options.minimum)
        || BitCast<u64>(state.m_maximum) != BitCast<u64>(options.maximum)
        || BitCast<u64>(state.m_keyStep) != BitCast<u64>(options.keyStep) || state.m_enabled != options.enabled
        || !SameRect(gesture.referenceRectangle, state.m_admission.centerTravel)
    )
        return false;
    SliderResult accepted = result;
    accepted.valid = true;
    if(!options.enabled || options.minimum == options.maximum || gesture.referenceRectangle.width <= 0.0f){
        accepted.dragging = false;
        result = accepted;
        return true;
    }
    const SIMDVectorDouble normalizedValue = VectorDoubleDivide(VectorDoubleSubtract(VectorDoubleReplicate(gesture.position.x), VectorDoubleReplicate(gesture.referenceRectangle.x)), VectorDoubleReplicate(gesture.referenceRectangle.width));
    const f64 normalized = VectorDoubleGetX(VectorDoubleSelect(VectorDoubleZero(), VectorDoubleMin(VectorDoubleSplatOne(), normalizedValue), VectorDoubleGreater(normalizedValue, VectorDoubleZero())));
    const auto value = Interpolate(options.minimum, options.maximum, normalized);
    if(!value)
        return false;
    accepted.valueChanged = accepted.valueChanged || BitCast<u64>(state.m_value) != BitCast<u64>(*value);
    accepted.dragging = gesture.state == PointerGestureState::Active;
    state.advanceRevision();
    state.m_value = *value;
    state.m_press = accepted.dragging ? gesture.id : InputActionId{};
    state.m_pressMoved = false;
    result = accepted;
    return true;
}

bool SliderBehavior::Drag(SliderState& state, const SliderOptions& options, const PointerGesture& gesture, SliderResult& result)noexcept{
    using namespace __hidden_ui_slider_behavior;
    const f64 baseline = BitCast<f64>(gesture.value);
    if(
        !Validate(options) || !state.m_admitted || !ValidGesture(gesture) || gesture.control != state.controlToken()
        || BitCast<u64>(state.m_minimum) != BitCast<u64>(options.minimum)
        || BitCast<u64>(state.m_maximum) != BitCast<u64>(options.maximum)
        || BitCast<u64>(state.m_keyStep) != BitCast<u64>(options.keyStep) || state.m_enabled != options.enabled
        || !SameRect(gesture.referenceRectangle, state.m_admission.travelBounds)
        || gesture.targetRectangle.width != state.m_admission.thumbExtent.x
        || gesture.targetRectangle.height != state.m_admission.thumbExtent.y || !IsFinite(baseline)
    )
        return false;
    SliderResult accepted = result;
    accepted.valid = true;
    const SIMDVectorDouble travelDelta = VectorDoubleSubtract(VectorDoubleSet(gesture.referenceRectangle.width, gesture.position.x, 0.0, 0.0), VectorDoubleSet(gesture.targetRectangle.width, gesture.origin.x, 0.0, 0.0));
    const f64 travel = VectorDoubleGetX(travelDelta);
    if(!options.enabled || options.minimum == options.maximum || travel <= 0.0){
        accepted.dragging = false;
        result = accepted;
        return true;
    }
    const f64 delta = VectorDoubleGetY(travelDelta);
    bool moved = SamePress(state.m_press, gesture.id) && state.m_pressMoved;
    f64 value = state.m_value;
    if(delta == 0.0){
        if(moved)
            value = baseline;
    }
    else{
        const auto normalized = Normalize(options.minimum, options.maximum, baseline);
        if(!normalized)
            return false;
        const SIMDVectorDouble normalizedValue = VectorDoubleAdd(VectorDoubleReplicate(*normalized), VectorDoubleDivide(VectorDoubleReplicate(delta), VectorDoubleReplicate(travel)));
        const f64 dragged = VectorDoubleGetX(VectorDoubleSelect(VectorDoubleZero(), VectorDoubleMin(VectorDoubleSplatOne(), normalizedValue), VectorDoubleGreater(normalizedValue, VectorDoubleZero())));
        const auto interpolated = Interpolate(options.minimum, options.maximum, dragged);
        if(!interpolated)
            return false;
        value = *interpolated;
        moved = true;
    }
    accepted.valueChanged = accepted.valueChanged || BitCast<u64>(state.m_value) != BitCast<u64>(value);
    accepted.dragging = gesture.state == PointerGestureState::Active;
    state.advanceRevision();
    state.m_value = value;
    state.m_press = accepted.dragging ? gesture.id : InputActionId{};
    state.m_pressMoved = accepted.dragging && moved;
    result = accepted;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

