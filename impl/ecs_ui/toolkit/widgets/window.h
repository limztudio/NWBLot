// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../paint.h"
#include <impl/ecs_ui/toolkit/widgets/style.adhd.h>
#include <impl/ecs_ui/toolkit/input/input.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Gesture baselines stay in the host model; frozen draw snapshots never borrow WindowState.
struct WindowGestureState{
    InputActionId id;
    Rect initialBounds;
};

struct WindowState{
    Rect bounds;
    WindowGestureState moveGesture;
    WindowGestureState resizeGesture;
    b8 initialized = false;
    b8 collapsed = false;
};

inline constexpr Rect s_WindowDefaultBounds = { 20.0f, 20.0f, 360.0f, 280.0f };
inline constexpr Point s_WindowDefaultMinimumSize = { 160.0f, 80.0f };

struct WindowOptions{
    Rect initialBounds = s_WindowDefaultBounds;
    Point minimumSize = s_WindowDefaultMinimumSize;
    LayoutDirection::Enum direction = LayoutDirection::Column;
    b8 contentWidthFirstUse = false;
    b8 contentHeightFirstUse = false;
    b8 movable = true;
    b8 resizable = true;
    b8 collapsible = true;
};

struct WindowMetrics{
    Insets contentPadding;
    Insets titlePadding;
    Point minimumSize;
    f32 titleHeight = 0.0f;
    f32 collapseExtent = 0.0f;
    f32 resizeExtent = 0.0f;
};

namespace SeparatorDirection{
    enum Enum : u8{ Horizontal, Vertical };
};

struct SeparatorOptions{
    LayoutSize length = { LayoutSizePolicy::Stretch, 1.0f };
    // Zero uses the skin's minimum thickness, or one logical unit when no minimum is specified.
    f32 thickness = 0.0f;
    SeparatorDirection::Enum direction = SeparatorDirection::Horizontal;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class WindowBehavior final{
public:
    [[nodiscard]] static b8 Initialize(WindowState& state, const WindowOptions& options, const WindowMetrics& metrics)noexcept;
    [[nodiscard]] static b8 ApplyMove(WindowState& state, const PointerGesture& gesture)noexcept;
    [[nodiscard]] static b8 ApplyResize(WindowState& state, const PointerGesture& gesture, const Point& minimumSize)noexcept;
    [[nodiscard]] static b8 Constrain(WindowState& state, const DisplayMetrics& display, f32 titleHeight)noexcept;
};

class WindowLayout final{
public:
    [[nodiscard]] static Expected<WindowMetrics> Measure(
        const UiSkinRegion& frame, const UiSkinRegion& title, const UiSkinRegion* collapse,
        const UiSkinRegion* resize, const WidgetStyle& style, const WindowOptions& options,
        const Point& titleSize, f32 density
    )noexcept;
    [[nodiscard]] static Rect Visible(const WindowState& state, const WindowMetrics& metrics)noexcept;
    [[nodiscard]] static Rect Content(const WindowState& state, const WindowMetrics& metrics)noexcept;
    [[nodiscard]] static Rect Collapse(const WindowState& state, const WindowMetrics& metrics)noexcept;
    [[nodiscard]] static Rect Resize(const WindowState& state, const WindowMetrics& metrics)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

