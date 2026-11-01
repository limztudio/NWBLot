// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "search_combo.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_search_combo{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 Matches(const SearchComboState& state, const SearchComboQuerySnapshot& query, const u64 comboGeneration, const u64 previewGeneration)noexcept{
    return
        state.query().instanceGeneration() == query.generation && state.query().externalRevision() == query.externalRevision
        && state.query().revision() == query.revision && state.query().compositionGeneration() == query.compositionGeneration
        && state.query().selectionGeneration() == query.selectionGeneration
        && state.query().anchor() == query.anchor && state.query().caret() == query.caret
        && state.combo().inputGeneration() == comboGeneration
        && state.combo().listState().inputGeneration() == previewGeneration
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SearchComboBehavior::Filter(SearchComboState& state, ISearchableListDataSource& source){
    using namespace __hidden_ui_search_combo;
    const SearchComboQuerySnapshot query(state.m_query.instanceGeneration(), state.m_query.externalRevision(), state.m_query.revision(), state.m_query.compositionGeneration(), state.m_query.selectionGeneration(), state.m_query.anchor(), state.m_query.caret());
    const u64 comboGeneration = state.m_combo.inputGeneration();
    const u64 previewGeneration = state.m_combo.listState().inputGeneration();
    const SearchComboSourceSnapshot full(source.instanceGeneration(), source.revision(), source.rowCount());
    const IListDataSource& previousView = source.filtered();
    const SearchComboSourceSnapshot previous(previousView.instanceGeneration(), previousView.revision(), previousView.rowCount());
    if(full.generation == 0u || full.revision == 0u || !Matches(state, query, comboGeneration, previewGeneration))
        return false;
    const b8 fullChanged = !state.m_filterValid || state.m_sourceGeneration != full.generation || state.m_sourceRevision != full.revision || state.m_sourceCount != full.count;
    const b8 viewChanged = !state.m_filterValid || state.m_viewGeneration != previous.generation || state.m_viewRevision != previous.revision || state.m_viewCount != previous.count;
    const AStringView cachedQuery{ state.m_filteredQuery.data(), state.m_filteredQuery.size() };
    const b8 queryChanged = !state.m_filterValid || cachedQuery != state.m_query.text();
    const b8 rebuild = fullChanged || viewChanged || queryChanged;
    AString<Core::Alloc::GlobalArena> candidate(state.m_arena);
    if(rebuild){
        const AStringView text = state.m_query.text();
        if(!text.empty())
            candidate.assign(text.data(), text.size());
        if(!source.filter({ candidate.data(), candidate.size() }))
            return false;
    }
    const IListDataSource& view = source.filtered();
    const SearchComboSourceSnapshot filtered(view.instanceGeneration(), view.revision(), view.rowCount());
    if(
        filtered.generation == 0u || filtered.revision == 0u || filtered.count > full.count
        || !ListBehavior::SourceMatches(view, filtered.generation, filtered.revision, filtered.count)
        || !ListBehavior::SourceMatches(source, full.generation, full.revision, full.count)
        || !Matches(state, query, comboGeneration, previewGeneration)
    )
        return false;
    const b8 externalQueryChange = state.m_filterValid && (state.m_queryGeneration != query.generation || state.m_queryExternalRevision != query.externalRevision);
    if(rebuild || externalQueryChange){
        if(!state.m_combo.m_list.scrollTo(0.0))
            return false;
        state.m_combo.m_list.select(state.m_combo.selectedKey());
    }
    if(rebuild)
        state.m_filteredQuery = Move(candidate);
    state.m_sourceGeneration = full.generation;
    state.m_sourceRevision = full.revision;
    state.m_sourceCount = full.count;
    state.m_viewGeneration = filtered.generation;
    state.m_viewRevision = filtered.revision;
    state.m_viewCount = filtered.count;
    state.m_queryGeneration = query.generation;
    state.m_queryExternalRevision = query.externalRevision;
    state.m_filterValid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

