// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_tools_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_tools_navigation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiPopupToolsTests;

class UiPopupToolsNavigationTests : public PopupToolsFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPopupToolsNavigationTests, SourceRevisionRestoresRowsFocusAtAcceptanceAndRetiresHeldInput){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(openBySecondary(2u));
    ASSERT_NE(menuHost(), nullptr);
    const WidgetId rows = menuHost()->id;
    ASSERT_EQ(m_context.input().focus(), rows);
    press(InputKey::End);
    ASSERT_TRUE(acceptTools(3u, false));
    ASSERT_EQ(m_menu.cursorKey(), 5u);
    ASSERT_FALSE(m_menuResult.activated);
    const u64 oldInputGeneration = m_menu.listState().inputGeneration();
    const u64 oldSourceRevision = menuHost()->control.contentRevision;

    press(InputKey::Enter);
    InputEvent heldNavigation;
    heldNavigation.type = InputEventType::KeyDown;
    heldNavigation.key = InputKey::Down;
    EXPECT_TRUE(send(heldNavigation).keyboardConsumed);
    ASSERT_FALSE(m_context.input().controlActions().empty());
    m_source.count = 4u;
    ++m_source.contentRevision;

    ASSERT_TRUE(prepareTools(4u, false));
    EXPECT_EQ(m_context.input().layoutGeneration(), 3u);
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_TRUE(m_context.input().controlActions().empty());
    EXPECT_NE(m_menu.listState().inputGeneration(), oldInputGeneration);
    EXPECT_EQ(m_menu.cursorKey(), 1u);
    EXPECT_TRUE(m_menu.isOpen());
    EXPECT_FALSE(m_menuResult.activated);
    EXPECT_EQ(m_menuResult.key, 0u);
    EXPECT_FALSE(m_context.commitFrame(3u));
    EXPECT_FALSE(m_context.input().focus().valid());

    ASSERT_TRUE(m_context.commitFrame(4u));
    ASSERT_NE(menuHost(), nullptr);
    EXPECT_EQ(menuHost()->id, rows);
    EXPECT_EQ(m_context.input().focus(), rows);
    EXPECT_NE(menuHost()->control.contentRevision, oldSourceRevision);
    EXPECT_EQ(menuHost()->control.contentRevision, m_source.contentRevision);
    EXPECT_EQ(menuHost()->control.instanceGeneration, m_menu.listState().inputGeneration());
    heldNavigation.repeat = true;
    EXPECT_TRUE(send(heldNavigation).keyboardConsumed);
    ASSERT_TRUE(acceptTools(5u, false));
    EXPECT_EQ(m_menu.cursorKey(), 1u);
    EXPECT_FALSE(m_menuResult.activated);
    EXPECT_TRUE(m_menu.isOpen());
    heldNavigation.type = InputEventType::KeyUp;
    heldNavigation.repeat = false;
    EXPECT_TRUE(send(heldNavigation).keyboardConsumed);

    press(InputKey::End);
    ASSERT_TRUE(acceptTools(6u, false));
    EXPECT_EQ(m_menu.cursorKey(), 4u);
    EXPECT_FALSE(m_menuResult.activated);
    press(InputKey::Home);
    ASSERT_TRUE(acceptTools(7u, false));
    EXPECT_EQ(m_menu.cursorKey(), 1u);
    EXPECT_FALSE(m_menuResult.activated);
    press(InputKey::End);
    ASSERT_TRUE(acceptTools(8u, false));
    EXPECT_EQ(m_menu.cursorKey(), 4u);
    press(InputKey::Enter);
    ASSERT_TRUE(acceptTools(9u, false));
    EXPECT_TRUE(m_menuResult.activated);
    EXPECT_EQ(m_menuResult.key, 4u);
    EXPECT_TRUE(m_menuResult.closed);
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_FALSE(m_context.input().hasPopup());
}

TEST_F(UiPopupToolsNavigationTests, FocusLossAndRegainCannotRestorePreparedRowsFocus){
    ASSERT_TRUE(acceptTools(1u, false));
    ASSERT_TRUE(openBySecondary(2u));
    ASSERT_NE(menuHost(), nullptr);
    ASSERT_EQ(m_context.input().focus(), menuHost()->id);
    m_source.count = 4u;
    ++m_source.contentRevision;
    ASSERT_TRUE(prepareTools(3u, false));
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(m_menu.isOpen());

    EXPECT_FALSE(send({ .type = InputEventType::FocusLost, .position = {} }).focus.valid());
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained, .position = {} }).focus.valid());
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(acceptTools(4u, false));
    EXPECT_TRUE(m_menuResult.closed);
    EXPECT_FALSE(m_menuResult.activated);
    EXPECT_FALSE(m_menu.isOpen());
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

