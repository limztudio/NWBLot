// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_action_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_submit_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditActionTestSupport;

TEST_F(UiEditActionHostTests, HeldSubmitObservesReleaseAfterDraftEventsWithoutRepeating){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(replaceNative("42"));
    const Ui::InputEvent down{ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Enter };
    ASSERT_TRUE(dispatch(down));
    ASSERT_TRUE(actionFrame());
    EXPECT_FALSE(m_result.submitted);
    EXPECT_TRUE(m_actions.records.empty());
    EXPECT_EQ(m_model.text(), "42");
    ASSERT_TRUE(dispatch(down));
    ASSERT_TRUE(actionFrame());
    EXPECT_FALSE(m_result.submitted);
    EXPECT_TRUE(m_actions.records.empty());
    ASSERT_TRUE(commitNative("7"));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .key = Core::Key::Enter }));
    ASSERT_TRUE(commitNative("9"));
    ASSERT_TRUE(actionFrame());
    EXPECT_TRUE(m_result.submitted);
    ASSERT_EQ(actionCount(Ui::EditAction::Submit), 1u);
    EXPECT_EQ(m_actions.records[0u].text, "427");
    EXPECT_EQ(m_actions.committed, "427");
    EXPECT_EQ(m_model.text(), "4279");
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .key = Core::Key::Enter }));
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 1u);
}

TEST_F(UiEditActionHostTests, EscapeCancelsConcurrentKeyboardAndControllerSubmitsBeforeRelease){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(replaceNative("42"));
    const Ui::InputSource controller{ 7u, 3u };
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Enter }));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::CommandDown, .command = Ui::InputCommand::Submit, .source = controller }));
    ASSERT_TRUE(actionFrame());
    EXPECT_TRUE(m_actions.records.empty());
    ASSERT_TRUE(key(Core::Key::Escape));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Enter, .repeat = true }));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .key = Core::Key::Enter }));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::CommandUp, .source = controller }));
    ASSERT_TRUE(actionFrame());
    EXPECT_FALSE(m_result.submitted);
    EXPECT_TRUE(m_result.cancelled);
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 0u);
    EXPECT_EQ(actionCount(Ui::EditAction::Cancel), 1u);
    EXPECT_EQ(m_actions.committed, "0");
    EXPECT_EQ(m_model.text(), "0");
    ASSERT_TRUE(focusEditor());
    ASSERT_TRUE(actionFrame());
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 1u);
}

TEST_F(UiEditActionHostTests, FocusDepartureAndReturnCannotRestoreHeldEditorSubmit){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Enter }));
    ASSERT_TRUE(focusOther());
    ASSERT_TRUE(focusEditor());
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .key = Core::Key::Enter }));
    ASSERT_TRUE(actionFrame());
    EXPECT_FALSE(m_result.submitted);
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 0u);
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 1u);
}

TEST_F(UiEditActionHostTests, ReboundEditorModelCannotReceiveAnEarlierHeldSubmit){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Enter }));
    Ui::EditModel replacement(m_arena);
    ASSERT_TRUE(replacement.setText("replacement"));
    ASSERT_TRUE(prepare(replacement, {}, { 10.0f, 20.0f, 180.0f, 30.0f }, &m_actions));
    ASSERT_TRUE(commit());
    m_actions.clear();
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .key = Core::Key::Enter }));
    ASSERT_TRUE(prepare(replacement, {}, { 10.0f, 20.0f, 180.0f, 30.0f }, &m_actions));
    ASSERT_TRUE(commit());
    EXPECT_FALSE(m_result.submitted);
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 0u);
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(prepare(replacement, {}, { 10.0f, 20.0f, 180.0f, 30.0f }, &m_actions));
    ASSERT_TRUE(commit());
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 1u);
}

TEST_F(UiEditActionHostTests, ExternalDraftReplacementRetiresThePendingSubmit){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Enter }));
    ASSERT_TRUE(m_model.setText("external"));
    ASSERT_TRUE(actionFrame());
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .key = Core::Key::Enter }));
    ASSERT_TRUE(actionFrame());
    EXPECT_FALSE(m_result.submitted);
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 0u);
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(m_actions.committed, "external");
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 1u);
}

TEST_F(UiEditBoxHostTests, PlainEditorPolicyChangesFenceHeldSubmitEvenAfterRestoration){
    ASSERT_TRUE(activate());
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Enter }));
    Ui::EditBoxOptions options;
    options.readOnly = true;
    ASSERT_TRUE(frame(m_model, options));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .key = Core::Key::Enter }));
    ASSERT_TRUE(frame(m_model));
    EXPECT_FALSE(m_result.submitted);
    ASSERT_TRUE(key(Core::Key::Enter));
    ASSERT_TRUE(frame(m_model));
    EXPECT_TRUE(m_result.submitted);
}

TEST_F(UiEditActionHostTests, CompositionOwnedEnterRemainsCancelledAfterNativeCommitBeforeRelease){
    ASSERT_TRUE(activateActions());
    ASSERT_EQ(m_textInput.preedit("draft", 0u, 5u), TextInputAdmission::Accepted);
    ASSERT_TRUE(actionFrame());
    ASSERT_TRUE(m_model.composition().active);
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Enter }));
    ASSERT_TRUE(commitNative("X"));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .key = Core::Key::Enter }));
    ASSERT_TRUE(actionFrame());
    EXPECT_FALSE(m_result.submitted);
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 0u);
    EXPECT_EQ(m_model.text(), "0X");
    EXPECT_FALSE(m_model.composition().active);
}

TEST_F(UiEditActionHostTests, PreeditBeginningAndEndingDuringHeldEnterRetiresItsSubmit){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Enter }));
    ASSERT_EQ(m_textInput.preedit("draft", 0u, 5u), TextInputAdmission::Accepted);
    ASSERT_TRUE(commitNative("X"));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .key = Core::Key::Enter }));
    ASSERT_TRUE(actionFrame());
    EXPECT_FALSE(m_result.submitted);
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 0u);
    EXPECT_EQ(m_model.text(), "0X");
}

TEST_F(UiEditBoxHostTests, MultilineAcceptEditsOnPressWhileExplicitSubmitWaitsForRelease){
    Ui::EditModel multiline(m_arena, {}, Ui::EditTextMode::Multiline);
    ASSERT_TRUE(frame(multiline));
    ASSERT_TRUE(key(Core::Key::Tab));
    ASSERT_TRUE(frame(multiline));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Enter }));
    ASSERT_TRUE(frame(multiline));
    EXPECT_EQ(multiline.text(), "\n");
    EXPECT_FALSE(m_result.submitted);
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Enter, .repeat = true }));
    ASSERT_TRUE(frame(multiline));
    EXPECT_EQ(multiline.text(), "\n\n");
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .key = Core::Key::Enter }));
    ASSERT_TRUE(frame(multiline));
    EXPECT_EQ(multiline.text(), "\n\n");
    EXPECT_FALSE(m_result.submitted);
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyDown, .key = Core::Key::Enter, .control = true }));
    ASSERT_TRUE(frame(multiline));
    EXPECT_FALSE(m_result.submitted);
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::KeyUp, .key = Core::Key::Enter }));
    ASSERT_TRUE(frame(multiline));
    EXPECT_TRUE(m_result.submitted);
    EXPECT_EQ(multiline.text(), "\n\n");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

