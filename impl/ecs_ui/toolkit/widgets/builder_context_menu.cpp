// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ContextMenuResult Builder::contextMenu(const AStringView stableKey, const AStringView anchorKey, const IListDataSource& source, ContextMenuState& state, const ContextMenuOptions& options){
    ContextMenuResult result;
    Item* anchor = annotationAnchor(anchorKey);
    if(
        panelDeclarationBlocked()
        || !anchor || !IsFinite(options.size.x) || !IsFinite(options.size.y) || options.size.x <= 0.0f || options.size.y <= 0.0f
        || !IsFinite(options.wheelRows) || options.wheelRows <= 0.0f
        || m_scope->m_contextMenus.size() >= s_InputMaxPopups
    ){
        m_context.fail();
        return result;
    }
    if(!synchronizePopup()){
        result.valid = true;
        return result;
    }
    for(const auto& previous : m_scope->m_contextMenus){
        if(previous.anchor.id == anchor->state.id){
            m_context.fail();
            return result;
        }
    }
    m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_declaring = false; });
    const auto widget = m_context.declare(stableKey, WidgetKind::ContextMenu);
    if(!widget || !m_context.claimState(**widget, state.instanceGeneration()))
        return result;
    ContextMenuFrame frame;
    frame.parentToken = m_context.popupToken();
    frame.widget = **widget;
    frame.anchor = anchor->state;
    frame.anchorIndex = static_cast<u32>(anchor - m_scope->m_items.data());
    frame.source = &source;
    frame.state = &state;
    frame.popupStyle = m_popupStyle;
    frame.options = options;
    frame.options.enabled = options.enabled && anchor->enabled;
    const b8 previouslyOpen = state.isOpen();
    if(state.m_owner != (*widget)->id || state.m_ownerDeclaration != (*widget)->declarationGeneration || state.m_anchorWidget != anchor->state.id || state.m_anchorDeclaration != anchor->state.declarationGeneration){
        if(state.m_owner.valid())
            state.close();
        state.m_owner = (*widget)->id;
        state.m_ownerDeclaration = (*widget)->declarationGeneration;
        state.m_anchorWidget = anchor->state.id;
        state.m_anchorDeclaration = anchor->state.declarationGeneration;
        state.advanceRevision();
    }
    const b8 boundOpen = state.isOpen();
    state.m_popup.bindParent(frame.parentToken);
    if(boundOpen && !state.isOpen())
        state.close();
    const auto popup = m_context.declarePart(frame.widget, s_WidgetPartPopup, WidgetKind::Popup);
    if(!popup)
        return result;
    frame.popup = **popup;
    const auto rows = m_context.declarePart(frame.widget, s_WidgetPartRows, WidgetKind::VirtualList);
    if(!rows)
        return result;
    frame.rows = **rows;
    const b8 previouslyListFocused = m_context.input().focus() == frame.rows.id;
    frame.popupToken = { frame.popup.id, frame.popup.declarationGeneration, state.instanceGeneration(), state.m_popup.openGeneration() };
    frame.revision = state.revision();
    frame.open = state.isOpen();
    frame.listToken = { state.m_list.inputGeneration(), source.instanceGeneration(), source.revision() };
    frame.rowCount = source.rowCount();
    if(!contextMenuMatches(frame)){
        m_context.fail();
        return result;
    }
    const u64 previousGeneration = state.m_list.m_sourceGeneration;
    const u64 previousRevision = state.m_list.m_sourceRevision;
    if(!ListBehavior::Reconcile(state.m_list, source) || !contextMenuMatches(frame)){
        m_context.fail();
        return result;
    }
    if(previousGeneration != 0u && previousGeneration != frame.listToken.contentGeneration)
        state.close();
    else if(previousRevision != 0u && previousRevision != frame.listToken.contentRevision){
        state.m_list.select(0u);
        if(!state.m_list.scrollTo(0.0)){
            m_context.fail();
            return result;
        }
    }
    snapshotContextMenu(frame);
    if(!prepareContextMenu(frame, result)){
        m_context.fail();
        return result;
    }
    ListFrame list(m_arena);
    list.source = &source;
    list.state = &state.m_list;
    list.widgetStyle = m_style;
    list.style = m_listStyle;
    list.token = frame.listToken;
    list.rowCount = frame.rowCount;
    list.options.wheelRows = options.wheelRows;
    list.options.selectOnNavigate = false;
    list.options.enabled = frame.options.enabled;
    list.focusOnCommit = frame.open && frame.options.enabled && previouslyListFocused && m_context.input().focus() != frame.rows.id;
    list.padding = list.style.padding;
    const UiSkinRegion* background = region(list.style.background, list.style.backgroundFallback);
    if(!background || !contextMenuMatches(frame)){
        m_context.fail();
        return result;
    }
    list.padding = MaxUiPadding(list.padding, background->padding);
    if(!prepareListMetrics(list) || !contextMenuMatches(frame)){
        m_context.fail();
        return result;
    }
    frame.list = static_cast<u32>(m_scope->m_lists.size());
    state.m_popup.bindParent(frame.parentToken);
    if(frame.open && !reserveCompoundPopup(frame.popup, frame.popupToken)){
        m_context.fail();
        return {};
    }
    m_scope->m_lists.push_back(Move(list));
    anchor->contextMenu = frame.options.enabled;
    anchor->annotated = true;
    result.valid = true;
    result.opened = !previouslyOpen && state.isOpen();
    result.closed = previouslyOpen && !state.isOpen();
    m_scope->m_contextMenus.push_back(frame);
    return result;
}

b8 Builder::contextMenuStateMatches(const ContextMenuFrame& frame)const noexcept{
    return
        popupAncestorsVisible() && frame.state && frame.state->revision() == frame.revision && frame.state->isOpen() == frame.open && frame.state->m_popup.instanceGeneration() == frame.popupToken.instanceGeneration
        && frame.state->m_popup.openGeneration() == frame.popupToken.openGeneration && frame.state->m_popup.m_parent == frame.parentToken && frame.state->m_list.inputGeneration() == frame.listToken.instanceGeneration
        && frame.state->m_owner == frame.widget.id && frame.state->m_ownerDeclaration == frame.widget.declarationGeneration && frame.state->m_anchorWidget == frame.anchor.id && frame.state->m_anchorDeclaration == frame.anchor.declarationGeneration
    ;
}

b8 Builder::contextMenuMatches(const ContextMenuFrame& frame)const{
    return
        frame.source && contextMenuStateMatches(frame)
        && frame.listToken.contentGeneration != 0u && frame.listToken.contentRevision != 0u && frame.source->instanceGeneration() == frame.listToken.contentGeneration && contextMenuStateMatches(frame)
        && frame.source->revision() == frame.listToken.contentRevision && contextMenuStateMatches(frame)
        && frame.source->rowCount() == frame.rowCount && contextMenuStateMatches(frame)
    ;
}

b8 Builder::prepareContextMenu(ContextMenuFrame& frame, ContextMenuResult& result){
    InputRouter& input = m_context.input();
    ContextMenuState& state = *frame.state;
    input.fencePopup(frame.popupToken);
    if(input.consumePopupDismissal(frame.popupToken))
        state.close();
    while(auto trigger = input.consumeContextMenu(frame.anchor.id, frame.anchor.declarationGeneration)){
        if(frame.options.enabled && trigger->popup == m_context.popupToken() && !state.open({ trigger->position.x, trigger->position.y, 0.0f, 0.0f }))
            return false;
    }
    if(!frame.options.enabled)
        state.close();
    snapshotContextMenu(frame);
    input.fencePopup(frame.popupToken);
    input.fenceControl(frame.rows.id, frame.rows.declarationGeneration, frame.listToken);
    if(!frame.open){
        input.closePopup(frame.popupToken);
        return contextMenuMatches(frame);
    }
    if(!ListBehavior::Reconcile(state.m_list, *frame.source) || !contextMenuMatches(frame))
        return false;
    if(state.m_list.cursorKey() == 0u){
        ControlAction first;
        first.control = frame.listToken;
        first.kind = ControlActionKind::Home;
        ListOptions options;
        options.wheelRows = frame.options.wheelRows;
        options.selectOnNavigate = false;
        ListResult preview;
        if(!ListBehavior::Apply(*frame.source, options, first, state.m_list, preview) || !contextMenuMatches(frame))
            return false;
    }
    return applyContextMenuInput(frame, result);
}

b8 Builder::applyContextMenuInput(ContextMenuFrame& frame, ContextMenuResult& result){
    InputRouter& input = m_context.input();
    WidgetId thumb;
    if(!TryMakeWidgetId(frame.rows.id, s_WidgetPartScrollbar, thumb))
        return false;
    auto gesture = input.consumePointerGesture(thumb, frame.rows.declarationGeneration);
    b8 haveGesture = gesture && gesture->popup == frame.popupToken && gesture->control == frame.listToken;
    auto action = input.consumeControlAction(frame.rows.id, frame.rows.declarationGeneration, frame.listToken);
    b8 haveAction = action && action->popup == frame.popupToken;
    ListOptions options;
    options.wheelRows = frame.options.wheelRows;
    options.selectOnNavigate = false;
    while(haveGesture || haveAction){
        if(!contextMenuMatches(frame))
            return false;
        if(haveGesture && (!haveAction || gesture->updateSequence < action->id.sequence)){
            if(!applyListGesture(frame.state->m_list, *gesture))
                return false;
            gesture = input.consumePointerGesture(thumb, frame.rows.declarationGeneration);
            haveGesture = gesture && gesture->popup == frame.popupToken && gesture->control == frame.listToken;
        }
        else{
            ListResult preview;
            if(!ListBehavior::Apply(*frame.source, options, *action, frame.state->m_list, preview) || !contextMenuMatches(frame))
                return false;
            if(preview.activated){
                result.activated = true;
                result.key = frame.state->m_list.selectedKey();
                frame.state->close();
                input.closePopup(frame.popupToken);
                snapshotContextMenu(frame);
                return true;
            }
            action = input.consumeControlAction(frame.rows.id, frame.rows.declarationGeneration, frame.listToken);
            haveAction = action && action->popup == frame.popupToken;
        }
    }
    return contextMenuMatches(frame);
}

void Builder::snapshotContextMenu(ContextMenuFrame& frame)noexcept{
    frame.revision = frame.state->revision();
    frame.open = frame.state->isOpen();
    frame.popupToken.openGeneration = frame.state->m_popup.openGeneration();
    frame.listToken.instanceGeneration = frame.state->m_list.inputGeneration();
    if(frame.list < m_scope->m_lists.size())
        m_scope->m_lists[frame.list].token = frame.listToken;
}

b8 Builder::paintContextMenus(){
    for(auto& frame : m_scope->m_contextMenus){
        if(!contextMenuMatches(frame) || frame.list >= m_scope->m_lists.size() || frame.anchorIndex >= m_scope->m_items.size())
            return false;
        if(frame.open && frame.options.enabled && !paintContextMenuPopup(frame))
            return false;
    }
    return true;
}

b8 Builder::paintContextMenuPopup(ContextMenuFrame& frame){
    const Item& anchor = m_scope->m_items[frame.anchorIndex];
    const LayoutBox* anchorBox = m_scope->m_layout.box(anchor.node);
    const UiSkinRegion* background = region(frame.popupStyle.background, frame.popupStyle.fallback);
    if(!anchorBox || !background || !contextMenuMatches(frame))
        return false;
    const Rect clip = visibleClip(anchorBox->clip);
    if(
        !anchor.enabled || clip.width <= 0.0f || clip.height <= 0.0f
        || anchorBox->rectangle.x >= clip.x + clip.width || anchorBox->rectangle.y >= clip.y + clip.height
        || anchorBox->rectangle.x + anchorBox->rectangle.width <= clip.x || anchorBox->rectangle.y + anchorBox->rectangle.height <= clip.y
    ){
        frame.state->close();
        m_context.discardPopupScope(frame.popupToken);
        snapshotContextMenu(frame);
        return true;
    }
    PopupOptions options;
    options.anchor = frame.state->m_anchor;
    options.size = frame.options.size;
    options.gap = 0.0f;
    const auto placement = PopupLayout::Place(options, m_paint.displayMetrics());
    if(!placement)
        return false;
    PopupScope scope;
    scope.token = frame.popupToken;
    scope.parent = frame.parentToken;
    scope.bounds = placement->bounds;
    scope.viewport = placement->viewport;
    if(!m_context.updatePopupScope(frame.popupToken, scope) || !m_context.activatePopupScope(frame.popupToken))
        return false;
    if(!m_paint.beginOverlay(m_context.popupLayer())){
        const b8 ended = m_context.endPopupScope(false);
        if(!ended)
            m_context.fail();
        return false;
    }
    const Insets padding = MaxUiPadding(frame.popupStyle.padding, background->padding);
    m_paint.pushClip(placement->bounds);
    const b8 backgroundPainted = m_paint.drawRegion(background->name, placement->bounds);
    const b8 popped = m_paint.popClip();
    HitTarget barrier;
    barrier.rectangle = placement->bounds;
    barrier.clip = placement->viewport;
    b8 painted = backgroundPainted && popped && m_context.addTarget(frame.popup, barrier);
    if(painted){
        Item item(m_arena);
        item.state = frame.rows;
        item.list = frame.list;
        LayoutBox box;
        const SIMDVectorSingle inset = InsetRectValue(LoadRect(placement->bounds), VectorSet(padding.left, padding.top, padding.right, padding.bottom));
        box.rectangle = StoreRect(inset);
        box.clip = placement->bounds;
        painted = paintList(item, box) && contextMenuMatches(frame);
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

