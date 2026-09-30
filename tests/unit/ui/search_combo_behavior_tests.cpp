// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/search_combo.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_search_combo_behavior_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

namespace Mutation{
    enum Enum : u8{
        None, Query, SameQuery, QuerySelection, Selection, Open, Close, PreviewScroll,
        CompositionBegin, CompositionUpdate, CompositionCancel, CompositionCycle
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FilteredView final : public IListDataSource{
public:
    explicit FilteredView(SearchComboState& state)
        : m_state(state)
    {}


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ return generation; }
    [[nodiscard]] virtual u64 revision()const override{ return revisionValue; }

    [[nodiscard]] virtual u64 rowCount()const override{
        if(changeQueryOnCount){
            changeQueryOnCount = false;
            if(!m_state.query().setText("callback"))
                ADD_FAILURE() << "query callback failed";
        }
        return count;
    }

    [[nodiscard]] virtual u64 key(const u64 index)const override{ return index < count ? start + index + 1u : 0u; }

    [[nodiscard]] virtual bool indexOf(const u64 keyValue, u64& index)const override{
        if(keyValue <= start || keyValue - start > count)
            return false;
        index = keyValue - start - 1u;
        return true;
    }

    [[nodiscard]] virtual bool findEnabled(const u64 first, bool, u64& index)const override{
        if(first >= count)
            return false;
        index = first;
        return true;
    }

    [[nodiscard]] virtual StringView text(u64)const override{ return "match"; }


public:
    u64 generation = 97u;
    u64 revisionValue = 1u;
    u64 start = 0u;
    u64 count = 0u;
    mutable bool changeQueryOnCount = false;


private:
    SearchComboState& m_state;
};

class SearchSource final : public ISearchableListDataSource{
public:
    SearchSource(Core::Alloc::GlobalArena& arena, SearchComboState& state)
        : view(state)
        , queryBytes(arena)
        , m_state(state)
    {}


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{
        if(changeQueryOnGeneration){
            changeQueryOnGeneration = false;
            if(!m_state.query().setText("callback"))
                ADD_FAILURE() << "query callback failed";
        }
        return generation;
    }

    [[nodiscard]] virtual u64 revision()const override{ return revisionValue; }
    [[nodiscard]] virtual u64 rowCount()const override{ return count; }

    [[nodiscard]] virtual u64 key(const u64 index)const override{
        ++keyCalls;
        return index < count ? index + 1u : 0u;
    }

    [[nodiscard]] virtual bool indexOf(const u64 keyValue, u64& index)const override{
        ++lookupCalls;
        if(keyValue == 0u || keyValue > count)
            return false;
        index = keyValue - 1u;
        return true;
    }

    [[nodiscard]] virtual bool findEnabled(const u64 start, bool, u64& index)const override{
        if(start >= count)
            return false;
        index = start;
        return true;
    }

    [[nodiscard]] virtual StringView text(u64)const override{ return "full row"; }

    [[nodiscard]] virtual bool filter(const AStringView query)override{
        ++filterCalls;
        if(failFilter)
            return false;
        const AStringView old{ queryBytes.data(), queryBytes.size() };
        if(!m_valid || old != query || m_generation != generation || m_revision != revisionValue || m_count != count){
            queryBytes.clear();
            if(!query.empty())
                queryBytes.assign(query.data(), query.size());
            view.start = query == "tail" ? count - Min<u64>(3u, count) : 0u;
            view.count = query == "none" ? 0u : count - view.start;
            ++view.revisionValue;
            ++rebuilds;
            m_generation = generation;
            m_revision = revisionValue;
            m_count = count;
            m_valid = true;
        }
        if(changeFullGeneration)
            ++generation;
        if(changeFullRevision)
            ++revisionValue;
        if(changeFullCount)
            ++count;
        if(invalidViewIdentity)
            view.revisionValue = 0u;
        if(oversizedView)
            view.count = count + 1u;
        mutate();
        return true;
    }

    [[nodiscard]] virtual const IListDataSource& filtered()const override{ return view; }


private:
    void mutate(){
        const Mutation::Enum mutation = onFilter;
        onFilter = Mutation::None;
        switch(mutation){
        case Mutation::None:
            break;
        case Mutation::Query:
            if(!m_state.query().setText("callback"))
                ADD_FAILURE() << "query callback failed";
            break;
        case Mutation::SameQuery:
            if(!m_state.query().setText(m_state.query().text()))
                ADD_FAILURE() << "query callback failed";
            break;
        case Mutation::QuerySelection:
            if(!m_state.query().selectAll())
                ADD_FAILURE() << "query selection callback failed";
            break;
        case Mutation::Selection:
            m_state.combo().select(8u);
            break;
        case Mutation::Open:
            m_state.combo().open();
            break;
        case Mutation::Close:
            m_state.combo().close();
            break;
        case Mutation::PreviewScroll:
            if(!ComboBehavior::Preview(m_state.combo()).scrollTo(64.0))
                ADD_FAILURE() << "preview callback failed";
            break;
        case Mutation::CompositionBegin:
            if(!m_state.query().beginComposition())
                ADD_FAILURE() << "composition callback failed";
            break;
        case Mutation::CompositionUpdate:
            if(!m_state.query().updateComposition("preedit", 0u, 7u))
                ADD_FAILURE() << "composition callback failed";
            break;
        case Mutation::CompositionCancel:
            m_state.query().cancelComposition();
            break;
        case Mutation::CompositionCycle:
            if(!m_state.query().beginComposition() || !m_state.query().updateComposition("preedit", 0u, 7u))
                ADD_FAILURE() << "composition callback failed";
            m_state.query().cancelComposition();
            break;
        }
    }


public:
    FilteredView view;
    AString<Core::Alloc::GlobalArena> queryBytes;
    u64 generation = 71u;
    u64 revisionValue = 1u;
    u64 count = 100000u;
    u64 filterCalls = 0u;
    u64 rebuilds = 0u;
    mutable u64 keyCalls = 0u;
    mutable u64 lookupCalls = 0u;
    bool failFilter = false;
    bool changeFullGeneration = false;
    bool changeFullRevision = false;
    bool changeFullCount = false;
    bool invalidViewIdentity = false;
    bool oversizedView = false;
    mutable bool changeQueryOnGeneration = false;
    Mutation::Enum onFilter = Mutation::None;


private:
    SearchComboState& m_state;
    u64 m_generation = 0u;
    u64 m_revision = 0u;
    u64 m_count = 0u;
    bool m_valid = false;
};

class UiSearchComboBehaviorTests : public testing::Test{
public:
    UiSearchComboBehaviorTests()
        : m_arena(Name("tests/ui/search_combo"))
        , m_state(m_arena)
        , m_source(m_arena, m_state)
    {}


protected:
    Core::Alloc::GlobalArena m_arena;
    SearchComboState m_state;
    SearchSource m_source;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiSearchComboBehaviorTests, InitialFilterAndCacheHitsDoNotInspectTheFullDataset){
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_source.filterCalls, 1u);
    EXPECT_EQ(m_source.rebuilds, 1u);
    EXPECT_EQ(m_source.queryBytes.size(), 0u);
    EXPECT_EQ(m_source.view.count, 100000u);
    EXPECT_EQ(m_source.keyCalls, 0u);
    EXPECT_EQ(m_source.lookupCalls, 0u);
}

TEST_F(UiSearchComboBehaviorTests, QueryChangeRetiresPreviewAndScrollWithoutChangingCommittedSelection){
    m_state.combo().select(5u);
    ASSERT_TRUE(ComboBehavior::Reconcile(m_state.combo(), m_source));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    ListState& preview = ComboBehavior::Preview(m_state.combo());
    ASSERT_TRUE(ListBehavior::Reconcile(preview, m_source.filtered()));
    preview.select(10u);
    ASSERT_TRUE(preview.scrollTo(96.0));
    const u64 previewGeneration = preview.inputGeneration();
    ASSERT_TRUE(m_state.query().setText("tail"));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_state.combo().selectedKey(), 5u);
    EXPECT_NE(preview.inputGeneration(), previewGeneration);
    EXPECT_DOUBLE_EQ(preview.scrollOffset(), 0.0);
    ASSERT_TRUE(ListBehavior::Reconcile(preview, m_source.filtered()));
    EXPECT_EQ(preview.selectedKey(), 0u);
    EXPECT_EQ(preview.cursorKey(), 0u);
    EXPECT_EQ(m_state.combo().selectedKey(), 5u);
    EXPECT_EQ(m_source.filterCalls, 2u);
}

TEST_F(UiSearchComboBehaviorTests, IncludedCommittedKeyReseedsTheFilteredPreview){
    m_state.combo().select(99999u);
    ASSERT_TRUE(ComboBehavior::Reconcile(m_state.combo(), m_source));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    ASSERT_TRUE(ListBehavior::Reconcile(ComboBehavior::Preview(m_state.combo()), m_source.filtered()));
    ASSERT_TRUE(m_state.query().setText("tail"));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    ASSERT_TRUE(ListBehavior::Reconcile(ComboBehavior::Preview(m_state.combo()), m_source.filtered()));
    EXPECT_EQ(m_state.combo().selectedKey(), 99999u);
    EXPECT_EQ(m_state.combo().listState().cursorKey(), 99999u);
    EXPECT_EQ(m_source.view.count, 3u);
}

TEST_F(UiSearchComboBehaviorTests, NoResultsPreserveTheFullSourceSelectionAndOpenPopup){
    m_state.combo().select(5u);
    ASSERT_TRUE(ComboBehavior::Reconcile(m_state.combo(), m_source));
    m_state.combo().open();
    ASSERT_TRUE(m_state.query().setText("none"));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    ASSERT_TRUE(ListBehavior::Reconcile(ComboBehavior::Preview(m_state.combo()), m_source.filtered()));
    EXPECT_EQ(m_state.combo().selectedKey(), 5u);
    EXPECT_TRUE(m_state.combo().isOpen());
    EXPECT_EQ(m_state.combo().listState().cursorKey(), 0u);
    EXPECT_EQ(m_source.view.count, 0u);
}

TEST_F(UiSearchComboBehaviorTests, FullSourceRevisionChangeRebuildsAndRetiresPreviewWithSameQuery){
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    ListState& preview = ComboBehavior::Preview(m_state.combo());
    preview.select(8u);
    ASSERT_TRUE(preview.scrollTo(96.0));
    const u64 inputGeneration = preview.inputGeneration();
    ++m_source.revisionValue;
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_source.filterCalls, 2u);
    EXPECT_EQ(m_source.rebuilds, 2u);
    EXPECT_NE(preview.inputGeneration(), inputGeneration);
    EXPECT_EQ(preview.cursorKey(), m_state.combo().selectedKey());
    EXPECT_DOUBLE_EQ(preview.scrollOffset(), 0.0);
}

TEST_F(UiSearchComboBehaviorTests, FullDatasetShrinkRebuildsAStaleLargerView){
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    ASSERT_EQ(m_source.view.count, 100000u);
    m_source.count = 10u;
    ++m_source.revisionValue;
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_source.view.count, 10u);
    EXPECT_EQ(m_source.filterCalls, 2u);
}

TEST_F(UiSearchComboBehaviorTests, FullSourceReplacementRebuildsWithoutHoldingAnySourcePointer){
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    SearchSource replacement(m_arena, m_state);
    replacement.generation = 72u;
    replacement.count = 12u;
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, replacement));
    EXPECT_EQ(replacement.filterCalls, 1u);
    EXPECT_EQ(replacement.view.count, 12u);
    EXPECT_EQ(m_source.filterCalls, 1u);
}

TEST_F(UiSearchComboBehaviorTests, SameTextApplicationAssignmentFencesPreviewWithoutRebuildingSource){
    ASSERT_TRUE(m_state.query().setText("tail"));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    ListState& preview = ComboBehavior::Preview(m_state.combo());
    const u64 previewGeneration = preview.inputGeneration();
    ASSERT_TRUE(m_state.query().setText("tail"));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_NE(preview.inputGeneration(), previewGeneration);
    EXPECT_EQ(m_source.filterCalls, 1u);
    EXPECT_EQ(m_source.rebuilds, 1u);
}

TEST_F(UiSearchComboBehaviorTests, SelectionAndPreeditUpdatesDoNotFilterOrResetPreview){
    ASSERT_TRUE(m_state.query().setText("all"));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    const u64 previewGeneration = m_state.combo().listState().inputGeneration();
    ASSERT_TRUE(m_state.query().selectAll());
    ASSERT_TRUE(m_state.query().beginComposition());
    ASSERT_TRUE(m_state.query().updateComposition("tail", 0u, 4u));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(AStringView(m_source.queryBytes.data(), m_source.queryBytes.size()), "all");
    EXPECT_EQ(m_state.combo().listState().inputGeneration(), previewGeneration);
    EXPECT_EQ(m_source.filterCalls, 1u);
    m_state.query().cancelComposition();
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_source.filterCalls, 1u);
}

TEST_F(UiSearchComboBehaviorTests, OnlyCommittedImeTextChangesTheFilteredQuery){
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    ASSERT_TRUE(m_state.query().beginComposition());
    ASSERT_TRUE(m_state.query().updateComposition("tail", 0u, 4u));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_source.filterCalls, 1u);
    ASSERT_TRUE(m_state.query().commitComposition("tail"));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_source.filterCalls, 2u);
    EXPECT_EQ(AStringView(m_source.queryBytes.data(), m_source.queryBytes.size()), "tail");
    EXPECT_EQ(m_source.view.count, 3u);
}

TEST_F(UiSearchComboBehaviorTests, QueryBytesAreCopiedExactlyIncludingUtf8){
    const AStringView query = "\xEA\xB2\x80\xEC\x83\x89";
    ASSERT_TRUE(m_state.query().setText(query));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(AStringView(m_source.queryBytes.data(), m_source.queryBytes.size()), query);
    ASSERT_TRUE(m_state.query().setText("tail"));
    EXPECT_EQ(AStringView(m_source.queryBytes.data(), m_source.queryBytes.size()), query);
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(AStringView(m_source.queryBytes.data(), m_source.queryBytes.size()), "tail");
}

TEST_F(UiSearchComboBehaviorTests, IndependentViewInvalidationReassertsTheCachedQuery){
    ASSERT_TRUE(m_state.query().setText("tail"));
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    const u64 previewGeneration = m_state.combo().listState().inputGeneration();
    ++m_source.view.revisionValue;
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_source.filterCalls, 2u);
    EXPECT_EQ(m_source.rebuilds, 1u);
    EXPECT_NE(m_state.combo().listState().inputGeneration(), previewGeneration);
}

TEST_F(UiSearchComboBehaviorTests, FailedFilterDoesNotPublishCacheOrResetPreviewAndCanRetry){
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    ListState& preview = ComboBehavior::Preview(m_state.combo());
    preview.select(8u);
    ASSERT_TRUE(preview.scrollTo(96.0));
    const u64 previewGeneration = preview.inputGeneration();
    ASSERT_TRUE(m_state.query().setText("tail"));
    m_source.failFilter = true;
    EXPECT_FALSE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(preview.inputGeneration(), previewGeneration);
    EXPECT_EQ(preview.cursorKey(), 8u);
    EXPECT_DOUBLE_EQ(preview.scrollOffset(), 96.0);
    m_source.failFilter = false;
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_source.filterCalls, 3u);
    EXPECT_EQ(m_source.view.count, 3u);
}

TEST_F(UiSearchComboBehaviorTests, InvalidFullIdentityRejectsWithoutCallingFilter){
    m_source.generation = 0u;
    EXPECT_FALSE(SearchComboBehavior::Filter(m_state, m_source));
    m_source.generation = 71u;
    m_source.revisionValue = 0u;
    EXPECT_FALSE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_source.filterCalls, 0u);
}

TEST_F(UiSearchComboBehaviorTests, FilterMayNotChangeFullGenerationRevisionOrCount){
    for(const u8 change : { 0u, 1u, 2u }){
        SearchComboState state(m_arena);
        SearchSource source(m_arena, state);
        state.combo().select(5u);
        const u64 previewGeneration = state.combo().listState().inputGeneration();
        source.changeFullGeneration = change == 0u;
        source.changeFullRevision = change == 1u;
        source.changeFullCount = change == 2u;
        EXPECT_FALSE(SearchComboBehavior::Filter(state, source));
        EXPECT_EQ(state.combo().selectedKey(), 5u);
        EXPECT_EQ(state.combo().listState().inputGeneration(), previewGeneration);
    }
}

TEST_F(UiSearchComboBehaviorTests, ResultingViewMustHaveValidIdentityAndBeBoundedByTheFullDataset){
    m_source.invalidViewIdentity = true;
    const u64 previewGeneration = m_state.combo().listState().inputGeneration();
    EXPECT_FALSE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_state.combo().listState().inputGeneration(), previewGeneration);
    m_source.invalidViewIdentity = false;
    m_source.view.revisionValue = 1u;
    m_source.oversizedView = true;
    EXPECT_FALSE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_state.combo().listState().inputGeneration(), previewGeneration);
}

TEST_F(UiSearchComboBehaviorTests, ReentrantQueryAssignmentPreservesTheCallbackQueryAndExistingPreview){
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    ListState& preview = ComboBehavior::Preview(m_state.combo());
    preview.select(8u);
    ASSERT_TRUE(preview.scrollTo(96.0));
    const u64 previewGeneration = preview.inputGeneration();
    ASSERT_TRUE(m_state.query().setText("tail"));
    m_source.onFilter = Mutation::Query;
    EXPECT_FALSE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_state.query().text(), "callback");
    EXPECT_EQ(preview.inputGeneration(), previewGeneration);
    EXPECT_EQ(preview.cursorKey(), 8u);
    EXPECT_DOUBLE_EQ(preview.scrollOffset(), 96.0);
    ASSERT_TRUE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(AStringView(m_source.queryBytes.data(), m_source.queryBytes.size()), "callback");
}

TEST_F(UiSearchComboBehaviorTests, SameQueryCallbackAssignmentStillInvalidatesTheFilterLoan){
    ASSERT_TRUE(m_state.query().setText("tail"));
    const u64 externalRevision = m_state.query().externalRevision();
    const u64 previewGeneration = m_state.combo().listState().inputGeneration();
    m_source.onFilter = Mutation::SameQuery;
    EXPECT_FALSE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_GT(m_state.query().externalRevision(), externalRevision);
    EXPECT_EQ(m_state.query().text(), "tail");
    EXPECT_EQ(m_state.combo().listState().inputGeneration(), previewGeneration);
}

TEST_F(UiSearchComboBehaviorTests, CompositionCallbacksInvalidateTheLoanWithoutChangingCommittedQuery){
    for(const Mutation::Enum mutation : {
        Mutation::CompositionBegin, Mutation::CompositionUpdate, Mutation::CompositionCancel
    }){
        SearchComboState state(m_arena);
        SearchSource source(m_arena, state);
        ASSERT_TRUE(state.query().setText("tail"));
        if(mutation != Mutation::CompositionBegin)
            ASSERT_TRUE(state.query().beginComposition());
        const u64 revision = state.query().revision();
        const u64 externalRevision = state.query().externalRevision();
        const u64 compositionGeneration = state.query().compositionGeneration();
        const u64 previewGeneration = state.combo().listState().inputGeneration();
        source.onFilter = mutation;
        EXPECT_FALSE(SearchComboBehavior::Filter(state, source));
        EXPECT_EQ(state.query().text(), "tail");
        EXPECT_EQ(state.query().revision(), revision);
        EXPECT_EQ(state.query().externalRevision(), externalRevision);
        EXPECT_NE(state.query().compositionGeneration(), compositionGeneration);
        EXPECT_EQ(state.combo().listState().inputGeneration(), previewGeneration);
        EXPECT_EQ(state.query().composition().active, mutation != Mutation::CompositionCancel);
    }
}

TEST_F(UiSearchComboBehaviorTests, ReentrantQuerySelectionRejectsTheLoanAndKeepsTheCallbackSelection){
    ASSERT_TRUE(m_state.query().setText("tail"));
    const u64 revision = m_state.query().revision();
    const u64 externalRevision = m_state.query().externalRevision();
    const u64 previewGeneration = m_state.combo().listState().inputGeneration();
    m_source.onFilter = Mutation::QuerySelection;
    EXPECT_FALSE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_state.query().text(), "tail");
    EXPECT_EQ(m_state.query().revision(), revision);
    EXPECT_EQ(m_state.query().externalRevision(), externalRevision);
    EXPECT_EQ(m_state.query().anchor(), 0u);
    EXPECT_EQ(m_state.query().caret(), 4u);
    EXPECT_EQ(m_state.combo().listState().inputGeneration(), previewGeneration);
}

TEST_F(UiSearchComboBehaviorTests, CompositionBeginCancelCycleCannotRestoreAnOldFilterLoan){
    ASSERT_TRUE(m_state.query().setText("tail"));
    const u64 compositionGeneration = m_state.query().compositionGeneration();
    const u64 previewGeneration = m_state.combo().listState().inputGeneration();
    m_source.onFilter = Mutation::CompositionCycle;
    EXPECT_FALSE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_FALSE(m_state.query().composition().active);
    EXPECT_EQ(m_state.query().text(), "tail");
    EXPECT_NE(m_state.query().compositionGeneration(), compositionGeneration);
    EXPECT_EQ(m_state.combo().listState().inputGeneration(), previewGeneration);
}

TEST_F(UiSearchComboBehaviorTests, ReentrantComboSelectionAndPopupChangesRemainAuthoritative){
    for(const Mutation::Enum mutation : { Mutation::Selection, Mutation::Open, Mutation::Close }){
        SearchComboState state(m_arena);
        SearchSource source(m_arena, state);
        state.combo().select(5u);
        state.combo().open();
        const u64 inputGeneration = state.combo().inputGeneration();
        source.onFilter = mutation;
        EXPECT_FALSE(SearchComboBehavior::Filter(state, source));
        EXPECT_NE(state.combo().inputGeneration(), inputGeneration);
        EXPECT_EQ(state.combo().selectedKey(), mutation == Mutation::Selection ? 8u : 5u);
        EXPECT_EQ(state.combo().isOpen(), mutation == Mutation::Open);
    }
}

TEST_F(UiSearchComboBehaviorTests, ReentrantPreviewScrollCannotBeOverwrittenByFilterReset){
    m_source.onFilter = Mutation::PreviewScroll;
    EXPECT_FALSE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_DOUBLE_EQ(m_state.combo().listState().scrollOffset(), 64.0);
}

TEST_F(UiSearchComboBehaviorTests, FullAndViewMetadataCallbacksAreIncludedInTheQueryFence){
    m_source.changeQueryOnGeneration = true;
    EXPECT_FALSE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_state.query().text(), "callback");
    EXPECT_EQ(m_source.filterCalls, 0u);
    ASSERT_TRUE(m_state.query().setText("tail"));
    m_source.view.changeQueryOnCount = true;
    EXPECT_FALSE(SearchComboBehavior::Filter(m_state, m_source));
    EXPECT_EQ(m_state.query().text(), "callback");
    EXPECT_EQ(m_source.filterCalls, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

