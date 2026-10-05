// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "search_combo.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_search_combo{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceSnapshot{
    u64 generation = 0u;
    u64 revision = 0u;
    u64 count = 0u;
};

struct QuerySnapshot{
    u64 generation = 0u;
    u64 externalRevision = 0u;
    u64 revision = 0u;
    u64 compositionGeneration = 0u;
    u64 selectionGeneration = 0u;
    usize anchor = 0u;
    usize caret = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SourceSnapshot Snapshot(const IListDataSource& source){
    return { source.instanceGeneration(), source.revision(), source.rowCount() };
}

[[nodiscard]] static bool Matches(const IListDataSource& source, const SourceSnapshot& snapshot){
    return
        snapshot.generation != 0u && snapshot.revision != 0u && source.instanceGeneration() == snapshot.generation
        && source.revision() == snapshot.revision && source.rowCount() == snapshot.count
    ;
}

[[nodiscard]] static bool Matches(const SearchComboState& state, const QuerySnapshot& query,
    const u64 comboGeneration, const u64 previewGeneration){
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


SearchComboState::SearchComboState(Core::Alloc::GlobalArena& arena)
    : m_arena(arena)
    , m_query(arena)
    , m_filteredQuery(arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SearchComboBehavior::filter(SearchComboState& state, ISearchableListDataSource& source){
    using namespace __hidden_ui_search_combo;
    const QuerySnapshot query{ state.m_query.instanceGeneration(), state.m_query.externalRevision(), state.m_query.revision(),
        state.m_query.compositionGeneration(), state.m_query.selectionGeneration(), state.m_query.anchor(), state.m_query.caret() };
    const u64 comboGeneration = state.m_combo.inputGeneration();
    const u64 previewGeneration = state.m_combo.listState().inputGeneration();
    const SourceSnapshot full = Snapshot(source);
    const IListDataSource& previousView = source.filtered();
    const SourceSnapshot previous = Snapshot(previousView);
    if(
        full.generation == 0u || full.revision == 0u || !Matches(state, query, comboGeneration, previewGeneration)
    )
        return false;
    const bool fullChanged = !state.m_filterValid || state.m_sourceGeneration != full.generation
        || state.m_sourceRevision != full.revision || state.m_sourceCount != full.count;
    const bool viewChanged = !state.m_filterValid || state.m_viewGeneration != previous.generation
        || state.m_viewRevision != previous.revision || state.m_viewCount != previous.count;
    const AStringView cachedQuery{ state.m_filteredQuery.data(), state.m_filteredQuery.size() };
    const bool queryChanged = !state.m_filterValid || cachedQuery != state.m_query.text();
    const bool rebuild = fullChanged || viewChanged || queryChanged;
    AString<Core::Alloc::GlobalArena> candidate(state.m_arena);
    if(rebuild){
        const AStringView text = state.m_query.text();
        if(!text.empty())
            candidate.assign(text.data(), text.size());
        if(!source.filter({ candidate.data(), candidate.size() }))
            return false;
    }
    const IListDataSource& view = source.filtered();
    const SourceSnapshot filtered = Snapshot(view);
    if(
        filtered.generation == 0u || filtered.revision == 0u || filtered.count > full.count
        || !Matches(view, filtered) || !Matches(source, full)
        || !Matches(state, query, comboGeneration, previewGeneration)
    )
        return false;
    const bool externalQueryChange = state.m_filterValid && (state.m_queryGeneration != query.generation
        || state.m_queryExternalRevision != query.externalRevision);
    if(rebuild || externalQueryChange){
        ListState& preview = ComboBehavior::preview(state.m_combo);
        if(!preview.scrollTo(0.0))
            return false;
        preview.select(state.m_combo.selectedKey());
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

