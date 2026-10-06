// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_action_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_action_order_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditActionTestSupport;

TEST_F(UiEditActionHostTests, SubmitObservesItsExactPositionBeforeLaterCopiedNativeText){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(commitNative("7"));
    m_host.collectNative();
    EXPECT_EQ(m_model.text(), "0");
    ASSERT_TRUE(actionFrame());
    EXPECT_TRUE(m_result.submitted);
    EXPECT_TRUE(m_result.focused);
    EXPECT_EQ(m_actions.committed, "42");
    EXPECT_EQ(m_model.text(), "427");
    ASSERT_EQ(m_actions.records.size(), 1u);
    EXPECT_EQ(m_actions.records[0u].text, "42");
    EXPECT_EQ(m_model.caret(), 3u);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "42");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "0");
}

TEST_F(UiEditActionHostTests, CanonicalizingSubmitPreservesLaterCopiedCommitWhileRetiringItsNativeSession){
    ASSERT_TRUE(activateActions());
    m_actions.canonicalizeSubmit = true;
    const u64 external = m_model.externalRevision();
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(commitNative("7"));
    m_host.collectNative();
    EXPECT_EQ(m_model.text(), "0");
    ASSERT_TRUE(actionFrame());
    EXPECT_TRUE(m_result.submitted);
    EXPECT_TRUE(m_result.focused);
    EXPECT_EQ(m_actions.committed, "42");
    EXPECT_EQ(m_model.text(), "427");
    EXPECT_EQ(m_model.externalRevision(), external + 1u);
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 3u);
    ASSERT_EQ(m_actions.records.size(), 1u);
    EXPECT_EQ(m_actions.records[0u].text, "42");
    ASSERT_TRUE(m_textInput.activeSession().valid());
    EXPECT_NE(m_textInput.activeSession(), oldSession);
    EXPECT_EQ(m_textInput.commitFor(oldSession, "late"), TextInputAdmission::InvalidSession);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "42");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditActionHostTests, SeveralSubmitsInOneBatchEachObserveTheirOwnDraft){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(replaceNative("12"));
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(commitNative("3"));
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(commitNative("4"));
    ASSERT_TRUE(actionFrame());
    ASSERT_EQ(m_actions.records.size(), 2u);
    EXPECT_EQ(m_actions.records[0u].text, "12");
    EXPECT_EQ(m_actions.records[1u].text, "123");
    EXPECT_EQ(m_actions.committed, "123");
    EXPECT_EQ(m_model.text(), "1234");
}

TEST_F(UiEditActionHostTests, CancelRestoresLastSubmittedDraftAndFencesRemainingOldOwnerText){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(commitNative("7"));
    ASSERT_TRUE(key(Core::Key::Escape));
    ASSERT_TRUE(commitNative("9"));
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_TRUE(actionFrame());
    EXPECT_TRUE(m_result.submitted);
    EXPECT_TRUE(m_result.cancelled);
    EXPECT_FALSE(m_result.focused);
    EXPECT_EQ(m_actions.committed, "42");
    EXPECT_EQ(m_model.text(), "42");
    ASSERT_EQ(m_actions.records.size(), 2u);
    EXPECT_EQ(m_actions.records[1u].action, Ui::EditAction::Cancel);
    EXPECT_EQ(m_actions.records[1u].text, "427");
    EXPECT_EQ(m_textInput.commitFor(oldSession, "late"), TextInputAdmission::InvalidSession);
}

TEST_F(UiEditActionHostTests, TextBeforeTabIsLentBeforeItsOrderedBlurBoundary){
    ASSERT_TRUE(activateActions());
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(focusOther());
    EXPECT_EQ(m_model.text(), "0");
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_textInput.commitFor(oldSession, "late"), TextInputAdmission::InvalidSession);
    ASSERT_TRUE(actionFrame());
    EXPECT_TRUE(m_result.blurred);
    EXPECT_FALSE(m_result.focused);
    EXPECT_EQ(m_model.text(), "42");
    EXPECT_EQ(m_actions.committed, "42");
    ASSERT_EQ(m_actions.records.size(), 1u);
    EXPECT_EQ(m_actions.records[0u].action, Ui::EditAction::Blur);
    EXPECT_EQ(m_actions.records[0u].text, "42");
    EXPECT_FALSE(m_model.canUndo());
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(actionCount(Ui::EditAction::Blur), 1u);
}

TEST_F(UiEditActionHostTests, BlurRestorationOccursBeforeTheCurrentPaintSnapshot){
    ASSERT_TRUE(activateActions());
    m_actions.rejectBlur = true;
    ASSERT_TRUE(replaceNative("-"));
    ASSERT_TRUE(focusOther());
    ASSERT_TRUE(actionFrame());
    ASSERT_EQ(m_actions.records.size(), 1u);
    EXPECT_EQ(m_actions.records[0u].text, "-");
    EXPECT_EQ(m_actions.committed, "0");
    EXPECT_EQ(m_model.text(), "0");
    EXPECT_EQ(m_model.caret(), 1u);
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_TRUE(m_result.blurred);
    EXPECT_TRUE(m_result.textChanged);
}

TEST_F(UiEditActionHostTests, BlurThenRefocusSameWidgetRetainsNewEpochCharacterAfterRestoration){
    ASSERT_TRUE(activateActions());
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(focusOther());
    ASSERT_TRUE(focusEditor());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(m_host.character('7'));
    EXPECT_EQ(m_textInput.commitFor(oldSession, "old"), TextInputAdmission::InvalidSession);
    ASSERT_TRUE(actionFrame());
    EXPECT_TRUE(m_result.blurred);
    EXPECT_TRUE(m_result.focused);
    EXPECT_EQ(m_actions.committed, "42");
    EXPECT_EQ(m_model.text(), "427");
    ASSERT_EQ(m_actions.records.size(), 1u);
    EXPECT_EQ(m_actions.records[0u].text, "42");
    EXPECT_NE(m_textInput.activeSession(), oldSession);
}

TEST_F(UiEditActionHostTests, OldEpochCancelDoesNotClearLaterRefocusOrApplyItsRetiredBlur){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(commitNative("7"));
    ASSERT_TRUE(key(Core::Key::Escape));
    ASSERT_TRUE(focusOther());
    ASSERT_TRUE(focusEditor());
    ASSERT_TRUE(m_host.character('8'));
    ASSERT_TRUE(actionFrame());
    EXPECT_TRUE(m_result.cancelled);
    EXPECT_TRUE(m_result.focused);
    EXPECT_EQ(m_actions.committed, "42");
    EXPECT_EQ(m_model.text(), "428");
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 1u);
    EXPECT_EQ(actionCount(Ui::EditAction::Cancel), 1u);
    EXPECT_EQ(actionCount(Ui::EditAction::Blur), 0u);
}

TEST_F(UiEditActionHostTests, ActivePreeditOwnsEnterAndFirstEscapeBeforeNumericCancel){
    ASSERT_TRUE(activateActions());
    ASSERT_EQ(m_textInput.preedit("\xEA\xB0\x80", 3u, 3u), TextInputAdmission::Accepted);
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(actionFrame());
    EXPECT_TRUE(m_model.composition().active);
    EXPECT_FALSE(m_result.submitted);
    EXPECT_TRUE(m_actions.records.empty());
    ASSERT_TRUE(key(Core::Key::Escape));
    ASSERT_TRUE(actionFrame());
    EXPECT_FALSE(m_model.composition().active);
    EXPECT_FALSE(m_result.cancelled);
    EXPECT_TRUE(m_result.focused);
    EXPECT_TRUE(m_actions.records.empty());
    ASSERT_TRUE(key(Core::Key::Escape));
    ASSERT_TRUE(actionFrame());
    EXPECT_TRUE(m_result.cancelled);
    EXPECT_FALSE(m_result.focused);
    ASSERT_EQ(m_actions.records.size(), 1u);
    EXPECT_EQ(m_actions.records[0u].action, Ui::EditAction::Cancel);
}

TEST_F(UiEditActionHostTests, FailedSubmitStopsLaterEventsAndKeepsPreviouslyAcceptedTargets){
    ASSERT_TRUE(activateActions());
    const u64 accepted = m_context.input().layoutGeneration();
    const usize targets = m_context.input().targets().size();
    m_actions.failSubmit = true;
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(commitNative("7"));
    EXPECT_FALSE(actionFrame());
    EXPECT_FALSE(m_result.valid);
    EXPECT_EQ(m_model.text(), "42");
    EXPECT_EQ(m_actions.committed, "0");
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 1u);
    EXPECT_EQ(m_context.input().layoutGeneration(), accepted);
    EXPECT_EQ(m_context.input().targets().size(), targets);
}

TEST_F(UiEditActionHostTests, HostResetReentryRejectsBeforeDestroyingBorrowedEntryStorage){
    ASSERT_TRUE(activateActions());
    const u64 accepted = m_context.input().layoutGeneration();
    const usize targets = m_context.input().targets().size();
    m_actions.resetHost = &m_host;
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(commitNative("7"));
    EXPECT_FALSE(actionFrame());
    EXPECT_FALSE(m_result.valid);
    EXPECT_EQ(m_model.text(), "42");
    EXPECT_EQ(m_actions.committed, "42");
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 1u);
    EXPECT_EQ(m_context.input().layoutGeneration(), accepted);
    EXPECT_EQ(m_context.input().targets().size(), targets);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

