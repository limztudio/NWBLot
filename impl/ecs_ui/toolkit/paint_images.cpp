// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_image_paint{
[[nodiscard]] static bool SameGlyphKey(const GlyphPageBinding& first, const GlyphPageBinding& second){
    return
        first.font == second.font && first.fontGeneration == second.fontGeneration
        && first.atlasIdentity == second.atlasIdentity && first.index == second.index
    ;
}

[[nodiscard]] static bool SameSdfKey(const SdfAtlasPageBinding& first, const SdfAtlasPageBinding& second){
    return
        first.font == second.font
        && first.fontGeneration == second.fontGeneration && first.atlasIdentity == second.atlasIdentity
        && first.generation == second.generation && first.index == second.index
    ;
}

[[nodiscard]] static bool SameTextureKey(const ImageSource& first, const ImageSource& second){
    return first.identity() == second.identity() && first.generation() == second.generation();
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PaintBuilder::prepareImages(
    const SharedGlyphPage* const glyphPages,
    const usize glyphCount,
    const SharedSdfAtlasPage* const sdfPages,
    const usize sdfCount,
    const SharedImageSource* const textureImages,
    const usize textureCount){
    GLOBAL_ASSERT(m_recording);
    if(
        glyphCount > s_PaintMaxImages || sdfCount > s_PaintMaxImages || textureCount > s_PaintMaxImages
        || (glyphCount != 0u && !glyphPages) || (sdfCount != 0u && !sdfPages)
        || (textureCount != 0u && !textureImages)
    )
        return false;
    usize addedGlyphs = 0u;
    for(usize candidate = 0u; candidate < glyphCount; ++candidate){
        if(!glyphPages[candidate])
            return false;
        const GlyphPageBinding& binding = glyphPages[candidate]->binding();
        bool found = false;
        for(const SharedGlyphPage& existing : m_snapshot.m_glyphPages){
            if(__hidden_ui_image_paint::SameGlyphKey(binding, existing->binding())){
                if(binding.width != existing->binding().width || binding.height != existing->binding().height)
                    return false;
                found = true;
                break;
            }
        }
        for(usize previous = 0u; previous < candidate; ++previous){
            if(__hidden_ui_image_paint::SameGlyphKey(binding, glyphPages[previous]->binding())){
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
        bool found = false;
        for(const SharedSdfAtlasPage& existing : m_snapshot.m_sdfPages){
            if(__hidden_ui_image_paint::SameSdfKey(binding, existing->binding())){
                if(!(binding == existing->binding()))
                    return false;
                found = true;
                break;
            }
        }
        for(usize previous = 0u; previous < candidate; ++previous){
            if(__hidden_ui_image_paint::SameSdfKey(binding, sdfPages[previous]->binding())){
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
        bool found = false;
        for(const SharedImageSource& existing : m_snapshot.m_textureImages){
            if(__hidden_ui_image_paint::SameTextureKey(*textureImages[candidate], *existing)){
                found = true;
                break;
            }
        }
        for(usize previous = 0u; previous < candidate; ++previous){
            if(__hidden_ui_image_paint::SameTextureKey(*textureImages[candidate], *textureImages[previous])){
                found = true;
                break;
            }
        }
        if(!found)
            ++addedTextures;
    }
    const usize existingCount =
        m_snapshot.m_glyphPages.size() + m_snapshot.m_sdfPages.size() + m_snapshot.m_textureImages.size();
    if(existingCount > s_PaintMaxImages || addedGlyphs + addedSdf + addedTextures > s_PaintMaxImages - existingCount)
        return false;
    // All image kinds are admitted before any binding is published, including append-only coverage upgrades.
    m_snapshot.m_glyphPages.reserve(m_snapshot.m_glyphPages.size() + addedGlyphs);
    m_snapshot.m_sdfPages.reserve(m_snapshot.m_sdfPages.size() + addedSdf);
    m_snapshot.m_textureImages.reserve(m_snapshot.m_textureImages.size() + addedTextures);
    for(usize candidate = 0u; candidate < glyphCount; ++candidate){
        SharedGlyphPage* existing = nullptr;
        for(SharedGlyphPage& bound : m_snapshot.m_glyphPages){
            if(__hidden_ui_image_paint::SameGlyphKey(bound->binding(), glyphPages[candidate]->binding())){
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
        bool found = false;
        for(const SharedSdfAtlasPage& bound : m_snapshot.m_sdfPages){
            if(__hidden_ui_image_paint::SameSdfKey(bound->binding(), sdfPages[candidate]->binding())){
                found = true;
                break;
            }
        }
        if(!found)
            m_snapshot.m_sdfPages.push_back(sdfPages[candidate]);
    }
    for(usize candidate = 0u; candidate < textureCount; ++candidate){
        bool found = false;
        for(const SharedImageSource& bound : m_snapshot.m_textureImages){
            if(__hidden_ui_image_paint::SameTextureKey(*bound, *textureImages[candidate])){
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

