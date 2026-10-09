// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scroll.h"

#include <impl/ecs_ui/toolkit/layout/validation.h>

#include <global/math/vector_double.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_scroll_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Rect Intersect(const Rect& lhs, const Rect& rhs)noexcept{
    const SIMDVectorDouble geometryPair0Operand0 = SIMDVectorDouble{ static_cast<f64>(lhs.x), static_cast<f64>(lhs.y) };
    const SIMDVectorDouble geometryPair0Operand1 = SIMDVectorDouble{ static_cast<f64>(rhs.x), static_cast<f64>(rhs.y) };
    const SIMDVectorDouble xYValue = ((geometryPair0Operand0 > geometryPair0Operand1) ? geometryPair0Operand0 : geometryPair0Operand1);
    const f64 x = xYValue.x;
    const f64 y = xYValue.y;
    const SIMDVectorDouble geometryPair1Operand0 = (SIMDVectorDouble{ static_cast<f64>(lhs.x), static_cast<f64>(lhs.y) } + SIMDVectorDouble{ lhs.width, lhs.height });
    const SIMDVectorDouble geometryPair1Operand1 = (SIMDVectorDouble{ static_cast<f64>(rhs.x), static_cast<f64>(rhs.y) } + SIMDVectorDouble{ rhs.width, rhs.height });
    const SIMDVectorDouble rightBottomValue = ((geometryPair1Operand0 < geometryPair1Operand1) ? geometryPair1Operand0 : geometryPair1Operand1);
    const f64 right = rightBottomValue.x;
    const f64 bottom = rightBottomValue.y;
    return { static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(Max(0.0, right - x)),
        static_cast<f32>(Max(0.0, bottom - y)) };
}

[[nodiscard]] static u64 RowIndex(const f64 position, const f64 rowHeight, const u64 rowCount, const bool exclusive)noexcept{
    const f64 quotient = position / rowHeight;
    const f64 rounded = exclusive ? Ceil(quotient) : Floor(quotient);
    if(rounded >= static_cast<f64>(rowCount))
        return rowCount;
    if(rounded <= 0.0)
        return 0u;
    return static_cast<u64>(rounded);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ScrollPlacement> ScrollLayout::Calculate(
    const Rect& bounds,
    const Rect& inheritedClip,
    const Insets& padding,
    const f32 scrollbarWidth,
    const f32 minimumThumb,
    const u64 rowCount,
    const f32 rowHeight,
    const f64 offset
)noexcept{
    using namespace __hidden_ui_scroll_layout;
    if(
        !IsValidUiRect(bounds) || !IsValidUiRect(inheritedClip) || !IsValidUiPadding(padding)
        || !IsFinite(scrollbarWidth) || scrollbarWidth < 0.0f
        || !IsFinite(minimumThumb) || minimumThumb < 0.0f
        || !IsFinite(rowHeight) || rowHeight <= 0.0f || !IsFinite(offset) || offset < 0.0
    )
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble geometryPair2Operand0 = SIMDVectorDouble{ 0.0, 0.0 };
    const SIMDVectorDouble geometryPair2Operand1 = ((SIMDVectorDouble{ static_cast<f64>(bounds.width), static_cast<f64>(bounds.height) } - SIMDVectorDouble{ padding.left, padding.top }) - SIMDVectorDouble{ padding.right, padding.bottom });
    const SIMDVectorDouble availableWidthAvailableHeightValue = ((geometryPair2Operand0 > geometryPair2Operand1) ? geometryPair2Operand0 : geometryPair2Operand1);
    const f64 availableWidth = availableWidthAvailableHeightValue.x;
    const f64 availableHeight = availableWidthAvailableHeightValue.y;
    const SIMDVectorDouble geometryPair3Operand0 = SIMDVectorDouble{ static_cast<f64>(padding.left), static_cast<f64>(padding.top) };
    const SIMDVectorDouble geometryPair3Operand1 = SIMDVectorDouble{ static_cast<f64>(bounds.width), static_cast<f64>(bounds.height) };
    const SIMDVectorDouble xYValue = (SIMDVectorDouble{ static_cast<f64>(bounds.x), static_cast<f64>(bounds.y) } + ((geometryPair3Operand0 < geometryPair3Operand1) ? geometryPair3Operand0 : geometryPair3Operand1));
    const f64 x = xYValue.x;
    const f64 y = xYValue.y;
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
    candidate.viewport = { static_cast<f32>(x), static_cast<f32>(y), static_cast<f32>(availableWidth - barWidth),
        static_cast<f32>(availableHeight) };
    if(!IsValidUiRect(candidate.viewport))
        return MakeUnexpected(Failure{});
    candidate.contentClip = Intersect(candidate.viewport, inheritedClip);
    candidate.scrollbarVisible = barWidth > 0.0 && availableHeight > 0.0;
    if(candidate.scrollbarVisible){
        candidate.track = { static_cast<f32>(x + availableWidth - barWidth), static_cast<f32>(y),
            static_cast<f32>(barWidth), static_cast<f32>(availableHeight) };
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
        candidate.firstRow = RowIndex(Max(0.0, start), rowHeight, rowCount, false);
        candidate.endRow = Max(candidate.firstRow, RowIndex(Max(0.0, end), rowHeight, rowCount, true));
    }
    return candidate;
}

Expected<Rect> ScrollLayout::RowBounds(const u64 index, const ScrollPlacement& placement, const f32 rowHeight)noexcept{
    using namespace __hidden_ui_scroll_layout;
    if(
        index < placement.firstRow || index >= placement.endRow || index >= placement.rowCount
        || !IsValidUiRect(placement.viewport) || !IsFinite(placement.offset) || placement.offset < 0.0
        || !IsFinite(rowHeight) || rowHeight <= 0.0f
    )
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

