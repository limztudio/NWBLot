// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"
#include "../rect_math.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::finishClipPaint(const Item& item, const Rect& bounds, const Rect& clip, bool painted){
    const bool popped = m_paint.popClip();
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
        const SIMDVector content = InsetRectValue(VectorSet(0.0f, 0.0f, box.rectangle.width, box.rectangle.height),
            VectorSet(item.padding.left, item.padding.top, item.padding.right, item.padding.bottom));
        const SIMDVector remaining = VectorScale(VectorSubtract(VectorSwizzle<2, 3, 2, 3>(content),
            VectorSet(measured.x, measured.y, 0.0f, 0.0f)), 0.5f);
        const SIMDVector centered = VectorSelect(remaining, VectorZero(), VectorGreater(VectorZero(), remaining));
        const SIMDVector offset = VectorAdd(VectorSet(item.padding.left, item.padding.top, 0.0f, 0.0f), centered);
        const SIMDVector textOrigin = VectorAdd(VectorSet(origin.x, origin.y, 0.0f, 0.0f), offset);
        origin = { VectorGetX(textOrigin), VectorGetY(textOrigin) };
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
            const SIMDVector markOrigin = VectorAdd(VectorSet(square.x, square.y, 0.0f, 0.0f), VectorReplicate(inset));
            const SIMDVector markSize = VectorNegativeMultiplySubtractExpression(VectorReplicate(2.0f), VectorReplicate(inset),
                VectorSet(square.width, square.height, square.width, square.height));
            const SIMDVector mark = VectorPermute<0, 1, 4, 5>(markOrigin, markSize);
            painted = m_paint.drawRegion(item.style.checkboxMark,
                { VectorGetX(mark), VectorGetY(mark), VectorGetZ(mark), VectorGetW(mark) }, tint
            );
        }
        const f32 horizontalOffset = item.checkboxExtent + item.style.gap;
        const f32 verticalOffset = Max(0.0f, (box.rectangle.height - measured.y) * 0.5f);
        const SIMDVector textOrigin = VectorAdd(VectorSet(origin.x, origin.y, origin.x, origin.y),
            VectorSet(horizontalOffset, verticalOffset, horizontalOffset, verticalOffset));
        origin = { VectorGetX(textOrigin), VectorGetY(textOrigin) };
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

Rect Builder::visibleClip(const Rect& clip)const noexcept{
    const DisplayMetrics& display = m_paint.displayMetrics();
    const SIMDVector intersection = IntersectRectValue(VectorSet(0.0f, 0.0f, display.logicalWidth, display.logicalHeight),
        VectorSet(clip.x, clip.y, clip.width, clip.height));
    return { VectorGetX(intersection), VectorGetY(intersection), VectorGetZ(intersection), VectorGetW(intersection) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

