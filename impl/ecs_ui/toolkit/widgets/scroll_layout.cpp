// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scroll.h"

#include <impl/ecs_ui/toolkit/layout/validation.h>

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_scroll_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Rect Intersect(const Rect& lhs, const Rect& rhs)noexcept{
    const SIMDVectorDouble geometryPair0Operand0 = VectorDoubleSet(static_cast<f64>(lhs.x), static_cast<f64>(lhs.y), 0.0, 0.0);
    const SIMDVectorDouble geometryPair0Operand1 = VectorDoubleSet(static_cast<f64>(rhs.x), static_cast<f64>(rhs.y), 0.0, 0.0);
    const SIMDVectorDouble xYValue = VectorDoubleSelect(geometryPair0Operand1, geometryPair0Operand0, VectorDoubleGreater(geometryPair0Operand0, geometryPair0Operand1));
    const f64 x = VectorDoubleGetX(xYValue);
    const f64 y = VectorDoubleGetY(xYValue);
    const SIMDVectorDouble geometryPair1Operand0 = VectorDoubleAdd(VectorDoubleSet(static_cast<f64>(lhs.x), static_cast<f64>(lhs.y), 0.0, 0.0), VectorDoubleSet(lhs.width, lhs.height, 0.0, 0.0));
    const SIMDVectorDouble geometryPair1Operand1 = VectorDoubleAdd(VectorDoubleSet(static_cast<f64>(rhs.x), static_cast<f64>(rhs.y), 0.0, 0.0), VectorDoubleSet(rhs.width, rhs.height, 0.0, 0.0));
    const SIMDVectorDouble rightBottomValue = VectorDoubleSelect(geometryPair1Operand1, geometryPair1Operand0, VectorDoubleLess(geometryPair1Operand0, geometryPair1Operand1));
    const SIMDVectorDouble extent = VectorDoubleSubtract(rightBottomValue, xYValue);
    const SIMDVectorDouble size = VectorDoubleSelect(extent, VectorDoubleZero(), VectorDoubleGreater(VectorDoubleZero(), extent));
    return { static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(VectorDoubleGetX(size)), static_cast<f32>(VectorDoubleGetY(size)) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ScrollPlacement> ScrollLayout::Calculate(const Rect& bounds, const Rect& inheritedClip, const Insets& padding, const f32 scrollbarWidth, const f32 minimumThumb, const u64 rowCount, const f32 rowHeight, const f64 offset)noexcept{
    using namespace __hidden_ui_scroll_layout;
    if(
        !IsValidUiRect(bounds) || !IsValidUiRect(inheritedClip) || !IsValidUiPadding(padding)
        || !IsFinite(scrollbarWidth) || scrollbarWidth < 0.0f
        || !IsFinite(minimumThumb) || minimumThumb < 0.0f
        || !IsFinite(rowHeight) || rowHeight <= 0.0f || !IsFinite(offset) || offset < 0.0
    )
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble geometryPair2Operand0 = VectorDoubleZero();
    const SIMDVectorDouble geometryPair2Operand1 = VectorDoubleSubtract(
        VectorDoubleSubtract(VectorDoubleSet(static_cast<f64>(bounds.width), static_cast<f64>(bounds.height), 0.0, 0.0), VectorDoubleSet(padding.left, padding.top, 0.0, 0.0)),
        VectorDoubleSet(padding.right, padding.bottom, 0.0, 0.0)
    );
    const SIMDVectorDouble availableWidthAvailableHeightValue = VectorDoubleSelect(geometryPair2Operand1, geometryPair2Operand0, VectorDoubleGreater(geometryPair2Operand0, geometryPair2Operand1));
    const f64 availableWidth = VectorDoubleGetX(availableWidthAvailableHeightValue);
    const f64 availableHeight = VectorDoubleGetY(availableWidthAvailableHeightValue);
    const SIMDVectorDouble geometryPair3Operand0 = VectorDoubleSet(static_cast<f64>(padding.left), static_cast<f64>(padding.top), 0.0, 0.0);
    const SIMDVectorDouble geometryPair3Operand1 = VectorDoubleSet(static_cast<f64>(bounds.width), static_cast<f64>(bounds.height), 0.0, 0.0);
    const SIMDVectorDouble xYValue = VectorDoubleAdd(
        VectorDoubleSet(static_cast<f64>(bounds.x), static_cast<f64>(bounds.y), 0.0, 0.0),
        VectorDoubleSelect(geometryPair3Operand1, geometryPair3Operand0, VectorDoubleLess(geometryPair3Operand0, geometryPair3Operand1))
    );
    const f64 x = VectorDoubleGetX(xYValue);
    const f64 y = VectorDoubleGetY(xYValue);
    ScrollPlacement candidate;
    candidate.bounds = bounds;
    candidate.rowCount = rowCount;
    candidate.contentHeight = static_cast<f64>(rowCount) * rowHeight;
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
    candidate.contentClip = Intersect(candidate.viewport, inheritedClip);
    candidate.scrollbarVisible = barWidth > 0.0 && availableHeight > 0.0;
    if(candidate.scrollbarVisible){
        candidate.track = { static_cast<f32>(x + availableWidth - barWidth), static_cast<f32>(y), static_cast<f32>(barWidth), static_cast<f32>(availableHeight) };
        const f64 proportionalHeight = availableHeight * (availableHeight / candidate.contentHeight);
        const f64 thumbHeight = Clamp(proportionalHeight, Min(static_cast<f64>(minimumThumb), availableHeight), availableHeight);
        const f64 thumbY = y + (availableHeight - thumbHeight) * (candidate.offset / candidate.maxOffset);
        candidate.thumb = { candidate.track.x, static_cast<f32>(thumbY), candidate.track.width, static_cast<f32>(thumbHeight) };
        if(!IsValidUiRect(candidate.track) || !IsValidUiRect(candidate.thumb))
            return MakeUnexpected(Failure{});
    }
    if(candidate.contentClip.width > 0.0f && candidate.contentClip.height > 0.0f && rowCount > 0u){
        const f64 clippedTop = static_cast<f64>(candidate.contentClip.y) - candidate.viewport.y;
        const f64 start = candidate.offset + clippedTop;
        const f64 end = start + candidate.contentClip.height;
        const SIMDVectorDouble endpoints = VectorDoubleSet(start, end, 0.0, 0.0);
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
    using namespace __hidden_ui_scroll_layout;
    if(index < placement.firstRow || index >= placement.endRow || index >= placement.rowCount || !IsValidUiRect(placement.viewport) || !IsFinite(placement.offset) || placement.offset < 0.0 || !IsFinite(rowHeight) || rowHeight <= 0.0f)
        return MakeUnexpected(Failure{});
    const f64 y = static_cast<f64>(index) * rowHeight - placement.offset + placement.viewport.y;
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

