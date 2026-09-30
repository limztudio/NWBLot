// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_image_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool SameGlyphKey(const GlyphPageBinding& first, const GlyphPageBinding& second){
    return
        first.font == second.font && first.fontGeneration == second.fontGeneration
        && first.atlasIdentity == second.atlasIdentity && first.index == second.index
    ;
}

[[nodiscard]] static bool SameSdfKey(const SdfAtlasPageBinding& first, const SdfAtlasPageBinding& second){
    return
        first.font == second.font && first.fontGeneration == second.fontGeneration
        && first.atlasIdentity == second.atlasIdentity && first.generation == second.generation && first.index == second.index
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuRendererState::trimImageCache(const DrawSnapshot& snapshot){
    usize missing = 0u;
    for(const SharedGlyphPage& page : snapshot.glyphPages()){
        const auto found = FindIf(m_glyphCache.begin(), m_glyphCache.end(), [&page](const GpuVersion<GpuGlyphVersion>& cached){
            return __hidden_ui_gpu_image_cache::SameGlyphKey(cached->m_page->binding(), page->binding());
        });
        if(found == m_glyphCache.end())
            ++missing;
    }
    for(const SharedSdfAtlasPage& page : snapshot.sdfPages()){
        const auto found = FindIf(m_sdfCache.begin(), m_sdfCache.end(), [&page](const GpuVersion<GpuSdfAtlasVersion>& cached){
            return __hidden_ui_gpu_image_cache::SameSdfKey(cached->m_page->binding(), page->binding());
        });
        if(found == m_sdfCache.end())
            ++missing;
    }
    for(const SharedImageSource& source : snapshot.textureImages()){
        const auto found = FindIf(m_textureCache.begin(), m_textureCache.end(), [&source](const auto& cached){
            return cached->m_source->generation() == source->generation();
        });
        if(found == m_textureCache.end())
            ++missing;
    }
    if(m_glyphCache.size() + m_sdfCache.size() + m_textureCache.size() + missing <= s_PaintMaxImages)
        return;
    // Retain requested logical keys even when their immutable metadata conflicts; preparation must reject that conflict.
    // Live frames retain evicted versions. Removing only cache ownership never changes sampled pixels or live descriptors.
    for(usize index = 0u; index < m_glyphCache.size();){
        const auto& cached = m_glyphCache[index];
        const auto found = FindIf(snapshot.glyphPages().begin(), snapshot.glyphPages().end(), [&cached](const SharedGlyphPage& page){
            return __hidden_ui_gpu_image_cache::SameGlyphKey(cached->m_page->binding(), page->binding());
        });
        if(found == snapshot.glyphPages().end())
            m_glyphCache.erase(m_glyphCache.begin() + static_cast<isize>(index));
        else
            ++index;
    }
    for(usize index = 0u; index < m_sdfCache.size();){
        const auto& cached = m_sdfCache[index];
        const auto found = FindIf(snapshot.sdfPages().begin(), snapshot.sdfPages().end(), [&cached](const SharedSdfAtlasPage& page){
            return __hidden_ui_gpu_image_cache::SameSdfKey(cached->m_page->binding(), page->binding());
        });
        if(found == snapshot.sdfPages().end())
            m_sdfCache.erase(m_sdfCache.begin() + static_cast<isize>(index));
        else
            ++index;
    }
    for(usize index = 0u; index < m_textureCache.size();){
        const auto& cached = m_textureCache[index];
        const auto found = FindIf(snapshot.textureImages().begin(), snapshot.textureImages().end(), [&cached](const auto& source){
            return cached->m_source->generation() == source->generation();
        });
        if(found == snapshot.textureImages().end())
            m_textureCache.erase(m_textureCache.begin() + static_cast<isize>(index));
        else
            ++index;
    }
}

void GpuRendererState::evictImageCacheEntry(){
    // Pending and in-flight frames keep their own strong version references after cache ownership is removed.
    if(!m_glyphCache.empty())
        m_glyphCache.erase(m_glyphCache.begin());
    else if(!m_sdfCache.empty())
        m_sdfCache.erase(m_sdfCache.begin());
    else if(!m_textureCache.empty())
        m_textureCache.erase(m_textureCache.begin());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

