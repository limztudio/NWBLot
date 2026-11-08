// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/combo_coordinator.h>
#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/list_input.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ComboCoordinator::applyComboInput(const WidgetState& field, ComboFrame& frame, ComboResult& result){
    InputRouter& input = m_builder.m_context.input();
    ComboState& state = *frame.state;
    input.fenceControl(field.id, field.declarationGeneration, frame.token);
    input.fencePopup(frame.popupToken);
    if(!frame.options.enabled){
        input.invalidateTarget(field.id);
        ComboBehavior::Close(state);
        input.closePopup(frame.popupToken);
        if(frame.search){
            frame.search->query().cancelComposition();
            snapshotComboQuery(frame);
        }
        return true;
    }
    if(input.consumePopupDismissal(frame.popupToken))
        ComboBehavior::Close(state);
    if(!state.isOpen())
        input.closePopup(frame.popupToken);
    if(m_builder.m_context.takeActivation(field, true))
        ComboBehavior::Open(state);
    while(auto action = m_builder.m_context.takeControlAction(field, true, frame.token)){
        if(state.isOpen())
            continue;
        if(action->kind != ControlActionKind::Submit && action->kind != ControlActionKind::Up && action->kind != ControlActionKind::Down)
            continue;
        ComboBehavior::Open(state);
        if(!ListBehavior::Reconcile(state.m_list, *frame.results))
            return false;
        if(action->kind != ControlActionKind::Submit){
            action->control = { state.m_list.inputGeneration(), frame.results->instanceGeneration(), frame.results->revision() };
            ListOptions options;
            options.wheelRows = frame.options.wheelRows;
            options.selectOnNavigate = false;
            ListResult preview;
            if(!ListBehavior::Apply(*frame.results, options, *action, state.m_list, preview))
                return false;
        }
    }
    if(!ListBehavior::Reconcile(state.m_list, *frame.results))
        return false;
    frame.popupToken.openGeneration = state.m_popup.openGeneration();
    frame.listToken.instanceGeneration = state.m_list.inputGeneration();
    frame.open = state.isOpen();
    input.fencePopup(frame.popupToken);
    input.fenceControl(frame.rows.id, frame.rows.declarationGeneration, frame.listToken);
    if(!m_builder.comboMatches(frame))
        return false;
    if(frame.search && !prepareComboSearch(field, frame))
        return false;
    return !state.isOpen() || applyComboListInput(frame, result);
}

b8 ComboCoordinator::applyComboListInput(ComboFrame& frame, ComboResult& result){
    ComboState& state = *frame.state;
    ListOptions options;
    options.wheelRows = frame.options.wheelRows;
    options.selectOnNavigate = false;
    return ListInput(m_builder.m_context).consume(
        frame.rows,
        frame.listToken,
        true,
        &frame.popupToken,
        state.m_list,
        [&](){ return m_builder.comboMatches(frame); },
        [&](const ControlAction& action){
            const b8 fromEditor = frame.editor != s_LayoutNoParent && action.source == m_builder.m_scope->m_comboEditors[frame.editor].state.id;
            if(fromEditor && (frame.search->query().composition().active || (action.kind == ControlActionKind::Submit && !frame.editorSubmitted)))
                return ListActionOutcome::Continue;
            ListResult preview;
            if(!ListBehavior::Apply(*frame.results, options, action, state.m_list, preview) || !m_builder.comboMatches(frame))
                return ListActionOutcome::Failed;
            if(!preview.activated)
                return ListActionOutcome::Continue;
            if(!ComboBehavior::Commit(state, *frame.source, state.m_list.selectedKey()))
                return ListActionOutcome::Failed;
            result.committed = true;
            m_builder.m_context.input().closePopup(frame.popupToken);
            return ListActionOutcome::Stop;
        });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

