// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_scroll_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiTextAreaTests;

class UiTextAreaScrollTests : public TextAreaFixture{
protected:
    [[nodiscard]] static AStringView denseText(){
        return "aaaaaaaaaaaaaaaaaaaa\naaaaaaaaaaaaaaaaaaaa\naaaaaaaaaaaaaaaaaaaa\n"
            "aaaaaaaaaaaaaaaaaaaa\naaaaaaaaaaaaaaaaaaaa\naaaaaaaaaaaaaaaaaaaa";
    }

    [[nodiscard]] static TextAreaOptions smallOptions(){
        TextAreaOptions options;
        options.width = { LayoutSizePolicy::Fixed, 90.0f };
        options.height = { LayoutSizePolicy::Fixed, 42.0f };
        return options;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextAreaScrollTests, ExplicitTwoAxisScrollSurvivesTheFirstModelBinding){
    ASSERT_TRUE(m_model.setText(denseText()));
    ASSERT_TRUE(m_state.scrollTo({ 17.0f, 19.0f }));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(frameArea(1u, smallOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 17.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 19.0f);
    EXPECT_EQ(m_state.revision(), revision);
    EXPECT_FLOAT_EQ(m_state.placement().textOrigin.x, m_state.placement().content.x - 17.0f);
    EXPECT_FLOAT_EQ(m_state.placement().textOrigin.y, m_state.placement().content.y - 19.0f);
    EXPECT_GT(m_state.placement().caret.x, m_state.placement().content.x + m_state.placement().content.width);
    EXPECT_GT(m_state.placement().caret.y, m_state.placement().content.y + m_state.placement().content.height);
    EXPECT_EQ(m_model.caret(), denseText().size());
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiTextAreaScrollTests, IdleAndResizeClampExplicitViewportWithoutAdvancingItsIntentEpoch){
    useHost();
    ASSERT_TRUE(m_model.setText(denseText()));
    ASSERT_TRUE(m_state.scrollTo({ 17.0f, 19.0f }));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(frameArea(1u, smallOptions()));
    ASSERT_TRUE(frameArea(2u, smallOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 17.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 19.0f);
    EXPECT_EQ(m_state.revision(), revision);
    TextAreaOptions larger;
    larger.width = { LayoutSizePolicy::Fixed, 500.0f };
    larger.height = { LayoutSizePolicy::Fixed, 300.0f };
    ASSERT_TRUE(frameArea(3u, larger));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    EXPECT_EQ(m_state.revision(), revision);
    EXPECT_FLOAT_EQ(m_state.placement().textOrigin.x, m_state.placement().content.x);
    EXPECT_FLOAT_EQ(m_state.placement().textOrigin.y, m_state.placement().content.y);
    ASSERT_EQ(m_host.publications.size(), 3u);
    EXPECT_FLOAT_EQ(m_host.publications[0u].placement.scroll, 17.0f);
    EXPECT_FLOAT_EQ(m_host.publications[0u].placement.scrollY, 19.0f);
    EXPECT_EQ(m_host.publications[0u].text, denseText());
}

TEST_F(UiTextAreaScrollTests, AutomaticRevealSurvivesIdleViewportShrinkWithoutNewModelIntent){
    useHost();
    ASSERT_TRUE(m_model.setText(denseText()));
    TextAreaOptions larger;
    larger.width = { LayoutSizePolicy::Fixed, 500.0f };
    larger.height = { LayoutSizePolicy::Fixed, 300.0f };
    ASSERT_TRUE(frameArea(1u, larger));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    const u64 revision = m_model.revision();
    const u64 selection = m_model.selectionGeneration();
    const u64 stateRevision = m_state.revision();
    ASSERT_TRUE(frameArea(2u, smallOptions()));
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
    const EditBoxPlacement& placement = m_state.placement();
    EXPECT_GE(placement.caret.x, placement.content.x);
    EXPECT_GE(placement.caret.y, placement.content.y);
    EXPECT_LE(placement.caret.x + placement.caret.width, placement.content.x + placement.content.width + 0.001f);
    EXPECT_LE(placement.caret.y + placement.caret.height, placement.content.y + placement.content.height + 0.001f);
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_EQ(m_model.selectionGeneration(), selection);
    EXPECT_EQ(m_state.revision(), stateRevision);
    EXPECT_FALSE(m_result.textChanged || m_result.selectionChanged);
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiTextAreaScrollTests, ExplicitOverscrollClampsToBothContentExtents){
    ASSERT_TRUE(m_model.setText(denseText()));
    ASSERT_TRUE(m_state.scrollTo({ 10000.0f, 10000.0f }));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(frameArea(1u, smallOptions()));
    const EditBoxPlacement& placement = m_state.placement();
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
    EXPECT_LT(m_state.scroll().x, 10000.0f);
    EXPECT_LT(m_state.scroll().y, 10000.0f);
    EXPECT_NEAR(placement.caret.x + placement.caret.width, placement.content.x + placement.content.width, 0.001f);
    EXPECT_NEAR(placement.caret.y + placement.caret.height, placement.content.y + placement.content.height, 0.001f);
    EXPECT_EQ(m_state.revision(), revision);
}

TEST_F(UiTextAreaScrollTests, AcceptedIdenticalSelectionResumesRevealAfterManualScrolling){
    ASSERT_TRUE(m_model.setText(denseText()));
    ASSERT_TRUE(frameArea(1u, smallOptions()));
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
    ASSERT_TRUE(m_state.scrollTo({ 0.0f, 0.0f }));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(frameArea(2u, smallOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    const u64 selectionGeneration = m_model.selectionGeneration();
    ASSERT_TRUE(m_model.setSelection(m_model.anchor(), m_model.caret()));
    EXPECT_GT(m_model.selectionGeneration(), selectionGeneration);
    ASSERT_TRUE(frameArea(3u, smallOptions()));
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
    EXPECT_EQ(m_state.revision(), revision);
    EXPECT_EQ(m_model.text(), denseText());
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiTextAreaScrollTests, CopyAndUnchangedSubmitPreserveTheExplicitViewport){
    useHost();
    ASSERT_TRUE(m_model.setText(denseText()));
    ASSERT_TRUE(frameArea(1u, smallOptions()));
    ASSERT_TRUE(m_state.scrollTo({ 0.0f, 0.0f }));
    const u64 revision = m_state.revision();
    const u64 selectionGeneration = m_model.selectionGeneration();
    m_host.key(EditKey::C, true);
    m_host.key(EditKey::Enter, true);
    ASSERT_TRUE(frameArea(2u, smallOptions()));
    EXPECT_TRUE(m_result.submitted);
    EXPECT_FALSE(m_result.textChanged || m_result.selectionChanged);
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    EXPECT_EQ(m_state.revision(), revision);
    EXPECT_EQ(m_model.selectionGeneration(), selectionGeneration);
    EXPECT_EQ(m_model.text(), denseText());
    ASSERT_EQ(m_host.actionTexts.size(), 1u);
    EXPECT_EQ(m_host.actionTexts.front(), denseText());
    m_host.selection(m_model.anchor(), m_model.caret());
    ASSERT_TRUE(frameArea(3u, smallOptions()));
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_GT(m_model.selectionGeneration(), selectionGeneration);
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
}

TEST_F(UiTextAreaScrollTests, AcceptedTextResumesRevealAndPublicationOwnsItsBytes){
    useHost();
    ASSERT_TRUE(m_model.setText(denseText()));
    ASSERT_TRUE(frameArea(1u, smallOptions()));
    ASSERT_TRUE(m_state.scrollTo({ 0.0f, 0.0f }));
    ASSERT_TRUE(frameArea(2u, smallOptions()));
    m_host.text("b");
    ASSERT_TRUE(frameArea(3u, smallOptions()));
    EXPECT_TRUE(m_result.textChanged);
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
    AString<Core::Alloc::GlobalArena> displayed(m_arena);
    displayed.assign(denseText().data(), denseText().size());
    displayed.push_back('b');
    ASSERT_EQ(m_host.publications.size(), 3u);
    EXPECT_EQ(m_host.publications.back().text, displayed);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), denseText());
    EXPECT_EQ(m_host.publications.back().text, displayed);
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiTextAreaScrollTests, KnownModelRebindingResetsAndRevealsTheNewCaret){
    ASSERT_TRUE(m_model.setText(denseText()));
    ASSERT_TRUE(m_state.scrollTo({ 17.0f, 19.0f }));
    ASSERT_TRUE(frameArea(1u, smallOptions()));
    EditModel replacement(m_arena, {}, EditTextMode::Multiline);
    ASSERT_TRUE(replacement.setText(denseText()));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(beginArea(2u));
    ASSERT_TRUE(m_builder.textArea("area", replacement, m_state, smallOptions()).valid);
    ASSERT_TRUE(acceptArea());
    EXPECT_GT(m_state.scroll().x, 17.0f);
    EXPECT_GT(m_state.scroll().y, 19.0f);
    EXPECT_EQ(m_state.revision(), revision);
    EXPECT_EQ(replacement.caret(), denseText().size());
    EXPECT_EQ(m_model.text(), denseText());
}

TEST_F(UiTextAreaScrollTests, ResetThenScrollSeedsANewInitialBinding){
    ASSERT_TRUE(m_model.setText(denseText()));
    ASSERT_TRUE(frameArea(1u, smallOptions()));
    m_state.reset();
    ASSERT_TRUE(m_state.scrollTo({ 17.0f, 19.0f }));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(frameArea(2u, smallOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 17.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 19.0f);
    EXPECT_EQ(m_state.revision(), revision);
    EXPECT_EQ(m_model.caret(), denseText().size());
    EXPECT_FALSE(m_model.canUndo());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

