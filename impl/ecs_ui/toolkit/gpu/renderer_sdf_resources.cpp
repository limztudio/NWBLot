// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_sdf_resources.h"
#include "renderer_internal.h"

#include <core/common/log.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/alloc/scratch.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_sdf_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidPage(const SharedSdfAtlasPage& page)noexcept{
    if(!page)
        return false;
    const SdfAtlasPageBinding& binding = page->binding();
    return
        binding.font.valid() && binding.fontGeneration != 0u && binding.atlasIdentity != 0u && binding.generation != 0u
        && binding.index < s_FontAtlasMaxGroupCount && binding.width != 0u && binding.height != 0u
        && binding.width <= s_FontAtlasMaxExtent && binding.height <= s_FontAtlasMaxExtent
        && binding.channelCount != 0u && binding.channelCount <= s_SdfMaxChannelCount
        && binding.spreadPixels >= s_FontAtlasMinSpreadPixels && binding.spreadPixels <= s_FontAtlasMaxSpreadPixels
        && binding.distanceEncoding == s_SdfDistanceEncodingFreeTypeU8
        && page->pixels().size() == static_cast<usize>(binding.width) * binding.height * binding.channelCount
    ;
}

[[nodiscard]] static bool PrepareDescriptor(GpuSdfAtlasVersion& version){
    if(version.m_sampledImage.valid())
        return true;
    Core::GpuDescriptorHeap& heap = version.m_graphics.getDevice().getDescriptorHeap();
    const Expected<Core::GpuDescriptorHandle> descriptorResult = heap.allocate(Core::GpuDescriptorClass::SampledImage);
    if(!descriptorResult)
        return false;
    const Core::GpuDescriptorHandle descriptor = *descriptorResult;
    Core::GpuDescriptorRetirementGuard descriptorGuard(heap, descriptor);
    if(!heap.write(descriptor, Core::DescriptorWriteItem::TextureSrv(0u, version.m_texture.get(), version.m_texture->getDescription().format, Core::s_AllSubresources, Core::TextureDimension::Texture2D)))
        return false;
    version.m_sampledImage = descriptor;
    descriptorGuard.release();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuSdfAtlasVersion::GpuSdfAtlasVersion(Core::GraphicsRuntime& graphics, SharedSdfAtlasPage page)noexcept
    : m_graphics(graphics)
    , m_page(Move(page))
{}

GpuSdfAtlasVersion::~GpuSdfAtlasVersion()noexcept{
    if(m_sampledImage.valid())
        m_graphics.getDevice().getDescriptorHeap().free(m_sampledImage);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuRendererState::ValidateSdfPages(const DrawSnapshot& snapshot)noexcept{
    const auto& pages = snapshot.sdfPages();
    if(pages.size() + snapshot.glyphPages().size() + snapshot.textureImages().size() > s_PaintMaxImages)
        return false;
    for(usize index = 0u; index < pages.size(); ++index){
        if(!__hidden_ui_gpu_sdf_resources::ValidPage(pages[index]))
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(SameSdfAtlasPageKey(pages[index]->binding(), pages[previous]->binding()))
                return false;
        }
    }
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material == PaintMaterial::SdfGlyph && (command.sdfPageIndex >= pages.size() || command.sdfChannel >= pages[command.sdfPageIndex]->binding().channelCount))
            return false;
    }
    return true;
}

Expected<GpuVersion<GpuSdfAtlasVersion>> GpuRendererState::prepareSdfPage(Core::Alloc::ScratchArena& scratchArena, const SharedSdfAtlasPage& page){
    for(usize index = 0u; index < m_sdfCache.size(); ++index){
        const auto& existing = m_sdfCache[index];
        if(!SameSdfAtlasPageKey(existing->m_page->binding(), page->binding()))
            continue;
        if(!(existing->m_page->binding() == page->binding()))
            return MakeUnexpected(Failure{});
        GpuVersion<GpuSdfAtlasVersion> reused = existing;
        m_sdfCache.erase(m_sdfCache.begin() + static_cast<isize>(index));
        m_sdfCache.push_back(reused);
        if(!__hidden_ui_gpu_sdf_resources::PrepareDescriptor(*reused))
            return MakeUnexpected(Failure{});
        return Move(reused);
    }
    Core::Device& device = m_graphics.getDevice();
    constexpr Core::FormatSupport::Mask s_RequiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderSample;
    const SdfAtlasPageBinding& binding = page->binding();
    constexpr Core::Format::Enum s_Formats[]{ Core::Format::R8_UNORM, Core::Format::RG8_UNORM, Core::Format::RGB8_UNORM, Core::Format::RGBA8_UNORM };
    Core::Format::Enum format = s_Formats[binding.channelCount - 1u];
    constexpr u32 s_RgbChannelCount = 3u;
    constexpr u32 s_RgbaChannelCount = 4u;
    u32 uploadChannels = binding.channelCount;
    if(binding.channelCount == s_RgbChannelCount && (device.queryFormatSupport(format) & s_RequiredSupport) != s_RequiredSupport){
        format = Core::Format::RGBA8_UNORM;
        uploadChannels = s_RgbaChannelCount;
    }
    if((device.queryFormatSupport(format) & s_RequiredSupport) != s_RequiredSupport)
        return MakeUnexpected(Failure{});
    auto versionResult = MakeGpuVersion<GpuSdfAtlasVersion>(m_arena, m_graphics, page);
    if(!versionResult)
        return MakeUnexpected(versionResult.error());
    GpuVersion<GpuSdfAtlasVersion> version = Move(*versionResult);
    Core::TextureDesc description;
    description
        .setName(Name("ui.sdf_page")).setWidth(binding.width).setHeight(binding.height)
        .setFormat(format).setInitialState(Core::ResourceStates::ShaderResource).setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    auto textureResult = device.createTexture(description);
    if(textureResult)
        version->m_texture = Move(*textureResult);
    else
        version->m_texture.reset();
    if(!textureResult)
        return MakeUnexpected(Failure{});
    Vector<u8, Core::Alloc::ScratchArena> expanded(scratchArena);
    NotNull<const u8*> uploadData(page->pixels().data());
    usize uploadBytes = page->pixels().size();
    if(uploadChannels != binding.channelCount){
        const usize texels = static_cast<usize>(binding.width) * binding.height;
        expanded.resize(texels * uploadChannels, 0u);
        for(usize texel = 0u; texel < texels; ++texel)
            NWB_MEMCPY(expanded.data() + texel * uploadChannels, uploadChannels, uploadData.get() + texel * binding.channelCount, binding.channelCount);
        uploadData = expanded.data();
        uploadBytes = expanded.size();
    }
    const Core::GraphicsRuntime::TextureUploadRegion region{ .data = uploadData.get(), .dataSize = uploadBytes, .rowPitch = static_cast<usize>(binding.width) * uploadChannels, .depthPitch = uploadBytes, };
    if(!m_graphics.uploadTextureBatch(Core::GraphicsRuntime::TextureUploadBatchDesc{
        .destination = version->m_texture,
        .regions = &region,
        .regionCount = 1u,
        .acceptedToken = &version->m_readinessToken,
        .finalState = Core::ResourceStates::ShaderResource,
        .physicalInitialState = Core::ResourceStates::Unknown,
        .hasPhysicalInitialState = true,
    }
    ))
        return MakeUnexpected(Failure{});
    if(!version->m_readinessToken.valid() || !version->m_readinessToken.hasPhysicalQueueIdentity())
        return MakeUnexpected(Failure{});
    // Native accepted upload commands retain their destination image until the physical queue completes.
    // Frame owners retain sampled descriptors; cache eviction therefore releases neither a live image nor a live draw view.
    if(m_sdfCache.size() + m_glyphCache.size() + m_textureCache.size() == s_PaintMaxImages)
        evictImageCacheEntry();
    m_sdfCache.push_back(version);
    // Preserve every accepted upload before descriptor publication; retrying a failed descriptor never uploads again.
    if(!__hidden_ui_gpu_sdf_resources::PrepareDescriptor(*version))
        return MakeUnexpected(Failure{});
    return Move(version);
}

bool GpuRendererState::prepareSdfPages(GpuFrameData& frame){
    const auto& pages = frame.m_snapshot.sdfPages();
    frame.m_sdfPages.reserve(pages.size());
    Core::Alloc::ScratchArena scratchArena(Name("impl/ecs_ui/gpu/sdf_upload"));
    while(frame.m_sdfPages.size() < pages.size()){
        auto version = prepareSdfPage(scratchArena, pages[frame.m_sdfPages.size()]);
        if(!version)
            return false;
        frame.m_sdfPages.push_back(Move(*version));
    }
    return true;
}

Expected<GpuSdfGraphResources> GpuRendererState::declareSdfPages(Core::GpuTaskGraph& graph, const GpuFrame& frame, GpuRasterResourceUses& uses){
    Expected<GpuSdfGraphResources> resources{s_InPlace};
    if(frame->m_sdfPages.size() != frame->m_snapshot.sdfPages().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: prepared SDF page count does not match the immutable snapshot"));
        return MakeUnexpected(Failure{});
    }
    for(usize index = 0u; index < frame->m_sdfPages.size(); ++index){
        const auto& page = frame->m_sdfPages[index];
        char identityText[TextDetail::s_DecimalTextBufferBytes];
        const AStringView suffix = FormatDecimal(index, identityText);
        const auto readyIdentity = DeriveName(Name("ui.sdf_page_ready/"), suffix);
        const auto textureIdentity = DeriveName(Name("ui.sdf_page/"), suffix);
        if(!readyIdentity || !textureIdentity)
            return MakeUnexpected(Failure{});
        const auto readyResult = graph.importExternalCompletion(Core::GpuExternalCompletionDesc().setIdentity(*readyIdentity).setMarkerLabel("UI SDF Page Upload Completion").setToken(page->m_readinessToken));
        if(!readyResult){
            NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: SDF page {} upload completion import failed (queue={}, generation={}, value={})"), index, page->m_readinessToken.physicalQueueIndex, page->m_readinessToken.deviceGeneration, page->m_readinessToken.value);
            return MakeUnexpected(Failure{});
        }
        const Core::GpuExternalCompletionId ready = *readyResult;
        const auto textureResult = graph.importTexture(
            page->m_texture,
            Core::GpuGraphResourceDesc().setIdentity(*textureIdentity).setMarkerLabel("UI Linear SDF Page")
                .setType(Core::GpuGraphResourceType::Texture).setInitialAvailabilityCompletion(ready)
                .setExternalFinalState(Core::ResourceStates::ShaderResource)
        );
        if(!textureResult){
            NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: SDF page {} texture import failed (atlas={}, group={}, generation={})"), index, page->m_page->binding().atlasIdentity, page->m_page->binding().index, page->m_page->binding().generation);
            return MakeUnexpected(Failure{});
        }
        const Core::GpuGraphResourceId texture = *textureResult;
        resources->push_back(texture);
        uses.push_back({ texture, {}, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read });
    }
    return resources;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

