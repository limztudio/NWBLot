// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "style.h"
#include "../paint.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_RadioGroupMaxChoices = 64u;

struct RadioGroupChoices;
struct RadioGroupOptions;
struct RadioGroupStyle;

struct RadioGroupMetrics{
    f32 rowHeight = 0.0f;
    f32 indicatorExtent = s_StyleControlExtent;
    f32 gap = s_StyleSpacingMedium;
    f32 rowGap = s_StyleSpacingSmall;
    f32 markInset = s_StyleMarkInset;
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
    [[nodiscard]] static Expected<RadioGroupMetrics> Measure(u32 count, const Point& maximumLabel, const RadioGroupOptions& options, const RadioGroupStyle& style)noexcept;
    [[nodiscard]] static Expected<RadioGroupPlacement> Place(const Rect& bounds, const Rect& clip, const RadioGroupChoices& choices, const RadioGroupMetrics& metrics)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

