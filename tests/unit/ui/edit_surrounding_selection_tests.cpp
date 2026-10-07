// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_fixture.h"
#include "multiline_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_surrounding_selection_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiEditTests;
using namespace NWB::UiMultilineTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool SeedHistoryAndPreedit(
    EditModel& model,
    const AStringView text,
    const usize anchor,
    const usize caret
){
    if(text.empty())
        return false;
    return
        model.setText(text.substr(0u, text.size() - 1u))
        && model.replaceSelection(text.substr(text.size() - 1u))
        && model.replaceSelection("!") && model.undo()
        && model.setSelection(anchor, caret) && model.beginComposition()
        && model.updateComposition("e\xCC\x81", 1u, 3u)
    ;
}

static void ExpectRetainedHistory(EditModel& model, const AStringView text){
    EXPECT_TRUE(model.canUndo());
    EXPECT_TRUE(model.canRedo());
    model.cancelComposition();
    ASSERT_TRUE(model.redo());
    ASSERT_EQ(model.text().size(), text.size() + 1u);
    EXPECT_EQ(model.text().substr(0u, text.size()), text);
    EXPECT_EQ(model.text().back(), '!');
    EXPECT_FALSE(model.canRedo());
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), text);
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), text.substr(0u, text.size() - 1u));
    EXPECT_FALSE(model.canUndo());
    EXPECT_TRUE(model.canRedo());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiEditSurroundingSelectionTests : public EditFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditSurroundingSelectionTests, OneSidedCountsAddressNormalizedSelectionEdgesForEitherDirection){
    for(const bool reverse : { false, true }){
        EditModel before(m_arena);
        ASSERT_TRUE(before.setText("abcDEFghi"));
        ASSERT_TRUE(before.setSelection(reverse ? 6u : 3u, reverse ? 3u : 6u));
        ASSERT_TRUE(before.eraseAroundSelection(2u, 0u));
        EXPECT_EQ(before.text(), "aDEFghi");
        EXPECT_EQ(before.selectedText(), "DEF");
        EXPECT_EQ(before.anchor(), reverse ? 4u : 1u);
        EXPECT_EQ(before.caret(), reverse ? 1u : 4u);

        EditModel after(m_arena);
        ASSERT_TRUE(after.setText("abcDEFghi"));
        ASSERT_TRUE(after.setSelection(reverse ? 6u : 3u, reverse ? 3u : 6u));
        ASSERT_TRUE(after.eraseAroundSelection(0u, 2u));
        EXPECT_EQ(after.text(), "abcDEFi");
        EXPECT_EQ(after.selectedText(), "DEF");
        EXPECT_EQ(after.anchor(), reverse ? 6u : 3u);
        EXPECT_EQ(after.caret(), reverse ? 3u : 6u);
    }
}

TEST_F(UiEditSurroundingSelectionTests, CollapsedSelectionDeletesAroundTheCaretAndStaysCollapsed){
    ASSERT_TRUE(m_model.setText("abcdef"));
    ASSERT_TRUE(m_model.setSelection(3u, 3u));
    ASSERT_TRUE(m_model.eraseAroundSelection(2u, 1u));
    EXPECT_EQ(m_model.text(), "aef");
    EXPECT_EQ(m_model.anchor(), 1u);
    EXPECT_EQ(m_model.caret(), 1u);
    EXPECT_FALSE(m_model.hasSelection());
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abcdef");
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 3u);
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditSurroundingSelectionTests, RemovingAllOutsideBytesLeavesTheCompleteSelection){
    ASSERT_TRUE(m_model.setText("abcDEFghi"));
    ASSERT_TRUE(m_model.setSelection(3u, 6u));
    ASSERT_TRUE(m_model.eraseAroundSelection(3u, 3u));
    EXPECT_EQ(m_model.text(), "DEF");
    EXPECT_EQ(m_model.anchor(), 0u);
    EXPECT_EQ(m_model.caret(), 3u);
    EXPECT_EQ(m_model.selectedText(), "DEF");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abcDEFghi");
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 6u);
    ASSERT_TRUE(m_model.redo());
    EXPECT_EQ(m_model.text(), "DEF");
}

TEST_F(UiEditSurroundingSelectionTests, SelectedCombiningAndZwjEmojiGraphemesRemainByteExact){
    constexpr AStringView s_Selected = "e\xCC\x81\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB";
    ASSERT_EQ(s_Selected.size(), 14u);
    ASSERT_TRUE(m_model.setText("abe\xCC\x81\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB" "yz"));
    ASSERT_TRUE(m_model.setSelection(2u, 16u));
    ASSERT_TRUE(m_model.eraseAroundSelection(1u, 1u));
    EXPECT_EQ(m_model.text(), "ae\xCC\x81\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB" "z");
    EXPECT_EQ(m_model.selectedText(), s_Selected);
    EXPECT_EQ(m_model.anchor(), 1u);
    EXPECT_EQ(m_model.caret(), 15u);
    EXPECT_EQ(m_model.graphemeBoundaries().size(), 5u);
}

TEST_F(UiEditSurroundingSelectionTests, MultilineSelectionKeepsItsLfBytesAndReverseDirection){
    EditModel model(m_arena, {}, EditTextMode::Multiline);
    ASSERT_TRUE(model.setText("ab\nC\nD\nef"));
    ASSERT_TRUE(model.setSelection(7u, 2u));
    ASSERT_TRUE(model.eraseAroundSelection(1u, 1u));
    EXPECT_EQ(model.text(), "a\nC\nD\nf");
    EXPECT_EQ(model.selectedText(), "\nC\nD\n");
    EXPECT_EQ(model.anchor(), 6u);
    EXPECT_EQ(model.caret(), 1u);
    ASSERT_TRUE(model.undo());
    EXPECT_EQ(model.text(), "ab\nC\nD\nef");
    EXPECT_EQ(model.anchor(), 7u);
    EXPECT_EQ(model.caret(), 2u);
    EXPECT_FALSE(model.canUndo());
}

TEST_F(UiEditSurroundingSelectionTests, AcceptedDeletionCancelsPreeditWithoutAddingTransientHistory){
    ASSERT_TRUE(m_model.setText("abcDEFghi"));
    ASSERT_TRUE(m_model.setSelection(3u, 6u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("transient", 0u, 9u));
    const u64 revision = m_model.revision();
    const u64 selection = m_model.selectionGeneration();
    const u64 composition = m_model.compositionGeneration();
    const u64 external = m_model.externalRevision();
    ASSERT_TRUE(m_model.eraseAroundSelection(2u, 1u));
    EXPECT_EQ(m_model.text(), "aDEFhi");
    EXPECT_EQ(m_model.anchor(), 1u);
    EXPECT_EQ(m_model.caret(), 4u);
    EXPECT_FALSE(m_model.composition().active);
    EXPECT_TRUE(m_model.composition().text.empty());
    EXPECT_EQ(m_model.revision(), revision + 1u);
    EXPECT_EQ(m_model.selectionGeneration(), selection + 1u);
    EXPECT_EQ(m_model.compositionGeneration(), composition + 1u);
    EXPECT_EQ(m_model.externalRevision(), external);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abcDEFghi");
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 6u);
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_FALSE(m_model.composition().active);
}

TEST_F(UiEditSurroundingSelectionTests, ZeroCountsPreserveSelectionPreeditHistoryAndAllEpochs){
    ASSERT_TRUE(SeedHistoryAndPreedit(m_model, "abcDEFghi", 6u, 3u));
    const MultilineSnapshot before(m_arena, m_model);
    ASSERT_TRUE(m_model.eraseAroundSelection(0u, 0u));
    before.expectUnchanged(m_model);
    ExpectRetainedHistory(m_model, "abcDEFghi");
}

TEST_F(UiEditSurroundingSelectionTests, OutOfRangeCountsPreserveTheCompleteDraftAndRedoBranch){
    ASSERT_TRUE(SeedHistoryAndPreedit(m_model, "abcDEFghi", 3u, 6u));
    const MultilineSnapshot before(m_arena, m_model);
    EXPECT_FALSE(m_model.eraseAroundSelection(4u, 0u));
    before.expectUnchanged(m_model);
    EXPECT_FALSE(m_model.eraseAroundSelection(0u, 4u));
    before.expectUnchanged(m_model);
    EXPECT_FALSE(m_model.eraseAroundSelection(Limit<usize>::s_Max, 0u));
    before.expectUnchanged(m_model);
    EXPECT_FALSE(m_model.eraseAroundSelection(0u, Limit<usize>::s_Max));
    before.expectUnchanged(m_model);
    ExpectRetainedHistory(m_model, "abcDEFghi");
}

TEST_F(UiEditSurroundingSelectionTests, OriginalCutsRejectBothUtf8SplitsAndScalarValidGraphemeSplits){
    constexpr AStringView s_Text = "ae\xCC\x81" "DEF\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB" "z";
    ASSERT_TRUE(SeedHistoryAndPreedit(m_model, s_Text, 4u, 7u));
    const MultilineSnapshot before(m_arena, m_model);
    EXPECT_FALSE(GraphemeSegmentation::IsScalarBoundary(s_Text, 3u));
    EXPECT_TRUE(GraphemeSegmentation::IsScalarBoundary(s_Text, 2u));
    EXPECT_FALSE(GraphemeSegmentation::IsScalarBoundary(s_Text, 8u));
    EXPECT_TRUE(GraphemeSegmentation::IsScalarBoundary(s_Text, 11u));
    EXPECT_FALSE(m_model.eraseAroundSelection(1u, 0u));
    before.expectUnchanged(m_model);
    EXPECT_FALSE(m_model.eraseAroundSelection(2u, 0u));
    before.expectUnchanged(m_model);
    EXPECT_FALSE(m_model.eraseAroundSelection(0u, 1u));
    before.expectUnchanged(m_model);
    EXPECT_FALSE(m_model.eraseAroundSelection(0u, 4u));
    before.expectUnchanged(m_model);
    ExpectRetainedHistory(m_model, s_Text);
}

TEST_F(UiEditSurroundingSelectionTests, CandidateSeamMergesRejectEitherEndpointWithoutSnappingOrCancelling){
    struct Case{
        AStringView text;
        usize anchor;
        usize caret;
        usize before;
        usize after;
        AStringView merged;
        usize lostBoundary;
    };
    constexpr Case s_Cases[] = {
        { "\xF0\x9F\x87\xA6" "x\xF0\x9F\x87\xA7z", 5u, 9u, 1u, 0u,
            "\xF0\x9F\x87\xA6\xF0\x9F\x87\xA7z", 4u },
        { "z\xF0\x9F\x87\xA6" "x\xF0\x9F\x87\xA7q", 1u, 5u, 0u, 1u,
            "z\xF0\x9F\x87\xA6\xF0\x9F\x87\xA7q", 5u },
    };
    for(const Case& value : s_Cases){
        EditModel model(m_arena);
        ASSERT_TRUE(SeedHistoryAndPreedit(model, value.text, value.anchor, value.caret));
        const MultilineSnapshot before(m_arena, model);
        EditModel merged(m_arena);
        ASSERT_TRUE(merged.setText(value.merged));
        EXPECT_TRUE(GraphemeSegmentation::IsScalarBoundary(value.merged, value.lostBoundary));
        EXPECT_FALSE(merged.setSelection(value.lostBoundary, value.lostBoundary));
        EXPECT_FALSE(model.eraseAroundSelection(value.before, value.after));
        before.expectUnchanged(model);
        ExpectRetainedHistory(model, value.text);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

