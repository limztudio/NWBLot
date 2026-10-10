// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_layout.h"

#include "image.h"

#include <impl/ecs_ui/toolkit/images/skin_region_geometry.h>
#include <impl/ecs_ui/toolkit/layout/rectangle.h>

#include <global/math/vector_double.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_image_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidSize(const LayoutSize& size)noexcept{
    return
        size.policy <= LayoutSizePolicy::Stretch && IsFinite(size.value) && size.value >= 0.0f
        && (size.policy != LayoutSizePolicy::Stretch || size.value > 0.0f)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ImageMetrics> ImageLayout::Measure(
    const ImageOptions& options,
    const UiSkinRegion& region,
    const f32 density
)noexcept{
    using namespace __hidden_ui_image_layout;
    if(
        !ValidSize(options.width) || !ValidSize(options.height) || !IsValidUiColor(options.tint)
        || !IsFinite(density) || density <= 0.0f
        || !IsFinite(region.minimumWidth) || region.minimumWidth < 0.0f
        || !IsFinite(region.minimumHeight) || region.minimumHeight < 0.0f
    )
        return MakeUnexpected(Failure{});
    const auto sliceExtents = LoadUiSkinSliceExtents(region);
    if(!sliceExtents)
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble geometryPair0Operand0 = SIMDVectorDouble{ static_cast<f64>(region.rectangle.width), static_cast<f64>(region.rectangle.height) };
    const SIMDVectorDouble geometryPair0Operand1 = *sliceExtents;
    const SIMDVectorDouble pixelWidthPixelHeightValue = ((geometryPair0Operand0 > geometryPair0Operand1) ? geometryPair0Operand0 : geometryPair0Operand1);
    const f64 pixelWidth = pixelWidthPixelHeightValue.x;
    const f64 pixelHeight = pixelWidthPixelHeightValue.y;
    const SIMDVectorDouble geometryPair1Operand0 = SIMDVectorDouble{ static_cast<f64>(region.minimumWidth), static_cast<f64>(region.minimumHeight) };
    const SIMDVectorDouble geometryPair1Operand1 = (SIMDVectorDouble{ pixelWidth, pixelHeight } / SIMDVectorDouble{ density, density });
    const SIMDVectorDouble widthHeightValue = ((geometryPair1Operand0 > geometryPair1Operand1) ? geometryPair1Operand0 : geometryPair1Operand1);
    const f64 width = widthHeightValue.x;
    const f64 height = widthHeightValue.y;
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    const ImageMetrics candidate{ { static_cast<f32>(width), static_cast<f32>(height) } };
    return candidate;
}

Expected<ImageMetrics> ImageLayout::Measure(const ImageOptions& options, const ImageSource& source)noexcept{
    using namespace __hidden_ui_image_layout;
    if(!ValidSize(options.width) || !ValidSize(options.height) || !IsValidUiColor(options.tint))
        return MakeUnexpected(Failure{});
    const Texture& texture = source.texture();
    // The immutable source factory admitted its complete static 2D payload before this per-frame measurement.
    const ImageMetrics candidate{ { static_cast<f32>(texture.width()), static_cast<f32>(texture.height()) } };
    return candidate;
}

Expected<ImagePlacement> ImageLayout::Place(const Rect& bounds, const Rect& clip)noexcept{
    using namespace __hidden_ui_image_layout;
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

