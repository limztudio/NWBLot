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
    ScrollPlacement candidate;
    if(!TryCalculateScrollPlacement(bounds, inheritedClip, padding, scrollbarWidth, minimumThumb, rowCount, rowHeight, offset, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}

Expected<Rect> ScrollLayout::RowBounds(const u64 index, const ScrollPlacement& placement, const f32 rowHeight)noexcept{
    Rect candidate;
    if(!TryCalculateScrollRowBounds(index, placement, rowHeight, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

