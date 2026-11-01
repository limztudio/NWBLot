// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "progress_layout.h"

#include <impl/ecs_ui/toolkit/images/skin_region_geometry.h>
#include <impl/ecs_ui/toolkit/layout/rectangle.h>
#include <impl/ecs_ui/toolkit/rect_math.h>

#include <global/simd/result.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>
#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ProgressMetrics> ProgressLayout::Measure(const ProgressOptions& options, const ProgressStyle& style, const UiSkinRegion& track, const UiSkinRegion& fill, const f32 density)noexcept{
    ProgressMetrics candidate;
    if(!TryMeasureProgress(options, style, track, fill, density, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}

Expected<ProgressPlacement> ProgressLayout::Place(const Rect& bounds, const Rect& clip, const ProgressMetrics& metrics, const f64 fraction)noexcept{
    ProgressPlacement candidate;
    if(!TryPlaceProgress(bounds, clip, metrics, fraction, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

