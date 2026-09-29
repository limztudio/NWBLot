// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "multiline_fixture.h"

#include <impl/ui/edit/commands.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_multiline_command_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiMultilineTests;
using MultilineCommandTests = MultilineFixture;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(MultilineCommandTests, TranslationPreservesDefaultSingleLineIdentityAndAddsMultilineIntents){
    EXPECT_EQ(TranslateEditCommand({ EditKey::Enter }).command, EditCommand::Submit);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Enter, false, true }).command, EditCommand::Submit);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Home, true }).command, EditCommand::Home);
    EXPECT_EQ(TranslateEditCommand({ EditKey::End, true }).command, EditCommand::End);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Enter }, EditTextMode::Multiline).command, EditCommand::Newline);
    const EditCommandRequest shifted = TranslateEditCommand({ EditKey::Enter, false, true, false, true }, EditTextMode::Multiline);
    EXPECT_EQ(shifted.command, EditCommand::Newline);
    EXPECT_TRUE(shifted.extend);
    EXPECT_TRUE(shifted.repeat);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Enter, true }, EditTextMode::Multiline).command, EditCommand::Submit);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Enter, true, true }, EditTextMode::Multiline).command, EditCommand::Submit);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Home, true }, EditTextMode::Multiline).command, EditCommand::DocumentHome);
    EXPECT_EQ(TranslateEditCommand({ EditKey::End, true }, EditTextMode::Multiline).command, EditCommand::DocumentEnd);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Home }, EditTextMode::Multiline).command, EditCommand::Home);
    EXPECT_EQ(TranslateEditCommand({ EditKey::End }, EditTextMode::Multiline).command, EditCommand::End);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Enter, false, false, true }, EditTextMode::Multiline).command, EditCommand::None);
    EXPECT_EQ(TranslateEditCommand({ EditKey::Enter }, static_cast<EditTextMode::Enum>(255u)).command, EditCommand::None);
}

TEST_F(MultilineCommandTests, EnterAndRepeatsInsertOneLfPerCommandAsUndoableEdits){
    ASSERT_TRUE(m_model.setText("ab"));
    ASSERT_TRUE(m_model.setSelection(1u, 1u));
    const EditCommandResult inserted = ApplyEditCommand(m_model, TranslateEditCommand({ EditKey::Enter }, m_model.textMode()));
    EXPECT_TRUE(inserted.handled);
    EXPECT_TRUE(inserted.textChanged);
    EXPECT_TRUE(inserted.selectionChanged);
    EXPECT_FALSE(inserted.submitted);
    EXPECT_EQ(m_model.text(), "a\nb");
    const EditCommandResult repeated = ApplyEditCommand(m_model,
        TranslateEditCommand({ EditKey::Enter, false, false, false, true }, m_model.textMode()));
    EXPECT_TRUE(repeated.textChanged);
    EXPECT_FALSE(repeated.submitted);
    EXPECT_EQ(m_model.text(), "a\n\nb");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "a\nb");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "ab");
    EXPECT_EQ(m_model.caret(), 1u);
    ASSERT_TRUE(m_model.redo());
    ASSERT_TRUE(m_model.redo());
    EXPECT_EQ(m_model.text(), "a\n\nb");
}

TEST_F(MultilineCommandTests, ShiftEnterReplacesTheSelectedCrossLineRange){
    ASSERT_TRUE(m_model.setText("ab\ncd"));
    ASSERT_TRUE(m_model.setSelection(4u, 1u));
    const EditCommandResult result = ApplyEditCommand(m_model,
        TranslateEditCommand({ EditKey::Enter, false, true }, m_model.textMode()));
    EXPECT_TRUE(result.textChanged);
    EXPECT_FALSE(result.submitted);
    EXPECT_EQ(m_model.text(), "a\nd");
    EXPECT_EQ(m_model.caret(), 2u);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "ab\ncd");
    EXPECT_EQ(m_model.anchor(), 4u);
    EXPECT_EQ(m_model.caret(), 1u);
}

TEST_F(MultilineCommandTests, CtrlEnterReportsSubmitWithoutEditingAndSuppressesRepeats){
    ASSERT_TRUE(m_model.setText("a\nb"));
    const MultilineSnapshot before(m_arena, m_model);
    const EditCommandResult submitted = ApplyEditCommand(m_model,
        TranslateEditCommand({ EditKey::Enter, true }, m_model.textMode()));
    EXPECT_TRUE(submitted.handled);
    EXPECT_TRUE(submitted.submitted);
    EXPECT_FALSE(submitted.textChanged);
    EXPECT_FALSE(submitted.selectionChanged);
    before.expectUnchanged(m_model);
    const EditCommandResult repeated = ApplyEditCommand(m_model,
        TranslateEditCommand({ EditKey::Enter, true, false, false, true }, m_model.textMode()));
    EXPECT_TRUE(repeated.handled);
    EXPECT_FALSE(repeated.submitted);
    before.expectUnchanged(m_model);
}

TEST_F(MultilineCommandTests, HomeEndAndCtrlVariantsSelectLineAndDocumentRanges){
    ASSERT_TRUE(m_model.setText("ab\ncd\nef"));
    ASSERT_TRUE(m_model.setSelection(4u, 4u));
    EXPECT_TRUE(ApplyEditCommand(m_model, TranslateEditCommand({ EditKey::Home, false, true }, m_model.textMode())).selectionChanged);
    EXPECT_EQ(m_model.selectedText(), "c");
    EXPECT_TRUE(ApplyEditCommand(m_model, TranslateEditCommand({ EditKey::End, false, true }, m_model.textMode())).selectionChanged);
    EXPECT_EQ(m_model.selectedText(), "d");
    EXPECT_TRUE(ApplyEditCommand(m_model, TranslateEditCommand({ EditKey::Home, true, true }, m_model.textMode())).selectionChanged);
    EXPECT_EQ(m_model.selectedText(), "ab\nc");
    EXPECT_TRUE(ApplyEditCommand(m_model, TranslateEditCommand({ EditKey::End, true, true }, m_model.textMode())).selectionChanged);
    EXPECT_EQ(m_model.selectedText(), "d\nef");
}

TEST_F(MultilineCommandTests, ReadOnlyConsumesNewlineAndMutationsWhileAllowingMovementCopyAndSubmit){
    ASSERT_TRUE(m_model.setText("ab\ncd"));
    ASSERT_TRUE(m_model.setSelection(1u, 4u));
    const MultilineSnapshot before(m_arena, m_model);
    const EditCommand::Enum mutations[]{ EditCommand::Newline, EditCommand::Backspace, EditCommand::Delete,
        EditCommand::WordBackspace, EditCommand::WordDelete, EditCommand::Cut, EditCommand::Paste, EditCommand::Undo, EditCommand::Redo };
    for(const EditCommand::Enum command : mutations){
        const EditCommandResult result = ApplyEditCommand(m_model, { command }, true);
        EXPECT_TRUE(result.handled);
        EXPECT_FALSE(result.textChanged);
        EXPECT_FALSE(result.selectionChanged);
        EXPECT_EQ(result.clipboard, EditClipboardAction::None);
        before.expectUnchanged(m_model);
    }
    EXPECT_EQ(ApplyEditCommand(m_model, { EditCommand::Copy }, true).clipboard, EditClipboardAction::Copy);
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::Home }, true).selectionChanged);
    EXPECT_EQ(m_model.caret(), 3u);
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::DocumentEnd }, true).selectionChanged);
    EXPECT_EQ(m_model.caret(), 5u);
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::Submit }, true).submitted);
}

TEST_F(MultilineCommandTests, PreeditOwnsNewlineDocumentMovementAndSubmitUntilFirstEscape){
    ASSERT_TRUE(m_model.setText("ab\ncd"));
    ASSERT_TRUE(m_model.setSelection(1u, 4u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("q\nr", 1u, 3u));
    const MultilineSnapshot before(m_arena, m_model);
    const EditCommand::Enum nativeOwned[]{ EditCommand::Newline, EditCommand::Home, EditCommand::End,
        EditCommand::DocumentHome, EditCommand::DocumentEnd, EditCommand::Submit, EditCommand::Backspace, EditCommand::Copy };
    for(const EditCommand::Enum command : nativeOwned){
        const EditCommandResult result = ApplyEditCommand(m_model, { command });
        EXPECT_TRUE(result.handled);
        EXPECT_FALSE(result.textChanged);
        EXPECT_FALSE(result.selectionChanged);
        EXPECT_FALSE(result.submitted);
        EXPECT_EQ(result.clipboard, EditClipboardAction::None);
        before.expectUnchanged(m_model);
    }
    const u64 selection = m_model.selectionGeneration();
    const EditCommandResult firstEscape = ApplyEditCommand(m_model, { EditCommand::Cancel });
    EXPECT_TRUE(firstEscape.compositionChanged);
    EXPECT_FALSE(firstEscape.cancelled);
    EXPECT_FALSE(m_model.composition().active);
    EXPECT_EQ(m_model.text(), "ab\ncd");
    EXPECT_EQ(m_model.selectionGeneration(), selection);
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::Cancel }).cancelled);
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::Newline }).textChanged);
    EXPECT_EQ(m_model.text(), "a\nd");
}

TEST_F(MultilineCommandTests, NewlineCapacityAndSingleLineRejectionPreserveAllState){
    EditModel limited(m_arena, { 3u, 8u, 128u }, EditTextMode::Multiline);
    ASSERT_TRUE(limited.setText("a\nb"));
    const MultilineSnapshot before(m_arena, limited);
    const EditCommandResult rejected = ApplyEditCommand(limited, { EditCommand::Newline });
    EXPECT_TRUE(rejected.handled);
    EXPECT_FALSE(rejected.textChanged);
    EXPECT_FALSE(rejected.selectionChanged);
    before.expectUnchanged(limited);
    EditModel single(m_arena);
    ASSERT_TRUE(single.setText("abc"));
    const MultilineSnapshot singleBefore(m_arena, single);
    EXPECT_FALSE(ApplyEditCommand(single, { EditCommand::Newline }).textChanged);
    singleBefore.expectUnchanged(single);
}

TEST_F(MultilineCommandTests, WordDeletionAcrossNewlineRestoresEachOriginalSelectionOnUndo){
    ASSERT_TRUE(m_model.setText("one\ntwo"));
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::WordBackspace }).textChanged);
    EXPECT_EQ(m_model.text(), "one\n");
    EXPECT_TRUE(ApplyEditCommand(m_model, { EditCommand::WordBackspace }).textChanged);
    EXPECT_TRUE(m_model.text().empty());
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "one\n");
    EXPECT_EQ(m_model.anchor(), 4u);
    EXPECT_EQ(m_model.caret(), 4u);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "one\ntwo");
    EXPECT_EQ(m_model.anchor(), 7u);
    EXPECT_EQ(m_model.caret(), 7u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

