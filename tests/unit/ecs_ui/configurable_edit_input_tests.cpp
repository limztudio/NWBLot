// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_navigation_host_fixture.h"
#include "edit_action_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_configurable_edit_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditNavigationTestSupport;
using UiEditActionTestSupport::UiEditActionHostTests;

class UiConfigurableEditInputTests : public UiEditNavigationHostTests{
protected:
    [[nodiscard]] bool command(const Ui::InputCommand::Enum command, const bool extend = false, const Ui::InputSource source = { 7u, 3u }){
        Ui::InputEvent event;
        event.type = Ui::InputEventType::CommandDown;
        event.source = source;
        event.command = command;
        event.extend = extend;
        event.edit = true;
        if(!dispatch(event))
            return false;
        event.type = Ui::InputEventType::CommandUp;
        return dispatch(event);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiConfigurableEditInputTests, EmptyKeyboardProfileLeavesNativeTextAndExplicitCommandsAvailable){
    ASSERT_TRUE(m_model.setText("abc"));
    ASSERT_TRUE(activate());
    ASSERT_TRUE(m_context.input().setBindings(nullptr, 0u));
    ASSERT_TRUE(key(Core::Key::Left));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.caret(), 3u);
    ASSERT_TRUE(command(Ui::InputCommand::Left));
    ASSERT_EQ(m_textInput.commit("X"), TextInputAdmission::Accepted);
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "abXc");
    EXPECT_EQ(m_model.caret(), 3u);
}

TEST_F(UiConfigurableEditInputTests, CopiedCommandRetainsItsMeaningAfterTheBindingProfileChanges){
    ASSERT_TRUE(m_model.setText("abc"));
    ASSERT_TRUE(activate());
    const Ui::InputKeyBinding left{ .key = Core::Key::F8, .command = Ui::InputCommand::Left };
    const Ui::InputKeyBinding erase{ .key = Core::Key::F8, .command = Ui::InputCommand::Backspace };
    ASSERT_TRUE(m_context.input().setBindings(&left, 1u));
    ASSERT_TRUE(key(Core::Key::F8));
    ASSERT_TRUE(m_context.input().setBindings(&erase, 1u));
    EXPECT_EQ(m_model.caret(), 3u);
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "abc");
    EXPECT_EQ(m_model.caret(), 2u);
    ASSERT_TRUE(key(Core::Key::F8));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "ac");
    EXPECT_EQ(m_model.caret(), 1u);
}

TEST_F(UiConfigurableEditInputTests, SemanticCancelFirstCancelsCompositionThenCancelsTheEditor){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    ASSERT_EQ(m_textInput.preedit("x", 1u, 1u), TextInputAdmission::Accepted);
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_model.composition().active);
    ASSERT_TRUE(command(Ui::InputCommand::Cancel));
    ASSERT_TRUE(frame(m_model));
    EXPECT_FALSE(m_model.composition().active);
    EXPECT_FALSE(m_result.cancelled);
    EXPECT_EQ(m_model.text(), "base");
    ASSERT_TRUE(command(Ui::InputCommand::Cancel));
    ASSERT_TRUE(frame(m_model));
    EXPECT_TRUE(m_result.cancelled);
    EXPECT_FALSE(m_context.input().focus().valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditActionHostTests, DuplicateSemanticAcceptDownSubmitsOncePerHeldSource){
    ASSERT_TRUE(activateActions());
    ASSERT_TRUE(replaceNative("42"));
    ASSERT_TRUE(actionFrame());
    ASSERT_EQ(m_model.text(), "42");
    Ui::InputEvent event;
    event.type = Ui::InputEventType::CommandDown;
    event.source = { 7u, 3u };
    event.command = Ui::InputCommand::Accept;
    ASSERT_TRUE(dispatch(event));
    ASSERT_TRUE(dispatch(event));
    event.type = Ui::InputEventType::CommandUp;
    ASSERT_TRUE(dispatch(event));
    EXPECT_TRUE(m_actions.records.empty());
    ASSERT_TRUE(actionFrame());
    EXPECT_TRUE(m_result.submitted);
    ASSERT_EQ(m_actions.records.size(), 1u);
    event.type = Ui::InputEventType::CommandDown;
    ASSERT_TRUE(dispatch(event));
    event.type = Ui::InputEventType::CommandUp;
    ASSERT_TRUE(dispatch(event));
    ASSERT_TRUE(actionFrame());
    EXPECT_EQ(actionCount(Ui::EditAction::Submit), 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

