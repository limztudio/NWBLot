// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../id.h"
#include "../paint.h"
#include "popup.h"
#include "control.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace InputEventType{
    enum Enum : u8{
        PointerMove, PrimaryDown, PrimaryUp, KeyDown, KeyUp, FocusLost, PointerLeave, PointerCaptureLost, FocusGained, PointerWheel,
        SecondaryDown, SecondaryUp
    };
};

namespace InputKey{
    enum Enum : u8{
        None, Tab, Enter, Space, Escape, Left, Right, Home, End, Backspace, Delete, A, C, X, V, Z, Y, Up, Down, PageUp, PageDown,
        Menu, F10
    };
};

static_assert(static_cast<u8>(InputKey::F10) <= 32u);

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
    f64 scrollX = 0.0;
    f64 scrollY = 0.0;
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
    PopupToken popup;
    u32 layer = 0u;
    ControlToken control;
    WidgetId owner;
    u64 ownerDeclarationGeneration = 0u;
    u64 value = 0u;
    bool navigable = false;
    bool scrollable = false;
    f64 scrollStep = 0.0;
    u64 pageRows = 1u;
    f64 gestureMaximum = 0.0;
    bool focusOnCommit = false;
    // A focused editor may borrow vertical navigation and an Enter intention from another accepted control lifetime.
    WidgetId keyboardOwner;
    u64 keyboardOwnerDeclarationGeneration = 0u;
    ControlToken keyboardControl;
    bool contextMenu = false;
    f64 scrollStepX = 0.0;
    f64 gestureMaximumX = 0.0;
    bool horizontalNavigation = false;
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
    PopupToken popup;
    ControlToken control;
};

// Context-menu triggers retain the accepted anchor and lifetime without borrowing the declaration.
struct ContextMenuAction{
    InputActionId id;
    PopupToken popup;
    ControlToken control;
    Point position;
    bool keyboard = false;
};

// Ordered copied input addressed to a focusable host, including stable values selected by its parts.
struct ControlAction{
    InputActionId id;
    WidgetId source;
    PopupToken popup;
    ControlToken control;
    ControlActionKind::Enum kind = ControlActionKind::Wheel;
    f64 delta = 0.0;
    u64 value = 0u;
    f64 step = 0.0;
    u64 pageRows = 1u;
    f64 offset = 0.0;
    f64 maximum = 0.0;
    u64 sourceDeclarationGeneration = 0u;
    ControlToken sourceControl;
    // Positive X moves right; positive Y moves up. Each wheel action carries one native event.
    f64 deltaX = 0.0;
    f64 stepX = 0.0;
    f64 maximumX = 0.0;
};

// One coalesced update per press retains the committed geometry; cancellation removes the gesture without delivery.
struct PointerGesture{
    InputActionId id;
    Point origin;
    Point position;
    Rect targetRectangle;
    Rect referenceRectangle;
    PointerGestureState::Enum state = PointerGestureState::Active;
    PopupToken popup;
    ControlToken control;
    f64 maximum = 0.0;
    u64 updateSequence = 0u;
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
inline constexpr usize s_InputMaxControlActions = 256u;
inline constexpr usize s_InputMaxContextMenuActions = 256u;

template<typename T>
using InputVector = Vector<T, Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

