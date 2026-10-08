// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_layout.h"

#include "image.h"

#include <impl/ecs_ui/toolkit/layout/validation.h>

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
    const f64 pixelWidth = Max(static_cast<f64>(region.rectangle.width), static_cast<f64>(horizontalSlices));
    const f64 pixelHeight = Max(static_cast<f64>(region.rectangle.height), static_cast<f64>(verticalSlices));
    const f64 width = Max(static_cast<f64>(region.minimumWidth), pixelWidth / density);
    const f64 height = Max(static_cast<f64>(region.minimumHeight), pixelHeight / density);
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
    const f64 left = Max(static_cast<f64>(bounds.x), static_cast<f64>(clip.x));
    const f64 top = Max(static_cast<f64>(bounds.y), static_cast<f64>(clip.y));
    const f64 right = Min(static_cast<f64>(bounds.x) + bounds.width, static_cast<f64>(clip.x) + clip.width);
    const f64 bottom = Min(static_cast<f64>(bounds.y) + bounds.height, static_cast<f64>(clip.y) + clip.height);
    const f64 width = Max(0.0, right - left);
    const f64 height = Max(0.0, bottom - top);
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

