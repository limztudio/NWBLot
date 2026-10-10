// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "progress_layout.h"

#include "progress.h"
#include "progress_style.h"

#include <impl/ecs_ui/toolkit/images/skin_region_geometry.h>
#include <impl/ecs_ui/toolkit/layout/rectangle.h>

#include <global/math/vector_double.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_progress_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidMetrics(const ProgressMetrics& metrics)noexcept{
    if(!IsBoundedUiPadding(metrics.padding) || !IsValidUiExtent(metrics.fillMinimum) || !IsValidUiExtent(metrics.contentSize))
        return false;
    const SIMDVectorDouble widthHeightValue = ((SIMDVectorDouble{ static_cast<f64>(metrics.padding.left), static_cast<f64>(metrics.padding.top) } + SIMDVectorDouble{ metrics.padding.right, metrics.padding.bottom }) + SIMDVectorDouble{ metrics.fillMinimum.x, metrics.fillMinimum.y });
    const f64 width = widthHeightValue.x;
    const f64 height = widthHeightValue.y;
    return
        width <= Limit<f32>::s_Max && height <= Limit<f32>::s_Max
        && metrics.contentSize.x >= static_cast<f32>(width) && metrics.contentSize.y >= static_cast<f32>(height)
    ;
}

[[nodiscard]] static Expected<Point> MinimumSize(const UiSkinRegion& region, const f32 density)noexcept{
    const Insets padding{ region.padding.left, region.padding.top, region.padding.right, region.padding.bottom };
    if(
        !IsBoundedUiPadding(padding)
        || !IsFinite(region.minimumWidth) || region.minimumWidth < 0.0f
        || !IsFinite(region.minimumHeight) || region.minimumHeight < 0.0f
    )
        return MakeUnexpected(Failure{});
    const auto sliceExtents = LoadUiSkinSliceExtents(region);
    if(!sliceExtents)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble geometryPair1Operand0 = SIMDVectorDouble{ static_cast<f64>(region.minimumWidth), static_cast<f64>(region.minimumHeight) };
    const SIMDVectorDouble geometryPair1Operand1 = (*sliceExtents / SIMDVectorDouble{ density, density });
    const SIMDVectorDouble widthHeightValue = ((geometryPair1Operand0 > geometryPair1Operand1) ? geometryPair1Operand0 : geometryPair1Operand1);
    const f64 width = widthHeightValue.x;
    const f64 height = widthHeightValue.y;
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    return Point{ static_cast<f32>(width), static_cast<f32>(height) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ProgressMetrics> ProgressLayout::Measure(
    const ProgressOptions& options,
    const ProgressStyle& style,
    const UiSkinRegion& track,
    const UiSkinRegion& fill,
    const f32 density
)noexcept{
    using namespace __hidden_ui_progress_layout;
    if(
        options.width.policy > LayoutSizePolicy::Stretch || !IsFinite(options.width.value) || options.width.value < 0.0f
        || (options.width.policy == LayoutSizePolicy::Stretch && options.width.value == 0.0f)
        || !IsFinite(options.height) || options.height < 0.0f || !IsBoundedUiPadding(style.padding)
        || !IsValidUiColor(style.trackTint) || !IsValidUiColor(style.fillTint) || !IsFinite(density) || density <= 0.0f
    )
        return MakeUnexpected(Failure{});
    const auto trackMinimum = MinimumSize(track, density);
    if(!trackMinimum)
        return MakeUnexpected(Failure{});
    const auto fillMinimum = MinimumSize(fill, density);
    if(!fillMinimum)
        return MakeUnexpected(Failure{});
    ProgressMetrics candidate;
    candidate.padding = {
        Max(style.padding.left, track.padding.left),
        Max(style.padding.top, track.padding.top),
        Max(style.padding.right, track.padding.right),
        Max(style.padding.bottom, track.padding.bottom)
    };
    candidate.fillMinimum = *fillMinimum;
    const SIMDVectorDouble innerWidthInnerHeightValue = ((SIMDVectorDouble{ static_cast<f64>(candidate.padding.left), static_cast<f64>(candidate.padding.top) } + SIMDVectorDouble{ candidate.padding.right, candidate.padding.bottom }) + SIMDVectorDouble{ fillMinimum->x, fillMinimum->y });
    const f64 innerWidth = innerWidthInnerHeightValue.x;
    const f64 innerHeight = innerWidthInnerHeightValue.y;
    const SIMDVectorDouble geometryPair5Operand0 = SIMDVectorDouble{ static_cast<f64>(trackMinimum->x), static_cast<f64>(options.height) };
    const SIMDVectorDouble geometryPair5Operand1 = SIMDVectorDouble{ innerWidth, Max(static_cast<f64>(trackMinimum->y), innerHeight) };
    const SIMDVectorDouble widthHeightValue = ((geometryPair5Operand0 > geometryPair5Operand1) ? geometryPair5Operand0 : geometryPair5Operand1);
    const f64 width = widthHeightValue.x;
    const f64 height = widthHeightValue.y;
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    candidate.contentSize = { static_cast<f32>(width), static_cast<f32>(height) };
    if(!ValidMetrics(candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}

Expected<ProgressPlacement> ProgressLayout::Place(
    const Rect& bounds,
    const Rect& clip,
    const ProgressMetrics& metrics,
    const f64 fraction
)noexcept{
    using namespace __hidden_ui_progress_layout;
    if(!IsBoundedUiRect(bounds) || !IsBoundedUiRect(clip) || !ValidMetrics(metrics) || !IsFinite(fraction))
        return MakeUnexpected(Failure{});
    ProgressPlacement candidate;
    candidate.bounds = bounds;
    const auto intersection = IntersectUiRects<UiRectPrecision::EmptyPaint>(bounds, clip);
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
    const auto content = MakeUiRect<UiRectPrecision::EmptyPaint>(
        static_cast<f64>(bounds.x) + leftInset,
        static_cast<f64>(bounds.y) + topInset,
        static_cast<f64>(bounds.width) - leftInset - rightInset,
        static_cast<f64>(bounds.height) - topInset - bottomInset
    );
    if(!content)
        return MakeUnexpected(Failure{});
    candidate.content = *content;
    const f64 normalized = Min(1.0, Max(0.0, fraction));
    candidate.fillReveal = candidate.content;
    candidate.fillCanvas = candidate.content;
    if(normalized == 0.0){
        candidate.fillReveal.width = 0.0f;
        candidate.fillCanvas.width = 0.0f;
    }
    else if(normalized != 1.0){
        const auto fillReveal = MakeUiRect<UiRectPrecision::EmptyPaint>(
            candidate.content.x,
            candidate.content.y,
            static_cast<f64>(candidate.content.width) * normalized,
            candidate.content.height
        );
        if(!fillReveal)
            return MakeUnexpected(Failure{});
        candidate.fillReveal = *fillReveal;
        const f64 canvasWidth = candidate.fillReveal.width == 0.0f ? 0.0 : Min(
            static_cast<f64>(candidate.content.width),
            Max(static_cast<f64>(candidate.fillReveal.width), static_cast<f64>(metrics.fillMinimum.x))
        );
        const auto fillCanvas = MakeUiRect<UiRectPrecision::EmptyPaint>(candidate.content.x, candidate.content.y, canvasWidth, candidate.content.height);
        if(!fillCanvas)
            return MakeUnexpected(Failure{});
        candidate.fillCanvas = *fillCanvas;
    }
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

