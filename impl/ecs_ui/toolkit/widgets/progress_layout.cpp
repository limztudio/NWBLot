// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "progress_layout.h"

#include <impl/ecs_ui/toolkit/images/skin_region_geometry.h>
#include <impl/ecs_ui/toolkit/layout/rectangle.h>
#include <impl/ecs_ui/toolkit/rect_math.h>

#include <global/simd/result.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>
#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_progress_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SIMDMatrixDouble LoadMetrics(const ProgressMetrics& metrics)noexcept{
    if(
        !IsFinite(metrics.padding.left) || !IsFinite(metrics.padding.top) || !IsFinite(metrics.padding.right) || !IsFinite(metrics.padding.bottom)
        || !IsFinite(metrics.fillMinimum.x) || !IsFinite(metrics.fillMinimum.y) || !IsFinite(metrics.contentSize.x) || !IsFinite(metrics.contentSize.y)
    )
        return MakeInvalidUiMatrixDouble();
    return SIMDMatrixDouble{ .v = {
        VectorDoubleSet(metrics.padding.left, metrics.padding.top, metrics.padding.right, metrics.padding.bottom),
        VectorDoubleSet(metrics.fillMinimum.x, metrics.fillMinimum.y, 0.0, 0.0),
        VectorDoubleSet(metrics.contentSize.x, metrics.contentSize.y, 0.0, 0.0),
        VectorDoubleZero()
    } };
}

[[nodiscard]] static b8 ValidMetrics(const SIMDMatrixDouble& values)noexcept{
    if(!AreUiMatrixRowsNonNegativeFinite(values, s_ProgressMetricRowCount))
        return false;
    const SIMDVectorDouble padding = VectorDoubleAdd(values.v[0], VectorDoubleSwizzle<2, 3, 2, 3>(values.v[0]));
    if(VectorDoubleGetX(padding) > Limit<f32>::s_Max || VectorDoubleGetY(padding) > Limit<f32>::s_Max)
        return false;
    const SIMDVectorDouble size = VectorDoubleAdd(padding, values.v[1]);
    const f64 width = VectorDoubleGetX(size);
    const f64 height = VectorDoubleGetY(size);
    return width <= Limit<f32>::s_Max && height <= Limit<f32>::s_Max && VectorDoubleGetX(values.v[2]) >= static_cast<f32>(width) && VectorDoubleGetY(values.v[2]) >= static_cast<f32>(height);
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
    if(
        !IsBoundedUiPadding(Insets{ track.padding.left, track.padding.top, track.padding.right, track.padding.bottom })
        || !IsBoundedUiPadding(Insets{ fill.padding.left, fill.padding.top, fill.padding.right, fill.padding.bottom })
    )
        return MakeUnexpected(Failure{});
    const auto trackMinimum = LoadUiSkinMinimumSize(track, VectorDoubleReplicate(density));
    if(!trackMinimum)
        return MakeUnexpected(Failure{});
    const auto fillMinimum = LoadUiSkinMinimumSize(fill, VectorDoubleReplicate(density));
    if(!fillMinimum)
        return MakeUnexpected(Failure{});
    ProgressMetrics candidate;
    candidate.padding = MaxUiPadding(style.padding, track.padding);
    candidate.fillMinimum = { VectorGetX(fillMinimum->value), VectorGetY(fillMinimum->value) };
    const SIMDVectorDouble innerWidthInnerHeightValue = VectorDoubleAdd(
        VectorDoubleAdd(VectorDoubleSet(static_cast<f64>(candidate.padding.left), static_cast<f64>(candidate.padding.top), 0.0, 0.0), VectorDoubleSet(candidate.padding.right, candidate.padding.bottom, 0.0, 0.0)),
        VectorDoubleAndInt(ConvertVectorSingleToDouble(fillMinimum->value), s_DoubleSIMDMaskXY)
    );
    const SIMDVectorDouble trackMinimumDouble = ConvertVectorSingleToDouble(trackMinimum->value);
    const SIMDVectorDouble geometryPair5Operand0 = VectorDoubleSelect(VectorDoubleSet(0.0, static_cast<f64>(options.height), 0.0, 0.0), trackMinimumDouble, s_DoubleSIMDMaskX);
    const SIMDVectorDouble geometryPair5Operand1 = VectorDoubleMax(VectorDoubleSelect(innerWidthInnerHeightValue, trackMinimumDouble, s_DoubleSIMDMaskY), innerWidthInnerHeightValue);
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
    const auto content = InsetUiRect<UiRectPrecision::EmptyPaint>(bounds, metrics.padding);
    if(!content)
        return MakeUnexpected(Failure{});
    candidate.content = *content;
    const f64 normalized = Clamp(fraction, 0.0, 1.0);
    const SIMDVectorDouble normalizedVector = VectorDoubleReplicate(normalized);
    candidate.fillReveal = candidate.content;
    candidate.fillCanvas = candidate.content;
    if(normalized == 0.0){
        candidate.fillReveal.width = 0.0f;
        candidate.fillCanvas.width = 0.0f;
    }
    else if(normalized != 1.0){
        const SIMDVectorDouble contentVector = VectorDoubleSet(candidate.content.x, candidate.content.y, candidate.content.width, candidate.content.height);
        const SIMDVectorDouble revealWidth = VectorDoubleMultiply(VectorDoubleSplatZ(contentVector), normalizedVector);
        const auto fillReveal = MakeUiRect<UiRectPrecision::EmptyPaint>(VectorDoubleSelect(contentVector, revealWidth, s_DoubleSIMDMaskZ));
        if(!fillReveal)
            return MakeUnexpected(Failure{});
        candidate.fillReveal = *fillReveal;
        const SIMDVectorDouble canvasWidth = candidate.fillReveal.width == 0.0f
            ? VectorDoubleZero()
            : VectorDoubleMin(VectorDoubleSplatZ(contentVector), VectorDoubleMax(VectorDoubleReplicate(candidate.fillReveal.width), VectorDoubleReplicate(metrics.fillMinimum.x)))
        ;
        const auto fillCanvas = MakeUiRect<UiRectPrecision::EmptyPaint>(VectorDoubleSelect(contentVector, canvasWidth, s_DoubleSIMDMaskZ));
        if(!fillCanvas)
            return MakeUnexpected(Failure{});
        candidate.fillCanvas = *fillCanvas;
    }
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

