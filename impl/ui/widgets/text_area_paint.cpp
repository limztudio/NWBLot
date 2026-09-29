// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintTextArea(const Item& item, const LayoutBox& box){
    if(item.textArea >= m_scope->m_textAreas.size())
        return false;
    const TextAreaFrame& frame = m_scope->m_textAreas[item.textArea];
    if(!textAreaMatches(frame))
        return false;
    TextAreaState& state = *frame.state;
    EditBoxPlacement placement;
    const f32 caretWidth = 1.0f / m_paint.displayMetrics().pixelScaleX;
    if(!item.editView.arrange(box.rectangle, item.padding, visibleClip(box.clip), state.scroll(), placement, caretWidth, state.m_revealCaret))
        return false;
    if(!item.editView.paint(m_text, m_paint, *m_skin, placement, m_editStyle, item.editFlags) || !textAreaMatches(frame))
        return false;
    state.m_visual.placement = placement;
    state.m_visual.scroll = placement.scroll;
    state.m_scrollY = placement.scrollY;
    HitTarget target;
    target.rectangle = box.rectangle;
    target.clip = visibleClip(box.clip);
    target.enabled = item.editOptions.enabled;
    target.focusable = item.editOptions.enabled;
    target.textEditable = item.editOptions.enabled;
    target.contextMenu = item.contextMenu;
    if(!m_context.addTarget(item.state, target))
        return false;
    const bool published = !m_editHost || m_editHost->publish(item.state, item.editView, placement, item.editOptions);
    if(!published || !textAreaMatches(frame))
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

