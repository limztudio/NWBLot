// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_navigation_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_navigation_geometry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditNavigationTestSupport;

TEST_F(UiEditNavigationHostTests, PublishedLineOwnershipMakesEqualXHitDifferentHardLines){
    ASSERT_TRUE(m_navigationModel.setText("ab\ncd\nxy"));
    ASSERT_TRUE(m_navigationModel.setSelection(0u, 0u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(click({ 20.0f, 26.0f }));
    ASSERT_TRUE(navigationFrame());
    EXPECT_EQ(m_navigationModel.caret(), 1u);
    ASSERT_TRUE(m_navigation.setPreferredX(99.0f));
    ASSERT_TRUE(click({ 20.0f, 38.0f }));
    ASSERT_TRUE(navigationFrame());
    EXPECT_EQ(m_navigationModel.caret(), 4u);
    EXPECT_FALSE(m_navigation.snapshot().valid);
    ASSERT_TRUE(click({ 20.0f, 50.0f }));
    ASSERT_TRUE(navigationFrame());
    EXPECT_EQ(m_navigationModel.caret(), 7u);
    EXPECT_EQ(m_navigationModel.anchor(), 7u);
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_FALSE(m_navigationModel.canUndo());
    EXPECT_TRUE(m_resolver.records.empty());
}

TEST_F(UiEditNavigationHostTests, CandidateGeometryCannotReplacePublishedYHitsBeforeAcceptance){
    ASSERT_TRUE(m_navigationModel.setText("ab\ncd\nxy"));
    ASSERT_TRUE(m_navigationModel.setSelection(0u, 0u));
    ASSERT_TRUE(activateNavigation());
    const u64 displayed = m_context.input().layoutGeneration();
    const TextInputRect native = m_textInput.nativeCaret();
    ASSERT_TRUE(prepareNavigation({}, { 10.0f, 80.0f, 180.0f, 48.0f }));
    EXPECT_EQ(m_context.input().layoutGeneration(), displayed);
    m_host.commitFrame(m_generation);
    EXPECT_EQ(m_textInput.nativeCaret().y, native.y);
    ASSERT_TRUE(click({ 20.0f, 38.0f }));
    ASSERT_TRUE(commit());
    ASSERT_TRUE(navigationFrame({}, { 10.0f, 80.0f, 180.0f, 48.0f }));
    EXPECT_EQ(m_navigationModel.caret(), 4u);
    EXPECT_EQ(m_navigationModel.anchor(), 4u);
    EXPECT_FLOAT_EQ(m_placement.caret.y, 92.0f);
    EXPECT_EQ(m_textInput.nativeCaret().y, 92);
    EXPECT_TRUE(m_resolver.records.empty());
}

TEST_F(UiEditNavigationHostTests, PublishedVerticalScrollOriginSelectsVisibleHardLines){
    ASSERT_TRUE(m_navigationModel.setText("a\nb\nc\nd\ne\nf"));
    ASSERT_TRUE(m_navigationModel.setSelection(8u, 8u));
    ASSERT_TRUE(activateNavigation({}, { 10.0f, 20.0f, 180.0f, 24.0f }));
    EXPECT_FLOAT_EQ(m_placement.scrollY, 36.0f);
    EXPECT_FLOAT_EQ(m_placement.textOrigin.y, -16.0f);
    ASSERT_TRUE(click({ 10.0f, 26.0f }));
    ASSERT_TRUE(navigationFrame({}, { 10.0f, 20.0f, 180.0f, 24.0f }, { 0.0f, 36.0f }));
    EXPECT_EQ(m_navigationModel.caret(), 6u);
    EXPECT_FLOAT_EQ(m_placement.scrollY, 36.0f);
    ASSERT_TRUE(click({ 10.0f, 38.0f }));
    ASSERT_TRUE(navigationFrame({}, { 10.0f, 20.0f, 180.0f, 24.0f }, { 0.0f, 36.0f }));
    EXPECT_EQ(m_navigationModel.caret(), 8u);
    EXPECT_FALSE(m_navigationModel.canUndo());
}

TEST_F(UiEditNavigationHostTests, PublishedTwoAxisScrollTransformsPointerCoordinates){
    ASSERT_TRUE(m_navigationModel.setText("abcdef\nuvwxyz"));
    ASSERT_TRUE(m_navigationModel.setSelection(13u, 13u));
    ASSERT_TRUE(activateNavigation({}, { 10.0f, 20.0f, 21.0f, 12.0f }));
    EXPECT_FLOAT_EQ(m_placement.scroll, 40.0f);
    EXPECT_FLOAT_EQ(m_placement.scrollY, 12.0f);
    EXPECT_FLOAT_EQ(m_placement.textOrigin.x, -30.0f);
    EXPECT_FLOAT_EQ(m_placement.textOrigin.y, 8.0f);
    ASSERT_TRUE(click({ 10.0f, 26.0f }));
    ASSERT_TRUE(navigationFrame({}, { 10.0f, 20.0f, 21.0f, 12.0f }, { 40.0f, 12.0f }));
    EXPECT_EQ(m_navigationModel.caret(), 11u);
    EXPECT_EQ(m_navigationModel.anchor(), 11u);
    EXPECT_FLOAT_EQ(m_placement.scroll, 40.0f);
    EXPECT_FLOAT_EQ(m_placement.scrollY, 12.0f);
}

TEST_F(UiEditNavigationHostTests, CapturedCrossLineDragClampsPastThePublishedViewport){
    ASSERT_TRUE(m_navigationModel.setText("ab\ncd\nxy"));
    ASSERT_TRUE(m_navigationModel.setSelection(0u, 0u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PrimaryDown, .position = { 20.0f, 26.0f } }));
    ASSERT_EQ(m_context.input().capture(), m_widget.id);
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PointerMove, .position = { 30.0f, 120.0f } }));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PrimaryUp, .position = { 30.0f, 120.0f } }));
    EXPECT_EQ(m_navigationModel.caret(), 0u);
    ASSERT_TRUE(navigationFrame());
    EXPECT_EQ(m_navigationModel.anchor(), 1u);
    EXPECT_EQ(m_navigationModel.caret(), 8u);
    EXPECT_EQ(m_navigationModel.selectedText(), "b\ncd\nxy");
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FALSE(m_navigationModel.canUndo());
}

TEST_F(UiEditNavigationHostTests, EmptyAndTrailingHardLinesPublishDistinctEndpoints){
    ASSERT_TRUE(m_navigationModel.setText("a\n\n"));
    ASSERT_TRUE(m_navigationModel.setSelection(0u, 0u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(click({ 20.0f, 38.0f }));
    ASSERT_TRUE(navigationFrame());
    EXPECT_EQ(m_navigationModel.caret(), 2u);
    ASSERT_TRUE(click({ 20.0f, 50.0f }));
    ASSERT_TRUE(navigationFrame());
    EXPECT_EQ(m_navigationModel.caret(), 3u);
    EXPECT_EQ(m_navigationModel.anchor(), 3u);
    EXPECT_EQ(m_navigationModel.text(), "a\n\n");
    EXPECT_FALSE(m_navigationModel.canUndo());
}

TEST_F(UiEditNavigationHostTests, EarlierCommittedTextRetiresQueuedPublishedPointerPositions){
    ASSERT_TRUE(m_navigationModel.setText("ab\ncd\nxy"));
    ASSERT_TRUE(m_navigationModel.setSelection(0u, 0u));
    ASSERT_TRUE(activateNavigation());
    ASSERT_TRUE(commitNative("Q"));
    ASSERT_TRUE(click({ 20.0f, 38.0f }));
    ASSERT_TRUE(navigationFrame());
    EXPECT_EQ(m_navigationModel.text(), "Qab\ncd\nxy");
    EXPECT_EQ(m_navigationModel.anchor(), 1u);
    EXPECT_EQ(m_navigationModel.caret(), 1u);
    EXPECT_TRUE(m_resolver.records.empty());
    ASSERT_TRUE(m_navigationModel.undo());
    EXPECT_EQ(m_navigationModel.text(), "ab\ncd\nxy");
    EXPECT_FALSE(m_navigationModel.canUndo());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

