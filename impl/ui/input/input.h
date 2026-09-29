// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../id.h"
#include "../paint.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace InputEventType{
    enum Enum : u8{ PointerMove, PrimaryDown, PrimaryUp, KeyDown, KeyUp, FocusLost, PointerLeave, PointerCaptureLost };
};

namespace InputKey{
    enum Enum : u8{ None, Tab, Enter, Space, Escape, Left, Right, Home, End, Backspace, Delete, A, C, X, V, Z, Y };
};

namespace InputActionSource{
    enum Enum : u8{ Pointer, Keyboard };
};

namespace PointerGestureState{
    enum Enum : u8{ Active, Completed };
};

struct InputEvent{
    InputEventType::Enum type = InputEventType::PointerMove;
    Point position;
    InputKey::Enum key = InputKey::None;
    bool shift = false;
    bool repeat = false;
    bool control = false;
    bool alt = false;
};

// Rectangle and clip use the same logical coordinates as painting; publication order provides the default Tab order.
struct HitTarget{
    WidgetId id;
    Rect rectangle;
    Rect clip;
    u64 declarationGeneration = 1u;
    u32 paintOrder = 0u;
    bool enabled = true;
    bool focusable = false;
    bool activatable = false;
    bool pointerGesture = false;
    bool textEditable = false;
    // Both zero dimensions omit the reference; a supplied reference has two positive dimensions.
    Rect gestureReference{};
};

// Actions retain values, never callbacks or declaration pointers; target lifetime must still match when consumed.
struct InputActionId{
    WidgetId target;
    u64 declarationGeneration = 0u;
    u64 layoutGeneration = 0u;
    u64 sequence = 0u;

    [[nodiscard]] bool valid()const{
        return target.valid() && declarationGeneration != 0u && layoutGeneration != 0u && sequence != 0u;
    }
};

struct InputAction{
    InputActionId id;
    InputActionSource::Enum source = InputActionSource::Pointer;
};

// One coalesced update per press retains the committed geometry; cancellation removes the gesture without delivery.
struct PointerGesture{
    InputActionId id;
    Point origin;
    Point position;
    Rect targetRectangle;
    Rect referenceRectangle;
    PointerGestureState::Enum state = PointerGestureState::Active;
};

struct InputRoutingResult{
    bool pointerConsumed = false;
    bool keyboardConsumed = false;
    bool wantsPointer = false;
    bool wantsKeyboard = false;
    bool activationOverflow = false;
    bool gestureOverflow = false;
    WidgetId hover;
    WidgetId focus;
    WidgetId capture;
};

inline constexpr usize s_InputMaxTargets = 4096u;
inline constexpr usize s_InputMaxEvents = 256u;
inline constexpr usize s_InputMaxActions = 256u;
inline constexpr usize s_InputMaxPointerGestures = 256u;

template<typename T>
using InputVector = Vector<T, Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

