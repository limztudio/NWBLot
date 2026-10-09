// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group_layout.h"

#include "radio_group.h"
#include "radio_group_style.h"

#include <impl/ecs_ui/toolkit/layout/validation.h>

#include <global/math/vector_double.h>
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
    const SIMDVectorDouble padding = SIMDVectorDouble{ metrics.padding.left, metrics.padding.top }
        + SIMDVectorDouble{ metrics.padding.right, metrics.padding.bottom };
    const f64 height = padding.y + static_cast<f64>(metrics.count) * metrics.rowHeight
        + static_cast<f64>(metrics.count == 0u ? 0u : metrics.count - 1u) * metrics.rowGap;
    const f64 minimumWidth = padding.x
        + (metrics.count == 0u ? 0.0 : static_cast<f64>(metrics.indicatorExtent) + metrics.gap);
    return
        height <= Limit<f32>::s_Max && metrics.contentSize.y == static_cast<f32>(height)
        && minimumWidth <= Limit<f32>::s_Max && metrics.contentSize.x >= static_cast<f32>(minimumWidth)
    ;
}

[[nodiscard]] static Expected<Rect> MakeRect(const f64 x, const f64 y, const f64 width, const f64 height)noexcept{
    if(
        !IsFinite(x) || x < -Limit<f32>::s_Max || x > Limit<f32>::s_Max
        || !IsFinite(y) || y < -Limit<f32>::s_Max || y > Limit<f32>::s_Max
        || !IsFinite(width) || width < 0.0 || width > Limit<f32>::s_Max
        || !IsFinite(height) || height < 0.0 || height > Limit<f32>::s_Max
        || x + width < -Limit<f32>::s_Max || x + width > Limit<f32>::s_Max
        || y + height < -Limit<f32>::s_Max || y + height > Limit<f32>::s_Max
    )
        return MakeUnexpected(Failure{});
    const Rect candidate{ static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(width), static_cast<f32>(height) };
    if(
        !IsValidUiRect(candidate) || (width > 0.0 && candidate.x + candidate.width <= candidate.x)
        || (height > 0.0 && candidate.y + candidate.height <= candidate.y)
    )
        return MakeUnexpected(Failure{});
    return candidate;
}

[[nodiscard]] static Expected<Rect> Intersect(const Rect& lhs, const Rect& rhs)noexcept{
    const SIMDVectorDouble geometryPair0Operand0 = SIMDVectorDouble{ static_cast<f64>(lhs.x), static_cast<f64>(lhs.y) };
    const SIMDVectorDouble geometryPair0Operand1 = SIMDVectorDouble{ static_cast<f64>(rhs.x), static_cast<f64>(rhs.y) };
    const SIMDVectorDouble leftTopValue = ((geometryPair0Operand0 > geometryPair0Operand1) ? geometryPair0Operand0 : geometryPair0Operand1);
    const f64 left = leftTopValue.x;
    const f64 top = leftTopValue.y;
    const SIMDVectorDouble geometryPair1Operand0 = (SIMDVectorDouble{ static_cast<f64>(lhs.x), static_cast<f64>(lhs.y) } + SIMDVectorDouble{ lhs.width, lhs.height });
    const SIMDVectorDouble geometryPair1Operand1 = (SIMDVectorDouble{ static_cast<f64>(rhs.x), static_cast<f64>(rhs.y) } + SIMDVectorDouble{ rhs.width, rhs.height });
    const SIMDVectorDouble rightBottomValue = ((geometryPair1Operand0 < geometryPair1Operand1) ? geometryPair1Operand0 : geometryPair1Operand1);
    const f64 right = rightBottomValue.x;
    const f64 bottom = rightBottomValue.y;
    return MakeRect(left, top, Max(0.0, right - left), Max(0.0, bottom - top));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<RadioGroupMetrics> RadioGroupLayout::Measure(
    const u32 count,
    const Point& maximumLabel,
    const RadioGroupOptions& options,
    const RadioGroupStyle& style
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
        return MakeUnexpected(Failure{});
    RadioGroupMetrics candidate;
    candidate.rowHeight = Max(options.rowHeight, Max(maximumLabel.y, style.indicatorExtent));
    candidate.indicatorExtent = style.indicatorExtent;
    candidate.gap = style.gap;
    candidate.rowGap = style.rowGap;
    candidate.markInset = style.markInset;
    candidate.padding = style.padding;
    candidate.count = count;
    const SIMDVectorDouble padding = SIMDVectorDouble{ style.padding.left, style.padding.top }
        + SIMDVectorDouble{ style.padding.right, style.padding.bottom };
    const f64 width = padding.x
        + (count == 0u ? 0.0 : static_cast<f64>(style.indicatorExtent) + style.gap + maximumLabel.x);
    const f64 height = padding.y + static_cast<f64>(count) * candidate.rowHeight
        + static_cast<f64>(count == 0u ? 0u : count - 1u) * style.rowGap;
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    candidate.contentSize = { static_cast<f32>(width), static_cast<f32>(height) };
    if(!ValidMetrics(candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}

Expected<RadioGroupPlacement> RadioGroupLayout::Place(
    const Rect& bounds,
    const Rect& clip,
    const RadioGroupChoices& choices,
    const RadioGroupMetrics& metrics
)noexcept{
    using namespace __hidden_ui_radio_group_layout;
    if(!IsValidUiRect(bounds) || !IsValidUiRect(clip) || !ValidMetrics(metrics) || choices.count != metrics.count)
        return MakeUnexpected(Failure{});
    RadioGroupPlacement candidate;
    candidate.bounds = bounds;
    candidate.count = choices.count;
    const auto intersection = Intersect(bounds, clip);
    if(!intersection)
        return MakeUnexpected(Failure{});
    candidate.clip = *intersection;
    const SIMDVectorDouble geometryPair2Operand0 = SIMDVectorDouble{ static_cast<f64>(metrics.padding.left), static_cast<f64>(metrics.padding.top) };
    const SIMDVectorDouble geometryPair2Operand1 = SIMDVectorDouble{ static_cast<f64>(bounds.width), static_cast<f64>(bounds.height) };
    const SIMDVectorDouble leftTopValue = ((geometryPair2Operand0 < geometryPair2Operand1) ? geometryPair2Operand0 : geometryPair2Operand1);
    const f64 left = leftTopValue.x;
    const f64 top = leftTopValue.y;
    const SIMDVectorDouble geometryPair3Operand0 = SIMDVectorDouble{ static_cast<f64>(metrics.padding.right), static_cast<f64>(metrics.padding.bottom) };
    const SIMDVectorDouble geometryPair3Operand1 = (SIMDVectorDouble{ static_cast<f64>(bounds.width), static_cast<f64>(bounds.height) } - SIMDVectorDouble{ left, top });
    const SIMDVectorDouble rightBottomValue = ((geometryPair3Operand0 < geometryPair3Operand1) ? geometryPair3Operand0 : geometryPair3Operand1);
    const f64 right = rightBottomValue.x;
    const f64 bottom = rightBottomValue.y;
    const SIMDVectorDouble xYValue = (SIMDVectorDouble{ static_cast<f64>(bounds.x), static_cast<f64>(bounds.y) } + SIMDVectorDouble{ left, top });
    const f64 x = xYValue.x;
    const f64 y = xYValue.y;
    const f64 width = static_cast<f64>(bounds.width) - left - right;
    const auto content = MakeRect(x, y, width, static_cast<f64>(bounds.height) - top - bottom);
    if(!content)
        return MakeUnexpected(Failure{});
    candidate.content = *content;
    const f64 extent = Min(static_cast<f64>(metrics.indicatorExtent), width);
    const f64 labelStart = Min(width, extent + metrics.gap);
    const f64 markInset = extent * metrics.markInset;
    for(u32 index = 0u; index < candidate.count; ++index){
        if(choices.rows[index].key == 0u)
            return MakeUnexpected(Failure{});
        for(u32 previous = 0u; previous < index; ++previous){
            if(choices.rows[previous].key == choices.rows[index].key)
                return MakeUnexpected(Failure{});
        }
        RadioGroupChoicePlacement& row = candidate.rows[index];
        row.key = choices.rows[index].key;
        row.enabled = choices.rows[index].enabled;
        const f64 rowY = y + static_cast<f64>(index) * (static_cast<f64>(metrics.rowHeight) + metrics.rowGap);
        const f64 indicatorY = rowY + (static_cast<f64>(metrics.rowHeight) - extent) * 0.5;
        const auto rectangle = MakeRect(x, rowY, width, metrics.rowHeight);
        if(!rectangle)
            return MakeUnexpected(Failure{});
        const auto rowClip = Intersect(*rectangle, candidate.clip);
        if(!rowClip)
            return MakeUnexpected(Failure{});
        const auto indicator = MakeRect(x, indicatorY, extent, extent);
        if(!indicator)
            return MakeUnexpected(Failure{});
        const auto mark = MakeRect(x + markInset, indicatorY + markInset, extent - 2.0 * markInset, extent - 2.0 * markInset);
        if(!mark)
            return MakeUnexpected(Failure{});
        const auto label = MakeRect(x + labelStart, rowY, width - labelStart, metrics.rowHeight);
        if(!label)
            return MakeUnexpected(Failure{});
        const auto textClip = Intersect(*label, *rowClip);
        if(!textClip)
            return MakeUnexpected(Failure{});
        row.rectangle = *rectangle;
        row.clip = *rowClip;
        row.indicator = *indicator;
        row.mark = *mark;
        row.textClip = *textClip;
    }
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

