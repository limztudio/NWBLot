// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scrollbar.h"

#include <impl/ecs_ui/toolkit/layout/rectangle.h>

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
    if(!IsFinite(bar.track.x) || !IsFinite(bar.track.y) || !IsFinite(bar.track.width) || !IsFinite(bar.track.height) || !IsFinite(bar.thumb.x) || !IsFinite(bar.thumb.y) || !IsFinite(bar.thumb.width) || !IsFinite(bar.thumb.height) || !IsFinite(bar.contentExtent) || !IsFinite(bar.viewportExtent) || !IsFinite(bar.maximum) || !IsFinite(bar.offset))
        return SIMDMatrixDouble{ .v = { VectorDoubleReplicate(Limit<f64>::s_Infinity), VectorDoubleZero(), VectorDoubleZero(), VectorDoubleZero() } };
    return SIMDMatrixDouble{ .v = {
        VectorDoubleSet(bar.track.x, bar.track.y, bar.track.width, bar.track.height),
        VectorDoubleSet(bar.thumb.x, bar.thumb.y, bar.thumb.width, bar.thumb.height),
        VectorDoubleSet(bar.contentExtent, bar.viewportExtent, bar.maximum, bar.offset),
        VectorDoubleReplicate(bar.visible ? 1.0 : 0.0)
    } };
}

[[nodiscard]] static bool ValidBar(const SIMDMatrixDouble& values, const ScrollAxis::Enum axis)noexcept{
    for(u32 index = 0u; index < 3u; ++index){
        if(VectorDoubleMoveMask(VectorDoubleOrInt(VectorDoubleIsNaN(values.v[index]), VectorDoubleIsInfinite(values.v[index]))) != 0u)
            return false;
    }
    const f32 trackX = static_cast<f32>(VectorDoubleGetX(values.v[0]));
    const f32 trackY = static_cast<f32>(VectorDoubleGetY(values.v[0]));
    const f32 trackWidth = static_cast<f32>(VectorDoubleGetZ(values.v[0]));
    const f32 trackHeight = static_cast<f32>(VectorDoubleGetW(values.v[0]));
    const f32 thumbX = static_cast<f32>(VectorDoubleGetX(values.v[1]));
    const f32 thumbY = static_cast<f32>(VectorDoubleGetY(values.v[1]));
    const f32 thumbWidth = static_cast<f32>(VectorDoubleGetZ(values.v[1]));
    const f32 thumbHeight = static_cast<f32>(VectorDoubleGetW(values.v[1]));
    const f64 contentExtent = VectorDoubleGetX(values.v[2]);
    const f64 viewportExtent = VectorDoubleGetY(values.v[2]);
    const f64 maximum = VectorDoubleGetZ(values.v[2]);
    const f64 offset = VectorDoubleGetW(values.v[2]);
    if(contentExtent < 0.0 || viewportExtent < 0.0)
        return false;
    const SIMDVectorDouble expectedMaximum = VectorDoubleMax(VectorDoubleZero(), VectorDoubleSubtract(VectorDoubleSplatX(values.v[2]), VectorDoubleSplatY(values.v[2])));
    if(
        trackWidth < 0.0f || trackHeight < 0.0f || thumbWidth < 0.0f || thumbHeight < 0.0f
        || !IsFinite(VectorGetX(VectorAdd(VectorReplicate(trackX), VectorReplicate(trackWidth)))) || !IsFinite(VectorGetX(VectorAdd(VectorReplicate(trackY), VectorReplicate(trackHeight))))
        || !IsFinite(VectorGetX(VectorAdd(VectorReplicate(thumbX), VectorReplicate(thumbWidth)))) || !IsFinite(VectorGetX(VectorAdd(VectorReplicate(thumbY), VectorReplicate(thumbHeight))))
        || contentExtent < 0.0 || viewportExtent < 0.0 || maximum != VectorDoubleGetX(expectedMaximum)
        || offset < 0.0 || offset > maximum || (viewportExtent > 0.0 && maximum > 0.0 && maximum >= contentExtent)
    )
        return false;
    if(VectorDoubleGetX(values.v[3]) == 0.0)
        return trackX == 0.0f && trackY == 0.0f && trackWidth == 0.0f && trackHeight == 0.0f && thumbX == 0.0f && thumbY == 0.0f && thumbWidth == 0.0f && thumbHeight == 0.0f;
    if(maximum <= 0.0 || trackWidth <= 0.0f || trackHeight <= 0.0f)
        return false;
    // Lanes are track right, track bottom, thumb right, thumb bottom. The checks above proved these sums finite.
    const SIMDVectorSingle end = VectorAdd(VectorSet(trackX, trackY, thumbX, thumbY), VectorSet(trackWidth, trackHeight, thumbWidth, thumbHeight));
    if(axis == ScrollAxis::Horizontal){
        return viewportExtent == static_cast<f64>(trackWidth) && thumbY == trackY && thumbHeight == trackHeight && thumbX >= trackX && thumbWidth <= trackWidth && VectorGetZ(end) <= VectorGetX(end);
    }
    return viewportExtent == static_cast<f64>(trackHeight) && thumbX == trackX && thumbWidth == trackWidth && thumbY >= trackY && thumbHeight <= trackHeight && VectorGetW(end) <= VectorGetY(end);
}

[[nodiscard]] static Expected<SIMDVectorDoubleResult> MoveThumb(const SIMDVectorDouble offset, const SIMDVectorDouble maximum, const SIMDVectorSingle track, const bool visible)noexcept{
    const SIMDVectorDouble acceptedOffset = VectorDoubleMin(offset, maximum);
    if(!visible)
        return SIMDVectorDoubleResult{ VectorDoubleSelect(VectorDoubleZero(), acceptedOffset, s_DoubleSIMDMaskX) };
    const SIMDVectorDouble trackValues = ConvertVectorSingleToDouble(track);
    const SIMDVectorDouble travel = VectorDoubleMax(VectorDoubleZero(), VectorDoubleSubtract(VectorDoubleSplatY(trackValues), VectorDoubleSplatZ(trackValues)));
    const SIMDVectorDouble position = VectorDoubleAdd(VectorDoubleSplatX(trackValues), VectorDoubleMultiply(travel, VectorDoubleDivide(acceptedOffset, maximum)));
    if(!IsFinite(VectorDoubleGetX(position)) || VectorDoubleGetX(position) < -Limit<f32>::s_Max || VectorDoubleGetX(position) > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle first = VectorSplatX(track);
    const SIMDVectorSingle last = VectorMax(first, VectorSubtract(VectorAdd(first, VectorSplatY(track)), VectorSplatZ(track)));
    const SIMDVectorSingle positionSingle = ConvertVectorDoubleToSingle(position);
    const SIMDVectorSingle origin = VectorSelect(first, VectorMin(last, positionSingle), VectorGreater(positionSingle, first));
    return SIMDVectorDoubleResult{ VectorDoubleSelect(acceptedOffset, ConvertVectorSingleToDouble(origin), s_DoubleSIMDMaskY) };
}

[[nodiscard]] static bool StoreBarOffset(const SIMDVectorDouble offset, const ScrollAxis::Enum axis, ScrollbarPlacement& bar)noexcept{
    const SIMDVectorSingle track = axis == ScrollAxis::Horizontal ? VectorSet(bar.track.x, bar.track.width, bar.thumb.width, 0.0f) : VectorSet(bar.track.y, bar.track.height, bar.thumb.height, 0.0f);
    const auto values = MoveThumb(offset, VectorDoubleReplicate(bar.maximum), track, bar.visible);
    if(!values)
        return false;
    bar.offset = VectorDoubleGetX(values->value);
    if(bar.visible)
        (axis == ScrollAxis::Horizontal ? bar.thumb.x : bar.thumb.y) = static_cast<f32>(VectorDoubleGetY(values->value));
    return !bar.visible || ValidBar(LoadBarValues(bar), axis);
}

[[nodiscard]] static Expected<ScrollbarPlacement> StoreBarPlacement(const f64 contentExtent, const f64 viewportExtent, const Rect& track, const f32 minThumb, const f64 offset, const ScrollAxis::Enum axis)noexcept{
    ScrollbarPlacement candidate;
    candidate.contentExtent = contentExtent;
    candidate.viewportExtent = viewportExtent;
    candidate.maximum = VectorDoubleGetX(VectorDoubleMax(VectorDoubleZero(), VectorDoubleSubtract(VectorDoubleReplicate(contentExtent), VectorDoubleReplicate(viewportExtent))));
    if(!IsFinite(candidate.maximum) || (viewportExtent > 0.0 && candidate.maximum > 0.0 && candidate.maximum >= contentExtent))
        return MakeUnexpected(Failure{});
    candidate.visible = track.width > 0.0f && track.height > 0.0f;
    if(candidate.visible){
        if(candidate.maximum <= 0.0)
            return MakeUnexpected(Failure{});
        candidate.track = track;
        const f64 length = axis == ScrollAxis::Horizontal ? track.width : track.height;
        const SIMDVectorDouble lengthValue = VectorDoubleReplicate(length);
        const SIMDVectorDouble proportional = VectorDoubleMultiply(lengthValue, VectorDoubleDivide(VectorDoubleReplicate(viewportExtent), VectorDoubleReplicate(contentExtent)));
        const SIMDVectorDouble minimum = VectorDoubleMin(VectorDoubleReplicate(minThumb), lengthValue);
        const f32 thumbExtent = static_cast<f32>(VectorDoubleGetX(VectorDoubleSelect(minimum, VectorDoubleMin(lengthValue, proportional), VectorDoubleGreater(proportional, minimum))));
        candidate.thumb = track;
        if(axis == ScrollAxis::Horizontal)
            candidate.thumb.width = VectorGetX(VectorMin(VectorReplicate(thumbExtent), VectorReplicate(track.width)));
        else
            candidate.thumb.height = VectorGetX(VectorMin(VectorReplicate(thumbExtent), VectorReplicate(track.height)));
    }
    if(!StoreBarOffset(VectorDoubleReplicate(offset), axis, candidate) || !ValidBar(LoadBarValues(candidate), axis))
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
    const SIMDVectorDouble geometryPair2Operand0 = VectorDoubleSet(static_cast<f64>(padding.left), static_cast<f64>(padding.top), 0.0, 0.0);
    const SIMDVectorDouble geometryPair2Operand1 = VectorDoubleSet(static_cast<f64>(bounds.width), static_cast<f64>(bounds.height), 0.0, 0.0);
    const SIMDVectorDouble xYValue = VectorDoubleAdd(
        VectorDoubleSet(static_cast<f64>(bounds.x), static_cast<f64>(bounds.y), 0.0, 0.0),
        VectorDoubleSelect(geometryPair2Operand1, geometryPair2Operand0, VectorDoubleLess(geometryPair2Operand0, geometryPair2Operand1))
    );
    const f64 x = VectorDoubleGetX(xYValue);
    const f64 y = VectorDoubleGetY(xYValue);
    const SIMDVectorDouble geometryPair3Operand0 = VectorDoubleZero();
    const SIMDVectorDouble geometryPair3Operand1 = VectorDoubleSubtract(
        VectorDoubleSubtract(VectorDoubleSet(static_cast<f64>(bounds.width), static_cast<f64>(bounds.height), 0.0, 0.0), VectorDoubleSet(padding.left, padding.top, 0.0, 0.0)),
        VectorDoubleSet(padding.right, padding.bottom, 0.0, 0.0)
    );
    const SIMDVectorDouble widthHeightValue = VectorDoubleSelect(geometryPair3Operand1, geometryPair3Operand0, VectorDoubleGreater(geometryPair3Operand0, geometryPair3Operand1));
    const f64 width = VectorDoubleGetX(widthHeightValue);
    const f64 height = VectorDoubleGetY(widthHeightValue);
    const auto padded = MakeUiRect<UiRectPrecision::RoundedEndpoints>(x, y, width, height);
    if(!padded)
        return MakeUnexpected(Failure{});
    const f64 availableWidth = padded->width;
    const f64 availableHeight = padded->height;
    const SIMDVectorDouble geometryPair4Operand0 = VectorDoubleSet(static_cast<f64>(thickness), static_cast<f64>(thickness), 0.0, 0.0);
    const SIMDVectorDouble geometryPair4Operand1 = VectorDoubleSet(availableWidth, availableHeight, 0.0, 0.0);
    const SIMDVectorDouble verticalWidthHorizontalHeightValue = VectorDoubleSelect(geometryPair4Operand1, geometryPair4Operand0, VectorDoubleLess(geometryPair4Operand0, geometryPair4Operand1));
    const f64 verticalWidth = VectorDoubleGetX(verticalWidthHorizontalHeightValue);
    const f64 horizontalHeight = VectorDoubleGetY(verticalWidthHorizontalHeightValue);
    const f64 contentWidth = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(contentMeasure.x), VectorDoubleReplicate(caretWidth)));
    const f64 contentHeight = contentMeasure.y;
    if(!IsFinite(contentWidth))
        return MakeUnexpected(Failure{});
    bool horizontal = false;
    bool vertical = false;
    // Each axis can only add a reservation; the other axis is rechecked after either addition.
    for(u32 iteration = 0u; iteration < 3u; ++iteration){
        const SIMDVectorDouble viewportSize = VectorDoubleSubtract(VectorDoubleSet(availableWidth, availableHeight, 0.0, 0.0), VectorDoubleSet(vertical ? verticalWidth : 0.0, horizontal ? horizontalHeight : 0.0, 0.0, 0.0));
        const f64 viewportWidth = static_cast<f32>(VectorDoubleGetX(viewportSize));
        const f64 viewportHeight = static_cast<f32>(VectorDoubleGetY(viewportSize));
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
    const SIMDVectorDouble viewportSize = VectorDoubleSubtract(VectorDoubleSet(availableWidth, availableHeight, 0.0, 0.0), VectorDoubleSet(barWidth, barHeight, 0.0, 0.0));
    const auto viewport = MakeUiRect<UiRectPrecision::RoundedEndpoints>(padded->x, padded->y, VectorDoubleGetX(viewportSize), VectorDoubleGetY(viewportSize));
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
    Rect horizontalTrack;
    Rect verticalTrack;
    if(horizontal && viewportWidth > 0.0 && barHeight > 0.0){
        const auto track = MakeUiRect<UiRectPrecision::RoundedEndpoints>(padded->x, VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(padded->y), VectorDoubleReplicate(viewportHeight))), viewportWidth, barHeight);
        if(!track)
            return MakeUnexpected(Failure{});
        horizontalTrack = *track;
    }
    if(vertical && viewportHeight > 0.0 && barWidth > 0.0){
        const auto track = MakeUiRect<UiRectPrecision::RoundedEndpoints>(VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(padded->x), VectorDoubleReplicate(viewportWidth))), padded->y, barWidth, viewportHeight);
        if(!track)
            return MakeUnexpected(Failure{});
        verticalTrack = *track;
    }
    if(barWidth > 0.0 && barHeight > 0.0){
        const SIMDVectorDouble cornerOrigin = VectorDoubleAdd(VectorDoubleSet(padded->x, padded->y, 0.0, 0.0), VectorDoubleSet(viewportWidth, viewportHeight, 0.0, 0.0));
        const auto corner = MakeUiRect<UiRectPrecision::RoundedEndpoints>(VectorDoubleGetX(cornerOrigin), VectorDoubleGetY(cornerOrigin), barWidth, barHeight);
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

bool ScrollbarLayout::UpdateOffsets(const Point& scroll, ScrollViewportPlacement& out)noexcept{
    using namespace __hidden_ui_scrollbar;
    if(
        !IsValidUiExtent(scroll) || !IsValidUiRect(out.viewport) || !IsValidUiRect(out.contentClip) || !IsValidUiRect(out.corner)
        || out.horizontal.viewportExtent != static_cast<f64>(out.viewport.width)
        || out.vertical.viewportExtent != static_cast<f64>(out.viewport.height)
        || !ValidBar(LoadBarValues(out.horizontal), ScrollAxis::Horizontal) || !ValidBar(LoadBarValues(out.vertical), ScrollAxis::Vertical)
    )
        return false;
    ScrollViewportPlacement candidate = out;
    if(!StoreBarOffset(VectorDoubleReplicate(scroll.x), ScrollAxis::Horizontal, candidate.horizontal) || !StoreBarOffset(VectorDoubleReplicate(scroll.y), ScrollAxis::Vertical, candidate.vertical))
        return false;
    out = candidate;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

