// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_layout.h"

#include "image.h"

#include <impl/ecs_ui/toolkit/layout/validation.h>

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
        || !IsFinite(density) || density <= 0.0f || region.rectangle.width == 0u || region.rectangle.height == 0u
        || !IsFinite(region.minimumWidth) || region.minimumWidth < 0.0f
        || !IsFinite(region.minimumHeight) || region.minimumHeight < 0.0f
    )
        return MakeUnexpected(Failure{});
    const u64 horizontalSlices = static_cast<u64>(region.sliceInsets.left) + region.sliceInsets.right;
    const u64 verticalSlices = static_cast<u64>(region.sliceInsets.top) + region.sliceInsets.bottom;
    if(region.drawMode == UiSkinDrawMode::Sprite){
        if(horizontalSlices != 0u || verticalSlices != 0u)
            return MakeUnexpected(Failure{});
    }
    else if(region.drawMode == UiSkinDrawMode::NineSlice){
        if(horizontalSlices > region.rectangle.width || verticalSlices > region.rectangle.height)
            return MakeUnexpected(Failure{});
    }
    else
        return MakeUnexpected(Failure{});
    const SIMDVectorDouble geometryPair0Operand0 = SIMDVectorDouble{ static_cast<f64>(region.rectangle.width), static_cast<f64>(region.rectangle.height) };
    const SIMDVectorDouble geometryPair0Operand1 = SIMDVectorDouble{ static_cast<f64>(horizontalSlices), static_cast<f64>(verticalSlices) };
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
    const SIMDVectorDouble geometryPair2Operand0 = SIMDVectorDouble{ static_cast<f64>(bounds.x), static_cast<f64>(bounds.y) };
    const SIMDVectorDouble geometryPair2Operand1 = SIMDVectorDouble{ static_cast<f64>(clip.x), static_cast<f64>(clip.y) };
    const SIMDVectorDouble leftTopValue = ((geometryPair2Operand0 > geometryPair2Operand1) ? geometryPair2Operand0 : geometryPair2Operand1);
    const f64 left = leftTopValue.x;
    const f64 top = leftTopValue.y;
    const SIMDVectorDouble geometryPair3Operand0 = (SIMDVectorDouble{ static_cast<f64>(bounds.x), static_cast<f64>(bounds.y) } + SIMDVectorDouble{ bounds.width, bounds.height });
    const SIMDVectorDouble geometryPair3Operand1 = (SIMDVectorDouble{ static_cast<f64>(clip.x), static_cast<f64>(clip.y) } + SIMDVectorDouble{ clip.width, clip.height });
    const SIMDVectorDouble rightBottomValue = ((geometryPair3Operand0 < geometryPair3Operand1) ? geometryPair3Operand0 : geometryPair3Operand1);
    const f64 right = rightBottomValue.x;
    const f64 bottom = rightBottomValue.y;
    const SIMDVectorDouble geometryPair4Operand0 = SIMDVectorDouble{ 0.0, 0.0 };
    const SIMDVectorDouble geometryPair4Operand1 = (SIMDVectorDouble{ right, bottom } - SIMDVectorDouble{ left, top });
    const SIMDVectorDouble widthHeightValue = ((geometryPair4Operand0 > geometryPair4Operand1) ? geometryPair4Operand0 : geometryPair4Operand1);
    const f64 width = widthHeightValue.x;
    const f64 height = widthHeightValue.y;
    if(width > Limit<f32>::s_Max || height > Limit<f32>::s_Max)
        return MakeUnexpected(Failure{});
    ImagePlacement candidate;
    candidate.bounds = bounds;
    candidate.clip = { static_cast<f32>(left), static_cast<f32>(top), static_cast<f32>(width), static_cast<f32>(height) };
    if(candidate.clip.width > 0.0f && candidate.clip.x + candidate.clip.width <= candidate.clip.x)
        candidate.clip.width = 0.0f;
    if(candidate.clip.height > 0.0f && candidate.clip.y + candidate.clip.height <= candidate.clip.y)
        candidate.clip.height = 0.0f;
    if(!IsBoundedUiRect(candidate.clip))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

