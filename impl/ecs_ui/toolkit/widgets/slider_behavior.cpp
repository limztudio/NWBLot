// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider.h"

#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<f64> SliderBehavior::Normalize(const f64 minimum, const f64 maximum, const f64 value)noexcept{
    f64 normalized = 0.0;
    if(!TryNormalizeSliderValue(minimum, maximum, value, normalized))
        return MakeUnexpected(Failure{});
    return normalized;
}

Expected<f64> SliderBehavior::Interpolate(const f64 minimum, const f64 maximum, const f64 normalized)noexcept{
    f64 value = 0.0;
    if(!TryInterpolateSliderValue(minimum, maximum, normalized, value))
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
    if(
        action.kind != ControlActionKind::Left && action.kind != ControlActionKind::Down
        && action.kind != ControlActionKind::Right && action.kind != ControlActionKind::Up
        && action.kind != ControlActionKind::PageDown && action.kind != ControlActionKind::PageUp
        && action.kind != ControlActionKind::Home && action.kind != ControlActionKind::End
    ){
        result = accepted;
        return true;
    }
    f64 value = state.m_value;
    if(!TryStepSliderValue(state.m_value, options, action.kind, value))
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
    f64 value = 0.0;
    if(!TryComputeSliderSeekValue(options, gesture, value))
        return false;
    accepted.valueChanged = accepted.valueChanged || BitCast<u64>(state.m_value) != BitCast<u64>(value);
    accepted.dragging = gesture.state == PointerGestureState::Active;
    state.advanceRevision();
    state.m_value = value;
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
    const f64 travel = static_cast<f64>(gesture.referenceRectangle.width) - static_cast<f64>(gesture.targetRectangle.width);
    if(!options.enabled || options.minimum == options.maximum || travel <= 0.0){
        accepted.dragging = false;
        result = accepted;
        return true;
    }
    b8 moved = state.m_press == gesture.id && state.m_pressMoved;
    f64 value = state.m_value;
    if(!TryComputeSliderDragValue(state.m_value, baseline, options, gesture, moved, value, moved))
        return false;
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

