// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 PaintBuilder::drawSdfGlyph(const SharedSdfAtlasPage& page, const u32 channel, const Rect& rectangle, const Rect& uv, const Color& tint){
    NWB_ASSERT(m_recording);
    if(!page || channel >= page->binding().channelCount || !IsValidUvRect(uv))
        return false;
    u32 index = 0u;
    for(; index < m_snapshot.m_sdfPages.size(); ++index){
        if(m_snapshot.m_sdfPages[index].get() == page.get())
            break;
    }
    if(index == m_snapshot.m_sdfPages.size()){
        if(!prepareSdfPages(Span<const SharedSdfAtlasPage>(&page, 1u)))
            return false;
        for(index = 0u; index < m_snapshot.m_sdfPages.size(); ++index){
            if(m_snapshot.m_sdfPages[index]->binding() == page->binding())
                break;
        }
    }
    NWB_ASSERT(index < m_snapshot.m_sdfPages.size());
    emitQuad(rectangle, uv, tint, PaintMaterial::SdfGlyph, Limit<u32>::s_Max, index, channel);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

