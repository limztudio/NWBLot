// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_layout.h"

#include "image.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_image_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidSize(const LayoutSize& size){
    return
        size.policy <= LayoutSizePolicy::Stretch && IsFinite(size.value) && size.value >= 0.0f
        && (size.policy != LayoutSizePolicy::Stretch || size.value > 0.0f)
    ;
}

[[nodiscard]] static bool ValidColor(const Color& color){
    return
        IsFinite(color.r) && color.r >= 0.0f && IsFinite(color.g) && color.g >= 0.0f
        && IsFinite(color.b) && color.b >= 0.0f && IsFinite(color.a) && color.a >= 0.0f && color.a <= 1.0f
    ;
}

[[nodiscard]] static bool ValidRect(const Rect& rectangle){
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y)
        && IsFinite(rectangle.width) && rectangle.width >= 0.0f
        && IsFinite(rectangle.height) && rectangle.height >= 0.0f
        && static_cast<f64>(rectangle.x) + rectangle.width <= Limit<f32>::s_Max
        && static_cast<f64>(rectangle.y) + rectangle.height <= Limit<f32>::s_Max
        && IsFinite(rectangle.x + rectangle.width) && IsFinite(rectangle.y + rectangle.height)
        && (rectangle.width == 0.0f || rectangle.x + rectangle.width > rectangle.x)
        && (rectangle.height == 0.0f || rectangle.y + rectangle.height > rectangle.y)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ImageLayout::Measure(
    const ImageOptions& options,
    const UiSkinRegion& region,
    const f32 density,
    ImageMetrics& out){
    using namespace __hidden_ui_image_layout;
    if(
        !ValidSize(options.width) || !ValidSize(options.height) || !ValidColor(options.tint)
        || !IsFinite(density) || density <= 0.0f || region.rectangle.width == 0u || region.rectangle.height == 0u
        || !IsFinite(region.minimumWidth) || region.minimumWidth < 0.0f
        || !IsFinite(region.minimumHeight) || region.minimumHeight < 0.0f
    )
        return false;
    const u64 horizontalSlices = static_cast<u64>(region.sliceInsets.left) + region.sliceInsets.right;
    const u64 verticalSlices = static_cast<u64>(region.sliceInsets.top) + region.sliceInsets.bottom;
    if(region.drawMode == UiSkinDrawMode::Sprite){
        if(horizontalSlices != 0u || verticalSlices != 0u)
            return false;
    }
    else if(region.drawMode == UiSkinDrawMode::NineSlice){
        if(horizontalSlices > region.rectangle.width || verticalSlices > region.rectangle.height)
            return false;
    }
    else
        return false;
    const f64 pixelWidth = Max(static_cast<f64>(region.rectangle.width), static_cast<f64>(horizontalSlices));
    const f64 pixelHeight = Max(static_cast<f64>(region.rectangle.height), static_cast<f64>(verticalSlices));
    const f64 width = Max(static_cast<f64>(region.minimumWidth), pixelWidth / density);
    const f64 height = Max(static_cast<f64>(region.minimumHeight), pixelHeight / density);
    if(!IsFinite(width) || width > Limit<f32>::s_Max || !IsFinite(height) || height > Limit<f32>::s_Max)
        return false;
    const ImageMetrics candidate{ { static_cast<f32>(width), static_cast<f32>(height) } };
    out = candidate;
    return true;
}

bool ImageLayout::Measure(const ImageOptions& options, const ImageSource& source, ImageMetrics& out){
    using namespace __hidden_ui_image_layout;
    if(!ValidSize(options.width) || !ValidSize(options.height) || !ValidColor(options.tint))
        return false;
    const Texture& texture = source.texture();
    // The immutable source factory admitted its complete static 2D payload before this per-frame measurement.
    const ImageMetrics candidate{ { static_cast<f32>(texture.width()), static_cast<f32>(texture.height()) } };
    out = candidate;
    return true;
}

bool ImageLayout::Place(const Rect& bounds, const Rect& clip, ImagePlacement& out){
    using namespace __hidden_ui_image_layout;
    if(!ValidRect(bounds) || !ValidRect(clip))
        return false;
    const f64 left = Max(static_cast<f64>(bounds.x), static_cast<f64>(clip.x));
    const f64 top = Max(static_cast<f64>(bounds.y), static_cast<f64>(clip.y));
    const f64 right = Min(static_cast<f64>(bounds.x) + bounds.width, static_cast<f64>(clip.x) + clip.width);
    const f64 bottom = Min(static_cast<f64>(bounds.y) + bounds.height, static_cast<f64>(clip.y) + clip.height);
    const f64 width = Max(0.0, right - left);
    const f64 height = Max(0.0, bottom - top);
    if(width > Limit<f32>::s_Max || height > Limit<f32>::s_Max)
        return false;
    ImagePlacement candidate;
    candidate.bounds = bounds;
    candidate.clip = { static_cast<f32>(left), static_cast<f32>(top), static_cast<f32>(width), static_cast<f32>(height) };
    if(candidate.clip.width > 0.0f && candidate.clip.x + candidate.clip.width <= candidate.clip.x)
        candidate.clip.width = 0.0f;
    if(candidate.clip.height > 0.0f && candidate.clip.y + candidate.clip.height <= candidate.clip.y)
        candidate.clip.height = 0.0f;
    if(!ValidRect(candidate.clip))
        return false;
    out = candidate;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

