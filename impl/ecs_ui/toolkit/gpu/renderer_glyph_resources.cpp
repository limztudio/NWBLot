// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"

#include <core/common/log.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_glyphs{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool SamePageVersion(const GlyphPage& first, const GlyphPage& second){
    const GlyphPageBinding& a = first.binding();
    const GlyphPageBinding& b = second.binding();
    if(!SameGlyphPageKey(a, b) || a.generation != b.generation || a.width != b.width || a.height != b.height)
        return false;
    if(&first == &second)
        return true;
    // Equal identities may be reconstructed, but cannot silently alias different immutable coverage bytes.
    return
        first.pixels().size() == second.pixels().size()
        && NWB_MEMCMP(first.pixels().data(), second.pixels().data(), first.pixels().size()) == 0
    ;
}

[[nodiscard]] static bool ValidPage(const SharedGlyphPage& page){
    if(!page)
        return false;
    const GlyphPageBinding& binding = page->binding();
    return
        binding.font.valid() && binding.fontGeneration != 0u && binding.atlasIdentity != 0u && binding.generation != 0u
        && binding.index < s_GlyphAtlasMaxPages && binding.width != 0u && binding.height != 0u
        && binding.width <= s_GlyphAtlasPageExtent && binding.height <= s_GlyphAtlasPageExtent
        && page->pixels().size() == static_cast<usize>(binding.width) * binding.height
    ;
}

[[nodiscard]] static bool PrepareDescriptor(GpuGlyphVersion& version){
    if(version.m_sampledImage.valid())
        return true;
    Core::GpuDescriptorHeap& heap = version.m_graphics.getDevice().getDescriptorHeap();
    const Core::GpuDescriptorHandle descriptor = heap.allocate(Core::GpuDescriptorClass::SampledImage);
    if(!descriptor.valid())
        return false;
    if(!heap.write(descriptor, Core::DescriptorWriteItem::TextureSrv(
        0u, version.m_texture.get(), Core::Format::R8_UNORM, Core::s_AllSubresources, Core::TextureDimension::Texture2D
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


GpuGlyphVersion::GpuGlyphVersion(Core::GraphicsRuntime& graphics, SharedGlyphPage page)
    : m_graphics(graphics)
    , m_page(Move(page))
{}

GpuGlyphVersion::~GpuGlyphVersion()noexcept{
    if(m_sampledImage.valid())
        m_graphics.getDevice().getDescriptorHeap().free(m_sampledImage);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuRendererState::ValidateGlyphPages(const DrawSnapshot& snapshot){
    const auto& pages = snapshot.glyphPages();
    if(pages.size() + snapshot.sdfPages().size() + snapshot.textureImages().size() > s_PaintMaxImages)
        return false;
    for(usize index = 0u; index < pages.size(); ++index){
        if(!__hidden_ui_gpu_glyphs::ValidPage(pages[index]))
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(SameGlyphPageKey(pages[index]->binding(), pages[previous]->binding()))
                return false;
        }
    }
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material == PaintMaterial::Glyph){
            if(command.glyphPageIndex >= pages.size() || command.sdfPageIndex != Limit<u32>::s_Max || command.sdfChannel != 0u)
                return false;
        }
        else if(command.material == PaintMaterial::SdfGlyph){
            if(command.glyphPageIndex != Limit<u32>::s_Max)
                return false;
        }
        else if(
            command.material == PaintMaterial::Solid || command.material == PaintMaterial::Skin
            || command.material == PaintMaterial::Image
        ){
            if(command.glyphPageIndex != Limit<u32>::s_Max || command.sdfPageIndex != Limit<u32>::s_Max || command.sdfChannel != 0u)
                return false;
        }
        else
            return false;
    }
    return true;
}

GpuVersion<GpuGlyphVersion> GpuRendererState::prepareGlyphPage(const SharedGlyphPage& page){
    usize replaceIndex = m_glyphCache.size();
    for(usize index = 0u; index < m_glyphCache.size(); ++index){
        const auto& existing = m_glyphCache[index];
        if(!SameGlyphPageKey(existing->m_page->binding(), page->binding()))
            continue;
        replaceIndex = index;
        if(existing->m_page->binding().generation != page->binding().generation)
            break;
        if(!__hidden_ui_gpu_glyphs::SamePageVersion(*existing->m_page, *page))
            return {};
        GpuVersion<GpuGlyphVersion> reused = existing;
        m_glyphCache.erase(m_glyphCache.begin() + static_cast<isize>(index));
        m_glyphCache.push_back(reused);
        if(!__hidden_ui_gpu_glyphs::PrepareDescriptor(*reused))
            return {};
        return reused;
    }
    Core::Device& device = m_graphics.getDevice();
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderSample;
    if((device.queryFormatSupport(Core::Format::R8_UNORM) & requiredSupport) != requiredSupport)
        return {};
    auto version = MakeGpuVersion<GpuGlyphVersion>(m_arena, m_graphics, page);
    if(!version)
        return {};
    const GlyphPageBinding& binding = page->binding();
    Core::TextureDesc description;
    description
        .setName(Name("ui.glyph_page")).setWidth(binding.width).setHeight(binding.height)
        .setFormat(Core::Format::R8_UNORM).setInitialState(Core::ResourceStates::ShaderResource).setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    version->m_texture = device.createTexture(description);
    if(!version->m_texture)
        return {};
    const Core::GraphicsRuntime::TextureUploadRegion region{
        .data = page->pixels().data(),
        .dataSize = page->pixels().size(),
        .rowPitch = binding.width,
        .depthPitch = page->pixels().size(),
    };
    // Upload only a fresh immutable image. Accepted readiness stays with this version across presentation retries.
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
    // Frames retain superseded versions; evicting a cache entry never overwrites an image or frees a live frame's descriptor.
    if(replaceIndex < m_glyphCache.size())
        m_glyphCache.erase(m_glyphCache.begin() + static_cast<isize>(replaceIndex));
    else if(m_glyphCache.size() + m_sdfCache.size() + m_textureCache.size() == s_PaintMaxImages)
        evictImageCacheEntry();
    m_glyphCache.push_back(version);
    // Cache accepted uploads before descriptor publication so descriptor exhaustion retries cannot discard their readiness.
    if(!__hidden_ui_gpu_glyphs::PrepareDescriptor(*version))
        return {};
    return version;
}

bool GpuRendererState::prepareGlyphPages(GpuFrameData& frame){
    const auto& pages = frame.m_snapshot.glyphPages();
    frame.m_glyphPages.reserve(pages.size());
    // Retain successful earlier uploads if preparation fails partway through the page set.
    while(frame.m_glyphPages.size() < pages.size()){
        auto version = prepareGlyphPage(pages[frame.m_glyphPages.size()]);
        if(!version)
            return false;
        frame.m_glyphPages.push_back(Move(version));
    }
    return true;
}

bool GpuRendererState::declareGlyphPages(
    Core::GpuTaskGraph& graph,
    const GpuFrame& frame,
    GpuGlyphGraphResources& resources,
    GpuRasterResourceUses& uses){
    if(frame->m_glyphPages.size() != frame->m_snapshot.glyphPages().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: prepared glyph page count does not match the immutable snapshot"));
        return false;
    }
    for(usize index = 0u; index < frame->m_glyphPages.size(); ++index){
        const auto& page = frame->m_glyphPages[index];
        char identityText[32u] = {};
        const AStringView suffix = FormatDecimal(index, identityText);
        // Graph imports unify equal identities; every immutable snapshot page needs a distinct texture and completion name.
        const Name readyIdentity = DeriveName(Name("ui.glyph_page_ready/"), suffix);
        const Name textureIdentity = DeriveName(Name("ui.glyph_page/"), suffix);
        const Core::GpuExternalCompletionId ready = graph.importExternalCompletion(
            Core::GpuExternalCompletionDesc().setIdentity(readyIdentity).setMarkerLabel("UI Glyph Page Upload Completion")
                .setToken(page->m_readinessToken)
        );
        if(!ready.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: glyph page {} upload completion import failed (queue={}, generation={}, value={})")
                , index, page->m_readinessToken.physicalQueueIndex, page->m_readinessToken.deviceGeneration, page->m_readinessToken.value
            );
            return false;
        }
        const Core::GpuGraphResourceId texture = graph.importTexture(
            page->m_texture,
            Core::GpuGraphResourceDesc().setIdentity(textureIdentity).setMarkerLabel("UI Glyph Coverage Page")
                .setType(Core::GpuGraphResourceType::Texture).setInitialAvailabilityCompletion(ready)
                .setExternalFinalState(Core::ResourceStates::ShaderResource)
        );
        if(!texture.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: glyph page {} texture import failed (atlas={}, page={}, generation={})")
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

