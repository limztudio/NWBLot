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
    return prepareImages(pages, count, nullptr, 0u);
}

bool PaintBuilder::drawGlyph(const SharedGlyphPage& page, const Rect& rectangle, const Rect& uv, const Color& tint){
    GLB_ASSERT(m_recording);
    if(
        !page || !IsFinite(uv.x) || !IsFinite(uv.y) || !IsFinite(uv.width) || !IsFinite(uv.height)
        || uv.x < 0.0f || uv.y < 0.0f || uv.width <= 0.0f || uv.height <= 0.0f
        || uv.x + uv.width > 1.0f || uv.y + uv.height > 1.0f
    )
        return false;
    u32 index = 0u;
    for(; index < m_snapshot.m_glyphPages.size(); ++index){
        if(m_snapshot.m_glyphPages[index].get() == page.get())
            break;
    }
    if(index == m_snapshot.m_glyphPages.size()){
        if(!prepareGlyphPages(&page, 1u))
            return false;
        for(index = 0u; index < m_snapshot.m_glyphPages.size(); ++index){
            if(__hidden_ui_glyph_paint::SamePage(m_snapshot.m_glyphPages[index]->binding(), page->binding()))
                break;
        }
    }
    GLB_ASSERT(index < m_snapshot.m_glyphPages.size());
    emitQuad(rectangle, uv, tint, PaintMaterial::Glyph, index);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

