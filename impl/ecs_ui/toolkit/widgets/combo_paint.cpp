// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"
#include "../rect_math.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::paintCombo(const Item& item, const LayoutBox& box){
    if(item.combo >= m_scope->m_combos.size())
        return false;
    ComboFrame& frame = m_scope->m_combos[item.combo];
    if(!comboMatches(frame))
        return false;
    const InputRouter& input = m_context.input();
    const bool focused = input.focus() == item.state.id;
    const bool hovered = input.hover() == item.state.id;
    const Name& name = !item.enabled ? frame.style.disabled : frame.open ? frame.style.open
        : focused ? frame.style.focused : hovered ? frame.style.hover : frame.style.normal;
    const UiSkinRegion* field = region(name, frame.style.fallback);
    const UiSkinRegion* arrow = region(frame.style.arrow, frame.style.arrowFallback);
    if(!field || !arrow)
        return false;
    const Rect clip = visibleClip(box.clip);
    m_paint.pushClip(clip);
    const bool fieldPainted = m_paint.drawRegion(field->name, box.rectangle);
    const SIMDVector content = InsetRectValue(VectorSet(0.0f, 0.0f, box.rectangle.width, box.rectangle.height),
        VectorSet(item.padding.left, item.padding.top, item.padding.right, item.padding.bottom));
    const f32 contentHeight = VectorGetW(content);
    const f32 extent = Min(frame.arrowExtent, Min(contentHeight, VectorGetZ(content)));
    const Rect icon{ box.rectangle.x + box.rectangle.width - item.padding.right - extent,
        box.rectangle.y + item.padding.top + Max(0.0f, (contentHeight - extent) * 0.5f), extent, extent };
    const bool arrowPainted = fieldPainted && m_paint.drawRegion(arrow->name, icon,
        item.enabled ? Color{} : Color{ 1.0f, 1.0f, 1.0f, 0.5f });
    const SIMDVector textOrigin = VectorAdd(VectorSet(box.rectangle.x, box.rectangle.y, box.rectangle.x, box.rectangle.y),
        VectorSet(item.padding.left, item.padding.top, item.padding.left, item.padding.top));
    const Rect textClip{ VectorGetX(textOrigin), VectorGetY(textOrigin),
        Max(0.0f, icon.x - frame.style.arrowGap - box.rectangle.x - item.padding.left), contentHeight };
    m_paint.pushClip(textClip);
    const Point origin{ textClip.x, textClip.y + Max(0.0f, (contentHeight - item.text.measure().y) * 0.5f) };
    const bool textPainted = arrowPainted && m_text.paint(m_paint, item.text, origin,
        item.enabled ? item.style.text : item.style.disabledText);
    const bool textPopped = m_paint.popClip();
    const bool focusPainted = textPainted && (!focused || !item.enabled || !m_skin->findRegion(item.style.focus)
        || m_paint.drawRegion(item.style.focus, box.rectangle));
    const bool popped = m_paint.popClip();
    if(!focusPainted || !textPopped || !popped || !comboMatches(frame))
        return false;
    HitTarget target;
    target.rectangle = box.rectangle;
    target.clip = clip;
    target.enabled = item.enabled;
    target.focusable = item.enabled;
    target.activatable = item.enabled;
    target.contextMenu = item.contextMenu;
    target.navigable = item.enabled;
    target.control = frame.token;
    target.focusOnCommit = frame.focusOnCommit;
    if(!m_context.addTarget(item.state, target))
        return false;
    frame.state->m_bounds = box.rectangle;
    const SIMDVector visible = IntersectRectValue(VectorSet(box.rectangle.x, box.rectangle.y, box.rectangle.width, box.rectangle.height),
        VectorSet(clip.x, clip.y, clip.width, clip.height));
    frame.visibleField = { VectorGetX(visible), VectorGetY(visible), VectorGetZ(visible), VectorGetW(visible) };
    return true;
}

bool Builder::paintCombos(){
    for(auto& frame : m_scope->m_combos){
        if(!comboMatches(frame) || frame.list >= m_scope->m_lists.size())
            return false;
        if(frame.open && frame.options.enabled && !paintComboPopup(frame))
            return false;
    }
    return true;
}

bool Builder::paintComboPopup(ComboFrame& frame){
    const UiSkinRegion* background = region(frame.popupStyle.background, frame.popupStyle.fallback);
    if(!background || !comboMatches(frame))
        return false;
    const Rect& anchor = frame.state->bounds();
    if(frame.visibleField.width <= 0.0f || frame.visibleField.height <= 0.0f){
        m_context.discardPopupScope(frame.popupToken);
        ComboBehavior::Close(*frame.state);
        frame.open = false;
        if(frame.search){
            frame.search->m_editor.focused = false;
            frame.search->query().cancelComposition();
            snapshotComboQuery(frame);
        }
        frame.listToken.instanceGeneration = frame.state->m_list.inputGeneration();
        m_scope->m_lists[frame.list].token = frame.listToken;
        return true;
    }
    PopupOptions options;
    options.anchor = anchor;
    options.size = { anchor.width, frame.options.popupHeight };
    const auto placement = PopupLayout::Place(options, m_paint.displayMetrics());
    if(!placement)
        return false;
    const SIMDVector firstPadding = VectorSet(frame.popupStyle.padding.left, frame.popupStyle.padding.top,
        frame.popupStyle.padding.right, frame.popupStyle.padding.bottom);
    const SIMDVector secondPadding = VectorSet(background->padding.left, background->padding.top,
        background->padding.right, background->padding.bottom);
    const SIMDVector paddingValue = VectorSelect(secondPadding, firstPadding, VectorGreater(firstPadding, secondPadding));
    const Insets padding{ VectorGetX(paddingValue), VectorGetY(paddingValue), VectorGetZ(paddingValue), VectorGetW(paddingValue) };
    PopupScope scope;
    scope.token = frame.popupToken;
    scope.parent = frame.parentToken;
    scope.bounds = placement->bounds;
    scope.viewport = placement->viewport;
    scope.dismissFocusTraversal = true;
    scope.focusAnchor = frame.state->m_owner;
    scope.focusAnchorDeclarationGeneration = frame.state->m_ownerDeclaration;
    if(!m_context.updatePopupScope(frame.popupToken, scope) || !m_context.activatePopupScope(frame.popupToken))
        return false;
    if(!m_paint.beginOverlay(m_context.popupLayer())){
        const bool ended = m_context.endPopupScope(false);
        if(!ended)
            m_context.fail();
        return false;
    }
    m_paint.pushClip(placement->viewport);
    const bool backgroundPainted = m_paint.drawRegion(background->name, placement->bounds);
    const bool clipPopped = m_paint.popClip();
    HitTarget barrier;
    barrier.rectangle = placement->bounds;
    barrier.clip = placement->viewport;
    bool painted = backgroundPainted && clipPopped && m_context.addTarget(frame.popup, barrier);
    if(painted){
        Item listItem(m_arena);
        listItem.state = frame.rows;
        listItem.list = frame.list;
        LayoutBox box;
        const SIMDVector inset = InsetRectValue(
            VectorSet(placement->bounds.x, placement->bounds.y, placement->bounds.width, placement->bounds.height),
            VectorSet(padding.left, padding.top, padding.right, padding.bottom)
        );
        box.rectangle = { VectorGetX(inset), VectorGetY(inset), VectorGetZ(inset), VectorGetW(inset) };
        box.clip = placement->bounds;
        painted = (!frame.search || paintComboQuery(frame, box)) && paintList(listItem, box) && comboMatches(frame);
    }
    const bool overlayEnded = m_paint.endOverlay();
    const bool scopeEnded = m_context.endPopupScope(painted);
    if(painted)
        frame.state->m_popup.m_placement = *placement;
    return painted && overlayEnded && scopeEnded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

