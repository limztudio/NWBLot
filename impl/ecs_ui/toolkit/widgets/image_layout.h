// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/paint.h>
#include <impl/ecs_ui/toolkit/widgets/image_style.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ImageFrame = ImageFrameRecord<SharedImageSource>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ImageLayout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Natural atlas extent uses reference density and logical minima; region padding does not inset images.
[[nodiscard]] inline Expected<ImageMetrics> Measure(const ImageOptions& options, const UiSkinRegion& region, const f32 density)noexcept{
    ImageMetrics candidate;
    if(!TryMeasureRegionImage(options, region, density, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}

// Engine images use one logical unit per texel without atlas density, slices or control padding.
[[nodiscard]] inline Expected<ImageMetrics> Measure(const ImageOptions& options, const ImageSource& source)noexcept{
    if(!IsValidLayoutSize(options.width) || !IsValidLayoutSize(options.height) || !IsValidUiColor(options.tint))
        return MakeUnexpected(Failure{});
    const Texture& texture = source.texture();
    // The immutable source factory admitted its complete static 2D payload before this per-frame measurement.
    return ImageMetrics{ { static_cast<f32>(texture.width()), static_cast<f32>(texture.height()) } };
}

// Empty visibility remains a valid placement.
[[nodiscard]] inline Expected<ImagePlacement> Place(const Rect& bounds, const Rect& clip)noexcept{
    ImagePlacement candidate;
    if(!TryPlaceImage(bounds, clip, candidate))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

