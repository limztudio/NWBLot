// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "multiline_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_multiline_composition_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiMultilineTests;
using MultilineCompositionTests = MultilineFixture;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(MultilineCompositionTests, PreeditKeepsOriginalByteOffsetsAcrossLinesAndCombiningScalars){
    ASSERT_TRUE(m_model.setText("ab\ncd\nef"));
    ASSERT_TRUE(m_model.setSelection(5u, 1u));
    const u64 revision = m_model.revision();
    const u64 selection = m_model.selectionGeneration();
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("a\xCC\x81\n\xE1\x84\x80\xE1\x85\xA1", 1u, 7u));
    const EditCompositionView composition = m_model.composition();
    EXPECT_EQ(composition.text, "a\xCC\x81\n\xE1\x84\x80\xE1\x85\xA1");
    EXPECT_EQ(composition.anchor, 1u);
    EXPECT_EQ(composition.caret, 7u);
    EXPECT_EQ(composition.replacementStart, 1u);
    EXPECT_EQ(composition.replacementEnd, 5u);
    EXPECT_EQ(m_model.text(), "ab\ncd\nef");
    EXPECT_EQ(m_model.anchor(), 5u);
    EXPECT_EQ(m_model.caret(), 1u);
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_EQ(m_model.selectionGeneration(), selection);
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(MultilineCompositionTests, PreeditAndCommitRejectNoncanonicalBytesWithoutNormalizingOffsets){
    ASSERT_TRUE(m_model.setText("a\nb"));
    ASSERT_TRUE(m_model.setSelection(1u, 2u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("x\ny", 2u, 3u));
    const MultilineSnapshot before(m_arena, m_model);
    const AStringView invalid[]{ "x\r\ny", "x\ty", "\xC2\x85", "\xE2\x80\xA8", "\xE2\x80\xA9",
        "\xED\xA0\x80", AStringView("x\0y", 3u) };
    for(const AStringView text : invalid){
        EXPECT_FALSE(m_model.updateComposition(text, 0u, text.size()));
        EXPECT_FALSE(m_model.commitComposition(text));
        before.expectUnchanged(m_model);
    }
    EXPECT_FALSE(m_model.updateComposition("a\xCC\x81\nz", 2u, 5u));
    before.expectUnchanged(m_model);
}

TEST_F(MultilineCompositionTests, OverCapacityPreeditAndCommitPreserveEveryModelField){
    EditModel limited(m_arena, { 6u, 8u, 128u }, EditTextMode::Multiline);
    ASSERT_TRUE(limited.setText("a\nb"));
    ASSERT_TRUE(limited.setSelection(1u, 2u));
    ASSERT_TRUE(limited.beginComposition());
    ASSERT_TRUE(limited.updateComposition("x", 0u, 1u));
    const MultilineSnapshot before(m_arena, limited);
    EXPECT_FALSE(limited.updateComposition("12\n34", 0u, 5u));
    EXPECT_FALSE(limited.commitComposition("12\n34"));
    before.expectUnchanged(limited);
}

TEST_F(MultilineCompositionTests, CancellationPreservesCommittedSelectionEpochTextAndHistory){
    ASSERT_TRUE(m_model.setText("a\nb"));
    ASSERT_TRUE(m_model.replaceSelection("c"));
    ASSERT_TRUE(m_model.setSelection(1u, 3u));
    const u64 selection = m_model.selectionGeneration();
    const u64 revision = m_model.revision();
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("x\ny", 0u, 3u));
    const u64 composition = m_model.compositionGeneration();
    m_model.cancelComposition();
    EXPECT_GT(m_model.compositionGeneration(), composition);
    EXPECT_EQ(m_model.selectionGeneration(), selection);
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_EQ(m_model.text(), "a\nbc");
    EXPECT_EQ(m_model.anchor(), 1u);
    EXPECT_EQ(m_model.caret(), 3u);
    EXPECT_FALSE(m_model.composition().active);
    EXPECT_TRUE(m_model.canUndo());
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "a\nb");
}

TEST_F(MultilineCompositionTests, AliasedCompositionCommitOwnsBytesBeforeRetiringPreedit){
    ASSERT_TRUE(m_model.setText("a\nb"));
    ASSERT_TRUE(m_model.selectAll());
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("a\xCC\x81\nz", 1u, 5u));
    ASSERT_TRUE(m_model.commitComposition(m_model.composition().text));
    EXPECT_EQ(m_model.text(), "a\xCC\x81\nz");
    EXPECT_EQ(m_model.caret(), 5u);
    EXPECT_FALSE(m_model.setSelection(1u, 1u));
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "a\nb");
    EXPECT_EQ(m_model.anchor(), 0u);
    EXPECT_EQ(m_model.caret(), 3u);
}

TEST_F(MultilineCompositionTests, EmptyPreeditRetainsTheOriginalCrossLineReplacementRange){
    ASSERT_TRUE(m_model.setText("ab\ncd"));
    ASSERT_TRUE(m_model.setSelection(1u, 4u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("x\ny", 0u, 3u));
    ASSERT_TRUE(m_model.updateComposition({}, 0u, 0u));
    EXPECT_TRUE(m_model.composition().active);
    EXPECT_EQ(m_model.composition().replacementStart, 1u);
    EXPECT_EQ(m_model.composition().replacementEnd, 4u);
    ASSERT_TRUE(m_model.commitComposition("\n"));
    EXPECT_EQ(m_model.text(), "a\nd");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

