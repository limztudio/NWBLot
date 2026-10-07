// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_texture_resources.h"
#include "renderer_internal.h"

#include <core/common/log.h>
#include <core/graphics/rhi/format.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_texture_images{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidSource(const SharedImageSource& source){
    if(!source || source->generation() == 0u || !source->identity().valid())
        return false;
    const Texture& texture = source->texture();
    return
        texture.virtualPath() == source->identity().name()
        && texture.dimension() == TextureDimension::Texture2D && texture.depth() == 1u
        && texture.width() != 0u && texture.height() != 0u
        && !texture.mipLevels().empty() && !texture.payloadBytes().empty()
        && IsValidTextureColorSpace(texture.colorSpace()) && IsValidTexturePayloadFormat(texture.payloadFormat())
        && IsValidTextureAlphaMode(texture.alphaMode())
        && (!IsHdrTexturePayloadFormat(texture.payloadFormat()) || texture.colorSpace() == TextureColorSpace::Linear)
    ;
}

[[nodiscard]] static bool ValidVersion(const GpuTextureImageVersion& version){
    if(!ValidSource(version.m_source) || !version.m_texture.valid())
        return false;
    const TextureGpuResource& resource = version.m_texture;
    if(resource.format == Core::Format::UNKNOWN || resource.format >= Core::Format::kCount)
        return false;
    const Core::FormatInfo& format = Core::GetFormatInfo(resource.format);
    if(
        (format.kind != Core::FormatKind::Normalized && format.kind != Core::FormatKind::Float)
        || format.hasDepth || format.hasStencil || !(format.hasRed || format.hasGreen || format.hasBlue || format.hasAlpha)
        || format.isSRGB != (version.m_source->texture().colorSpace() == TextureColorSpace::Srgb)
        || resource.sampledImageHeapHandle.descriptorClass() != Core::GpuDescriptorClass::SampledImage
        || !resource.readinessToken.valid() || !resource.readinessToken.hasPhysicalQueueIdentity()
    )
        return false;
    const Core::TextureDesc& description = resource.texture->getCreationDescription();
    const Texture& texture = version.m_source->texture();
    return
        description.dimension == Core::TextureDimension::Texture2D && description.depth == 1u
        && description.arraySize == 1u && description.sampleCount == 1u && description.format == resource.format
        && description.width == texture.width() && description.height == texture.height()
        && description.mipLevels == texture.mipLevels().size()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTextureImageVersion::GpuTextureImageVersion(Core::GraphicsRuntime& graphics, SharedImageSource source)
    : m_graphics(graphics)
    , m_source(Move(source))
{}

GpuTextureImageVersion::~GpuTextureImageVersion()noexcept{
    TextureAssetLoader::Release(m_texture, m_graphics);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuRendererState::ValidateTextureImages(const DrawSnapshot& snapshot){
    const auto& images = snapshot.textureImages();
    if(
        images.size() > s_PaintMaxImages || snapshot.glyphPages().size() > s_PaintMaxImages
        || snapshot.sdfPages().size() > s_PaintMaxImages
        || images.size() + snapshot.glyphPages().size() + snapshot.sdfPages().size() > s_PaintMaxImages
    )
        return false;
    for(usize index = 0u; index < images.size(); ++index){
        if(!__hidden_ui_gpu_texture_images::ValidSource(images[index]))
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(images[index]->generation() == images[previous]->generation())
                return false;
        }
    }
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material == PaintMaterial::Image){
            if(
                command.textureImageIndex >= images.size() || command.glyphPageIndex != Limit<u32>::s_Max
                || command.sdfPageIndex != Limit<u32>::s_Max || command.sdfChannel != 0u
            )
                return false;
        }
        else if(command.textureImageIndex != Limit<u32>::s_Max)
            return false;
    }
    return true;
}

GpuVersion<GpuTextureImageVersion> GpuRendererState::prepareTextureImage(const SharedImageSource& source){
    if(!__hidden_ui_gpu_texture_images::ValidSource(source))
        return {};
    for(usize index = 0u; index < m_textureCache.size(); ++index){
        const auto& existing = m_textureCache[index];
        if(existing->m_source->generation() != source->generation())
            continue;
        if(existing->m_source->identity() != source->identity() || !__hidden_ui_gpu_texture_images::ValidVersion(*existing))
            return {};
        GpuVersion<GpuTextureImageVersion> reused = existing;
        m_textureCache.erase(m_textureCache.begin() + static_cast<isize>(index));
        m_textureCache.push_back(reused);
        return reused;
    }
    // The source factory owns structural payload validation; immutable cache insertion keeps a debug-only invariant.
    NWB_ASSERT(source->texture().validatePayload());
    auto version = MakeGpuVersion<GpuTextureImageVersion>(m_arena, m_graphics, source);
    if(!version)
        return {};
    if(!TextureAssetLoader::Create(
        version->m_texture, source->texture(), source->identity().name(), m_graphics, NWB_TEXT("UI Texture Image")
    ))
        return {};
    if(!__hidden_ui_gpu_texture_images::ValidVersion(*version)){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: texture image {} has invalid GPU format, geometry, or readiness")
            , source->generation()
        );
        return {};
    }
    if(m_glyphCache.size() + m_sdfCache.size() + m_textureCache.size() == s_PaintMaxImages)
        evictImageCacheEntry();
    // Retain each successful immutable upload before a later image or frame preparation can fail.
    m_textureCache.push_back(version);
    return version;
}

bool GpuRendererState::prepareTextureImages(GpuFrameData& frame){
    const auto& images = frame.m_snapshot.textureImages();
    frame.m_textureImages.reserve(images.size());
    while(frame.m_textureImages.size() < images.size()){
        auto version = prepareTextureImage(images[frame.m_textureImages.size()]);
        if(!version)
            return false;
        frame.m_textureImages.push_back(Move(version));
    }
    return true;
}

bool GpuRendererState::declareTextureImages(
    Core::GpuTaskGraph& graph,
    const GpuFrame& frame,
    GpuTextureGraphResources& resources,
    GpuRasterResourceUses& uses){
    if(frame->m_textureImages.size() != frame->m_snapshot.textureImages().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: prepared texture image count does not match the immutable snapshot"));
        return false;
    }
    for(usize index = 0u; index < frame->m_textureImages.size(); ++index){
        const auto& image = frame->m_textureImages[index];
        const auto& source = frame->m_snapshot.textureImages()[index];
        if(
            !image || !source || !image->m_source || image->m_source->generation() != source->generation()
            || image->m_source->identity() != source->identity() || !__hidden_ui_gpu_texture_images::ValidVersion(*image)
        )
            return false;
        char identityText[32u] = {};
        const AStringView suffix = FormatDecimal(index, identityText);
        const Name readyIdentity = DeriveName(Name("ui.texture_image_ready/"), suffix);
        const Name textureIdentity = DeriveName(Name("ui.texture_image/"), suffix);
        const Core::QueueSubmissionToken& readiness = image->m_texture.readinessToken;
        const Core::GpuExternalCompletionId ready = graph.importExternalCompletion(
            Core::GpuExternalCompletionDesc().setIdentity(readyIdentity).setMarkerLabel("UI Texture Image Upload Completion")
                .setToken(readiness)
        );
        if(!ready.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: texture {} completion import failed (queue={}, generation={}, value={})")
                , index, readiness.physicalQueueIndex, readiness.deviceGeneration, readiness.value
            );
            return false;
        }
        const Core::GpuGraphResourceId texture = graph.importTexture(
            image->m_texture.texture,
            Core::GpuGraphResourceDesc().setIdentity(textureIdentity).setMarkerLabel("UI Texture Image")
                .setType(Core::GpuGraphResourceType::Texture).setInitialAvailabilityCompletion(ready)
                .setExternalFinalState(Core::ResourceStates::ShaderResource)
        );
        if(!texture.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: texture image {} import failed (source generation={})")
                , index, source->generation()
            );
            return false;
        }
        resources.push_back(texture);
        uses.push_back({ texture, {}, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read });
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

