// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintPanel(){
    const LayoutBox* panel = m_scope->m_layout.box(0u);
    if(!panel)
        return false;
    m_paint.pushClip(visibleClip(panel->clip));
    const bool painted = m_paint.drawRegion(m_scope->m_panelStyle.panel, panel->rectangle);
    const bool popped = m_paint.popClip();
    HitTarget barrier;
    barrier.rectangle = panel->rectangle;
    barrier.clip = visibleClip(panel->clip);
    if(!painted || !popped || !m_context.addTarget(m_scope->m_panelState, barrier))
        return false;
    return paintItems();
}

bool Builder::paintItems(){
    for(const auto& item : m_scope->m_items){
        const LayoutBox* box = m_scope->m_layout.box(item.node);
        if(!popupAncestorsVisible() || !box || !paintItem(item, *box))
            return false;
    }
    return true;
}

bool Builder::paintItem(const Item& item, const LayoutBox& box){
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
    const bool hover = input.hover() == item.state.id;
    const bool captured = input.capture() == item.state.id && input.primaryDown();
    const bool focused = input.focus() == item.state.id;
    const bool interactive = item.state.kind == WidgetKind::Button || item.state.kind == WidgetKind::Checkbox;
    m_paint.pushClip(visibleClip(box.clip));
    bool painted = true;
    Point origin{ box.rectangle.x, box.rectangle.y };
    const Point measured = item.text.measure();
    if(item.state.kind == WidgetKind::Separator){
        painted = m_paint.drawRegion(item.style.separator, box.rectangle);
        const bool popped = m_paint.popClip();
        return painted && popped;
    }
    if(item.state.kind == WidgetKind::Button){
        const Name& preferred = !item.enabled ? item.style.buttonDisabled
            : captured && hover ? item.style.buttonPressed : hover ? item.style.buttonHover : item.style.button;
        const UiSkinRegion* skinRegion = region(preferred, item.style.button);
        painted = skinRegion && m_paint.drawRegion(skinRegion->name, box.rectangle);
        const f32 contentWidth = Max(0.0f, box.rectangle.width - item.padding.left - item.padding.right);
        const f32 contentHeight = Max(0.0f, box.rectangle.height - item.padding.top - item.padding.bottom);
        origin.x += item.padding.left + Max(0.0f, (contentWidth - measured.x) * 0.5f);
        origin.y += item.padding.top + Max(0.0f, (contentHeight - measured.y) * 0.5f);
    }
    else if(item.state.kind == WidgetKind::Checkbox){
        const Name& preferred = !item.enabled ? item.style.checkboxDisabled
            : item.checked ? item.style.checkboxChecked : hover ? item.style.checkboxHover : item.style.checkbox;
        const UiSkinRegion* skinRegion = region(preferred, item.style.checkbox);
        const Rect square{ origin.x, origin.y + Max(0.0f, (box.rectangle.height - item.checkboxExtent) * 0.5f),
            item.checkboxExtent, item.checkboxExtent };
        painted = skinRegion && m_paint.drawRegion(skinRegion->name, square);
        if(painted && item.checked && m_skin->findRegion(item.style.checkboxMark)){
            const f32 inset = item.checkboxExtent * 0.2f;
            const Color tint = item.enabled ? Color{} : Color{ 1.0f, 1.0f, 1.0f, 0.5f };
            painted = m_paint.drawRegion(item.style.checkboxMark,
                { square.x + inset, square.y + inset, square.width - 2.0f * inset, square.height - 2.0f * inset }, tint
            );
        }
        origin.x += item.checkboxExtent + item.style.gap;
        origin.y += Max(0.0f, (box.rectangle.height - measured.y) * 0.5f);
    }
    if(painted && focused && item.enabled && interactive && m_skin->findRegion(item.style.focus))
        painted = m_paint.drawRegion(item.style.focus, box.rectangle);
    if(painted)
        painted = m_text.paint(m_paint, item.text, origin, item.enabled ? item.style.text : item.style.disabledText);
    const bool popped = m_paint.popClip();
    if(!painted || !popped)
        return false;
    if(interactive || item.annotated){
        HitTarget target;
        target.rectangle = box.rectangle;
        target.clip = visibleClip(box.clip);
        // Disabled controls still cover the panel; the router consumes the panel barrier beneath them.
        target.enabled = item.enabled;
        target.focusable = item.enabled && interactive;
        target.activatable = item.enabled && interactive;
        target.contextMenu = item.contextMenu;
        return m_context.addTarget(item.state, target);
    }
    return true;
}

Rect Builder::visibleClip(const Rect& clip)const{
    const DisplayMetrics& display = m_paint.displayMetrics();
    const f32 left = Max(0.0f, clip.x);
    const f32 top = Max(0.0f, clip.y);
    const f32 right = Min(display.logicalWidth, clip.x + clip.width);
    const f32 bottom = Min(display.logicalHeight, clip.y + clip.height);
    return { left, top, Max(0.0f, right - left), Max(0.0f, bottom - top) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

