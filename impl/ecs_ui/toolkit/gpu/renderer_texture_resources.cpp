// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_resources.h"

#include <core/common/log.h>
#include <core/graphics/rhi/format.h>

#include <global/range_algorithms.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_texture_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_ImageLoadContext = NWB_TEXT("UI Texture Image");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 ValidVersion(const GpuTextureImageVersion& version)noexcept{
    if(!version.m_source || !ValidUiTextureImageSource(*version.m_source) || !version.m_texture.valid())
        return false;
    const TextureGpuResource& resource = version.m_texture;
    if(resource.format == Core::Format::UNKNOWN || resource.format >= Core::Format::Count)
        return false;
    const Core::FormatInfo& format = Core::GetFormatInfo(resource.format);
    const Core::TextureDesc& description = resource.texture->getCreationDescription();
    const Texture& texture = version.m_source->texture();
    return IsValidGpuTextureImageResourceDesc(
        format.kind == Core::FormatKind::Normalized || format.kind == Core::FormatKind::Float,
        format.hasDepth,
        format.hasStencil,
        format.hasRed || format.hasGreen || format.hasBlue || format.hasAlpha,
        resource.sampledImageHeapHandle.descriptorClass() == Core::GpuDescriptorClass::SampledImage,
        resource.readinessToken.valid(),
        resource.readinessToken.hasPhysicalQueueIdentity(),
        description.dimension == Core::TextureDimension::Texture2D,
        description.depth,
        description.arraySize,
        description.sampleCount,
        description.format == resource.format,
        description.width,
        texture.width(),
        description.height,
        texture.height(),
        description.mipLevels,
        texture.mipLevels().size()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuImageResources::ValidateTextureImages(const DrawSnapshot& snapshot)noexcept{
    if(!ValidSnapshotImageCounts(snapshot.glyphPages().size(), snapshot.sdfPages().size(), snapshot.textureImages().size()))
        return false;
    const auto& images = snapshot.textureImages();
    for(usize index = 0u; index < images.size(); ++index){
        if(!images[index] || !ValidUiTextureImageSource(*images[index]))
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(images[index]->generation() == images[previous]->generation())
                return false;
        }
    }
    return ValidateDrawCommandImageBindings(snapshot.commands(), images.size());
}

Expected<GpuVersion<GpuTextureImageVersion>> GpuImageResources::prepareTextureImage(const SharedImageSource& source){
    if(!source || !ValidUiTextureImageSource(*source))
        return MakeUnexpected(Failure{});
    for(usize index = 0u; index < m_imageCache.textures().size(); ++index){
        const auto& existing = m_imageCache.textures()[index];
        if(existing->m_source->generation() != source->generation())
            continue;
        if(!SameTextureImageKey(*existing->m_source, *source) || !__hidden_ui_gpu_texture_resources::ValidVersion(*existing))
            return MakeUnexpected(Failure{});
        GpuVersion<GpuTextureImageVersion> reused = existing;
        m_imageCache.textures().erase(m_imageCache.textures().begin() + static_cast<isize>(index));
        m_imageCache.textures().push_back(reused);
        return Move(reused);
    }
    // The source factory owns structural payload validation; immutable cache insertion keeps a debug-only invariant.
    NWB_ASSERT(source->texture().validatePayload());
    auto versionResult = MakeGpuVersion<GpuTextureImageVersion>(m_arena, m_graphics, source);
    if(!versionResult)
        return MakeUnexpected(versionResult.error());
    GpuVersion<GpuTextureImageVersion> version = Move(*versionResult);
    if(!TextureAssetLoader::Create(source->texture(), source->identity().name(), __hidden_ui_gpu_texture_resources::s_ImageLoadContext, m_graphics, version->m_texture))
        return MakeUnexpected(Failure{});
    if(!__hidden_ui_gpu_texture_resources::ValidVersion(*version)){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: texture image {} has invalid GPU format, geometry, or readiness"), source->generation());
        return MakeUnexpected(Failure{});
    }
    if(m_imageCache.glyphs().size() + m_imageCache.sdfs().size() + m_imageCache.textures().size() == s_PaintMaxImages)
        m_imageCache.evict();
    // Retain each successful immutable upload before a later image or frame preparation can fail.
    m_imageCache.textures().push_back(version);
    return Move(version);
}

b8 GpuImageResources::prepareTextureImages(GpuFrameData& frame){
    const auto& images = frame.m_snapshot.textureImages();
    frame.m_textureImages.reserve(images.size());
    while(frame.m_textureImages.size() < images.size()){
        auto version = prepareTextureImage(images[frame.m_textureImages.size()]);
        if(!version)
            return false;
        frame.m_textureImages.push_back(Move(*version));
    }
    return true;
}

Expected<GpuSampledGraphResources> GpuImageResources::declareTextureImages(Core::GpuTaskGraph& graph, const GpuFrame& frame, GpuRasterResourceUses& uses){
    Expected<GpuSampledGraphResources> resources{s_InPlace};
    if(frame->m_textureImages.size() != frame->m_snapshot.textureImages().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: prepared texture image count does not match the immutable snapshot"));
        return MakeUnexpected(Failure{});
    }
    for(usize index = 0u; index < frame->m_textureImages.size(); ++index){
        const auto& image = frame->m_textureImages[index];
        const auto& source = frame->m_snapshot.textureImages()[index];
        if(!image || !source || !image->m_source || !SameTextureImageKey(*image->m_source, *source) || !__hidden_ui_gpu_texture_resources::ValidVersion(*image))
            return MakeUnexpected(Failure{});
        const auto texture = GpuSampledTextures::importTexture(
            GpuSampledTextureImportDesc{
                .readyPrefix = s_GpuTextureImageReadyPrefix,
                .readyMarkerLabel = s_GpuTextureImageReadyMarkerLabel,
                .completionLabel = NWB_TEXT("texture"),
                .readinessToken = image->m_texture.readinessToken,
                .texturePrefix = s_GpuTextureImagePrefix,
                .textureMarkerLabel = s_GpuTextureImageMarkerLabel,
                .texture = image->m_texture.texture,
                .index = index,
            },
            graph,
            uses
        );
        if(!texture){
            if(texture.error() == GpuSampledTextureImportError::Texture)
                NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: texture image {} import failed (source generation={})"), index, source->generation());
            return MakeUnexpected(Failure{});
        }
        resources->push_back(*texture);
    }
    return resources;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

