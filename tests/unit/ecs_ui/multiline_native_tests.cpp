// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_multiline_native_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditBoxTestSupport;

class UiMultilineNativeTests : public UiEditBoxHostTests{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiMultilineNativeTests, NoncanonicalPreeditRejectsRatherThanChangingNativeOffsets){
    Ui::EditModel model(m_arena, {}, Ui::EditTextMode::Multiline);
    UiTextEditSession session(m_arena, m_textInput);
    const UiTextEditOwner owner{ { 81u }, 1u, model.instanceGeneration() };
    ASSERT_TRUE(model.setText("a\nb"));
    const u64 selection = model.selectionGeneration();
    ASSERT_EQ(session.begin(owner, model, { 30, 40, 1, 18 }), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.preedit("x\r\ny", 1u, 4u), TextInputAdmission::Accepted);
    EXPECT_EQ(session.drain(owner, model).status, UiTextEditStatus::ModelRejected);
    EXPECT_EQ(model.text(), "a\nb");
    EXPECT_EQ(model.selectionGeneration(), selection);
    EXPECT_FALSE(model.composition().active);
    EXPECT_FALSE(session.token().valid());
}

TEST_F(UiMultilineNativeTests, LaterOversizedCommitPreservesEarlierAcceptedNativeEdit){
    Ui::EditModel model(m_arena, { 6u }, Ui::EditTextMode::Multiline);
    UiTextEditSession session(m_arena, m_textInput);
    const UiTextEditOwner owner{ { 81u }, 1u, model.instanceGeneration() };
    ASSERT_TRUE(model.setText("a\nb"));
    ASSERT_EQ(session.begin(owner, model, { 30, 40, 1, 18 }), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
    ASSERT_EQ(m_textInput.commit("y\r\nzz"), TextInputAdmission::Accepted);
    const auto result = session.drain(owner, model);
    EXPECT_EQ(result.status, UiTextEditStatus::ModelRejected);
    EXPECT_EQ(result.eventsApplied, 1u);
    EXPECT_EQ(model.text(), "a\nbx");
    EXPECT_FALSE(session.token().valid());
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), "a\nb");
    EXPECT_FALSE(model.canUndo());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

