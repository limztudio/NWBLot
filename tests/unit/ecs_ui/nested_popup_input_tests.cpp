// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_input_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_ui_popup_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPopupInputTests, NestedAcceptanceCancelsTheParentSessionOnlyAfterGeometryPublication){
    focusPopup();
    const TextInputSessionToken parentSession = m_textInput.activeSession();
    m_childState.open();
    ASSERT_TRUE(prepareNested());
    EXPECT_EQ(m_context.input().focus(), m_popupWidget.id);
    EXPECT_EQ(m_textInput.activeSession(), parentSession);
    ASSERT_TRUE(commit());
    ASSERT_EQ(m_context.input().popupCount(), 2u);
    EXPECT_EQ(m_context.input().popupScope(m_childToken)->parent, m_token);
    EXPECT_EQ(m_context.input().focus(), m_childWidget.id);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.commit(parentSession, "late parent"), TextInputAdmission::InvalidSession);
    ASSERT_TRUE(nestedFrame());
    EXPECT_TRUE(m_childResult.focused);
    EXPECT_TRUE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.publishedText(), "child");
}

TEST_F(UiPopupInputTests, NestedPreeditEscapeCancelsCompositionThenDismissesOnlyTheChild){
    focusPopup();
    m_childState.open();
    ASSERT_TRUE(nestedFrame());
    ASSERT_TRUE(nestedFrame());
    const TextInputSessionToken childSession = m_textInput.activeSession();
    ASSERT_TRUE(childSession.valid());
    ASSERT_EQ(m_textInput.preedit("한"), TextInputAdmission::Accepted);
    ASSERT_TRUE(nestedFrame());
    ASSERT_TRUE(m_childModel.composition().active);
    key(Core::Key::Escape);
    ASSERT_TRUE(nestedFrame());
    EXPECT_FALSE(m_childModel.composition().active);
    EXPECT_FALSE(m_childResult.cancelled);
    EXPECT_EQ(m_childModel.text(), "child");
    EXPECT_FALSE(m_childModel.canUndo());
    EXPECT_TRUE(m_childState.isOpen());
    EXPECT_TRUE(m_popupState.isOpen());
    key(Core::Key::Escape);
    ASSERT_TRUE(nestedFrame());
    EXPECT_TRUE(m_childResult.cancelled);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(prepareNested());
    EXPECT_FALSE(m_childState.isOpen());
    EXPECT_TRUE(m_popupState.isOpen());
    ASSERT_TRUE(commit());
    ASSERT_EQ(m_context.input().popupCount(), 1u);
    EXPECT_EQ(m_context.input().focus(), m_popupWidget.id);
    ASSERT_TRUE(nestedFrame());
    EXPECT_EQ(m_textInput.publishedText(), "popup");
    EXPECT_NE(m_textInput.activeSession(), childSession);
}

TEST_F(UiPopupInputTests, AncestorReopeningFencesQueuedChildNativeAndClipboardIntentions){
    focusPopup();
    m_childState.open();
    ASSERT_TRUE(nestedFrame());
    ASSERT_TRUE(nestedFrame());
    key(Core::Key::V, true);
    ASSERT_TRUE(nestedFrame());
    const TextInputSessionToken childSession = m_textInput.activeSession();
    ASSERT_TRUE(childSession.valid());
    ASSERT_TRUE(m_clipboard.pump());
    const ClipboardRequestToken paste = m_clipboard.startedToken;
    ASSERT_TRUE(paste.valid());
    ASSERT_TRUE(m_clipboard.complete(paste, "late paste"));
    ASSERT_EQ(m_textInput.commit(childSession, "late native"), TextInputAdmission::Accepted);
    m_popupState.close();
    m_popupState.open();
    ASSERT_TRUE(prepareNested());
    EXPECT_EQ(m_childModel.text(), "child");
    EXPECT_FALSE(m_childModel.canUndo());
    EXPECT_FALSE(m_childState.isOpen());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.commit(childSession, "later"), TextInputAdmission::InvalidSession);
    EXPECT_FALSE(m_clipboard.complete(paste, "later"));
    ASSERT_TRUE(commit());
    ASSERT_EQ(m_context.input().popupCount(), 1u);
    EXPECT_EQ(m_context.input().focus(), m_popupWidget.id);
    ASSERT_TRUE(nestedFrame());
    EXPECT_EQ(m_textInput.publishedText(), "popup");
}

TEST_F(UiPopupInputTests, NestedFocusLossCancelsTheWholeFamilyWithoutRestoringAnUnfocusedEditor){
    focusPopup();
    m_childState.open();
    ASSERT_TRUE(nestedFrame());
    ASSERT_TRUE(nestedFrame());
    const TextInputSessionToken childSession = m_textInput.activeSession();
    ASSERT_TRUE(m_textInput.setFocused(false));
    EXPECT_TRUE(dispatch({ .type = Ui::InputEventType::FocusLost, .position = {} }).keyboardConsumed);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_EQ(m_textInput.commit(childSession, "late"), TextInputAdmission::InvalidSession);
    ASSERT_TRUE(nestedFrame());
    EXPECT_FALSE(m_popupState.isOpen());
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(m_textInput.setFocused(true));
    ASSERT_TRUE(nestedFrame());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_FALSE(m_context.input().focus().valid());
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

