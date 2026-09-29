// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::comboStateMatches(const ComboFrame& frame)const{
    return
        frame.state && frame.state->inputGeneration() == frame.token.instanceGeneration
        && frame.state->m_list.inputGeneration() == frame.listToken.instanceGeneration
        && frame.state->isOpen() == frame.open
        && frame.state->m_popup.instanceGeneration() == frame.popupToken.instanceGeneration
        && frame.state->m_popup.openGeneration() == frame.popupToken.openGeneration
    ;
}

bool Builder::comboMatches(const ComboFrame& frame)const{
    return
        frame.source && comboStateMatches(frame) && frame.source->instanceGeneration() == frame.token.contentGeneration
        && frame.source->revision() == frame.token.contentRevision && frame.source->rowCount() == frame.rowCount
        && comboStateMatches(frame)
    ;
}

bool Builder::applyComboInput(const WidgetState& field, ComboFrame& frame, ComboResult& result){
    InputRouter& input = m_context.input();
    ComboState& state = *frame.state;
    input.fenceControl(field.id, field.declarationGeneration, frame.token);
    input.fencePopup(frame.popupToken);
    if(!frame.options.enabled){
        input.invalidateTarget(field.id);
        ComboBehavior::Close(state);
        input.closePopup(frame.popupToken);
        return true;
    }
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    if(input.consumePopupDismissal(frame.popupToken, reason))
        ComboBehavior::Close(state);
    if(!state.isOpen())
        input.closePopup(frame.popupToken);
    if(m_context.takeActivation(field, true))
        ComboBehavior::Open(state);
    ControlAction action;
    while(m_context.takeControlAction(field, true, frame.token, action)){
        if(state.isOpen())
            continue;
        if(
            action.kind != ControlActionKind::Submit && action.kind != ControlActionKind::Up
            && action.kind != ControlActionKind::Down
        )
            continue;
        ComboBehavior::Open(state);
        if(!ListBehavior::Reconcile(state.m_list, *frame.source))
            return false;
        if(action.kind != ControlActionKind::Submit){
            action.control.instanceGeneration = state.m_list.inputGeneration();
            ListOptions options;
            options.rowHeight = frame.options.rowHeight;
            options.wheelRows = frame.options.wheelRows;
            options.selectOnNavigate = false;
            ListResult preview;
            if(!ListBehavior::Apply(state.m_list, *frame.source, options, action, preview))
                return false;
        }
    }
    if(!ListBehavior::Reconcile(state.m_list, *frame.source))
        return false;
    frame.popupToken.openGeneration = state.m_popup.openGeneration();
    frame.listToken.instanceGeneration = state.m_list.inputGeneration();
    frame.open = state.isOpen();
    input.fencePopup(frame.popupToken);
    input.fenceControl(frame.rows.id, frame.rows.declarationGeneration, frame.listToken);
    if(!comboMatches(frame))
        return false;
    return !state.isOpen() || applyComboListInput(frame, result);
}

bool Builder::applyComboListInput(ComboFrame& frame, ComboResult& result){
    InputRouter& input = m_context.input();
    ComboState& state = *frame.state;
    const WidgetId thumb = MakeWidgetId(frame.rows.id, "scrollbar");
    PointerGesture gesture;
    bool haveGesture = input.consumePointerGesture(thumb, frame.rows.declarationGeneration, gesture)
        && gesture.control == frame.listToken && gesture.popup == frame.popupToken;
    ControlAction action;
    bool haveAction = input.consumeControlAction(frame.rows.id, frame.rows.declarationGeneration, frame.listToken, action)
        && action.popup == frame.popupToken;
    ListOptions options;
    options.rowHeight = frame.options.rowHeight;
    options.wheelRows = frame.options.wheelRows;
    options.selectOnNavigate = false;
    while(haveGesture || haveAction){
        if(!comboMatches(frame))
            return false;
        if(haveGesture && (!haveAction || gesture.updateSequence < action.id.sequence)){
            if(!applyListGesture(state.m_list, gesture))
                return false;
            haveGesture = input.consumePointerGesture(thumb, frame.rows.declarationGeneration, gesture)
                && gesture.control == frame.listToken && gesture.popup == frame.popupToken;
        }
        else{
            ListResult preview;
            if(!ListBehavior::Apply(state.m_list, *frame.source, options, action, preview) || !comboMatches(frame))
                return false;
            if(preview.activated){
                if(!ComboBehavior::Commit(state, *frame.source, state.m_list.selectedKey()))
                    return false;
                result.committed = true;
                input.closePopup(frame.popupToken);
                return true;
            }
            haveAction = input.consumeControlAction(frame.rows.id, frame.rows.declarationGeneration, frame.listToken, action)
                && action.popup == frame.popupToken;
        }
    }
    return comboMatches(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

