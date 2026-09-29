// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_box_host_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;

using namespace UiEditBoxTestSupport;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditBoxHostTests, NativeCommitLocalLeftAndNativeCommitRetainTheirEventOrder){
    ASSERT_TRUE(activate());
    ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
    ASSERT_TRUE(key(Ui::InputKey::Left));
    ASSERT_EQ(m_textInput.commit("y"), TextInputAdmission::Accepted);
    m_host.collectNative();
    EXPECT_EQ(m_model.text(), "");
    ASSERT_TRUE(frame(m_model));
    EXPECT_TRUE(m_result.textChanged);
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_EQ(m_model.text(), "yx");
    EXPECT_EQ(m_model.caret(), 1u);
    ASSERT_TRUE(frame(m_model));
    EXPECT_FALSE(m_result.textChanged);
    EXPECT_EQ(m_model.text(), "yx");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "x");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "");
}

TEST_F(UiEditBoxHostTests, QueuedCommitCannotReachAReplacementModelAtTheSameWidget){
    ASSERT_TRUE(m_model.setText("old"));
    ASSERT_TRUE(activate());
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    m_host.collectNative();
    Ui::EditModel replacement(m_arena);
    ASSERT_TRUE(replacement.setText("replacement"));
    ASSERT_TRUE(frame(replacement));
    EXPECT_EQ(m_model.text(), "old");
    EXPECT_EQ(replacement.text(), "replacement");
    EXPECT_FALSE(replacement.canUndo());
    EXPECT_EQ(m_textInput.commitFor(oldSession, "later"), TextInputAdmission::InvalidSession);
}

TEST_F(UiEditBoxHostTests, RemovingThenRedeclaringTheWidgetFencesItsOldNativeQueue){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    const Ui::WidgetState oldWidget = m_widget;
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(emptyFrame());
    EXPECT_FALSE(m_host.hasTextFocus());
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_widget.id, oldWidget.id);
    EXPECT_NE(m_widget.declarationGeneration, oldWidget.declarationGeneration);
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_EQ(m_textInput.commitFor(oldSession, "later"), TextInputAdmission::InvalidSession);
}

TEST_F(UiEditBoxHostTests, ExternalSameTextResetFencesAnAlreadyCollectedCommit){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    const u64 revision = m_model.revision();
    const u64 externalRevision = m_model.externalRevision();
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(m_model.setText("base"));
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_GT(m_model.externalRevision(), externalRevision);
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_EQ(m_textInput.commitFor(oldSession, "later"), TextInputAdmission::InvalidSession);
}

TEST_F(UiEditBoxHostTests, FocusLossAndImmediateRefocusCannotReviveAnOwnedOldCommit){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    m_host.collectNative();
    m_context.input().clearFocus();
    m_host.synchronizeFocus();
    ASSERT_TRUE(key(Ui::InputKey::Tab));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_NE(m_textInput.activeSession(), oldSession);
    ASSERT_EQ(m_textInput.commit("fresh"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "basefresh");
}

TEST_F(UiEditBoxHostTests, LosingFocusCancelsPreeditAtTheNextModelLoanAndRefocusStartsFresh){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.preedit("IME", 0u, 3u, false), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_TRUE(m_model.composition().active);
    EXPECT_EQ(m_model.composition().text, "IME");
    EXPECT_FALSE(m_result.preeditCaretVisible);
    m_context.input().clearFocus();
    m_host.synchronizeFocus();
    ASSERT_TRUE(frame(m_model));
    EXPECT_FALSE(m_model.composition().active);
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_FALSE(m_host.hasTextFocus());
    ASSERT_TRUE(key(Ui::InputKey::Tab));
    ASSERT_TRUE(frame(m_model));
    EXPECT_NE(m_textInput.activeSession(), oldSession);
    ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "basex");
}

TEST_F(UiEditBoxHostTests, ReadOnlyPolicyChangeDiscardsQueuedMutationAndNativeSession){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    m_host.collectNative();
    Ui::EditBoxOptions options;
    options.readOnly = true;
    ASSERT_TRUE(frame(m_model, options));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_TRUE(m_host.character('x'));
    ASSERT_TRUE(frame(m_model, options));
    EXPECT_EQ(m_model.text(), "base");
    options.readOnly = false;
    ASSERT_TRUE(frame(m_model, options));
    ASSERT_TRUE(m_textInput.activeSession().valid());
    ASSERT_EQ(m_textInput.commit("fresh"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model, options));
    EXPECT_EQ(m_model.text(), "basefresh");
}

TEST_F(UiEditBoxHostTests, SurroundingDeletionCannotFollowALocalMoveAgainstOldNativeCaret){
    ASSERT_TRUE(m_model.setText("abcd"));
    ASSERT_TRUE(activate());
    ASSERT_TRUE(key(Ui::InputKey::Left));
    ASSERT_EQ(m_textInput.erase(1u, 0u), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "abcd");
    EXPECT_EQ(m_model.caret(), 3u);
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, CandidateGeometryCannotMoveTheNativeCaretBeforeContextAcceptance){
    ASSERT_TRUE(m_model.setText("abc"));
    ASSERT_TRUE(activate());
    const TextInputRect displayed = m_textInput.nativeCaret();
    ASSERT_TRUE(prepare(m_model, {}, { 200.0f, 20.0f, 180.0f, 30.0f }));
    EXPECT_EQ(m_context.input().layoutGeneration(), m_generation - 1u);
    m_host.commitFrame(m_generation);
    EXPECT_EQ(m_textInput.nativeCaret().x, displayed.x);
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PrimaryDown, .position = { 20.0f, 25.0f } }));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PrimaryUp, .position = { 20.0f, 25.0f } }));
    ASSERT_TRUE(commit());
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.caret(), 1u);
    EXPECT_EQ(m_model.anchor(), 1u);
}

TEST_F(UiEditBoxHostTests, HostEventOverflowDiscardsTheBoundedBatchBeforeAnyModelMutation){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    usize accepted = 0u;
    for(usize index = 0u; index < Ui::s_InputMaxEvents + 2u; ++index){
        if(!m_textInput.activeSession().valid())
            break;
        ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
        ++accepted;
        m_host.collectNative();
    }
    EXPECT_LE(accepted, Ui::s_InputMaxEvents + 1u);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_model.text(), "base");
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, NativeEventOverflowCancelsTheBatchAndCanRecoverOnANewDeclaration){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    for(usize index = 0u; index < s_TextInputMaxEvents; ++index)
        ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
    EXPECT_EQ(m_textInput.commit("overflow"), TextInputAdmission::QueueFull);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
    ASSERT_TRUE(m_textInput.activeSession().valid());
    ASSERT_EQ(m_textInput.commit("ok"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "baseok");
}

TEST_F(UiEditBoxHostTests, NativeCommitPathConsumesCharacterFallbackWithoutDuplicatingIt){
    ASSERT_TRUE(activate());
    EXPECT_TRUE(m_host.character('x'));
    ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "x");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, DelayedPasteAppliesOnlyAfterSuccessfulCompletion){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::V, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_clipboard.pump());
    EXPECT_EQ(m_clipboard.startedOperation, ClipboardOperation::ReadText);
    EXPECT_EQ(m_model.text(), "base");
    ASSERT_TRUE(m_clipboard.deliver(m_clipboard.startedToken, ClipboardStatus::Success, "paste"));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "basepaste");
    EXPECT_TRUE(m_result.textChanged);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, QueuedNativeEditFencesACompletedPasteBeforeModelMutation){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::V, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_clipboard.pump());
    const auto old = m_clipboard.startedToken;
    ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(m_clipboard.deliver(old, ClipboardStatus::Success, "old"));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "basex");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, QueuedLocalSelectionFencesCompletedCutWithoutDeletingOldSelection){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    ASSERT_TRUE(key(Ui::InputKey::A, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_EQ(m_model.selectedText(), "base");
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::X, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_clipboard.pump());
    const auto old = m_clipboard.startedToken;
    EXPECT_EQ(m_clipboard.startedOperation, ClipboardOperation::WriteText);
    ASSERT_TRUE(key(Ui::InputKey::Left));
    ASSERT_TRUE(m_clipboard.deliver(old, ClipboardStatus::Success));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_EQ(m_model.caret(), 0u);
    EXPECT_FALSE(m_model.hasSelection());
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_EQ(m_clipboard.document, "base");
}

TEST_F(UiEditBoxHostTests, ClipboardCompletionCannotSurviveFocusLossAndImmediateRefocus){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::V, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_clipboard.pump());
    const auto old = m_clipboard.startedToken;
    m_context.input().clearFocus();
    m_host.synchronizeFocus();
    ASSERT_TRUE(key(Ui::InputKey::Tab));
    ASSERT_TRUE(frame(m_model));
    EXPECT_FALSE(m_clipboard.deliver(old, ClipboardStatus::Success, "old"));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, RemovingReadOnlyCopyOwnerPreservesCopiedNativePublication){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    Ui::EditBoxOptions options;
    options.readOnly = true;
    ASSERT_TRUE(frame(m_model, options));
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(key(Ui::InputKey::A, false, true));
    ASSERT_TRUE(frame(m_model, options));
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::C, false, true));
    ASSERT_TRUE(frame(m_model, options));
    ASSERT_TRUE(m_clipboard.pump());
    const auto old = m_clipboard.startedToken;
    EXPECT_EQ(m_clipboard.startedOperation, ClipboardOperation::WriteText);
    ASSERT_TRUE(emptyFrame());
    EXPECT_TRUE(m_clipboard.deliver(old, ClipboardStatus::Success));
    EXPECT_EQ(m_clipboard.document, "base");
    EXPECT_EQ(m_model.text(), "base");
}

TEST_F(UiEditBoxHostTests, ClipboardCompletionCannotReachIdenticalExternallyResetModel){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::V, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_clipboard.pump());
    const auto old = m_clipboard.startedToken;
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(frame(m_model));
    EXPECT_FALSE(m_clipboard.deliver(old, ClipboardStatus::Success, "old"));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, QueuedCopyThenPastePreservesAsynchronousNativePublicationOrder){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    ASSERT_TRUE(key(Ui::InputKey::A, false, true));
    ASSERT_TRUE(frame(m_model));
    m_clipboard.document.assign("old");
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::C, false, true));
    ASSERT_TRUE(key(Ui::InputKey::V, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_clipboard.pump());
    ASSERT_EQ(m_clipboard.startedOperation, ClipboardOperation::WriteText);
    EXPECT_EQ(m_clipboard.startedText, "base");
    ASSERT_TRUE(m_clipboard.deliver(m_clipboard.startedToken, ClipboardStatus::Success));
    ASSERT_TRUE(m_clipboard.pump());
    ASSERT_EQ(m_clipboard.startedOperation, ClipboardOperation::ReadText);
    ASSERT_TRUE(m_clipboard.deliver(m_clipboard.startedToken, ClipboardStatus::Success, m_clipboard.document));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

