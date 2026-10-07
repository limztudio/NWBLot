// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scrollbar.h"

#include <impl/ecs_ui/toolkit/layout/validation.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_scrollbar{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool MakeRect(const f64 x, const f64 y, const f64 width, const f64 height, Rect& out)noexcept{
    if(
        !IsFinite(x) || x < -Limit<f32>::s_Max || x > Limit<f32>::s_Max
        || !IsFinite(y) || y < -Limit<f32>::s_Max || y > Limit<f32>::s_Max
        || !IsFinite(width) || width < 0.0 || width > Limit<f32>::s_Max
        || !IsFinite(height) || height < 0.0 || height > Limit<f32>::s_Max
    )
        return false;
    const Rect candidate{ static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(width), static_cast<f32>(height) };
    if(!IsValidUiRect(candidate))
        return false;
    out = candidate;
    return true;
}

[[nodiscard]] static bool Intersect(const Rect& lhs, const Rect& rhs, Rect& out)noexcept{
    const f64 x = Max(static_cast<f64>(lhs.x), static_cast<f64>(rhs.x));
    const f64 y = Max(static_cast<f64>(lhs.y), static_cast<f64>(rhs.y));
    const f64 right = Min(static_cast<f64>(lhs.x) + lhs.width, static_cast<f64>(rhs.x) + rhs.width);
    const f64 bottom = Min(static_cast<f64>(lhs.y) + lhs.height, static_cast<f64>(rhs.y) + rhs.height);
    return MakeRect(x, y, Max(0.0, right - x), Max(0.0, bottom - y), out);
}

[[nodiscard]] static bool ValidBar(const ScrollbarPlacement& bar, const ScrollAxis::Enum axis)noexcept{
    if(
        !IsValidUiRect(bar.track) || !IsValidUiRect(bar.thumb)
        || !IsFinite(bar.contentExtent) || bar.contentExtent < 0.0
        || !IsFinite(bar.viewportExtent) || bar.viewportExtent < 0.0
        || !IsFinite(bar.maximum) || bar.maximum != Max(0.0, bar.contentExtent - bar.viewportExtent)
        || !IsFinite(bar.offset) || bar.offset < 0.0 || bar.offset > bar.maximum
        || (bar.viewportExtent > 0.0 && bar.maximum > 0.0 && bar.maximum >= bar.contentExtent)
    )
        return false;
    if(!bar.visible){
        return
            bar.track.x == 0.0f && bar.track.y == 0.0f && bar.track.width == 0.0f && bar.track.height == 0.0f
            && bar.thumb.x == 0.0f && bar.thumb.y == 0.0f && bar.thumb.width == 0.0f && bar.thumb.height == 0.0f
        ;
    }
    if(bar.maximum <= 0.0 || bar.track.width <= 0.0f || bar.track.height <= 0.0f)
        return false;
    if(axis == ScrollAxis::Horizontal){
        return
            bar.viewportExtent == static_cast<f64>(bar.track.width)
            && bar.thumb.y == bar.track.y && bar.thumb.height == bar.track.height
            && bar.thumb.x >= bar.track.x && bar.thumb.width <= bar.track.width
            && bar.thumb.x + bar.thumb.width <= bar.track.x + bar.track.width
        ;
    }
    return
        bar.viewportExtent == static_cast<f64>(bar.track.height)
        && bar.thumb.x == bar.track.x && bar.thumb.width == bar.track.width
        && bar.thumb.y >= bar.track.y && bar.thumb.height <= bar.track.height
        && bar.thumb.y + bar.thumb.height <= bar.track.y + bar.track.height
    ;
}

[[nodiscard]] static bool MoveThumb(const f64 offset, const ScrollAxis::Enum axis, ScrollbarPlacement& bar)noexcept{
    bar.offset = Min(offset, bar.maximum);
    if(!bar.visible)
        return true;
    const f32 trackOrigin = axis == ScrollAxis::Horizontal ? bar.track.x : bar.track.y;
    const f32 trackExtent = axis == ScrollAxis::Horizontal ? bar.track.width : bar.track.height;
    const f32 thumbExtent = axis == ScrollAxis::Horizontal ? bar.thumb.width : bar.thumb.height;
    const f64 travel = Max(0.0, static_cast<f64>(trackExtent) - thumbExtent);
    const f64 position = static_cast<f64>(trackOrigin) + travel * (bar.offset / bar.maximum);
    if(!IsFinite(position) || position < -Limit<f32>::s_Max || position > Limit<f32>::s_Max)
        return false;
    const f32 lastOrigin = Max(trackOrigin, trackOrigin + trackExtent - thumbExtent);
    const f32 origin = Clamp(static_cast<f32>(position), trackOrigin, lastOrigin);
    if(axis == ScrollAxis::Horizontal)
        bar.thumb.x = origin;
    else
        bar.thumb.y = origin;
    return ValidBar(bar, axis);
}

[[nodiscard]] static bool BuildBar(
    const f64 contentExtent,
    const f64 viewportExtent,
    const Rect& track,
    const f32 minThumb,
    const f64 offset,
    const ScrollAxis::Enum axis,
    ScrollbarPlacement& out
)noexcept{
    ScrollbarPlacement candidate;
    candidate.contentExtent = contentExtent;
    candidate.viewportExtent = viewportExtent;
    candidate.maximum = Max(0.0, contentExtent - viewportExtent);
    if(!IsFinite(candidate.maximum) || (viewportExtent > 0.0 && candidate.maximum > 0.0 && candidate.maximum >= contentExtent))
        return false;
    candidate.visible = track.width > 0.0f && track.height > 0.0f;
    if(candidate.visible){
        if(candidate.maximum <= 0.0)
            return false;
        candidate.track = track;
        const f64 length = axis == ScrollAxis::Horizontal ? track.width : track.height;
        const f64 proportional = length * (viewportExtent / contentExtent);
        const f32 thumbExtent = static_cast<f32>(Clamp(proportional, Min(static_cast<f64>(minThumb), length), length));
        candidate.thumb = track;
        if(axis == ScrollAxis::Horizontal)
            candidate.thumb.width = Min(thumbExtent, track.width);
        else
            candidate.thumb.height = Min(thumbExtent, track.height);
    }
    if(!MoveThumb(offset, axis, candidate) || !ValidBar(candidate, axis))
        return false;
    out = candidate;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ScrollbarLayout::Calculate(
    const Rect& bounds,
    const Rect& clip,
    const Insets& padding,
    const Point& contentMeasure,
    const f32 caretWidth,
    const Point& previousScroll,
    const f32 thickness,
    const f32 minThumb,
    ScrollViewportPlacement& out
)noexcept{
    using namespace __hidden_ui_scrollbar;
    if(
        !IsValidUiRect(bounds) || !IsValidUiRect(clip) || !IsValidUiPadding(padding)
        || !IsValidUiExtent(contentMeasure) || !IsValidUiExtent(previousScroll)
        || !IsFinite(caretWidth) || caretWidth <= 0.0f
        || !IsFinite(thickness) || thickness < 0.0f || !IsFinite(minThumb) || minThumb < 0.0f
    )
        return false;
    const f64 x = static_cast<f64>(bounds.x) + Min(static_cast<f64>(padding.left), static_cast<f64>(bounds.width));
    const f64 y = static_cast<f64>(bounds.y) + Min(static_cast<f64>(padding.top), static_cast<f64>(bounds.height));
    const f64 width = Max(0.0, static_cast<f64>(bounds.width) - padding.left - padding.right);
    const f64 height = Max(0.0, static_cast<f64>(bounds.height) - padding.top - padding.bottom);
    Rect padded;
    if(!MakeRect(x, y, width, height, padded))
        return false;
    const f64 availableWidth = padded.width;
    const f64 availableHeight = padded.height;
    const f64 verticalWidth = Min(static_cast<f64>(thickness), availableWidth);
    const f64 horizontalHeight = Min(static_cast<f64>(thickness), availableHeight);
    const f64 contentWidth = static_cast<f64>(contentMeasure.x) + caretWidth;
    const f64 contentHeight = contentMeasure.y;
    if(!IsFinite(contentWidth))
        return false;
    bool horizontal = false;
    bool vertical = false;
    // Each axis can only add a reservation; the other axis is rechecked after either addition.
    for(u32 iteration = 0u; iteration < 3u; ++iteration){
        const f64 viewportWidth = static_cast<f32>(availableWidth - (vertical ? verticalWidth : 0.0));
        const f64 viewportHeight = static_cast<f32>(availableHeight - (horizontal ? horizontalHeight : 0.0));
        const bool nextHorizontal = horizontal || (horizontalHeight > 0.0 && availableWidth > 0.0 && contentWidth > viewportWidth);
        const bool nextVertical = vertical || (verticalWidth > 0.0 && availableHeight > 0.0 && contentHeight > viewportHeight);
        if(nextHorizontal == horizontal && nextVertical == vertical)
            break;
        horizontal = nextHorizontal;
        vertical = nextVertical;
    }
    const f64 barWidth = vertical ? verticalWidth : 0.0;
    const f64 barHeight = horizontal ? horizontalHeight : 0.0;
    ScrollViewportPlacement candidate;
    if(!MakeRect(padded.x, padded.y, availableWidth - barWidth, availableHeight - barHeight, candidate.viewport))
        return false;
    if((barWidth > 0.0 && candidate.viewport.width >= padded.width) || (barHeight > 0.0 && candidate.viewport.height >= padded.height))
        return false;
    if(!Intersect(candidate.viewport, clip, candidate.contentClip))
        return false;
    const f64 viewportWidth = candidate.viewport.width;
    const f64 viewportHeight = candidate.viewport.height;
    Rect horizontalTrack;
    Rect verticalTrack;
    if(horizontal && viewportWidth > 0.0 && barHeight > 0.0){
        if(!MakeRect(padded.x, static_cast<f64>(padded.y) + viewportHeight, viewportWidth, barHeight, horizontalTrack))
            return false;
    }
    if(vertical && viewportHeight > 0.0 && barWidth > 0.0){
        if(!MakeRect(static_cast<f64>(padded.x) + viewportWidth, padded.y, barWidth, viewportHeight, verticalTrack))
            return false;
    }
    if(barWidth > 0.0 && barHeight > 0.0){
        if(!MakeRect(
            static_cast<f64>(padded.x) + viewportWidth, static_cast<f64>(padded.y) + viewportHeight,
            barWidth, barHeight, candidate.corner
        ))
            return false;
    }
    if(!BuildBar(contentWidth, viewportWidth, horizontalTrack, minThumb, previousScroll.x, ScrollAxis::Horizontal, candidate.horizontal))
        return false;
    if(!BuildBar(contentHeight, viewportHeight, verticalTrack, minThumb, previousScroll.y, ScrollAxis::Vertical, candidate.vertical))
        return false;
    out = candidate;
    return true;
}

bool ScrollbarLayout::UpdateOffsets(const Point& scroll, ScrollViewportPlacement& out)noexcept{
    using namespace __hidden_ui_scrollbar;
    if(
        !IsValidUiExtent(scroll) || !IsValidUiRect(out.viewport) || !IsValidUiRect(out.contentClip) || !IsValidUiRect(out.corner)
        || out.horizontal.viewportExtent != static_cast<f64>(out.viewport.width)
        || out.vertical.viewportExtent != static_cast<f64>(out.viewport.height)
        || !ValidBar(out.horizontal, ScrollAxis::Horizontal) || !ValidBar(out.vertical, ScrollAxis::Vertical)
    )
        return false;
    ScrollViewportPlacement candidate = out;
    if(!MoveThumb(scroll.x, ScrollAxis::Horizontal, candidate.horizontal) || !MoveThumb(scroll.y, ScrollAxis::Vertical, candidate.vertical))
        return false;
    out = candidate;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

