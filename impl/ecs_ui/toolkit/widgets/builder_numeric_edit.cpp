// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/numeric_edit_coordinator.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NumericEditBoxResult Builder::integerEdit(const AStringView stableKey, IntegerEditModel& model, EditBoxState& state, const IntegerEditOptions& options){
    return NumericEditCoordinator(*this).integerEdit(stableKey, model, state, options);
}

NumericEditBoxResult Builder::floatEdit(const AStringView stableKey, FloatEditModel& model, EditBoxState& state, const FloatEditOptions& options){
    return NumericEditCoordinator(*this).floatEdit(stableKey, model, state, options);
}

b8 Builder::numericDraftMatches(const NumericEditFrame& frame, const EditModel& draft)const noexcept{
    return
        popupAncestorsVisible() && frame.state
        && draft.matchesStamp(frame.draftStamp)
        && frame.state->matchesStamp(frame.draftStamp, frame.focused)
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

