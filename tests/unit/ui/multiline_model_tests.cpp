// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "multiline_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_multiline_model_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiMultilineTests;
using MultilineModelTests = MultilineFixture;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(MultilineModelTests, DefaultSingleLineRetainsItsOriginalAdmissionAndMovement){
    EditModel single(m_arena);
    EXPECT_EQ(single.textMode(), EditTextMode::SingleLine);
    ASSERT_TRUE(single.setText("a\t\x01\x7F\xC2\x85" "b"));
    ASSERT_TRUE(single.move(EditMove::Home));
    EXPECT_EQ(single.caret(), 0u);
    ASSERT_TRUE(single.move(EditMove::End));
    EXPECT_EQ(single.caret(), single.text().size());
    const MultilineSnapshot before(m_arena, single);
    for(const AStringView rejected : { AStringView("\n"), AStringView("\r"), AStringView("\xE2\x80\xA8"),
        AStringView("\xE2\x80\xA9"), AStringView("\0", 1u) }){
        EXPECT_FALSE(single.setText(rejected));
        before.expectUnchanged(single);
    }
    EXPECT_EQ(m_model.textMode(), EditTextMode::Multiline);
}

TEST_F(MultilineModelTests, CanonicalLinesPreserveUnicodeGraphemeBoundariesAndTrailingEmptyLine){
    ASSERT_TRUE(m_model.setText("a\xCC\x81\n\xE1\x84\x80\xE1\x85\xA1\n\xF0\x9F\x87\xB0\xF0\x9F\x87\xB7\n"));
    const usize expected[]{ 0u, 3u, 4u, 10u, 11u, 19u, 20u };
    ASSERT_EQ(m_model.graphemeBoundaries().size(), 7u);
    for(usize index = 0u; index < 7u; ++index)
        EXPECT_EQ(m_model.graphemeBoundaries()[index], expected[index]);
    EXPECT_EQ(m_model.caret(), 20u);
    EXPECT_FALSE(m_model.setSelection(1u, 1u));
    EXPECT_FALSE(m_model.setSelection(7u, 7u));
    ASSERT_TRUE(m_model.move(EditMove::Home));
    EXPECT_EQ(m_model.caret(), 20u);
    ASSERT_TRUE(m_model.move(EditMove::End));
    EXPECT_EQ(m_model.caret(), 20u);
}

TEST_F(MultilineModelTests, InvalidSetAndReplacementPreserveTextHistorySelectionAndComposition){
    ASSERT_TRUE(m_model.setText("a\nb"));
    ASSERT_TRUE(m_model.replaceSelection("x"));
    ASSERT_TRUE(m_model.replaceSelection("y"));
    ASSERT_TRUE(m_model.undo());
    ASSERT_TRUE(m_model.setSelection(1u, 3u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("q\nr", 0u, 3u));
    const MultilineSnapshot before(m_arena, m_model);
    const AStringView invalid[]{ "\r\n", "\t", "\xC2\x85", "\xE2\x80\xA8", "\xE2\x80\xA9",
        "\xC0\xAF", "\xED\xA0\x80", AStringView("a\0b", 3u) };
    for(const AStringView value : invalid){
        SCOPED_TRACE(value);
        EXPECT_FALSE(m_model.setText(value));
        EXPECT_FALSE(m_model.replaceSelection(value));
        before.expectUnchanged(m_model);
    }
    for(u32 scalar = 0u; scalar < 32u; ++scalar){
        if(scalar == '\n')
            continue;
        const char bytes[]{ static_cast<char>(scalar) };
        EXPECT_FALSE(m_model.setText({ bytes, 1u }));
        EXPECT_FALSE(m_model.replaceSelection({ bytes, 1u }));
        before.expectUnchanged(m_model);
    }
    for(u32 scalar = 0x80u; scalar <= 0x9Fu; ++scalar){
        const char bytes[]{ static_cast<char>(0xC2u), static_cast<char>(scalar) };
        EXPECT_FALSE(m_model.setText({ bytes, 2u }));
        EXPECT_FALSE(m_model.replaceSelection({ bytes, 2u }));
        before.expectUnchanged(m_model);
    }
    m_model.cancelComposition();
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "a\nb");
    ASSERT_TRUE(m_model.redo());
    EXPECT_EQ(m_model.text(), "a\nbx");
    ASSERT_TRUE(m_model.redo());
    EXPECT_EQ(m_model.text(), "a\nbxy");
}

TEST_F(MultilineModelTests, CapacityFailureKeepsTheExactBorrowedModelState){
    EditModel limited(m_arena, { 6u, 8u, 128u }, EditTextMode::Multiline);
    ASSERT_TRUE(limited.setText("a\nb"));
    ASSERT_TRUE(limited.replaceSelection("d"));
    ASSERT_TRUE(limited.setSelection(0u, 1u));
    ASSERT_TRUE(limited.beginComposition());
    ASSERT_TRUE(limited.updateComposition("x", 0u, 1u));
    const MultilineSnapshot before(m_arena, limited);
    EXPECT_FALSE(limited.setText("abcdef\n"));
    EXPECT_FALSE(limited.replaceSelection("x\nyz"));
    before.expectUnchanged(limited);
}

TEST_F(MultilineModelTests, AliasedInsertionAndExternalSameTextReplacementOwnTheirBytes){
    ASSERT_TRUE(m_model.setText("ab\ncd"));
    ASSERT_TRUE(m_model.setSelection(1u, 4u));
    ASSERT_TRUE(m_model.replaceSelection(m_model.text()));
    EXPECT_EQ(m_model.text(), "aab\ncdd");
    EXPECT_EQ(m_model.caret(), 6u);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "ab\ncd");
    EXPECT_EQ(m_model.anchor(), 1u);
    EXPECT_EQ(m_model.caret(), 4u);
    ASSERT_TRUE(m_model.redo());
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("x", 0u, 1u));
    const u64 external = m_model.externalRevision();
    const u64 selection = m_model.selectionGeneration();
    const u64 revision = m_model.revision();
    ASSERT_TRUE(m_model.setText(m_model.text()));
    EXPECT_EQ(m_model.text(), "aab\ncdd");
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_GT(m_model.externalRevision(), external);
    EXPECT_GT(m_model.selectionGeneration(), selection);
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_FALSE(m_model.composition().active);
}

TEST_F(MultilineModelTests, CrossLineSelectionAndHistoryRetainWholeCombiningGraphemes){
    ASSERT_TRUE(m_model.setText("a\xCC\x81\nz"));
    ASSERT_TRUE(m_model.setSelection(4u, 0u));
    EXPECT_EQ(m_model.selectedText(), "a\xCC\x81\n");
    ASSERT_TRUE(m_model.replaceSelection("q\n"));
    EXPECT_EQ(m_model.text(), "q\nz");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "a\xCC\x81\nz");
    EXPECT_EQ(m_model.anchor(), 4u);
    EXPECT_EQ(m_model.caret(), 0u);
    ASSERT_TRUE(m_model.redo());
    EXPECT_EQ(m_model.text(), "q\nz");
    EXPECT_EQ(m_model.anchor(), 2u);
    EXPECT_EQ(m_model.caret(), 2u);
}

TEST_F(MultilineModelTests, DeletingLineBreaksJoinsLinesWithoutSplittingGraphemes){
    ASSERT_TRUE(m_model.setText("a\xCC\x81\nz"));
    ASSERT_TRUE(m_model.setSelection(4u, 4u));
    ASSERT_TRUE(m_model.backspace());
    EXPECT_EQ(m_model.text(), "a\xCC\x81z");
    EXPECT_EQ(m_model.caret(), 3u);
    ASSERT_TRUE(m_model.undo());
    ASSERT_TRUE(m_model.setSelection(3u, 3u));
    ASSERT_TRUE(m_model.eraseForward());
    EXPECT_EQ(m_model.text(), "a\xCC\x81z");
    ASSERT_TRUE(m_model.undo());
    ASSERT_TRUE(m_model.setSelection(3u, 3u));
    ASSERT_TRUE(m_model.backspace());
    EXPECT_EQ(m_model.text(), "\nz");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "a\xCC\x81\nz");
    EXPECT_EQ(m_model.caret(), 3u);
}

TEST_F(MultilineModelTests, SurroundingDeletionAcrossLinesRemainsBoundaryCheckedAndUndoable){
    ASSERT_TRUE(m_model.setText("a\xCC\x81\nz"));
    ASSERT_TRUE(m_model.setSelection(4u, 4u));
    const MultilineSnapshot before(m_arena, m_model);
    EXPECT_FALSE(m_model.eraseSurrounding(2u, 0u));
    before.expectUnchanged(m_model);
    ASSERT_TRUE(m_model.eraseSurrounding(1u, 1u));
    EXPECT_EQ(m_model.text(), "a\xCC\x81");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "a\xCC\x81\nz");
    EXPECT_EQ(m_model.anchor(), 4u);
    EXPECT_EQ(m_model.caret(), 4u);
}

TEST_F(MultilineModelTests, HomeEndAddressCurrentHardLineIncludingEmptyAndTrailingLines){
    ASSERT_TRUE(m_model.setText("ab\n\ncde\n"));
    const usize homes[]{ 0u, 0u, 0u, 3u, 4u, 4u, 4u, 4u, 8u };
    const usize ends[]{ 2u, 2u, 2u, 3u, 7u, 7u, 7u, 7u, 8u };
    for(usize caret = 0u; caret <= 8u; ++caret){
        ASSERT_TRUE(m_model.setSelection(caret, caret));
        ASSERT_TRUE(m_model.move(EditMove::Home));
        EXPECT_EQ(m_model.caret(), homes[caret]);
        ASSERT_TRUE(m_model.setSelection(caret, caret));
        ASSERT_TRUE(m_model.move(EditMove::End));
        EXPECT_EQ(m_model.caret(), ends[caret]);
    }
    ASSERT_TRUE(m_model.setText({}));
    ASSERT_TRUE(m_model.move(EditMove::Home));
    ASSERT_TRUE(m_model.move(EditMove::End));
    EXPECT_EQ(m_model.caret(), 0u);
}

TEST_F(MultilineModelTests, ExtendedLineAndDocumentMovementPreserveTheSelectionAnchor){
    ASSERT_TRUE(m_model.setText("ab\n\ncde\n"));
    ASSERT_TRUE(m_model.setSelection(5u, 5u));
    ASSERT_TRUE(m_model.move(EditMove::Home, true));
    EXPECT_EQ(m_model.anchor(), 5u);
    EXPECT_EQ(m_model.selectedText(), "c");
    ASSERT_TRUE(m_model.move(EditMove::End, true));
    EXPECT_EQ(m_model.anchor(), 5u);
    EXPECT_EQ(m_model.selectedText(), "de");
    ASSERT_TRUE(m_model.move(EditMove::DocumentHome, true));
    EXPECT_EQ(m_model.anchor(), 5u);
    EXPECT_EQ(m_model.selectedText(), "ab\n\nc");
    ASSERT_TRUE(m_model.move(EditMove::DocumentEnd, true));
    EXPECT_EQ(m_model.anchor(), 5u);
    EXPECT_EQ(m_model.selectedText(), "de\n");
}

TEST_F(MultilineModelTests, WordRunsMayCrossHardLineWhitespace){
    ASSERT_TRUE(m_model.setText("one\ntwo"));
    ASSERT_TRUE(m_model.move(EditMove::WordLeft));
    EXPECT_EQ(m_model.caret(), 4u);
    ASSERT_TRUE(m_model.move(EditMove::WordLeft));
    EXPECT_EQ(m_model.caret(), 0u);
    ASSERT_TRUE(m_model.move(EditMove::WordRight));
    EXPECT_EQ(m_model.caret(), 4u);
    ASSERT_TRUE(m_model.move(EditMove::WordRight));
    EXPECT_EQ(m_model.caret(), 7u);
}

TEST_F(MultilineModelTests, AcceptedNoopSelectionAndMovementAdvanceTheSelectionEpochInBothModes){
    for(const EditTextMode::Enum mode : { EditTextMode::SingleLine, EditTextMode::Multiline }){
        EditModel model(m_arena, {}, mode);
        ASSERT_TRUE(model.setText("a\xCC\x81z"));
        const u64 revision = model.revision();
        const u64 external = model.externalRevision();
        u64 selection = model.selectionGeneration();
        ASSERT_TRUE(model.setSelection(model.anchor(), model.caret()));
        EXPECT_GT(model.selectionGeneration(), selection);
        selection = model.selectionGeneration();
        ASSERT_TRUE(model.move(EditMove::Right));
        EXPECT_GT(model.selectionGeneration(), selection);
        selection = model.selectionGeneration();
        ASSERT_TRUE(model.selectAll());
        EXPECT_GT(model.selectionGeneration(), selection);
        selection = model.selectionGeneration();
        ASSERT_TRUE(model.selectAll());
        EXPECT_GT(model.selectionGeneration(), selection);
        EXPECT_EQ(model.revision(), revision);
        EXPECT_EQ(model.externalRevision(), external);
        const MultilineSnapshot before(m_arena, model);
        EXPECT_FALSE(model.setSelection(1u, 1u));
        EXPECT_FALSE(model.move(static_cast<EditMove::Enum>(255u)));
        before.expectUnchanged(model);
    }
}

TEST_F(MultilineModelTests, SelectionAwayAndBackCannotReviveAnOldLoanEpoch){
    for(const EditTextMode::Enum mode : { EditTextMode::SingleLine, EditTextMode::Multiline }){
        EditModel model(m_arena, {}, mode);
        ASSERT_TRUE(model.setText("ab"));
        const u64 revision = model.revision();
        const u64 external = model.externalRevision();
        const u64 selection = model.selectionGeneration();
        ASSERT_TRUE(model.setSelection(0u, 0u));
        ASSERT_TRUE(model.setSelection(2u, 2u));
        EXPECT_EQ(model.anchor(), 2u);
        EXPECT_EQ(model.caret(), 2u);
        EXPECT_GT(model.selectionGeneration(), selection);
        EXPECT_EQ(model.revision(), revision);
        EXPECT_EQ(model.externalRevision(), external);
    }
}

TEST_F(MultilineModelTests, ReplacementUndoRedoAndSameTextAssignmentAdvanceSelectionEpoch){
    ASSERT_TRUE(m_model.setText("a\nb"));
    u64 selection = m_model.selectionGeneration();
    ASSERT_TRUE(m_model.replaceSelection("c"));
    EXPECT_GT(m_model.selectionGeneration(), selection);
    selection = m_model.selectionGeneration();
    ASSERT_TRUE(m_model.undo());
    EXPECT_GT(m_model.selectionGeneration(), selection);
    selection = m_model.selectionGeneration();
    ASSERT_TRUE(m_model.redo());
    EXPECT_GT(m_model.selectionGeneration(), selection);
    selection = m_model.selectionGeneration();
    ASSERT_TRUE(m_model.setText(m_model.text()));
    EXPECT_GT(m_model.selectionGeneration(), selection);
    selection = m_model.selectionGeneration();
    ASSERT_TRUE(m_model.replaceSelection({}));
    EXPECT_GT(m_model.selectionGeneration(), selection);
    selection = m_model.selectionGeneration();
    ASSERT_TRUE(m_model.eraseSurrounding(0u, 0u));
    EXPECT_EQ(m_model.selectionGeneration(), selection);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

