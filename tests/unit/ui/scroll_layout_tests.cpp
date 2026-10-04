// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/scroll.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_scroll_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

static_assert(!IsConstructible_V<ScrollState, const ScrollState&>);
static_assert(!IsConstructible_V<ScrollState, ScrollState&&>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ExpectPlacement(const ScrollPlacement& actual, const ScrollPlacement& expected){
    NWB::UiWidgetTests::ExpectRect(actual.bounds, expected.bounds);
    NWB::UiWidgetTests::ExpectRect(actual.viewport, expected.viewport);
    NWB::UiWidgetTests::ExpectRect(actual.contentClip, expected.contentClip);
    NWB::UiWidgetTests::ExpectRect(actual.track, expected.track);
    NWB::UiWidgetTests::ExpectRect(actual.thumb, expected.thumb);
    EXPECT_DOUBLE_EQ(actual.contentHeight, expected.contentHeight);
    EXPECT_DOUBLE_EQ(actual.maxOffset, expected.maxOffset);
    EXPECT_DOUBLE_EQ(actual.offset, expected.offset);
    EXPECT_EQ(actual.firstRow, expected.firstRow);
    EXPECT_EQ(actual.endRow, expected.endRow);
    EXPECT_EQ(actual.rowCount, expected.rowCount);
    EXPECT_EQ(actual.scrollbarVisible, expected.scrollbarVisible);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiScrollLayoutTests, EmptyContentUsesFullPaddedViewportAndClearsPreviousRange){
    ScrollPlacement placement;
    placement.firstRow = 7u;
    placement.endRow = 9u;
    ASSERT_TRUE(ScrollLayout::Calculate({ 10.0f, 20.0f, 200.0f, 120.0f }, { 0.0f, 0.0f, 400.0f, 300.0f },
        { 8.0f, 6.0f, 4.0f, 14.0f }, 12.0f, 16.0f, 0u, 20.0f, 500.0, placement));
    NWB::UiWidgetTests::ExpectRect(placement.viewport, { 18.0f, 26.0f, 188.0f, 100.0f });
    NWB::UiWidgetTests::ExpectRect(placement.contentClip, placement.viewport);
    NWB::UiWidgetTests::ExpectRect(placement.track, {});
    NWB::UiWidgetTests::ExpectRect(placement.thumb, {});
    EXPECT_DOUBLE_EQ(placement.contentHeight, 0.0);
    EXPECT_DOUBLE_EQ(placement.offset, 0.0);
    EXPECT_DOUBLE_EQ(placement.maxOffset, 0.0);
    EXPECT_EQ(placement.firstRow, 0u);
    EXPECT_EQ(placement.endRow, 0u);
    EXPECT_EQ(placement.rowCount, 0u);
    EXPECT_FALSE(placement.scrollbarVisible);
}

TEST(UiScrollLayoutTests, ContentExactlyFittingDoesNotReserveScrollbarWidth){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 5u, 20.0f, 10.0, placement));
    NWB::UiWidgetTests::ExpectRect(placement.viewport, bounds);
    EXPECT_FALSE(placement.scrollbarVisible);
    EXPECT_DOUBLE_EQ(placement.maxOffset, 0.0);
    EXPECT_DOUBLE_EQ(placement.offset, 0.0);
    EXPECT_EQ(placement.firstRow, 0u);
    EXPECT_EQ(placement.endRow, 5u);
}

TEST(UiScrollLayoutTests, ThumbEndpointsAndMiddleFollowClampedOffset){
    const Rect bounds{ 0.0f, 0.0f, 200.0f, 100.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 20u, 20.0f, 150.0, placement));
    NWB::UiWidgetTests::ExpectRect(placement.thumb, { 188.0f, 37.5f, 12.0f, 25.0f });
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 20u, 20.0f, 999.0, placement));
    EXPECT_DOUBLE_EQ(placement.offset, 300.0);
    NWB::UiWidgetTests::ExpectRect(placement.thumb, { 188.0f, 75.0f, 12.0f, 25.0f });
    EXPECT_EQ(placement.firstRow, 15u);
    EXPECT_EQ(placement.endRow, 20u);
}

TEST(UiScrollLayoutTests, MinimumThumbNeverExceedsItsTrack){
    const Rect bounds{ 0.0f, 0.0f, 200.0f, 20.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 80.0f, 100000u, 32.0f, 1000.0, placement));
    NWB::UiWidgetTests::ExpectRect(placement.thumb, placement.track);
    EXPECT_DOUBLE_EQ(placement.maxOffset, 3199980.0);
}

TEST(UiScrollLayoutTests, ZeroScrollbarWidthLeavesScrollableContentAtFullWidth){
    const Rect bounds{ 0.0f, 0.0f, 100.0f, 100.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 0.0f, 16.0f, 100u, 10.0f, 150.0, placement));
    NWB::UiWidgetTests::ExpectRect(placement.viewport, bounds);
    NWB::UiWidgetTests::ExpectRect(placement.track, {});
    EXPECT_FALSE(placement.scrollbarVisible);
    EXPECT_DOUBLE_EQ(placement.maxOffset, 900.0);
    EXPECT_EQ(placement.firstRow, 15u);
    EXPECT_EQ(placement.endRow, 25u);
}

TEST(UiScrollLayoutTests, ExcessiveScrollbarWidthShrinksToAvailableWidthAndCullsAllRows){
    const Rect bounds{ 10.0f, 20.0f, 8.0f, 100.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 80.0f, 16.0f, 20u, 20.0f, 0.0, placement));
    NWB::UiWidgetTests::ExpectRect(placement.viewport, { 10.0f, 20.0f, 0.0f, 100.0f });
    NWB::UiWidgetTests::ExpectRect(placement.track, bounds);
    EXPECT_EQ(placement.firstRow, 0u);
    EXPECT_EQ(placement.endRow, 0u);
}

TEST(UiScrollLayoutTests, HundredThousandRowsOnlyExposeTheCurrentVisibleInterval){
    const Rect bounds{ 10.0f, 20.0f, 240.0f, 128.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 100000u, 32.0f, 1600000.0, placement));
    EXPECT_DOUBLE_EQ(placement.contentHeight, 3200000.0);
    EXPECT_DOUBLE_EQ(placement.maxOffset, 3199872.0);
    EXPECT_EQ(placement.firstRow, 50000u);
    EXPECT_EQ(placement.endRow, 50004u);
    Rect first;
    ASSERT_TRUE(ScrollLayout::RowBounds(50000u, placement, 32.0f, first));
    NWB::UiWidgetTests::ExpectRect(first, { 10.0f, 20.0f, 228.0f, 32.0f });
}

TEST(UiScrollLayoutTests, ExtremeFiniteOffsetClampsBeforeProducingVisibleFloatGeometry){
    const Rect bounds{ 10.0f, 20.0f, 240.0f, 128.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 100000u, 32.0f,
        Limit<f64>::s_Max, placement));
    EXPECT_DOUBLE_EQ(placement.offset, 3199872.0);
    EXPECT_EQ(placement.firstRow, 99996u);
    EXPECT_EQ(placement.endRow, 100000u);
    Rect last;
    ASSERT_TRUE(ScrollLayout::RowBounds(99999u, placement, 32.0f, last));
    NWB::UiWidgetTests::ExpectRect(last, { 10.0f, 116.0f, 228.0f, 32.0f });
}

TEST(UiScrollLayoutTests, FractionalOffsetsIncludePartialRowsAtBothEdges){
    const Rect bounds{ 0.0f, 20.0f, 200.0f, 100.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 100u, 12.5f, 6.25, placement));
    EXPECT_EQ(placement.firstRow, 0u);
    EXPECT_EQ(placement.endRow, 9u);
    Rect first;
    ASSERT_TRUE(ScrollLayout::RowBounds(0u, placement, 12.5f, first));
    NWB::UiWidgetTests::ExpectRect(first, { 0.0f, 13.75f, 188.0f, 12.5f });
}

TEST(UiScrollLayoutTests, ExclusiveBottomBoundaryDoesNotDeclareTheTouchingNextRow){
    const Rect bounds{ 0.0f, 0.0f, 200.0f, 60.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 100u, 20.0f, 40.0, placement));
    EXPECT_EQ(placement.firstRow, 2u);
    EXPECT_EQ(placement.endRow, 5u);
}

TEST(UiScrollLayoutTests, InheritedVerticalClipCullsRowsWithoutChangingLogicalRangeOrThumb){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, { 10.0f, 60.0f, 200.0f, 20.0f }, {},
        12.0f, 16.0f, 20u, 20.0f, 40.0, placement));
    NWB::UiWidgetTests::ExpectRect(placement.viewport, { 10.0f, 20.0f, 188.0f, 100.0f });
    NWB::UiWidgetTests::ExpectRect(placement.contentClip, { 10.0f, 60.0f, 188.0f, 20.0f });
    EXPECT_DOUBLE_EQ(placement.maxOffset, 300.0);
    EXPECT_FLOAT_EQ(placement.thumb.height, 25.0f);
    EXPECT_EQ(placement.firstRow, 4u);
    EXPECT_EQ(placement.endRow, 5u);
}

TEST(UiScrollLayoutTests, InheritedHorizontalClipKeepsRowOriginAndWidthStable){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, { 50.0f, 20.0f, 30.0f, 100.0f }, {},
        12.0f, 16.0f, 20u, 20.0f, 40.0, placement));
    NWB::UiWidgetTests::ExpectRect(placement.contentClip, { 50.0f, 20.0f, 30.0f, 100.0f });
    Rect first;
    ASSERT_TRUE(ScrollLayout::RowBounds(2u, placement, 20.0f, first));
    NWB::UiWidgetTests::ExpectRect(first, { 10.0f, 20.0f, 188.0f, 20.0f });
}

TEST(UiScrollLayoutTests, DisjointAndZeroAreaClipsHaveNoVisibleRowsButKeepScrolling){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    const Rect clips[] = { { 300.0f, 20.0f, 100.0f, 100.0f }, { 10.0f, 200.0f, 100.0f, 100.0f },
        { 10.0f, 20.0f, 0.0f, 100.0f }, { 10.0f, 20.0f, 100.0f, 0.0f } };
    for(const Rect& clip : clips){
        ScrollPlacement placement;
        ASSERT_TRUE(ScrollLayout::Calculate(bounds, clip, {}, 12.0f, 16.0f, 20u, 20.0f, 40.0, placement));
        EXPECT_EQ(placement.firstRow, 0u);
        EXPECT_EQ(placement.endRow, 0u);
        EXPECT_DOUBLE_EQ(placement.offset, 40.0);
        EXPECT_DOUBLE_EQ(placement.maxOffset, 300.0);
        EXPECT_TRUE(placement.scrollbarVisible);
    }
}

TEST(UiScrollLayoutTests, PaddingCanCollapseLogicalViewportWithoutNegativeSizes){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, { 300.0f, 200.0f, 300.0f, 200.0f },
        12.0f, 16.0f, 20u, 20.0f, 40.0, placement));
    NWB::UiWidgetTests::ExpectRect(placement.viewport, { 210.0f, 120.0f, 0.0f, 0.0f });
    EXPECT_EQ(placement.firstRow, 0u);
    EXPECT_EQ(placement.endRow, 0u);
    EXPECT_DOUBLE_EQ(placement.maxOffset, 400.0);
    EXPECT_FALSE(placement.scrollbarVisible);
}

TEST(UiScrollLayoutTests, ZeroHeightBoundsCanKeepAStoredOffsetUntilRestored){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 0.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 20u, 20.0f, 300.0, placement));
    EXPECT_DOUBLE_EQ(placement.offset, 300.0);
    EXPECT_DOUBLE_EQ(placement.maxOffset, 400.0);
    EXPECT_EQ(placement.firstRow, 0u);
    EXPECT_EQ(placement.endRow, 0u);
}

TEST(UiScrollLayoutTests, RowBoundsRejectsCulledAndInvalidRowsAtomically){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    ScrollPlacement placement;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 20u, 20.0f, 40.0, placement));
    const Rect previous{ 1.0f, 2.0f, 3.0f, 4.0f };
    Rect result = previous;
    EXPECT_FALSE(ScrollLayout::RowBounds(1u, placement, 20.0f, result));
    NWB::UiWidgetTests::ExpectRect(result, previous);
    EXPECT_FALSE(ScrollLayout::RowBounds(7u, placement, 20.0f, result));
    NWB::UiWidgetTests::ExpectRect(result, previous);
    EXPECT_FALSE(ScrollLayout::RowBounds(Limit<u64>::s_Max, placement, 20.0f, result));
    NWB::UiWidgetTests::ExpectRect(result, previous);
    EXPECT_FALSE(ScrollLayout::RowBounds(2u, placement, Limit<f32>::s_QuietNaN, result));
    NWB::UiWidgetTests::ExpectRect(result, previous);
    placement.offset = Limit<f64>::s_Infinity;
    EXPECT_FALSE(ScrollLayout::RowBounds(2u, placement, 20.0f, result));
    NWB::UiWidgetTests::ExpectRect(result, previous);
}

TEST(UiScrollLayoutTests, LogicalInputsRemainInvariantUnderAsymmetricDisplayScaling){
    const Rect bounds{ 24.0f, 38.0f, 240.0f, 160.0f };
    const DisplayMetrics displays[] = { { 800.0f, 600.0f, 1.0f, 1.0f }, { 800.0f, 600.0f, 2.5f, 1.25f } };
    ScrollPlacement reference;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, { 0.0f, 0.0f, displays[0].logicalWidth, displays[0].logicalHeight },
        { 4.0f, 8.0f, 12.0f, 16.0f }, 10.0f, 20.0f, 100000u, 24.0f, 24005.5, reference));
    ScrollPlacement scaled;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, { 0.0f, 0.0f, displays[1].logicalWidth, displays[1].logicalHeight },
        { 4.0f, 8.0f, 12.0f, 16.0f }, 10.0f, 20.0f, 100000u, 24.0f, 24005.5, scaled));
    ExpectPlacement(scaled, reference);
}

TEST(UiScrollLayoutTests, ExtentsBeyondDoubleViewportPrecisionFailInsteadOfLosingTheFinalPage){
    const Rect bounds{ 0.0f, 0.0f, 200.0f, 100.0f };
    ScrollPlacement previous;
    ASSERT_TRUE(ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 20u, 20.0f, 40.0, previous));
    ScrollPlacement result = previous;
    EXPECT_FALSE(ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, Limit<u64>::s_Max,
        Limit<f32>::s_Max, Limit<f64>::s_Max, result));
    ExpectPlacement(result, previous);
}

TEST(UiScrollLayoutTests, InvalidLayoutInputsLeaveThePreviousPlacementUntouched){
    const Rect validBounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    const Insets validPadding{ 4.0f, 8.0f, 12.0f, 16.0f };
    ScrollPlacement previous;
    ASSERT_TRUE(ScrollLayout::Calculate(validBounds, validBounds, validPadding, 12.0f, 16.0f, 20u, 20.0f,
        40.0, previous));
    const auto reject = [&previous](const Rect& bounds, const Rect& clip, const Insets& padding,
        const f32 width, const f32 minimum, const f32 height, const f64 offset){
        ScrollPlacement result = previous;
        EXPECT_FALSE(ScrollLayout::Calculate(bounds, clip, padding, width, minimum, 20u, height, offset, result));
        ExpectPlacement(result, previous);
    };
    Rect bounds = validBounds;
    bounds.x = Limit<f32>::s_QuietNaN;
    reject(bounds, validBounds, validPadding, 12.0f, 16.0f, 20.0f, 40.0);
    bounds = validBounds;
    bounds.width = -1.0f;
    reject(bounds, validBounds, validPadding, 12.0f, 16.0f, 20.0f, 40.0);
    bounds = { Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 100.0f };
    reject(bounds, validBounds, validPadding, 12.0f, 16.0f, 20.0f, 40.0);
    Rect clip = validBounds;
    clip.height = Limit<f32>::s_Infinity;
    reject(validBounds, clip, validPadding, 12.0f, 16.0f, 20.0f, 40.0);
    clip = validBounds;
    clip.y = -Limit<f32>::s_Infinity;
    reject(validBounds, clip, validPadding, 12.0f, 16.0f, 20.0f, 40.0);
    Insets padding = validPadding;
    padding.left = -1.0f;
    reject(validBounds, validBounds, padding, 12.0f, 16.0f, 20.0f, 40.0);
    padding = validPadding;
    padding.bottom = Limit<f32>::s_QuietNaN;
    reject(validBounds, validBounds, padding, 12.0f, 16.0f, 20.0f, 40.0);
    reject(validBounds, validBounds, validPadding, -1.0f, 16.0f, 20.0f, 40.0);
    reject(validBounds, validBounds, validPadding, Limit<f32>::s_Infinity, 16.0f, 20.0f, 40.0);
    reject(validBounds, validBounds, validPadding, 12.0f, -1.0f, 20.0f, 40.0);
    reject(validBounds, validBounds, validPadding, 12.0f, Limit<f32>::s_QuietNaN, 20.0f, 40.0);
    reject(validBounds, validBounds, validPadding, 12.0f, 16.0f, 0.0f, 40.0);
    reject(validBounds, validBounds, validPadding, 12.0f, 16.0f, Limit<f32>::s_Infinity, 40.0);
    reject(validBounds, validBounds, validPadding, 12.0f, 16.0f, 20.0f, -1.0);
    reject(validBounds, validBounds, validPadding, 12.0f, 16.0f, 20.0f, Limit<f64>::s_QuietNaN);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiScrollStateTests, IndependentAndRecreatedStatesHaveUniqueNonzeroIdentity){
    u64 retiredIdentity = 0u;
    {
        ScrollState retired;
        retiredIdentity = retired.instanceGeneration();
        EXPECT_NE(retiredIdentity, 0u);
    }
    ScrollState first;
    ScrollState second;
    EXPECT_NE(first.instanceGeneration(), retiredIdentity);
    EXPECT_NE(second.instanceGeneration(), retiredIdentity);
    EXPECT_NE(first.instanceGeneration(), second.instanceGeneration());
    EXPECT_DOUBLE_EQ(first.offset(), 0.0);
}

TEST(UiScrollStateTests, OffsetAcceptsFiniteNonnegativeValuesAndRejectsInvalidAtomically){
    ScrollState state;
    const u64 identity = state.instanceGeneration();
    ASSERT_TRUE(state.setOffset(45.25));
    EXPECT_DOUBLE_EQ(state.offset(), 45.25);
    EXPECT_FALSE(state.setOffset(-1.0));
    EXPECT_FALSE(state.setOffset(Limit<f64>::s_QuietNaN));
    EXPECT_FALSE(state.setOffset(Limit<f64>::s_Infinity));
    EXPECT_DOUBLE_EQ(state.offset(), 45.25);
    ASSERT_TRUE(state.setOffset(Limit<f64>::s_Max));
    EXPECT_DOUBLE_EQ(state.offset(), Limit<f64>::s_Max);
    ASSERT_TRUE(state.setOffset(-0.0));
    EXPECT_DOUBLE_EQ(state.offset(), 0.0);
    EXPECT_EQ(state.instanceGeneration(), identity);
}

TEST(UiScrollStateTests, EnsureVisibleKeepsContainedRowsAndMovesOnlyAsFarAsNeeded){
    ScrollState state;
    ASSERT_TRUE(state.setOffset(100.0));
    ASSERT_TRUE(state.ensureVisible(110.0, 130.0, 80.0));
    EXPECT_DOUBLE_EQ(state.offset(), 100.0);
    ASSERT_TRUE(state.ensureVisible(180.0, 200.0, 80.0));
    EXPECT_DOUBLE_EQ(state.offset(), 120.0);
    ASSERT_TRUE(state.ensureVisible(60.0, 80.0, 80.0));
    EXPECT_DOUBLE_EQ(state.offset(), 60.0);
}

TEST(UiScrollStateTests, OversizedRowsAlignTheirStartAndZeroViewportCanStoreTheirStart){
    ScrollState state;
    ASSERT_TRUE(state.setOffset(100.0));
    ASSERT_TRUE(state.ensureVisible(120.0, 320.0, 80.0));
    EXPECT_DOUBLE_EQ(state.offset(), 120.0);
    ASSERT_TRUE(state.ensureVisible(300.0, 320.0, 0.0));
    EXPECT_DOUBLE_EQ(state.offset(), 300.0);
}

TEST(UiScrollStateTests, EnsureVisibleAvoidsOffsetPlusViewportOverflow){
    ScrollState state;
    const f64 large = Limit<f64>::s_Max * 0.75;
    ASSERT_TRUE(state.setOffset(large));
    ASSERT_TRUE(state.ensureVisible(large, Limit<f64>::s_Max, large));
    EXPECT_DOUBLE_EQ(state.offset(), large);
}

TEST(UiScrollStateTests, InvalidEnsureVisibleRequestsLeaveTheOffsetUntouched){
    ScrollState state;
    ASSERT_TRUE(state.setOffset(75.0));
    EXPECT_FALSE(state.ensureVisible(-1.0, 40.0, 100.0));
    EXPECT_FALSE(state.ensureVisible(60.0, 40.0, 100.0));
    EXPECT_FALSE(state.ensureVisible(0.0, Limit<f64>::s_Infinity, 100.0));
    EXPECT_FALSE(state.ensureVisible(0.0, 40.0, -1.0));
    EXPECT_FALSE(state.ensureVisible(0.0, 40.0, Limit<f64>::s_QuietNaN));
    EXPECT_DOUBLE_EQ(state.offset(), 75.0);
}

TEST(UiScrollStateTests, ScrollByClampsBothEndsAndAppliesDeltaAfterContentShrink){
    ScrollState state;
    ASSERT_TRUE(state.setOffset(50.0));
    ASSERT_TRUE(state.scrollBy(25.5, 400.0, 100.0));
    EXPECT_DOUBLE_EQ(state.offset(), 75.5);
    ASSERT_TRUE(state.scrollBy(-40.5, 400.0, 100.0));
    EXPECT_DOUBLE_EQ(state.offset(), 35.0);
    ASSERT_TRUE(state.scrollBy(-1000.0, 400.0, 100.0));
    EXPECT_DOUBLE_EQ(state.offset(), 0.0);
    ASSERT_TRUE(state.scrollBy(1000.0, 400.0, 100.0));
    EXPECT_DOUBLE_EQ(state.offset(), 300.0);
    ASSERT_TRUE(state.scrollBy(-20.0, 200.0, 100.0));
    EXPECT_DOUBLE_EQ(state.offset(), 80.0);
}

TEST(UiScrollStateTests, ExtremeDeltaAndOffsetDoNotOverflowWhenClamping){
    ScrollState state;
    ASSERT_TRUE(state.setOffset(Limit<f64>::s_Max));
    ASSERT_TRUE(state.scrollBy(Limit<f64>::s_Max, 400.0, 100.0));
    EXPECT_DOUBLE_EQ(state.offset(), 300.0);
    ASSERT_TRUE(state.scrollBy(-Limit<f64>::s_Max, 400.0, 100.0));
    EXPECT_DOUBLE_EQ(state.offset(), 0.0);
    ASSERT_TRUE(state.scrollBy(Limit<f64>::s_Max, Limit<f64>::s_Max, 0.0));
    EXPECT_DOUBLE_EQ(state.offset(), Limit<f64>::s_Max);
}

TEST(UiScrollStateTests, ClampSupportsEmptyContentAndCollapsedViewports){
    ScrollState state;
    ASSERT_TRUE(state.setOffset(350.0));
    ASSERT_TRUE(state.clamp(400.0, 100.0));
    EXPECT_DOUBLE_EQ(state.offset(), 300.0);
    ASSERT_TRUE(state.clamp(400.0, 0.0));
    EXPECT_DOUBLE_EQ(state.offset(), 300.0);
    ASSERT_TRUE(state.clamp(50.0, 100.0));
    EXPECT_DOUBLE_EQ(state.offset(), 0.0);
    ASSERT_TRUE(state.setOffset(20.0));
    ASSERT_TRUE(state.clamp(0.0, 0.0));
    EXPECT_DOUBLE_EQ(state.offset(), 0.0);
}

TEST(UiScrollStateTests, InvalidScrollAndClampRangesLeaveTheOffsetUntouched){
    ScrollState state;
    ASSERT_TRUE(state.setOffset(75.0));
    EXPECT_FALSE(state.scrollBy(Limit<f64>::s_QuietNaN, 400.0, 100.0));
    EXPECT_FALSE(state.scrollBy(10.0, -1.0, 100.0));
    EXPECT_FALSE(state.scrollBy(10.0, 400.0, -1.0));
    EXPECT_FALSE(state.scrollBy(10.0, Limit<f64>::s_Infinity, 100.0));
    EXPECT_FALSE(state.clamp(-1.0, 100.0));
    EXPECT_FALSE(state.clamp(400.0, Limit<f64>::s_QuietNaN));
    EXPECT_DOUBLE_EQ(state.offset(), 75.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

