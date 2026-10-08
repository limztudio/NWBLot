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



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiScrollLayoutTests, EmptyContentUsesFullPaddedViewportAndEmptyVisibleRange){
    ScrollPlacement placement;
    const auto placementResult = ScrollLayout::Calculate(
        { 10.0f, 20.0f, 200.0f, 120.0f },
        { 0.0f, 0.0f, 400.0f, 300.0f },
        { 8.0f, 6.0f, 4.0f, 14.0f },
        12.0f,
        16.0f,
        0u,
        20.0f,
        500.0
    );
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
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
    const auto placementResult = ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 5u, 20.0f, 10.0);
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    NWB::UiWidgetTests::ExpectRect(placement.viewport, bounds);
    EXPECT_FALSE(placement.scrollbarVisible);
    EXPECT_DOUBLE_EQ(placement.maxOffset, 0.0);
    EXPECT_DOUBLE_EQ(placement.offset, 0.0);
    EXPECT_EQ(placement.firstRow, 0u);
    EXPECT_EQ(placement.endRow, 5u);
}

TEST(UiScrollLayoutTests, OverscrollClampsThumbToTheFinalVisibleRow){
    const Rect bounds{ 0.0f, 0.0f, 200.0f, 100.0f };
    ScrollPlacement placement;
    const auto placementResult = ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 20u, 20.0f, 999.0);
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    EXPECT_DOUBLE_EQ(placement.offset, 300.0);
    NWB::UiWidgetTests::ExpectRect(placement.thumb, { 188.0f, 75.0f, 12.0f, 25.0f });
    EXPECT_EQ(placement.firstRow, 15u);
    EXPECT_EQ(placement.endRow, 20u);
}

TEST(UiScrollLayoutTests, MinimumThumbNeverExceedsItsTrack){
    const Rect bounds{ 0.0f, 0.0f, 200.0f, 20.0f };
    ScrollPlacement placement;
    const auto placementResult = ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 80.0f, 100000u, 32.0f, 1000.0);
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    NWB::UiWidgetTests::ExpectRect(placement.thumb, placement.track);
    EXPECT_DOUBLE_EQ(placement.maxOffset, 3199980.0);
}

TEST(UiScrollLayoutTests, ZeroScrollbarWidthLeavesScrollableContentAtFullWidth){
    const Rect bounds{ 0.0f, 0.0f, 100.0f, 100.0f };
    ScrollPlacement placement;
    const auto placementResult = ScrollLayout::Calculate(bounds, bounds, {}, 0.0f, 16.0f, 100u, 10.0f, 150.0);
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
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
    const auto placementResult = ScrollLayout::Calculate(bounds, bounds, {}, 80.0f, 16.0f, 20u, 20.0f, 0.0);
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    NWB::UiWidgetTests::ExpectRect(placement.viewport, { 10.0f, 20.0f, 0.0f, 100.0f });
    NWB::UiWidgetTests::ExpectRect(placement.track, bounds);
    EXPECT_EQ(placement.firstRow, 0u);
    EXPECT_EQ(placement.endRow, 0u);
}

TEST(UiScrollLayoutTests, HundredThousandRowsOnlyExposeTheCurrentVisibleInterval){
    const Rect bounds{ 10.0f, 20.0f, 240.0f, 128.0f };
    ScrollPlacement placement;
    const auto placementResult = ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 100000u, 32.0f, 1600000.0);
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    EXPECT_EQ(placement.firstRow, 50000u);
    EXPECT_EQ(placement.endRow, 50004u);
}

TEST(UiScrollLayoutTests, ExtremeFiniteOffsetClampsBeforeProducingVisibleFloatGeometry){
    const Rect bounds{ 10.0f, 20.0f, 240.0f, 128.0f };
    ScrollPlacement placement;
    const auto placementResult = ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 100000u, 32.0f, Limit<f64>::s_Max);
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    EXPECT_DOUBLE_EQ(placement.offset, 3199872.0);
    EXPECT_EQ(placement.firstRow, 99996u);
    EXPECT_EQ(placement.endRow, 100000u);
    Rect last;
    const auto lastResult = ScrollLayout::RowBounds(99999u, placement, 32.0f);
    ASSERT_TRUE(lastResult);
    last = *lastResult;
    NWB::UiWidgetTests::ExpectRect(last, { 10.0f, 116.0f, 228.0f, 32.0f });
}

TEST(UiScrollLayoutTests, FractionalOffsetsIncludePartialRowsAtBothEdges){
    const Rect bounds{ 0.0f, 20.0f, 200.0f, 100.0f };
    ScrollPlacement placement;
    const auto placementResult = ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 100u, 12.5f, 6.25);
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    EXPECT_EQ(placement.firstRow, 0u);
    EXPECT_EQ(placement.endRow, 9u);
    Rect first;
    const auto firstResult = ScrollLayout::RowBounds(0u, placement, 12.5f);
    ASSERT_TRUE(firstResult);
    first = *firstResult;
    NWB::UiWidgetTests::ExpectRect(first, { 0.0f, 13.75f, 188.0f, 12.5f });
}

TEST(UiScrollLayoutTests, ExclusiveBottomBoundaryDoesNotDeclareTheTouchingNextRow){
    const Rect bounds{ 0.0f, 0.0f, 200.0f, 60.0f };
    ScrollPlacement placement;
    const auto placementResult = ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 100u, 20.0f, 40.0);
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    EXPECT_EQ(placement.firstRow, 2u);
    EXPECT_EQ(placement.endRow, 5u);
}

TEST(UiScrollLayoutTests, InheritedVerticalClipCullsRowsWithoutChangingLogicalRangeOrThumb){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    ScrollPlacement placement;
    const auto placementResult = ScrollLayout::Calculate(
        bounds,
        { 10.0f, 60.0f, 200.0f, 20.0f },
        {},
        12.0f,
        16.0f,
        20u,
        20.0f,
        40.0
    );
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
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
    const auto placementResult = ScrollLayout::Calculate(
        bounds,
        { 50.0f, 20.0f, 30.0f, 100.0f },
        {},
        12.0f,
        16.0f,
        20u,
        20.0f,
        40.0
    );
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    NWB::UiWidgetTests::ExpectRect(placement.contentClip, { 50.0f, 20.0f, 30.0f, 100.0f });
    Rect first;
    const auto firstResult = ScrollLayout::RowBounds(2u, placement, 20.0f);
    ASSERT_TRUE(firstResult);
    first = *firstResult;
    NWB::UiWidgetTests::ExpectRect(first, { 10.0f, 20.0f, 188.0f, 20.0f });
}

TEST(UiScrollLayoutTests, DisjointAndZeroAreaClipsHaveNoVisibleRowsButKeepScrolling){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    const Rect clips[] = { { 300.0f, 20.0f, 100.0f, 100.0f }, { 10.0f, 200.0f, 100.0f, 100.0f },
        { 10.0f, 20.0f, 0.0f, 100.0f }, { 10.0f, 20.0f, 100.0f, 0.0f } };
    for(const Rect& clip : clips){
        ScrollPlacement placement;
        const auto placementResult = ScrollLayout::Calculate(bounds, clip, {}, 12.0f, 16.0f, 20u, 20.0f, 40.0);
        ASSERT_TRUE(placementResult);
        placement = *placementResult;
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
    const auto placementResult = ScrollLayout::Calculate(
        bounds,
        bounds,
        { 300.0f, 200.0f, 300.0f, 200.0f },
        12.0f,
        16.0f,
        20u,
        20.0f,
        40.0
    );
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    NWB::UiWidgetTests::ExpectRect(placement.viewport, { 210.0f, 120.0f, 0.0f, 0.0f });
    EXPECT_EQ(placement.firstRow, 0u);
    EXPECT_EQ(placement.endRow, 0u);
    EXPECT_DOUBLE_EQ(placement.maxOffset, 400.0);
    EXPECT_FALSE(placement.scrollbarVisible);
}

TEST(UiScrollLayoutTests, ZeroHeightBoundsCanKeepAStoredOffsetUntilRestored){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 0.0f };
    ScrollPlacement placement;
    const auto placementResult = ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 20u, 20.0f, 300.0);
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    EXPECT_DOUBLE_EQ(placement.offset, 300.0);
    EXPECT_DOUBLE_EQ(placement.maxOffset, 400.0);
    EXPECT_EQ(placement.firstRow, 0u);
    EXPECT_EQ(placement.endRow, 0u);
}

TEST(UiScrollLayoutTests, RowBoundsRejectsCulledAndInvalidRows){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    ScrollPlacement placement;
    const auto placementResult = ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 20u, 20.0f, 40.0);
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    EXPECT_FALSE(ScrollLayout::RowBounds(1u, placement, 20.0f));
    EXPECT_FALSE(ScrollLayout::RowBounds(7u, placement, 20.0f));
    EXPECT_FALSE(ScrollLayout::RowBounds(Limit<u64>::s_Max, placement, 20.0f));
    EXPECT_FALSE(ScrollLayout::RowBounds(2u, placement, Limit<f32>::s_QuietNaN));
    placement.offset = Limit<f64>::s_Infinity;
    EXPECT_FALSE(ScrollLayout::RowBounds(2u, placement, 20.0f));
}

TEST(UiScrollLayoutTests, ExtentsBeyondDoubleViewportPrecisionFailInsteadOfLosingTheFinalPage){
    const Rect bounds{ 0.0f, 0.0f, 200.0f, 100.0f };
    const auto previousResult = ScrollLayout::Calculate(bounds, bounds, {}, 12.0f, 16.0f, 20u, 20.0f, 40.0);
    ASSERT_TRUE(previousResult);
    EXPECT_FALSE(ScrollLayout::Calculate(
        bounds,
        bounds,
        {},
        12.0f,
        16.0f,
        Limit<u64>::s_Max,
        Limit<f32>::s_Max,
        Limit<f64>::s_Max
    ));
}

TEST(UiScrollLayoutTests, InvalidLayoutInputsRejectPlacement){
    const Rect validBounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    const Insets validPadding{ 4.0f, 8.0f, 12.0f, 16.0f };
    const auto previousResult = ScrollLayout::Calculate(validBounds, validBounds, validPadding, 12.0f, 16.0f, 20u, 20.0f, 40.0);
    ASSERT_TRUE(previousResult);
    const auto reject = [](const Rect& bounds, const Rect& clip, const Insets& padding,
        const f32 width, const f32 minimum, const f32 height, const f64 offset
    ){
        EXPECT_FALSE(ScrollLayout::Calculate(bounds, clip, padding, width, minimum, 20u, height, offset));
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

