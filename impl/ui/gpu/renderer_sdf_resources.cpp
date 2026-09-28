// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_sdf_resources.h"
#include "renderer_internal.h"

#include <core/common/log.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_sdf{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool SamePageKey(const SdfAtlasPageBinding& first, const SdfAtlasPageBinding& second){
    return
        first.font == second.font && first.fontGeneration == second.fontGeneration
        && first.atlasIdentity == second.atlasIdentity && first.generation == second.generation && first.index == second.index
    ;
}

[[nodiscard]] static bool ValidPage(const SharedSdfAtlasPage& page){
    if(!page)
        return false;
    const SdfAtlasPageBinding& binding = page->binding();
    return
        binding.font.valid() && binding.fontGeneration != 0u && binding.atlasIdentity != 0u && binding.generation != 0u
        && binding.index < s_FontAtlasMaxGroupCount && binding.width != 0u && binding.height != 0u
        && binding.width <= s_FontAtlasMaxExtent && binding.height <= s_FontAtlasMaxExtent
        && binding.spreadPixels >= s_FontAtlasMinSpreadPixels && binding.spreadPixels <= s_FontAtlasMaxSpreadPixels
        && binding.distanceEncoding == s_SdfDistanceEncodingFreeTypeU8
        && page->pixels().size() == static_cast<usize>(binding.width) * binding.height * 4u
    ;
}

[[nodiscard]] static bool PrepareDescriptor(GpuSdfAtlasVersion& version){
    if(version.m_sampledImage.valid())
        return true;
    Core::GpuDescriptorHeap& heap = version.m_graphics.getDevice().getDescriptorHeap();
    const Core::GpuDescriptorHandle descriptor = heap.allocate(Core::GpuDescriptorClass::SampledImage);
    if(!descriptor.valid())
        return false;
    if(!heap.write(descriptor, Core::DescriptorWriteItem::Texture_SRV(
        0u, version.m_texture.get(), Core::Format::RGBA8_UNORM, Core::s_AllSubresources, Core::TextureDimension::Texture2D
    ))){
        heap.free(descriptor);
        return false;
    }
    version.m_sampledImage = descriptor;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuSdfAtlasVersion::GpuSdfAtlasVersion(Core::GraphicsRuntime& graphics, SharedSdfAtlasPage page)
    : m_graphics(graphics)
    , m_page(Move(page))
{}

GpuSdfAtlasVersion::~GpuSdfAtlasVersion()noexcept{
    if(m_sampledImage.valid())
        m_graphics.getDevice().getDescriptorHeap().free(m_sampledImage);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuRendererState::validateSdfPages(const DrawSnapshot& snapshot){
    const auto& pages = snapshot.sdfPages();
    if(pages.size() + snapshot.glyphPages().size() > s_PaintMaxImages)
        return false;
    for(usize index = 0u; index < pages.size(); ++index){
        if(!__hidden_ui_gpu_sdf::ValidPage(pages[index]))
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(__hidden_ui_gpu_sdf::SamePageKey(pages[index]->binding(), pages[previous]->binding()))
                return false;
        }
    }
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material == PaintMaterial::SdfGlyph && (command.sdfPageIndex >= pages.size() || command.sdfChannel >= 4u))
            return false;
    }
    return true;
}

GpuVersion<GpuSdfAtlasVersion> GpuRendererState::prepareSdfPage(const SharedSdfAtlasPage& page){
    for(usize index = 0u; index < m_sdfCache.size(); ++index){
        const auto& existing = m_sdfCache[index];
        if(!__hidden_ui_gpu_sdf::SamePageKey(existing->m_page->binding(), page->binding()))
            continue;
        if(!(existing->m_page->binding() == page->binding()))
            return {};
        GpuVersion<GpuSdfAtlasVersion> reused = existing;
        m_sdfCache.erase(m_sdfCache.begin() + static_cast<isize>(index));
        m_sdfCache.push_back(reused);
        if(!__hidden_ui_gpu_sdf::PrepareDescriptor(*reused))
            return {};
        return reused;
    }
    Core::Device& device = m_graphics.getDevice();
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderSample;
    if((device.queryFormatSupport(Core::Format::RGBA8_UNORM) & requiredSupport) != requiredSupport)
        return {};
    auto version = MakeGpuVersion<GpuSdfAtlasVersion>(m_arena, m_graphics, page);
    if(!version)
        return {};
    const SdfAtlasPageBinding& binding = page->binding();
    Core::TextureDesc description;
    description
        .setName(Name("ui.sdf_page")).setWidth(binding.width).setHeight(binding.height)
        .setFormat(Core::Format::RGBA8_UNORM).setInitialState(Core::ResourceStates::ShaderResource).setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    version->m_texture = device.createTexture(description);
    if(!version->m_texture)
        return {};
    const Core::GraphicsRuntime::TextureUploadRegion region{
        .data = page->pixels().data(),
        .dataSize = page->pixels().size(),
        .rowPitch = static_cast<usize>(binding.width) * 4u,
        .depthPitch = page->pixels().size(),
    };
    if(!m_graphics.uploadTextureBatch(Core::GraphicsRuntime::TextureUploadBatchDesc{
        .destination = version->m_texture,
        .regions = &region,
        .regionCount = 1u,
        .acceptedToken = &version->m_readinessToken,
        .finalState = Core::ResourceStates::ShaderResource,
        .physicalInitialState = Core::ResourceStates::Unknown,
        .hasPhysicalInitialState = true,
    }))
        return {};
    if(!version->m_readinessToken.valid() || !version->m_readinessToken.hasPhysicalQueueIdentity())
        return {};
    // Native accepted upload commands retain their destination image until the physical queue completes.
    // Frame owners retain sampled descriptors; cache eviction therefore releases neither a live image nor a live draw view.
    if(m_sdfCache.size() + m_glyphCache.size() == s_PaintMaxImages){
        if(!m_sdfCache.empty())
            m_sdfCache.erase(m_sdfCache.begin());
        else
            m_glyphCache.erase(m_glyphCache.begin());
    }
    m_sdfCache.push_back(version);
    // Preserve every accepted upload before descriptor publication; retrying a failed descriptor never uploads again.
    if(!__hidden_ui_gpu_sdf::PrepareDescriptor(*version))
        return {};
    return version;
}

bool GpuRendererState::prepareSdfPages(GpuFrameData& frame){
    const auto& pages = frame.m_snapshot.sdfPages();
    frame.m_sdfPages.reserve(pages.size());
    while(frame.m_sdfPages.size() < pages.size()){
        auto version = prepareSdfPage(pages[frame.m_sdfPages.size()]);
        if(!version)
            return false;
        frame.m_sdfPages.push_back(Move(version));
    }
    return true;
}

bool GpuRendererState::declareSdfPages(
    Core::GpuTaskGraph& graph,
    const GpuFrame& frame,
    GpuSdfGraphResources& resources,
    GpuRasterResourceUses& uses){
    if(frame->m_sdfPages.size() != frame->m_snapshot.sdfPages().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: prepared SDF page count does not match the immutable snapshot"));
        return false;
    }
    for(usize index = 0u; index < frame->m_sdfPages.size(); ++index){
        const auto& page = frame->m_sdfPages[index];
        char identityText[32u] = {};
        const AStringView suffix = FormatDecimal(index, identityText);
        const Name readyIdentity = DeriveName(Name("ui.sdf_page_ready/"), suffix);
        const Name textureIdentity = DeriveName(Name("ui.sdf_page/"), suffix);
        const Core::GpuExternalCompletionId ready = graph.importExternalCompletion(
            Core::GpuExternalCompletionDesc().setIdentity(readyIdentity).setMarkerLabel("UI SDF Page Upload Completion")
                .setToken(page->m_readinessToken)
        );
        if(!ready.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: SDF page {} upload completion import failed (queue={}, generation={}, value={})")
                , index, page->m_readinessToken.physicalQueueIndex, page->m_readinessToken.deviceGeneration, page->m_readinessToken.value
            );
            return false;
        }
        const Core::GpuGraphResourceId texture = graph.importTexture(
            page->m_texture,
            Core::GpuGraphResourceDesc().setIdentity(textureIdentity).setMarkerLabel("UI Linear RGBA SDF Page")
                .setType(Core::GpuGraphResourceType::Texture).setInitialAvailabilityCompletion(ready)
                .setExternalFinalState(Core::ResourceStates::ShaderResource)
        );
        if(!texture.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: SDF page {} texture import failed (atlas={}, group={}, generation={})")
                , index, page->m_page->binding().atlasIdentity, page->m_page->binding().index, page->m_page->binding().generation
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

