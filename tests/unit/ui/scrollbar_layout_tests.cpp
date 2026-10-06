// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/scrollbar.h>

#include <global/simplemath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_scrollbar_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;

static void ExpectBar(const ScrollbarPlacement& actual, const ScrollbarPlacement& expected){
    UiWidgetTests::ExpectRect(actual.track, expected.track);
    UiWidgetTests::ExpectRect(actual.thumb, expected.thumb);
    EXPECT_DOUBLE_EQ(actual.contentExtent, expected.contentExtent);
    EXPECT_DOUBLE_EQ(actual.viewportExtent, expected.viewportExtent);
    EXPECT_DOUBLE_EQ(actual.maximum, expected.maximum);
    EXPECT_DOUBLE_EQ(actual.offset, expected.offset);
    EXPECT_EQ(actual.visible, expected.visible);
}

static void ExpectPlacement(const ScrollViewportPlacement& actual, const ScrollViewportPlacement& expected){
    UiWidgetTests::ExpectRect(actual.viewport, expected.viewport);
    UiWidgetTests::ExpectRect(actual.contentClip, expected.contentClip);
    UiWidgetTests::ExpectRect(actual.corner, expected.corner);
    ExpectBar(actual.horizontal, expected.horizontal);
    ExpectBar(actual.vertical, expected.vertical);
}

static void ExpectReservedGeometry(const ScrollViewportPlacement& actual, const ScrollViewportPlacement& expected){
    UiWidgetTests::ExpectRect(actual.viewport, expected.viewport);
    UiWidgetTests::ExpectRect(actual.contentClip, expected.contentClip);
    UiWidgetTests::ExpectRect(actual.corner, expected.corner);
    UiWidgetTests::ExpectRect(actual.horizontal.track, expected.horizontal.track);
    UiWidgetTests::ExpectRect(actual.vertical.track, expected.vertical.track);
    EXPECT_FLOAT_EQ(
        actual.horizontal.thumb.width, expected.horizontal.thumb.width);
        EXPECT_FLOAT_EQ(actual.horizontal.thumb.height, expected.horizontal.thumb.height
    );
    EXPECT_FLOAT_EQ(
        actual.vertical.thumb.width, expected.vertical.thumb.width);
        EXPECT_FLOAT_EQ(actual.vertical.thumb.height, expected.vertical.thumb.height
    );
    EXPECT_DOUBLE_EQ(actual.horizontal.contentExtent, expected.horizontal.contentExtent);
    EXPECT_DOUBLE_EQ(actual.vertical.contentExtent, expected.vertical.contentExtent);
    EXPECT_DOUBLE_EQ(actual.horizontal.viewportExtent, expected.horizontal.viewportExtent);
    EXPECT_DOUBLE_EQ(actual.vertical.viewportExtent, expected.vertical.viewportExtent);
    EXPECT_DOUBLE_EQ(actual.horizontal.maximum, expected.horizontal.maximum);
    EXPECT_DOUBLE_EQ(actual.vertical.maximum, expected.vertical.maximum);
    EXPECT_EQ(actual.horizontal.visible, expected.horizontal.visible);
    EXPECT_EQ(actual.vertical.visible, expected.vertical.visible);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiScrollbarLayoutTests, FittingContentKeepsPaddedViewportAndClampsStoredOffsets){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 120.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, bounds, { 8.0f, 6.0f, 4.0f, 14.0f },
        { 100.0f, 80.0f }, 2.0f, { 40.0f, 60.0f }, 12.0f, 16.0f, placement
    ));
    UiWidgetTests::ExpectRect(placement.viewport, { 18.0f, 26.0f, 188.0f, 100.0f });
    UiWidgetTests::ExpectRect(placement.contentClip, placement.viewport);
    UiWidgetTests::ExpectRect(placement.corner, {});
    UiWidgetTests::ExpectRect(placement.horizontal.track, {});
    UiWidgetTests::ExpectRect(placement.vertical.track, {});
    EXPECT_DOUBLE_EQ(placement.horizontal.offset, 0.0);
    EXPECT_DOUBLE_EQ(placement.vertical.offset, 0.0);
    EXPECT_FALSE(placement.horizontal.visible);
    EXPECT_FALSE(placement.vertical.visible);
}

TEST(UiScrollbarLayoutTests, ExactContentFitDoesNotReserveEitherBar){
    const Rect bounds{ 0.0f, 0.0f, 100.0f, 80.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 99.0f, 80.0f }, 1.0f, {}, 12.0f, 16.0f, placement));
    UiWidgetTests::ExpectRect(placement.viewport, bounds);
    EXPECT_FALSE(placement.horizontal.visible);
    EXPECT_FALSE(placement.vertical.visible);
    EXPECT_DOUBLE_EQ(placement.horizontal.maximum, 0.0);
    EXPECT_DOUBLE_EQ(placement.vertical.maximum, 0.0);
}

TEST(UiScrollbarLayoutTests, HorizontalExtentIncludesCaretWidth){
    const Rect bounds{ 0.0f, 0.0f, 100.0f, 80.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 100.0f, 40.0f }, 1.0f, {}, 12.0f, 16.0f, placement));
    UiWidgetTests::ExpectRect(placement.viewport, { 0.0f, 0.0f, 100.0f, 68.0f });
    EXPECT_TRUE(placement.horizontal.visible);
    EXPECT_FALSE(placement.vertical.visible);
    EXPECT_DOUBLE_EQ(placement.horizontal.maximum, 1.0);
}

TEST(UiScrollbarLayoutTests, VerticalReservationCanRequireHorizontalBar){
    const Rect bounds{ 0.0f, 0.0f, 100.0f, 100.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 89.0f, 200.0f }, 1.0f, {}, 12.0f, 16.0f, placement));
    UiWidgetTests::ExpectRect(placement.viewport, { 0.0f, 0.0f, 88.0f, 88.0f });
    UiWidgetTests::ExpectRect(placement.horizontal.track, { 0.0f, 88.0f, 88.0f, 12.0f });
    UiWidgetTests::ExpectRect(placement.vertical.track, { 88.0f, 0.0f, 12.0f, 88.0f });
    UiWidgetTests::ExpectRect(placement.corner, { 88.0f, 88.0f, 12.0f, 12.0f });
    EXPECT_TRUE(placement.horizontal.visible);
    EXPECT_TRUE(placement.vertical.visible);
    EXPECT_DOUBLE_EQ(placement.horizontal.maximum, 2.0);
    EXPECT_DOUBLE_EQ(placement.vertical.maximum, 112.0);
}

TEST(UiScrollbarLayoutTests, HorizontalReservationCanRequireVerticalBar){
    const Rect bounds{ 0.0f, 0.0f, 100.0f, 100.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 199.0f, 90.0f }, 1.0f, {}, 12.0f, 16.0f, placement));
    UiWidgetTests::ExpectRect(placement.viewport, { 0.0f, 0.0f, 88.0f, 88.0f });
    EXPECT_TRUE(placement.horizontal.visible);
    EXPECT_TRUE(placement.vertical.visible);
    EXPECT_DOUBLE_EQ(placement.horizontal.maximum, 112.0);
    EXPECT_DOUBLE_EQ(placement.vertical.maximum, 2.0);
}

TEST(UiScrollbarLayoutTests, ExactFitAfterOtherAxisReservationDoesNotAddBar){
    const Rect bounds{ 0.0f, 0.0f, 100.0f, 100.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 87.0f, 200.0f }, 1.0f, {}, 12.0f, 16.0f, placement));
    UiWidgetTests::ExpectRect(placement.viewport, { 0.0f, 0.0f, 88.0f, 100.0f });
    EXPECT_FALSE(placement.horizontal.visible);
    EXPECT_TRUE(placement.vertical.visible);
    ASSERT_TRUE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 199.0f, 88.0f }, 1.0f, {}, 12.0f, 16.0f, placement));
    UiWidgetTests::ExpectRect(placement.viewport, { 0.0f, 0.0f, 100.0f, 88.0f });
    EXPECT_TRUE(placement.horizontal.visible);
    EXPECT_FALSE(placement.vertical.visible);
}

TEST(UiScrollbarLayoutTests, SmallerContentRemovesReservationsAndClampsBothAxes){
    const Rect bounds{ 0.0f, 0.0f, 100.0f, 100.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, bounds, {}, { 399.0f, 300.0f }, 1.0f,
        { 200.0f, 150.0f }, 12.0f, 16.0f, placement
    ));
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, bounds, {}, { 49.0f, 40.0f }, 1.0f,
        { 200.0f, 150.0f }, 12.0f, 16.0f, placement
    ));
    UiWidgetTests::ExpectRect(placement.viewport, bounds);
    UiWidgetTests::ExpectRect(placement.corner, {});
    EXPECT_FALSE(placement.horizontal.visible);
    EXPECT_FALSE(placement.vertical.visible);
    EXPECT_DOUBLE_EQ(placement.horizontal.offset, 0.0);
    EXPECT_DOUBLE_EQ(placement.vertical.offset, 0.0);
}

TEST(UiScrollbarLayoutTests, MinimumThumbIsClampedToEachTrackEvenWithRemainingScrollRange){
    const Rect bounds{ 10.0f, 20.0f, 100.0f, 80.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, bounds, {}, { 999.0f, 900.0f }, 1.0f,
        { 400.0f, 300.0f }, 12.0f, 1000.0f, placement
    ));
    UiWidgetTests::ExpectRect(placement.horizontal.thumb, placement.horizontal.track);
    UiWidgetTests::ExpectRect(placement.vertical.thumb, placement.vertical.track);
    EXPECT_GT(placement.horizontal.maximum, 0.0);
    EXPECT_GT(placement.vertical.maximum, 0.0);
    ASSERT_TRUE(ScrollbarLayout::UpdateOffsets({ Limit<f32>::s_Max, Limit<f32>::s_Max }, placement));
    UiWidgetTests::ExpectRect(placement.horizontal.thumb, placement.horizontal.track);
    UiWidgetTests::ExpectRect(placement.vertical.thumb, placement.vertical.track);
    EXPECT_DOUBLE_EQ(placement.horizontal.offset, placement.horizontal.maximum);
    EXPECT_DOUBLE_EQ(placement.vertical.offset, placement.vertical.maximum);
}

TEST(UiScrollbarLayoutTests, ZeroMinimumThumbUsesViewportToContentRatio){
    const Rect bounds{ 0.0f, 0.0f, 200.0f, 100.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 399.0f, 200.0f }, 1.0f, {}, 12.0f, 0.0f, placement));
    EXPECT_FLOAT_EQ(
        placement.horizontal.thumb.width, 88.36f);
        EXPECT_FLOAT_EQ(placement.vertical.thumb.height, 38.72f
    );
}

TEST(UiScrollbarLayoutTests, ZeroThicknessKeepsFullViewportWhileRetainingScrollableExtents){
    const Rect bounds{ 10.0f, 20.0f, 100.0f, 80.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, bounds, {}, { 399.0f, 200.0f }, 1.0f,
        { 350.0f, 100.0f }, 0.0f, 16.0f, placement
    ));
    UiWidgetTests::ExpectRect(placement.viewport, bounds);
    EXPECT_FALSE(placement.horizontal.visible);
    EXPECT_FALSE(placement.vertical.visible);
    EXPECT_DOUBLE_EQ(placement.horizontal.maximum, 300.0);
    EXPECT_DOUBLE_EQ(placement.vertical.maximum, 120.0);
    EXPECT_DOUBLE_EQ(placement.horizontal.offset, 300.0);
    EXPECT_DOUBLE_EQ(placement.vertical.offset, 100.0);
    ASSERT_TRUE(ScrollbarLayout::UpdateOffsets({ 40.0f, 50.0f }, placement));
    EXPECT_DOUBLE_EQ(placement.horizontal.offset, 40.0);
    EXPECT_DOUBLE_EQ(placement.vertical.offset, 50.0);
    UiWidgetTests::ExpectRect(placement.horizontal.thumb, {});
    UiWidgetTests::ExpectRect(placement.vertical.thumb, {});
}

TEST(UiScrollbarLayoutTests, InheritedClipChangesOnlyContentClip){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    ScrollViewportPlacement reference;
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, bounds, {}, { 399.0f, 300.0f }, 1.0f,
        { 60.0f, 70.0f }, 12.0f, 16.0f, reference
    ));
    ScrollViewportPlacement clipped;
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, { 50.0f, 60.0f, 40.0f, 20.0f }, {},
        { 399.0f, 300.0f }, 1.0f, { 60.0f, 70.0f }, 12.0f, 16.0f, clipped
    ));
    UiWidgetTests::ExpectRect(clipped.contentClip, { 50.0f, 60.0f, 40.0f, 20.0f });
    reference.contentClip = clipped.contentClip;
    ExpectPlacement(clipped, reference);
}

TEST(UiScrollbarLayoutTests, DisjointAndZeroAreaClipsKeepOffsetsAndLogicalBars){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    const Rect clips[] = { { 300.0f, 20.0f, 10.0f, 100.0f }, { 10.0f, 200.0f, 200.0f, 10.0f },
        { 10.0f, 20.0f, 0.0f, 100.0f }, { 10.0f, 20.0f, 200.0f, 0.0f } };
    for(const Rect& clip : clips){
        ScrollViewportPlacement placement;
        ASSERT_TRUE(ScrollbarLayout::Calculate(
            bounds, clip, {}, { 399.0f, 300.0f }, 1.0f,
            { 60.0f, 70.0f }, 12.0f, 16.0f, placement
        ));
        EXPECT_TRUE(placement.contentClip.width == 0.0f || placement.contentClip.height == 0.0f);
        EXPECT_TRUE(placement.horizontal.visible);
        EXPECT_TRUE(placement.vertical.visible);
        EXPECT_DOUBLE_EQ(placement.horizontal.offset, 60.0);
        EXPECT_DOUBLE_EQ(placement.vertical.offset, 70.0);
    }
}

TEST(UiScrollbarLayoutTests, ExcessivePaddingCollapsesViewportWithoutNegativeTracks){
    const Rect bounds{ 10.0f, 20.0f, 100.0f, 80.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, bounds, { 200.0f, 120.0f, 200.0f, 120.0f },
        { 399.0f, 300.0f }, 1.0f, { 60.0f, 70.0f }, 12.0f, 16.0f, placement
    ));
    UiWidgetTests::ExpectRect(placement.viewport, { 110.0f, 100.0f, 0.0f, 0.0f });
    UiWidgetTests::ExpectRect(placement.corner, {});
    EXPECT_FALSE(placement.horizontal.visible);
    EXPECT_FALSE(placement.vertical.visible);
    EXPECT_DOUBLE_EQ(placement.horizontal.maximum, 400.0);
    EXPECT_DOUBLE_EQ(placement.vertical.maximum, 300.0);
    EXPECT_DOUBLE_EQ(placement.horizontal.offset, 60.0);
    EXPECT_DOUBLE_EQ(placement.vertical.offset, 70.0);
}

TEST(UiScrollbarLayoutTests, TinyBoundsClampBarThicknessAndKeepZeroLengthTracksHidden){
    const Rect bounds{ 10.0f, 20.0f, 2.0f, 3.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, bounds, {}, { 99.0f, 100.0f }, 1.0f,
        { 10.0f, 20.0f }, Limit<f32>::s_Max, Limit<f32>::s_Max, placement
    ));
    UiWidgetTests::ExpectRect(placement.viewport, { 10.0f, 20.0f, 0.0f, 0.0f });
    UiWidgetTests::ExpectRect(placement.corner, bounds);
    UiWidgetTests::ExpectRect(placement.horizontal.track, {});
    UiWidgetTests::ExpectRect(placement.vertical.track, {});
    EXPECT_FALSE(placement.horizontal.visible);
    EXPECT_FALSE(placement.vertical.visible);
    EXPECT_DOUBLE_EQ(placement.horizontal.maximum, 100.0);
    EXPECT_DOUBLE_EQ(placement.vertical.maximum, 100.0);
    ASSERT_TRUE(ScrollbarLayout::UpdateOffsets({ 1000.0f, 1000.0f }, placement));
    EXPECT_DOUBLE_EQ(placement.horizontal.offset, 100.0);
    EXPECT_DOUBLE_EQ(placement.vertical.offset, 100.0);
}

TEST(UiScrollbarLayoutTests, OneCollapsedAxisKeepsOtherAxisExtentWithoutVisibleTrack){
    const Rect bounds{ 10.0f, 20.0f, 0.0f, 80.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, bounds, {}, { 99.0f, 200.0f }, 1.0f,
        { 10.0f, 20.0f }, 12.0f, 16.0f, placement
    ));
    UiWidgetTests::ExpectRect(placement.viewport, bounds);
    EXPECT_FALSE(placement.horizontal.visible);
    EXPECT_FALSE(placement.vertical.visible);
    EXPECT_DOUBLE_EQ(placement.horizontal.maximum, 100.0);
    EXPECT_DOUBLE_EQ(placement.vertical.maximum, 120.0);
}

TEST(UiScrollbarLayoutTests, ExtremeFiniteStoredOffsetsClampBeforeThumbArithmetic){
    const Rect bounds{ 10.0f, 20.0f, 100.0f, 80.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, bounds, {}, { 399.0f, 200.0f }, 1.0f,
        { Limit<f32>::s_Max, Limit<f32>::s_Max }, 12.0f, 16.0f, placement
    ));
    EXPECT_DOUBLE_EQ(placement.horizontal.offset, 312.0);
    EXPECT_DOUBLE_EQ(placement.vertical.offset, 132.0);
    EXPECT_FLOAT_EQ(
        placement.horizontal.thumb.x + placement.horizontal.thumb.width,
        placement.horizontal.track.x + placement.horizontal.track.width
    );
    EXPECT_FLOAT_EQ(
        placement.vertical.thumb.y + placement.vertical.thumb.height,
        placement.vertical.track.y + placement.vertical.track.height
    );
}

TEST(UiScrollbarLayoutTests, OverscrollUpdatesClampBothThumbsAndRestoreExactStartGeometry){
    const Rect bounds{ 10.0f, 20.0f, 112.0f, 112.0f };
    ScrollViewportPlacement placement;
    ASSERT_TRUE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 399.0f, 400.0f }, 1.0f, {}, 12.0f, 0.0f, placement));
    const ScrollViewportPlacement initial = placement;
    ASSERT_TRUE(ScrollbarLayout::UpdateOffsets({ 1000.0f, 1000.0f }, placement));
    ExpectReservedGeometry(placement, initial);
    UiWidgetTests::ExpectRect(placement.horizontal.thumb, { 85.0f, 120.0f, 25.0f, 12.0f });
    UiWidgetTests::ExpectRect(placement.vertical.thumb, { 110.0f, 95.0f, 12.0f, 25.0f });
    ASSERT_TRUE(ScrollbarLayout::UpdateOffsets({}, placement));
    ExpectPlacement(placement, initial);
}

TEST(UiScrollbarLayoutTests, InvalidCalculationInputsPreserveCompletePreviousPlacement){
    const Rect bounds{ 10.0f, 20.0f, 112.0f, 112.0f };
    const Insets padding{ 1.0f, 2.0f, 3.0f, 4.0f };
    ScrollViewportPlacement previous;
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, bounds, padding, { 399.0f, 400.0f }, 1.0f,
        { 40.0f, 50.0f }, 12.0f, 16.0f, previous
    ));
    const auto reject = [&previous](const Rect& valueBounds, const Rect& clip, const Insets& valuePadding,
        const Point& measure, const f32 caret, const Point& scroll, const f32 thickness, const f32 minimum){
        ScrollViewportPlacement placement = previous;
        EXPECT_FALSE(ScrollbarLayout::Calculate(valueBounds, clip, valuePadding, measure, caret, scroll, thickness, minimum, placement));
        ExpectPlacement(placement, previous);
    };
    const f32 nan = Limit<f32>::s_QuietNaN;
    const f32 infinity = Limit<f32>::s_Infinity;
    reject({ nan, 20.0f, 112.0f, 112.0f }, bounds, padding, { 399.0f, 400.0f }, 1.0f, {}, 12.0f, 16.0f);
    reject({ 10.0f, 20.0f, -1.0f, 112.0f }, bounds, padding, { 399.0f, 400.0f }, 1.0f, {}, 12.0f, 16.0f);
    reject(
        { Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 112.0f }, bounds, padding,
        { 399.0f, 400.0f }, 1.0f, {}, 12.0f, 16.0f
    );
    reject(bounds, { 10.0f, infinity, 112.0f, 112.0f }, padding, { 399.0f, 400.0f }, 1.0f, {}, 12.0f, 16.0f);
    reject(bounds, { 10.0f, 20.0f, 112.0f, -1.0f }, padding, { 399.0f, 400.0f }, 1.0f, {}, 12.0f, 16.0f);
    reject(bounds, bounds, { -1.0f, 2.0f, 3.0f, 4.0f }, { 399.0f, 400.0f }, 1.0f, {}, 12.0f, 16.0f);
    reject(bounds, bounds, { 1.0f, 2.0f, 3.0f, nan }, { 399.0f, 400.0f }, 1.0f, {}, 12.0f, 16.0f);
    reject(bounds, bounds, padding, { -1.0f, 400.0f }, 1.0f, {}, 12.0f, 16.0f);
    reject(bounds, bounds, padding, { 399.0f, nan }, 1.0f, {}, 12.0f, 16.0f);
    reject(bounds, bounds, padding, { infinity, 400.0f }, 1.0f, {}, 12.0f, 16.0f);
    reject(bounds, bounds, padding, { 399.0f, 400.0f }, 0.0f, {}, 12.0f, 16.0f);
    reject(bounds, bounds, padding, { 399.0f, 400.0f }, infinity, {}, 12.0f, 16.0f);
    reject(bounds, bounds, padding, { 399.0f, 400.0f }, nan, {}, 12.0f, 16.0f);
    reject(bounds, bounds, padding, { 399.0f, 400.0f }, 1.0f, { -1.0f, 0.0f }, 12.0f, 16.0f);
    reject(bounds, bounds, padding, { 399.0f, 400.0f }, 1.0f, { 0.0f, infinity }, 12.0f, 16.0f);
    reject(bounds, bounds, padding, { 399.0f, 400.0f }, 1.0f, { nan, 0.0f }, 12.0f, 16.0f);
    reject(bounds, bounds, padding, { 399.0f, 400.0f }, 1.0f, {}, -1.0f, 16.0f);
    reject(bounds, bounds, padding, { 399.0f, 400.0f }, 1.0f, {}, infinity, 16.0f);
    reject(bounds, bounds, padding, { 399.0f, 400.0f }, 1.0f, {}, 12.0f, -1.0f);
    reject(bounds, bounds, padding, { 399.0f, 400.0f }, 1.0f, {}, 12.0f, nan);
}

TEST(UiScrollbarLayoutTests, UnrepresentableFinalViewportRejectsExtremeContentAtomically){
    const Rect bounds{ 0.0f, 0.0f, 112.0f, 112.0f };
    ScrollViewportPlacement previous;
    ASSERT_TRUE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 399.0f, 400.0f }, 1.0f, {}, 12.0f, 16.0f, previous));
    ScrollViewportPlacement placement = previous;
    EXPECT_FALSE(ScrollbarLayout::Calculate(
        bounds, bounds, {}, { Limit<f32>::s_Max, Limit<f32>::s_Max },
        Limit<f32>::s_Max, { Limit<f32>::s_Max, Limit<f32>::s_Max }, 12.0f, 16.0f, placement
    ));
    ExpectPlacement(placement, previous);
    EXPECT_FALSE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 1.0e30f, 1.0e30f }, 1.0f, {}, 0.0f, 16.0f, placement));
    ExpectPlacement(placement, previous);
}

TEST(UiScrollbarLayoutTests, UnrepresentableBarReservationRejectsAtomically){
    const Rect bounds{ 0.0f, 0.0f, 112.0f, 112.0f };
    ScrollViewportPlacement previous;
    ASSERT_TRUE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 399.0f, 400.0f }, 1.0f, {}, 12.0f, 16.0f, previous));
    ScrollViewportPlacement placement = previous;
    EXPECT_FALSE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 399.0f, 400.0f }, 1.0f, {}, 1.0e-30f, 0.0f, placement));
    ExpectPlacement(placement, previous);
}

TEST(UiScrollbarLayoutTests, InvalidOffsetRequestsPreserveBothAxesAndGeometry){
    const Rect bounds{ 0.0f, 0.0f, 112.0f, 112.0f };
    ScrollViewportPlacement previous;
    ASSERT_TRUE(ScrollbarLayout::Calculate(
        bounds, bounds, {}, { 399.0f, 400.0f }, 1.0f,
        { 40.0f, 50.0f }, 12.0f, 16.0f, previous
    ));
    const Point inputs[] = { { -1.0f, 0.0f }, { 0.0f, -1.0f }, { Limit<f32>::s_QuietNaN, 0.0f },
        { 0.0f, Limit<f32>::s_Infinity } };
    for(const Point& input : inputs){
        ScrollViewportPlacement placement = previous;
        EXPECT_FALSE(ScrollbarLayout::UpdateOffsets(input, placement));
        ExpectPlacement(placement, previous);
    }
}

TEST(UiScrollbarLayoutTests, InvalidOwnedProjectionRejectsOffsetUpdateAtomically){
    const Rect bounds{ 0.0f, 0.0f, 112.0f, 112.0f };
    ScrollViewportPlacement previous;
    ASSERT_TRUE(ScrollbarLayout::Calculate(bounds, bounds, {}, { 399.0f, 400.0f }, 1.0f, {}, 12.0f, 16.0f, previous));
    const auto reject = [](ScrollViewportPlacement& placement){
        const ScrollViewportPlacement expected = placement;
        EXPECT_FALSE(ScrollbarLayout::UpdateOffsets({ 40.0f, 50.0f }, placement));
        ExpectPlacement(placement, expected);
    };
    ScrollViewportPlacement placement = previous;
    placement.horizontal.maximum = -1.0;
    reject(placement);
    placement = previous;
    placement.vertical.offset = placement.vertical.maximum + 1.0;
    reject(placement);
    placement = previous;
    placement.horizontal.viewportExtent += 1.0;
    reject(placement);
    placement = previous;
    placement.vertical.thumb.height = placement.vertical.track.height + 1.0f;
    reject(placement);
    placement = previous;
    placement.horizontal.visible = false;
    reject(placement);
    placement = previous;
    placement.corner.width = -1.0f;
    reject(placement);
    placement = previous;
    placement.contentClip.y = Limit<f32>::s_Infinity;
    reject(placement);
    placement = previous;
    placement.vertical.contentExtent = Limit<f64>::s_Infinity;
    reject(placement);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

