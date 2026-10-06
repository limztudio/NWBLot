// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "window.h"

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

[[nodiscard]] bool Displacement(const PointerGesture& gesture, f64& x, f64& y)noexcept{
    if(
        !gesture.id.valid() || !IsFinite(gesture.origin.x) || !IsFinite(gesture.origin.y)
        || !IsFinite(gesture.position.x) || !IsFinite(gesture.position.y)
        || !ValidBounds(gesture.referenceRectangle) || gesture.referenceRectangle.width <= 0.0f
        || gesture.referenceRectangle.height <= 0.0f || gesture.state > PointerGestureState::Completed
    )
        return false;
    x = static_cast<f64>(gesture.position.x) - gesture.origin.x;
    y = static_cast<f64>(gesture.position.y) - gesture.origin.y;
    return true;
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
    candidate.bounds.width = Max(candidate.bounds.width, metrics.minimumSize.x);
    candidate.bounds.height = Max(candidate.bounds.height, metrics.minimumSize.y);
    candidate.initialized = true;
    if(!__hidden_ui_window_behavior::ValidBounds(candidate.bounds))
        return false;
    state = candidate;
    return true;
}

bool WindowBehavior::ApplyMove(WindowState& state, const PointerGesture& gesture)noexcept{
    f64 x = 0.0;
    f64 y = 0.0;
    if(!__hidden_ui_window_behavior::Displacement(gesture, x, y))
        return false;
    WindowGestureState movement = state.moveGesture;
    if(!__hidden_ui_window_behavior::SameGesture(movement.id, gesture.id)){
        movement.id = gesture.id;
        movement.initialBounds = gesture.referenceRectangle;
    }
    const f64 left = static_cast<f64>(movement.initialBounds.x) + x;
    const f64 top = static_cast<f64>(movement.initialBounds.y) + y;
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
    f64 x = 0.0;
    f64 y = 0.0;
    if(
        !__hidden_ui_window_behavior::Displacement(gesture, x, y)
        || !IsFinite(minimumSize.x) || minimumSize.x <= 0.0f
        || !IsFinite(minimumSize.y) || minimumSize.y <= 0.0f
    )
        return false;
    WindowGestureState resizing = state.resizeGesture;
    if(!__hidden_ui_window_behavior::SameGesture(resizing.id, gesture.id)){
        resizing.id = gesture.id;
        resizing.initialBounds = gesture.referenceRectangle;
    }
    const f64 width = Max(static_cast<f64>(minimumSize.x), static_cast<f64>(resizing.initialBounds.width) + x);
    const f64 height = Max(static_cast<f64>(minimumSize.y), static_cast<f64>(resizing.initialBounds.height) + y);
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
    state.bounds.x = Clamp(state.bounds.x, 0.0f, Max(0.0f, display.logicalWidth - state.bounds.width));
    state.bounds.y = Clamp(state.bounds.y, 0.0f, Max(0.0f, display.logicalHeight - height));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

