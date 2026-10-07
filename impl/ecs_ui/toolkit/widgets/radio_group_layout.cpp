// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group_layout.h"

#include "radio_group.h"
#include "radio_group_style.h"

#include <impl/ecs_ui/toolkit/layout/validation.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidMetrics(const RadioGroupMetrics& metrics)noexcept{
    if(
        metrics.count > s_RadioGroupMaxChoices || !IsValidUiPadding(metrics.padding) || !IsValidUiExtent(metrics.contentSize)
        || !IsFinite(metrics.rowHeight) || metrics.rowHeight < s_RadioGroupMinimumRowHeight
        || !IsFinite(metrics.indicatorExtent) || metrics.indicatorExtent <= 0.0f || metrics.indicatorExtent > metrics.rowHeight
        || !IsFinite(metrics.gap) || metrics.gap < 0.0f || !IsFinite(metrics.rowGap) || metrics.rowGap < 0.0f
        || !IsFinite(metrics.markInset) || metrics.markInset < 0.0f || metrics.markInset > 0.5f
    )
        return false;
    const f64 height = static_cast<f64>(metrics.padding.top) + metrics.padding.bottom
        + static_cast<f64>(metrics.count) * metrics.rowHeight
        + static_cast<f64>(metrics.count == 0u ? 0u : metrics.count - 1u) * metrics.rowGap;
    const f64 minimumWidth = static_cast<f64>(metrics.padding.left) + metrics.padding.right
        + (metrics.count == 0u ? 0.0 : static_cast<f64>(metrics.indicatorExtent) + metrics.gap);
    return
        height <= Limit<f32>::s_Max && metrics.contentSize.y == static_cast<f32>(height)
        && minimumWidth <= Limit<f32>::s_Max && metrics.contentSize.x >= static_cast<f32>(minimumWidth)
    ;
}

[[nodiscard]] static bool MakeRect(const f64 x, const f64 y, const f64 width, const f64 height, Rect& out)noexcept{
    if(
        !IsFinite(x) || x < -Limit<f32>::s_Max || x > Limit<f32>::s_Max
        || !IsFinite(y) || y < -Limit<f32>::s_Max || y > Limit<f32>::s_Max
        || !IsFinite(width) || width < 0.0 || width > Limit<f32>::s_Max
        || !IsFinite(height) || height < 0.0 || height > Limit<f32>::s_Max
        || x + width < -Limit<f32>::s_Max || x + width > Limit<f32>::s_Max
        || y + height < -Limit<f32>::s_Max || y + height > Limit<f32>::s_Max
    )
        return false;
    const Rect candidate{ static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(width), static_cast<f32>(height) };
    if(
        !IsValidUiRect(candidate) || (width > 0.0 && candidate.x + candidate.width <= candidate.x)
        || (height > 0.0 && candidate.y + candidate.height <= candidate.y)
    )
        return false;
    out = candidate;
    return true;
}

[[nodiscard]] static bool Intersect(const Rect& lhs, const Rect& rhs, Rect& out)noexcept{
    const f64 left = Max(static_cast<f64>(lhs.x), static_cast<f64>(rhs.x));
    const f64 top = Max(static_cast<f64>(lhs.y), static_cast<f64>(rhs.y));
    const f64 right = Min(static_cast<f64>(lhs.x) + lhs.width, static_cast<f64>(rhs.x) + rhs.width);
    const f64 bottom = Min(static_cast<f64>(lhs.y) + lhs.height, static_cast<f64>(rhs.y) + rhs.height);
    return MakeRect(left, top, Max(0.0, right - left), Max(0.0, bottom - top), out);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RadioGroupLayout::Measure(
    const u32 count,
    const Point& maximumLabel,
    const RadioGroupOptions& options,
    const RadioGroupStyle& style,
    RadioGroupMetrics& out
)noexcept{
    using namespace __hidden_ui_radio_group_layout;
    if(
        count > s_RadioGroupMaxChoices || !IsValidUiExtent(maximumLabel) || !IsValidUiPadding(style.padding)
        || !IsFinite(options.rowHeight) || options.rowHeight < s_RadioGroupMinimumRowHeight
        || options.width.policy > LayoutSizePolicy::Stretch || !IsFinite(options.width.value) || options.width.value < 0.0f
        || (options.width.policy == LayoutSizePolicy::Stretch && options.width.value <= 0.0f)
        || !IsFinite(style.indicatorExtent) || style.indicatorExtent <= 0.0f || !IsFinite(style.gap) || style.gap < 0.0f
        || !IsFinite(style.rowGap) || style.rowGap < 0.0f
        || !IsFinite(style.markInset) || style.markInset < 0.0f || style.markInset > 0.5f
        || !IsValidUiColor(style.hoverTint) || !IsValidUiColor(style.pressedTint) || !IsValidUiColor(style.disabledTint)
    )
        return false;
    RadioGroupMetrics candidate;
    candidate.rowHeight = Max(options.rowHeight, Max(maximumLabel.y, style.indicatorExtent));
    candidate.indicatorExtent = style.indicatorExtent;
    candidate.gap = style.gap;
    candidate.rowGap = style.rowGap;
    candidate.markInset = style.markInset;
    candidate.padding = style.padding;
    candidate.count = count;
    const f64 width = static_cast<f64>(style.padding.left) + style.padding.right
        + (count == 0u ? 0.0 : static_cast<f64>(style.indicatorExtent) + style.gap + maximumLabel.x);
    const f64 height = static_cast<f64>(style.padding.top) + style.padding.bottom
        + static_cast<f64>(count) * candidate.rowHeight + static_cast<f64>(count == 0u ? 0u : count - 1u) * style.rowGap;
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return false;
    candidate.contentSize = { static_cast<f32>(width), static_cast<f32>(height) };
    if(!ValidMetrics(candidate))
        return false;
    out = candidate;
    return true;
}

bool RadioGroupLayout::Place(
    const Rect& bounds,
    const Rect& clip,
    const RadioGroupChoices& choices,
    const RadioGroupMetrics& metrics,
    RadioGroupPlacement& out
)noexcept{
    using namespace __hidden_ui_radio_group_layout;
    if(!IsValidUiRect(bounds) || !IsValidUiRect(clip) || !ValidMetrics(metrics) || choices.count != metrics.count)
        return false;
    RadioGroupPlacement candidate;
    candidate.bounds = bounds;
    candidate.count = choices.count;
    if(!Intersect(bounds, clip, candidate.clip))
        return false;
    const f64 left = Min(static_cast<f64>(metrics.padding.left), static_cast<f64>(bounds.width));
    const f64 top = Min(static_cast<f64>(metrics.padding.top), static_cast<f64>(bounds.height));
    const f64 right = Min(static_cast<f64>(metrics.padding.right), static_cast<f64>(bounds.width) - left);
    const f64 bottom = Min(static_cast<f64>(metrics.padding.bottom), static_cast<f64>(bounds.height) - top);
    const f64 x = static_cast<f64>(bounds.x) + left;
    const f64 y = static_cast<f64>(bounds.y) + top;
    const f64 width = static_cast<f64>(bounds.width) - left - right;
    if(!MakeRect(x, y, width, static_cast<f64>(bounds.height) - top - bottom, candidate.content))
        return false;
    const f64 extent = Min(static_cast<f64>(metrics.indicatorExtent), width);
    const f64 labelStart = Min(width, extent + metrics.gap);
    const f64 markInset = extent * metrics.markInset;
    for(u32 index = 0u; index < candidate.count; ++index){
        if(choices.rows[index].key == 0u)
            return false;
        for(u32 previous = 0u; previous < index; ++previous){
            if(choices.rows[previous].key == choices.rows[index].key)
                return false;
        }
        RadioGroupChoicePlacement& row = candidate.rows[index];
        row.key = choices.rows[index].key;
        row.enabled = choices.rows[index].enabled;
        const f64 rowY = y + static_cast<f64>(index) * (static_cast<f64>(metrics.rowHeight) + metrics.rowGap);
        const f64 indicatorY = rowY + (static_cast<f64>(metrics.rowHeight) - extent) * 0.5;
        if(
            !MakeRect(x, rowY, width, metrics.rowHeight, row.rectangle) || !Intersect(row.rectangle, candidate.clip, row.clip)
            || !MakeRect(x, indicatorY, extent, extent, row.indicator)
            || !MakeRect(x + markInset, indicatorY + markInset, extent - 2.0 * markInset, extent - 2.0 * markInset, row.mark)
        )
            return false;
        Rect label;
        if(
            !MakeRect(x + labelStart, rowY, width - labelStart, metrics.rowHeight, label)
            || !Intersect(label, row.clip, row.textClip)
        )
            return false;
    }
    out = candidate;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

