// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_multiline_clipboard_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditBoxTestSupport;

class UiMultilineClipboardTests : public UiEditBoxHostTests{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiMultilineClipboardTests, PasteNormalizesHardLinesAsOneReplacementAndUndoRecord){
    Ui::EditModel model(m_arena, {}, Ui::EditTextMode::Multiline);
    UiEditClipboardController controller(m_arena, m_clipboard);
    const UiTextEditOwner owner{ { 51u }, 1u, model.instanceGeneration() };
    ASSERT_TRUE(model.setText("ab\ncd"));
    ASSERT_TRUE(model.setSelection(1u, 4u));
    m_clipboard.delayed = true;
    ASSERT_EQ(controller.request(owner, model, Ui::EditClipboardAction::Paste).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(m_clipboard.pump());
    ASSERT_TRUE(m_clipboard.deliver(controller.token(), ClipboardStatus::Success, "x\r\ny\tz\xC2\x85"));
    const auto result = controller.drain(owner, model);
    EXPECT_EQ(result.status, UiEditClipboardStatus::Applied);
    EXPECT_TRUE(result.textChanged);
    EXPECT_TRUE(result.selectionChanged);
    EXPECT_EQ(model.text(), "ax\ny z\nd");
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), "ab\ncd");
    EXPECT_EQ(model.anchor(), 1u);
    EXPECT_EQ(model.caret(), 4u);
    EXPECT_FALSE(model.canUndo());
    ASSERT_TRUE(model.redo());
    EXPECT_EQ(model.text(), "ax\ny z\nd");
}

TEST_F(UiMultilineClipboardTests, CanonicalLineSelectionIsCopiedWithoutFlattening){
    Ui::EditModel model(m_arena, {}, Ui::EditTextMode::Multiline);
    UiEditClipboardController controller(m_arena, m_clipboard);
    const UiTextEditOwner owner{ { 51u }, 1u, model.instanceGeneration() };
    ASSERT_TRUE(model.setText("first\n\nlast\n"));
    ASSERT_TRUE(model.selectAll());
    const u64 selection = model.selectionGeneration();
    ASSERT_EQ(controller.request(owner, model, Ui::EditClipboardAction::Copy).status, UiEditClipboardStatus::Pending);
    ASSERT_TRUE(m_clipboard.pump());
    EXPECT_EQ(controller.drain(owner, model).status, UiEditClipboardStatus::Published);
    EXPECT_EQ(AStringView(m_clipboard.document), model.text());
    EXPECT_EQ(model.selectionGeneration(), selection);
    EXPECT_FALSE(model.canUndo());
}

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

