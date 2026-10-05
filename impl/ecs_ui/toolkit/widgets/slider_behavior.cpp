// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider.h"

#include <impl/ecs_ui/toolkit/layout/validation.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_behavior{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidRange(const f64 minimum, const f64 maximum){
    return IsFinite(minimum) && IsFinite(maximum) && minimum <= maximum;
}

[[nodiscard]] static bool SameRect(const Rect& lhs, const Rect& rhs){
    return
        BitCast<u32>(lhs.x) == BitCast<u32>(rhs.x) && BitCast<u32>(lhs.y) == BitCast<u32>(rhs.y)
        && BitCast<u32>(lhs.width) == BitCast<u32>(rhs.width) && BitCast<u32>(lhs.height) == BitCast<u32>(rhs.height)
    ;
}

[[nodiscard]] static bool ValidPlacement(const SliderPlacement& placement){
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

[[nodiscard]] static bool SamePlacement(const SliderPlacement& lhs, const SliderPlacement& rhs){
    return
        SameRect(lhs.bounds, rhs.bounds) && SameRect(lhs.clip, rhs.clip) && SameRect(lhs.travelBounds, rhs.travelBounds)
        && SameRect(lhs.track, rhs.track) && SameRect(lhs.centerTravel, rhs.centerTravel)
        && BitCast<u32>(lhs.thumbExtent.x) == BitCast<u32>(rhs.thumbExtent.x)
        && BitCast<u32>(lhs.thumbExtent.y) == BitCast<u32>(rhs.thumbExtent.y)
    ;
}

[[nodiscard]] static bool SamePress(const InputActionId& lhs, const InputActionId& rhs){
    return
        lhs.target == rhs.target && lhs.declarationGeneration == rhs.declarationGeneration
        && lhs.layoutGeneration == rhs.layoutGeneration && lhs.sequence == rhs.sequence
    ;
}

[[nodiscard]] static bool ValidGesture(const PointerGesture& gesture){
    return
        gesture.id.valid() && gesture.updateSequence != 0u && gesture.state <= PointerGestureState::Completed
        && IsFinite(gesture.origin.x) && IsFinite(gesture.origin.y)
        && IsFinite(gesture.position.x) && IsFinite(gesture.position.y)
        && IsPreciseUiRect(gesture.targetRectangle) && IsPreciseUiRect(gesture.referenceRectangle)
    ;
}

[[nodiscard]] static f64 ClampValue(const f64 value, const f64 minimum, const f64 maximum){
    return value < minimum ? minimum : value > maximum ? maximum : value;
}

[[nodiscard]] static f64 KeyStep(const SliderOptions& options){
    if(options.keyStep > 0.0)
        return options.keyStep;
    const f64 span = options.maximum - options.minimum;
    if(IsFinite(span)){
        const f64 step = span / 100.0;
        return step == 0.0 ? span : step;
    }
    return options.maximum / 100.0 - options.minimum / 100.0;
}

[[nodiscard]] static f64 StepValue(const f64 value, const SliderOptions& options, const f64 step, const bool increasing){
    const f64 current = ClampValue(value, options.minimum, options.maximum);
    if(increasing){
        const f64 distance = options.maximum - current;
        return step >= distance ? options.maximum : ClampValue(current + step, options.minimum, options.maximum);
    }
    const f64 distance = current - options.minimum;
    return step >= distance ? options.minimum : ClampValue(current - step, options.minimum, options.maximum);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SliderBehavior::validate(const SliderOptions& options){
    return
        __hidden_ui_slider_behavior::ValidRange(options.minimum, options.maximum)
        && IsFinite(options.keyStep) && options.keyStep >= 0.0
        && options.width.policy <= LayoutSizePolicy::Stretch && IsFinite(options.width.value) && options.width.value >= 0.0f
        && (options.width.policy != LayoutSizePolicy::Stretch || options.width.value > 0.0f)
        && IsFinite(options.height) && options.height >= 0.0f
    ;
}

bool SliderBehavior::normalize(const f64 minimum, const f64 maximum, const f64 value, f64& out){
    using namespace __hidden_ui_slider_behavior;
    if(!ValidRange(minimum, maximum) || !IsFinite(value))
        return false;
    f64 normalized = 0.0;
    if(minimum != maximum && value > minimum){
        if(value >= maximum)
            normalized = 1.0;
        else{
            const f64 span = maximum - minimum;
            normalized = IsFinite(span) ? (value - minimum) / span
                : (value * 0.5 - minimum * 0.5) / (maximum * 0.5 - minimum * 0.5);
        }
    }
    if(!IsFinite(normalized))
        return false;
    out = Clamp(normalized, 0.0, 1.0);
    return true;
}

bool SliderBehavior::interpolate(const f64 minimum, const f64 maximum, const f64 normalized, f64& out){
    using namespace __hidden_ui_slider_behavior;
    if(!ValidRange(minimum, maximum) || !IsFinite(normalized) || normalized < 0.0 || normalized > 1.0)
        return false;
    f64 value = minimum;
    if(normalized == 1.0)
        value = maximum;
    else if(normalized > 0.0 && minimum != maximum){
        value = minimum < 0.0 && maximum > 0.0 ? minimum * (1.0 - normalized) + maximum * normalized
            : minimum + (maximum - minimum) * normalized;
        value = ClampValue(value, minimum, maximum);
    }
    if(!IsFinite(value))
        return false;
    out = value;
    return true;
}

bool SliderBehavior::admit(SliderState& state, const SliderOptions& options, const SliderPlacement& placement){
    using namespace __hidden_ui_slider_behavior;
    if(!validate(options) || !ValidPlacement(placement))
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

bool SliderBehavior::apply(
    SliderState& state,
    const SliderOptions& options,
    const ControlAction& action,
    SliderResult& result){
    using namespace __hidden_ui_slider_behavior;
    if(
        !validate(options) || !state.m_admitted || !action.id.valid() || action.control != state.controlToken()
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
    f64 value = state.m_value;
    switch(action.kind){
    case ControlActionKind::Left:
    case ControlActionKind::Down:
        value = StepValue(value, options, KeyStep(options), false);
        break;
    case ControlActionKind::Right:
    case ControlActionKind::Up:
        value = StepValue(value, options, KeyStep(options), true);
        break;
    case ControlActionKind::PageDown:
    case ControlActionKind::PageUp:
        for(u32 index = 0u; index < 10u; ++index)
            value = StepValue(value, options, KeyStep(options), action.kind == ControlActionKind::PageUp);
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

bool SliderBehavior::seek(
    SliderState& state,
    const SliderOptions& options,
    const PointerGesture& gesture,
    SliderResult& result){
    using namespace __hidden_ui_slider_behavior;
    if(
        !validate(options) || !state.m_admitted || !ValidGesture(gesture) || gesture.control != state.controlToken()
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
    const f64 normalized = Clamp((static_cast<f64>(gesture.position.x) - gesture.referenceRectangle.x)
        / gesture.referenceRectangle.width, 0.0, 1.0);
    f64 value = state.m_value;
    if(!interpolate(options.minimum, options.maximum, normalized, value))
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

bool SliderBehavior::drag(
    SliderState& state,
    const SliderOptions& options,
    const PointerGesture& gesture,
    SliderResult& result){
    using namespace __hidden_ui_slider_behavior;
    const f64 baseline = BitCast<f64>(gesture.value);
    if(
        !validate(options) || !state.m_admitted || !ValidGesture(gesture) || gesture.control != state.controlToken()
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
    const f64 travel = static_cast<f64>(gesture.referenceRectangle.width) - gesture.targetRectangle.width;
    if(!options.enabled || options.minimum == options.maximum || travel <= 0.0){
        accepted.dragging = false;
        result = accepted;
        return true;
    }
    const f64 delta = static_cast<f64>(gesture.position.x) - gesture.origin.x;
    bool moved = SamePress(state.m_press, gesture.id) && state.m_pressMoved;
    f64 value = state.m_value;
    if(delta == 0.0){
        if(moved)
            value = baseline;
    }
    else{
        f64 normalized = 0.0;
        if(!normalize(options.minimum, options.maximum, baseline, normalized))
            return false;
        normalized = Clamp(normalized + delta / travel, 0.0, 1.0);
        if(!interpolate(options.minimum, options.maximum, normalized, value))
            return false;
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

