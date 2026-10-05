// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../paint.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_RadioGroupMaxChoices = 64u;
inline constexpr f32 s_RadioGroupMinimumRowHeight = 32.0f;

struct RadioGroupChoices;
struct RadioGroupOptions;
struct RadioGroupStyle;

struct RadioGroupMetrics{
    f32 rowHeight = s_RadioGroupMinimumRowHeight;
    f32 indicatorExtent = 24.0f;
    f32 gap = 8.0f;
    f32 rowGap = 4.0f;
    f32 markInset = 0.3f;
    Insets padding;
    // Complete intrinsic group size, including its outer padding.
    Point contentSize;
    u32 count = 0u;
};

struct RadioGroupChoicePlacement{
    u64 key = 0u;
    bool enabled = false;
    Rect rectangle;
    Rect clip;
    Rect indicator;
    Rect mark;
    Rect textClip;
};

struct RadioGroupPlacement{
    Rect bounds;
    Rect clip;
    Rect content;
    Array<RadioGroupChoicePlacement, s_RadioGroupMaxChoices> rows{};
    u32 count = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RadioGroupLayout final{
public:
    // A rejected measurement or placement preserves the caller's previous output.
    [[nodiscard]] static bool measure(
        u32 count,
        const Point& maximumLabel,
        const RadioGroupOptions& options,
        const RadioGroupStyle& style,
        RadioGroupMetrics& out
    );
    [[nodiscard]] static bool place(
        const Rect& bounds,
        const Rect& clip,
        const RadioGroupChoices& choices,
        const RadioGroupMetrics& metrics,
        RadioGroupPlacement& out
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

