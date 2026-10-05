// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintImage(const Item& item, const LayoutBox& box){
    if(item.image >= m_scope->m_images.size())
        return false;
    const ImageFrame& frame = m_scope->m_images[item.image];
    ImagePlacement placement;
    if(!ImageLayout::place(box.rectangle, visibleClip(box.clip), placement))
        return false;
    m_paint.pushClip(placement.clip);
    const bool painted = frame.source
        ? m_paint.drawImage(frame.source, placement.bounds, { 0.0f, 0.0f, 1.0f, 1.0f }, frame.options.tint)
        : m_paint.drawRegion(frame.region, placement.bounds, frame.options.tint);
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

