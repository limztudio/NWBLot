// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_layout.h"

#include <impl/ecs_ui/toolkit/images/skin_region_geometry.h>
#include <impl/ecs_ui/toolkit/layout/rectangle.h>

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ImageMetrics> ImageLayout::Measure(const ImageOptions& options, const UiSkinRegion& region, const f32 density)noexcept{
    ImageMetrics candidate;
    if(!TryMeasureRegionImage(options, region, density, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}

Expected<ImageMetrics> ImageLayout::Measure(const ImageOptions& options, const ImageSource& source)noexcept{
    if(!IsValidLayoutSize(options.width) || !IsValidLayoutSize(options.height) || !IsValidUiColor(options.tint))
        return MakeUnexpected(Failure{});
    const Texture& texture = source.texture();
    // The immutable source factory admitted its complete static 2D payload before this per-frame measurement.
    const ImageMetrics candidate{ { static_cast<f32>(texture.width()), static_cast<f32>(texture.height()) } };
    return candidate;
}

Expected<ImagePlacement> ImageLayout::Place(const Rect& bounds, const Rect& clip)noexcept{
    ImagePlacement candidate;
    if(!TryPlaceImage(bounds, clip, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

