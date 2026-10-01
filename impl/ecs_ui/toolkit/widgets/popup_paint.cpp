// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintPopup(){
    const LayoutBox* panel = m_scope->m_layout.box(0u);
    const UiSkinRegion* background = region(m_scope->m_popupPaintStyle.background, m_scope->m_popupPaintStyle.fallback);
    if(!panel || !background)
        return false;
    if(m_scope->m_popupOptions.modal){
        const Color& color = m_scope->m_popupPaintStyle.backdrop;
        if(!IsFinite(color.r) || !IsFinite(color.g) || !IsFinite(color.b) || !IsFinite(color.a) || color.a < 0.0f || color.a > 1.0f)
            return false;
        m_paint.fillRect(m_scope->m_popupPlacement.viewport, color);
    }
    m_paint.pushClip(m_scope->m_popupPlacement.viewport);
    const bool painted = m_paint.drawRegion(background->name, panel->rectangle);
    const bool popped = m_paint.popClip();
    HitTarget barrier;
    barrier.rectangle = panel->rectangle;
    barrier.clip = m_scope->m_popupPlacement.viewport;
    if(!painted || !popped || !m_context.addTarget(m_scope->m_panelState, barrier))
        return false;
    return paintItems();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

