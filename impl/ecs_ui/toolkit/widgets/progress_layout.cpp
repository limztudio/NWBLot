// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "progress_layout.h"

#include "progress.h"
#include "progress_style.h"

#include <impl/ecs_ui/toolkit/layout/validation.h>

#include <global/math/vector_double.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_progress_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidPadding(const Insets& padding)noexcept{
    return
        IsFinite(padding.left) && padding.left >= 0.0f && IsFinite(padding.top) && padding.top >= 0.0f
        && IsFinite(padding.right) && padding.right >= 0.0f && IsFinite(padding.bottom) && padding.bottom >= 0.0f
        && static_cast<f64>(padding.left) + padding.right <= Limit<f32>::s_Max
        && static_cast<f64>(padding.top) + padding.bottom <= Limit<f32>::s_Max
    ;
}

[[nodiscard]] static bool ValidMetrics(const ProgressMetrics& metrics)noexcept{
    if(!ValidPadding(metrics.padding) || !IsValidUiExtent(metrics.fillMinimum) || !IsValidUiExtent(metrics.contentSize))
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
        region.rectangle.width == 0u || region.rectangle.height == 0u || !ValidPadding(padding)
        || !IsFinite(region.minimumWidth) || region.minimumWidth < 0.0f
        || !IsFinite(region.minimumHeight) || region.minimumHeight < 0.0f
    )
        return MakeUnexpected(Failure{});
    const u64 horizontalSlices = static_cast<u64>(region.sliceInsets.left) + region.sliceInsets.right;
    const u64 verticalSlices = static_cast<u64>(region.sliceInsets.top) + region.sliceInsets.bottom;
    if(region.drawMode == UiSkinDrawMode::Sprite){
        if(horizontalSlices != 0u || verticalSlices != 0u)
            return MakeUnexpected(Failure{});
    }
    else if(region.drawMode == UiSkinDrawMode::NineSlice){
        if(horizontalSlices > region.rectangle.width || verticalSlices > region.rectangle.height)
            return MakeUnexpected(Failure{});
    }
    else
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble geometryPair1Operand0 = SIMDVectorDouble{ static_cast<f64>(region.minimumWidth), static_cast<f64>(region.minimumHeight) };
    const SIMDVectorDouble geometryPair1Operand1 = (SIMDVectorDouble{ static_cast<f64>(horizontalSlices), static_cast<f64>(verticalSlices) } / SIMDVectorDouble{ density, density });
    const SIMDVectorDouble widthHeightValue = ((geometryPair1Operand0 > geometryPair1Operand1) ? geometryPair1Operand0 : geometryPair1Operand1);
    const f64 width = widthHeightValue.x;
    const f64 height = widthHeightValue.y;
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    return Point{ static_cast<f32>(width), static_cast<f32>(height) };
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
    Rect candidate{ static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(width), static_cast<f32>(height) };
    // Derived slivers below coordinate precision are valid empty paint geometry.
    if(candidate.width > 0.0f && candidate.x + candidate.width <= candidate.x)
        candidate.width = 0.0f;
    if(candidate.height > 0.0f && candidate.y + candidate.height <= candidate.y)
        candidate.height = 0.0f;
    if(!IsBoundedUiRect(candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}

[[nodiscard]] static Expected<Rect> Intersect(const Rect& lhs, const Rect& rhs)noexcept{
    const SIMDVectorDouble geometryPair2Operand0 = SIMDVectorDouble{ static_cast<f64>(lhs.x), static_cast<f64>(lhs.y) };
    const SIMDVectorDouble geometryPair2Operand1 = SIMDVectorDouble{ static_cast<f64>(rhs.x), static_cast<f64>(rhs.y) };
    const SIMDVectorDouble leftTopValue = ((geometryPair2Operand0 > geometryPair2Operand1) ? geometryPair2Operand0 : geometryPair2Operand1);
    const f64 left = leftTopValue.x;
    const f64 top = leftTopValue.y;
    const SIMDVectorDouble geometryPair3Operand0 = (SIMDVectorDouble{ static_cast<f64>(lhs.x), static_cast<f64>(lhs.y) } + SIMDVectorDouble{ lhs.width, lhs.height });
    const SIMDVectorDouble geometryPair3Operand1 = (SIMDVectorDouble{ static_cast<f64>(rhs.x), static_cast<f64>(rhs.y) } + SIMDVectorDouble{ rhs.width, rhs.height });
    const SIMDVectorDouble rightBottomValue = ((geometryPair3Operand0 < geometryPair3Operand1) ? geometryPair3Operand0 : geometryPair3Operand1);
    const f64 right = rightBottomValue.x;
    const f64 bottom = rightBottomValue.y;
    return MakeRect(left, top, Max(0.0, right - left), Max(0.0, bottom - top));
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
        || !IsFinite(options.height) || options.height < 0.0f || !ValidPadding(style.padding)
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
    const auto intersection = Intersect(bounds, clip);
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
    const auto content = MakeRect(
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
        const auto fillReveal = MakeRect(
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
        const auto fillCanvas = MakeRect(candidate.content.x, candidate.content.y, canvasWidth, candidate.content.height);
        if(!fillCanvas)
            return MakeUnexpected(Failure{});
        candidate.fillCanvas = *fillCanvas;
    }
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

