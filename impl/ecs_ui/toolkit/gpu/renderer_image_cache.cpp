// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"

#include <global/range_algorithms.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuRendererState::trimImageCache(const DrawSnapshot& snapshot){
    const auto sameGlyphKey = [](const GpuVersion<GpuGlyphVersion>& cached, const SharedGlyphPage& page)noexcept{
        return SameGlyphPageKey(cached->m_page->binding(), page->binding());
    };
    const auto sameSdfKey = [](const GpuVersion<GpuSdfAtlasVersion>& cached, const SharedSdfAtlasPage& page)noexcept{
        return SameSdfAtlasPageKey(cached->m_page->binding(), page->binding());
    };
    const auto sameTextureKey = [](const GpuVersion<GpuTextureImageVersion>& cached, const SharedImageSource& source)noexcept{
        return SameTextureImageKey(*cached->m_source, *source);
    };
    const auto countMissing = [](const auto& cache, const auto& requested, const auto& sameKey)noexcept{
        return CountMissingCacheEntries(cache, requested, sameKey);
    };
    const auto retainReferenced = [](const auto& requested, const auto& sameKey, auto& cache)noexcept{
        RetainReferencedCacheEntries(requested, sameKey, cache);
    };
    const usize missing = countMissing(m_glyphCache, snapshot.glyphPages(), sameGlyphKey)
        + countMissing(m_sdfCache, snapshot.sdfPages(), sameSdfKey)
        + countMissing(m_textureCache, snapshot.textureImages(), sameTextureKey);
    if(m_glyphCache.size() + m_sdfCache.size() + m_textureCache.size() + missing <= s_PaintMaxImages)
        return;
    // Retain requested logical keys even when their immutable metadata conflicts; preparation must reject that conflict.
    // Live frames retain evicted versions.
    // Removing only cache ownership never changes sampled pixels or live descriptors.
    retainReferenced(snapshot.glyphPages(), sameGlyphKey, m_glyphCache);
    retainReferenced(snapshot.sdfPages(), sameSdfKey, m_sdfCache);
    retainReferenced(snapshot.textureImages(), sameTextureKey, m_textureCache);
}

void GpuRendererState::evictImageCacheEntry()noexcept{
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

