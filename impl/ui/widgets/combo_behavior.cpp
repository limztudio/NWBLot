// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "combo.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_combo_behavior{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ResolveKey(const IListDataSource& source, const u64 count, const u64 key, bool& present){
    present = false;
    u64 index = 0u;
    if(key == 0u || count == 0u || !source.indexOf(key, index))
        return true;
    if(index >= count || source.key(index) != key)
        return false;
    present = source.enabled(index);
    return true;
}

[[nodiscard]] static bool SourceMatches(const IListDataSource& source, const u64 generation,
    const u64 revision, const u64 count){
    return
        generation != 0u && revision != 0u && source.instanceGeneration() == generation
        && source.revision() == revision && source.rowCount() == count
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ComboBehavior::Bind(ComboState& state, const WidgetId owner, const u64 declarationGeneration){
    if(!owner.valid() || declarationGeneration == 0u)
        return false;
    if(state.m_owner.valid() && (state.m_owner != owner || state.m_ownerDeclaration != declarationGeneration))
        Close(state);
    state.m_owner = owner;
    state.m_ownerDeclaration = declarationGeneration;
    return true;
}

bool ComboBehavior::Reconcile(ComboState& state, const IListDataSource& source){
    using namespace __hidden_ui_combo_behavior;
    const u64 inputGeneration = state.m_inputGeneration;
    const u64 generation = source.instanceGeneration();
    const u64 revision = source.revision();
    const u64 count = source.rowCount();
    if(generation == 0u || revision == 0u)
        return false;
    const bool replacement = state.m_sourceGeneration != 0u && state.m_sourceGeneration != generation;
    const u64 selected = replacement ? 0u : state.m_selectedKey;
    bool present = false;
    if(!ResolveKey(source, count, selected, present))
        return false;
    if(!SourceMatches(source, generation, revision, count) || state.m_inputGeneration != inputGeneration)
        return false;
    if(replacement && !state.m_list.scrollTo(0.0))
        return false;
    state.m_selectedKey = present ? selected : 0u;
    state.m_sourceGeneration = generation;
    state.m_sourceRevision = revision;
    if(replacement)
        Close(state);
    return true;
}

void ComboBehavior::Open(ComboState& state){
    state.m_list.select(state.m_selectedKey);
    state.m_popup.close();
    state.m_popup.open();
}

void ComboBehavior::Close(ComboState& state){
    state.m_list.select(state.m_selectedKey);
    state.m_popup.close();
}

bool ComboBehavior::Commit(ComboState& state, const IListDataSource& source, const u64 key){
    using namespace __hidden_ui_combo_behavior;
    if(!state.m_popup.isOpen() || key == 0u)
        return false;
    const u64 inputGeneration = state.m_inputGeneration;
    const u64 generation = state.m_sourceGeneration;
    const u64 revision = state.m_sourceRevision;
    const u64 count = source.rowCount();
    if(!SourceMatches(source, generation, revision, count) || state.m_inputGeneration != inputGeneration)
        return false;
    bool present = false;
    if(!ResolveKey(source, count, key, present) || !present)
        return false;
    if(
        !SourceMatches(source, generation, revision, count) || state.m_inputGeneration != inputGeneration
        || !state.m_popup.isOpen()
    )
        return false;
    state.m_selectedKey = key;
    Close(state);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

