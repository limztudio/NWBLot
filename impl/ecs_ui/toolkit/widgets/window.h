// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../paint.h"
#include <impl/ecs_ui/toolkit/widgets/style.adhd.h>
#include <impl/ecs_ui/toolkit/input/input.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


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

