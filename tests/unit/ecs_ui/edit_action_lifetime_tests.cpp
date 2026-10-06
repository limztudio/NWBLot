// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_action_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_action_lifetime_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditActionTestSupport;

TEST_F(UiEditActionHostTests, FreshActionBindingAbandonsOnlyWhileTheModelIsLent){
    ASSERT_TRUE(m_model.setText("orphan"));
    ASSERT_TRUE(actionFrame());
    EXPECT_TRUE(m_result.abandoned);
    EXPECT_EQ(m_model.text(), "0");
    EXPECT_EQ(actionCount(Ui::EditAction::Abandon), 1u);
    ASSERT_TRUE(actionFrame());
    EXPECT_FALSE(m_result.abandoned);
    EXPECT_EQ(actionCount(Ui::EditAction::Abandon), 1u);
}

TEST_F(UiEditActionHostTests, ExternalSameTextAssignmentFencesQueuedInputWithoutAbandoningTheNewDraft){
    ASSERT_TRUE(activateActions());
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_TRUE(commitNative("late"));
    m_host.collectNative();
    ASSERT_TRUE(m_model.setText("0"));
    const u64 external = m_model.externalRevision();
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(m_model.text(), "0");
    EXPECT_EQ(m_model.externalRevision(), external);
    EXPECT_FALSE(m_result.abandoned);
    EXPECT_TRUE(m_actions.records.empty());
    EXPECT_EQ(m_textInput.commitFor(oldSession, "old"), TextInputAdmission::InvalidSession);
}

TEST_F(UiEditActionHostTests, ExternalDraftReplacementRemainsAuthoritativeWhileOldNativeTextIsDiscarded){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(commitNative("old"));
    m_host.collectNative();
    ASSERT_TRUE(m_model.setText("fresh"));
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(m_model.text(), "fresh");
    EXPECT_EQ(m_actions.committed, "0");
    EXPECT_TRUE(m_actions.records.empty());
    EXPECT_FALSE(m_result.abandoned);
}

TEST_F(UiEditActionHostTests, OmissionRetiresNativeOwnershipAndRestoresOnlyOnRedeclaration){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(m_model.text(), "42");
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    const u64 declaration = m_widget.declarationGeneration;
    ASSERT_TRUE(emptyFrame());
    EXPECT_EQ(m_model.text(), "42");
    EXPECT_TRUE(m_actions.records.empty());
    EXPECT_EQ(m_textInput.commitFor(oldSession, "late"), TextInputAdmission::InvalidSession);
    ASSERT_TRUE(actionFrame());
    EXPECT_NE(m_widget.declarationGeneration, declaration);
    EXPECT_EQ(m_model.text(), "0");
    EXPECT_TRUE(m_result.abandoned);
    EXPECT_EQ(actionCount(Ui::EditAction::Abandon), 1u);
}

TEST_F(UiEditActionHostTests, HostResetCannotTouchUnlentDraftButTheNextBindingAbandonsIt){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(actionFrame());
    m_host.reset();
    EXPECT_EQ(m_model.text(), "42");
    EXPECT_TRUE(m_actions.records.empty());
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(m_model.text(), "0");
    EXPECT_TRUE(m_result.abandoned);
    EXPECT_EQ(actionCount(Ui::EditAction::Abandon), 1u);
}

TEST_F(UiEditActionHostTests, DisableAbandonsDirtyDraftAndRejectsCollectedOldSessionText){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(actionFrame());
    ASSERT_TRUE(commitNative("late"));
    m_host.collectNative();
    Ui::EditBoxOptions disabled;
    disabled.enabled = false;
    ASSERT_TRUE(actionFrame(disabled));
    EXPECT_EQ(m_model.text(), "0");
    EXPECT_TRUE(m_result.abandoned);
    EXPECT_FALSE(m_result.focused);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(actionCount(Ui::EditAction::Abandon), 1u);
    ASSERT_TRUE(actionFrame(disabled));
    EXPECT_EQ(actionCount(Ui::EditAction::Abandon), 1u);
}

TEST_F(UiEditActionHostTests, ReadOnlyTransitionRestoresBeforeSnapshotAndKeepsSelectionCopyAvailable){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(actionFrame());
    Ui::EditBoxOptions readOnly;
    readOnly.readOnly = true;
    ASSERT_TRUE(actionFrame(readOnly));
    EXPECT_EQ(m_model.text(), "0");
    EXPECT_TRUE(m_result.abandoned);
    EXPECT_TRUE(m_result.focused);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(key(Core::Key::A, false, true));
    ASSERT_TRUE(key(Core::Key::C, false, true));
    ASSERT_TRUE(actionFrame(readOnly));
    EXPECT_EQ(m_model.selectedText(), "0");
    ASSERT_TRUE(m_clipboard.pump());
    ASSERT_TRUE(actionFrame(readOnly));
    EXPECT_EQ(m_clipboard.document, "0");
    EXPECT_EQ(m_model.text(), "0");
    EXPECT_EQ(m_actions.committed, "0");
}

TEST_F(UiEditActionHostTests, NativeFocusLossAndGainAbandonsRatherThanCommittingOrdinaryBlur){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(actionFrame());
    ASSERT_TRUE(commitNative("late"));
    m_host.collectNative();
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::FocusLost }));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::FocusGained }));
    EXPECT_EQ(m_model.text(), "42");
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(m_model.text(), "0");
    EXPECT_EQ(actionCount(Ui::EditAction::Blur), 0u);
    EXPECT_EQ(actionCount(Ui::EditAction::Abandon), 1u);
    EXPECT_FALSE(m_result.focused);
}

TEST_F(UiEditActionHostTests, FocusTransferCancelsPendingPasteBeforeOrderedBlurCanCommit){
    ASSERT_TRUE(activateActions());
    m_clipboard.document.assign("old paste");
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Core::Key::V, false, true));
    ASSERT_TRUE(actionFrame());
    ASSERT_TRUE(m_clipboard.pump());
    const ClipboardRequestToken request = m_clipboard.startedToken;
    ASSERT_TRUE(request.valid());
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(focusOther());
    EXPECT_FALSE(m_clipboard.deliver(request, ClipboardStatus::Success, "old paste"));
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(m_model.text(), "42");
    EXPECT_EQ(m_actions.committed, "42");
    EXPECT_EQ(actionCount(Ui::EditAction::Blur), 1u);
    ASSERT_TRUE(focusEditor());
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(m_model.text(), "42");
}

TEST_F(UiEditActionHostTests, CancelRestorationFencesNativeSessionAndDelayedClipboardCompletion){
    ASSERT_TRUE(activateActions());
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Core::Key::V, false, true));
    ASSERT_TRUE(actionFrame());
    ASSERT_TRUE(m_clipboard.pump());
    const ClipboardRequestToken request = m_clipboard.startedToken;
    ASSERT_TRUE(request.valid());
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_TRUE(key(Core::Key::Escape));
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(m_model.text(), "0");
    EXPECT_TRUE(m_result.cancelled);
    EXPECT_FALSE(m_clipboard.deliver(request, ClipboardStatus::Success, "late"));
    EXPECT_EQ(m_textInput.commitFor(oldSession, "late"), TextInputAdmission::InvalidSession);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

