// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider_layout.h"

#include "slider.h"
#include "slider_style.h"

#include <impl/ecs_ui/toolkit/layout/rectangle.h>

#include <global/math/vector_double.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidMetrics(const SliderMetrics& metrics)noexcept{
    if(
        !IsValidUiPadding(metrics.padding) || !IsFinite(metrics.thumbExtent.x) || metrics.thumbExtent.x <= 0.0f
        || !IsFinite(metrics.thumbExtent.y) || metrics.thumbExtent.y <= 0.0f
        || !IsFinite(metrics.trackHeight) || metrics.trackHeight <= 0.0f
        || !IsFinite(metrics.contentSize.x) || !IsFinite(metrics.contentSize.y)
    )
        return false;
    const SIMDVectorDouble padding = SIMDVectorDouble{ metrics.padding.left, metrics.padding.top }
        + SIMDVectorDouble{ metrics.padding.right, metrics.padding.bottom };
    const SIMDVectorDouble size = padding + SIMDVectorDouble{ metrics.thumbExtent.x,
        Max(static_cast<f64>(metrics.thumbExtent.y), static_cast<f64>(metrics.trackHeight)) };
    const f64 width = size.x;
    const f64 height = size.y;
    return
        width <= Limit<f32>::s_Max && height <= Limit<f32>::s_Max
        && metrics.contentSize.x >= static_cast<f32>(width) && metrics.contentSize.y >= static_cast<f32>(height)
    ;
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
    const f64 width = static_cast<f64>(style.padding.left) + style.padding.right + style.thumbExtent.x;
    const f64 height = Max(static_cast<f64>(options.height), static_cast<f64>(style.padding.top) + style.padding.bottom
        + Max(static_cast<f64>(style.thumbExtent.y), static_cast<f64>(style.trackHeight)));
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    candidate.contentSize = { static_cast<f32>(width), static_cast<f32>(height) };
    if(!ValidMetrics(candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}

Expected<SliderPlacement> SliderLayout::Place(
    const Rect& bounds,
    const Rect& clip,
    const SliderMetrics& metrics,
    const f64 normalized
)noexcept{
    using namespace __hidden_ui_slider_layout;
    if(
        !IsPreciseUiRect(bounds) || !IsPreciseUiRect(clip) || !ValidMetrics(metrics)
        || !IsFinite(normalized) || normalized < 0.0 || normalized > 1.0
    )
        return MakeUnexpected(Failure{});
    SliderPlacement candidate;
    candidate.bounds = bounds;
    const auto intersection = IntersectUiRects<UiRectPrecision::PreciseExtents>(bounds, clip);
    if(!intersection)
        return MakeUnexpected(Failure{});
    candidate.clip = *intersection;
    const f64 leftInset = Min(static_cast<f64>(metrics.padding.left), static_cast<f64>(bounds.width));
    const SIMDVectorDouble geometryPair0Operand0 = SIMDVectorDouble{ static_cast<f64>(metrics.padding.right), static_cast<f64>(metrics.padding.top) };
    const SIMDVectorDouble geometryPair0Operand1 = SIMDVectorDouble{ (static_cast<f64>(bounds.width) - leftInset), static_cast<f64>(bounds.height) };
    const SIMDVectorDouble rightInsetTopInsetValue = ((geometryPair0Operand0 < geometryPair0Operand1) ? geometryPair0Operand0 : geometryPair0Operand1);
    const f64 rightInset = rightInsetTopInsetValue.x;
    const f64 topInset = rightInsetTopInsetValue.y;
    const f64 bottomInset = Min(static_cast<f64>(metrics.padding.bottom), static_cast<f64>(bounds.height) - topInset);
    const SIMDVectorDouble xYValue = (SIMDVectorDouble{ static_cast<f64>(bounds.x), static_cast<f64>(bounds.y) } + SIMDVectorDouble{ leftInset, topInset });
    const f64 x = xYValue.x;
    const f64 y = xYValue.y;
    const SIMDVectorDouble widthHeightValue = ((SIMDVectorDouble{ static_cast<f64>(bounds.width), static_cast<f64>(bounds.height) } - SIMDVectorDouble{ leftInset, topInset }) - SIMDVectorDouble{ rightInset, bottomInset });
    const f64 width = widthHeightValue.x;
    const f64 height = widthHeightValue.y;
    const SIMDVectorDouble geometryPair4Operand0 = SIMDVectorDouble{ static_cast<f64>(metrics.thumbExtent.x), static_cast<f64>(metrics.thumbExtent.y) };
    const SIMDVectorDouble geometryPair4Operand1 = SIMDVectorDouble{ width, height };
    const SIMDVectorDouble thumbWidthThumbHeightValue = ((geometryPair4Operand0 < geometryPair4Operand1) ? geometryPair4Operand0 : geometryPair4Operand1);
    const f64 thumbWidth = thumbWidthThumbHeightValue.x;
    const f64 thumbHeight = thumbWidthThumbHeightValue.y;
    const f64 trackHeight = Min(static_cast<f64>(metrics.trackHeight), height);
    const f64 travel = width - thumbWidth;
    const f64 centerStart = x + thumbWidth * 0.5;
    const f64 thumbX = normalized == 0.0 ? x : normalized == 1.0 ? x + travel : x + travel * normalized;
    const auto travelBounds = MakeUiRect<UiRectPrecision::PreciseExtents>(x, y, width, height);
    if(!travelBounds)
        return MakeUnexpected(Failure{});
    const auto centerTravel = MakeUiRect<UiRectPrecision::PreciseExtents>(centerStart, y, travel, height);
    if(!centerTravel)
        return MakeUnexpected(Failure{});
    const auto track = MakeUiRect<UiRectPrecision::PreciseExtents>(centerStart, y + (height - trackHeight) * 0.5, travel, trackHeight);
    if(!track)
        return MakeUnexpected(Failure{});
    const auto thumb = MakeUiRect<UiRectPrecision::PreciseExtents>(thumbX, y + (height - thumbHeight) * 0.5, thumbWidth, thumbHeight);
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

