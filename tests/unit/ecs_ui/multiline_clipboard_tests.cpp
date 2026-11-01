// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_multiline_clipboard_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditBoxTestSupport;

class UiMultilineClipboardTests : public UiEditBoxHostTests{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiMultilineClipboardTests, NormalizedCapacityFailurePreservesSelectionAndHistory){
    Ui::EditModel model(m_arena, { 6u }, Ui::EditTextMode::Multiline);
    UiEditClipboardController controller(m_arena, m_clipboard);
    const UiTextEditOwner owner{ { 51u }, 1u, model.instanceGeneration() };
    ASSERT_TRUE(model.setText("a\nb"));
    ASSERT_TRUE(model.setSelection(1u, 2u));
    const u64 revision = model.revision();
    const u64 selection = model.selectionGeneration();
    m_clipboard.delayed = true;
    ASSERT_EQ(controller.request(owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(m_clipboard.pump());
    ASSERT_TRUE(m_clipboard.deliver(controller.token(), ClipboardStatus::Success, "x\r\ny\r\nz"));
    EXPECT_EQ(controller.drain(owner, model).status, UiEditClipboardStatus::TooLarge);
    EXPECT_EQ(model.text(), "a\nb");
    EXPECT_EQ(model.anchor(), 1u);
    EXPECT_EQ(model.caret(), 2u);
    EXPECT_EQ(model.revision(), revision);
    EXPECT_EQ(model.selectionGeneration(), selection);
    EXPECT_FALSE(model.canUndo());
}

TEST_F(UiMultilineClipboardTests, UnsupportedControlsRejectBeforeReplacement){
    Ui::EditModel model(m_arena, {}, Ui::EditTextMode::Multiline);
    UiEditClipboardController controller(m_arena, m_clipboard);
    const UiTextEditOwner owner{ { 51u }, 1u, model.instanceGeneration() };
    ASSERT_TRUE(model.setText("a\nb"));
    ASSERT_TRUE(model.selectAll());
    const u64 selection = model.selectionGeneration();
    m_clipboard.delayed = true;
    ASSERT_EQ(controller.request(owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(m_clipboard.pump());
    ASSERT_TRUE(m_clipboard.deliver(controller.token(), ClipboardStatus::Success, "x\x01y"));
    EXPECT_EQ(controller.drain(owner, model).status, UiEditClipboardStatus::InvalidText);
    EXPECT_EQ(model.text(), "a\nb");
    EXPECT_EQ(model.anchor(), 0u);
    EXPECT_EQ(model.caret(), 3u);
    EXPECT_EQ(model.selectionGeneration(), selection);
    EXPECT_FALSE(model.canUndo());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

