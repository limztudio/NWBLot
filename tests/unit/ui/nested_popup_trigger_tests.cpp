// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_tools_fixture.h"
#include "combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_trigger_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiPopupToolsTests;
using namespace NWB::UiComboTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupTriggerTests : public ComboFixture{
protected:
    virtual void SetUp()override{
        ComboFixture::SetUp();
        m_source.count = 5u;
        m_source.disabled = 3u;
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

    [[nodiscard]] static WidgetOptions Control(){
        WidgetOptions options;
        options.width = { LayoutSizePolicy::Fixed, 220.0f };
        options.height = { LayoutSizePolicy::Fixed, 32.0f };
        return options;
    }

    [[nodiscard]] WidgetId parentId()const{ return MakeWidgetId(MakeRootId(m_root), "parent"); }
    [[nodiscard]] WidgetId childId()const{ return MakeWidgetId(parentId(), "child"); }
    [[nodiscard]] WidgetId fieldId()const{ return MakeWidgetId(childId(), "combo"); }
    [[nodiscard]] WidgetId rowsId()const{ return MakeWidgetId(fieldId(), "rows"); }
    [[nodiscard]] WidgetId anchorId()const{ return MakeWidgetId(childId(), "anchor"); }
    [[nodiscard]] WidgetId menuRows()const{ return MakeWidgetId(MakeWidgetId(childId(), "menu"), "rows"); }

    [[nodiscard]] WidgetId comboRow(const u64 key)const{
        return MakeWidgetPartId(MakeWidgetId(rowsId(), "rows"), key);
    }

    [[nodiscard]] bool beginChild(const u64 generation){
        if(!begin(generation) || !m_builder.beginPopup("parent", m_parent, ParentOptions()))
            return false;
        if(m_builder.button("before", "Parent before", Control()))
            return false;
        return m_builder.beginPopup("child", m_child, ChildOptions());
    }

    [[nodiscard]] bool finishChild(const u64 generation){
        if(m_builder.button("after", "Child after", Control()) || !m_builder.endPopup())
            return false;
        if(m_builder.button("after", "Parent after", Control()) || !m_builder.endPopup())
            return false;
        return m_context.endRoot() && m_context.finishFrame() && m_context.commitFrame(generation);
    }

    [[nodiscard]] bool acceptChildCombo(const u64 generation){
        if(!beginChild(generation))
            return false;
        m_result = m_builder.comboBox("combo", m_source, m_state, Options());
        return m_result.valid && finishChild(generation);
    }

    [[nodiscard]] bool acceptChildMenu(const u64 generation){
        if(!beginChild(generation))
            return false;
        m_anchorActivated = m_builder.button("anchor", "Commands", Control());
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


TEST_F(UiNestedPopupTriggerTests, NestedComboRejectsDisabledRowWithoutActivatingUserAncestors){
    ASSERT_TRUE(acceptChildCombo(1u));
    ASSERT_NE(target(fieldId()), nullptr);
    EXPECT_EQ(target(rowsId()), nullptr);
    click(Center(target(fieldId())->rectangle));
    ASSERT_TRUE(acceptChildCombo(2u));
    EXPECT_FALSE(m_result.committed);
    EXPECT_TRUE(m_state.isOpen());
    ASSERT_NE(target(rowsId()), nullptr);
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
    expectUserParentsOpen();
}

TEST_F(UiNestedPopupTriggerTests, NestedMenuSkipsDisabledCommandsWithoutActivatingUserAncestors){
    ASSERT_TRUE(acceptChildMenu(1u));
    ASSERT_NE(target(anchorId()), nullptr);
    const Point point = Center(target(anchorId())->rectangle);
    EXPECT_TRUE(send({ InputEventType::SecondaryDown, point }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::SecondaryUp, point }).pointerConsumed);
    ASSERT_TRUE(acceptChildMenu(2u));
    EXPECT_TRUE(m_menuResult.opened);
    EXPECT_FALSE(m_anchorActivated);
    EXPECT_TRUE(m_menu.isOpen());
    ASSERT_NE(target(menuRows()), nullptr);
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
    expectUserParentsOpen();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

