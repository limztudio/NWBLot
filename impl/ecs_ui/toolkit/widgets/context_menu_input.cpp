// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        if(!ListBehavior::Apply(state.m_list, preview, *frame.source, options, first) || !contextMenuMatches(frame))
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
            if(!ListBehavior::Apply(frame.state->m_list, preview, *frame.source, options, *action) || !contextMenuMatches(frame))
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

