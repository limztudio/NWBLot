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
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PaintBuilder::prepareImages(
    const SharedGlyphPage* const glyphPages,
    const usize glyphCount,
    const SharedSdfAtlasPage* const sdfPages,
    const usize sdfCount){
    NWB_ASSERT(m_recording);
    if(
        glyphCount > s_PaintMaxImages || sdfCount > s_PaintMaxImages
        || (glyphCount != 0u && !glyphPages) || (sdfCount != 0u && !sdfPages)
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
                if(binding.width != glyphPages[previous]->binding().width || binding.height != glyphPages[previous]->binding().height)
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
    const usize existingCount = m_snapshot.m_glyphPages.size() + m_snapshot.m_sdfPages.size();
    if(addedGlyphs + addedSdf > s_PaintMaxImages - existingCount)
        return false;
    // Both sets are admitted before either binding set is published, so a mixed label fails without partial upgrades.
    m_snapshot.m_glyphPages.reserve(m_snapshot.m_glyphPages.size() + addedGlyphs);
    m_snapshot.m_sdfPages.reserve(m_snapshot.m_sdfPages.size() + addedSdf);
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
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

