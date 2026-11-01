// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 PaintBuilder::prepareImages(const Span<const SharedGlyphPage> glyphPages, const Span<const SharedSdfAtlasPage> sdfPages, const Span<const SharedImageSource> textureImages){
    NWB_ASSERT(m_recording);
    const usize glyphCount = glyphPages.size();
    const usize sdfCount = sdfPages.size();
    const usize textureCount = textureImages.size();
    if(glyphCount > s_PaintMaxImages || sdfCount > s_PaintMaxImages || textureCount > s_PaintMaxImages)
        return false;
    usize addedGlyphs = 0u;
    for(usize candidate = 0u; candidate < glyphCount; ++candidate){
        if(!glyphPages[candidate])
            return false;
        const GlyphPageBinding& binding = glyphPages[candidate]->binding();
        b8 found = false;
        for(const SharedGlyphPage& existing : m_snapshot.m_glyphPages){
            if(SameGlyphPageKey(binding, existing->binding())){
                if(binding.width != existing->binding().width || binding.height != existing->binding().height)
                    return false;
                found = true;
                break;
            }
        }
        for(usize previous = 0u; previous < candidate; ++previous){
            if(SameGlyphPageKey(binding, glyphPages[previous]->binding())){
                const GlyphPageBinding& previousBinding = glyphPages[previous]->binding();
                if(binding.width != previousBinding.width || binding.height != previousBinding.height)
                    return false;
                found = true;
                break;
            }
        }
        if(!found)
            ++addedGlyphs;
    }
    usize addedSdf = 0u;
    for(usize candidate = 0u; candidate < sdfCount; ++candidate){
        if(!sdfPages[candidate])
            return false;
        const SdfAtlasPageBinding& binding = sdfPages[candidate]->binding();
        b8 found = false;
        for(const SharedSdfAtlasPage& existing : m_snapshot.m_sdfPages){
            if(SameSdfAtlasPageKey(binding, existing->binding())){
                if(!(binding == existing->binding()))
                    return false;
                found = true;
                break;
            }
        }
        for(usize previous = 0u; previous < candidate; ++previous){
            if(SameSdfAtlasPageKey(binding, sdfPages[previous]->binding())){
                if(!(binding == sdfPages[previous]->binding()))
                    return false;
                found = true;
                break;
            }
        }
        if(!found)
            ++addedSdf;
    }
    usize addedTextures = 0u;
    for(usize candidate = 0u; candidate < textureCount; ++candidate){
        if(!textureImages[candidate])
            return false;
        b8 found = false;
        for(const SharedImageSource& existing : m_snapshot.m_textureImages){
            if(SameTextureImageKey(*textureImages[candidate], *existing)){
                found = true;
                break;
            }
        }
        for(usize previous = 0u; previous < candidate; ++previous){
            if(SameTextureImageKey(*textureImages[candidate], *textureImages[previous])){
                found = true;
                break;
            }
        }
        if(!found)
            ++addedTextures;
    }
    const usize existingCount = m_snapshot.m_glyphPages.size() + m_snapshot.m_sdfPages.size() + m_snapshot.m_textureImages.size();
    if(existingCount > s_PaintMaxImages || addedGlyphs + addedSdf + addedTextures > s_PaintMaxImages - existingCount)
        return false;
    // All image kinds are admitted before any binding is published, including append-only coverage upgrades.
    m_snapshot.m_glyphPages.reserve(m_snapshot.m_glyphPages.size() + addedGlyphs);
    m_snapshot.m_sdfPages.reserve(m_snapshot.m_sdfPages.size() + addedSdf);
    m_snapshot.m_textureImages.reserve(m_snapshot.m_textureImages.size() + addedTextures);
    for(usize candidate = 0u; candidate < glyphCount; ++candidate){
        SharedGlyphPage* existing = nullptr;
        for(SharedGlyphPage& bound : m_snapshot.m_glyphPages){
            if(SameGlyphPageKey(bound->binding(), glyphPages[candidate]->binding())){
                existing = &bound;
                break;
            }
        }
        if(!existing)
            m_snapshot.m_glyphPages.push_back(glyphPages[candidate]);
        else if((*existing)->binding().generation < glyphPages[candidate]->binding().generation)
            *existing = glyphPages[candidate];
    }
    for(usize candidate = 0u; candidate < sdfCount; ++candidate){
        b8 found = false;
        for(const SharedSdfAtlasPage& bound : m_snapshot.m_sdfPages){
            if(SameSdfAtlasPageKey(bound->binding(), sdfPages[candidate]->binding())){
                found = true;
                break;
            }
        }
        if(!found)
            m_snapshot.m_sdfPages.push_back(sdfPages[candidate]);
    }
    for(usize candidate = 0u; candidate < textureCount; ++candidate){
        b8 found = false;
        for(const SharedImageSource& bound : m_snapshot.m_textureImages){
            if(SameTextureImageKey(*bound, *textureImages[candidate])){
                found = true;
                break;
            }
        }
        if(!found)
            m_snapshot.m_textureImages.push_back(textureImages[candidate]);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

