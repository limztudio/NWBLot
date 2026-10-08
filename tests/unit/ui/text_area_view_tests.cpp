// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "multiline_view_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_view_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiMultilineViewTests;

class UiTextAreaViewTests : public MultilineViewFixture{
protected:
    [[nodiscard]] bool prepareDense(){ return m_model.setText("abcdef\nabcdef\nabcdef") && shapeView(); }

    [[nodiscard]] bool manualPlace(const Point scroll, const f32 width = 20.0f, const f32 height = 12.0f){
        const auto placement = m_view.arrange(
            { 10.0f, 20.0f, width, height }, {}, { 0.0f, 0.0f, 500.0f, 500.0f }, scroll, 1.0f, false
        );
        if(!placement)
            return false;
        m_placement = *placement;
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextAreaViewTests, SuppressedCaretRevealPreservesBothRequestedAxesAndHitMapping){
    ASSERT_TRUE(prepareDense());
    ASSERT_TRUE(manualPlace({ 5.0f, 6.0f }));
    EXPECT_FLOAT_EQ(m_placement.scroll, 5.0f);
    EXPECT_FLOAT_EQ(m_placement.scrollY, 6.0f);
    EXPECT_FLOAT_EQ(m_placement.textOrigin.x, 5.0f);
    EXPECT_FLOAT_EQ(m_placement.textOrigin.y, 14.0f);
    EXPECT_GT(m_placement.caret.x, m_placement.clip.x + m_placement.clip.width);
    EXPECT_GT(m_placement.caret.y, m_placement.clip.y + m_placement.clip.height);
    Expected<usize> hit = MakeUnexpected(Failure{});
    hit = m_view.hitTest({ 15.0f, 20.0f }, m_placement);
    ASSERT_TRUE(hit);
    EXPECT_EQ(*hit, 1u);
}

TEST_F(UiTextAreaViewTests, SuppressedRevealStillClampsToBothContentExtents){
    ASSERT_TRUE(prepareDense());
    ASSERT_TRUE(manualPlace({ 100.0f, 100.0f }));
    EXPECT_FLOAT_EQ(m_placement.scroll, 41.0f);
    EXPECT_FLOAT_EQ(m_placement.scrollY, 24.0f);
    ExpectRect(m_placement.caret, { 29.0f, 20.0f, 1.0f, 12.0f });
}

TEST_F(UiTextAreaViewTests, ShrinkingContentClampsExplicitScrollWithoutCaretFollowing){
    ASSERT_TRUE(prepareDense());
    ASSERT_TRUE(manualPlace({ 100.0f, 100.0f }));
    ASSERT_TRUE(m_model.setText("x\n"));
    ASSERT_TRUE(m_model.setSelection(0u, 0u));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(manualPlace({ 41.0f, 24.0f }, 200.0f, 100.0f));
    EXPECT_FLOAT_EQ(m_placement.scroll, 0.0f);
    EXPECT_FLOAT_EQ(m_placement.scrollY, 0.0f);
    EXPECT_FLOAT_EQ(m_placement.textOrigin.x, 10.0f);
    EXPECT_FLOAT_EQ(m_placement.textOrigin.y, 20.0f);
    ExpectRect(m_placement.caret, { 10.0f, 20.0f, 1.0f, 12.0f });
}

TEST_F(UiTextAreaViewTests, ZeroViewportDoesNotForceManualScrollToTheCaret){
    ASSERT_TRUE(prepareDense());
    ASSERT_TRUE(manualPlace({ 5.0f, 6.0f }, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(m_placement.scroll, 5.0f);
    EXPECT_FLOAT_EQ(m_placement.scrollY, 6.0f);
    EXPECT_FLOAT_EQ(m_placement.content.width, 0.0f);
    EXPECT_FLOAT_EQ(m_placement.content.height, 0.0f);
    EXPECT_FLOAT_EQ(m_placement.clip.width, 0.0f);
    EXPECT_FLOAT_EQ(m_placement.clip.height, 0.0f);
    EXPECT_TRUE(m_view.ready());
}

TEST_F(UiTextAreaViewTests, FailedManualArrangementPreservesThePriorPlacement){
    ASSERT_TRUE(prepareDense());
    ASSERT_TRUE(manualPlace({ 5.0f, 6.0f }));
    const EditBoxPlacement previous = m_placement;
    const Point invalid[]{ { -1.0f, 0.0f }, { 0.0f, Limit<f32>::s_QuietNaN }, { Limit<f32>::s_Infinity, 0.0f } };
    for(const Point scroll : invalid){
        EXPECT_FALSE(manualPlace(scroll));
        ExpectPlacement(m_placement, previous);
    }
    EXPECT_FALSE(m_view.arrange({ 10.0f, 20.0f, 20.0f, 12.0f }, {}, { 0.0f, 0.0f, 500.0f, 500.0f }, Point{ 5.0f, 6.0f }, 0.0f, false));
    ExpectPlacement(m_placement, previous);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

