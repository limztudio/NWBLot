// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_tools_fixture.h"
#include "widget_id_assertion.h"
#include "combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_trigger_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiPopupToolsTests;
using namespace NWB::UiComboTests;

constexpr AStringView s_MenuRowsKey = "rows";
constexpr AStringView s_AfterKey = "after";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupTriggerTests : public ComboFixture{
protected:
    [[nodiscard]] static WidgetOptions Control()noexcept{
        WidgetOptions options;
        options.width = { LayoutSizePolicy::Fixed, 220.0f };
        options.height = { LayoutSizePolicy::Fixed, 32.0f };
        return options;
    }


protected:
    virtual void SetUp()override{
        ComboFixture::SetUp();
        m_source.count = 5u;
        m_source.disabled = 3u;
        m_parent.open();
        m_child.open();
    }

    [[nodiscard]] WidgetId parentId()const{ return UiTestSupport::RequireWidgetId(MakeWidgetId(UiTestSupport::RequireWidgetId(MakeRootId(m_root)), s_ParentPopupKey)); }

    [[nodiscard]] WidgetId childId()const{ return UiTestSupport::RequireWidgetId(MakeWidgetId(parentId(), s_ChildPopupKey)); }

    [[nodiscard]] WidgetId fieldId()const{ return UiTestSupport::RequireWidgetId(MakeWidgetId(childId(), s_ComboKey)); }

    [[nodiscard]] WidgetId rowsId()const{ return UiTestSupport::RequireWidgetId(MakeWidgetId(fieldId(), s_ComboRowsKey)); }

    [[nodiscard]] WidgetId anchorId()const{ return UiTestSupport::RequireWidgetId(MakeWidgetId(childId(), s_AnchorKey)); }

    [[nodiscard]] WidgetId menuRows()const{ return UiTestSupport::RequireWidgetId(MakeWidgetId(UiTestSupport::RequireWidgetId(MakeWidgetId(childId(), s_MenuKey)), s_MenuRowsKey)); }

    [[nodiscard]] WidgetId comboRow(const u64 key)const{
        return UiTestSupport::RequireWidgetId(MakeWidgetPartId(UiTestSupport::RequireWidgetId(MakeWidgetId(rowsId(), s_ListRowsKey)), key));
    }

    [[nodiscard]] bool beginChild(const u64 generation){
        if(!begin(generation) || !m_builder.beginPopup(s_ParentPopupKey, m_parent, UiWidgetTests::NestedParentOptions()))
            return false;
        if(m_builder.button("before", "Parent before", Control()))
            return false;
        return m_builder.beginPopup(s_ChildPopupKey, m_child, UiWidgetTests::NestedChildOptions());
    }

    [[nodiscard]] bool finishChild(const u64 generation){
        if(m_builder.button(s_AfterKey, "Child after", Control()) || !m_builder.endPopup())
            return false;
        if(m_builder.button(s_AfterKey, "Parent after", Control()) || !m_builder.endPopup())
            return false;
        return m_context.endRoot() && m_context.finishFrame() && m_context.commitFrame(generation);
    }

    [[nodiscard]] bool acceptChildCombo(const u64 generation){
        if(!beginChild(generation))
            return false;
        m_result = m_builder.comboBox(s_ComboKey, m_source, m_state, Options());
        return m_result.valid && finishChild(generation);
    }

    [[nodiscard]] bool acceptChildMenu(const u64 generation){
        if(!beginChild(generation))
            return false;
        m_anchorActivated = m_builder.button(s_AnchorKey, "Commands", Control());
        m_menuResult = m_builder.contextMenu(s_MenuKey, s_AnchorKey, m_menuSource, m_menu);
        return m_menuResult.valid && finishChild(generation);
    }

    void expectUserParentsOpen()const{
        EXPECT_TRUE(m_parent.isOpen());
        EXPECT_TRUE(m_child.isOpen());
        ASSERT_NE(target(parentId()), nullptr);
        ASSERT_NE(target(childId()), nullptr);
        EXPECT_EQ(target(parentId())->layer, 1u);
        EXPECT_EQ(target(childId())->layer, 2u);
        EXPECT_NE(target(UiTestSupport::RequireWidgetId(MakeWidgetId(parentId(), s_AfterKey))), nullptr);
        EXPECT_NE(target(UiTestSupport::RequireWidgetId(MakeWidgetId(childId(), s_AfterKey))), nullptr);
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
    EXPECT_TRUE(send({ .type = InputEventType::SecondaryDown, .position = point }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::SecondaryUp, .position = point }).pointerConsumed);
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

