// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Builder::snapshotContextMenu(ContextMenuFrame& frame){
    frame.revision = frame.state->revision();
    frame.open = frame.state->isOpen();
    frame.popupToken.openGeneration = frame.state->m_popup.openGeneration();
    frame.listToken.instanceGeneration = frame.state->m_list.inputGeneration();
    if(frame.list < m_scope->m_lists.size())
        m_scope->m_lists[frame.list].token = frame.listToken;
}

bool Builder::contextMenuStateMatches(const ContextMenuFrame& frame)const{
    return
        popupAncestorsVisible() && frame.state && frame.state->revision() == frame.revision && frame.state->isOpen() == frame.open
        && frame.state->m_popup.instanceGeneration() == frame.popupToken.instanceGeneration
        && frame.state->m_popup.openGeneration() == frame.popupToken.openGeneration
        && frame.state->m_popup.m_parent == frame.parentToken
        && frame.state->m_list.inputGeneration() == frame.listToken.instanceGeneration
        && frame.state->m_owner == frame.widget.id && frame.state->m_ownerDeclaration == frame.widget.declarationGeneration
        && frame.state->m_anchorWidget == frame.anchor.id && frame.state->m_anchorDeclaration == frame.anchor.declarationGeneration
    ;
}

bool Builder::contextMenuMatches(const ContextMenuFrame& frame)const{
    return
        frame.source && contextMenuStateMatches(frame)
        && frame.listToken.contentGeneration != 0u && frame.listToken.contentRevision != 0u
        && frame.source->instanceGeneration() == frame.listToken.contentGeneration && contextMenuStateMatches(frame)
        && frame.source->revision() == frame.listToken.contentRevision && contextMenuStateMatches(frame)
        && frame.source->rowCount() == frame.rowCount
        && contextMenuStateMatches(frame)
    ;
}

bool Builder::prepareContextMenu(ContextMenuFrame& frame, ContextMenuResult& result){
    InputRouter& input = m_context.input();
    ContextMenuState& state = *frame.state;
    input.fencePopup(frame.popupToken);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    if(input.consumePopupDismissal(frame.popupToken, reason))
        state.close();
    ContextMenuAction trigger;
    while(input.consumeContextMenu(frame.anchor.id, frame.anchor.declarationGeneration, trigger)){
        if(frame.options.enabled && trigger.popup == m_context.popupToken()
            && !state.open({ trigger.position.x, trigger.position.y, 0.0f, 0.0f }))
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
    if(!ListBehavior::reconcile(state.m_list, *frame.source) || !contextMenuMatches(frame))
        return false;
    if(state.m_list.cursorKey() == 0u){
        ControlAction first;
        first.control = frame.listToken;
        first.kind = ControlActionKind::Home;
        ListOptions options;
        options.rowHeight = frame.options.rowHeight;
        options.wheelRows = frame.options.wheelRows;
        options.selectOnNavigate = false;
        ListResult preview;
        if(!ListBehavior::apply(state.m_list, *frame.source, options, first, preview) || !contextMenuMatches(frame))
            return false;
    }
    return applyContextMenuInput(frame, result);
}

bool Builder::applyContextMenuInput(ContextMenuFrame& frame, ContextMenuResult& result){
    InputRouter& input = m_context.input();
    const WidgetId thumb = MakeWidgetId(frame.rows.id, "scrollbar");
    PointerGesture gesture;
    bool haveGesture = input.consumePointerGesture(thumb, frame.rows.declarationGeneration, gesture)
        && gesture.popup == frame.popupToken && gesture.control == frame.listToken;
    ControlAction action;
    bool haveAction = input.consumeControlAction(frame.rows.id, frame.rows.declarationGeneration, frame.listToken, action)
        && action.popup == frame.popupToken;
    ListOptions options;
    options.rowHeight = frame.options.rowHeight;
    options.wheelRows = frame.options.wheelRows;
    options.selectOnNavigate = false;
    while(haveGesture || haveAction){
        if(!contextMenuMatches(frame))
            return false;
        if(haveGesture && (!haveAction || gesture.updateSequence < action.id.sequence)){
            if(!applyListGesture(frame.state->m_list, gesture))
                return false;
            haveGesture = input.consumePointerGesture(thumb, frame.rows.declarationGeneration, gesture)
                && gesture.popup == frame.popupToken && gesture.control == frame.listToken;
        }
        else{
            ListResult preview;
            if(!ListBehavior::apply(frame.state->m_list, *frame.source, options, action, preview) || !contextMenuMatches(frame))
                return false;
            if(preview.activated){
                result.activated = true;
                result.key = frame.state->m_list.selectedKey();
                frame.state->close();
                input.closePopup(frame.popupToken);
                snapshotContextMenu(frame);
                return true;
            }
            haveAction = input.consumeControlAction(frame.rows.id, frame.rows.declarationGeneration, frame.listToken, action)
                && action.popup == frame.popupToken;
        }
    }
    return contextMenuMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

