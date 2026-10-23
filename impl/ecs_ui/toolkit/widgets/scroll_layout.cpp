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


[[nodiscard]] static SIMDVectorDouble Intersect(const SIMDVectorDouble lhs, const SIMDVectorDouble rhs)noexcept{
    const SIMDVectorDouble lhsOrigin = VectorDoubleSwizzle<0, 1, 0, 1>(lhs);
    const SIMDVectorDouble rhsOrigin = VectorDoubleSwizzle<0, 1, 0, 1>(rhs);
    const SIMDVectorDouble origin = VectorDoubleSelect(rhsOrigin, lhsOrigin, VectorDoubleGreater(lhsOrigin, rhsOrigin));
    const SIMDVectorDouble lhsEnd = VectorDoubleAdd(lhsOrigin, VectorDoubleSwizzle<2, 3, 2, 3>(lhs));
    const SIMDVectorDouble rhsEnd = VectorDoubleAdd(rhsOrigin, VectorDoubleSwizzle<2, 3, 2, 3>(rhs));
    const SIMDVectorDouble end = VectorDoubleSelect(rhsEnd, lhsEnd, VectorDoubleLess(lhsEnd, rhsEnd));
    const SIMDVectorDouble extent = VectorDoubleSubtract(end, origin);
    const SIMDVectorDouble size = VectorDoubleSelect(extent, VectorDoubleZero(), VectorDoubleGreater(VectorDoubleZero(), extent));
    return VectorDoubleSelect(VectorDoubleSwizzle<0, 1, 0, 1>(size), origin, s_DoubleSIMDMaskXY);
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
    candidate.contentHeight = VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(rowCount)), VectorDoubleReplicate(rowHeight)));
    if(!IsFinite(candidate.contentHeight))
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble widthValue = VectorDoubleSplatX(availableWidthAvailableHeightValue);
    const SIMDVectorDouble heightValue = VectorDoubleSplatY(availableWidthAvailableHeightValue);
    candidate.maxOffset = VectorDoubleGetX(VectorDoubleMax(VectorDoubleZero(), VectorDoubleSubtract(VectorDoubleReplicate(candidate.contentHeight), heightValue)));
    if(availableHeight > 0.0 && candidate.maxOffset > 0.0 && candidate.maxOffset >= candidate.contentHeight)
        return MakeUnexpected(Failure{});
    candidate.offset = VectorDoubleGetX(VectorDoubleMin(VectorDoubleReplicate(offset), VectorDoubleReplicate(candidate.maxOffset)));
    const SIMDVectorDouble barWidthVector = candidate.maxOffset > 0.0 ? VectorDoubleAndInt(VectorDoubleMin(VectorDoubleReplicate(scrollbarWidth), widthValue), s_DoubleSIMDMaskX) : VectorDoubleZero();
    const f64 barWidth = VectorDoubleGetX(barWidthVector);
    const SIMDVectorDouble viewportSize = VectorDoubleSubtract(availableWidthAvailableHeightValue, barWidthVector);
    candidate.viewport = { static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(VectorDoubleGetX(viewportSize)), static_cast<f32>(availableHeight) };
    if(!IsValidUiRect(candidate.viewport))
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble contentClip = Intersect(VectorDoubleSet(candidate.viewport.x, candidate.viewport.y, candidate.viewport.width, candidate.viewport.height), VectorDoubleSet(inheritedClip.x, inheritedClip.y, inheritedClip.width, inheritedClip.height));
    candidate.contentClip = { static_cast<f32>(VectorDoubleGetX(contentClip)), static_cast<f32>(VectorDoubleGetY(contentClip)), static_cast<f32>(VectorDoubleGetZ(contentClip)), static_cast<f32>(VectorDoubleGetW(contentClip)) };
    candidate.scrollbarVisible = barWidth > 0.0 && availableHeight > 0.0;
    if(candidate.scrollbarVisible){
        const SIMDVectorDouble trackOrigin = VectorDoubleSubtract(VectorDoubleAdd(xYValue, VectorDoubleAndInt(widthValue, s_DoubleSIMDMaskX)), barWidthVector);
        candidate.track = { static_cast<f32>(VectorDoubleGetX(trackOrigin)), static_cast<f32>(y), static_cast<f32>(barWidth), static_cast<f32>(availableHeight) };
        const SIMDVectorDouble proportionalHeight = VectorDoubleMultiply(heightValue, VectorDoubleDivide(heightValue, VectorDoubleReplicate(candidate.contentHeight)));
        const SIMDVectorDouble minimumSize = VectorDoubleMin(VectorDoubleReplicate(minimumThumb), heightValue);
        const SIMDVectorDouble thumbSize = VectorDoubleSelect(minimumSize, VectorDoubleMin(heightValue, proportionalHeight), VectorDoubleGreater(proportionalHeight, minimumSize));
        const f64 thumbHeight = VectorDoubleGetX(thumbSize);
        const SIMDVectorDouble thumbTravel = VectorDoubleSubtract(heightValue, thumbSize);
        const f64 thumbY = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleSplatY(xYValue), VectorDoubleMultiply(thumbTravel, VectorDoubleDivide(VectorDoubleReplicate(candidate.offset), VectorDoubleReplicate(candidate.maxOffset)))));
        candidate.thumb = { candidate.track.x, static_cast<f32>(thumbY), candidate.track.width, static_cast<f32>(thumbHeight) };
        if(!IsValidUiRect(candidate.track) || !IsValidUiRect(candidate.thumb))
            return MakeUnexpected(Failure{});
    }
    if(candidate.contentClip.width > 0.0f && candidate.contentClip.height > 0.0f && rowCount > 0u){
        const SIMDVectorDouble clippedTop = VectorDoubleSubtract(VectorDoubleReplicate(candidate.contentClip.y), VectorDoubleReplicate(candidate.viewport.y));
        const SIMDVectorDouble start = VectorDoubleAdd(VectorDoubleReplicate(candidate.offset), clippedTop);
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
    using namespace __hidden_ui_scroll_layout;
    if(index < placement.firstRow || index >= placement.endRow || index >= placement.rowCount || !IsValidUiRect(placement.viewport) || !IsFinite(placement.offset) || placement.offset < 0.0 || !IsFinite(rowHeight) || rowHeight <= 0.0f)
        return MakeUnexpected(Failure{});
    const f64 y = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleSubtract(VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(index)), VectorDoubleReplicate(rowHeight)), VectorDoubleReplicate(placement.offset)), VectorDoubleReplicate(placement.viewport.y)));
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

