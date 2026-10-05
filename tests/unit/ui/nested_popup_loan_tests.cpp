// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "search_combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_loan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiSearchComboTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class NestedMutationSource final : public ComboSource{
public:
    explicit NestedMutationSource(const u64 instance){
        generation = instance;
        count = 5u;
    }


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ ++metadataCalls; return generation; }

    [[nodiscard]] virtual u64 revision()const override{
        ++metadataCalls;
        if(selectListOnRevision){
            ListState* state = selectListOnRevision;
            selectListOnRevision = nullptr;
            state->select(4u);
        }
        return contentRevision;
    }

    [[nodiscard]] virtual u64 rowCount()const override{ ++metadataCalls; return count; }

    [[nodiscard]] virtual StringView text(const u64 index)const override{
        const StringView value = ComboSource::text(index);
        if(closePopupOnText){
            PopupState* state = closePopupOnText;
            closePopupOnText = nullptr;
            state->close();
        }
        if(reopenPopupOnText){
            PopupState* state = reopenPopupOnText;
            reopenPopupOnText = nullptr;
            state->close();
            state->open();
        }
        if(selectListOnText){
            ListState* state = selectListOnText;
            selectListOnText = nullptr;
            state->select(4u);
        }
        if(selectComboOnText){
            ComboState* state = selectComboOnText;
            selectComboOnText = nullptr;
            state->select(4u);
        }
        if(closeMenuOnText){
            ContextMenuState* state = closeMenuOnText;
            closeMenuOnText = nullptr;
            state->close();
        }
        if(queryOnText){
            EditModel* query = queryOnText;
            queryOnText = nullptr;
            if(!query->setText("callback"))
                ADD_FAILURE() << "query callback failed";
        }
        if(armSourceOnText){
            NestedMutationSource* source = armSourceOnText;
            armSourceOnText = nullptr;
            source->selectListOnRevision = armList;
        }
        return value;
    }

    void resetAllCounters()const{ resetCounters(); metadataCalls = 0u; }


public:
    mutable u64 metadataCalls = 0u;
    mutable PopupState* closePopupOnText = nullptr;
    mutable PopupState* reopenPopupOnText = nullptr;
    mutable ListState* selectListOnText = nullptr;
    mutable ListState* selectListOnRevision = nullptr;
    mutable ComboState* selectComboOnText = nullptr;
    mutable ContextMenuState* closeMenuOnText = nullptr;
    mutable EditModel* queryOnText = nullptr;
    mutable NestedMutationSource* armSourceOnText = nullptr;
    ListState* armList = nullptr;
};

class NestedSearchSource final : public ISearchableListDataSource{
public:
    explicit NestedSearchSource(Core::Alloc::GlobalArena& arena)
        : m_query(arena)
    {}


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ return full.instanceGeneration(); }
    [[nodiscard]] virtual u64 revision()const override{ return full.revision(); }
    [[nodiscard]] virtual u64 rowCount()const override{ return full.rowCount(); }
    [[nodiscard]] virtual u64 key(const u64 index)const override{ return full.key(index); }
    [[nodiscard]] virtual bool indexOf(const u64 key, u64& index)const override{ return full.indexOf(key, index); }
    [[nodiscard]] virtual bool findEnabled(const u64 start, const bool reverse, u64& index)const override{
        return full.findEnabled(start, reverse, index);
    }
    [[nodiscard]] virtual StringView text(const u64 index)const override{ return full.text(index); }
    [[nodiscard]] virtual bool enabled(const u64 index)const override{ return full.enabled(index); }

    [[nodiscard]] virtual bool filter(const AStringView query)override{
        ++filterCalls;
        const AStringView previous{ m_query.data(), m_query.size() };
        if(previous != query){
            m_query.assign(query.data(), query.size());
            ++view.contentRevision;
        }
        view.count = query.empty() ? full.count : 0u;
        return true;
    }

    [[nodiscard]] virtual const IListDataSource& filtered()const override{ ++filteredCalls; return view; }


private:
    AString<Core::Alloc::GlobalArena> m_query;


public:
    NestedMutationSource full{ 2201u };
    NestedMutationSource view{ 2202u };
    u64 filterCalls = 0u;
    mutable u64 filteredCalls = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupLoanTests : public WidgetFixture{
public:
    UiNestedPopupLoanTests()
        : m_searchSource(m_arena)
        , m_search(m_arena)
        , m_host(m_context)
    {}


protected:
    virtual void SetUp()override{
        WidgetFixture::SetUp();
        m_builder.setEditHost(&m_host);
        m_parent.open();
        m_child.open();
    }

    [[nodiscard]] static PopupOptions ParentOptions(){
        PopupOptions options;
        options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 300.0f, 380.0f };
        return options;
    }

    [[nodiscard]] static PopupOptions ChildOptions(){
        PopupOptions options;
        options.anchor = { 420.0f, 60.0f, 80.0f, 24.0f };
        options.size = { 340.0f, 380.0f };
        return options;
    }

    [[nodiscard]] static ListOptions ListOptions(){
        NWB::Impl::Ui::ListOptions options;
        options.height = { LayoutSizePolicy::Fixed, 150.0f };
        options.rowHeight = 24.0f;
        return options;
    }

    [[nodiscard]] bool beginParent(const u64 generation){
        return begin(generation) && m_builder.beginPopup("parent", m_parent, ParentOptions());
    }

    [[nodiscard]] bool finishRoot(){ return m_context.endRoot() && m_context.finishFrame(); }

    [[nodiscard]] bool acceptBase(const u64 generation){
        if(!begin(generation) || !m_builder.beginPanel("base", { 0.0f, 0.0f, 100.0f, 80.0f }))
            return false;
        if(!m_builder.label("label", "Baseline") || !m_builder.endPanel() || !finishRoot())
            return false;
        return m_context.commitFrame(generation);
    }

    [[nodiscard]] bool declareLists(const u64 generation){
        if(!beginParent(generation) || !m_builder.virtualList("list", m_parentSource, m_parentList, ListOptions()).valid)
            return false;
        if(!m_builder.beginPopup("child", m_child, ChildOptions())
            || !m_builder.virtualList("list", m_childSource, m_childList, ListOptions()).valid)
            return false;
        return m_builder.endPopup();
    }

    [[nodiscard]] bool acceptLists(const u64 generation){
        return declareLists(generation) && m_builder.endPopup() && finishRoot() && m_context.commitFrame(generation);
    }

    [[nodiscard]] bool declareChildCompounds(const u64 generation){
        if(!beginParent(generation) || !m_builder.beginPopup("child", m_child, ChildOptions()))
            return false;
        if(!m_builder.virtualList("list", m_childSource, m_childList, ListOptions()).valid
            || !m_builder.comboBox("combo", m_comboSource, m_combo, Options()).valid)
            return false;
        SearchComboOptions searchOptions;
        searchOptions.combo = Options();
        searchOptions.combo.popupHeight = 220.0f;
        if(!m_builder.searchComboBox("search", m_searchSource, m_search, searchOptions).combo.valid)
            return false;
        WidgetOptions anchorOptions;
        anchorOptions.width = { LayoutSizePolicy::Fixed, 220.0f };
        anchorOptions.height = { LayoutSizePolicy::Fixed, 30.0f };
        const bool activated = m_builder.button("anchor", "Menu", anchorOptions);
        if(activated)
            return false;
        if(!m_builder.contextMenu("menu", "anchor", m_menuSource, m_menu).valid)
            return false;
        return m_builder.endPopup();
    }

    void resetChildCounters(){
        m_childSource.resetAllCounters();
        m_comboSource.resetAllCounters();
        m_menuSource.resetAllCounters();
        m_searchSource.full.resetAllCounters();
        m_searchSource.view.resetAllCounters();
        m_searchSource.filterCalls = 0u;
        m_searchSource.filteredCalls = 0u;
        m_host.publications = 0u;
    }

    void expectNoChildCallbacks()const{
        for(const NestedMutationSource* source : { &m_childSource, &m_comboSource, &m_menuSource,
            &m_searchSource.full, &m_searchSource.view }){
            EXPECT_EQ(source->metadataCalls, 0u);
            EXPECT_EQ(source->keyCalls, 0u);
            EXPECT_EQ(source->lookupCalls, 0u);
            EXPECT_EQ(source->searchCalls, 0u);
            EXPECT_EQ(source->textCalls, 0u);
            EXPECT_EQ(source->enabledCalls, 0u);
        }
        EXPECT_EQ(m_searchSource.filterCalls, 0u);
        EXPECT_EQ(m_searchSource.filteredCalls, 0u);
        EXPECT_EQ(m_host.publications, 0u);
    }


protected:
    PopupState m_parent;
    PopupState m_child;
    NestedMutationSource m_parentSource{ 2101u };
    NestedMutationSource m_childSource{ 2102u };
    NestedMutationSource m_comboSource{ 2103u };
    NestedMutationSource m_menuSource{ 2104u };
    ListState m_parentList;
    ListState m_childList;
    ComboState m_combo;
    ContextMenuState m_menu;
    NestedSearchSource m_searchSource;
    SearchComboState m_search;
    SearchEditHost m_host;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiNestedPopupLoanTests, ChildEndDefersVisibleRowCallbacksUntilTheOuterPopupEnds){
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", m_childSource, m_childList, ListOptions()).valid);
    m_childSource.resetAllCounters();
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_EQ(m_childSource.metadataCalls, 0u);
    EXPECT_EQ(m_childSource.textCalls, 0u);
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_GT(m_childSource.textCalls, 0u);
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));
}

TEST_F(UiNestedPopupLoanTests, ExplicitChildListMutationAfterChildEndRejectsTheEnclosingLoan){
    ASSERT_TRUE(acceptLists(1u));
    ASSERT_TRUE(declareLists(2u));
    const u64 inputGeneration = m_childList.inputGeneration();
    m_childList.select(4u);
    EXPECT_NE(m_childList.inputGeneration(), inputGeneration);
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(m_childList.selectedKey(), 4u);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiNestedPopupLoanTests, ChildVisibleRowCallbackCannotMutateThePaintedParentListLoan){
    ASSERT_TRUE(acceptLists(1u));
    ASSERT_TRUE(declareLists(2u));
    const u64 inputGeneration = m_parentList.inputGeneration();
    m_childSource.selectListOnText = &m_parentList;
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(m_parentList.selectedKey(), 4u);
    EXPECT_NE(m_parentList.inputGeneration(), inputGeneration);
    EXPECT_EQ(m_childSource.selectListOnText, nullptr);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiNestedPopupLoanTests, ParentVisibleRowCallbackCannotMutateTheEndedChildListLoan){
    ASSERT_TRUE(acceptLists(1u));
    ASSERT_TRUE(declareLists(2u));
    const u64 inputGeneration = m_childList.inputGeneration();
    m_parentSource.selectListOnText = &m_childList;
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(m_childList.selectedKey(), 4u);
    EXPECT_NE(m_childList.inputGeneration(), inputGeneration);
    EXPECT_EQ(m_parentSource.selectListOnText, nullptr);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiNestedPopupLoanTests, ParentMetadataCallbackArmedByChildPaintStillRejectsThePaintedChildMutation){
    ASSERT_TRUE(acceptLists(1u));
    ASSERT_TRUE(declareLists(2u));
    const u64 inputGeneration = m_childList.inputGeneration();
    m_childSource.armSourceOnText = &m_parentSource;
    m_childSource.armList = &m_childList;
    m_childSource.textCalls = 0u;
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_GT(m_childSource.textCalls, 0u);
    EXPECT_EQ(m_childSource.armSourceOnText, nullptr);
    EXPECT_EQ(m_parentSource.selectListOnRevision, nullptr);
    EXPECT_EQ(m_childList.selectedKey(), 4u);
    EXPECT_NE(m_childList.inputGeneration(), inputGeneration);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiNestedPopupLoanTests, ChildSourceCallbackCannotMutateTheParentComboAfterItsFieldPaint){
    ASSERT_TRUE(acceptBase(1u));
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.comboBox("combo", m_comboSource, m_combo, Options()).valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", m_childSource, m_childList, ListOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    const u64 inputGeneration = m_combo.inputGeneration();
    m_childSource.selectComboOnText = &m_combo;
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(m_combo.selectedKey(), 4u);
    EXPECT_NE(m_combo.inputGeneration(), inputGeneration);
    EXPECT_GT(m_combo.bounds().width, 0.0f);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiNestedPopupLoanTests, ParentSourceCallbackCannotMutateTheEndedChildSearchQueryLoan){
    ASSERT_TRUE(acceptBase(1u));
    m_search.combo().open();
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.virtualList("list", m_parentSource, m_parentList, ListOptions()).valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    SearchComboOptions searchOptions;
    searchOptions.combo = Options();
    ASSERT_TRUE(m_builder.searchComboBox("search", m_searchSource, m_search, searchOptions).combo.valid);
    ASSERT_TRUE(m_builder.endPopup());
    const u64 externalRevision = m_search.query().externalRevision();
    m_parentSource.queryOnText = &m_search.query();
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(m_search.query().text(), "callback");
    EXPECT_GT(m_search.query().externalRevision(), externalRevision);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiNestedPopupLoanTests, ChildSourceCallbackCannotCloseTheParentContextMenuAndPublishTheFrame){
    ASSERT_TRUE(acceptBase(1u));
    ASSERT_TRUE(m_menu.open({ 80.0f, 120.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(beginParent(2u));
    EXPECT_FALSE(m_builder.button("anchor", "Commands"));
    ASSERT_TRUE(m_builder.contextMenu("menu", "anchor", m_menuSource, m_menu).valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", m_childSource, m_childList, ListOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    const u64 revision = m_menu.revision();
    m_childSource.closeMenuOnText = &m_menu;
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_GT(m_menu.revision(), revision);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiNestedPopupLoanTests, ClosingTheParentBeforeOuterEndSuppressesEveryDescendantSourceAndQueryPublication){
    m_combo.open();
    m_search.combo().open();
    ASSERT_TRUE(m_menu.open({ 510.0f, 280.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(declareChildCompounds(1u));
    resetChildCounters();
    m_parent.close();
    ASSERT_TRUE(m_builder.endPopup());
    expectNoChildCallbacks();
    ASSERT_TRUE(finishRoot());
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_TRUE(snapshot.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(m_context.input().targets().empty());
    EXPECT_FALSE(m_context.input().hasPopup());
}

TEST_F(UiNestedPopupLoanTests, ReopeningTheParentBeforeOuterEndSuppressesItsOldDescendantLoans){
    m_combo.open();
    m_search.combo().open();
    ASSERT_TRUE(m_menu.open({ 510.0f, 280.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(declareChildCompounds(1u));
    resetChildCounters();
    const u64 openGeneration = m_parent.openGeneration();
    m_parent.close();
    m_parent.open();
    EXPECT_NE(m_parent.openGeneration(), openGeneration);
    ASSERT_TRUE(m_builder.endPopup());
    expectNoChildCallbacks();
    EXPECT_TRUE(m_parent.isOpen());
    ASSERT_TRUE(finishRoot());
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_TRUE(snapshot.commands().empty());
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(m_context.input().targets().empty());
    EXPECT_FALSE(m_context.input().hasPopup());
}

TEST_F(UiNestedPopupLoanTests, ClosingTheEndedChildSkipsItsCallbacksAndKeepsParentControlsPainted){
    ASSERT_TRUE(beginParent(1u));
    EXPECT_FALSE(m_builder.button("before", "Parent before"));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", m_childSource, m_childList, ListOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    m_childSource.resetAllCounters();
    m_child.close();
    EXPECT_FALSE(m_builder.button("after", "Parent after"));
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_EQ(m_childSource.metadataCalls, 0u);
    EXPECT_EQ(m_childSource.textCalls, 0u);
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId parent = MakeWidgetId(MakeRootId(m_root), "parent");
    EXPECT_NE(target(MakeWidgetId(parent, "before")), nullptr);
    EXPECT_NE(target(MakeWidgetId(parent, "after")), nullptr);
    EXPECT_EQ(target(MakeWidgetId(parent, "child")), nullptr);
}

TEST_F(UiNestedPopupLoanTests, ReopeningTheEndedChildRetiresItsOldBodyBeforeSourceCallbacks){
    ASSERT_TRUE(beginParent(1u));
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", m_childSource, m_childList, ListOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    m_childSource.resetAllCounters();
    const u64 openGeneration = m_child.openGeneration();
    m_child.close();
    m_child.open();
    EXPECT_NE(m_child.openGeneration(), openGeneration);
    EXPECT_FALSE(m_builder.button("after", "Parent after"));
    ASSERT_TRUE(m_builder.endPopup());
    EXPECT_EQ(m_childSource.metadataCalls, 0u);
    EXPECT_EQ(m_childSource.textCalls, 0u);
    EXPECT_TRUE(m_child.isOpen());
    ASSERT_TRUE(finishRoot());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId parent = MakeWidgetId(MakeRootId(m_root), "parent");
    EXPECT_NE(target(MakeWidgetId(parent, "after")), nullptr);
    EXPECT_EQ(target(MakeWidgetId(parent, "child")), nullptr);
}

TEST_F(UiNestedPopupLoanTests, ParentFirstRowClosingItsPopupRejectsBeforeAnyDescendantTextCallback){
    ASSERT_TRUE(acceptLists(1u));
    ASSERT_TRUE(declareLists(2u));
    m_parentSource.resetAllCounters();
    m_childSource.resetAllCounters();
    m_parentSource.closePopupOnText = &m_parent;
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_FALSE(m_parent.isOpen());
    EXPECT_EQ(m_parentSource.closePopupOnText, nullptr);
    EXPECT_EQ(m_parentSource.textCalls, 1u);
    EXPECT_EQ(m_childSource.textCalls, 0u);
    EXPECT_EQ(m_childSource.metadataCalls, 0u);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiNestedPopupLoanTests, ChildFirstRowReopeningItsAncestorRejectsBeforeLaterRowsOrSiblingCallbacks){
    ASSERT_TRUE(acceptLists(1u));
    ASSERT_TRUE(declareLists(2u));
    PopupState sibling;
    sibling.open();
    NestedMutationSource siblingSource(2301u);
    ListState siblingList;
    ASSERT_TRUE(m_builder.beginPopup("sibling", sibling, ChildOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", siblingSource, siblingList, ListOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    m_parentSource.resetAllCounters();
    m_childSource.resetAllCounters();
    siblingSource.resetAllCounters();
    const u64 openGeneration = m_parent.openGeneration();
    m_childSource.reopenPopupOnText = &m_parent;
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_TRUE(m_parent.isOpen());
    EXPECT_NE(m_parent.openGeneration(), openGeneration);
    EXPECT_EQ(m_childSource.reopenPopupOnText, nullptr);
    EXPECT_GT(m_parentSource.textCalls, 0u);
    EXPECT_EQ(m_childSource.textCalls, 1u);
    EXPECT_EQ(siblingSource.textCalls, 0u);
    EXPECT_EQ(siblingSource.metadataCalls, 0u);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

