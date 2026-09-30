// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "combo.h"
#include "edit_box_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The source owns a cached stable-key view and copies the query; filtering must not change its full-source metadata.
interface ISearchableListDataSource : public IListDataSource{
    virtual ~ISearchableListDataSource()override = default;
    [[nodiscard]] virtual bool filter(AStringView query) = 0;
    [[nodiscard]] virtual const IListDataSource& filtered()const = 0;
};

struct SearchComboOptions{
    ComboOptions combo;
    f32 queryHeight = 36.0f;
    f32 queryGap = 4.0f;
};

struct SearchComboResult{
    ComboResult combo;
    bool queryChanged = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SearchComboState final : NoCopy{
    friend class Builder;
    friend class SearchComboBehavior;


public:
    explicit SearchComboState(Core::Alloc::GlobalArena& arena);


public:
    SearchComboState(SearchComboState&&) = delete;
    SearchComboState& operator=(SearchComboState&&) = delete;


public:
    [[nodiscard]] const ComboState& combo()const{ return m_combo; }
    [[nodiscard]] ComboState& combo(){ return m_combo; }
    [[nodiscard]] const EditModel& query()const{ return m_query; }
    [[nodiscard]] EditModel& query(){ return m_query; }
    [[nodiscard]] const EditBoxState& editorState()const{ return m_editor; }


private:
    Core::Alloc::GlobalArena& m_arena;
    ComboState m_combo;
    EditModel m_query;
    EditBoxState m_editor;
    AString<Core::Alloc::GlobalArena> m_filteredQuery;
    u64 m_sourceGeneration = 0u;
    u64 m_sourceRevision = 0u;
    u64 m_sourceCount = 0u;
    u64 m_viewGeneration = 0u;
    u64 m_viewRevision = 0u;
    u64 m_viewCount = 0u;
    u64 m_queryGeneration = 0u;
    u64 m_queryExternalRevision = 0u;
    u64 m_queryRevision = 0u;
    bool m_filterValid = false;
};

class SearchComboBehavior final{
public:
    // Reconcile the committed combo against the full source first; reconcile its preview against filtered() afterward.
    [[nodiscard]] static bool Filter(SearchComboState& state, ISearchableListDataSource& source);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

