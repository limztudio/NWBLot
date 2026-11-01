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


Expected<ScrollViewportPlacement> ScrollbarLayout::Calculate(const Rect& bounds, const Rect& clip, const Insets& padding, const Point& contentMeasure, const f32 caretWidth, const Point& previousScroll, const f32 thickness, const f32 minThumb)noexcept{
    ScrollViewportPlacement candidate;
    if(!TryCalculateScrollViewportPlacement(bounds, clip, padding, contentMeasure, caretWidth, previousScroll, thickness, minThumb, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}

b8 ScrollbarLayout::UpdateOffsets(const Point& scroll, ScrollViewportPlacement& placement)noexcept{
    return TryUpdateScrollViewportOffsets(scroll, placement);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

