// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "progress_layout.h"

#include "progress.h"
#include "progress_style.h"

#include <impl/ecs_ui/toolkit/layout/validation.h>

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
    const f64 width = static_cast<f64>(metrics.padding.left) + metrics.padding.right + metrics.fillMinimum.x;
    const f64 height = static_cast<f64>(metrics.padding.top) + metrics.padding.bottom + metrics.fillMinimum.y;
    return
        width <= Limit<f32>::s_Max && height <= Limit<f32>::s_Max
        && metrics.contentSize.x >= static_cast<f32>(width) && metrics.contentSize.y >= static_cast<f32>(height)
    ;
}

[[nodiscard]] static bool MinimumSize(const UiSkinRegion& region, const f32 density, Point& out)noexcept{
    const Insets padding{ region.padding.left, region.padding.top, region.padding.right, region.padding.bottom };
    if(
        region.rectangle.width == 0u || region.rectangle.height == 0u || !ValidPadding(padding)
        || !IsFinite(region.minimumWidth) || region.minimumWidth < 0.0f
        || !IsFinite(region.minimumHeight) || region.minimumHeight < 0.0f
    )
        return false;
    const u64 horizontalSlices = static_cast<u64>(region.sliceInsets.left) + region.sliceInsets.right;
    const u64 verticalSlices = static_cast<u64>(region.sliceInsets.top) + region.sliceInsets.bottom;
    if(region.drawMode == UiSkinDrawMode::Sprite){
        if(horizontalSlices != 0u || verticalSlices != 0u)
            return false;
    }
    else if(region.drawMode == UiSkinDrawMode::NineSlice){
        if(horizontalSlices > region.rectangle.width || verticalSlices > region.rectangle.height)
            return false;
    }
    else
        return false;
    const f64 width = Max(static_cast<f64>(region.minimumWidth), static_cast<f64>(horizontalSlices) / density);
    const f64 height = Max(static_cast<f64>(region.minimumHeight), static_cast<f64>(verticalSlices) / density);
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return false;
    out = { static_cast<f32>(width), static_cast<f32>(height) };
    return true;
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
    Rect candidate{ static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(width), static_cast<f32>(height) };
    // Derived slivers below coordinate precision are valid empty paint geometry.
    if(candidate.width > 0.0f && candidate.x + candidate.width <= candidate.x)
        candidate.width = 0.0f;
    if(candidate.height > 0.0f && candidate.y + candidate.height <= candidate.y)
        candidate.height = 0.0f;
    if(!IsBoundedUiRect(candidate))
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


bool ProgressLayout::Measure(
    const ProgressOptions& options,
    const ProgressStyle& style,
    const UiSkinRegion& track,
    const UiSkinRegion& fill,
    const f32 density,
    ProgressMetrics& out
)noexcept{
    using namespace __hidden_ui_progress_layout;
    if(
        options.width.policy > LayoutSizePolicy::Stretch || !IsFinite(options.width.value) || options.width.value < 0.0f
        || (options.width.policy == LayoutSizePolicy::Stretch && options.width.value == 0.0f)
        || !IsFinite(options.height) || options.height < 0.0f || !ValidPadding(style.padding)
        || !IsValidUiColor(style.trackTint) || !IsValidUiColor(style.fillTint) || !IsFinite(density) || density <= 0.0f
    )
        return false;
    Point trackMinimum;
    Point fillMinimum;
    if(!MinimumSize(track, density, trackMinimum) || !MinimumSize(fill, density, fillMinimum))
        return false;
    ProgressMetrics candidate;
    candidate.padding = {
        Max(style.padding.left, track.padding.left),
        Max(style.padding.top, track.padding.top),
        Max(style.padding.right, track.padding.right),
        Max(style.padding.bottom, track.padding.bottom)
    };
    candidate.fillMinimum = fillMinimum;
    const f64 innerWidth = static_cast<f64>(candidate.padding.left) + candidate.padding.right + fillMinimum.x;
    const f64 innerHeight = static_cast<f64>(candidate.padding.top) + candidate.padding.bottom + fillMinimum.y;
    const f64 width = Max(static_cast<f64>(trackMinimum.x), innerWidth);
    const f64 height = Max(static_cast<f64>(options.height), Max(static_cast<f64>(trackMinimum.y), innerHeight));
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return false;
    candidate.contentSize = { static_cast<f32>(width), static_cast<f32>(height) };
    if(!ValidMetrics(candidate))
        return false;
    out = candidate;
    return true;
}

bool ProgressLayout::Place(
    const Rect& bounds,
    const Rect& clip,
    const ProgressMetrics& metrics,
    const f64 fraction,
    ProgressPlacement& out
)noexcept{
    using namespace __hidden_ui_progress_layout;
    if(!IsBoundedUiRect(bounds) || !IsBoundedUiRect(clip) || !ValidMetrics(metrics) || !IsFinite(fraction))
        return false;
    ProgressPlacement candidate;
    candidate.bounds = bounds;
    if(!Intersect(bounds, clip, candidate.clip))
        return false;
    const f64 leftInset = Min(static_cast<f64>(metrics.padding.left), static_cast<f64>(bounds.width));
    const f64 rightInset = Min(static_cast<f64>(metrics.padding.right), static_cast<f64>(bounds.width) - leftInset);
    const f64 topInset = Min(static_cast<f64>(metrics.padding.top), static_cast<f64>(bounds.height));
    const f64 bottomInset = Min(static_cast<f64>(metrics.padding.bottom), static_cast<f64>(bounds.height) - topInset);
    if(!MakeRect(
        static_cast<f64>(bounds.x) + leftInset,
        static_cast<f64>(bounds.y) + topInset,
        static_cast<f64>(bounds.width) - leftInset - rightInset,
        static_cast<f64>(bounds.height) - topInset - bottomInset,
        candidate.content
    ))
        return false;
    const f64 normalized = Min(1.0, Max(0.0, fraction));
    candidate.fillReveal = candidate.content;
    candidate.fillCanvas = candidate.content;
    if(normalized == 0.0){
        candidate.fillReveal.width = 0.0f;
        candidate.fillCanvas.width = 0.0f;
    }
    else if(normalized != 1.0){
        if(!MakeRect(
            candidate.content.x,
            candidate.content.y,
            static_cast<f64>(candidate.content.width) * normalized,
            candidate.content.height,
            candidate.fillReveal
        ))
            return false;
        const f64 canvasWidth = candidate.fillReveal.width == 0.0f ? 0.0 : Min(
            static_cast<f64>(candidate.content.width),
            Max(static_cast<f64>(candidate.fillReveal.width), static_cast<f64>(metrics.fillMinimum.x))
        );
        if(!MakeRect(candidate.content.x, candidate.content.y, canvasWidth, candidate.content.height, candidate.fillCanvas))
            return false;
    }
    out = candidate;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

