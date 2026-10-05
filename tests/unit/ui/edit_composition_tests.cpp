// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_composition_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiEditTests;

class UiEditCompositionTests : public EditFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditCompositionTests, PreeditOwnsTransientTextWithoutPublishingCommittedRevisionOrHistory){
    ASSERT_TRUE(m_model.setText("abcd"));
    ASSERT_TRUE(m_model.setSelection(3u, 1u));
    const u64 revision = m_model.revision();
    ASSERT_TRUE(m_model.beginComposition());
    AString<Core::Alloc::GlobalArena> value("xy", m_arena);
    ASSERT_TRUE(m_model.updateComposition({ value.data(), value.size() }, 0u, 2u));
    value.assign("zz");
    const EditCompositionView composition = m_model.composition();
    EXPECT_TRUE(composition.active);
    EXPECT_EQ(composition.text, "xy");
    EXPECT_EQ(composition.anchor, 0u);
    EXPECT_EQ(composition.caret, 2u);
    EXPECT_EQ(composition.replacementStart, 1u);
    EXPECT_EQ(composition.replacementEnd, 3u);
    EXPECT_EQ(m_model.text(), "abcd");
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_EQ(m_model.anchor(), 3u);
    EXPECT_EQ(m_model.caret(), 1u);
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditCompositionTests, NativePreeditClearBeforeCommitRetainsOriginalReplacementBaseline){
    ASSERT_TRUE(m_model.setText("abcd"));
    ASSERT_TRUE(m_model.setSelection(1u, 3u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("transient", 4u, 4u));
    ASSERT_TRUE(m_model.updateComposition({}, 0u, 0u));
    EXPECT_TRUE(m_model.composition().active);
    EXPECT_EQ(m_model.composition().replacementStart, 1u);
    EXPECT_EQ(m_model.composition().replacementEnd, 3u);
    ASSERT_TRUE(m_model.commitComposition("X"));
    EXPECT_EQ(m_model.text(), "aXd");
}

TEST_F(UiEditCompositionTests, CancellationPreservesCommittedTextSelectionRevisionAndHistory){
    ASSERT_TRUE(m_model.setText("abc"));
    ASSERT_TRUE(m_model.replaceSelection("d"));
    ASSERT_TRUE(m_model.setSelection(1u, 3u));
    const u64 revision = m_model.revision();
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("x", 0u, 1u));
    m_model.cancelComposition();
    EXPECT_EQ(m_model.text(), "abcd");
    EXPECT_EQ(m_model.anchor(), 1u);
    EXPECT_EQ(m_model.caret(), 3u);
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_TRUE(m_model.canUndo());
    EXPECT_FALSE(m_model.composition().active);
}

TEST_F(UiEditCompositionTests, InvalidUpdateAndCommitKeepPreviousCompositionAndCommittedState){
    EditModel limited(m_arena, { 4u, 8u, 128u });
    ASSERT_TRUE(limited.setText("abcd"));
    ASSERT_TRUE(limited.setSelection(1u, 3u));
    const u64 revision = limited.revision();
    ASSERT_TRUE(limited.beginComposition());
    ASSERT_TRUE(limited.updateComposition("x", 0u, 1u));
    EXPECT_FALSE(limited.updateComposition("xyz", 0u, 3u));
    EXPECT_FALSE(limited.updateComposition("\xC0\xAF", 0u, 2u));
    EXPECT_FALSE(limited.updateComposition("\xE1\x84\x80", 1u, 1u));
    EXPECT_FALSE(limited.commitComposition("xyz"));
    EXPECT_FALSE(limited.commitComposition("\n"));
    EXPECT_EQ(limited.text(), "abcd");
    EXPECT_EQ(limited.anchor(), 1u);
    EXPECT_EQ(limited.caret(), 3u);
    EXPECT_EQ(limited.revision(), revision);
    EXPECT_TRUE(limited.composition().active);
    EXPECT_EQ(limited.composition().text, "x");
    EXPECT_FALSE(limited.canUndo());
}

TEST_F(UiEditCompositionTests, PreeditMaySelectScalarBoundaryInsideCombiningGrapheme){
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("a\xCC\x81", 1u, 3u));
    EXPECT_EQ(m_model.composition().anchor, 1u);
    EXPECT_EQ(m_model.composition().caret, 3u);
    ASSERT_TRUE(m_model.commitComposition(m_model.composition().text));
    EXPECT_EQ(m_model.text(), "a\xCC\x81");
    EXPECT_EQ(m_model.caret(), 3u);
    EXPECT_FALSE(m_model.setSelection(1u, 1u));
}

TEST_F(UiEditCompositionTests, AcceptedOrdinaryEditAndNavigationCancelPreeditWithoutPublishingIt){
    ASSERT_TRUE(m_model.setText("ab"));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("ignored", 0u, 7u));
    ASSERT_TRUE(m_model.replaceSelection("c"));
    EXPECT_EQ(m_model.text(), "abc");
    EXPECT_FALSE(m_model.composition().active);
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("ignored", 0u, 7u));
    ASSERT_TRUE(m_model.move(EditMove::Home));
    EXPECT_EQ(m_model.caret(), 0u);
    EXPECT_FALSE(m_model.composition().active);
}

TEST_F(UiEditCompositionTests, InvalidSelectionPreservesActiveCompositionAndNoSessionCommitIsRejected){
    EXPECT_FALSE(m_model.commitComposition("x"));
    EXPECT_FALSE(m_model.updateComposition("x", 0u, 1u));
    ASSERT_TRUE(m_model.setText("a\xCC\x81"));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("x", 0u, 1u));
    EXPECT_FALSE(m_model.setSelection(1u, 1u));
    EXPECT_TRUE(m_model.composition().active);
    EXPECT_EQ(m_model.composition().text, "x");
    ASSERT_TRUE(m_model.beginComposition());
    EXPECT_EQ(m_model.composition().text, "x");
}

TEST_F(UiEditCompositionTests, InvalidSurroundingByteRangesPreserveCompositionAndZeroDeletePreservesSelection){
    ASSERT_TRUE(m_model.setText("a\xCC\x81z"));
    ASSERT_TRUE(m_model.setSelection(0u, 3u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("x", 0u, 1u));
    const u64 revision = m_model.revision();
    EXPECT_FALSE(m_model.eraseSurrounding(1u, 0u));
    EXPECT_FALSE(m_model.eraseSurrounding(4u, 0u));
    EXPECT_FALSE(m_model.eraseSurrounding(0u, 2u));
    ASSERT_TRUE(m_model.eraseSurrounding(0u, 0u));
    EXPECT_EQ(m_model.text(), "a\xCC\x81z");
    EXPECT_EQ(m_model.anchor(), 0u);
    EXPECT_EQ(m_model.caret(), 3u);
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_TRUE(m_model.composition().active);
    EXPECT_EQ(m_model.composition().text, "x");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditCompositionTests, AcceptedSurroundingDeleteCancelsPreeditWithoutPublishingTransientText){
    ASSERT_TRUE(m_model.setText("abcd"));
    ASSERT_TRUE(m_model.setSelection(2u, 2u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("transient", 0u, 9u));
    ASSERT_TRUE(m_model.eraseSurrounding(1u, 1u));
    EXPECT_EQ(m_model.text(), "ad");
    EXPECT_FALSE(m_model.composition().active);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "abcd");
    EXPECT_EQ(m_model.anchor(), 2u);
    EXPECT_EQ(m_model.caret(), 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

