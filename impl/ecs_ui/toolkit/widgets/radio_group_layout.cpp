// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group_layout.h"

#include "radio_group.h"

#include <impl/ecs_ui/toolkit/layout/rectangle.h>
#include <impl/ecs_ui/toolkit/rect_math.h>

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<RadioGroupMetrics> RadioGroupLayout::Measure(const u32 count, const Point& maximumLabel, const RadioGroupOptions& options, const RadioGroupStyle& style)noexcept{
    RadioGroupMetrics candidate;
    if(!TryMeasureRadioGroup(count, maximumLabel, options, style, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}

Expected<RadioGroupPlacement> RadioGroupLayout::Place(const Rect& bounds, const Rect& clip, const RadioGroupChoices& choices, const RadioGroupMetrics& metrics)noexcept{
    RadioGroupPlacement candidate;
    if(!TryPlaceRadioGroup(bounds, clip, choices, metrics, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

