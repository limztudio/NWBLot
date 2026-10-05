// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_fixture.h"

#include <impl/ecs_ui/toolkit/edit/commands.h>
#include <impl/ecs_ui/toolkit/input/bindings.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_command_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiEditTests;

class UiEditCommandTests : public EditFixture{
public:
    UiEditCommandTests()
        : m_bindings(m_arena)
    {}


protected:
    [[nodiscard]] EditCommandRequest translate(const i32 key, const bool control = false, const bool shift = false,
        const bool alt = false, const bool repeat = false){
        const i32 modifiers = (control ? Core::InputModifier::Control : 0) | (shift ? Core::InputModifier::Shift : 0)
            | (alt ? Core::InputModifier::Alt : 0);
        return TranslateEditCommand(m_bindings.resolve(key, modifiers), repeat);
    }


protected:
    InputBindings m_bindings;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditCommandTests, ShortcutTranslationPreservesWordSelectionAndLeavesAltGrWithOs){
    ASSERT_TRUE(m_model.setText("one two"));
    const auto left = translate(Core::Key::Left, true, true, false, true);
    EXPECT_TRUE(left.repeat);
    EXPECT_TRUE(ApplyEditCommand(m_model, left).selectionChanged);
    EXPECT_EQ(m_model.selectedText(), "two");
    const u64 revision = m_model.revision();
    const u64 selection = m_model.selectionGeneration();
    for(const EditCommandRequest& rejected : {
        translate(Core::Key::V), translate(Core::Key::V, true, false, true),
        translate(Core::Key::Left, true, true, true), translate(static_cast<i32>(Core::Key::Menu) + 1)
    }){
        EXPECT_FALSE(ApplyEditCommand(m_model, rejected).handled);
        EXPECT_EQ(m_model.text(), "one two");
        EXPECT_EQ(m_model.selectedText(), "two");
        EXPECT_EQ(m_model.revision(), revision);
        EXPECT_EQ(m_model.selectionGeneration(), selection);
    }
}

TEST_F(UiEditCommandTests, AlternateClipboardChordsRejectAmbiguousModifiersRepeatsAndPreedit){
    ASSERT_TRUE(m_model.setText("selected"));
    ASSERT_TRUE(m_model.selectAll());
    const EditCommandRequest copy = translate(Core::Key::Insert, true);
    const EditCommandRequest paste = translate(Core::Key::Insert, false, true);
    const EditCommandRequest cut = translate(Core::Key::Delete, false, true);
    EXPECT_EQ(copy.command, EditCommand::Copy);
    EXPECT_EQ(paste.command, EditCommand::Paste);
    EXPECT_EQ(cut.command, EditCommand::Cut);
    EXPECT_EQ(translate(Core::Key::Delete, true, true).command, EditCommand::WordDelete);
    EXPECT_EQ(translate(Core::Key::Insert, true, true).command, EditCommand::None);
    EXPECT_EQ(translate(Core::Key::Insert, true, false, true).command, EditCommand::None);
    EXPECT_EQ(translate(Core::Key::Delete, false, true, true).command, EditCommand::None);
    EXPECT_EQ(ApplyEditCommand(m_model, copy, true).clipboard, EditClipboardAction::Copy);
    EXPECT_EQ(ApplyEditCommand(m_model, paste, true).clipboard, EditClipboardAction::None);
    EXPECT_EQ(ApplyEditCommand(m_model, cut, true).clipboard, EditClipboardAction::None);
    EXPECT_EQ(ApplyEditCommand(m_model, translate(Core::Key::Insert, true, false, false, true)).clipboard,
        EditClipboardAction::None);
    EXPECT_EQ(ApplyEditCommand(m_model, translate(Core::Key::Insert, false, true, false, true)).clipboard,
        EditClipboardAction::None);
    EXPECT_EQ(ApplyEditCommand(m_model, translate(Core::Key::Delete, false, true, false, true)).clipboard,
        EditClipboardAction::None);
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("draft", 0u, 5u));
    for(const EditCommandRequest request : { copy, paste, cut })
        EXPECT_EQ(ApplyEditCommand(m_model, request).clipboard, EditClipboardAction::None);
    EXPECT_EQ(m_model.text(), "selected");
}

TEST_F(UiEditCommandTests, ReboundWordMovementExtendsTheAcceptedSelectionWithoutArrowBindings){
    ASSERT_TRUE(m_model.setText("one two"));
    const InputKeyBinding rebound{ Core::Key::F2, 0, 0, InputCommand::WordLeft, InputSelectionPolicy::Always };
    ASSERT_TRUE(m_bindings.set(&rebound, 1u));
    const u64 revision = m_model.revision();
    EXPECT_FALSE(ApplyEditCommand(m_model, translate(Core::Key::Left)).handled);
    EXPECT_EQ(m_model.caret(), 7u);
    const EditCommandResult selected = ApplyEditCommand(m_model, translate(Core::Key::F2));
    EXPECT_TRUE(selected.handled);
    EXPECT_TRUE(selected.selectionChanged);
    EXPECT_FALSE(selected.textChanged);
    EXPECT_EQ(m_model.selectedText(), "two");
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_TRUE(ApplyEditCommand(m_model, TranslateEditCommand({ InputCommand::WordLeft, true }, true)).selectionChanged);
    EXPECT_EQ(m_model.selectedText(), "one two");
}

TEST_F(UiEditCommandTests, NonEditingMalformedAndUnrelatedIntentsCannotMutateOrMoveTheModel){
    ASSERT_TRUE(m_model.setText("one two"));
    ASSERT_TRUE(m_model.setSelection(1u, 5u));
    const u64 revision = m_model.revision();
    const u64 selection = m_model.selectionGeneration();
    const InputCommandIntent rejected[]{
        { InputCommand::Backspace, true, false }, { InputCommand::Copy, true, false },
        { InputCommand::Accept, true, false }, { InputCommand::WordLeft, true, false },
        { InputCommand::None }, { InputCommand::Activate }, { InputCommand::ContextMenu },
        { static_cast<InputCommand::Enum>(255u), true }
    };
    for(const InputCommandIntent& intent : rejected){
        const EditCommandResult result = ApplyEditCommand(m_model, TranslateEditCommand(intent, false));
        EXPECT_FALSE(result.handled);
        EXPECT_EQ(result.clipboard, EditClipboardAction::None);
        EXPECT_FALSE(result.submitted);
        EXPECT_FALSE(result.cancelled);
        EXPECT_EQ(m_model.text(), "one two");
        EXPECT_EQ(m_model.anchor(), 1u);
        EXPECT_EQ(m_model.caret(), 5u);
        EXPECT_EQ(m_model.revision(), revision);
        EXPECT_EQ(m_model.selectionGeneration(), selection);
    }
    EXPECT_FALSE(ApplyEditCommand(m_model,
        TranslateEditCommand({ InputCommand::Backspace }, false, static_cast<EditTextMode::Enum>(255u))).handled);
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_EQ(m_model.selectionGeneration(), selection);
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

TEST_F(UiEditCommandTests, EmptyAndUnknownCommandsPreserveTextAndSelection){
    ASSERT_TRUE(m_model.setText("abcd"));
    ASSERT_TRUE(m_model.setSelection(1u, 3u));
    EXPECT_FALSE(ApplyEditCommand(m_model, {}).handled);
    EXPECT_FALSE(ApplyEditCommand(m_model, { static_cast<EditCommand::Enum>(255u) }).handled);
    EXPECT_EQ(m_model.text(), "abcd");
    EXPECT_EQ(m_model.selectedText(), "bc");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

