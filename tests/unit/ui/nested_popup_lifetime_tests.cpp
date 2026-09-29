// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_tools_fixture.h"
#include "search_combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_lifetime_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiPopupToolsTests;
using namespace NWB::UiSearchComboTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupLifetimeTests : public SearchFixture{
protected:
    virtual void SetUp()override{
        SearchFixture::SetUp();
        m_source.count = 5u;
        m_searchSource.full.count = 5u;
        m_parent.open();
        m_child.open();
        for(PopupState& state : m_deeper)
            state.open();
    }

    [[nodiscard]] static PopupOptions parentOptions(){
        PopupOptions options;
        options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 300.0f, 380.0f };
        return options;
    }

    [[nodiscard]] static PopupOptions childOptions(){
        PopupOptions options;
        options.anchor = { 420.0f, 60.0f, 80.0f, 24.0f };
        options.size = { 340.0f, 380.0f };
        return options;
    }

    [[nodiscard]] WidgetId parentId()const{ return MakeWidgetId(MakeRootId(m_root), "parent"); }
    [[nodiscard]] WidgetId childId()const{ return MakeWidgetId(parentId(), "child"); }
    [[nodiscard]] WidgetId fieldId()const{ return MakeWidgetId(parentId(), "combo"); }
    [[nodiscard]] WidgetId menuRows()const{ return MakeWidgetId(MakeWidgetId(parentId(), "menu"), "rows"); }

    [[nodiscard]] bool beginParent(const u64 generation){
        if(!begin(generation) || !m_builder.beginPopup("parent", m_parent, parentOptions()))
            return false;
        const bool activated = m_builder.button("before", "Before");
        return !activated;
    }

    [[nodiscard]] bool finishParent(const u64 generation){
        const bool activated = m_builder.button("after", "After");
        return !activated && m_builder.endPopup() && m_context.endRoot()
            && m_context.finishFrame() && m_context.commitFrame(generation);
    }

    [[nodiscard]] bool acceptPlain(const u64 generation){
        if(!beginParent(generation))
            return false;
        m_childVisible = m_builder.beginPopup("child", m_child, childOptions());
        if(m_childVisible){
            const bool activated = m_builder.button("apply", "Child");
            if(activated || !m_builder.endPopup())
                return false;
        }
        else if(m_context.failed())
            return false;
        return finishParent(generation);
    }

    [[nodiscard]] bool acceptCombo(const u64 generation){
        if(!beginParent(generation))
            return false;
        m_result = m_builder.comboBox("combo", m_source, m_state, Options());
        return m_result.valid && finishParent(generation);
    }

    [[nodiscard]] bool acceptSearch(const u64 generation){
        if(!beginParent(generation))
            return false;
        m_searchResult = m_builder.searchComboBox("combo", m_searchSource, m_search, options());
        return m_searchResult.combo.valid && finishParent(generation);
    }

    [[nodiscard]] bool acceptMenu(const u64 generation){
        if(!beginParent(generation))
            return false;
        const bool activated = m_builder.button("anchor", "Commands");
        if(activated)
            return false;
        m_menuResult = m_builder.contextMenu("menu", "anchor", m_menuSource, m_menu);
        return m_menuResult.valid && finishParent(generation);
    }

    [[nodiscard]] bool declareReserved(const u64 generation, const bool overflow){
        if(!beginParent(generation) || !m_builder.comboBox("combo", m_source, m_state, Options()).valid)
            return false;
        const bool activated = m_builder.button("anchor", "Commands");
        if(activated || !m_builder.contextMenu("menu", "anchor", m_menuSource, m_menu).valid)
            return false;
        if(!m_builder.beginPopup("child", m_child, childOptions())
            || !m_builder.comboBox("combo", m_source, m_childCombo, Options()).valid)
            return false;
        for(usize index = 0u; index < 3u; ++index){
            if(!m_builder.beginPopup("deep", m_deeper[index], childOptions()))
                return false;
        }
        if(overflow)
            return m_builder.beginPopup("deep", m_deeper[3u], childOptions());
        const bool leafActivated = m_builder.button("leaf", "Deepest");
        if(leafActivated)
            return false;
        for(usize index = 0u; index < 4u; ++index){
            if(!m_builder.endPopup())
                return false;
        }
        const bool afterActivated = m_builder.button("after", "Parent after");
        return !afterActivated && m_builder.endPopup() && m_context.endRoot() && m_context.finishFrame();
    }

    void reopenParent(){ m_parent.close(); m_parent.open(); }

    void expectParentControls()const{
        const HitTarget* before = target(MakeWidgetId(parentId(), "before"));
        const HitTarget* after = target(MakeWidgetId(parentId(), "after"));
        ASSERT_NE(before, nullptr);
        ASSERT_NE(after, nullptr);
        EXPECT_EQ(before->popup, after->popup);
        EXPECT_EQ(before->popup.openGeneration, m_parent.openGeneration());
        EXPECT_TRUE(m_parent.isOpen());
    }


protected:
    PopupState m_parent;
    PopupState m_child;
    Array<PopupState, 4u> m_deeper;
    ComboState m_childCombo;
    ContextMenuState m_menu;
    PopupToolsSource m_menuSource;
    ContextMenuResult m_menuResult;
    bool m_childVisible = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiNestedPopupLifetimeTests, ParentReopeningRetiresTheOldPlainChildAndKeepsParentControls){
    ASSERT_TRUE(acceptPlain(1u));
    ASSERT_TRUE(m_childVisible);
    ASSERT_NE(target(childId()), nullptr);
    const PopupToken oldChild = target(childId())->popup;
    const u64 oldParentGeneration = m_parent.openGeneration();
    reopenParent();
    EXPECT_NE(m_parent.openGeneration(), oldParentGeneration);
    ASSERT_TRUE(acceptPlain(2u));
    EXPECT_FALSE(m_childVisible);
    EXPECT_FALSE(m_child.isOpen());
    EXPECT_EQ(target(childId()), nullptr);
    EXPECT_EQ(target(MakeWidgetId(childId(), "apply")), nullptr);
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_TRUE(m_context.input().controlActions().empty());
    EXPECT_EQ(oldChild.openGeneration, m_child.openGeneration());
    expectParentControls();
}

TEST_F(UiNestedPopupLifetimeTests, DeliberateChildReopeningAfterParentReopeningUsesANewPermittedLifetime){
    ASSERT_TRUE(acceptPlain(1u));
    ASSERT_NE(target(childId()), nullptr);
    const PopupToken oldChild = target(childId())->popup;
    reopenParent();
    m_child.close();
    m_child.open();
    ASSERT_TRUE(acceptPlain(2u));
    EXPECT_TRUE(m_childVisible);
    EXPECT_TRUE(m_child.isOpen());
    ASSERT_NE(target(childId()), nullptr);
    const PopupToken newChild = target(childId())->popup;
    EXPECT_EQ(newChild.instanceGeneration, oldChild.instanceGeneration);
    EXPECT_NE(newChild.openGeneration, oldChild.openGeneration);
    EXPECT_EQ(newChild, target(MakeWidgetId(childId(), "apply"))->popup);
    EXPECT_EQ(m_context.input().focus(), MakeWidgetId(childId(), "apply"));
    expectParentControls();
}

TEST_F(UiNestedPopupLifetimeTests, ParentReopeningClosesTheOldComboPopupAndPreservesItsCommittedKey){
    m_state.select(2u);
    m_state.open();
    ASSERT_TRUE(acceptCombo(1u));
    ASSERT_NE(target(MakeWidgetId(fieldId(), "rows")), nullptr);
    reopenParent();
    ASSERT_TRUE(acceptCombo(2u));
    EXPECT_TRUE(m_result.closed);
    EXPECT_FALSE(m_result.committed);
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_EQ(m_state.selectedKey(), 2u);
    EXPECT_EQ(target(MakeWidgetId(fieldId(), "rows")), nullptr);
    ASSERT_NE(target(fieldId()), nullptr);
    EXPECT_EQ(target(fieldId())->popup.openGeneration, m_parent.openGeneration());
    expectParentControls();
}

TEST_F(UiNestedPopupLifetimeTests, ExplicitComboOpeningAfterParentReopeningKeepsTheNewPopupOpen){
    m_state.select(2u);
    m_state.open();
    ASSERT_TRUE(acceptCombo(1u));
    const WidgetId rowsId = MakeWidgetId(fieldId(), "rows");
    ASSERT_NE(target(rowsId), nullptr);
    const PopupToken oldCombo = target(rowsId)->popup;
    reopenParent();
    m_state.open();
    ASSERT_TRUE(acceptCombo(2u));
    EXPECT_TRUE(m_state.isOpen());
    EXPECT_EQ(m_state.selectedKey(), 2u);
    ASSERT_NE(target(rowsId), nullptr);
    EXPECT_NE(target(rowsId)->popup.openGeneration, oldCombo.openGeneration);
    EXPECT_EQ(target(rowsId)->popup.instanceGeneration, oldCombo.instanceGeneration);
    EXPECT_EQ(m_context.input().focus(), rowsId);
    expectParentControls();
}

TEST_F(UiNestedPopupLifetimeTests, SearchParentReplacementCancelsStalePreeditAndPermitsAFreshOpening){
    m_search.combo().select(2u);
    ASSERT_TRUE(m_search.query().setText("Second"));
    m_search.combo().open();
    ASSERT_TRUE(acceptSearch(1u));
    const WidgetId queryId = MakeWidgetId(fieldId(), "query");
    ASSERT_NE(target(queryId), nullptr);
    const PopupToken oldSearch = target(queryId)->popup;
    const u64 loans = m_host.loans;
    ASSERT_TRUE(m_search.query().beginComposition());
    ASSERT_TRUE(m_search.query().updateComposition("preedit", 0u, 7u));
    reopenParent();
    ASSERT_TRUE(acceptSearch(2u));
    EXPECT_TRUE(m_searchResult.combo.closed);
    EXPECT_FALSE(m_search.combo().isOpen());
    EXPECT_EQ(m_search.combo().selectedKey(), 2u);
    EXPECT_EQ(m_search.query().text(), "Second");
    EXPECT_FALSE(m_search.query().composition().active);
    EXPECT_FALSE(m_search.editorState().focused);
    EXPECT_EQ(m_host.loans, loans);
    EXPECT_EQ(target(queryId), nullptr);
    expectParentControls();

    reopenParent();
    m_search.combo().open();
    ASSERT_TRUE(acceptSearch(3u));
    EXPECT_TRUE(m_search.combo().isOpen());
    EXPECT_EQ(m_search.combo().selectedKey(), 2u);
    ASSERT_NE(target(queryId), nullptr);
    EXPECT_NE(target(queryId)->popup.openGeneration, oldSearch.openGeneration);
    EXPECT_GT(m_host.loans, loans);
    EXPECT_EQ(m_host.lastPopup, target(queryId)->popup);
    EXPECT_EQ(m_context.input().focus(), queryId);
    expectParentControls();
}

TEST_F(UiNestedPopupLifetimeTests, ParentReopeningClosesTheOldContextMenuWithoutRemovingItsAnchor){
    ASSERT_TRUE(m_menu.open({ 80.0f, 120.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(acceptMenu(1u));
    ASSERT_NE(target(menuRows()), nullptr);
    reopenParent();
    ASSERT_TRUE(acceptMenu(2u));
    EXPECT_TRUE(m_menuResult.closed);
    EXPECT_FALSE(m_menuResult.activated);
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_EQ(m_menu.cursorKey(), 0u);
    EXPECT_EQ(target(menuRows()), nullptr);
    const HitTarget* anchor = target(MakeWidgetId(parentId(), "anchor"));
    ASSERT_NE(anchor, nullptr);
    EXPECT_TRUE(anchor->contextMenu);
    EXPECT_EQ(anchor->popup.openGeneration, m_parent.openGeneration());
    expectParentControls();
}

TEST_F(UiNestedPopupLifetimeTests, ExplicitMenuOpeningAfterParentReopeningKeepsTheNewCommandLifetime){
    ASSERT_TRUE(m_menu.open({ 80.0f, 120.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(acceptMenu(1u));
    ASSERT_NE(target(menuRows()), nullptr);
    const PopupToken oldMenu = target(menuRows())->popup;
    reopenParent();
    ASSERT_TRUE(m_menu.open({ 100.0f, 160.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(acceptMenu(2u));
    EXPECT_TRUE(m_menu.isOpen());
    EXPECT_EQ(m_menu.cursorKey(), 1u);
    ASSERT_NE(target(menuRows()), nullptr);
    EXPECT_NE(target(menuRows())->popup.openGeneration, oldMenu.openGeneration);
    EXPECT_EQ(target(menuRows())->popup.instanceGeneration, oldMenu.instanceGeneration);
    EXPECT_EQ(m_context.input().focus(), menuRows());
    expectParentControls();
}

TEST_F(UiNestedPopupLifetimeTests, AutomaticChildrenReserveTheirDeclaredLayersAndCountTowardTheEightScopeLimit){
    m_state.open();
    m_childCombo.open();
    ASSERT_TRUE(m_menu.open({ 80.0f, 180.0f, 0.0f, 0.0f }));
    ASSERT_TRUE(declareReserved(1u, false));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId parentRows = MakeWidgetId(fieldId(), "rows");
    const WidgetId childField = MakeWidgetId(childId(), "combo");
    const WidgetId childRows = MakeWidgetId(childField, "rows");
    ASSERT_NE(target(parentId()), nullptr);
    ASSERT_NE(target(parentRows), nullptr);
    ASSERT_NE(target(menuRows()), nullptr);
    ASSERT_NE(target(childId()), nullptr);
    ASSERT_NE(target(childRows), nullptr);
    EXPECT_EQ(target(parentId())->layer, 1u);
    EXPECT_EQ(target(parentRows)->layer, 2u);
    EXPECT_EQ(target(menuRows())->layer, 3u);
    EXPECT_EQ(target(childId())->layer, 4u);
    EXPECT_EQ(target(childRows)->layer, 5u);
    WidgetId deepest = childId();
    for(usize index = 0u; index < 3u; ++index)
        deepest = MakeWidgetId(deepest, "deep");
    ASSERT_NE(target(deepest), nullptr);
    EXPECT_EQ(target(deepest)->layer, s_InputMaxPopups);
    ASSERT_NE(target(MakeWidgetId(deepest, "leaf")), nullptr);
    EXPECT_EQ(m_context.input().focus(), MakeWidgetId(deepest, "leaf"));
    u32 previousLayer = 0u;
    for(const DrawCommand& command : snapshot.commands()){
        EXPECT_GE(command.layer, previousLayer);
        previousLayer = command.layer;
    }

    EXPECT_FALSE(declareReserved(2u, true));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    ASSERT_NE(target(parentRows), nullptr);
    EXPECT_EQ(target(parentRows)->layer, 2u);
    EXPECT_EQ(m_context.input().focus(), MakeWidgetId(deepest, "leaf"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

