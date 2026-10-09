// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "window.h"

#include <global/math/vector_double.h>
#include <global/math/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_window_behavior{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidBounds(const Rect& bounds)noexcept{
    return IsFinite(bounds.x) && IsFinite(bounds.y) && IsFinite(bounds.width) && IsFinite(bounds.height)
        && bounds.width >= 0.0f && bounds.height >= 0.0f && IsFinite(bounds.x + bounds.width)
        && IsFinite(bounds.y + bounds.height);
}

[[nodiscard]] bool SameGesture(const InputActionId& lhs, const InputActionId& rhs)noexcept{
    return lhs.target == rhs.target && lhs.declarationGeneration == rhs.declarationGeneration
        && lhs.layoutGeneration == rhs.layoutGeneration && lhs.sequence == rhs.sequence;
}

struct PointerDisplacement{
    f64 x;
    f64 y;
};

[[nodiscard]] Expected<PointerDisplacement> Displacement(const PointerGesture& gesture)noexcept{
    if(
        !gesture.id.valid() || !IsFinite(gesture.origin.x) || !IsFinite(gesture.origin.y)
        || !IsFinite(gesture.position.x) || !IsFinite(gesture.position.y)
        || !ValidBounds(gesture.referenceRectangle) || gesture.referenceRectangle.width <= 0.0f
        || gesture.referenceRectangle.height <= 0.0f || gesture.state > PointerGestureState::Completed
    )
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble displacement = SIMDVectorDouble{ gesture.position.x, gesture.position.y }
        - SIMDVectorDouble{ gesture.origin.x, gesture.origin.y };
    return PointerDisplacement{ displacement.x, displacement.y };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool WindowBehavior::Initialize(WindowState& state, const WindowOptions& options, const WindowMetrics& metrics)noexcept{
    if(
        !__hidden_ui_window_behavior::ValidBounds(options.initialBounds)
        || options.direction > LayoutDirection::Column || options.direction == LayoutDirection::Leaf
        || !IsFinite(options.minimumSize.x) || options.minimumSize.x < 0.0f
        || !IsFinite(options.minimumSize.y) || options.minimumSize.y < 0.0f
        || !IsFinite(metrics.minimumSize.x) || metrics.minimumSize.x <= 0.0f
        || !IsFinite(metrics.minimumSize.y) || metrics.minimumSize.y <= 0.0f
    )
        return false;
    WindowState candidate = state;
    if(!candidate.initialized){
        candidate.bounds = options.initialBounds;
        candidate.moveGesture = {};
        candidate.resizeGesture = {};
    }
    if(!__hidden_ui_window_behavior::ValidBounds(candidate.bounds))
        return false;
    const SIMDVector size = VectorSet(candidate.bounds.width, candidate.bounds.height, candidate.bounds.width, candidate.bounds.height);
    const SIMDVector minimum = VectorSet(metrics.minimumSize.x, metrics.minimumSize.y, metrics.minimumSize.x, metrics.minimumSize.y);
    const SIMDVector constrained = VectorSelect(minimum, size, VectorGreater(size, minimum));
    candidate.bounds.width = VectorGetX(constrained);
    candidate.bounds.height = VectorGetY(constrained);
    candidate.initialized = true;
    if(!__hidden_ui_window_behavior::ValidBounds(candidate.bounds))
        return false;
    state = candidate;
    return true;
}

bool WindowBehavior::ApplyMove(WindowState& state, const PointerGesture& gesture)noexcept{
    const auto displacement = __hidden_ui_window_behavior::Displacement(gesture);
    if(!displacement)
        return false;
    WindowGestureState movement = state.moveGesture;
    if(!__hidden_ui_window_behavior::SameGesture(movement.id, gesture.id)){
        movement.id = gesture.id;
        movement.initialBounds = gesture.referenceRectangle;
    }
    const SIMDVectorDouble leftTopValue = (SIMDVectorDouble{ static_cast<f64>(movement.initialBounds.x), static_cast<f64>(movement.initialBounds.y) } + SIMDVectorDouble{ displacement->x, displacement->y });
    const f64 left = leftTopValue.x;
    const f64 top = leftTopValue.y;
    if(Abs(left) > Limit<f32>::s_Max || Abs(top) > Limit<f32>::s_Max)
        return false;
    Rect candidate = state.bounds;
    candidate.x = static_cast<f32>(left);
    candidate.y = static_cast<f32>(top);
    if(!__hidden_ui_window_behavior::ValidBounds(candidate))
        return false;
    state.bounds = candidate;
    state.moveGesture = movement;
    return true;
}

bool WindowBehavior::ApplyResize(WindowState& state, const PointerGesture& gesture, const Point& minimumSize)noexcept{
    const auto displacement = __hidden_ui_window_behavior::Displacement(gesture);
    if(
        !displacement
        || !IsFinite(minimumSize.x) || minimumSize.x <= 0.0f
        || !IsFinite(minimumSize.y) || minimumSize.y <= 0.0f
    )
        return false;
    WindowGestureState resizing = state.resizeGesture;
    if(!__hidden_ui_window_behavior::SameGesture(resizing.id, gesture.id)){
        resizing.id = gesture.id;
        resizing.initialBounds = gesture.referenceRectangle;
    }
    const SIMDVectorDouble geometryPair1Operand0 = SIMDVectorDouble{ static_cast<f64>(minimumSize.x), static_cast<f64>(minimumSize.y) };
    const SIMDVectorDouble geometryPair1Operand1 = (SIMDVectorDouble{ static_cast<f64>(resizing.initialBounds.width), static_cast<f64>(resizing.initialBounds.height) } + SIMDVectorDouble{ displacement->x, displacement->y });
    const SIMDVectorDouble widthHeightValue = ((geometryPair1Operand0 > geometryPair1Operand1) ? geometryPair1Operand0 : geometryPair1Operand1);
    const f64 width = widthHeightValue.x;
    const f64 height = widthHeightValue.y;
    if(width > Limit<f32>::s_Max || height > Limit<f32>::s_Max)
        return false;
    Rect candidate = state.bounds;
    candidate.width = static_cast<f32>(width);
    candidate.height = static_cast<f32>(height);
    if(!__hidden_ui_window_behavior::ValidBounds(candidate))
        return false;
    state.bounds = candidate;
    state.resizeGesture = resizing;
    return true;
}

bool WindowBehavior::Constrain(WindowState& state, const DisplayMetrics& display, const f32 titleHeight)noexcept{
    if(
        !IsFinite(display.logicalWidth) || display.logicalWidth <= 0.0f
        || !IsFinite(display.logicalHeight) || display.logicalHeight <= 0.0f
        || !IsFinite(titleHeight) || titleHeight <= 0.0f
        || !__hidden_ui_window_behavior::ValidBounds(state.bounds)
    )
        return false;
    const f32 height = state.collapsed ? titleHeight : state.bounds.height;
    const SIMDVector remaining = VectorSubtract(
        VectorSet(display.logicalWidth, display.logicalHeight, display.logicalWidth, display.logicalHeight),
        VectorSet(state.bounds.width, height, state.bounds.width, height)
    );
    const SIMDVector maximum = VectorSelect(remaining, VectorZero(), VectorGreater(VectorZero(), remaining));
    const SIMDVector origin = VectorSet(state.bounds.x, state.bounds.y, state.bounds.x, state.bounds.y);
    const SIMDVector positive = VectorSelect(VectorZero(), origin, VectorGreater(origin, VectorZero()));
    const SIMDVector constrained = VectorSelect(positive, maximum, VectorGreater(positive, maximum));
    state.bounds.x = VectorGetX(constrained);
    state.bounds.y = VectorGetY(constrained);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

