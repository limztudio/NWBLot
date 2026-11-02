// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_image_cache{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TCached, typename TRequested, typename TSameKey>
[[nodiscard]] static usize CountMissingCacheEntries(
    const Vector<TCached, Core::Alloc::GlobalArena>& cache,
    const PaintVector<TRequested>& requested,
    TSameKey&& sameKey
)noexcept{
    usize missing = 0u;
    for(const TRequested& item : requested){
        const auto found = FindIf(cache.begin(), cache.end(), [&item, &sameKey](const TCached& cached)noexcept{
            return sameKey(cached, item);
        });
        if(found == cache.end())
            ++missing;
    }
    return missing;
}

template<typename TCached, typename TRequested, typename TSameKey>
static void RetainReferencedCacheEntries(
    const PaintVector<TRequested>& requested,
    TSameKey&& sameKey,
    Vector<TCached, Core::Alloc::GlobalArena>& cache
)noexcept{
    for(usize index = 0u; index < cache.size();){
        const TCached& cached = cache[index];
        const auto found = FindIf(requested.begin(), requested.end(), [&cached, &sameKey](const TRequested& item)noexcept{
            return sameKey(cached, item);
        });
        if(found == requested.end())
            cache.erase(cache.begin() + static_cast<isize>(index));
        else
            ++index;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuRendererState::trimImageCache(const DrawSnapshot& snapshot){
    using namespace __hidden_ui_gpu_image_cache;
    const auto sameGlyphKey = [](const GpuVersion<GpuGlyphVersion>& cached, const SharedGlyphPage& page)noexcept{
        return SameGlyphPageKey(cached->m_page->binding(), page->binding());
    };
    const auto sameSdfKey = [](const GpuVersion<GpuSdfAtlasVersion>& cached, const SharedSdfAtlasPage& page)noexcept{
        return SameSdfAtlasPageKey(cached->m_page->binding(), page->binding());
    };
    const auto sameTextureKey = [](const GpuVersion<GpuTextureImageVersion>& cached, const SharedImageSource& source)noexcept{
        return SameTextureImageKey(*cached->m_source, *source);
    };
    const usize missing = CountMissingCacheEntries(m_glyphCache, snapshot.glyphPages(), sameGlyphKey)
        + CountMissingCacheEntries(m_sdfCache, snapshot.sdfPages(), sameSdfKey)
        + CountMissingCacheEntries(m_textureCache, snapshot.textureImages(), sameTextureKey);
    if(m_glyphCache.size() + m_sdfCache.size() + m_textureCache.size() + missing <= s_PaintMaxImages)
        return;
    // Retain requested logical keys even when their immutable metadata conflicts; preparation must reject that conflict.
    // Live frames retain evicted versions.
    // Removing only cache ownership never changes sampled pixels or live descriptors.
    RetainReferencedCacheEntries(snapshot.glyphPages(), sameGlyphKey, m_glyphCache);
    RetainReferencedCacheEntries(snapshot.sdfPages(), sameSdfKey, m_sdfCache);
    RetainReferencedCacheEntries(snapshot.textureImages(), sameTextureKey, m_textureCache);
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

