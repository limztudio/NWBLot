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
    if(
        !IsValidLayoutSize(options.width) || !IsValidLayoutSize(options.height) || !IsValidUiColor(options.tint)
        || !IsFinite(density) || density <= 0.0f
    )
        return MakeUnexpected(Failure{});
    const auto sliceExtents = LoadUiSkinSliceExtents(region);
    if(!sliceExtents)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble geometryPair0Operand0 = VectorDoubleSet(static_cast<f64>(region.rectangle.width), static_cast<f64>(region.rectangle.height), 0.0, 0.0);
    const SIMDVectorDouble geometryPair0Operand1 = sliceExtents->value;
    const SIMDVectorDouble pixelWidthPixelHeightValue = VectorDoubleSelect(geometryPair0Operand1, geometryPair0Operand0, VectorDoubleGreater(geometryPair0Operand0, geometryPair0Operand1));
    const auto minimum = LoadUiSkinMinimumSize(region, pixelWidthPixelHeightValue, VectorDoubleReplicate(density));
    if(!minimum)
        return MakeUnexpected(Failure{});
    const ImageMetrics candidate{ { VectorGetX(minimum->value), VectorGetY(minimum->value) } };
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
    if(!IsBoundedUiRect(bounds) || !IsBoundedUiRect(clip))
        return MakeUnexpected(Failure{});
    const auto intersection = IntersectUiRects<UiRectPrecision::EmptyPaint>(bounds, clip);
    if(!intersection)
        return MakeUnexpected(Failure{});
    ImagePlacement candidate;
    candidate.bounds = bounds;
    candidate.clip = *intersection;
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

