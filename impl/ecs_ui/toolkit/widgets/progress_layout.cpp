// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "progress_layout.h"

#include "progress.h"
#include "progress_style.h"

#include <impl/ecs_ui/toolkit/images/skin_region_geometry.h>
#include <impl/ecs_ui/toolkit/layout/rectangle.h>

#include <global/simd/result.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>
#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_progress_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Metric rows 0..2 carry data; row 3 is padding.
inline constexpr u32 s_MetricRowCount = 3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SIMDMatrixDouble LoadMetrics(const ProgressMetrics& metrics)noexcept{
    if(!IsFinite(metrics.padding.left) || !IsFinite(metrics.padding.top) || !IsFinite(metrics.padding.right) || !IsFinite(metrics.padding.bottom) || !IsFinite(metrics.fillMinimum.x) || !IsFinite(metrics.fillMinimum.y) || !IsFinite(metrics.contentSize.x) || !IsFinite(metrics.contentSize.y))
        return SIMDMatrixDouble{ .v = { VectorDoubleReplicate(Limit<f64>::s_Infinity), VectorDoubleZero(), VectorDoubleZero(), VectorDoubleZero() } };
    return SIMDMatrixDouble{ .v = {
        VectorDoubleSet(metrics.padding.left, metrics.padding.top, metrics.padding.right, metrics.padding.bottom),
        VectorDoubleSet(metrics.fillMinimum.x, metrics.fillMinimum.y, 0.0, 0.0),
        VectorDoubleSet(metrics.contentSize.x, metrics.contentSize.y, 0.0, 0.0),
        VectorDoubleZero()
    } };
}

[[nodiscard]] static bool ValidMetrics(const SIMDMatrixDouble& values)noexcept{
    for(u32 index = 0u; index < s_MetricRowCount; ++index){
        if(VectorDoubleMoveMask(VectorDoubleOrInt(VectorDoubleIsNaN(values.v[index]), VectorDoubleIsInfinite(values.v[index]))) != 0u
            || VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(values.v[index], VectorDoubleZero())) != VectorDoubleComponentMask::s_DoubleXYZW)
            return false;
    }
    const SIMDVectorDouble padding = VectorDoubleAdd(values.v[0], VectorDoubleSwizzle<2, 3, 2, 3>(values.v[0]));
    if(VectorDoubleGetX(padding) > Limit<f32>::s_Max || VectorDoubleGetY(padding) > Limit<f32>::s_Max)
        return false;
    const SIMDVectorDouble size = VectorDoubleAdd(padding, values.v[1]);
    const f64 width = VectorDoubleGetX(size);
    const f64 height = VectorDoubleGetY(size);
    return width <= Limit<f32>::s_Max && height <= Limit<f32>::s_Max && VectorDoubleGetX(values.v[2]) >= static_cast<f32>(width) && VectorDoubleGetY(values.v[2]) >= static_cast<f32>(height);
}

[[nodiscard]] static Expected<SIMDVectorSingleResult> LoadMinimumSize(const UiSkinRegion& region, const SIMDVectorDouble density)noexcept{
    const Insets padding{ region.padding.left, region.padding.top, region.padding.right, region.padding.bottom };
    if(!IsBoundedUiPadding(padding) || !IsFinite(region.minimumWidth) || region.minimumWidth < 0.0f || !IsFinite(region.minimumHeight) || region.minimumHeight < 0.0f)
        return MakeUnexpected(Failure{});
    const auto sliceExtents = LoadUiSkinSliceExtents(region);
    if(!sliceExtents)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble geometryPair1Operand0 = VectorDoubleSet(static_cast<f64>(region.minimumWidth), static_cast<f64>(region.minimumHeight), 0.0, 0.0);
    const SIMDVectorDouble geometryPair1Operand1 = VectorDoubleDivide(sliceExtents->value, density);
    const SIMDVectorDouble widthHeightValue = VectorDoubleSelect(geometryPair1Operand1, geometryPair1Operand0, VectorDoubleGreater(geometryPair1Operand0, geometryPair1Operand1));
    const f64 width = VectorDoubleGetX(widthHeightValue);
    const f64 height = VectorDoubleGetY(widthHeightValue);
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    return SIMDVectorSingleResult{ ConvertVectorDoubleToSingle(widthHeightValue) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ProgressMetrics> ProgressLayout::Measure(const ProgressOptions& options, const ProgressStyle& style, const UiSkinRegion& track, const UiSkinRegion& fill, const f32 density)noexcept{
    using namespace __hidden_ui_progress_layout;
    if(
        options.width.policy > LayoutSizePolicy::Stretch || !IsFinite(options.width.value) || options.width.value < 0.0f
        || (options.width.policy == LayoutSizePolicy::Stretch && options.width.value == 0.0f)
        || !IsFinite(options.height) || options.height < 0.0f || !IsBoundedUiPadding(style.padding)
        || !IsValidUiColor(style.trackTint) || !IsValidUiColor(style.fillTint) || !IsFinite(density) || density <= 0.0f
    )
        return MakeUnexpected(Failure{});
    const auto trackMinimum = LoadMinimumSize(track, VectorDoubleReplicate(density));
    if(!trackMinimum)
        return MakeUnexpected(Failure{});
    const auto fillMinimum = LoadMinimumSize(fill, VectorDoubleReplicate(density));
    if(!fillMinimum)
        return MakeUnexpected(Failure{});
    ProgressMetrics candidate;
    const SIMDVectorSingle stylePadding = VectorSet(style.padding.left, style.padding.top, style.padding.right, style.padding.bottom);
    const SIMDVectorSingle trackPadding = VectorSet(track.padding.left, track.padding.top, track.padding.right, track.padding.bottom);
    const SIMDVectorSingle padding = VectorSelect(trackPadding, stylePadding, VectorGreater(stylePadding, trackPadding));
    candidate.padding = { VectorGetX(padding), VectorGetY(padding), VectorGetZ(padding), VectorGetW(padding) };
    candidate.fillMinimum = { VectorGetX(fillMinimum->value), VectorGetY(fillMinimum->value) };
    const SIMDVectorDouble innerWidthInnerHeightValue = VectorDoubleAdd(
        VectorDoubleAdd(VectorDoubleSet(static_cast<f64>(candidate.padding.left), static_cast<f64>(candidate.padding.top), 0.0, 0.0), VectorDoubleSet(candidate.padding.right, candidate.padding.bottom, 0.0, 0.0)),
        VectorDoubleSet(VectorGetX(fillMinimum->value), VectorGetY(fillMinimum->value), 0.0, 0.0)
    );
    const f64 innerWidth = VectorDoubleGetX(innerWidthInnerHeightValue);
    const SIMDVectorDouble geometryPair5Operand0 = VectorDoubleSet(static_cast<f64>(VectorGetX(trackMinimum->value)), static_cast<f64>(options.height), 0.0, 0.0);
    const SIMDVectorDouble geometryPair5Operand1 = VectorDoubleMax(VectorDoubleSet(innerWidth, VectorGetY(trackMinimum->value), 0.0, 0.0), innerWidthInnerHeightValue);
    const SIMDVectorDouble widthHeightValue = VectorDoubleSelect(geometryPair5Operand1, geometryPair5Operand0, VectorDoubleGreater(geometryPair5Operand0, geometryPair5Operand1));
    const f64 width = VectorDoubleGetX(widthHeightValue);
    const f64 height = VectorDoubleGetY(widthHeightValue);
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    candidate.contentSize = { static_cast<f32>(width), static_cast<f32>(height) };
    if(!ValidMetrics(LoadMetrics(candidate)))
        return MakeUnexpected(Failure{});
    return candidate;
}

Expected<ProgressPlacement> ProgressLayout::Place(const Rect& bounds, const Rect& clip, const ProgressMetrics& metrics, const f64 fraction)noexcept{
    using namespace __hidden_ui_progress_layout;
    if(!IsBoundedUiRect(bounds) || !IsBoundedUiRect(clip) || !ValidMetrics(LoadMetrics(metrics)) || !IsFinite(fraction))
        return MakeUnexpected(Failure{});
    ProgressPlacement candidate;
    candidate.bounds = bounds;
    const auto intersection = IntersectUiRects<UiRectPrecision::EmptyPaint>(bounds, clip);
    if(!intersection)
        return MakeUnexpected(Failure{});
    candidate.clip = *intersection;
    const SIMDVectorDouble boundsSize = VectorDoubleSet(bounds.width, bounds.height, 0.0, 0.0);
    const SIMDVectorDouble paddingStart = VectorDoubleSet(metrics.padding.left, metrics.padding.top, 0.0, 0.0);
    const SIMDVectorDouble firstInset = VectorDoubleSelect(boundsSize, paddingStart, VectorDoubleLess(paddingStart, boundsSize));
    const SIMDVectorDouble remaining = VectorDoubleSubtract(boundsSize, firstInset);
    const SIMDVectorDouble paddingEnd = VectorDoubleSet(metrics.padding.right, metrics.padding.bottom, 0.0, 0.0);
    const SIMDVectorDouble lastInset = VectorDoubleSelect(remaining, paddingEnd, VectorDoubleLess(paddingEnd, remaining));
    const SIMDVectorDouble origin = VectorDoubleAdd(VectorDoubleSet(bounds.x, bounds.y, 0.0, 0.0), firstInset);
    const SIMDVectorDouble size = VectorDoubleSubtract(remaining, lastInset);
    const auto content = MakeUiRect<UiRectPrecision::EmptyPaint>(VectorDoublePermute<0, 1, 4, 5>(origin, size));
    if(!content)
        return MakeUnexpected(Failure{});
    candidate.content = *content;
    const f64 normalized = VectorDoubleGetX(VectorDoubleClamp(VectorDoubleReplicate(fraction), VectorDoubleZero(), VectorDoubleSplatOne()));
    candidate.fillReveal = candidate.content;
    candidate.fillCanvas = candidate.content;
    if(normalized == 0.0){
        candidate.fillReveal.width = 0.0f;
        candidate.fillCanvas.width = 0.0f;
    }
    else if(normalized != 1.0){
        const auto fillReveal = MakeUiRect<UiRectPrecision::EmptyPaint>(VectorDoubleSet(candidate.content.x, candidate.content.y, VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(candidate.content.width), VectorDoubleReplicate(normalized))), candidate.content.height));
        if(!fillReveal)
            return MakeUnexpected(Failure{});
        candidate.fillReveal = *fillReveal;
        const f64 canvasWidth = candidate.fillReveal.width == 0.0f ? 0.0 : VectorDoubleGetX(VectorDoubleMin(VectorDoubleReplicate(candidate.content.width), VectorDoubleMax(VectorDoubleReplicate(candidate.fillReveal.width), VectorDoubleReplicate(metrics.fillMinimum.x))));
        const auto fillCanvas = MakeUiRect<UiRectPrecision::EmptyPaint>(VectorDoubleSet(candidate.content.x, candidate.content.y, canvasWidth, candidate.content.height));
        if(!fillCanvas)
            return MakeUnexpected(Failure{});
        candidate.fillCanvas = *fillCanvas;
    }
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

