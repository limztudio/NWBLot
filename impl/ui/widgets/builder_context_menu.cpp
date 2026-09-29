// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ContextMenuResult Builder::contextMenu(const AStringView stableKey, const AStringView anchorKey,
    const IListDataSource& source, ContextMenuState& state, const ContextMenuOptions& options){
    ContextMenuResult result;
    Item* anchor = annotationAnchor(anchorKey);
    if(
        !m_scope->m_panelActive || m_scope->m_popupState || (m_scope->m_windowActive && m_scope->m_window.state->collapsed) || m_context.failed()
        || !anchor || !IsFinite(options.size.x) || !IsFinite(options.size.y) || options.size.x <= 0.0f || options.size.y <= 0.0f
        || !IsFinite(options.rowHeight) || options.rowHeight <= 0.0f || !IsFinite(options.wheelRows) || options.wheelRows <= 0.0f
        || m_scope->m_contextMenus.size() >= s_InputMaxPopups
    ){
        m_context.fail();
        return result;
    }
    for(const auto& previous : m_scope->m_contextMenus){
        if(previous.anchor.id == anchor->state.id){
            m_context.fail();
            return result;
        }
    }
    WidgetState* widget = m_context.declare(stableKey, WidgetKind::ContextMenu);
    if(!widget || !m_context.claimState(*widget, state.instanceGeneration()))
        return result;
    ContextMenuFrame frame;
    frame.widget = *widget;
    frame.anchor = anchor->state;
    frame.anchorIndex = static_cast<u32>(anchor - m_scope->m_items.data());
    frame.source = &source;
    frame.state = &state;
    frame.options = options;
    frame.options.enabled = options.enabled && anchor->enabled;
    const bool previouslyOpen = state.isOpen();
    if(
        state.m_owner != widget->id || state.m_ownerDeclaration != widget->declarationGeneration
        || state.m_anchorWidget != anchor->state.id || state.m_anchorDeclaration != anchor->state.declarationGeneration
    ){
        if(state.m_owner.valid())
            state.close();
        state.m_owner = widget->id;
        state.m_ownerDeclaration = widget->declarationGeneration;
        state.m_anchorWidget = anchor->state.id;
        state.m_anchorDeclaration = anchor->state.declarationGeneration;
        state.advanceRevision();
    }
    WidgetState* popup = m_context.declarePart(frame.widget, "popup", WidgetKind::Popup);
    if(!popup)
        return result;
    frame.popup = *popup;
    WidgetState* rows = m_context.declarePart(frame.widget, "rows", WidgetKind::VirtualList);
    if(!rows)
        return result;
    frame.rows = *rows;
    const bool previouslyListFocused = m_context.input().focus() == frame.rows.id;
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
    ListFrame list;
    list.source = &source;
    list.state = &state.m_list;
    list.token = frame.listToken;
    list.rowCount = frame.rowCount;
    list.options.rowHeight = options.rowHeight;
    list.options.wheelRows = options.wheelRows;
    list.options.selectOnNavigate = false;
    list.options.enabled = frame.options.enabled;
    list.focusOnCommit = frame.open && frame.options.enabled && previouslyListFocused
        && m_context.input().focus() != frame.rows.id;
    list.padding = m_listStyle.padding;
    const UiSkinRegion* background = region(m_listStyle.background, m_listStyle.backgroundFallback);
    if(!background || !contextMenuMatches(frame)){
        m_context.fail();
        return result;
    }
    list.padding.left = Max(list.padding.left, background->padding.left);
    list.padding.top = Max(list.padding.top, background->padding.top);
    list.padding.right = Max(list.padding.right, background->padding.right);
    list.padding.bottom = Max(list.padding.bottom, background->padding.bottom);
    frame.list = static_cast<u32>(m_scope->m_lists.size());
    m_scope->m_lists.push_back(list);
    anchor->contextMenu = frame.options.enabled;
    anchor->annotated = true;
    result.valid = true;
    result.opened = !previouslyOpen && state.isOpen();
    result.closed = previouslyOpen && !state.isOpen();
    m_scope->m_contextMenus.push_back(frame);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

