// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "window.h"

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_window_behavior{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidBounds(const Rect& bounds)noexcept{
    return IsFinite(bounds.x) && IsFinite(bounds.y) && IsFinite(bounds.width) && IsFinite(bounds.height) && bounds.width >= 0.0f && bounds.height >= 0.0f && IsFinite(bounds.x + bounds.width) && IsFinite(bounds.y + bounds.height);
}

[[nodiscard]] bool SameGesture(const InputActionId& lhs, const InputActionId& rhs)noexcept{
    return lhs.target == rhs.target && lhs.declarationGeneration == rhs.declarationGeneration && lhs.layoutGeneration == rhs.layoutGeneration && lhs.sequence == rhs.sequence;
}

[[nodiscard]] Expected<SIMDVectorDouble> LoadPointerDisplacement(const PointerGesture& gesture)noexcept{
    if(
        !gesture.id.valid() || !IsFinite(gesture.origin.x) || !IsFinite(gesture.origin.y)
        || !IsFinite(gesture.position.x) || !IsFinite(gesture.position.y)
        || !ValidBounds(gesture.referenceRectangle) || gesture.referenceRectangle.width <= 0.0f
        || gesture.referenceRectangle.height <= 0.0f || gesture.state > PointerGestureState::Completed
    )
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble displacement = VectorDoubleSubtract(VectorDoubleSet(gesture.position.x, gesture.position.y, 0.0, 0.0), VectorDoubleSet(gesture.origin.x, gesture.origin.y, 0.0, 0.0));
    return displacement;
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
    const SIMDVectorSingle size = VectorSet(candidate.bounds.width, candidate.bounds.height, candidate.bounds.width, candidate.bounds.height);
    const SIMDVectorSingle minimum = VectorSet(metrics.minimumSize.x, metrics.minimumSize.y, metrics.minimumSize.x, metrics.minimumSize.y);
    const SIMDVectorSingle constrained = VectorSelect(minimum, size, VectorGreater(size, minimum));
    candidate.bounds.width = VectorGetX(constrained);
    candidate.bounds.height = VectorGetY(constrained);
    candidate.initialized = true;
    if(!__hidden_ui_window_behavior::ValidBounds(candidate.bounds))
        return false;
    state = candidate;
    return true;
}

bool WindowBehavior::ApplyMove(WindowState& state, const PointerGesture& gesture)noexcept{
    const auto displacement = __hidden_ui_window_behavior::LoadPointerDisplacement(gesture);
    if(!displacement)
        return false;
    WindowGestureState movement = state.moveGesture;
    if(!__hidden_ui_window_behavior::SameGesture(movement.id, gesture.id)){
        movement.id = gesture.id;
        movement.initialBounds = gesture.referenceRectangle;
    }
    const SIMDVectorDouble leftTopValue = VectorDoubleAdd(VectorDoubleSet(static_cast<f64>(movement.initialBounds.x), static_cast<f64>(movement.initialBounds.y), 0.0, 0.0), *displacement);
    const f64 left = VectorDoubleGetX(leftTopValue);
    const f64 top = VectorDoubleGetY(leftTopValue);
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
    const auto displacement = __hidden_ui_window_behavior::LoadPointerDisplacement(gesture);
    if(!displacement || !IsFinite(minimumSize.x) || minimumSize.x <= 0.0f || !IsFinite(minimumSize.y) || minimumSize.y <= 0.0f)
        return false;
    WindowGestureState resizing = state.resizeGesture;
    if(!__hidden_ui_window_behavior::SameGesture(resizing.id, gesture.id)){
        resizing.id = gesture.id;
        resizing.initialBounds = gesture.referenceRectangle;
    }
    const SIMDVectorDouble geometryPair1Operand0 = VectorDoubleSet(static_cast<f64>(minimumSize.x), static_cast<f64>(minimumSize.y), 0.0, 0.0);
    const SIMDVectorDouble geometryPair1Operand1 = VectorDoubleAdd(VectorDoubleSet(static_cast<f64>(resizing.initialBounds.width), static_cast<f64>(resizing.initialBounds.height), 0.0, 0.0), *displacement);
    const SIMDVectorDouble widthHeightValue = VectorDoubleSelect(geometryPair1Operand1, geometryPair1Operand0, VectorDoubleGreater(geometryPair1Operand0, geometryPair1Operand1));
    const f64 width = VectorDoubleGetX(widthHeightValue);
    const f64 height = VectorDoubleGetY(widthHeightValue);
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
    if(!IsFinite(display.logicalWidth) || display.logicalWidth <= 0.0f || !IsFinite(display.logicalHeight) || display.logicalHeight <= 0.0f || !IsFinite(titleHeight) || titleHeight <= 0.0f || !__hidden_ui_window_behavior::ValidBounds(state.bounds))
        return false;
    const f32 height = state.collapsed ? titleHeight : state.bounds.height;
    const SIMDVectorSingle remaining = VectorSubtract(VectorSet(display.logicalWidth, display.logicalHeight, display.logicalWidth, display.logicalHeight), VectorSet(state.bounds.width, height, state.bounds.width, height));
    const SIMDVectorSingle maximum = VectorSelect(remaining, VectorZero(), VectorGreater(VectorZero(), remaining));
    const SIMDVectorSingle origin = VectorSet(state.bounds.x, state.bounds.y, state.bounds.x, state.bounds.y);
    const SIMDVectorSingle positive = VectorSelect(VectorZero(), origin, VectorGreater(origin, VectorZero()));
    const SIMDVectorSingle constrained = VectorSelect(positive, maximum, VectorGreater(positive, maximum));
    state.bounds.x = VectorGetX(constrained);
    state.bounds.y = VectorGetY(constrained);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

