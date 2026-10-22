// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group_layout.h"

#include "radio_group.h"
#include "radio_group_style.h"

#include <impl/ecs_ui/toolkit/layout/rectangle.h>
#include <impl/ecs_ui/toolkit/rect_math.h>

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Metric rows 0..2 carry data; row 3 is padding.
inline constexpr u32 s_MetricRowCount = 3u;
inline constexpr f32 s_MaxMarkInset = 0.5f;
inline constexpr f64 s_MaxMarkInsetF64 = static_cast<f64>(s_MaxMarkInset);
// The mark loses its inset on both opposite sides.
inline constexpr f64 s_MarkInsetSides = 2.0;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SIMDMatrixDouble LoadMetrics(const RadioGroupMetrics& metrics)noexcept{
    if(
        metrics.count > s_RadioGroupMaxChoices
        || !IsFinite(metrics.padding.left)
        || !IsFinite(metrics.padding.top)
        || !IsFinite(metrics.padding.right)
        || !IsFinite(metrics.padding.bottom)
        || !IsFinite(metrics.rowHeight)
        || !IsFinite(metrics.indicatorExtent)
        || !IsFinite(metrics.gap)
        || !IsFinite(metrics.rowGap)
        || !IsFinite(metrics.contentSize.x)
        || !IsFinite(metrics.contentSize.y)
        || !IsFinite(metrics.markInset)
    )
        return SIMDMatrixDouble{ .v = { VectorDoubleReplicate(Limit<f64>::s_Infinity), VectorDoubleZero(), VectorDoubleZero(), VectorDoubleZero() } };
    return SIMDMatrixDouble{ .v = {
        VectorDoubleSet(metrics.padding.left, metrics.padding.top, metrics.padding.right, metrics.padding.bottom),
        VectorDoubleSet(metrics.rowHeight, metrics.indicatorExtent, metrics.gap, metrics.rowGap),
        VectorDoubleSet(metrics.contentSize.x, metrics.contentSize.y, metrics.markInset, metrics.count),
        VectorDoubleZero()
    } };
}

[[nodiscard]] static bool ValidMetrics(const SIMDMatrixDouble& values)noexcept{
    for(u32 index = 0u; index < s_MetricRowCount; ++index){
        if(
            VectorDoubleMoveMask(VectorDoubleOrInt(VectorDoubleIsNaN(values.v[index]), VectorDoubleIsInfinite(values.v[index]))) != 0u
            || VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(values.v[index], VectorDoubleZero())) != VectorDoubleComponentMask::s_DoubleXYZW
        )
            return false;
    }
    const u32 count = static_cast<u32>(VectorDoubleGetW(values.v[2]));
    const f64 rowHeight = VectorDoubleGetX(values.v[1]);
    const f64 indicatorExtent = VectorDoubleGetY(values.v[1]);
    if(count > s_RadioGroupMaxChoices || rowHeight <= 0.0 || indicatorExtent <= 0.0 || indicatorExtent > rowHeight || VectorDoubleGetZ(values.v[2]) > s_MaxMarkInsetF64)
        return false;
    const SIMDVectorDouble padding = VectorDoubleAdd(values.v[0], VectorDoubleSwizzle<2, 3, 2, 3>(values.v[0]));
    const SIMDVectorDouble rowExtents = VectorDoubleMultiply(VectorDoubleSet(count, count == 0u ? 0u : count - 1u, 0.0, 0.0), VectorDoubleSwizzle<0, 3, 0, 3>(values.v[1]));
    const SIMDVectorDouble labelWidth = count == 0u ? VectorDoubleZero() : VectorDoubleAdd(VectorDoubleSplatY(values.v[1]), VectorDoubleSplatZ(values.v[1]));
    const SIMDVectorDouble labelAndRows = VectorDoubleSelect(labelWidth, VectorDoubleSplatX(rowExtents), s_DoubleSIMDMaskY);
    const SIMDVectorDouble size = VectorDoubleAdd(VectorDoubleAdd(padding, labelAndRows), VectorDoubleSwizzle<2, 1, 2, 3>(rowExtents));
    const f64 height = VectorDoubleGetY(size);
    const f64 minimumWidth = VectorDoubleGetX(size);
    return height <= Limit<f32>::s_Max && VectorDoubleGetY(values.v[2]) == static_cast<f32>(height) && minimumWidth <= Limit<f32>::s_Max && VectorDoubleGetX(values.v[2]) >= static_cast<f32>(minimumWidth);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<RadioGroupMetrics> RadioGroupLayout::Measure(const u32 count, const Point& maximumLabel, const RadioGroupOptions& options, const RadioGroupStyle& style)noexcept{
    using namespace __hidden_ui_radio_group_layout;
    if(
        count > s_RadioGroupMaxChoices || !IsValidUiExtent(maximumLabel) || !IsValidUiPadding(style.padding)
        || options.width.policy > LayoutSizePolicy::Stretch || !IsFinite(options.width.value) || options.width.value < 0.0f
        || (options.width.policy == LayoutSizePolicy::Stretch && options.width.value <= 0.0f)
        || !IsFinite(style.indicatorExtent) || style.indicatorExtent <= 0.0f || !IsFinite(style.gap) || style.gap < 0.0f
        || !IsFinite(style.rowGap) || style.rowGap < 0.0f
        || !IsFinite(style.markInset) || style.markInset < 0.0f || style.markInset > s_MaxMarkInset
        || !IsValidUiColor(style.hoverTint) || !IsValidUiColor(style.pressedTint) || !IsValidUiColor(style.disabledTint)
    )
        return MakeUnexpected(Failure{});
    RadioGroupMetrics candidate;
    candidate.rowHeight = VectorGetY(VectorMax(VectorSet(maximumLabel.x, maximumLabel.y, 0.0f, 0.0f), VectorSet(maximumLabel.x, style.indicatorExtent, 0.0f, 0.0f)));
    candidate.indicatorExtent = style.indicatorExtent;
    candidate.gap = style.gap;
    candidate.rowGap = style.rowGap;
    candidate.markInset = style.markInset;
    candidate.padding = style.padding;
    candidate.count = count;
    const SIMDVectorDouble padding = VectorDoubleAdd(VectorDoubleSet(style.padding.left, style.padding.top, 0.0, 0.0), VectorDoubleSet(style.padding.right, style.padding.bottom, 0.0, 0.0));
    const SIMDVectorDouble rowExtents = VectorDoubleMultiply(VectorDoubleSet(count, count == 0u ? 0u : count - 1u, 0.0, 0.0), VectorDoubleSet(candidate.rowHeight, style.rowGap, 0.0, 0.0));
    const SIMDVectorDouble labelWidth = count == 0u ? VectorDoubleZero() : VectorDoubleAdd(VectorDoubleAdd(VectorDoubleReplicate(style.indicatorExtent), VectorDoubleReplicate(style.gap)), VectorDoubleReplicate(maximumLabel.x));
    const SIMDVectorDouble size = VectorDoubleAdd(VectorDoubleAdd(padding, VectorDoublePermute<0, 4, 2, 3>(labelWidth, rowExtents)), VectorDoubleSwizzle<2, 1, 2, 3>(rowExtents));
    const f64 width = VectorDoubleGetX(size);
    const f64 height = VectorDoubleGetY(size);
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    candidate.contentSize = { static_cast<f32>(width), static_cast<f32>(height) };
    if(!ValidMetrics(LoadMetrics(candidate)))
        return MakeUnexpected(Failure{});
    return candidate;
}

Expected<RadioGroupPlacement> RadioGroupLayout::Place(const Rect& bounds, const Rect& clip, const RadioGroupChoices& choices, const RadioGroupMetrics& metrics)noexcept{
    using namespace __hidden_ui_radio_group_layout;
    if(!IsValidUiRect(bounds) || !IsValidUiRect(clip) || !ValidMetrics(LoadMetrics(metrics)) || choices.count != metrics.count)
        return MakeUnexpected(Failure{});
    RadioGroupPlacement candidate;
    candidate.bounds = bounds;
    candidate.count = choices.count;
    const auto intersection = IntersectUiRects<UiRectPrecision::PreciseExtents>(bounds, clip);
    if(!intersection)
        return MakeUnexpected(Failure{});
    candidate.clip = *intersection;
    const SIMDVectorDouble geometryPair2Operand0 = VectorDoubleSet(static_cast<f64>(metrics.padding.left), static_cast<f64>(metrics.padding.top), 0.0, 0.0);
    const SIMDVectorDouble geometryPair2Operand1 = VectorDoubleSet(static_cast<f64>(bounds.width), static_cast<f64>(bounds.height), 0.0, 0.0);
    const SIMDVectorDouble leftTopValue = VectorDoubleSelect(geometryPair2Operand1, geometryPair2Operand0, VectorDoubleLess(geometryPair2Operand0, geometryPair2Operand1));
    const SIMDVectorDouble geometryPair3Operand0 = VectorDoubleSet(static_cast<f64>(metrics.padding.right), static_cast<f64>(metrics.padding.bottom), 0.0, 0.0);
    const SIMDVectorDouble geometryPair3Operand1 = VectorDoubleSubtract(VectorDoubleSet(static_cast<f64>(bounds.width), static_cast<f64>(bounds.height), 0.0, 0.0), VectorDoubleSelect(VectorDoubleZero(), leftTopValue, s_DoubleSIMDMaskXY));
    const SIMDVectorDouble rightBottomValue = VectorDoubleSelect(geometryPair3Operand1, geometryPair3Operand0, VectorDoubleLess(geometryPair3Operand0, geometryPair3Operand1));
    const SIMDVectorDouble xYValue = VectorDoubleAdd(VectorDoubleSet(static_cast<f64>(bounds.x), static_cast<f64>(bounds.y), 0.0, 0.0), VectorDoubleSelect(VectorDoubleZero(), leftTopValue, s_DoubleSIMDMaskXY));
    const SIMDVectorDouble contentSize = VectorDoubleSubtract(geometryPair3Operand1, rightBottomValue);
    const SIMDVectorDouble widthVector = VectorDoubleSplatX(contentSize);
    const auto content = MakeUiRect<UiRectPrecision::PreciseExtents>(VectorDoublePermute<0, 1, 4, 5>(xYValue, contentSize));
    if(!content)
        return MakeUnexpected(Failure{});
    candidate.content = *content;
    const SIMDVectorDouble indicatorExtent = VectorDoubleMin(VectorDoubleReplicate(metrics.indicatorExtent), widthVector);
    const SIMDVectorDouble labelOffset = VectorDoubleMin(widthVector, VectorDoubleAdd(indicatorExtent, VectorDoubleReplicate(metrics.gap)));
    const SIMDVectorDouble labelStart = VectorDoubleSelect(VectorDoubleZero(), labelOffset, s_DoubleSIMDMaskXY);
    const SIMDVectorDouble labelShift = VectorDoubleSelect(labelStart, VectorDoubleNegate(labelStart), s_DoubleSIMDMaskY);
    const SIMDVectorDouble markInset = VectorDoubleAndInt(VectorDoubleMultiply(indicatorExtent, VectorDoubleReplicate(metrics.markInset)), s_DoubleSIMDMaskXY);
    const SIMDVectorDouble rowHeightVector = VectorDoubleReplicate(metrics.rowHeight);
    const SIMDVectorDouble baseRowRect = VectorDoublePermute<0, 1, 4, 7>(xYValue, VectorDoubleSelect(widthVector, rowHeightVector, s_DoubleSIMDMaskW));
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
        const SIMDVectorDouble rowOffset = VectorDoubleMultiply(VectorDoubleReplicate(index), VectorDoubleAdd(rowHeightVector, VectorDoubleReplicate(metrics.rowGap)));
        const SIMDVectorDouble rowOrigin = VectorDoubleAdd(VectorDoubleSplatY(xYValue), rowOffset);
        const SIMDVectorDouble indicatorSpace = VectorDoubleSubtract(rowHeightVector, indicatorExtent);
        const SIMDVectorDouble indicatorY = VectorDoubleAdd(rowOrigin, VectorDoubleMultiply(indicatorSpace, VectorDoubleReplicate(s_RectCenterFractionF64)));
        const auto rectangle = MakeUiRect<UiRectPrecision::PreciseExtents>(VectorDoubleSelect(baseRowRect, rowOrigin, s_DoubleSIMDMaskY));
        if(!rectangle)
            return MakeUnexpected(Failure{});
        const auto rowClip = IntersectUiRects<UiRectPrecision::PreciseExtents>(*rectangle, candidate.clip);
        if(!rowClip)
            return MakeUnexpected(Failure{});
        const SIMDVectorDouble indicatorOrigin = VectorDoubleSelect(xYValue, indicatorY, s_DoubleSIMDMaskY);
        const auto indicator = MakeUiRect<UiRectPrecision::PreciseExtents>(VectorDoublePermute<0, 1, 4, 5>(indicatorOrigin, indicatorExtent));
        if(!indicator)
            return MakeUnexpected(Failure{});
        const SIMDVectorDouble markOrigin = VectorDoubleAdd(VectorDoubleAndInt(indicatorOrigin, s_DoubleSIMDMaskXY), markInset);
        const SIMDVectorDouble markSize = VectorDoubleSubtract(VectorDoubleAndInt(indicatorExtent, s_DoubleSIMDMaskXY), VectorDoubleMultiply(VectorDoubleSet(s_MarkInsetSides, s_MarkInsetSides, 0.0, 0.0), markInset));
        const auto mark = MakeUiRect<UiRectPrecision::PreciseExtents>(VectorDoublePermute<0, 1, 4, 5>(markOrigin, markSize));
        if(!mark)
            return MakeUnexpected(Failure{});
        const SIMDVectorDouble labelBounds = VectorDoubleAdd(VectorDoublePermute<0, 4, 2, 3>(xYValue, widthVector), labelShift);
        const auto label = MakeUiRect<UiRectPrecision::PreciseExtents>(VectorDoublePermute<0, 4, 1, 7>(labelBounds, VectorDoubleSelect(rowOrigin, rowHeightVector, s_DoubleSIMDMaskW)));
        if(!label)
            return MakeUnexpected(Failure{});
        const auto textClip = IntersectUiRects<UiRectPrecision::PreciseExtents>(*label, *rowClip);
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

