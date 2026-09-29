// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_history_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiEditTests;

class UiEditHistoryTests : public EditFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditHistoryTests, UndoRedoRestoreCopiedTextAndSelectionAndAdvanceRevision){
    ASSERT_TRUE(m_model.setText("abcd"));
    ASSERT_TRUE(m_model.setSelection(3u, 1u));
    ASSERT_TRUE(m_model.replaceSelection("x"));
    EXPECT_EQ(m_model.text(), "axd");
    const u64 revision = m_model.revision();
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abcd");
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 1u);
    EXPECT_GT(m_model.revision(), revision);
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_TRUE(m_model.canRedo());
    ASSERT_TRUE(m_model.redo());
    EXPECT_EQ(m_model.text(), "axd");
    EXPECT_EQ(m_model.anchor(), 2u);
    EXPECT_EQ(m_model.caret(), 2u);
    EXPECT_FALSE(m_model.canRedo());
}

TEST_F(UiEditHistoryTests, BranchingAfterUndoDiscardsOnlyFutureHistory){
    ASSERT_TRUE(m_model.replaceSelection("a"));
    ASSERT_TRUE(m_model.replaceSelection("b"));
    ASSERT_TRUE(m_model.undo());
    ASSERT_TRUE(m_model.replaceSelection("c"));
    EXPECT_EQ(m_model.text(), "ac");
    EXPECT_FALSE(m_model.canRedo());
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "a");
    ASSERT_TRUE(m_model.undo());
    EXPECT_TRUE(m_model.text().empty());
}

TEST_F(UiEditHistoryTests, SelectionNavigationAndValidNoopPreserveRedo){
    ASSERT_TRUE(m_model.replaceSelection("a"));
    ASSERT_TRUE(m_model.replaceSelection("b"));
    ASSERT_TRUE(m_model.undo());
    ASSERT_TRUE(m_model.move(EditMove::Home));
    ASSERT_TRUE(m_model.replaceSelection({}));
    EXPECT_TRUE(m_model.canRedo());
    ASSERT_TRUE(m_model.redo());
    EXPECT_EQ(m_model.text(), "ab");
}

TEST_F(UiEditHistoryTests, RecordBoundEvictsOldestCompleteRecords){
    EditModel limited(m_arena, { 16u, 2u, 128u });
    ASSERT_TRUE(limited.replaceSelection("a"));
    ASSERT_TRUE(limited.replaceSelection("b"));
    ASSERT_TRUE(limited.replaceSelection("c"));
    ASSERT_TRUE(limited.undo());
    EXPECT_EQ(limited.text(), "ab");
    ASSERT_TRUE(limited.undo());
    EXPECT_EQ(limited.text(), "a");
    EXPECT_FALSE(limited.undo());
    ASSERT_TRUE(limited.redo());
    ASSERT_TRUE(limited.redo());
    EXPECT_EQ(limited.text(), "abc");
}

TEST_F(UiEditHistoryTests, ByteBoundAdmitsCompleteNewestRecord){
    EditModel limited(m_arena, { 16u, 8u, 6u });
    ASSERT_TRUE(limited.replaceSelection("a"));
    ASSERT_TRUE(limited.replaceSelection("b"));
    ASSERT_TRUE(limited.replaceSelection("c"));
    ASSERT_TRUE(limited.undo());
    EXPECT_EQ(limited.text(), "ab");
    EXPECT_FALSE(limited.undo());
    ASSERT_TRUE(limited.redo());
    EXPECT_EQ(limited.text(), "abc");
}

TEST_F(UiEditHistoryTests, OversizedHistoryRecordClearsEarlierUndoToPreventSkippingLatestEdit){
    EditModel limited(m_arena, { 32u, 8u, 4u });
    ASSERT_TRUE(limited.replaceSelection("a"));
    ASSERT_TRUE(limited.replaceSelection("b"));
    EXPECT_TRUE(limited.canUndo());
    ASSERT_TRUE(limited.replaceSelection("c"));
    EXPECT_EQ(limited.text(), "abc");
    EXPECT_FALSE(limited.canUndo());
    EXPECT_FALSE(limited.canRedo());
}

TEST_F(UiEditHistoryTests, DisabledHistoryAndCopiedReplacementHaveBoundedOwnership){
    EditModel limited(m_arena, { 32u, 0u, 0u });
    AString<Core::Alloc::GlobalArena> replacement("owned", m_arena);
    ASSERT_TRUE(limited.replaceSelection({ replacement.data(), replacement.size() }));
    replacement.assign("other");
    EXPECT_EQ(limited.text(), "owned");
    EXPECT_FALSE(limited.undo());
    EXPECT_FALSE(limited.redo());
}

TEST_F(UiEditHistoryTests, NativeSurroundingDeleteRestoresOriginalCaretAndSelectionOnUndo){
    ASSERT_TRUE(m_model.setText("abCde"));
    ASSERT_TRUE(m_model.setSelection(3u, 3u));
    ASSERT_TRUE(m_model.eraseSurrounding(1u, 1u));
    EXPECT_EQ(m_model.text(), "abe");
    EXPECT_EQ(m_model.caret(), 2u);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abCde");
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 3u);
    ASSERT_TRUE(m_model.setSelection(1u, 3u));
    ASSERT_TRUE(m_model.eraseSurrounding(1u, 1u));
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.anchor(), 1u);
    EXPECT_EQ(m_model.caret(), 3u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

