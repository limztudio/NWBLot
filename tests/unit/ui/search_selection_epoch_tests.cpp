// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "search_combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_search_selection_epoch_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiSearchComboTests;

namespace SelectionMutation{
    enum Enum : u8{ None, Identical, AwayAndBack };
};

class SelectionProbe final{
public:
    explicit SelectionProbe(EditModel& query)
        : m_query(query)
    {}


public:
    void arm(const SelectionMutation::Enum mutation){
        m_mutation = mutation;
        m_anchor = m_query.anchor();
        m_caret = m_query.caret();
    }

    [[nodiscard]] bool apply(){
        const SelectionMutation::Enum mutation = m_mutation;
        m_mutation = SelectionMutation::None;
        if(mutation == SelectionMutation::None)
            return true;
        ++m_calls;
        if(mutation == SelectionMutation::AwayAndBack && !m_query.setSelection(0u, 0u))
            return false;
        return m_query.setSelection(m_anchor, m_caret);
    }

    [[nodiscard]] u64 calls()const{ return m_calls; }


private:
    EditModel& m_query;
    SelectionMutation::Enum m_mutation = SelectionMutation::None;
    usize m_anchor = 0u;
    usize m_caret = 0u;
    u64 m_calls = 0u;
};

class SelectionView final : public IListDataSource{
public:
    SelectionView(const IListDataSource& source, EditModel& query)
        : m_source(source)
        , m_probe(query)
    {}


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ return m_source.instanceGeneration(); }
    [[nodiscard]] virtual u64 revision()const override{ return m_source.revision(); }
    [[nodiscard]] virtual u64 rowCount()const override{ return m_source.rowCount(); }
    [[nodiscard]] virtual u64 key(const u64 index)const override{ return m_source.key(index); }
    [[nodiscard]] virtual Expected<u64> indexOf(const u64 key)const override{ return m_source.indexOf(key); }
    [[nodiscard]] virtual Expected<u64> findEnabled(const u64 start, const bool reverse)const override{
        return m_source.findEnabled(start, reverse);
    }
    [[nodiscard]] virtual StringView text(const u64 index)const override{
        ++m_textCalls;
        if(!m_probe.apply())
            ADD_FAILURE() << "row callback selection change failed";
        return m_source.text(index);
    }
    [[nodiscard]] virtual bool enabled(const u64 index)const override{ return m_source.enabled(index); }

    void arm(const SelectionMutation::Enum mutation){ m_probe.arm(mutation); }
    [[nodiscard]] u64 mutationCalls()const{ return m_probe.calls(); }
    [[nodiscard]] u64 textCalls()const{ return m_textCalls; }


private:
    const IListDataSource& m_source;
    mutable SelectionProbe m_probe;
    mutable u64 m_textCalls = 0u;
};

class SelectionSource final : public ISearchableListDataSource{
public:
    SelectionSource(SearchSource& source, EditModel& query)
        : m_source(source)
        , m_probe(query)
        , m_view(source.filtered(), query)
    {}


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ return m_source.instanceGeneration(); }
    [[nodiscard]] virtual u64 revision()const override{ return m_source.revision(); }
    [[nodiscard]] virtual u64 rowCount()const override{ return m_source.rowCount(); }
    [[nodiscard]] virtual u64 key(const u64 index)const override{ return m_source.key(index); }
    [[nodiscard]] virtual Expected<u64> indexOf(const u64 key)const override{ return m_source.indexOf(key); }
    [[nodiscard]] virtual Expected<u64> findEnabled(const u64 start, const bool reverse)const override{
        return m_source.findEnabled(start, reverse);
    }
    [[nodiscard]] virtual StringView text(const u64 index)const override{ return m_source.text(index); }
    [[nodiscard]] virtual bool enabled(const u64 index)const override{ return m_source.enabled(index); }
    [[nodiscard]] virtual bool filter(const AStringView query)override{
        return m_probe.apply() && m_source.filter(query);
    }
    [[nodiscard]] virtual const IListDataSource& filtered()const override{ return m_view; }

    void armFilter(const SelectionMutation::Enum mutation){ m_probe.arm(mutation); }
    [[nodiscard]] u64 filterMutationCalls()const{ return m_probe.calls(); }
    [[nodiscard]] SelectionView& view(){ return m_view; }


private:
    SearchSource& m_source;
    SelectionProbe m_probe;
    SelectionView m_view;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSearchSelectionEpochTests : public SearchFixture{
public:
    UiSearchSelectionEpochTests()
        : m_epochSource(m_searchSource, m_search.query())
    {}


protected:
    [[nodiscard]] bool declareEpochSearch(const u64 generation){
        if(!begin(generation) || !m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 280.0f }))
            return false;
        m_searchResult = m_builder.searchComboBox("combo", m_epochSource, m_search, Options());
        return m_searchResult.combo.valid;
    }

    [[nodiscard]] bool acceptInitial(){
        m_search.combo().select(4u);
        m_search.combo().open();
        if(!m_search.query().setText("Second") || !m_search.query().setSelection(1u, 4u))
            return false;
        if(!declareEpochSearch(1u) || !finishPanel() || !m_context.commitFrame(1u))
            return false;
        const HitTarget* editor = target(query());
        if(!editor)
            return false;
        m_displayedEditor = editor->rectangle;
        m_displayedFocus = m_context.input().focus();
        m_queryRevision = m_search.query().revision();
        m_queryExternalRevision = m_search.query().externalRevision();
        m_compositionGeneration = m_search.query().compositionGeneration();
        m_selectionGeneration = m_search.query().selectionGeneration();
        return true;
    }

    void expectPreservedApplicationState(const bool focusRetired = false){
        EXPECT_EQ(m_search.query().text(), "Second");
        EXPECT_EQ(m_search.query().anchor(), 1u);
        EXPECT_EQ(m_search.query().caret(), 4u);
        EXPECT_EQ(m_search.query().revision(), m_queryRevision);
        EXPECT_EQ(m_search.query().externalRevision(), m_queryExternalRevision);
        EXPECT_EQ(m_search.query().compositionGeneration(), m_compositionGeneration);
        EXPECT_NE(m_search.query().selectionGeneration(), m_selectionGeneration);
        EXPECT_EQ(m_search.combo().selectedKey(), 4u);
        EXPECT_TRUE(m_search.combo().isOpen());
        EXPECT_TRUE(m_context.failed());
        EXPECT_FALSE(m_context.commitFrame(2u));
        EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
        if(focusRetired)
            EXPECT_FALSE(m_context.input().focus().valid());
        else
            EXPECT_EQ(m_context.input().focus(), m_displayedFocus);
        ASSERT_NE(target(query()), nullptr);
        ExpectRect(target(query())->rectangle, m_displayedEditor);
    }

    void verifyFilterRejection(const SelectionMutation::Enum mutation){
        ASSERT_TRUE(acceptInitial());
        const u64 loans = m_host.loans;
        const u64 rowCalls = m_epochSource.view().textCalls();
        ++m_searchSource.full.contentRevision;
        m_epochSource.armFilter(mutation);
        EXPECT_FALSE(declareEpochSearch(2u));
        EXPECT_EQ(m_epochSource.filterMutationCalls(), 1u);
        EXPECT_EQ(m_host.loans, loans);
        EXPECT_EQ(m_epochSource.view().textCalls(), rowCalls);
        EXPECT_EQ(m_searchSource.full.contentRevision, 2u);
        expectPreservedApplicationState();
    }

    void verifyDeferredPaintRejection(const SelectionMutation::Enum mutation){
        ASSERT_TRUE(acceptInitial());
        ASSERT_TRUE(declareEpochSearch(2u));
        const u64 rowCalls = m_epochSource.view().textCalls();
        m_epochSource.view().arm(mutation);
        EXPECT_FALSE(m_builder.endPanel());
        EXPECT_EQ(m_epochSource.view().mutationCalls(), 1u);
        EXPECT_GT(m_epochSource.view().textCalls(), rowCalls);
        expectPreservedApplicationState(true);
    }


private:
    SelectionSource m_epochSource;
    Rect m_displayedEditor;
    WidgetId m_displayedFocus;
    u64 m_queryRevision = 0u;
    u64 m_queryExternalRevision = 0u;
    u64 m_compositionGeneration = 0u;
    u64 m_selectionGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiSearchSelectionEpochTests, IdenticalAcceptedQuerySelectionDuringFilterRejectsBeforeEditorLoan){
    verifyFilterRejection(SelectionMutation::Identical);
}

TEST_F(UiSearchSelectionEpochTests, QuerySelectionAwayAndBackDuringFilterRejectsBeforeEditorLoan){
    verifyFilterRejection(SelectionMutation::AwayAndBack);
}

TEST_F(UiSearchSelectionEpochTests, IdenticalAcceptedQuerySelectionDuringRowPaintRejectsTheCandidate){
    verifyDeferredPaintRejection(SelectionMutation::Identical);
}

TEST_F(UiSearchSelectionEpochTests, QuerySelectionAwayAndBackDuringRowPaintRejectsTheCandidate){
    verifyDeferredPaintRejection(SelectionMutation::AwayAndBack);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

