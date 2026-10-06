// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Builder::textAreaAvailable(const EditModel& model, const TextAreaState& state)const noexcept{
    for(usize index = 0u; index <= m_popupFrameCount; ++index){
        const BuilderScopeFrame& scope = index == 0u ? m_scopeFrame : *m_popupFrames[index - 1u];
        for(const auto& frame : scope.m_textAreas){
            if(frame.model == &model || frame.state == &state)
                return false;
        }
    }
    return true;
}

void Builder::snapshotTextArea(TextAreaFrame& frame){
    frame.stateRevision = frame.state->revision();
    frame.navigation = frame.state->navigation().snapshot();
    frame.modelGeneration = frame.model->instanceGeneration();
    frame.modelRevision = frame.model->revision();
    frame.externalRevision = frame.model->externalRevision();
    frame.selectionGeneration = frame.model->selectionGeneration();
    frame.compositionGeneration = frame.model->compositionGeneration();
    frame.anchor = frame.model->anchor();
    frame.caret = frame.model->caret();
}

bool Builder::textAreaMatches(const TextAreaFrame& frame)const{
    return
        popupAncestorsVisible() && frame.model && frame.state
        && frame.state->revision() == frame.stateRevision && frame.state->navigation().matches(frame.navigation)
        && frame.model->instanceGeneration() == frame.modelGeneration && frame.model->revision() == frame.modelRevision
        && frame.model->externalRevision() == frame.externalRevision && frame.model->selectionGeneration() == frame.selectionGeneration
        && frame.model->compositionGeneration() == frame.compositionGeneration
        && frame.model->anchor() == frame.anchor && frame.model->caret() == frame.caret
        && frame.state->m_visual.modelGeneration == frame.modelGeneration && frame.state->m_visual.revision == frame.modelRevision
        && frame.state->m_visual.selectionGeneration == frame.selectionGeneration
        && frame.state->m_visual.anchor == frame.anchor && frame.state->m_visual.caret == frame.caret
        && frame.state->m_visual.focused == frame.focused
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

