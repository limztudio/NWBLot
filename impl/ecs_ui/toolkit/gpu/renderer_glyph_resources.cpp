// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"

#include <core/common/log.h>

#include <global/binary.h>
#include <global/range_algorithms.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuRendererState::ValidateGlyphPages(const DrawSnapshot& snapshot)noexcept{
    if(!ValidSnapshotImageCount(snapshot))
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

Expected<GpuVersion<GpuGlyphVersion>> GpuRendererState::prepareGlyphPage(const SharedGlyphPage& page){
    usize replaceIndex = m_glyphCache.size();
    for(usize index = 0u; index < m_glyphCache.size(); ++index){
        const auto& existing = m_glyphCache[index];
        if(!SameGlyphPageKey(existing->m_page->binding(), page->binding()))
            continue;
        replaceIndex = index;
        if(existing->m_page->binding().generation != page->binding().generation)
            break;
        if(!SameGlyphPageVersion(existing->m_page->binding(), existing->m_page->pixels(), page->binding(), page->pixels(), existing->m_page.get() == page.get()))
            return MakeUnexpected(Failure{});
        GpuVersion<GpuGlyphVersion> reused = existing;
        m_glyphCache.erase(m_glyphCache.begin() + static_cast<isize>(index));
        m_glyphCache.push_back(reused);
        if(!PrepareGpuSampledImageDescriptor(reused->m_graphics, reused->m_texture, Core::Format::R8_UNORM, reused->m_sampledImage))
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
    if(!CreateAndUploadGpuPageTexture(
        m_graphics,
        GpuPageTextureUploadDesc{
            .name = s_GpuGlyphPageTextureName,
            .width = binding.width,
            .height = binding.height,
            .format = Core::Format::R8_UNORM,
            .data = page->pixels().data(),
            .dataSize = page->pixels().size(),
            .rowPitch = binding.width,
        },
        version->m_texture,
        version->m_readinessToken
    ))
        return MakeUnexpected(Failure{});
    // Frames retain superseded versions; evicting a cache entry never overwrites an image or frees a live frame's descriptor.
    if(replaceIndex < m_glyphCache.size())
        m_glyphCache.erase(m_glyphCache.begin() + static_cast<isize>(replaceIndex));
    else if(m_glyphCache.size() + m_sdfCache.size() + m_textureCache.size() == s_PaintMaxImages)
        evictImageCacheEntry();
    m_glyphCache.push_back(version);
    // Cache accepted uploads before descriptor publication so descriptor exhaustion retries cannot discard their readiness.
    if(!PrepareGpuSampledImageDescriptor(version->m_graphics, version->m_texture, Core::Format::R8_UNORM, version->m_sampledImage))
        return MakeUnexpected(Failure{});
    return Move(version);
}

b8 GpuRendererState::prepareGlyphPages(GpuFrameData& frame){
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

Expected<GpuSampledGraphResources> GpuRendererState::declareGlyphPages(Core::GpuTaskGraph& graph, const GpuFrame& frame, GpuRasterResourceUses& uses){
    Expected<GpuSampledGraphResources> resources{s_InPlace};
    if(frame->m_glyphPages.size() != frame->m_snapshot.glyphPages().size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GpuRenderer: prepared glyph page count does not match the immutable snapshot"));
        return MakeUnexpected(Failure{});
    }
    for(usize index = 0u; index < frame->m_glyphPages.size(); ++index){
        const auto& page = frame->m_glyphPages[index];
        const auto texture = ImportGpuSampledTexture(
            GpuSampledTextureImportDesc{
                .index = index,
                .readyPrefix = s_GpuGlyphPageReadyPrefix,
                .readyMarkerLabel = s_GpuGlyphPageReadyMarkerLabel,
                .completionLabel = NWB_TEXT("glyph page"),
                .readinessToken = page->m_readinessToken,
                .texturePrefix = s_GpuGlyphPagePrefix,
                .textureMarkerLabel = s_GpuGlyphPageMarkerLabel,
                .texture = page->m_texture,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

