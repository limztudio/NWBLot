// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "commands.h"
#include "../id.h"
#include "../paint.h"
#include "popup.h"
#include "control.h"

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace InputEventType{
    enum Enum : u8{
        PointerMove, PrimaryDown, PrimaryUp, KeyDown, KeyUp, FocusLost, PointerLeave, PointerCaptureLost, FocusGained, PointerWheel,
        SecondaryDown, SecondaryUp, CommandDown, CommandUp
    };
};

namespace InputActionSource{
    enum Enum : u8{ Pointer, Keyboard, Command };
};

namespace PointerGestureState{
    enum Enum : u8{ Active, Completed };
};

struct InputEvent{
    InputEventType::Enum type = InputEventType::PointerMove;
    Point position{};
    i32 key = Core::Key::Unknown;
    bool shift = false;
    bool repeat = false;
    bool control = false;
    bool alt = false;
    f64 scrollX = 0.0;
    f64 scrollY = 0.0;
    // Optional monotonic timestamp at native ingress; zero leaves pointer clicks ungrouped.
    u64 timestampMs = 0u;
    InputSource source{};
    InputCommand::Enum command = InputCommand::None;
    bool extend = false;
    bool edit = true;
    bool super = false;
    bool allowText = false;
};

// Rectangle and clip use the same logical coordinates as painting; publication order provides the default focus traversal order.
// Members are ordered by alignment (8-byte, then 4-byte, then 1-byte) to minimize padding.
struct HitTarget{
    WidgetId id;
    u64 declarationGeneration = 1u;
    PopupToken popup;
    ControlToken control;
    WidgetId owner;
    u64 ownerDeclarationGeneration = 0u;
    u64 value = 0u;
    f64 scrollStep = 0.0;
    u64 pageRows = 1u;
    f64 gestureMaximum = 0.0;
    WidgetId keyboardOwner;
    u64 keyboardOwnerDeclarationGeneration = 0u;
    ControlToken keyboardControl;
    f64 scrollStepX = 0.0;
    f64 gestureMaximumX = 0.0;
    Rect rectangle;
    Rect clip;
    // Both zero dimensions omit the reference; a supplied reference has two positive dimensions.
    Rect gestureReference{};
    u32 paintOrder = 0u;
    u32 layer = 0u;
    bool enabled = true;
    bool focusable = false;
    bool activatable = false;
    bool pointerGesture = false;
    bool textEditable = false;
    bool navigable = false;
    bool scrollable = false;
    bool focusOnCommit = false;
    // A focused editor may borrow vertical navigation and an Enter intention from another accepted control lifetime.
    bool contextMenu = false;
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
    // Copied once from the accepted target at the initial press; later updates retain these opaque bits.
    u64 value = 0u;
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
inline constexpr usize s_InputMaxSources = 256u;
inline constexpr usize s_InputMaxActions = 256u;
inline constexpr usize s_InputMaxPointerGestures = 256u;
inline constexpr usize s_InputMaxControlActions = 256u;
inline constexpr usize s_InputMaxContextMenuActions = 256u;

template<typename T>
using InputVector = Vector<T, Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

