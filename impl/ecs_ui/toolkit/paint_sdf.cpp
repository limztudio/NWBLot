// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool PaintBuilder::prepareSdfPages(const SharedSdfAtlasPage* const pages, const usize count){
    return prepareImages(nullptr, 0u, pages, count);
}

bool PaintBuilder::drawSdfGlyph(
    const SharedSdfAtlasPage& page,
    const u32 channel,
    const Rect& rectangle,
    const Rect& uv,
    const Color& tint){
    NWB_ASSERT(m_recording);
    if(
        !page || channel >= 4u || !IsFinite(uv.x) || !IsFinite(uv.y) || !IsFinite(uv.width) || !IsFinite(uv.height)
        || uv.x < 0.0f || uv.y < 0.0f || uv.width <= 0.0f || uv.height <= 0.0f
        || uv.x + uv.width > 1.0f || uv.y + uv.height > 1.0f
    )
        return false;
    if(!prepareSdfPages(&page, 1u))
        return false;
    u32 index = 0u;
    for(; index < m_snapshot.m_sdfPages.size(); ++index){
        if(m_snapshot.m_sdfPages[index]->binding() == page->binding())
            break;
    }
    NWB_ASSERT(index < m_snapshot.m_sdfPages.size());
    emitQuad(rectangle, uv, tint, PaintMaterial::SdfGlyph, Limit<u32>::s_Max, index, channel);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

