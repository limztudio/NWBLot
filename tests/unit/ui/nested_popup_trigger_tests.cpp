// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_tools_fixture.h"
#include "search_combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_trigger_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiPopupToolsTests;
using namespace NWB::UiSearchComboTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupTriggerTests : public SearchFixture{
protected:
    virtual void SetUp()override{
        SearchFixture::SetUp();
        m_source.count = 5u;
        m_source.disabled = 3u;
        m_searchSource.full.count = 5u;
        m_searchSource.full.disabled = 3u;
        m_parent.open();
        m_child.open();
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

    [[nodiscard]] static WidgetOptions control(){
        WidgetOptions options;
        options.width = { LayoutSizePolicy::Fixed, 220.0f };
        options.height = { LayoutSizePolicy::Fixed, 32.0f };
        return options;
    }

    [[nodiscard]] WidgetId parentId()const{ return MakeWidgetId(MakeRootId(m_root), "parent"); }
    [[nodiscard]] WidgetId childId()const{ return MakeWidgetId(parentId(), "child"); }
    [[nodiscard]] WidgetId fieldId()const{ return MakeWidgetId(childId(), "combo"); }
    [[nodiscard]] WidgetId rowsId()const{ return MakeWidgetId(fieldId(), "rows"); }
    [[nodiscard]] WidgetId queryId()const{ return MakeWidgetId(fieldId(), "query"); }
    [[nodiscard]] WidgetId anchorId()const{ return MakeWidgetId(childId(), "anchor"); }
    [[nodiscard]] WidgetId menuRows()const{ return MakeWidgetId(MakeWidgetId(childId(), "menu"), "rows"); }

    [[nodiscard]] WidgetId comboRow(const u64 key)const{
        return MakeWidgetPartId(MakeWidgetId(rowsId(), "rows"), key);
    }

    [[nodiscard]] bool beginChild(const u64 generation){
        if(!begin(generation) || !m_builder.beginPopup("parent", m_parent, parentOptions()))
            return false;
        if(m_builder.button("before", "Parent before", control()))
            return false;
        return m_builder.beginPopup("child", m_child, childOptions());
    }

    [[nodiscard]] bool finishChild(const u64 generation){
        if(m_builder.button("after", "Child after", control()) || !m_builder.endPopup())
            return false;
        if(m_builder.button("after", "Parent after", control()) || !m_builder.endPopup())
            return false;
        return m_context.endRoot() && m_context.finishFrame() && m_context.commitFrame(generation);
    }

    [[nodiscard]] bool acceptChildCombo(const u64 generation){
        if(!beginChild(generation))
            return false;
        m_result = m_builder.comboBox("combo", m_source, m_state, Options());
        return m_result.valid && finishChild(generation);
    }

    [[nodiscard]] bool acceptChildSearch(const u64 generation){
        if(!beginChild(generation))
            return false;
        m_searchResult = m_builder.searchComboBox("combo", m_searchSource, m_search, options());
        return m_searchResult.combo.valid && finishChild(generation);
    }

    [[nodiscard]] bool acceptChildMenu(const u64 generation){
        if(!beginChild(generation))
            return false;
        m_anchorActivated = m_builder.button("anchor", "Commands", control());
        m_menuResult = m_builder.contextMenu("menu", "anchor", m_menuSource, m_menu);
        return m_menuResult.valid && finishChild(generation);
    }

    void expectUserParentsOpen()const{
        EXPECT_TRUE(m_parent.isOpen());
        EXPECT_TRUE(m_child.isOpen());
        ASSERT_NE(target(parentId()), nullptr);
        ASSERT_NE(target(childId()), nullptr);
        EXPECT_EQ(target(parentId())->layer, 1u);
        EXPECT_EQ(target(childId())->layer, 2u);
        EXPECT_NE(target(MakeWidgetId(parentId(), "after")), nullptr);
        EXPECT_NE(target(MakeWidgetId(childId(), "after")), nullptr);
    }


protected:
    PopupState m_parent;
    PopupState m_child;
    ContextMenuState m_menu;
    PopupToolsSource m_menuSource;
    ContextMenuResult m_menuResult;
    bool m_anchorActivated = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiNestedPopupTriggerTests, AcceptedChildFieldPointerOpensComboAndOnlyAnEnabledRowCommits){
    ASSERT_TRUE(acceptChildCombo(1u));
    ASSERT_NE(target(fieldId()), nullptr);
    const PopupToken child = target(fieldId())->popup;
    EXPECT_EQ(child.widget, childId());
    EXPECT_EQ(m_context.input().focus(), fieldId());
    EXPECT_EQ(target(rowsId()), nullptr);
    click(Center(target(fieldId())->rectangle));
    ASSERT_TRUE(acceptChildCombo(2u));
    EXPECT_TRUE(m_result.opened);
    EXPECT_FALSE(m_result.committed);
    EXPECT_TRUE(m_state.isOpen());
    ASSERT_NE(target(rowsId()), nullptr);
    EXPECT_EQ(target(fieldId())->popup, child);
    EXPECT_EQ(target(rowsId())->layer, 3u);
    EXPECT_EQ(target(rowsId())->popup.widget, MakeWidgetId(fieldId(), "popup"));
    EXPECT_EQ(m_context.input().focus(), rowsId());
    ASSERT_NE(target(comboRow(3u)), nullptr);
    EXPECT_FALSE(target(comboRow(3u))->enabled);
    click(Center(target(comboRow(3u))->rectangle));
    ASSERT_TRUE(acceptChildCombo(3u));
    EXPECT_FALSE(m_result.committed);
    EXPECT_TRUE(m_state.isOpen());
    EXPECT_EQ(m_state.selectedKey(), 0u);

    ASSERT_NE(target(comboRow(4u)), nullptr);
    click(Center(target(comboRow(4u))->rectangle));
    ASSERT_TRUE(acceptChildCombo(4u));
    EXPECT_TRUE(m_result.committed);
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_TRUE(m_result.closed);
    EXPECT_EQ(m_state.selectedKey(), 4u);
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_EQ(target(rowsId()), nullptr);
    EXPECT_EQ(target(fieldId())->popup, child);
    EXPECT_EQ(m_context.input().focus(), fieldId());
    expectUserParentsOpen();
}

TEST_F(UiNestedPopupTriggerTests, AcceptedChildSearchPointerOpensExplicitQueryScopeAndTypedResultsCommit){
    ASSERT_TRUE(acceptChildSearch(1u));
    ASSERT_NE(target(fieldId()), nullptr);
    const PopupToken child = target(fieldId())->popup;
    EXPECT_EQ(m_host.loans, 0u);
    click(Center(target(fieldId())->rectangle));
    ASSERT_TRUE(acceptChildSearch(2u));
    EXPECT_TRUE(m_searchResult.combo.opened);
    EXPECT_TRUE(m_search.combo().isOpen());
    ASSERT_NE(target(queryId()), nullptr);
    ASSERT_NE(target(rowsId()), nullptr);
    EXPECT_EQ(target(queryId())->layer, 3u);
    EXPECT_EQ(target(queryId())->popup, target(rowsId())->popup);
    EXPECT_EQ(target(queryId())->popup.widget, MakeWidgetId(fieldId(), "popup"));
    EXPECT_EQ(m_host.loanContextPopup, child);
    EXPECT_EQ(m_host.lastPopup, target(queryId())->popup);
    EXPECT_EQ(m_host.publishContextPopup, target(queryId())->popup);
    EXPECT_EQ(target(queryId())->keyboardOwner, rowsId());
    EXPECT_EQ(m_context.input().focus(), queryId());

    m_host.text("Second");
    ASSERT_TRUE(acceptChildSearch(3u));
    EXPECT_TRUE(m_searchResult.queryChanged);
    EXPECT_EQ(m_search.query().text(), "Second");
    EXPECT_TRUE(m_search.editorState().focused);
    EXPECT_EQ(m_context.input().focus(), queryId());
    EXPECT_EQ(target(rowsId())->control.contentGeneration, m_searchSource.view.generation);
    EXPECT_EQ(target(comboRow(1u)), nullptr);
    ASSERT_NE(target(comboRow(2u)), nullptr);
    press(Core::Key::Down);
    ASSERT_TRUE(acceptChildSearch(4u));
    EXPECT_EQ(m_search.combo().listState().cursorKey(), 2u);
    EXPECT_EQ(m_search.combo().selectedKey(), 0u);
    EXPECT_EQ(m_context.input().focus(), queryId());
    m_host.submitted = true;
    press(Core::Key::Enter);
    ASSERT_TRUE(acceptChildSearch(5u));
    EXPECT_TRUE(m_searchResult.combo.committed);
    EXPECT_TRUE(m_searchResult.combo.closed);
    EXPECT_EQ(m_search.combo().selectedKey(), 2u);
    EXPECT_FALSE(m_search.combo().isOpen());
    EXPECT_EQ(target(queryId()), nullptr);
    EXPECT_EQ(target(rowsId()), nullptr);
    EXPECT_EQ(m_host.contextScopedLoans, 0u);
    EXPECT_EQ(m_context.input().focus(), fieldId());
    expectUserParentsOpen();
}

TEST_F(UiNestedPopupTriggerTests, AcceptedChildSecondaryAndMenuTriggersCommitStableCommandKeys){
    ASSERT_TRUE(acceptChildMenu(1u));
    ASSERT_NE(target(anchorId()), nullptr);
    const PopupToken child = target(anchorId())->popup;
    EXPECT_EQ(child.widget, childId());
    EXPECT_EQ(m_context.input().focus(), anchorId());
    const Point point = Center(target(anchorId())->rectangle);
    EXPECT_TRUE(send({ InputEventType::SecondaryDown, point }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::SecondaryUp, point }).pointerConsumed);
    ASSERT_TRUE(acceptChildMenu(2u));
    EXPECT_TRUE(m_menuResult.opened);
    EXPECT_FALSE(m_anchorActivated);
    EXPECT_TRUE(m_menu.isOpen());
    ASSERT_NE(target(menuRows()), nullptr);
    const PopupToken firstMenu = target(menuRows())->popup;
    EXPECT_EQ(target(menuRows())->layer, 3u);
    EXPECT_EQ(target(anchorId())->popup, child);
    EXPECT_EQ(m_context.input().focus(), menuRows());
    press(Core::Key::Down);
    press(Core::Key::Down);
    ASSERT_TRUE(acceptChildMenu(3u));
    EXPECT_EQ(m_menu.cursorKey(), 4u);
    EXPECT_FALSE(m_menuResult.activated);
    press(Core::Key::Enter);
    ASSERT_TRUE(acceptChildMenu(4u));
    EXPECT_TRUE(m_menuResult.activated);
    EXPECT_EQ(m_menuResult.key, 4u);
    EXPECT_TRUE(m_menuResult.closed);
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_FALSE(m_anchorActivated);
    EXPECT_EQ(target(menuRows()), nullptr);
    EXPECT_EQ(m_context.input().focus(), anchorId());

    press(Core::Key::Menu);
    ASSERT_TRUE(acceptChildMenu(5u));
    EXPECT_TRUE(m_menuResult.opened);
    EXPECT_TRUE(m_menu.isOpen());
    ASSERT_NE(target(menuRows()), nullptr);
    EXPECT_NE(target(menuRows())->popup.openGeneration, firstMenu.openGeneration);
    EXPECT_EQ(target(menuRows())->popup.instanceGeneration, firstMenu.instanceGeneration);
    EXPECT_EQ(target(anchorId())->popup, child);
    press(Core::Key::End);
    ASSERT_TRUE(acceptChildMenu(6u));
    EXPECT_EQ(m_menu.cursorKey(), 5u);
    press(Core::Key::Enter);
    ASSERT_TRUE(acceptChildMenu(7u));
    EXPECT_TRUE(m_menuResult.activated);
    EXPECT_EQ(m_menuResult.key, 5u);
    EXPECT_TRUE(m_menuResult.closed);
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_FALSE(m_anchorActivated);
    EXPECT_EQ(m_context.input().focus(), anchorId());
    expectUserParentsOpen();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

