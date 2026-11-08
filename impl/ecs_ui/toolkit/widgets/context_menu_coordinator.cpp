// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/context_menu_coordinator.h>
#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/list_input.h>
#include <impl/ecs_ui/toolkit/widgets/ui_scope_descriptor_factory.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/hit_target_factory.adhd.h>

#include <global/simd/vector_arithmetic.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ContextMenuResult ContextMenuCoordinator::declareMenu(const AStringView stableKey, const AStringView anchorKey, const IListDataSource& source, ContextMenuState& state, const ContextMenuOptions& options){
    ContextMenuResult result;
    Item* anchor = m_builder.annotationAnchor(anchorKey);
    if(m_builder.panelDeclarationBlocked() || !anchor || !IsValidContextMenuDeclarationOptions(options, m_builder.m_scope->m_contextMenus.size(), s_InputMaxPopups)){
        m_builder.m_context.fail();
        return result;
    }
    if(!m_builder.synchronizePopup()){
        result.valid = true;
        return result;
    }
    for(const auto& previous : m_builder.m_scope->m_contextMenus){
        if(previous.anchor.id == anchor->state.id){
            m_builder.m_context.fail();
            return result;
        }
    }
    m_builder.m_declaring = true;
    ScopeExit finish([this]()noexcept{ m_builder.m_declaring = false; });
    const auto widget = m_builder.m_context.declare(stableKey, WidgetKind::ContextMenu);
    if(!widget || !m_builder.m_context.claimState(**widget, state.instanceGeneration()))
        return result;
    ContextMenuFrame frame;
    frame.parentToken = m_builder.m_context.popupToken();
    frame.widget = **widget;
    frame.anchor = anchor->state;
    frame.anchorIndex = static_cast<u32>(anchor - m_builder.m_scope->m_items.data());
    frame.source = &source;
    frame.state = &state;
    frame.popupStyle = m_builder.m_popupStyle;
    frame.options = options;
    frame.options.enabled = options.enabled && anchor->enabled;
    const b8 previouslyOpen = state.isOpen();
    if(DidContextMenuBindingChange(
        (*widget)->id,
        (*widget)->declarationGeneration,
        anchor->state.id,
        anchor->state.declarationGeneration,
        state.m_owner,
        state.m_ownerDeclaration,
        state.m_anchorWidget,
        state.m_anchorDeclaration
    )){
        if(state.m_owner.valid())
            state.close();
        state.m_owner = (*widget)->id;
        state.m_ownerDeclaration = (*widget)->declarationGeneration;
        state.m_anchorWidget = anchor->state.id;
        state.m_anchorDeclaration = anchor->state.declarationGeneration;
        AdvanceNonWrappingRevision(state.m_revision);
    }
    const b8 boundOpen = state.isOpen();
    state.m_popup.bindParent(frame.parentToken);
    if(boundOpen && !state.isOpen())
        state.close();
    const auto popup = m_builder.m_context.declarePart(frame.widget, s_WidgetPartPopup, WidgetKind::Popup);
    if(!popup)
        return result;
    frame.popup = **popup;
    const auto rows = m_builder.m_context.declarePart(frame.widget, s_WidgetPartRows, WidgetKind::VirtualList);
    if(!rows)
        return result;
    frame.rows = **rows;
    const b8 previouslyListFocused = m_builder.m_context.input().focus() == frame.rows.id;
    frame.popupToken = { frame.popup.id, frame.popup.declarationGeneration, state.instanceGeneration(), state.m_popup.openGeneration() };
    frame.revision = state.revision();
    frame.open = state.isOpen();
    frame.listToken = { state.m_list.inputGeneration(), source.instanceGeneration(), source.revision() };
    frame.rowCount = source.rowCount();
    if(!m_builder.contextMenuMatches(frame)){
        m_builder.m_context.fail();
        return result;
    }
    const u64 previousGeneration = state.m_list.m_sourceGeneration;
    const u64 previousRevision = state.m_list.m_sourceRevision;
    if(!ListBehavior::Reconcile(state.m_list, source) || !m_builder.contextMenuMatches(frame)){
        m_builder.m_context.fail();
        return result;
    }
    if(previousGeneration != 0u && previousGeneration != frame.listToken.contentGeneration)
        state.close();
    else if(previousRevision != 0u && previousRevision != frame.listToken.contentRevision){
        state.m_list.select(0u);
        if(!state.m_list.scrollTo(0.0)){
            m_builder.m_context.fail();
            return result;
        }
    }
    snapshotContextMenu(frame);
    if(!prepareContextMenu(frame, result)){
        m_builder.m_context.fail();
        return result;
    }
    ListOptions listOptions;
    listOptions.wheelRows = options.wheelRows;
    listOptions.enabled = frame.options.enabled;
    listOptions.selectOnNavigate = false;
    const b8 focusOnCommit = frame.open && frame.options.enabled && previouslyListFocused && m_builder.m_context.input().focus() != frame.rows.id;
    ListFrame list(m_builder.m_arena);
    if(!m_builder.prepareListFrame(source, state.m_list, listOptions, frame.listToken, frame.rowCount, focusOnCommit, {}, list) || !m_builder.contextMenuMatches(frame)){
        m_builder.m_context.fail();
        return result;
    }
    frame.list = static_cast<u32>(m_builder.m_scope->m_lists.size());
    state.m_popup.bindParent(frame.parentToken);
    if(frame.open && !m_builder.reserveCompoundPopup(frame.popup, frame.popupToken)){
        m_builder.m_context.fail();
        return {};
    }
    m_builder.m_scope->m_lists.push_back(Move(list));
    anchor->contextMenu = frame.options.enabled;
    anchor->annotated = true;
    result.valid = true;
    result.opened = !previouslyOpen && state.isOpen();
    result.closed = previouslyOpen && !state.isOpen();
    m_builder.m_scope->m_contextMenus.push_back(frame);
    return result;
}

b8 ContextMenuCoordinator::prepareContextMenu(ContextMenuFrame& frame, ContextMenuResult& result){
    InputRouter& input = m_builder.m_context.input();
    ContextMenuState& state = *frame.state;
    input.fencePopup(frame.popupToken);
    if(input.consumePopupDismissal(frame.popupToken))
        state.close();
    while(auto trigger = input.consumeContextMenu(frame.anchor.id, frame.anchor.declarationGeneration)){
        if(frame.options.enabled && trigger->popup == m_builder.m_context.popupToken() && !state.open({ trigger->position.x, trigger->position.y, 0.0f, 0.0f }))
            return false;
    }
    if(!frame.options.enabled)
        state.close();
    snapshotContextMenu(frame);
    input.fencePopup(frame.popupToken);
    input.fenceControl(frame.rows.id, frame.rows.declarationGeneration, frame.listToken);
    if(!frame.open){
        input.closePopup(frame.popupToken);
        return m_builder.contextMenuMatches(frame);
    }
    if(!ListBehavior::Reconcile(state.m_list, *frame.source) || !m_builder.contextMenuMatches(frame))
        return false;
    if(state.m_list.cursorKey() == 0u){
        ControlAction first;
        first.control = frame.listToken;
        first.kind = ControlActionKind::Home;
        ListOptions options;
        options.wheelRows = frame.options.wheelRows;
        options.selectOnNavigate = false;
        ListResult preview;
        if(!ListBehavior::Apply(*frame.source, options, first, state.m_list, preview) || !m_builder.contextMenuMatches(frame))
            return false;
    }
    return applyContextMenuInput(frame, result);
}

b8 ContextMenuCoordinator::applyContextMenuInput(ContextMenuFrame& frame, ContextMenuResult& result){
    ListOptions options;
    options.wheelRows = frame.options.wheelRows;
    options.selectOnNavigate = false;
    return ListInput(m_builder.m_context).consume(
        frame.rows,
        frame.listToken,
        true,
        &frame.popupToken,
        frame.state->m_list,
        [&](){ return m_builder.contextMenuMatches(frame); },
        [&](const ControlAction& action){
            ListResult preview;
            if(!ListBehavior::Apply(*frame.source, options, action, frame.state->m_list, preview) || !m_builder.contextMenuMatches(frame))
                return ListActionOutcome::Failed;
            if(!preview.activated)
                return ListActionOutcome::Continue;
            result.activated = true;
            result.key = frame.state->m_list.selectedKey();
            frame.state->close();
            m_builder.m_context.input().closePopup(frame.popupToken);
            snapshotContextMenu(frame);
            return ListActionOutcome::Stop;
        });
}

void ContextMenuCoordinator::snapshotContextMenu(ContextMenuFrame& frame)noexcept{
    frame.revision = frame.state->revision();
    frame.open = frame.state->isOpen();
    frame.popupToken.openGeneration = frame.state->m_popup.openGeneration();
    frame.listToken.instanceGeneration = frame.state->m_list.inputGeneration();
    if(frame.list < m_builder.m_scope->m_lists.size())
        m_builder.m_scope->m_lists[frame.list].token = frame.listToken;
}

b8 ContextMenuCoordinator::paintMenus(){
    for(auto& frame : m_builder.m_scope->m_contextMenus){
        if(!m_builder.contextMenuMatches(frame) || frame.list >= m_builder.m_scope->m_lists.size() || frame.anchorIndex >= m_builder.m_scope->m_items.size())
            return false;
        if(frame.open && frame.options.enabled && !paintContextMenuPopup(frame))
            return false;
    }
    return true;
}

b8 ContextMenuCoordinator::paintContextMenuPopup(ContextMenuFrame& frame){
    const Item& anchor = m_builder.m_scope->m_items[frame.anchorIndex];
    const LayoutBox* anchorBox = m_builder.m_scope->m_layout.box(anchor.node);
    const UiSkinRegion* background = m_builder.region(frame.popupStyle.background, frame.popupStyle.fallback);
    if(!anchorBox || !background || !m_builder.contextMenuMatches(frame))
        return false;
    const Rect clip = m_builder.visibleClip(anchorBox->clip);
    if(!IsContextMenuAnchorVisible(anchorBox->rectangle, clip, anchor.enabled)){
        frame.state->close();
        m_builder.m_context.discardPopupScope(frame.popupToken);
        snapshotContextMenu(frame);
        return true;
    }
    PopupOptions options;
    options.anchor = frame.state->m_anchor;
    options.size = frame.options.size;
    options.gap = 0.0f;
    const auto placement = PlacePopup(options, m_builder.m_paint.displayMetrics());
    if(!placement)
        return false;
    const PopupScope scope = UiScopeDescriptorFactory().basicPopup(frame.popupToken, frame.parentToken, *placement);
    if(!m_builder.m_context.updatePopupScope(frame.popupToken, scope) || !m_builder.m_context.activatePopupScope(frame.popupToken))
        return false;
    if(!m_builder.m_paint.beginOverlay(m_builder.m_context.popupLayer())){
        const b8 ended = m_builder.m_context.endPopupScope(false);
        if(!ended)
            m_builder.m_context.fail();
        return false;
    }
    const Insets padding = MaxUiPadding(frame.popupStyle.padding, background->padding);
    m_builder.m_paint.pushClip(placement->bounds);
    const b8 backgroundPainted = m_builder.m_paint.drawRegion(background->name, placement->bounds);
    const b8 popped = m_builder.m_paint.popClip();
    const HitTarget barrier = HitTargetFactory().makeBarrierHitTarget(placement->bounds, placement->viewport);
    b8 painted = backgroundPainted && popped && m_builder.m_context.addTarget(frame.popup, barrier);
    if(painted){
        Item item(m_builder.m_arena);
        item.state = frame.rows;
        item.list = frame.list;
        LayoutBox box;
        box.rectangle = InsetUiRectBounds(placement->bounds, padding);
        box.clip = placement->bounds;
        painted = m_builder.paintList(item, box) && m_builder.contextMenuMatches(frame);
    }
    const b8 overlayEnded = m_builder.m_paint.endOverlay();
    const b8 scopeEnded = m_builder.m_context.endPopupScope(painted);
    if(painted)
        frame.state->m_popup.m_placement = *placement;
    return painted && overlayEnded && scopeEnded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

