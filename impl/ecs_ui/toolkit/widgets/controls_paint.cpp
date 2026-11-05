// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::paintPanel(){
    const LayoutBox* panel = m_scope->m_layout.box(0u);
    if(!panel)
        return false;
    m_paint.pushClip(visibleClip(panel->clip));
    const b8 painted = m_paint.drawRegion(m_scope->m_panelStyle.panel, panel->rectangle);
    const b8 popped = m_paint.popClip();
    HitTarget barrier;
    barrier.rectangle = panel->rectangle;
    barrier.clip = visibleClip(panel->clip);
    if(!painted || !popped || !m_context.addTarget(m_scope->m_panelState, barrier))
        return false;
    return paintItems();
}

b8 Builder::paintItems(){
    for(const auto& item : m_scope->m_items){
        const LayoutBox* box = m_scope->m_layout.box(item.node);
        if(!popupAncestorsVisible() || !box || !paintItem(item, *box))
            return false;
    }
    return true;
}

b8 Builder::paintItem(const Item& item, const LayoutBox& box){
    if(item.state.kind == WidgetKind::ComboBox || item.state.kind == WidgetKind::SearchComboBox)
        return paintCombo(item, box);
    if(item.state.kind == WidgetKind::TextArea)
        return paintTextArea(item, box);
    if(item.state.kind == WidgetKind::EditBox)
        return paintEditBox(item, box);
    if(item.state.kind == WidgetKind::Selectable)
        return paintSelectable(item, box);
    if(item.state.kind == WidgetKind::VirtualList)
        return paintList(item, box);
    if(item.state.kind == WidgetKind::RadioGroup)
        return paintRadioGroup(item, box);
    if(item.state.kind == WidgetKind::Slider)
        return paintSlider(item, box);
    if(item.state.kind == WidgetKind::Progress)
        return paintProgress(item, box);
    if(item.state.kind == WidgetKind::Image)
        return paintImage(item, box);
    const InputRouter& input = m_context.input();
    const b8 hover = input.hover() == item.state.id;
    const b8 pressed = input.pressed(item.state.id);
    const b8 focused = input.focus() == item.state.id;
    const b8 interactive = item.state.kind == WidgetKind::Button || item.state.kind == WidgetKind::Checkbox;
    const Rect clip = visibleClip(box.clip);
    if(!PaintBasicControlItemVisual(
        m_paint,
        m_text,
        *m_skin,
        item.state.kind,
        item.style,
        item.text,
        item.textBounds,
        item.padding,
        item.checkboxExtent,
        box.rectangle,
        clip,
        item.enabled,
        item.checked,
        hover,
        pressed,
        focused,
        interactive
    ))
        return false;
    if(item.state.kind == WidgetKind::Separator)
        return true;
    if(interactive || item.annotated){
        HitTarget target;
        target.rectangle = box.rectangle;
        target.clip = clip;
        // Disabled controls still cover the panel; the router consumes the panel barrier beneath them.
        target.enabled = item.enabled;
        target.focusable = item.enabled && interactive;
        target.activatable = item.enabled && interactive;
        target.contextMenu = item.contextMenu;
        return m_context.addTarget(item.state, target);
    }
    return true;
}

b8 Builder::paintSelectable(const Item& item, const LayoutBox& box){
    const InputRouter& input = m_context.input();
    const SelectablePaintFlags flags{ item.enabled, item.checked, input.hover() == item.state.id, input.focus() == item.state.id };
    const Rect clip = visibleClip(box.clip);
    if(!PaintSelectable(m_paint, m_text, *m_skin, item.text, item.textBounds, item.padding, box.rectangle, clip, item.selectableStyle, flags, item.style.text, item.style.disabledText))
        return false;
    HitTarget target;
    target.rectangle = box.rectangle;
    target.clip = clip;
    target.enabled = item.enabled;
    target.focusable = item.enabled;
    target.activatable = item.enabled;
    target.contextMenu = item.contextMenu;
    return m_context.addTarget(item.state, target);
}

b8 Builder::finishClipPaint(const Item& item, const Rect& bounds, const Rect& clip, b8 painted){
    const b8 popped = m_paint.popClip();
    if(!painted || !popped)
        return false;
    if(!item.annotated)
        return true;
    HitTarget target;
    target.rectangle = bounds;
    target.clip = clip;
    target.focusable = false;
    target.activatable = false;
    target.contextMenu = item.contextMenu;
    return m_context.addTarget(item.state, target);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

