// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider_layout.h"

#include "slider.h"
#include "slider_style.h"

#include <impl/ecs_ui/toolkit/layout/rectangle.h>

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SIMDMatrixDouble LoadMetrics(const SliderMetrics& metrics)noexcept{
    if(!IsFinite(metrics.padding.left) || !IsFinite(metrics.padding.top) || !IsFinite(metrics.padding.right) || !IsFinite(metrics.padding.bottom) || !IsFinite(metrics.thumbExtent.x) || !IsFinite(metrics.thumbExtent.y) || !IsFinite(metrics.trackHeight) || !IsFinite(metrics.contentSize.x) || !IsFinite(metrics.contentSize.y))
        return SIMDMatrixDouble{ .v = { VectorDoubleReplicate(Limit<f64>::s_Infinity), VectorDoubleZero(), VectorDoubleZero(), VectorDoubleZero() } };
    return SIMDMatrixDouble{ .v = {
        VectorDoubleSet(metrics.padding.left, metrics.padding.top, metrics.padding.right, metrics.padding.bottom),
        VectorDoubleSet(metrics.thumbExtent.x, metrics.thumbExtent.y, metrics.trackHeight, 0.0),
        VectorDoubleSet(metrics.contentSize.x, metrics.contentSize.y, 0.0, 0.0),
        VectorDoubleZero()
    } };
}

[[nodiscard]] static bool ValidMetrics(const SIMDMatrixDouble& values)noexcept{
    if(VectorDoubleMoveMask(VectorDoubleOrInt(VectorDoubleIsNaN(values.v[0]), VectorDoubleIsInfinite(values.v[0]))) != 0u
        || VectorDoubleMoveMask(VectorDoubleOrInt(VectorDoubleIsNaN(values.v[1]), VectorDoubleIsInfinite(values.v[1]))) != 0u
        || VectorDoubleMoveMask(VectorDoubleOrInt(VectorDoubleIsNaN(values.v[2]), VectorDoubleIsInfinite(values.v[2]))) != 0u
        || VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(values.v[0], VectorDoubleZero())) != VectorDoubleComponentMask::s_DoubleXYZW
        || (VectorDoubleMoveMask(VectorDoubleGreater(values.v[1], VectorDoubleZero())) & VectorDoubleComponentMask::s_DoubleXYZ) != VectorDoubleComponentMask::s_DoubleXYZ)
        return false;
    const SIMDVectorDouble padding = VectorDoubleAdd(values.v[0], VectorDoubleSwizzle<2, 3, 2, 3>(values.v[0]));
    const SIMDVectorDouble size = VectorDoubleAdd(padding, VectorDoubleMax(values.v[1], VectorDoubleSwizzle<0, 2, 0, 2>(values.v[1])));
    const f64 width = VectorDoubleGetX(size);
    const f64 height = VectorDoubleGetY(size);
    return width <= Limit<f32>::s_Max && height <= Limit<f32>::s_Max && VectorDoubleGetX(values.v[2]) >= static_cast<f32>(width) && VectorDoubleGetY(values.v[2]) >= static_cast<f32>(height);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SliderMetrics> SliderLayout::Measure(const SliderOptions& options, const SliderStyle& style)noexcept{
    using namespace __hidden_ui_slider_layout;
    if(
        !SliderBehavior::Validate(options) || !IsValidUiPadding(style.padding)
        || !IsFinite(style.thumbExtent.x) || style.thumbExtent.x <= 0.0f
        || !IsFinite(style.thumbExtent.y) || style.thumbExtent.y <= 0.0f
        || !IsFinite(style.trackHeight) || style.trackHeight <= 0.0f
        || !IsValidUiColor(style.hoverTint) || !IsValidUiColor(style.pressedTint) || !IsValidUiColor(style.disabledTint)
    )
        return MakeUnexpected(Failure{});
    SliderMetrics candidate;
    candidate.padding = style.padding;
    candidate.thumbExtent = style.thumbExtent;
    candidate.trackHeight = style.trackHeight;
    const SIMDVectorDouble padding = VectorDoubleAdd(VectorDoubleSet(style.padding.left, style.padding.top, 0.0, 0.0), VectorDoubleSet(style.padding.right, style.padding.bottom, 0.0, 0.0));
    const SIMDVectorDouble intrinsic = VectorDoubleAdd(padding, VectorDoubleMax(VectorDoubleSet(style.thumbExtent.x, style.thumbExtent.y, 0.0, 0.0), VectorDoubleSet(style.thumbExtent.x, style.trackHeight, 0.0, 0.0)));
    const SIMDVectorDouble minimum = VectorDoubleSet(VectorDoubleGetX(intrinsic), options.height, 0.0, 0.0);
    const SIMDVectorDouble size = VectorDoubleSelect(intrinsic, minimum, VectorDoubleGreater(minimum, intrinsic));
    const f64 width = VectorDoubleGetX(size);
    const f64 height = VectorDoubleGetY(size);
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    candidate.contentSize = { static_cast<f32>(width), static_cast<f32>(height) };
    if(!ValidMetrics(LoadMetrics(candidate)))
        return MakeUnexpected(Failure{});
    return candidate;
}

Expected<SliderPlacement> SliderLayout::Place(const Rect& bounds, const Rect& clip, const SliderMetrics& metrics, const f64 normalized)noexcept{
    using namespace __hidden_ui_slider_layout;
    if(!IsPreciseUiRect(bounds) || !IsPreciseUiRect(clip) || !ValidMetrics(LoadMetrics(metrics)) || !IsFinite(normalized) || normalized < 0.0 || normalized > 1.0)
        return MakeUnexpected(Failure{});
    SliderPlacement candidate;
    candidate.bounds = bounds;
    const auto intersection = IntersectUiRects<UiRectPrecision::PreciseExtents>(bounds, clip);
    if(!intersection)
        return MakeUnexpected(Failure{});
    candidate.clip = *intersection;
    const SIMDVectorDouble boundsSize = VectorDoubleSet(bounds.width, bounds.height, 0.0, 0.0);
    const SIMDVectorDouble paddingStart = VectorDoubleSet(metrics.padding.left, metrics.padding.top, 0.0, 0.0);
    const SIMDVectorDouble firstInset = VectorDoubleSelect(boundsSize, paddingStart, VectorDoubleLess(paddingStart, boundsSize));
    const SIMDVectorDouble remaining = VectorDoubleSubtract(boundsSize, firstInset);
    const SIMDVectorDouble paddingEnd = VectorDoubleSet(metrics.padding.right, metrics.padding.bottom, 0.0, 0.0);
    const SIMDVectorDouble lastInset = VectorDoubleSelect(remaining, paddingEnd, VectorDoubleLess(paddingEnd, remaining));
    const SIMDVectorDouble xYValue = VectorDoubleAdd(VectorDoubleSet(bounds.x, bounds.y, 0.0, 0.0), firstInset);
    const f64 x = VectorDoubleGetX(xYValue);
    const f64 y = VectorDoubleGetY(xYValue);
    const SIMDVectorDouble widthHeightValue = VectorDoubleSubtract(remaining, lastInset);
    const f64 width = VectorDoubleGetX(widthHeightValue);
    const f64 height = VectorDoubleGetY(widthHeightValue);
    const SIMDVectorDouble geometryPair4Operand0 = VectorDoubleSet(static_cast<f64>(metrics.thumbExtent.x), static_cast<f64>(metrics.thumbExtent.y), 0.0, 0.0);
    const SIMDVectorDouble geometryPair4Operand1 = VectorDoubleSelect(VectorDoubleZero(), widthHeightValue, s_DoubleSIMDMaskXY);
    const SIMDVectorDouble thumbWidthThumbHeightValue = VectorDoubleSelect(geometryPair4Operand1, geometryPair4Operand0, VectorDoubleLess(geometryPair4Operand0, geometryPair4Operand1));
    const f64 thumbWidth = VectorDoubleGetX(thumbWidthThumbHeightValue);
    const f64 thumbHeight = VectorDoubleGetY(thumbWidthThumbHeightValue);
    const f64 trackHeight = VectorDoubleGetY(VectorDoubleMin(VectorDoubleSet(width, metrics.trackHeight, 0.0, 0.0), widthHeightValue));
    const f64 travel = VectorDoubleGetX(VectorDoubleSubtract(widthHeightValue, thumbWidthThumbHeightValue));
    const SIMDVectorDouble offsets = VectorDoubleMultiply(VectorDoubleSet(thumbWidth, travel, 0.0, 0.0), VectorDoubleSet(0.5, normalized, 0.0, 0.0));
    const SIMDVectorDouble centers = VectorDoubleAdd(VectorDoubleReplicate(x), offsets);
    const f64 centerStart = VectorDoubleGetX(centers);
    const f64 thumbX = normalized == 0.0 ? x : normalized == 1.0 ? VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(x), VectorDoubleReplicate(travel))) : VectorDoubleGetY(centers);
    const auto travelBounds = MakeUiRect<UiRectPrecision::PreciseExtents>(x, y, width, height);
    if(!travelBounds)
        return MakeUnexpected(Failure{});
    const auto centerTravel = MakeUiRect<UiRectPrecision::PreciseExtents>(centerStart, y, travel, height);
    if(!centerTravel)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble centeredY = VectorDoubleAdd(VectorDoubleReplicate(y), VectorDoubleMultiply(VectorDoubleSubtract(VectorDoubleReplicate(height), VectorDoubleSet(trackHeight, thumbHeight, height, height)), VectorDoubleReplicate(0.5)));
    const auto track = MakeUiRect<UiRectPrecision::PreciseExtents>(centerStart, VectorDoubleGetX(centeredY), travel, trackHeight);
    if(!track)
        return MakeUnexpected(Failure{});
    const auto thumb = MakeUiRect<UiRectPrecision::PreciseExtents>(thumbX, VectorDoubleGetY(centeredY), thumbWidth, thumbHeight);
    if(!thumb)
        return MakeUnexpected(Failure{});
    candidate.travelBounds = *travelBounds;
    candidate.centerTravel = *centerTravel;
    candidate.track = *track;
    candidate.thumb = *thumb;
    candidate.thumbExtent = { static_cast<f32>(thumbWidth), static_cast<f32>(thumbHeight) };
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

