// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintProgress(const Item& item, const LayoutBox& box){
    if(item.progress >= m_scope->m_progress.size())
        return false;
    const ProgressFrame& frame = m_scope->m_progress[item.progress];
    ProgressPlacement placement;
    if(!ProgressLayout::place(box.rectangle, visibleClip(box.clip), frame.metrics, frame.fraction, placement))
        return false;
    m_paint.pushClip(placement.clip);
    bool painted = m_paint.drawRegion(frame.style.track, placement.bounds, frame.style.trackTint);
    if(painted && placement.fillReveal.width > 0.0f && placement.fillReveal.height > 0.0f){
        m_paint.pushClip(placement.fillReveal);
        painted = m_paint.drawRegion(frame.style.fill, placement.fillCanvas, frame.style.fillTint);
        const bool popped = m_paint.popClip();
        painted = painted && popped;
    }
    const bool popped = m_paint.popClip();
    if(!painted || !popped)
        return false;
    if(!item.annotated)
        return true;
    HitTarget target;
    target.rectangle = placement.bounds;
    target.clip = placement.clip;
    target.focusable = false;
    target.activatable = false;
    target.contextMenu = item.contextMenu;
    return m_context.addTarget(item.state, target);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

