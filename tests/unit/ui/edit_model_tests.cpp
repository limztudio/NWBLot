// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_model_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiEditTests;

class UiEditModelTests : public EditFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditModelTests, SelectionRejectsInteriorScalarAndGraphemeOffsets){
    ASSERT_TRUE(m_model.setText("a\xCC\x81z"));
    ASSERT_TRUE(m_model.setSelection(0u, 3u));
    EXPECT_EQ(m_model.selectedText(), "a\xCC\x81");
    EXPECT_FALSE(m_model.setSelection(1u, 3u));
    EXPECT_FALSE(m_model.setSelection(2u, 3u));
    EXPECT_FALSE(m_model.setSelection(0u, 5u));
    EXPECT_EQ(m_model.anchor(), 0u);
    EXPECT_EQ(m_model.caret(), 3u);
}

TEST_F(UiEditModelTests, MovementAndDeletionPreserveWholeCombiningGraphemes){
    ASSERT_TRUE(m_model.setText("a\xCC\x81z"));
    ASSERT_TRUE(m_model.move(EditMove::Home));
    ASSERT_TRUE(m_model.move(EditMove::Right));
    EXPECT_EQ(m_model.caret(), 3u);
    ASSERT_TRUE(m_model.backspace());
    EXPECT_EQ(m_model.text(), "z");
    EXPECT_EQ(m_model.caret(), 0u);
    ASSERT_TRUE(m_model.eraseForward());
    EXPECT_TRUE(m_model.text().empty());
    const u64 revision = m_model.revision();
    ASSERT_TRUE(m_model.backspace());
    ASSERT_TRUE(m_model.eraseForward());
    EXPECT_EQ(m_model.revision(), revision);
}

TEST_F(UiEditModelTests, DevanagariConjunctAndKoreanJamoAreSingleEditableGraphemes){
    const AStringView conjunct("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7");
    ASSERT_TRUE(m_model.setText(conjunct));
    ASSERT_EQ(m_model.graphemeBoundaries().size(), 2u);
    ASSERT_TRUE(m_model.backspace());
    EXPECT_TRUE(m_model.text().empty());
    const AStringView jamo("\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8");
    ASSERT_TRUE(m_model.setText(jamo));
    ASSERT_TRUE(m_model.move(EditMove::Home));
    ASSERT_TRUE(m_model.eraseForward());
    EXPECT_TRUE(m_model.text().empty());
}

TEST_F(UiEditModelTests, ReplacementCopiesAliasedCommittedText){
    ASSERT_TRUE(m_model.setText("abc"));
    ASSERT_TRUE(m_model.setSelection(1u, 2u));
    ASSERT_TRUE(m_model.replaceSelection(m_model.text()));
    EXPECT_EQ(m_model.text(), "aabcc");
    EXPECT_EQ(m_model.caret(), 4u);
}

TEST_F(UiEditModelTests, InsertionMayMergeClustersAndCaretUsesFollowingValidBoundary){
    ASSERT_TRUE(m_model.setText("a"));
    ASSERT_TRUE(m_model.replaceSelection("\xCC\x81"));
    EXPECT_EQ(m_model.text(), "a\xCC\x81");
    EXPECT_EQ(m_model.caret(), 3u);
    EXPECT_EQ(m_model.graphemeBoundaries().size(), 2u);
    ASSERT_TRUE(m_model.setText("\xCC\x81"));
    ASSERT_TRUE(m_model.move(EditMove::Home));
    ASSERT_TRUE(m_model.replaceSelection("a"));
    EXPECT_EQ(m_model.caret(), 3u);
    ASSERT_TRUE(m_model.backspace());
    EXPECT_TRUE(m_model.text().empty());
}

TEST_F(UiEditModelTests, InvalidUtf8AndMultilineValuesPreserveAllCommittedState){
    ASSERT_TRUE(m_model.setText("ab"));
    ASSERT_TRUE(m_model.replaceSelection("c"));
    ASSERT_TRUE(m_model.setSelection(1u, 2u));
    const u64 revision = m_model.revision();
    const AStringView invalid[] = { "\xED\xA0\x80", "\xC0\xAF", "\xF4\x90\x80\x80", "\xE2\x82",
        "x\ny", "x\ry", "\xE2\x80\xA8", "\xE2\x80\xA9", AStringView("a\0b", 3u) };
    for(const AStringView value : invalid){
        EXPECT_FALSE(m_model.setText(value));
        EXPECT_FALSE(m_model.replaceSelection(value));
        EXPECT_EQ(m_model.text(), "abc");
        EXPECT_EQ(m_model.anchor(), 1u);
        EXPECT_EQ(m_model.caret(), 2u);
        EXPECT_EQ(m_model.revision(), revision);
        EXPECT_TRUE(m_model.canUndo());
    }
}

TEST_F(UiEditModelTests, ByteLimitFailureIsAtomicAndEmptyLimitAcceptsOnlyEmptyText){
    EditModel limited(m_arena, { 3u, 2u, 12u });
    ASSERT_TRUE(limited.setText("ab"));
    const u64 revision = limited.revision();
    EXPECT_FALSE(limited.replaceSelection("cd"));
    EXPECT_FALSE(limited.setText("abcd"));
    EXPECT_EQ(limited.text(), "ab");
    EXPECT_EQ(limited.caret(), 2u);
    EXPECT_EQ(limited.revision(), revision);
    EXPECT_FALSE(limited.canUndo());
    EditModel empty(m_arena, { 0u, 0u, 0u });
    EXPECT_TRUE(empty.setText({}));
    EXPECT_TRUE(empty.replaceSelection({}));
    EXPECT_FALSE(empty.replaceSelection("a"));
}

TEST_F(UiEditModelTests, WordRangeRejectsInteriorGraphemeAndKeepsHardLinesSeparate){
    const auto empty = m_model.wordRangeAt(0u);
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty->begin, 0u);
    EXPECT_EQ(empty->end, 0u);
    ASSERT_TRUE(m_model.setText("a\xCC\x81" "b,  z"));
    EXPECT_FALSE(m_model.wordRangeAt(1u));
    Expected<EditWordRange> range = MakeUnexpected(Failure{});
    range = m_model.wordRangeAt(3u);
    ASSERT_TRUE(range);
    EXPECT_EQ(range->begin, 0u);
    EXPECT_EQ(range->end, 4u);
    EditModel multiline(m_arena, {}, EditTextMode::Multiline);
    ASSERT_TRUE(multiline.setText("ab\ncd"));
    range = multiline.wordRangeAt(2u);
    ASSERT_TRUE(range);
    EXPECT_EQ(range->begin, 2u);
    EXPECT_EQ(range->end, 3u);
    range = multiline.wordRangeAt(3u);
    ASSERT_TRUE(range);
    EXPECT_EQ(range->begin, 3u);
    EXPECT_EQ(range->end, 5u);
}

TEST_F(UiEditModelTests, ExternalValueResetsSelectionHistoryAndComposition){
    ASSERT_TRUE(m_model.setText("abc"));
    ASSERT_TRUE(m_model.replaceSelection("d"));
    ASSERT_TRUE(m_model.selectAll());
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("x", 0u, 1u));
    ASSERT_TRUE(m_model.setText("reset"));
    EXPECT_EQ(m_model.text(), "reset");
    EXPECT_EQ(m_model.caret(), 5u);
    EXPECT_FALSE(m_model.hasSelection());
    EXPECT_FALSE(m_model.composition().active);
    EXPECT_FALSE(m_model.canUndo());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

