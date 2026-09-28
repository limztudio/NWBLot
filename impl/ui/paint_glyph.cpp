// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_glyph_paint{
static bool SamePage(const GlyphPageBinding& first, const GlyphPageBinding& second){
    return
        first.font == second.font && first.fontGeneration == second.fontGeneration
        && first.atlasIdentity == second.atlasIdentity && first.index == second.index
    ;
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PaintBuilder::prepareGlyphPages(const SharedGlyphPage* const pages, const usize count){
    NWB_ASSERT(m_recording);
    if(count > s_PaintMaxGlyphPages || (count != 0u && !pages))
        return false;
    usize added = 0u;
    for(usize candidate = 0u; candidate < count; ++candidate){
        if(!pages[candidate])
            return false;
        const GlyphPageBinding& binding = pages[candidate]->binding();
        bool found = false;
        for(const SharedGlyphPage& existing : m_snapshot.m_glyphPages){
            if(__hidden_ui_glyph_paint::SamePage(binding, existing->binding())){
                if(binding.width != existing->binding().width || binding.height != existing->binding().height)
                    return false;
                found = true;
                break;
            }
        }
        for(usize previous = 0u; previous < candidate; ++previous){
            if(__hidden_ui_glyph_paint::SamePage(binding, pages[previous]->binding())){
                if(binding.width != pages[previous]->binding().width || binding.height != pages[previous]->binding().height)
                    return false;
                found = true;
                break;
            }
        }
        if(!found)
            ++added;
    }
    if(added > s_PaintMaxGlyphPages - m_snapshot.m_glyphPages.size())
        return false;
    m_snapshot.m_glyphPages.reserve(m_snapshot.m_glyphPages.size() + added);
    for(usize candidate = 0u; candidate < count; ++candidate){
        SharedGlyphPage* existing = nullptr;
        for(SharedGlyphPage& bound : m_snapshot.m_glyphPages){
            if(__hidden_ui_glyph_paint::SamePage(bound->binding(), pages[candidate]->binding())){
                existing = &bound;
                break;
            }
        }
        if(!existing)
            m_snapshot.m_glyphPages.push_back(pages[candidate]);
        else if((*existing)->binding().generation < pages[candidate]->binding().generation)
            *existing = pages[candidate];
    }
    return true;
}

bool PaintBuilder::drawGlyph(const SharedGlyphPage& page, const Rect& rectangle, const Rect& uv, const Color& tint){
    NWB_ASSERT(m_recording);
    if(
        !page || !IsFinite(uv.x) || !IsFinite(uv.y) || !IsFinite(uv.width) || !IsFinite(uv.height)
        || uv.x < 0.0f || uv.y < 0.0f || uv.width <= 0.0f || uv.height <= 0.0f
        || uv.x + uv.width > 1.0f || uv.y + uv.height > 1.0f
    )
        return false;
    if(!prepareGlyphPages(&page, 1u))
        return false;
    u32 index = 0u;
    for(; index < m_snapshot.m_glyphPages.size(); ++index){
        if(__hidden_ui_glyph_paint::SamePage(m_snapshot.m_glyphPages[index]->binding(), page->binding()))
            break;
    }
    NWB_ASSERT(index < m_snapshot.m_glyphPages.size());
    emitQuad(rectangle, uv, tint, PaintMaterial::Glyph, index);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

