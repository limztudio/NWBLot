// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"
#include "../rect_math.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_controls_paint{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_CheckmarkInsetFraction = 0.2f;
// The mark loses its inset on both opposite sides.
inline constexpr f32 s_CheckmarkInsetSides = 2.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintPanel(){
    const LayoutBox* panel = m_scope->m_layout.box(0u);
    if(!panel)
        return false;
    m_paint.pushClip(StoreRectangle(loadVisibleClip(LoadRectangle(panel->clip))));
    const bool painted = m_paint.drawRegion(m_scope->m_panelStyle.panel, panel->rectangle);
    const bool popped = m_paint.popClip();
    HitTarget barrier;
    barrier.rectangle = panel->rectangle;
    barrier.clip = StoreRectangle(loadVisibleClip(LoadRectangle(panel->clip)));
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
    const bool pressed = input.pressed(item.state.id);
    const bool focused = input.focus() == item.state.id;
    const bool interactive = item.state.kind == WidgetKind::Button || item.state.kind == WidgetKind::Checkbox;
    m_paint.pushClip(StoreRectangle(loadVisibleClip(LoadRectangle(box.clip))));
    bool painted = true;
    Point origin{ box.rectangle.x, box.rectangle.y };
    Rect textClip = box.rectangle;
    const Point measured{ item.textBounds.width, item.textBounds.height };
    if(item.state.kind == WidgetKind::Separator){
        painted = m_paint.drawRegion(item.style.separator, box.rectangle);
        const bool popped = m_paint.popClip();
        return painted && popped;
    }
    if(item.state.kind == WidgetKind::Button){
        const Name& preferred = !item.enabled ? item.style.buttonDisabled : pressed ? item.style.buttonPressed : hover ? item.style.buttonHover : item.style.button;
        const UiSkinRegion* skinRegion = region(preferred, item.style.button);
        painted = skinRegion && m_paint.drawRegion(skinRegion->name, box.rectangle);
        const SIMDVectorSingle content = InsetRectValue(VectorSet(box.rectangle.x, box.rectangle.y, box.rectangle.width, box.rectangle.height), VectorSet(item.padding.left, item.padding.top, item.padding.right, item.padding.bottom));
        textClip = { VectorGetX(content), VectorGetY(content), VectorGetZ(content), VectorGetW(content) };
        const SIMDVectorSingle remaining = VectorScale(VectorSubtract(VectorSwizzle<2, 3, 2, 3>(content), VectorSet(measured.x, measured.y, 0.0f, 0.0f)), 0.5f);
        const SIMDVectorSingle centered = VectorSelect(remaining, VectorZero(), VectorGreater(VectorZero(), remaining));
        const SIMDVectorSingle textOrigin = VectorAdd(content, centered);
        origin = { VectorGetX(textOrigin), VectorGetY(textOrigin) };
    }
    else if(item.state.kind == WidgetKind::Checkbox){
        const Name& preferred = !item.enabled ? item.style.checkboxDisabled : item.checked ? item.style.checkboxChecked : hover ? item.style.checkboxHover : item.style.checkbox;
        const UiSkinRegion* skinRegion = region(preferred, item.style.checkbox);
        const SIMDVectorSingle centered = VectorMax(VectorZero(), VectorScale(VectorSubtract(VectorReplicate(box.rectangle.height), VectorSet(item.checkboxExtent, measured.y, 0.0f, 0.0f)), s_RectCenterFraction));
        const SIMDVectorSingle squareOrigin = VectorAdd(VectorSet(origin.x, origin.y, 0.0f, 0.0f), VectorSet(0.0f, VectorGetX(centered), 0.0f, 0.0f));
        const Rect square{ VectorGetX(squareOrigin), VectorGetY(squareOrigin), item.checkboxExtent, item.checkboxExtent };
        painted = skinRegion && m_paint.drawRegion(skinRegion->name, square);
        if(painted && item.checked && m_skin->findRegion(item.style.checkboxMark)){
            const f32 inset = VectorGetX(VectorScale(VectorReplicate(item.checkboxExtent), __hidden_ui_controls_paint::s_CheckmarkInsetFraction));
            const Color tint = item.enabled ? Color{} : Color{ 1.0f, 1.0f, 1.0f, s_StyleDisabledIconAlpha };
            const SIMDVectorSingle markOrigin = VectorAdd(VectorSet(square.x, square.y, 0.0f, 0.0f), VectorReplicate(inset));
            const SIMDVectorSingle markSize = VectorNegativeMultiplySubtractExpression(VectorReplicate(__hidden_ui_controls_paint::s_CheckmarkInsetSides), VectorReplicate(inset), VectorSet(square.width, square.height, square.width, square.height));
            const SIMDVectorSingle mark = VectorPermute<0, 1, 4, 5>(markOrigin, markSize);
            painted = m_paint.drawRegion(item.style.checkboxMark, { VectorGetX(mark), VectorGetY(mark), VectorGetZ(mark), VectorGetW(mark) }, tint);
        }
        const SIMDVectorSingle textOffset = VectorAdd(VectorSet(item.checkboxExtent, VectorGetY(centered), 0.0f, 0.0f), VectorSet(item.style.gap, 0.0f, 0.0f, 0.0f));
        const SIMDVectorSingle textOrigin = VectorAdd(VectorSet(origin.x, origin.y, 0.0f, 0.0f), textOffset);
        origin = { VectorGetX(textOrigin), VectorGetY(textOrigin) };
        const SIMDVectorSingle clipSize = VectorMax(VectorZero(), VectorSubtract(VectorSet(box.rectangle.width, box.rectangle.height, 0.0f, 0.0f), VectorSet(VectorGetX(textOffset), 0.0f, 0.0f, 0.0f)));
        textClip = { origin.x, box.rectangle.y, VectorGetX(clipSize), box.rectangle.height };
    }
    if(painted && focused && item.enabled && interactive && m_skin->findRegion(item.style.focus))
        painted = m_paint.drawRegion(item.style.focus, box.rectangle);
    const SIMDVectorSingle alignedOrigin = VectorSubtract(VectorSet(origin.x, origin.y, 0.0f, 0.0f), VectorSet(item.textBounds.x, item.textBounds.y, 0.0f, 0.0f));
    origin = { VectorGetX(alignedOrigin), VectorGetY(alignedOrigin) };
    m_paint.pushClip(textClip);
    if(painted)
        painted = m_text.paint(m_paint, item.text, origin, item.enabled ? item.style.text : item.style.disabledText);
    const bool textPopped = m_paint.popClip();
    const bool popped = m_paint.popClip();
    if(!painted || !textPopped || !popped)
        return false;
    if(interactive || item.annotated){
        HitTarget target;
        target.rectangle = box.rectangle;
        target.clip = StoreRectangle(loadVisibleClip(LoadRectangle(box.clip)));
        // Disabled controls still cover the panel; the router consumes the panel barrier beneath them.
        target.enabled = item.enabled;
        target.focusable = item.enabled && interactive;
        target.activatable = item.enabled && interactive;
        target.contextMenu = item.contextMenu;
        return m_context.addTarget(item.state, target);
    }
    return true;
}

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

SIMDVectorSingle Builder::loadVisibleClip(const SIMDVectorSingle clip)const noexcept{
    const DisplayMetrics& display = m_paint.displayMetrics();
    return IntersectRectValue(VectorSet(0.0f, 0.0f, display.logicalWidth, display.logicalHeight), clip);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

