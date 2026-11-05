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
    const Rect clip = visibleClip(box.clip);
    if(!PaintComboFieldVisual(
        m_paint,
        m_text,
        *m_skin,
        frame.style,
        item.style,
        item.text,
        item.textBounds,
        item.padding,
        frame.arrowExtent,
        box.rectangle,
        clip,
        item.enabled,
        frame.open,
        focused,
        hovered
    ) || !comboMatches(frame))
        return false;
    const HitTarget target = MakeComboFieldHitTarget(box.rectangle, clip, item.enabled, item.contextMenu, frame.token, frame.focusOnCommit);
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
    const PopupScope scope = MakeComboPopupScope(
        frame.popupToken,
        frame.parentToken,
        *placement,
        frame.state->m_owner,
        frame.state->m_ownerDeclaration
    );
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
    const HitTarget barrier = MakeBarrierHitTarget(placement->bounds, placement->viewport);
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

