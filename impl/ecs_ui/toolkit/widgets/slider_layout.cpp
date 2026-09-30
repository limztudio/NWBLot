// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider_layout.h"

#include "slider.h"
#include "slider_style.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidRect(const Rect& rectangle){
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y)
        && IsFinite(rectangle.width) && rectangle.width >= 0.0f
        && IsFinite(rectangle.height) && rectangle.height >= 0.0f
        && IsFinite(rectangle.x + rectangle.width) && IsFinite(rectangle.y + rectangle.height)
        && (rectangle.width == 0.0f || rectangle.x + rectangle.width > rectangle.x)
        && (rectangle.height == 0.0f || rectangle.y + rectangle.height > rectangle.y)
    ;
}

[[nodiscard]] static bool ValidPadding(const Insets& padding){
    return
        IsFinite(padding.left) && padding.left >= 0.0f && IsFinite(padding.top) && padding.top >= 0.0f
        && IsFinite(padding.right) && padding.right >= 0.0f && IsFinite(padding.bottom) && padding.bottom >= 0.0f
    ;
}

[[nodiscard]] static bool ValidColor(const Color& color){
    return
        IsFinite(color.r) && color.r >= 0.0f && IsFinite(color.g) && color.g >= 0.0f
        && IsFinite(color.b) && color.b >= 0.0f && IsFinite(color.a) && color.a >= 0.0f && color.a <= 1.0f
    ;
}

[[nodiscard]] static bool ValidMetrics(const SliderMetrics& metrics){
    if(
        !ValidPadding(metrics.padding) || !IsFinite(metrics.thumbExtent.x) || metrics.thumbExtent.x <= 0.0f
        || !IsFinite(metrics.thumbExtent.y) || metrics.thumbExtent.y <= 0.0f
        || !IsFinite(metrics.trackHeight) || metrics.trackHeight <= 0.0f
        || !IsFinite(metrics.contentSize.x) || !IsFinite(metrics.contentSize.y)
    )
        return false;
    const f64 width = static_cast<f64>(metrics.padding.left) + metrics.padding.right + metrics.thumbExtent.x;
    const f64 height = static_cast<f64>(metrics.padding.top) + metrics.padding.bottom
        + Max(static_cast<f64>(metrics.thumbExtent.y), static_cast<f64>(metrics.trackHeight));
    return
        width <= Limit<f32>::s_Max && height <= Limit<f32>::s_Max
        && metrics.contentSize.x >= static_cast<f32>(width) && metrics.contentSize.y >= static_cast<f32>(height)
    ;
}

[[nodiscard]] static bool MakeRect(const f64 x, const f64 y, const f64 width, const f64 height, Rect& out){
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
        !ValidRect(candidate) || (width > 0.0 && candidate.x + candidate.width <= candidate.x)
        || (height > 0.0 && candidate.y + candidate.height <= candidate.y)
    )
        return false;
    out = candidate;
    return true;
}

[[nodiscard]] static bool Intersect(const Rect& lhs, const Rect& rhs, Rect& out){
    const f64 left = Max(static_cast<f64>(lhs.x), static_cast<f64>(rhs.x));
    const f64 top = Max(static_cast<f64>(lhs.y), static_cast<f64>(rhs.y));
    const f64 right = Min(static_cast<f64>(lhs.x) + lhs.width, static_cast<f64>(rhs.x) + rhs.width);
    const f64 bottom = Min(static_cast<f64>(lhs.y) + lhs.height, static_cast<f64>(rhs.y) + rhs.height);
    return MakeRect(left, top, Max(0.0, right - left), Max(0.0, bottom - top), out);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SliderLayout::Measure(const SliderOptions& options, const SliderStyle& style, SliderMetrics& out){
    using namespace __hidden_ui_slider_layout;
    if(
        !SliderBehavior::Validate(options) || !ValidPadding(style.padding)
        || !IsFinite(style.thumbExtent.x) || style.thumbExtent.x <= 0.0f
        || !IsFinite(style.thumbExtent.y) || style.thumbExtent.y <= 0.0f
        || !IsFinite(style.trackHeight) || style.trackHeight <= 0.0f
        || !ValidColor(style.hoverTint) || !ValidColor(style.pressedTint) || !ValidColor(style.disabledTint)
    )
        return false;
    SliderMetrics candidate;
    candidate.padding = style.padding;
    candidate.thumbExtent = style.thumbExtent;
    candidate.trackHeight = style.trackHeight;
    const f64 width = static_cast<f64>(style.padding.left) + style.padding.right + style.thumbExtent.x;
    const f64 height = Max(static_cast<f64>(options.height), static_cast<f64>(style.padding.top) + style.padding.bottom
        + Max(static_cast<f64>(style.thumbExtent.y), static_cast<f64>(style.trackHeight)));
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return false;
    candidate.contentSize = { static_cast<f32>(width), static_cast<f32>(height) };
    if(!ValidMetrics(candidate))
        return false;
    out = candidate;
    return true;
}

bool SliderLayout::Place(
    const Rect& bounds,
    const Rect& clip,
    const SliderMetrics& metrics,
    const f64 normalized,
    SliderPlacement& out){
    using namespace __hidden_ui_slider_layout;
    if(
        !ValidRect(bounds) || !ValidRect(clip) || !ValidMetrics(metrics)
        || !IsFinite(normalized) || normalized < 0.0 || normalized > 1.0
    )
        return false;
    SliderPlacement candidate;
    candidate.bounds = bounds;
    if(!Intersect(bounds, clip, candidate.clip))
        return false;
    const f64 leftInset = Min(static_cast<f64>(metrics.padding.left), static_cast<f64>(bounds.width));
    const f64 rightInset = Min(static_cast<f64>(metrics.padding.right), static_cast<f64>(bounds.width) - leftInset);
    const f64 topInset = Min(static_cast<f64>(metrics.padding.top), static_cast<f64>(bounds.height));
    const f64 bottomInset = Min(static_cast<f64>(metrics.padding.bottom), static_cast<f64>(bounds.height) - topInset);
    const f64 x = static_cast<f64>(bounds.x) + leftInset;
    const f64 y = static_cast<f64>(bounds.y) + topInset;
    const f64 width = static_cast<f64>(bounds.width) - leftInset - rightInset;
    const f64 height = static_cast<f64>(bounds.height) - topInset - bottomInset;
    const f64 thumbWidth = Min(static_cast<f64>(metrics.thumbExtent.x), width);
    const f64 thumbHeight = Min(static_cast<f64>(metrics.thumbExtent.y), height);
    const f64 trackHeight = Min(static_cast<f64>(metrics.trackHeight), height);
    const f64 travel = width - thumbWidth;
    const f64 centerStart = x + thumbWidth * 0.5;
    const f64 thumbX = normalized == 0.0 ? x : normalized == 1.0 ? x + travel : x + travel * normalized;
    if(
        !MakeRect(x, y, width, height, candidate.travelBounds)
        || !MakeRect(centerStart, y, travel, height, candidate.centerTravel)
        || !MakeRect(centerStart, y + (height - trackHeight) * 0.5, travel, trackHeight, candidate.track)
        || !MakeRect(thumbX, y + (height - thumbHeight) * 0.5, thumbWidth, thumbHeight, candidate.thumb)
    )
        return false;
    candidate.thumbExtent = { static_cast<f32>(thumbWidth), static_cast<f32>(thumbHeight) };
    out = candidate;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

