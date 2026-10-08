// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


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
    const Rect clip = visibleClip(box.clip);
    const f32 caretWidth = 1.0f / m_paint.displayMetrics().pixelScaleX;
    const auto viewport = ScrollbarLayout::Calculate(
        box.rectangle, clip, item.padding, item.editView.layout().measure(), caretWidth,
        state.scroll(), item.scrollbarStyle.thickness, item.scrollbarStyle.minimumThumb
    );
    if(!viewport)
        return false;
    const f32 lineHeight = item.editView.layout().lines().empty() ? item.editView.layout().fontSize() : item.editView.layout().lines().front().height;
    const Point step{ item.editView.layout().fontSize() * frame.wheelLines, lineHeight * frame.wheelLines };
    if(!IsFinite(step.x) || !IsFinite(step.y) || step.x <= 0.0f || step.y <= 0.0f)
        return false;
    const ControlToken token = state.m_scrollInput.prepare(
        item.state, m_context.popupToken(), state.instanceGeneration(), state.revision(), *frame.model,
        item.editOptions.enabled, item.editOptions.readOnly, *viewport, step
    );
    m_context.input().fenceControl(item.state.id, item.state.declarationGeneration, token);
    if(!applyTextAreaScrollInput(item, frame, token))
        return false;
    const auto arranged = item.editView.arrangeViewport(box.rectangle, viewport->viewport, clip, state.scroll(), caretWidth, state.m_revealCaret);
    if(!arranged)
        return false;
    const EditBoxPlacement& placement = *arranged;
    if(!state.m_scrollInput.updateOffsets({ placement.scroll, placement.scrollY }))
        return false;
    if(!item.editView.paint(m_text, m_paint, *m_skin, placement, item.editStyle, item.editFlags) || !textAreaMatches(frame))
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
    target.control = token;
    target.scrollable = item.editOptions.enabled;
    target.scrollStep = step.y;
    target.scrollStepX = step.x;
    target.gestureMaximum = state.scrollbars().vertical.maximum;
    target.gestureMaximumX = state.scrollbars().horizontal.maximum;
    if(!m_context.addTarget(item.state, target) || !paintTextAreaScrollbars(item, state.scrollbars(), clip, token))
        return false;
    const bool published = !m_editHost || m_editHost->publish(item.state, item.editView, placement, item.editOptions);
    if(!published || !textAreaMatches(frame))
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

