// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"
#include "image_resources.h"
#include "target_factory.h"
#include "shared_resources_factory.h"

#include <core/common/log.h>
#include <core/graphics/rhi/format.h>

#include <global/binary.h>
#include <global/range_algorithms.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_resources{


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

template<typename TElement>
[[nodiscard]] static b8 EnsureGeometryBuffer(
    Core::Device& device,
    const usize requiredCount,
    const usize initialCapacity,
    const Name& debugName,
    const b8 isVertexBuffer,
    Core::BufferHandle& inOutBuffer,
    usize& inOutCapacity
){
    if(inOutCapacity >= requiredCount)
        return true;
    const usize capacity = ComputeGpuGrowingBufferCapacity(inOutCapacity, requiredCount, initialCapacity, sizeof(TElement));
    Core::BufferDesc description;
    description.setByteSize(static_cast<u64>(capacity * sizeof(TElement))).setIsVertexBuffer(isVertexBuffer).setIsIndexBuffer(!isVertexBuffer)
        .setDebugName(debugName).enableAutomaticStateTracking(Core::ResourceStates::Common)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    auto bufferResult = device.createBuffer(description);
    if(!bufferResult)
        return false;
    inOutBuffer = Move(*bufferResult);
    inOutCapacity = capacity;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuImageResources::ValidateGlyphPages(const DrawSnapshot& snapshot)noexcept{
    if(!ValidSnapshotImageCounts(snapshot.glyphPages().size(), snapshot.sdfPages().size(), snapshot.textureImages().size()))
        return false;
    const auto& pages = snapshot.glyphPages();
    for(usize index = 0u; index < pages.size(); ++index){
        if(!pages[index] || !ValidGlyphPageBinding(pages[index]->binding(), pages[index]->pixels().size()))
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(SameGlyphPageKey(pages[index]->binding(), pages[previous]->binding()))
                return false;
        }
    }
    return ValidateDrawCommandGlyphBindings(snapshot.commands(), pages.size());
}

Expected<GpuVersion<GpuGlyphVersion>> GpuImageResources::prepareGlyphPage(const SharedGlyphPage& page){
    usize replaceIndex = m_imageCache.glyphs().size();
    for(usize index = 0u; index < m_imageCache.glyphs().size(); ++index){
        const auto& existing = m_imageCache.glyphs()[index];
        if(!SameGlyphPageKey(existing->m_page->binding(), page->binding()))
            continue;
        replaceIndex = index;
        if(existing->m_page->binding().generation != page->binding().generation)
            break;
        if(!SameGlyphPageVersion(existing->m_page->binding(), existing->m_page->pixels(), page->binding(), page->pixels(), existing->m_page.get() == page.get()))
            return MakeUnexpected(Failure{});
        GpuVersion<GpuGlyphVersion> reused = existing;
        m_imageCache.glyphs().erase(m_imageCache.glyphs().begin() + static_cast<isize>(index));
        m_imageCache.glyphs().push_back(reused);
        if(!GpuSampledTextures::PrepareDescriptor(reused->m_graphics, reused->m_texture, Core::Format::R8_UNORM, reused->m_sampledImage))
            return MakeUnexpected(Failure{});
        return Move(reused);
    }
    Core::Device& device = m_graphics.getDevice();
    if((device.queryFormatSupport(Core::Format::R8_UNORM) & s_RequiredSampledTextureSupport) != s_RequiredSampledTextureSupport)
        return MakeUnexpected(Failure{});
    auto versionResult = MakeGpuVersion<GpuGlyphVersion>(m_arena, m_graphics, page);
    if(!versionResult)
        return MakeUnexpected(versionResult.error());
    GpuVersion<GpuGlyphVersion> version = Move(*versionResult);
    const GlyphPageBinding& binding = page->binding();
    // Upload only a fresh immutable image. Accepted readiness stays with this version across presentation retries.
    if(!GpuSampledTextures::CreateAndUploadPage(
        m_graphics,
        GpuPageTextureUploadDesc{
            .name = s_GpuGlyphPageTextureName,
            .data = page->pixels().data(),
            .dataSize = page->pixels().size(),
            .rowPitch = binding.width,
            .width = binding.width,
            .height = binding.height,
            .format = Core::Format::R8_UNORM,
        },
        version->m_texture,
        version->m_readinessToken
    ))
        return MakeUnexpected(Failure{});
    // Frames retain superseded versions; evicting a cache entry never overwrites an image or frees a live frame's descriptor.
    if(replaceIndex < m_imageCache.glyphs().size())
        m_imageCache.glyphs().erase(m_imageCache.glyphs().begin() + static_cast<isize>(replaceIndex));
    else if(ShouldEvictGpuImageCacheBeforeInsert(m_imageCache.glyphs().size(), m_imageCache.sdfs().size(), m_imageCache.textures().size()))
        m_imageCache.evict();
    m_imageCache.glyphs().push_back(version);
    // Cache accepted uploads before descriptor publication so descriptor exhaustion retries cannot discard their readiness.
    if(!GpuSampledTextures::PrepareDescriptor(version->m_graphics, version->m_texture, Core::Format::R8_UNORM, version->m_sampledImage))
        return MakeUnexpected(Failure{});
    return Move(version);
}

b8 GpuImageResources::prepareGlyphPages(GpuFrameData& frame){
    const auto& pages = frame.m_snapshot.glyphPages();
    frame.m_glyphPages.reserve(pages.size());
    // Retain successful earlier uploads if preparation fails partway through the page set.
    while(frame.m_glyphPages.size() < pages.size()){
        auto version = prepareGlyphPage(pages[frame.m_glyphPages.size()]);
        if(!version)
            return false;
        frame.m_glyphPages.push_back(Move(*version));
    }
    return true;
}

Expected<GpuSampledGraphResources> GpuImageResources::declareGlyphPages(Core::GpuTaskGraph& graph, const GpuFrame& frame, GpuRasterResourceUses& uses){
    Expected<GpuSampledGraphResources> resources{s_InPlace};
    if(frame->m_glyphPages.size() != frame->m_snapshot.glyphPages().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: prepared glyph page count does not match the immutable snapshot"));
        return MakeUnexpected(Failure{});
    }
    for(usize index = 0u; index < frame->m_glyphPages.size(); ++index){
        const auto& page = frame->m_glyphPages[index];
        const auto texture = GpuSampledTextures::ImportTexture(
            GpuSampledTextureImportDesc{
                .readyPrefix = s_GpuGlyphPageReadyPrefix,
                .readyMarkerLabel = s_GpuGlyphPageReadyMarkerLabel,
                .completionLabel = NWB_TEXT("glyph page"),
                .readinessToken = page->m_readinessToken,
                .texturePrefix = s_GpuGlyphPagePrefix,
                .textureMarkerLabel = s_GpuGlyphPageMarkerLabel,
                .texture = page->m_texture,
                .index = index,
            },
            graph,
            uses
        );
        if(!texture){
            if(texture.error() == GpuSampledTextureImportError::Texture)
                NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: glyph page {} texture import failed (atlas={}, page={}, generation={})"), index, page->m_page->binding().atlasIdentity, page->m_page->binding().index, page->m_page->binding().generation);
            return MakeUnexpected(Failure{});
        }
        resources->push_back(*texture);
    }
    return resources;
}

b8 GpuImageResources::ValidateSdfPages(const DrawSnapshot& snapshot)noexcept{
    if(!ValidSnapshotImageCounts(snapshot.glyphPages().size(), snapshot.sdfPages().size(), snapshot.textureImages().size()))
        return false;
    const auto& pages = snapshot.sdfPages();
    if(!AllValidAndDistinct(Span<const SharedSdfAtlasPage>(pages.data(), pages.size()), [](const SharedSdfAtlasPage& current)noexcept{ return current && ValidSdfAtlasPageBinding(current->binding(), current->pixels().size()); }, [](const SharedSdfAtlasPage& current, const SharedSdfAtlasPage& previous)noexcept{ return SameSdfAtlasPageKey(current->binding(), previous->binding()); }))
        return false;
    for(const DrawCommand& command : snapshot.commands()){
        const u32 channelCount = command.sdfPageIndex < pages.size() ? pages[command.sdfPageIndex]->binding().channelCount : 0u;
        if(!ValidDrawCommandSdfBinding(command, pages.size(), channelCount))
            return false;
    }
    return true;
}

Expected<GpuVersion<GpuSdfAtlasVersion>> GpuImageResources::prepareSdfPage(Core::Alloc::ScratchArena& scratchArena, const SharedSdfAtlasPage& page){
    for(usize index = 0u; index < m_imageCache.sdfs().size(); ++index){
        const auto& existing = m_imageCache.sdfs()[index];
        if(!SameSdfAtlasPageKey(existing->m_page->binding(), page->binding()))
            continue;
        if(!(existing->m_page->binding() == page->binding()))
            return MakeUnexpected(Failure{});
        GpuVersion<GpuSdfAtlasVersion> reused = existing;
        m_imageCache.sdfs().erase(m_imageCache.sdfs().begin() + static_cast<isize>(index));
        m_imageCache.sdfs().push_back(reused);
        if(!GpuSampledTextures::PrepareDescriptor(reused->m_graphics, reused->m_texture, reused->m_texture->getDescription().format, reused->m_sampledImage))
            return MakeUnexpected(Failure{});
        return Move(reused);
    }
    Core::Device& device = m_graphics.getDevice();
    const SdfAtlasPageBinding& binding = page->binding();
    constexpr Array<Core::Format::Enum, 4> s_Formats{ Core::Format::R8_UNORM, Core::Format::RG8_UNORM, Core::Format::RGB8_UNORM, Core::Format::RGBA8_UNORM };
    Core::Format::Enum format = s_Formats[binding.channelCount - 1u];
    u32 uploadChannels = binding.channelCount;
    if(binding.channelCount == s_GpuRgbChannelCount && (device.queryFormatSupport(format) & s_RequiredSampledTextureSupport) != s_RequiredSampledTextureSupport){
        format = Core::Format::RGBA8_UNORM;
        uploadChannels = s_GpuRgbaChannelCount;
    }
    if((device.queryFormatSupport(format) & s_RequiredSampledTextureSupport) != s_RequiredSampledTextureSupport)
        return MakeUnexpected(Failure{});
    auto versionResult = MakeGpuVersion<GpuSdfAtlasVersion>(m_arena, m_graphics, page);
    if(!versionResult)
        return MakeUnexpected(versionResult.error());
    GpuVersion<GpuSdfAtlasVersion> version = Move(*versionResult);
    Vector<u8, Core::Alloc::ScratchArena> expanded(scratchArena);
    NotNull<const u8*> uploadData(page->pixels().data());
    usize uploadBytes = page->pixels().size();
    if(uploadChannels != binding.channelCount){
        ExpandGpuSdfPagePixels(binding.width, binding.height, binding.channelCount, uploadChannels, page->pixels(), expanded);
        uploadData = expanded.data();
        uploadBytes = expanded.size();
    }
    if(!GpuSampledTextures::CreateAndUploadPage(
        m_graphics,
        GpuPageTextureUploadDesc{
            .name = s_GpuSdfPageTextureName,
            .data = uploadData.get(),
            .dataSize = uploadBytes,
            .rowPitch = static_cast<usize>(binding.width) * uploadChannels,
            .width = binding.width,
            .height = binding.height,
            .format = format,
        },
        version->m_texture,
        version->m_readinessToken
    ))
        return MakeUnexpected(Failure{});
    // Native accepted upload commands retain their destination image until the physical queue completes.
    // Frame owners retain sampled descriptors; cache eviction therefore releases neither a live image nor a live draw view.
    if(ShouldEvictGpuImageCacheBeforeInsert(m_imageCache.glyphs().size(), m_imageCache.sdfs().size(), m_imageCache.textures().size()))
        m_imageCache.evict();
    m_imageCache.sdfs().push_back(version);
    // Preserve every accepted upload before descriptor publication; retrying a failed descriptor never uploads again.
    if(!GpuSampledTextures::PrepareDescriptor(version->m_graphics, version->m_texture, version->m_texture->getDescription().format, version->m_sampledImage))
        return MakeUnexpected(Failure{});
    return Move(version);
}

b8 GpuImageResources::prepareSdfPages(GpuFrameData& frame){
    const auto& pages = frame.m_snapshot.sdfPages();
    frame.m_sdfPages.reserve(pages.size());
    Core::Alloc::ScratchArena scratchArena(s_GpuSdfUploadArenaName);
    while(frame.m_sdfPages.size() < pages.size()){
        auto version = prepareSdfPage(scratchArena, pages[frame.m_sdfPages.size()]);
        if(!version)
            return false;
        frame.m_sdfPages.push_back(Move(*version));
    }
    return true;
}

Expected<GpuSampledGraphResources> GpuImageResources::declareSdfPages(Core::GpuTaskGraph& graph, const GpuFrame& frame, GpuRasterResourceUses& uses){
    Expected<GpuSampledGraphResources> resources{s_InPlace};
    if(frame->m_sdfPages.size() != frame->m_snapshot.sdfPages().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: prepared SDF page count does not match the immutable snapshot"));
        return MakeUnexpected(Failure{});
    }
    for(usize index = 0u; index < frame->m_sdfPages.size(); ++index){
        const auto& page = frame->m_sdfPages[index];
        const auto texture = GpuSampledTextures::ImportTexture(
            GpuSampledTextureImportDesc{
                .readyPrefix = s_GpuSdfPageReadyPrefix,
                .readyMarkerLabel = s_GpuSdfPageReadyMarkerLabel,
                .completionLabel = NWB_TEXT("SDF page"),
                .readinessToken = page->m_readinessToken,
                .texturePrefix = s_GpuSdfPagePrefix,
                .textureMarkerLabel = s_GpuSdfPageMarkerLabel,
                .texture = page->m_texture,
                .index = index,
            },
            graph,
            uses
        );
        if(!texture){
            if(texture.error() == GpuSampledTextureImportError::Texture)
                NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: SDF page {} texture import failed (atlas={}, group={}, generation={})"), index, page->m_page->binding().atlasIdentity, page->m_page->binding().index, page->m_page->binding().generation);
            return MakeUnexpected(Failure{});
        }
        resources->push_back(*texture);
    }
    return resources;
}

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
        if(!SameTextureImageKey(*existing->m_source, *source) || !__hidden_ui_gpu_resources::ValidVersion(*existing))
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
    if(!TextureAssetLoader::Create(source->texture(), source->identity().name(), __hidden_ui_gpu_resources::s_ImageLoadContext, m_graphics, version->m_texture))
        return MakeUnexpected(Failure{});
    if(!__hidden_ui_gpu_resources::ValidVersion(*version)){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: texture image {} has invalid GPU format, geometry, or readiness"), source->generation());
        return MakeUnexpected(Failure{});
    }
    if(ShouldEvictGpuImageCacheBeforeInsert(m_imageCache.glyphs().size(), m_imageCache.sdfs().size(), m_imageCache.textures().size()))
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
        if(!image || !source || !image->m_source || !SameTextureImageKey(*image->m_source, *source) || !__hidden_ui_gpu_resources::ValidVersion(*image))
            return MakeUnexpected(Failure{});
        const auto texture = GpuSampledTextures::ImportTexture(
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


void FreeGpuDescriptor(Core::GraphicsRuntime& graphics, const Core::GpuDescriptorHandle& descriptor)noexcept{
    if(descriptor.valid())
        graphics.getDevice().getDescriptorHeap().free(descriptor);
}

b8 GpuTokenComplete(Core::Device& device, const Core::QueueSubmissionToken& token){
    if(!token.valid())
        return true;
    if(!token.hasPhysicalQueueIdentity())
        return false;
    const Core::GpuPhysicalQueueId queue{ token.physicalQueueIndex, token.deviceGeneration };
    const auto timeline = device.getQueueTimelineSnapshot(queue);
    return timeline && timeline->valid() && timeline->queue == queue && timeline->completedValue >= token.value;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuRendererState::MatchesAcquired(const Core::AcquiredPresentationFrame& first, const Core::AcquiredPresentationFrame& second)noexcept{
    return
        first.valid() && second.valid()
        && first.backBuffer.texture == second.backBuffer.texture && first.framebuffer == second.framebuffer
        && first.backBuffer.index == second.backBuffer.index && first.backBuffer.nativeInitialState == second.backBuffer.nativeInitialState
        && first.backBuffer.availabilityCompletion == second.backBuffer.availabilityCompletion
    ;
}

b8 GpuRendererState::createResources(){
    return GpuSharedResourcesFactory{}.create(*this);
}

Expected<GpuVersion<GpuTargetVersion>> GpuRendererState::createTarget(const u32 width, const u32 height){
    return GpuTargetFactory{}.create(m_arena, m_graphics, width, height);
}

b8 GpuRendererState::prepareBuffers(GpuFrameSlot& slot, const DrawSnapshot& snapshot){
    const usize vertexCount = snapshot.vertices().size();
    const usize indexCount = snapshot.indices().size();
    if(vertexCount == 0u && indexCount == 0u)
        return true;
    if(!IsValidGpuSnapshotBufferCounts(vertexCount, indexCount, sizeof(Vertex), sizeof(u32)))
        return false;
    Core::Device& device = m_graphics.getDevice();
    return __hidden_ui_gpu_resources::EnsureGeometryBuffer<Vertex>(device, vertexCount, s_GpuInitialVertexCapacity, s_GpuVertexBufferName, true, slot.vertices, slot.vertexCapacity)
        && __hidden_ui_gpu_resources::EnsureGeometryBuffer<u32>(device, indexCount, s_GpuInitialIndexCapacity, s_GpuIndexBufferName, false, slot.indices, slot.indexCapacity);
}

b8 GpuRendererState::prepareOutputPipeline(const Core::FramebufferInfo& info){
    return GpuSharedResourcesFactory{}.prepareOutputPipeline(*this, info);
}

b8 GpuRendererState::prepare(const Core::AcquiredPresentationFrame& acquired){
    m_readyToDeclare = false;
    if(!m_pending)
        return true;
    if(!acquired.valid() || !m_resources || m_width == 0u || m_height == 0u)
        return false;
    const Core::FramebufferDesc& attachments = acquired.framebuffer->getDescription();
    const Core::TextureDesc& backBuffer = acquired.backBuffer.texture->getDescription();
    if(!IsValidGpuPresentationAttachments(
        attachments.colorAttachments.size(),
        attachments.colorAttachments.size() == 1u && attachments.colorAttachments[0].texture == acquired.backBuffer.texture.get(),
        attachments.depthAttachment.valid(),
        attachments.shadingRateAttachment.valid(),
        backBuffer.width,
        m_width,
        backBuffer.height,
        m_height,
        backBuffer.sampleCount
    ))
        return false;
    if(!m_resources->m_outputPipeline || m_resources->m_outputPipeline->getFramebufferInfo() != acquired.framebuffer->getFramebufferInfo())
        return false;
    if(m_pending->m_prepared && MatchesAcquired(m_pending->m_acquired, acquired)){
        m_readyToDeclare = true;
        return true;
    }
    Core::Device& device = m_graphics.getDevice();
    if(m_pending->m_prepared){
        // A failed graph may have accepted uploads or raster. Rebuild only after that exact prefix completes.
        if(!m_pending->prefixComplete(device))
            return true;
        m_pending->m_acquired = acquired;
        m_readyToDeclare = true;
        m_claimed = false;
        m_declaredGraph = nullptr;
        return true;
    }
    // Retire all completed owners before cache eviction or descriptor allocation can need their heap slots.
    for(auto& slot : m_slots){
        if(slot.inFlight && slot.inFlight->complete(device))
            slot.inFlight.reset();
    }
    m_imageCache.trim(m_pending->m_snapshot);
    for(auto& slot : m_slots){
        if(slot.inFlight)
            continue;
        GpuImageResources imageResources(m_arena, m_graphics, m_imageCache);
        if(!imageResources.prepareGlyphPages(*m_pending) || !imageResources.prepareSdfPages(*m_pending) || !imageResources.prepareTextureImages(*m_pending) || !prepareBuffers(slot, m_pending->m_snapshot))
            return false;
        m_pending->m_target = slot.target;
        m_pending->m_resources = m_resources;
        m_pending->m_vertices = slot.vertices;
        m_pending->m_indices = slot.indices;
        m_pending->m_outputPipeline = m_resources->m_outputPipeline;
        m_pending->m_acquired = acquired;
        m_pending->m_presentationMode = m_graphics.isHDR10OutputActive() ? NWB_UI_PRESENTATION_HDR10 : NWB_UI_PRESENTATION_SDR;
        m_pending->m_prepared = true;
        m_readyToDeclare = true;
        slot.inFlight = m_pending;
        m_claimed = false;
        m_declaredGraph = nullptr;
        return true;
    }
    // Admission is bounded. The pending immutable CPU generation survives until a slot becomes available.
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
