// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scrollbar.h"

#include <impl/ecs_ui/toolkit/layout/rectangle.h>
#include <impl/ecs_ui/toolkit/rect_math.h>

#include <global/simd/result.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>
#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_scrollbar{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SIMDMatrixDouble LoadBarValues(const ScrollbarPlacement& bar)noexcept{
    if(
        !IsFinite(bar.track.x)
        || !IsFinite(bar.track.y)
        || !IsFinite(bar.track.width)
        || !IsFinite(bar.track.height)
        || !IsFinite(bar.thumb.x)
        || !IsFinite(bar.thumb.y)
        || !IsFinite(bar.thumb.width)
        || !IsFinite(bar.thumb.height)
        || !IsFinite(bar.contentExtent)
        || !IsFinite(bar.viewportExtent)
        || !IsFinite(bar.maximum)
        || !IsFinite(bar.offset)
    )
        return MakeInvalidUiMatrixDouble();
    return SIMDMatrixDouble{ .v = {
        VectorDoubleSet(bar.track.x, bar.track.y, bar.track.width, bar.track.height),
        VectorDoubleSet(bar.thumb.x, bar.thumb.y, bar.thumb.width, bar.thumb.height),
        VectorDoubleSet(bar.contentExtent, bar.viewportExtent, bar.maximum, bar.offset),
        VectorDoubleReplicate(bar.visible ? 1.0 : 0.0)
    } };
}

[[nodiscard]] static b8 ValidBar(const SIMDMatrixDouble& values, const ScrollAxis::Enum axis)noexcept{
    if(!AreUiMatrixRowsFinite(values, s_ScrollbarValueRowCount))
        return false;
    const SIMDVectorSingle trackSingle = ConvertVectorDoubleToSingle(values.v[0]);
    const SIMDVectorSingle thumbSingle = ConvertVectorDoubleToSingle(values.v[1]);
    const f32 trackX = VectorGetX(trackSingle);
    const f32 trackY = VectorGetY(trackSingle);
    const f32 trackWidth = VectorGetZ(trackSingle);
    const f32 trackHeight = VectorGetW(trackSingle);
    const f32 thumbX = VectorGetX(thumbSingle);
    const f32 thumbY = VectorGetY(thumbSingle);
    const f32 thumbWidth = VectorGetZ(thumbSingle);
    const f32 thumbHeight = VectorGetW(thumbSingle);
    const f64 contentExtent = VectorDoubleGetX(values.v[2]);
    const f64 viewportExtent = VectorDoubleGetY(values.v[2]);
    const f64 maximum = VectorDoubleGetZ(values.v[2]);
    const f64 offset = VectorDoubleGetW(values.v[2]);
    if(contentExtent < 0.0 || viewportExtent < 0.0)
        return false;
    const f64 expectedMaximum = Max(0.0, contentExtent - viewportExtent);
    // Lanes are track right, track bottom, thumb right, thumb bottom.
    const SIMDVectorSingle end = VectorAdd(VectorPermute<0, 1, 4, 5>(trackSingle, thumbSingle), VectorPermute<2, 3, 6, 7>(trackSingle, thumbSingle));
    if(
        trackWidth < 0.0f || trackHeight < 0.0f || thumbWidth < 0.0f || thumbHeight < 0.0f
        || !VectorIsFinite(end, VectorComponentMask::s_XYZW)
        || contentExtent < 0.0 || viewportExtent < 0.0 || maximum != expectedMaximum
        || offset < 0.0 || offset > maximum || (viewportExtent > 0.0 && maximum > 0.0 && maximum >= contentExtent)
    )
        return false;
    if(VectorDoubleGetX(values.v[3]) == 0.0)
        return trackX == 0.0f && trackY == 0.0f && trackWidth == 0.0f && trackHeight == 0.0f && thumbX == 0.0f && thumbY == 0.0f && thumbWidth == 0.0f && thumbHeight == 0.0f;
    if(maximum <= 0.0 || trackWidth <= 0.0f || trackHeight <= 0.0f)
        return false;
    if(axis == ScrollAxis::Horizontal){
        return viewportExtent == static_cast<f64>(trackWidth) && thumbY == trackY && thumbHeight == trackHeight && thumbX >= trackX && thumbWidth <= trackWidth && VectorGetZ(end) <= VectorGetX(end);
    }
    return viewportExtent == static_cast<f64>(trackHeight) && thumbX == trackX && thumbWidth == trackWidth && thumbY >= trackY && thumbHeight <= trackHeight && VectorGetW(end) <= VectorGetY(end);
}

struct MovedThumb{
    f64 offset = 0.0;
    f32 origin = 0.0f;
};

[[nodiscard]] static Expected<MovedThumb> MoveThumb(const f64 offset, const f64 maximum, const f32 trackOrigin, const f32 trackLength, const f32 thumbLength, const b8 visible)noexcept{
    const f64 acceptedOffset = Min(offset, maximum);
    if(!visible)
        return MovedThumb{ acceptedOffset, 0.0f };
    const f64 travel = Max(0.0, static_cast<f64>(trackLength) - static_cast<f64>(thumbLength));
    const f64 position = static_cast<f64>(trackOrigin) + travel * (acceptedOffset / maximum);
    if(!IsFinite(position) || position < -Limit<f32>::s_Max || position > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    const f32 first = trackOrigin;
    const f32 last = Max(first, (first + trackLength) - thumbLength);
    const f32 positionSingle = static_cast<f32>(position);
    const f32 origin = positionSingle > first ? Min(last, positionSingle) : first;
    return MovedThumb{ acceptedOffset, origin };
}

[[nodiscard]] static b8 StoreBarOffset(const f64 offset, const ScrollAxis::Enum axis, ScrollbarPlacement& bar)noexcept{
    const f32 trackOrigin = axis == ScrollAxis::Horizontal ? bar.track.x : bar.track.y;
    const f32 trackLength = axis == ScrollAxis::Horizontal ? bar.track.width : bar.track.height;
    const f32 thumbLength = axis == ScrollAxis::Horizontal ? bar.thumb.width : bar.thumb.height;
    const auto values = MoveThumb(offset, bar.maximum, trackOrigin, trackLength, thumbLength, bar.visible);
    if(!values)
        return false;
    bar.offset = values->offset;
    if(bar.visible)
        (axis == ScrollAxis::Horizontal ? bar.thumb.x : bar.thumb.y) = values->origin;
    return !bar.visible || ValidBar(LoadBarValues(bar), axis);
}

[[nodiscard]] static Expected<ScrollbarPlacement> StoreBarPlacement(const f64 contentExtent, const f64 viewportExtent, const Rect& track, const f32 minThumb, const f64 offset, const ScrollAxis::Enum axis)noexcept{
    ScrollbarPlacement candidate;
    candidate.contentExtent = contentExtent;
    candidate.viewportExtent = viewportExtent;
    candidate.maximum = Max(0.0, contentExtent - viewportExtent);
    if(!IsFinite(candidate.maximum) || (viewportExtent > 0.0 && candidate.maximum > 0.0 && candidate.maximum >= contentExtent))
        return MakeUnexpected(Failure{});
    candidate.visible = track.width > 0.0f && track.height > 0.0f;
    if(candidate.visible){
        if(candidate.maximum <= 0.0)
            return MakeUnexpected(Failure{});
        candidate.track = track;
        const f64 length = axis == ScrollAxis::Horizontal ? track.width : track.height;
        const f64 proportional = length * (viewportExtent / contentExtent);
        const f64 minimum = Min(static_cast<f64>(minThumb), length);
        const f32 thumbExtent = static_cast<f32>(proportional > minimum ? Min(length, proportional) : minimum);
        candidate.thumb = track;
        if(axis == ScrollAxis::Horizontal)
            candidate.thumb.width = Min(thumbExtent, track.width);
        else
            candidate.thumb.height = Min(thumbExtent, track.height);
    }
    if(!StoreBarOffset(offset, axis, candidate) || !ValidBar(LoadBarValues(candidate), axis))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ScrollViewportPlacement> ScrollbarLayout::Calculate(const Rect& bounds, const Rect& clip, const Insets& padding, const Point& contentMeasure, const f32 caretWidth, const Point& previousScroll, const f32 thickness, const f32 minThumb)noexcept{
    using namespace __hidden_ui_scrollbar;
    if(
        !IsValidUiRect(bounds) || !IsValidUiRect(clip) || !IsValidUiPadding(padding)
        || !IsValidUiExtent(contentMeasure) || !IsValidUiExtent(previousScroll)
        || !IsFinite(caretWidth) || caretWidth <= 0.0f
        || !IsFinite(thickness) || thickness < 0.0f || !IsFinite(minThumb) || minThumb < 0.0f
    )
        return MakeUnexpected(Failure{});
    const auto padded = InsetUiRect<UiRectPrecision::RoundedEndpoints>(bounds, padding);
    if(!padded)
        return MakeUnexpected(Failure{});
    const f64 availableWidth = padded->width;
    const f64 availableHeight = padded->height;
    const SIMDVectorDouble geometryPair4Operand0 = VectorDoubleSet(static_cast<f64>(thickness), static_cast<f64>(thickness), 0.0, 0.0);
    const SIMDVectorDouble geometryPair4Operand1 = VectorDoubleSet(availableWidth, availableHeight, 0.0, 0.0);
    const SIMDVectorDouble verticalWidthHorizontalHeightValue = VectorDoubleSelect(geometryPair4Operand1, geometryPair4Operand0, VectorDoubleLess(geometryPair4Operand0, geometryPair4Operand1));
    const f64 verticalWidth = VectorDoubleGetX(verticalWidthHorizontalHeightValue);
    const f64 horizontalHeight = VectorDoubleGetY(verticalWidthHorizontalHeightValue);
    const f64 contentWidth = static_cast<f64>(contentMeasure.x) + static_cast<f64>(caretWidth);
    const f64 contentHeight = contentMeasure.y;
    if(!IsFinite(contentWidth))
        return MakeUnexpected(Failure{});
    b8 horizontal = false;
    b8 vertical = false;
    // Each axis can only add a reservation; the other axis is rechecked after either addition.
    for(u32 iteration = 0u; iteration < s_ScrollbarReservationPassLimit; ++iteration){
        const SIMDVectorDouble viewportSize = VectorDoubleSubtract(VectorDoubleSet(availableWidth, availableHeight, 0.0, 0.0), VectorDoubleSet(vertical ? verticalWidth : 0.0, horizontal ? horizontalHeight : 0.0, 0.0, 0.0));
        const f64 viewportWidth = static_cast<f32>(VectorDoubleGetX(viewportSize));
        const f64 viewportHeight = static_cast<f32>(VectorDoubleGetY(viewportSize));
        const b8 nextHorizontal = horizontal || (horizontalHeight > 0.0 && availableWidth > 0.0 && contentWidth > viewportWidth);
        const b8 nextVertical = vertical || (verticalWidth > 0.0 && availableHeight > 0.0 && contentHeight > viewportHeight);
        if(nextHorizontal == horizontal && nextVertical == vertical)
            break;
        horizontal = nextHorizontal;
        vertical = nextVertical;
    }
    const f64 barWidth = vertical ? verticalWidth : 0.0;
    const f64 barHeight = horizontal ? horizontalHeight : 0.0;
    ScrollViewportPlacement candidate;
    const SIMDVectorDouble paddedOrigin = VectorDoubleSet(padded->x, padded->y, 0.0, 0.0);
    const SIMDVectorDouble barSize = VectorDoubleSet(barWidth, barHeight, 0.0, 0.0);
    const SIMDVectorDouble viewportSize = VectorDoubleSubtract(geometryPair4Operand1, barSize);
    const auto viewport = MakeUiRect<UiRectPrecision::RoundedEndpoints>(VectorDoublePermute<0, 1, 4, 5>(paddedOrigin, viewportSize));
    if(!viewport)
        return MakeUnexpected(Failure{});
    candidate.viewport = *viewport;
    if((barWidth > 0.0 && candidate.viewport.width >= padded->width) || (barHeight > 0.0 && candidate.viewport.height >= padded->height))
        return MakeUnexpected(Failure{});
    const auto contentClip = IntersectUiRects<UiRectPrecision::RoundedEndpoints>(candidate.viewport, clip);
    if(!contentClip)
        return MakeUnexpected(Failure{});
    candidate.contentClip = *contentClip;
    const f64 viewportWidth = candidate.viewport.width;
    const f64 viewportHeight = candidate.viewport.height;
    const SIMDVectorDouble cornerOrigin = VectorDoubleAdd(paddedOrigin, VectorDoubleSet(viewportWidth, viewportHeight, 0.0, 0.0));
    Rect horizontalTrack;
    Rect verticalTrack;
    if(horizontal && viewportWidth > 0.0 && barHeight > 0.0){
        const auto track = MakeUiRect<UiRectPrecision::RoundedEndpoints>(VectorDoubleSelect(VectorDoubleSet(padded->x, 0.0, viewportWidth, barHeight), cornerOrigin, s_DoubleSIMDMaskY));
        if(!track)
            return MakeUnexpected(Failure{});
        horizontalTrack = *track;
    }
    if(vertical && viewportHeight > 0.0 && barWidth > 0.0){
        const auto track = MakeUiRect<UiRectPrecision::RoundedEndpoints>(VectorDoubleSelect(VectorDoubleSet(0.0, padded->y, barWidth, viewportHeight), cornerOrigin, s_DoubleSIMDMaskX));
        if(!track)
            return MakeUnexpected(Failure{});
        verticalTrack = *track;
    }
    if(barWidth > 0.0 && barHeight > 0.0){
        const auto corner = MakeUiRect<UiRectPrecision::RoundedEndpoints>(VectorDoublePermute<0, 1, 4, 5>(cornerOrigin, barSize));
        if(!corner)
            return MakeUnexpected(Failure{});
        candidate.corner = *corner;
    }
    const auto horizontalBar = StoreBarPlacement(contentWidth, viewportWidth, horizontalTrack, minThumb, previousScroll.x, ScrollAxis::Horizontal);
    if(!horizontalBar)
        return MakeUnexpected(Failure{});
    candidate.horizontal = *horizontalBar;
    const auto verticalBar = StoreBarPlacement(contentHeight, viewportHeight, verticalTrack, minThumb, previousScroll.y, ScrollAxis::Vertical);
    if(!verticalBar)
        return MakeUnexpected(Failure{});
    candidate.vertical = *verticalBar;
    return candidate;
}

b8 ScrollbarLayout::UpdateOffsets(const Point& scroll, ScrollViewportPlacement& placement)noexcept{
    using namespace __hidden_ui_scrollbar;
    if(
        !IsValidUiExtent(scroll) || !IsValidUiRect(placement.viewport) || !IsValidUiRect(placement.contentClip) || !IsValidUiRect(placement.corner)
        || placement.horizontal.viewportExtent != static_cast<f64>(placement.viewport.width)
        || placement.vertical.viewportExtent != static_cast<f64>(placement.viewport.height)
        || !ValidBar(LoadBarValues(placement.horizontal), ScrollAxis::Horizontal) || !ValidBar(LoadBarValues(placement.vertical), ScrollAxis::Vertical)
    )
        return false;
    ScrollViewportPlacement candidate = placement;
    if(!StoreBarOffset(scroll.x, ScrollAxis::Horizontal, candidate.horizontal) || !StoreBarOffset(scroll.y, ScrollAxis::Vertical, candidate.vertical))
        return false;
    placement = candidate;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

