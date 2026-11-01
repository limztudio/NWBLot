// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scroll.h"

#include <impl/ecs_ui/toolkit/layout/rectangle.h>
#include <impl/ecs_ui/toolkit/rect_math.h>

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ScrollPlacement> ScrollLayout::Calculate(const Rect& bounds, const Rect& inheritedClip, const Insets& padding, const f32 scrollbarWidth, const f32 minimumThumb, const u64 rowCount, const f32 rowHeight, const f64 offset)noexcept{
    if(
        !IsValidUiRect(bounds) || !IsValidUiRect(inheritedClip) || !IsValidUiPadding(padding)
        || !IsFinite(scrollbarWidth) || scrollbarWidth < 0.0f
        || !IsFinite(minimumThumb) || minimumThumb < 0.0f
        || !IsFinite(rowHeight) || rowHeight <= 0.0f || !IsFinite(offset) || offset < 0.0
    )
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble inset = InsetUiRectValue(bounds, padding);
    const f64 x = VectorDoubleGetX(inset);
    const f64 y = VectorDoubleGetY(inset);
    const f64 availableWidth = VectorDoubleGetZ(inset);
    const f64 availableHeight = VectorDoubleGetW(inset);
    ScrollPlacement candidate;
    candidate.bounds = bounds;
    candidate.rowCount = rowCount;
    candidate.contentHeight = static_cast<f64>(rowCount) * static_cast<f64>(rowHeight);
    if(!IsFinite(candidate.contentHeight))
        return MakeUnexpected(Failure{});
    candidate.maxOffset = Max(0.0, candidate.contentHeight - availableHeight);
    if(availableHeight > 0.0 && candidate.maxOffset > 0.0 && candidate.maxOffset >= candidate.contentHeight)
        return MakeUnexpected(Failure{});
    candidate.offset = Min(offset, candidate.maxOffset);
    const f64 barWidth = candidate.maxOffset > 0.0 ? Min(static_cast<f64>(scrollbarWidth), availableWidth) : 0.0;
    candidate.viewport = { static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(availableWidth - barWidth), static_cast<f32>(availableHeight) };
    if(!IsValidUiRect(candidate.viewport))
        return MakeUnexpected(Failure{});
    candidate.contentClip = StoreRect(IntersectRectValue(LoadRect(candidate.viewport), LoadRect(inheritedClip)));
    candidate.scrollbarVisible = barWidth > 0.0 && availableHeight > 0.0;
    if(candidate.scrollbarVisible){
        candidate.track = { static_cast<f32>((x + availableWidth) - barWidth), static_cast<f32>(y), static_cast<f32>(barWidth), static_cast<f32>(availableHeight) };
        const f64 proportionalHeight = availableHeight * (availableHeight / candidate.contentHeight);
        const f64 minimumSize = Min(static_cast<f64>(minimumThumb), availableHeight);
        const f64 thumbHeight = proportionalHeight > minimumSize ? Min(availableHeight, proportionalHeight) : minimumSize;
        const f64 thumbTravel = availableHeight - thumbHeight;
        const f64 thumbY = y + thumbTravel * (candidate.offset / candidate.maxOffset);
        candidate.thumb = { candidate.track.x, static_cast<f32>(thumbY), candidate.track.width, static_cast<f32>(thumbHeight) };
        if(!IsValidUiRect(candidate.track) || !IsValidUiRect(candidate.thumb))
            return MakeUnexpected(Failure{});
    }
    if(candidate.contentClip.width > 0.0f && candidate.contentClip.height > 0.0f && rowCount > 0u){
        const f64 clippedTop = static_cast<f64>(candidate.contentClip.y) - static_cast<f64>(candidate.viewport.y);
        const SIMDVectorDouble start = VectorDoubleReplicate(candidate.offset + clippedTop);
        const SIMDVectorDouble endpoints = VectorDoubleAdd(start, VectorDoubleSet(0.0, candidate.contentClip.height, 0.0, 0.0));
        const SIMDVectorDouble positions = VectorDoubleSelect(endpoints, VectorDoubleZero(), VectorDoubleGreater(VectorDoubleZero(), endpoints));
        const SIMDVectorDouble quotients = VectorDoubleDivide(positions, VectorDoubleReplicate(rowHeight));
        const SIMDVectorDouble rounded = VectorDoubleSelect(VectorDoubleFloor(quotients), VectorDoubleCeiling(quotients), s_DoubleSIMDMaskY);
        const f64 firstRow = VectorDoubleGetX(rounded);
        const f64 endRow = VectorDoubleGetY(rounded);
        candidate.firstRow = firstRow >= static_cast<f64>(rowCount) ? rowCount : firstRow <= 0.0 ? 0u : static_cast<u64>(firstRow);
        const u64 boundedEnd = endRow >= static_cast<f64>(rowCount) ? rowCount : endRow <= 0.0 ? 0u : static_cast<u64>(endRow);
        candidate.endRow = Max(candidate.firstRow, boundedEnd);
    }
    return candidate;
}

Expected<Rect> ScrollLayout::RowBounds(const u64 index, const ScrollPlacement& placement, const f32 rowHeight)noexcept{
    if(index < placement.firstRow || index >= placement.endRow || index >= placement.rowCount || !IsValidUiRect(placement.viewport) || !IsFinite(placement.offset) || placement.offset < 0.0 || !IsFinite(rowHeight) || rowHeight <= 0.0f)
        return MakeUnexpected(Failure{});
    const f64 y = (static_cast<f64>(index) * static_cast<f64>(rowHeight) - placement.offset) + static_cast<f64>(placement.viewport.y);
    if(!IsFinite(y) || y < -Limit<f32>::s_Max || y > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    const Rect candidate{ placement.viewport.x, static_cast<f32>(y), placement.viewport.width, rowHeight };
    if(!IsValidUiRect(candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

