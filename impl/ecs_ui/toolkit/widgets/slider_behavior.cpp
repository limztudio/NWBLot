// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider.h"

#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_behavior{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static constexpr f64 KeyStep(const f64 minimum, const f64 maximum, const f64 configuredStep)noexcept{
    if(configuredStep > 0.0)
        return configuredStep;
    const f64 span = maximum - minimum;
    if(IsFinite(span)){
        const f64 step = span / s_SliderDefaultKeyStepDivisor;
        return step == 0.0 ? span : step;
    }
    return (maximum / s_SliderDefaultKeyStepDivisor) - (minimum / s_SliderDefaultKeyStepDivisor);
}

[[nodiscard]] static constexpr f64 StepValue(const f64 value, const f64 minimum, const f64 maximum, const f64 step, const b8 increasing)noexcept{
    const f64 current = Clamp(value, minimum, maximum);
    if(increasing){
        const f64 distance = maximum - current;
        if(step >= distance)
            return maximum;
        return Clamp(current + step, minimum, maximum);
    }
    const f64 distance = current - minimum;
    if(step >= distance)
        return minimum;
    return Clamp(current - step, minimum, maximum);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<f64> SliderBehavior::Normalize(const f64 minimum, const f64 maximum, const f64 value)noexcept{
    if(!IsValidSliderRange(minimum, maximum) || !IsFinite(value))
        return MakeUnexpected(Failure{});
    f64 normalizedValue = 0.0;
    if(minimum != maximum && value > minimum){
        if(value >= maximum)
            normalizedValue = 1.0;
        else{
            const f64 span = maximum - minimum;
            if(IsFinite(span))
                normalizedValue = (value - minimum) / span;
            else{
                const SIMDVectorDouble scaled = VectorDoubleMultiply(VectorDoubleSet(value, minimum, maximum, minimum), VectorDoubleReplicate(s_SliderOverflowGuardScale));
                normalizedValue = (VectorDoubleGetX(scaled) - VectorDoubleGetY(scaled)) / (VectorDoubleGetZ(scaled) - VectorDoubleGetW(scaled));
            }
        }
    }
    if(!IsFinite(normalizedValue))
        return MakeUnexpected(Failure{});
    return normalizedValue > 0.0 ? Min(1.0, normalizedValue) : 0.0;
}

Expected<f64> SliderBehavior::Interpolate(const f64 minimum, const f64 maximum, const f64 normalized)noexcept{
    if(!IsValidSliderRange(minimum, maximum) || !IsFinite(normalized) || normalized < 0.0 || normalized > 1.0)
        return MakeUnexpected(Failure{});
    f64 value = minimum;
    if(normalized == 1.0)
        value = maximum;
    else if(normalized > 0.0 && minimum != maximum){
        if(minimum < 0.0 && maximum > 0.0){
            const f64 lowerWeight = 1.0 - normalized;
            value = minimum * lowerWeight + maximum * normalized;
        }
        else{
            const f64 span = maximum - minimum;
            value = minimum + span * normalized;
        }
        value = Clamp(value, minimum, maximum);
    }
    if(!IsFinite(value))
        return MakeUnexpected(Failure{});
    return value;
}

b8 SliderBehavior::Admit(SliderState& state, const SliderOptions& options, const SliderPlacement& placement)noexcept{
    if(!IsValidSliderOptions(options) || !IsValidSliderPlacement(placement))
        return false;
    const b8 changed = !state.m_admitted || BitCast<u64>(state.m_minimum) != BitCast<u64>(options.minimum)
        || BitCast<u64>(state.m_maximum) != BitCast<u64>(options.maximum)
        || BitCast<u64>(state.m_keyStep) != BitCast<u64>(options.keyStep) || state.m_enabled != options.enabled
        || !SameSliderPlacementGeometry(state.m_admission, placement);
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

b8 SliderBehavior::Apply(SliderState& state, SliderResult& result, const SliderOptions& options, const ControlAction& action)noexcept{
    using namespace __hidden_ui_slider_behavior;
    if(
        !IsValidSliderOptions(options) || !state.m_admitted || !action.id.valid() || action.control != state.controlToken()
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
    f64 step = 0.0;
    f64 value = state.m_value;
    switch(action.kind){
    case ControlActionKind::Left:
    case ControlActionKind::Down:
        step = KeyStep(options.minimum, options.maximum, options.keyStep);
        value = StepValue(value, options.minimum, options.maximum, step, false);
        break;
    case ControlActionKind::Right:
    case ControlActionKind::Up:
        step = KeyStep(options.minimum, options.maximum, options.keyStep);
        value = StepValue(value, options.minimum, options.maximum, step, true);
        break;
    case ControlActionKind::PageDown:
    case ControlActionKind::PageUp:
        step = KeyStep(options.minimum, options.maximum, options.keyStep);
        for(u32 index = 0u; index < s_SliderPageStepCount; ++index)
            value = StepValue(value, options.minimum, options.maximum, step, action.kind == ControlActionKind::PageUp);
        break;
    case ControlActionKind::Home:
        value = options.minimum;
        break;
    case ControlActionKind::End:
        value = options.maximum;
        break;
    default:
        result = accepted;
        return true;
    }
    if(!IsFinite(value))
        return false;
    accepted.valueChanged = accepted.valueChanged || BitCast<u64>(state.m_value) != BitCast<u64>(value);
    state.advanceRevision();
    state.m_value = value;
    result = accepted;
    return true;
}

b8 SliderBehavior::Seek(SliderState& state, SliderResult& result, const SliderOptions& options, const PointerGesture& gesture)noexcept{
    if(
        !IsValidSliderOptions(options) || !state.m_admitted || !IsValidSliderGesture(gesture) || gesture.control != state.controlToken()
        || BitCast<u64>(state.m_minimum) != BitCast<u64>(options.minimum)
        || BitCast<u64>(state.m_maximum) != BitCast<u64>(options.maximum)
        || BitCast<u64>(state.m_keyStep) != BitCast<u64>(options.keyStep) || state.m_enabled != options.enabled
        || !EqualUiRect(gesture.referenceRectangle, state.m_admission.centerTravel)
    )
        return false;
    SliderResult accepted = result;
    accepted.valid = true;
    if(!options.enabled || options.minimum == options.maximum || gesture.referenceRectangle.width <= 0.0f){
        accepted.dragging = false;
        result = accepted;
        return true;
    }
    const f64 normalizedValue = (static_cast<f64>(gesture.position.x) - static_cast<f64>(gesture.referenceRectangle.x)) / static_cast<f64>(gesture.referenceRectangle.width);
    const f64 normalized = normalizedValue > 0.0 ? Min(1.0, normalizedValue) : 0.0;
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

b8 SliderBehavior::Drag(SliderState& state, SliderResult& result, const SliderOptions& options, const PointerGesture& gesture)noexcept{
    const f64 baseline = BitCast<f64>(gesture.value);
    if(
        !IsValidSliderOptions(options) || !state.m_admitted || !IsValidSliderGesture(gesture) || gesture.control != state.controlToken()
        || BitCast<u64>(state.m_minimum) != BitCast<u64>(options.minimum)
        || BitCast<u64>(state.m_maximum) != BitCast<u64>(options.maximum)
        || BitCast<u64>(state.m_keyStep) != BitCast<u64>(options.keyStep) || state.m_enabled != options.enabled
        || !EqualUiRect(gesture.referenceRectangle, state.m_admission.travelBounds)
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
    b8 moved = state.m_press == gesture.id && state.m_pressMoved;
    f64 value = state.m_value;
    if(delta == 0.0){
        if(moved)
            value = baseline;
    }
    else{
        const auto normalized = Normalize(options.minimum, options.maximum, baseline);
        if(!normalized)
            return false;
        const f64 normalizedValue = *normalized + delta / travel;
        const f64 dragged = normalizedValue > 0.0 ? Min(1.0, normalizedValue) : 0.0;
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

