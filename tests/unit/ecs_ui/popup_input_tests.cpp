// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "popup_input_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EcsUiPopupInputTestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPopupInputTests, PreparedPopupPreservesBaseSessionAndAcceptedFocusCancelsIt){
    focusBase();
    const TextInputSessionToken baseSession = m_textInput.activeSession();
    const u32 starts = m_textInput.starts;
    const u32 ends = m_textInput.ends;
    m_popupState.open();
    ASSERT_TRUE(prepare());
    EXPECT_EQ(m_context.input().focus(), m_baseWidget.id);
    EXPECT_EQ(m_textInput.activeSession(), baseSession);
    EXPECT_EQ(m_textInput.starts, starts);
    EXPECT_EQ(m_textInput.ends, ends);
    EXPECT_FALSE(m_context.input().hasPopup());
    ASSERT_TRUE(commit());
    EXPECT_TRUE(m_context.input().hasPopup());
    EXPECT_EQ(m_context.input().focus(), m_popupWidget.id);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.starts, starts);
    EXPECT_EQ(m_textInput.ends, ends + 1u);
    EXPECT_EQ(m_textInput.commit(baseSession, "late"), TextInputAdmission::InvalidSession);
    EXPECT_EQ(m_baseModel.text(), "base");
    EXPECT_EQ(m_popupModel.text(), "popup");
}

TEST_F(UiPopupInputTests, PopupSessionBeginsOnlyAfterAcceptedGeometryAndANewModelDeclaration){
    focusBase();
    const TextInputSessionToken baseSession = m_textInput.activeSession();
    const u32 starts = m_textInput.starts;
    m_popupState.open();
    ASSERT_TRUE(frame());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.starts, starts);
    ASSERT_TRUE(prepare());
    EXPECT_TRUE(m_popupResult.focused);
    EXPECT_TRUE(m_textInput.activeSession().valid());
    EXPECT_NE(m_textInput.activeSession(), baseSession);
    EXPECT_EQ(m_textInput.starts, starts + 1u);
    EXPECT_EQ(m_textInput.publishedText(), "popup");
    ASSERT_TRUE(commit());
}

TEST_F(UiPopupInputTests, ReopenedSameModelFencesQueuedNativeAndLocalIntentionsBeforeItsCandidateIsAccepted){
    focusPopup();
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    const usize anchor = m_popupModel.anchor();
    const usize caret = m_popupModel.caret();
    ASSERT_EQ(m_textInput.commit(oldSession, "late"), TextInputAdmission::Accepted);
    key(Core::Key::Backspace);
    EXPECT_EQ(m_popupModel.text(), "popup");
    m_popupState.close();
    m_popupState.open();
    ASSERT_TRUE(prepare());
    EXPECT_EQ(m_popupModel.text(), "popup");
    EXPECT_EQ(m_popupModel.anchor(), anchor);
    EXPECT_EQ(m_popupModel.caret(), caret);
    EXPECT_FALSE(m_popupModel.canUndo());
    EXPECT_FALSE(m_popupResult.textChanged);
    EXPECT_FALSE(m_popupResult.focused);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.commit(oldSession, "later"), TextInputAdmission::InvalidSession);
    ASSERT_TRUE(commit());
    EXPECT_EQ(m_context.input().focus(), m_popupWidget.id);
    ASSERT_TRUE(frame());
    EXPECT_TRUE(m_textInput.activeSession().valid());
    EXPECT_NE(m_textInput.activeSession(), oldSession);
    EXPECT_EQ(m_textInput.publishedText(), "popup");
}

TEST_F(UiPopupInputTests, ReopenedSameModelRejectsAnAlreadyCompletedPasteFromThePriorPopupLifetime){
    focusPopup();
    key(Core::Key::V, true);
    ASSERT_TRUE(frame());
    ASSERT_TRUE(m_clipboard.pump());
    const ClipboardRequestToken oldRequest = m_clipboard.startedToken;
    ASSERT_TRUE(oldRequest.valid());
    ASSERT_TRUE(m_clipboard.complete(oldRequest, "late paste"));
    EXPECT_EQ(m_popupModel.text(), "popup");
    m_popupState.close();
    m_popupState.open();
    ASSERT_TRUE(prepare());
    EXPECT_EQ(m_popupModel.text(), "popup");
    EXPECT_FALSE(m_popupModel.canUndo());
    EXPECT_FALSE(m_popupResult.textChanged);
    EXPECT_FALSE(m_clipboard.complete(oldRequest, "later paste"));
    ASSERT_TRUE(commit());
    ASSERT_TRUE(frame());
    EXPECT_EQ(m_textInput.publishedText(), "popup");
}

TEST_F(UiPopupInputTests, FirstEscapeCancelsTransientPreeditAndSecondDismissesBeforeAcceptedRestoration){
    focusPopup();
    ASSERT_EQ(m_textInput.preedit("한"), TextInputAdmission::Accepted);
    ASSERT_TRUE(frame());
    ASSERT_TRUE(m_popupModel.composition().active);
    EXPECT_EQ(m_popupModel.text(), "popup");
    EXPECT_FALSE(m_popupModel.canUndo());
    key(Core::Key::Escape);
    EXPECT_EQ(m_context.input().focus(), m_popupWidget.id);
    ASSERT_TRUE(frame());
    EXPECT_FALSE(m_popupModel.composition().active);
    EXPECT_FALSE(m_popupResult.cancelled);
    EXPECT_EQ(m_popupModel.text(), "popup");
    EXPECT_FALSE(m_popupModel.canUndo());
    EXPECT_TRUE(m_popupState.isOpen());
    EXPECT_EQ(m_context.input().focus(), m_popupWidget.id);
    EXPECT_FALSE(m_context.input().consumePopupDismissal(m_token));
    key(Core::Key::Escape);
    ASSERT_TRUE(frame());
    EXPECT_TRUE(m_popupResult.cancelled);
    EXPECT_TRUE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(prepare());
    EXPECT_FALSE(m_popupState.isOpen());
    EXPECT_TRUE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(commit());
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_EQ(m_context.input().focus(), m_baseWidget.id);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(frame());
    EXPECT_TRUE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.publishedText(), "base");
}

TEST_F(UiPopupInputTests, PlainEscapeCancelsPopupEditAndRestartsBaseSessionOnlyAfterAcceptedRemoval){
    focusPopup();
    const TextInputSessionToken popupSession = m_textInput.activeSession();
    key(Core::Key::Escape);
    EXPECT_EQ(m_context.input().focus(), m_popupWidget.id);
    EXPECT_EQ(m_textInput.activeSession(), popupSession);
    ASSERT_TRUE(frame());
    EXPECT_TRUE(m_popupResult.cancelled);
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(prepare());
    EXPECT_FALSE(m_popupState.isOpen());
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(commit());
    EXPECT_EQ(m_context.input().focus(), m_baseWidget.id);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(frame());
    EXPECT_TRUE(m_textInput.activeSession().valid());
    EXPECT_NE(m_textInput.activeSession(), popupSession);
    EXPECT_EQ(m_textInput.publishedText(), "base");
}

TEST_F(UiPopupInputTests, NativeFocusLossCancelsSessionAndRemovalDoesNotRestoreAnUnfocusedEditor){
    focusPopup();
    const TextInputSessionToken popupSession = m_textInput.activeSession();
    ASSERT_TRUE(m_textInput.setFocused(false));
    EXPECT_TRUE(dispatch({ .type = Ui::InputEventType::FocusLost, .position = {} }).keyboardConsumed);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_EQ(m_textInput.commit(popupSession, "late"), TextInputAdmission::InvalidSession);
    ASSERT_TRUE(frame());
    EXPECT_FALSE(m_popupState.isOpen());
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_baseModel.text(), "base");
    EXPECT_EQ(m_popupModel.text(), "popup");
    ASSERT_TRUE(m_textInput.setFocused(true));
    EXPECT_FALSE(dispatch({ .type = Ui::InputEventType::FocusGained }).focus.valid());
    ASSERT_TRUE(frame());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    key(Core::Key::Tab);
    ASSERT_TRUE(frame());
    EXPECT_EQ(m_context.input().focus(), m_baseWidget.id);
    EXPECT_TRUE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.publishedText(), "base");
}


TEST_F(UiPopupInputTests, DeferredEditorStartsNativeSessionOnlyAfterPopupGeometryAcceptance){
    m_popupState.open();
    ASSERT_TRUE(prepareDeferred());
    EXPECT_FALSE(m_context.popupToken().valid());
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(commit());
    EXPECT_EQ(m_context.input().focus(), m_popupWidget.id);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(prepareDeferred());
    EXPECT_TRUE(m_popupResult.focused);
    EXPECT_TRUE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.publishedText(), "popup");
    ASSERT_TRUE(commit());
}

TEST_F(UiPopupInputTests, DeferredEditorRejectsQueuedNativeCommitWhenPopupLifetimeChanges){
    m_popupState.open();
    ASSERT_TRUE(prepareDeferred() && commit());
    ASSERT_TRUE(prepareDeferred() && commit());
    const auto old = m_textInput.activeSession();
    ASSERT_TRUE(old.valid());
    ASSERT_EQ(m_textInput.commit(old, "late"), TextInputAdmission::Accepted);
    m_popupState.close();
    m_popupState.open();
    ASSERT_TRUE(prepareDeferred());
    EXPECT_EQ(m_popupModel.text(), "popup");
    EXPECT_FALSE(m_popupResult.textChanged);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.commit(old, "later"), TextInputAdmission::InvalidSession);
    ASSERT_TRUE(commit());
}

TEST_F(UiPopupInputTests, DeferredEditorKeepsPreeditSeparateAndGatesEnterUntilCompositionEnds){
    m_popupState.open();
    ASSERT_TRUE(prepareDeferred() && commit());
    ASSERT_TRUE(prepareDeferred() && commit());
    ASSERT_EQ(m_textInput.preedit("한"), TextInputAdmission::Accepted);
    key(Core::Key::Enter);
    ASSERT_TRUE(prepareDeferred() && commit());
    EXPECT_TRUE(m_popupModel.composition().active);
    EXPECT_EQ(m_popupModel.text(), "popup");
    EXPECT_FALSE(m_popupResult.submitted);
    key(Core::Key::Escape);
    ASSERT_TRUE(prepareDeferred() && commit());
    EXPECT_FALSE(m_popupModel.composition().active);
    EXPECT_FALSE(m_popupResult.cancelled);
    EXPECT_TRUE(m_context.input().hasPopup());
    key(Core::Key::Enter);
    ASSERT_TRUE(prepareDeferred() && commit());
    EXPECT_TRUE(m_popupResult.submitted);
}

TEST_F(UiPopupInputTests, DeferredEditorRejectsCompletedPasteFromPriorPopupLifetime){
    m_popupState.open();
    ASSERT_TRUE(prepareDeferred() && commit());
    ASSERT_TRUE(prepareDeferred() && commit());
    key(Core::Key::V, true);
    ASSERT_TRUE(prepareDeferred() && commit());
    ASSERT_TRUE(m_clipboard.pump());
    const auto old = m_clipboard.startedToken;
    ASSERT_TRUE(old.valid());
    ASSERT_TRUE(m_clipboard.complete(old, "late paste"));
    m_popupState.close();
    m_popupState.open();
    ASSERT_TRUE(prepareDeferred());
    EXPECT_EQ(m_popupModel.text(), "popup");
    EXPECT_FALSE(m_popupResult.textChanged);
    EXPECT_FALSE(m_popupModel.canUndo());
    EXPECT_FALSE(m_clipboard.complete(old, "later"));
    ASSERT_TRUE(commit());
}


TEST_F(UiPopupInputTests, DeferredEditorRejectsNativeIntentionAfterCompositionReturnsToIdenticalState){
    m_popupState.open();
    ASSERT_TRUE(prepareDeferred() && commit());
    ASSERT_TRUE(prepareDeferred() && commit());
    const auto old = m_textInput.activeSession();
    ASSERT_TRUE(old.valid());
    ASSERT_EQ(m_textInput.commit(old, "late"), TextInputAdmission::Accepted);
    const u64 revision = m_popupModel.revision();
    const u64 generation = m_popupModel.compositionGeneration();
    ASSERT_TRUE(m_popupModel.beginComposition());
    ASSERT_TRUE(m_popupModel.updateComposition("한", 0u, 3u));
    m_popupModel.cancelComposition();
    ASSERT_EQ(m_popupModel.revision(), revision);
    ASSERT_GT(m_popupModel.compositionGeneration(), generation);
    ASSERT_TRUE(prepareDeferred() && commit());
    EXPECT_EQ(m_popupModel.text(), "popup");
    EXPECT_FALSE(m_popupResult.textChanged);
    EXPECT_FALSE(m_popupModel.canUndo());
    EXPECT_NE(m_textInput.activeSession(), old);
    EXPECT_EQ(m_textInput.commit(old, "later"), TextInputAdmission::InvalidSession);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

