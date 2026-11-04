// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 Builder::paintCombo(const Item& item, const LayoutBox& box){
    if(item.combo >= m_scope->m_combos.size())
        return false;
    ComboFrame& frame = m_scope->m_combos[item.combo];
    if(!comboMatches(frame))
        return false;
    const InputRouter& input = m_context.input();
    const b8 focused = input.focus() == item.state.id;
    const b8 hovered = input.hover() == item.state.id;
    const Name name = ResolveComboFieldRegionName(frame.style, item.enabled, frame.open, focused, hovered);
    const UiSkinRegion* field = region(name, frame.style.fallback);
    const UiSkinRegion* arrow = region(frame.style.arrow, frame.style.arrowFallback);
    if(!field || !arrow)
        return false;
    const Rect clip = visibleClip(box.clip);
    m_paint.pushClip(clip);
    const b8 fieldPainted = m_paint.drawRegion(field->name, box.rectangle);
    Rect icon;
    Rect textClip;
    ResolveComboFieldLayout(box.rectangle, item.padding, frame.arrowExtent, frame.style.arrowGap, icon, textClip);
    const b8 arrowPainted = fieldPainted && m_paint.drawRegion(arrow->name, icon, item.enabled ? Color{} : Color{ 1.0f, 1.0f, 1.0f, s_StyleDisabledIconAlpha });
    m_paint.pushClip(textClip);
    const Point origin = ResolveCenteredTextOrigin(textClip, 0.0f, item.textBounds);
    const b8 textPainted = arrowPainted && m_text.paint(m_paint, item.text, origin, item.enabled ? item.style.text : item.style.disabledText);
    const b8 textPopped = m_paint.popClip();
    const b8 focusPainted = textPainted && (!focused || !item.enabled || !m_skin->findRegion(item.style.focus) || m_paint.drawRegion(item.style.focus, box.rectangle));
    const b8 popped = m_paint.popClip();
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
    frame.visibleField = IntersectUiRectsBounds(box.rectangle, clip);
    return true;
}

b8 Builder::paintCombos(){
    for(auto& frame : m_scope->m_combos){
        if(!comboMatches(frame) || frame.list >= m_scope->m_lists.size())
            return false;
        if(frame.open && frame.options.enabled && !paintComboPopup(frame))
            return false;
    }
    return true;
}

b8 Builder::paintComboPopup(ComboFrame& frame){
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
    const auto placement = PlacePopup(options, m_paint.displayMetrics());
    if(!placement)
        return false;
    const Insets padding = MaxUiPadding(frame.popupStyle.padding, background->padding);
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
        const b8 ended = m_context.endPopupScope(false);
        if(!ended)
            m_context.fail();
        return false;
    }
    m_paint.pushClip(placement->viewport);
    const b8 backgroundPainted = m_paint.drawRegion(background->name, placement->bounds);
    const b8 clipPopped = m_paint.popClip();
    HitTarget barrier;
    barrier.rectangle = placement->bounds;
    barrier.clip = placement->viewport;
    b8 painted = backgroundPainted && clipPopped && m_context.addTarget(frame.popup, barrier);
    if(painted){
        Item listItem(m_arena);
        listItem.state = frame.rows;
        listItem.list = frame.list;
        LayoutBox box;
        box.rectangle = InsetUiRectBounds(placement->bounds, padding);
        box.clip = placement->bounds;
        painted = (!frame.search || paintComboQuery(frame, box)) && paintList(listItem, box) && comboMatches(frame);
    }
    const b8 overlayEnded = m_paint.endOverlay();
    const b8 scopeEnded = m_context.endPopupScope(painted);
    if(painted)
        frame.state->m_popup.m_placement = *placement;
    return painted && overlayEnded && scopeEnded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

