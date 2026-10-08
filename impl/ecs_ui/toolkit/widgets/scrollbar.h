// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../paint.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ScrollAxis{
    enum Enum : u8{ Horizontal, Vertical };
};

struct ScrollbarPlacement{
    Rect track;
    Rect thumb;
    f64 contentExtent = 0.0;
    f64 viewportExtent = 0.0;
    f64 maximum = 0.0;
    f64 offset = 0.0;
    bool visible = false;
};

struct ScrollViewportPlacement{
    Rect viewport;
    Rect contentClip;
    Rect corner;
    ScrollbarPlacement horizontal;
    ScrollbarPlacement vertical;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ScrollbarLayout final{
public:
    // Resolve coupled bar visibility in logical coordinates.
    [[nodiscard]] static Expected<ScrollViewportPlacement> Calculate(
        const Rect& bounds,
        const Rect& clip,
        const Insets& padding,
        const Point& contentMeasure,
        f32 caretWidth,
        const Point& previousScroll,
        f32 thickness,
        f32 minThumb
    )noexcept;
    // Update only clamped offsets and thumb positions after caret reveal; extents and reserved geometry remain fixed.
    [[nodiscard]] static bool UpdateOffsets(const Point& scroll, ScrollViewportPlacement& out)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

