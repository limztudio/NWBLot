// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_caret_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_caret_range_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiEditCaretTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(EditCaretFixture, SingleLineLigatureRangesUseInterpolatedCaretEdges){
    ASSERT_TRUE(adoptText("ffi", EditTextMode::SingleLine));
    expectRange({ 1u, 2u }, 0u, { 10.0f, 0.0f, 10.0f, 12.0f });
    expectRange({ 1u, 1u }, 0u, {});
}

TEST_F(EditCaretFixture, CrossLineSelectionIncludesContentAndEachSelectedBreakCap){
    ASSERT_TRUE(adoptText("abc\nx\nyz"));
    expectRange({ 1u, 7u }, 0u, { 10.0f, 0.0f, 21.0f, 12.0f });
    expectRange({ 1u, 7u }, 1u, { 0.0f, 12.0f, 11.0f, 12.0f });
    expectRange({ 1u, 7u }, 2u, { 0.0f, 24.0f, 10.0f, 12.0f });
}

TEST_F(EditCaretFixture, SelectingOnlyLfPaintsItsPrecedingLineCap){
    ASSERT_TRUE(adoptText("abc\nx"));
    expectRange({ 3u, 4u }, 0u, { 30.0f, 0.0f, 1.0f, 12.0f });
    expectRange({ 3u, 4u }, 1u, {});
}

TEST_F(EditCaretFixture, EmptyLineLfSelectionHasAVisibleCap){
    ASSERT_TRUE(adoptText("a\n\nb"));
    expectRange({ 2u, 3u }, 0u, {});
    expectRange({ 2u, 3u }, 1u, { 0.0f, 12.0f, 1.0f, 12.0f });
    expectRange({ 2u, 3u }, 2u, {});
}

TEST_F(EditCaretFixture, TrailingLfDoesNotAddASecondCapOnTheEmptyTerminalLine){
    ASSERT_TRUE(adoptText("ab\n"));
    expectRange({ 0u, 3u }, 0u, { 0.0f, 0.0f, 21.0f, 12.0f });
    expectRange({ 0u, 3u }, 1u, {});
    expectRange({ 3u, 3u }, 0u, {});
    expectRange({ 3u, 3u }, 1u, {});
}

TEST_F(EditCaretFixture, NonintersectingAndCollapsedRangesReturnEmptyRectangles){
    ASSERT_TRUE(adoptText("abc\nx\nyz"));
    expectRange({ 6u, 8u }, 0u, {});
    expectRange({ 6u, 8u }, 1u, {});
    expectRange({ 0u, 1u }, 2u, {});
    for(u32 line = 0u; line < m_geometry.lines().size(); ++line)
        expectRange({ 0u, 0u }, line, {});
}

TEST_F(EditCaretFixture, ScalarPreeditRangesCanCrossLinesInsideDisplayGraphemes){
    m_mapping.push_back({ 0u, 0u });
    m_mapping.push_back({ 0u, 5u });
    ASSERT_TRUE(adopt("e\xcc\x81\nZ", 0u));
    expectRange({ 1u, 5u }, 0u, { 5.0f, 0.0f, 6.0f, 12.0f });
    expectRange({ 1u, 5u }, 1u, { 0.0f, 12.0f, 10.0f, 12.0f });
    expectRange({ 0u, 1u }, 0u, { 0.0f, 0.0f, 5.0f, 12.0f });
}

TEST_F(EditCaretFixture, BreakCapWidthIsAppliedOnlyToSelectedLfBytes){
    ASSERT_TRUE(adoptText("ab\nc"));
    expectRange({ 0u, 3u }, 0u, { 0.0f, 0.0f, 20.0f, 12.0f }, 0.0f);
    expectRange({ 0u, 3u }, 0u, { 0.0f, 0.0f, 20.5f, 12.0f }, 0.5f);
    expectRange({ 1u, 2u }, 0u, { 10.0f, 0.0f, 10.0f, 12.0f }, 100.0f);
    expectRange({ 3u, 4u }, 1u, { 0.0f, 12.0f, 10.0f, 12.0f }, 100.0f);
}

TEST_F(EditCaretFixture, InvalidRangeEndpointsAndLineIndicesRejectQueries){
    ASSERT_TRUE(adoptText("e\xcc\x81\nx"));
    const EditBoxRange invalid[]{ { 3u, 1u }, { 0u, 6u }, { 2u, 3u }, { 1u, 2u } };
    for(const EditBoxRange range : invalid){
        EXPECT_FALSE(m_geometry.rangeOnLine(range, 0u, 1.0f));
    }
    EXPECT_FALSE(m_geometry.rangeOnLine({ 0u, 1u }, 2u, 1.0f));
}

TEST_F(EditCaretFixture, InvalidBreakCapsRejectEvenCollapsedRanges){
    ASSERT_TRUE(adoptText("ab\nc"));
    const f32 invalid[]{ -1.0f, Limit<f32>::s_QuietNaN, Limit<f32>::s_Infinity, -Limit<f32>::s_Infinity };
    for(const f32 cap : invalid){
        EXPECT_FALSE(m_geometry.rangeOnLine({ 0u, 3u }, 0u, cap));
        EXPECT_FALSE(m_geometry.rangeOnLine({ 1u, 1u }, 0u, cap));
    }
}

TEST_F(EditCaretFixture, OverflowingSelectedBreakWidthRejectsQuery){
    m_shaper.scalarAdvance = Limit<f32>::s_Max;
    ASSERT_TRUE(adoptText("a\n"));
    EXPECT_FALSE(m_geometry.rangeOnLine({ 0u, 2u }, 0u, Limit<f32>::s_Max));
    expectRange({ 0u, 1u }, 0u, { 0.0f, 0.0f, Limit<f32>::s_Max, 12.0f });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

