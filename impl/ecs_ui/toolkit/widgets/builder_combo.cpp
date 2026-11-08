// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/combo_coordinator.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ComboResult Builder::declareCombo(AStringView stableKey, const IListDataSource& source, const ComboOptions& options, const SearchComboOptions* searchOptions, ComboState& state, SearchComboState* search, ISearchableListDataSource* searchSource){
    return ComboCoordinator(*this).declareCombo(stableKey, source, options, searchOptions, state, search, searchSource);
}

b8 Builder::comboStateMatches(const ComboFrame& frame)const noexcept{
    return
        popupAncestorsVisible() && frame.state && frame.state->inputGeneration() == frame.token.instanceGeneration && frame.state->m_list.inputGeneration() == frame.listToken.instanceGeneration && frame.state->isOpen() == frame.open
        && frame.state->m_popup.instanceGeneration() == frame.popupToken.instanceGeneration && frame.state->m_popup.openGeneration() == frame.popupToken.openGeneration && frame.state->m_popup.m_parent == frame.parentToken
        && (!frame.search || frame.search->query().matchesStamp(frame.queryStamp))
    ;
}

b8 Builder::comboMatches(const ComboFrame& frame)const{
    return
        frame.source && comboStateMatches(frame) && (!frame.searchSource || &frame.searchSource->filtered() == frame.results) && comboStateMatches(frame)
        && frame.source->instanceGeneration() == frame.token.contentGeneration && comboStateMatches(frame)
        && frame.source->revision() == frame.token.contentRevision && comboStateMatches(frame)
        && frame.source->rowCount() == frame.rowCount && comboStateMatches(frame)
        && frame.results && frame.results->instanceGeneration() == frame.listToken.contentGeneration && comboStateMatches(frame)
        && frame.results->revision() == frame.listToken.contentRevision && comboStateMatches(frame)
        && frame.results->rowCount() == frame.resultCount && comboStateMatches(frame)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

