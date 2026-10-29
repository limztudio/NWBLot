// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Builder::snapshotNumericEdit(NumericEditFrame& frame, const EditModel& draft)noexcept{
    frame.draftGeneration = draft.instanceGeneration();
    frame.draftRevision = draft.revision();
    frame.draftExternalRevision = draft.externalRevision();
    frame.draftCompositionGeneration = draft.compositionGeneration();
    frame.draftSelectionGeneration = draft.selectionGeneration();
    frame.anchor = draft.anchor();
    frame.caret = draft.caret();
}

b8 Builder::numericDraftMatches(const NumericEditFrame& frame, const EditModel& draft)const noexcept{
    return
        popupAncestorsVisible() && frame.state && draft.instanceGeneration() == frame.draftGeneration
        && draft.revision() == frame.draftRevision && draft.externalRevision() == frame.draftExternalRevision
        && draft.compositionGeneration() == frame.draftCompositionGeneration && draft.selectionGeneration() == frame.draftSelectionGeneration
        && draft.anchor() == frame.anchor && draft.caret() == frame.caret
        && frame.state->modelGeneration == frame.draftGeneration && frame.state->revision == frame.draftRevision
        && frame.state->selectionGeneration == frame.draftSelectionGeneration
        && frame.state->anchor == frame.anchor && frame.state->caret == frame.caret && frame.state->focused == frame.focused
    ;
}

b8 Builder::numericEditMatches(const Item& item)const noexcept{
    if(item.integerEdit != s_LayoutNoParent){
        if(item.integerEdit >= m_scope->m_integerEdits.size() || !numericEditFrameMatches(m_scope->m_integerEdits[item.integerEdit]))
            return false;
    }
    if(item.floatEdit != s_LayoutNoParent){
        if(item.floatEdit >= m_scope->m_floatEdits.size() || !numericEditFrameMatches(m_scope->m_floatEdits[item.floatEdit]))
            return false;
    }
    return true;
}

b8 Builder::numericStateAvailable(const EditBoxState& state)const noexcept{
    for(usize index = 0u; index <= m_popupFrameCount; ++index){
        const BuilderScopeFrame& scope = index == 0u ? m_scopeFrame : *m_popupFrames[index - 1u];
        for(const auto& frame : scope.m_integerEdits){
            if(frame.state == &state)
                return false;
        }
        for(const auto& frame : scope.m_floatEdits){
            if(frame.state == &state)
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

