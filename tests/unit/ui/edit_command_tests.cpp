// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_fixture.h"

#include <impl/ecs_ui/toolkit/edit/commands.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_command_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiEditTests;

class UiEditCommandTests : public EditFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditCommandTests, ShortcutTranslationPreservesWordSelectionAndLeavesAltGrWithOs){
    const auto left = TranslateEditCommand({ EditKey::Left, true, true, false, true });
    EXPECT_EQ(left.command, EditCommand::WordLeft);
    EXPECT_TRUE(left.extend);
    EXPECT_TRUE(left.repeat);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Right, true }).command, EditCommand::WordRight);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Backspace, true }).command, EditCommand::WordBackspace);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Delete, true }).command, EditCommand::WordDelete);
    EXPECT_EQ(TranslateEditCommand({ EditKey::A, true }).command, EditCommand::SelectAll);
    EXPECT_EQ(TranslateEditCommand({ EditKey::C, true }).command, EditCommand::Copy);
    EXPECT_EQ(TranslateEditCommand({ EditKey::X, true }).command, EditCommand::Cut);
    EXPECT_EQ(TranslateEditCommand({ EditKey::V, true }).command, EditCommand::Paste);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Z, true }).command, EditCommand::Undo);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Z, true, true }).command, EditCommand::Redo);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Y, true }).command, EditCommand::Redo);
    EXPECT_EQ(TranslateEditCommand({ EditKey::V }).command, EditCommand::None);
    EXPECT_EQ(TranslateEditCommand({ EditKey::V, true, false, true }).command, EditCommand::None);
    EXPECT_EQ(TranslateEditCommand({ static_cast<EditKey::Enum>(255u) }).command, EditCommand::None);
}

TEST_F(UiEditCommandTests, ShiftMovementSelectsWholeGraphemesAndArrowsCollapseAtSelectionEdges){
    ASSERT_TRUE(m_model.setText("a\xCC\x81z"));
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::Home }).selectionChanged);
    const auto selected = ApplyEditCommand(m_model, { EditCommand::Right, true });
    EXPECT_TRUE(selected.handled);
    EXPECT_TRUE(selected.selectionChanged);
    EXPECT_FALSE(selected.textChanged);
    EXPECT_EQ(m_model.selectedText(), "a\xCC\x81");
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::Left }).selectionChanged);
    EXPECT_EQ(m_model.caret(), 0u);
    ASSERT_TRUE(m_model.selectAll());
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::Right }).selectionChanged);
    EXPECT_EQ(m_model.caret(), 4u);
    EXPECT_FALSE(m_model.hasSelection());
}

TEST_F(UiEditCommandTests, WordDeletionUndoRestoresOriginalCaretAndSelectedDeletionPreservesItsUndoSelection){
    ASSERT_TRUE(m_model.setText("one two"));
    const auto erased = ApplyEditCommand(m_model, { EditCommand::WordBackspace });
    EXPECT_TRUE(erased.textChanged);
    EXPECT_EQ(m_model.text(), "one ");
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::Undo }).textChanged);
    EXPECT_EQ(m_model.text(), "one two");
    EXPECT_EQ(m_model.anchor(), 7u);
    EXPECT_EQ(m_model.caret(), 7u);
    ASSERT_TRUE(m_model.setSelection(0u, 0u));
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::WordDelete }).textChanged);
    EXPECT_EQ(m_model.text(), "two");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.caret(), 0u);
    ASSERT_TRUE(m_model.setSelection(0u, 3u));
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::WordDelete }).textChanged);
    EXPECT_EQ(m_model.text(), " two");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.anchor(), 0u);
    EXPECT_EQ(m_model.caret(), 3u);
}

TEST_F(UiEditCommandTests, ReadOnlyConsumesMutationsAndAllowsSelectionAndCopy){
    ASSERT_TRUE(m_model.setText("abcd"));
    const EditCommand::Enum mutations[]{ EditCommand::Backspace, EditCommand::Delete, EditCommand::WordBackspace,
        EditCommand::WordDelete, EditCommand::Cut, EditCommand::Paste, EditCommand::Undo, EditCommand::Redo };
    for(const auto command : mutations){
        const auto result = ApplyEditCommand(m_model, { command }, true);
        EXPECT_TRUE(result.handled);
        EXPECT_FALSE(result.textChanged);
        EXPECT_EQ(result.clipboard, EditClipboardAction::None);
        EXPECT_EQ(m_model.text(), "abcd");
    }
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::SelectAll }, true).selectionChanged);
    EXPECT_EQ(ApplyEditCommand(m_model, { EditCommand::Copy }, true).clipboard, EditClipboardAction::Copy);
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::Submit }, true).submitted);
}

TEST_F(UiEditCommandTests, RepeatsEditTextButNeverDuplicateClipboardSubmitOrCancelRequests){
    ASSERT_TRUE(m_model.setText("abcd"));
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::Backspace, false, true }).textChanged);
    EXPECT_EQ(m_model.text(), "abc");
    const EditCommand::Enum oneShot[]{ EditCommand::Copy, EditCommand::Cut, EditCommand::Paste,
        EditCommand::SelectAll, EditCommand::Submit, EditCommand::Cancel };
    for(const auto command : oneShot){
        const auto result = ApplyEditCommand(m_model, { command, false, true });
        EXPECT_TRUE(result.handled);
        EXPECT_FALSE(result.submitted);
        EXPECT_FALSE(result.cancelled);
        EXPECT_FALSE(result.selectionChanged);
        EXPECT_EQ(result.clipboard, EditClipboardAction::None);
    }
}

TEST_F(UiEditCommandTests, PreeditOwnsCommandsAndEnterWhileEscapeFirstCancelsOnlyPreedit){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("draft", 0u, 5u));
    const EditCommand::Enum nativeOwned[]{ EditCommand::Backspace, EditCommand::Right, EditCommand::SelectAll,
        EditCommand::Copy, EditCommand::Cut, EditCommand::Paste, EditCommand::Undo, EditCommand::Submit };
    for(const auto command : nativeOwned){
        const auto result = ApplyEditCommand(m_model, { command });
        EXPECT_TRUE(result.handled);
        EXPECT_FALSE(result.textChanged);
        EXPECT_FALSE(result.submitted);
        EXPECT_EQ(result.clipboard, EditClipboardAction::None);
        EXPECT_EQ(m_model.composition().text, "draft");
    }
    const auto cancelled = ApplyEditCommand(m_model, { EditCommand::Cancel });
    EXPECT_TRUE(cancelled.compositionChanged);
    EXPECT_FALSE(cancelled.cancelled);
    EXPECT_FALSE(m_model.composition().active);
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::Cancel }).cancelled);
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::Submit }).submitted);
}

TEST_F(UiEditCommandTests, ClipboardCommandsDescribeExchangeWithoutEditingTheModel){
    ASSERT_TRUE(m_model.setText("abcd"));
    ASSERT_TRUE(m_model.setSelection(1u, 3u));
    EXPECT_EQ(ApplyEditCommand(m_model, { EditCommand::Copy }).clipboard, EditClipboardAction::Copy);
    EXPECT_EQ(ApplyEditCommand(m_model, { EditCommand::Cut }).clipboard, EditClipboardAction::Cut);
    EXPECT_EQ(ApplyEditCommand(m_model, { EditCommand::Paste }).clipboard, EditClipboardAction::Paste);
    EXPECT_EQ(m_model.text(), "abcd");
    EXPECT_EQ(m_model.selectedText(), "bc");
    EXPECT_FALSE(ApplyEditCommand(m_model, {}).handled);
    EXPECT_FALSE(ApplyEditCommand(m_model, { static_cast<EditCommand::Enum>(255u) }).handled);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

