// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ui/widgets/slider.h>
#include <impl/ui/widgets/slider_style.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;


static void ExpectRect(const Rect& actual, const Rect& expected){
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.width, expected.width);
    EXPECT_FLOAT_EQ(actual.height, expected.height);
}

static void ExpectMetrics(const SliderMetrics& actual, const SliderMetrics& expected){
    EXPECT_FLOAT_EQ(actual.padding.left, expected.padding.left);
    EXPECT_FLOAT_EQ(actual.padding.top, expected.padding.top);
    EXPECT_FLOAT_EQ(actual.padding.right, expected.padding.right);
    EXPECT_FLOAT_EQ(actual.padding.bottom, expected.padding.bottom);
    EXPECT_FLOAT_EQ(actual.thumbExtent.x, expected.thumbExtent.x);
    EXPECT_FLOAT_EQ(actual.thumbExtent.y, expected.thumbExtent.y);
    EXPECT_FLOAT_EQ(actual.trackHeight, expected.trackHeight);
    EXPECT_FLOAT_EQ(actual.contentSize.x, expected.contentSize.x);
    EXPECT_FLOAT_EQ(actual.contentSize.y, expected.contentSize.y);
}

static void ExpectPlacement(const SliderPlacement& actual, const SliderPlacement& expected){
    ExpectRect(actual.bounds, expected.bounds);
    ExpectRect(actual.clip, expected.clip);
    ExpectRect(actual.travelBounds, expected.travelBounds);
    ExpectRect(actual.track, expected.track);
    ExpectRect(actual.centerTravel, expected.centerTravel);
    ExpectRect(actual.thumb, expected.thumb);
    EXPECT_FLOAT_EQ(actual.thumbExtent.x, expected.thumbExtent.x);
    EXPECT_FLOAT_EQ(actual.thumbExtent.y, expected.thumbExtent.y);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiSliderLayoutTests, DefaultMetricsIncludePaddingAroundTheStableThumbAndTrack){
    SliderMetrics metrics;
    ASSERT_TRUE(SliderLayout::Measure({}, {}, metrics));
    EXPECT_FLOAT_EQ(metrics.contentSize.x, 32.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 32.0f);
    EXPECT_FLOAT_EQ(metrics.thumbExtent.x, 24.0f);
    EXPECT_FLOAT_EQ(metrics.thumbExtent.y, 24.0f);
    EXPECT_FLOAT_EQ(metrics.trackHeight, 12.0f);
}

TEST(UiSliderLayoutTests, TallTracksAndThumbsGrowIntrinsicHeightAndRequestedHeightIsHonored){
    SliderStyle style;
    style.thumbExtent = { 40.0f, 50.0f };
    style.trackHeight = 60.0f;
    SliderMetrics metrics;
    ASSERT_TRUE(SliderLayout::Measure({}, style, metrics));
    EXPECT_FLOAT_EQ(metrics.contentSize.x, 48.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 68.0f);
    SliderOptions options;
    options.height = 90.0f;
    ASSERT_TRUE(SliderLayout::Measure(options, style, metrics));
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 90.0f);
}

TEST(UiSliderLayoutTests, PlacementSeparatesBaseTrackTravelAndThumbGeometry){
    SliderMetrics metrics;
    ASSERT_TRUE(SliderLayout::Measure({}, {}, metrics));
    const Rect bounds{ 10.0f, 20.0f, 240.0f, 32.0f };
    SliderPlacement placement;
    ASSERT_TRUE(SliderLayout::Place(bounds, bounds, metrics, 0.25, placement));
    ExpectRect(placement.bounds, bounds);
    ExpectRect(placement.clip, bounds);
    ExpectRect(placement.travelBounds, { 14.0f, 24.0f, 232.0f, 24.0f });
    ExpectRect(placement.centerTravel, { 26.0f, 24.0f, 208.0f, 24.0f });
    ExpectRect(placement.track, { 26.0f, 30.0f, 208.0f, 12.0f });
    ExpectRect(placement.thumb, { 66.0f, 24.0f, 24.0f, 24.0f });
}

TEST(UiSliderLayoutTests, EndpointThumbPositionsMeetTheExactPaddedBounds){
    SliderMetrics metrics;
    ASSERT_TRUE(SliderLayout::Measure({}, {}, metrics));
    const Rect bounds{ 10.0f, 20.0f, 240.0f, 32.0f };
    SliderPlacement first;
    SliderPlacement last;
    ASSERT_TRUE(SliderLayout::Place(bounds, bounds, metrics, 0.0, first));
    ASSERT_TRUE(SliderLayout::Place(bounds, bounds, metrics, 1.0, last));
    EXPECT_FLOAT_EQ(first.thumb.x, first.travelBounds.x);
    EXPECT_FLOAT_EQ(last.thumb.x + last.thumb.width, last.travelBounds.x + last.travelBounds.width);
    EXPECT_FLOAT_EQ(last.thumb.x, 222.0f);
    ExpectRect(first.track, last.track);
    ExpectRect(first.centerTravel, last.centerTravel);
}

TEST(UiSliderLayoutTests, ClipChangesOnlyVisibilityAndPreservesAcceptedTravelGeometry){
    SliderMetrics metrics;
    ASSERT_TRUE(SliderLayout::Measure({}, {}, metrics));
    const Rect bounds{ 10.0f, 20.0f, 240.0f, 32.0f };
    SliderPlacement full;
    SliderPlacement clipped;
    ASSERT_TRUE(SliderLayout::Place(bounds, bounds, metrics, 0.25, full));
    ASSERT_TRUE(SliderLayout::Place(bounds, { 50.0f, 25.0f, 100.0f, 10.0f }, metrics, 0.25, clipped));
    ExpectRect(clipped.clip, { 50.0f, 25.0f, 100.0f, 10.0f });
    ExpectRect(clipped.travelBounds, full.travelBounds);
    ExpectRect(clipped.centerTravel, full.centerTravel);
    ExpectRect(clipped.track, full.track);
    ExpectRect(clipped.thumb, full.thumb);
}

TEST(UiSliderLayoutTests, TinyAreasBoundThumbDimensionsAndProduceZeroTravelSafely){
    SliderMetrics metrics;
    ASSERT_TRUE(SliderLayout::Measure({}, {}, metrics));
    const Rect bounds{ 10.0f, 20.0f, 20.0f, 12.0f };
    SliderPlacement placement;
    ASSERT_TRUE(SliderLayout::Place(bounds, bounds, metrics, 0.5, placement));
    ExpectRect(placement.travelBounds, { 14.0f, 24.0f, 12.0f, 4.0f });
    ExpectRect(placement.thumb, placement.travelBounds);
    EXPECT_FLOAT_EQ(placement.thumbExtent.x, 12.0f);
    EXPECT_FLOAT_EQ(placement.thumbExtent.y, 4.0f);
    EXPECT_FLOAT_EQ(placement.centerTravel.width, 0.0f);
    EXPECT_FLOAT_EQ(placement.track.width, 0.0f);
}

TEST(UiSliderLayoutTests, PaddingLargerThanTheAreaAndEmptyAreasRemainValid){
    SliderMetrics metrics;
    ASSERT_TRUE(SliderLayout::Measure({}, {}, metrics));
    for(const Rect bounds : { Rect{ 10.0f, 20.0f, 4.0f, 3.0f }, Rect{ 10.0f, 20.0f, 0.0f, 0.0f } }){
        SliderPlacement placement;
        ASSERT_TRUE(SliderLayout::Place(bounds, bounds, metrics, 1.0, placement));
        EXPECT_FLOAT_EQ(placement.travelBounds.width, 0.0f);
        EXPECT_FLOAT_EQ(placement.travelBounds.height, 0.0f);
        EXPECT_FLOAT_EQ(placement.thumb.width, 0.0f);
        EXPECT_FLOAT_EQ(placement.thumb.height, 0.0f);
        EXPECT_FLOAT_EQ(placement.centerTravel.width, 0.0f);
    }
}

TEST(UiSliderLayoutTests, AsymmetricPaddingAndIndependentThumbDimensionsCenterBothParts){
    SliderStyle style;
    style.padding = { 2.0f, 3.0f, 5.0f, 7.0f };
    style.thumbExtent = { 30.0f, 10.0f };
    style.trackHeight = 16.0f;
    SliderMetrics metrics;
    ASSERT_TRUE(SliderLayout::Measure({}, style, metrics));
    const Rect bounds{ 0.0f, 0.0f, 100.0f, 40.0f };
    SliderPlacement placement;
    ASSERT_TRUE(SliderLayout::Place(bounds, bounds, metrics, 0.5, placement));
    ExpectRect(placement.travelBounds, { 2.0f, 3.0f, 93.0f, 30.0f });
    ExpectRect(placement.centerTravel, { 17.0f, 3.0f, 63.0f, 30.0f });
    ExpectRect(placement.track, { 17.0f, 10.0f, 63.0f, 16.0f });
    ExpectRect(placement.thumb, { 33.5f, 13.0f, 30.0f, 10.0f });
}

TEST(UiSliderLayoutTests, InvalidOptionsStylesAndTintsPreservePreviouslyMeasuredMetrics){
    SliderMetrics measured;
    ASSERT_TRUE(SliderLayout::Measure({}, {}, measured));
    for(u32 field = 0u; field < 13u; ++field){
        SliderOptions options;
        SliderStyle style;
        switch(field){
        case 0u: options.minimum = 2.0; break;
        case 1u: options.maximum = Limit<f64>::s_Infinity; break;
        case 2u: options.keyStep = -1.0; break;
        case 3u: options.height = -1.0f; break;
        case 4u: options.width.value = 0.0f; break;
        case 5u: style.padding.left = -1.0f; break;
        case 6u: style.thumbExtent.x = 0.0f; break;
        case 7u: style.thumbExtent.y = Limit<f32>::s_QuietNaN; break;
        case 8u: style.trackHeight = 0.0f; break;
        case 9u: style.hoverTint.r = -1.0f; break;
        case 10u: style.pressedTint.a = 1.01f; break;
        case 11u: style.disabledTint.a = Limit<f32>::s_QuietNaN; break;
        default: style.padding.right = Limit<f32>::s_Infinity; break;
        }
        SliderMetrics output = measured;
        EXPECT_FALSE(SliderLayout::Measure(options, style, output));
        ExpectMetrics(output, measured);
    }
}

TEST(UiSliderLayoutTests, OverflowingIntrinsicMetricsAreRejectedAtomically){
    SliderStyle style;
    style.thumbExtent.x = Limit<f32>::s_Max;
    style.padding.left = Limit<f32>::s_Max;
    SliderMetrics output;
    ASSERT_TRUE(SliderLayout::Measure({}, {}, output));
    const SliderMetrics before = output;
    EXPECT_FALSE(SliderLayout::Measure({}, style, output));
    ExpectMetrics(output, before);
}

TEST(UiSliderLayoutTests, InvalidProspectivePlacementPreservesEveryOutputRectangle){
    SliderMetrics metrics;
    ASSERT_TRUE(SliderLayout::Measure({}, {}, metrics));
    const Rect normal{ 10.0f, 20.0f, 240.0f, 32.0f };
    SliderPlacement measured;
    ASSERT_TRUE(SliderLayout::Place(normal, normal, metrics, 0.25, measured));
    for(u32 field = 0u; field < 10u; ++field){
        Rect bounds = normal;
        Rect clip = normal;
        SliderMetrics invalid = metrics;
        f64 normalized = 0.5;
        switch(field){
        case 0u: bounds.width = -1.0f; break;
        case 1u: bounds.x = Limit<f32>::s_QuietNaN; break;
        case 2u: bounds = { Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 32.0f }; break;
        case 3u: clip.height = -1.0f; break;
        case 4u: invalid.thumbExtent.x = -1.0f; break;
        case 5u: invalid.contentSize.y = 1.0f; break;
        case 6u: normalized = -0.01; break;
        case 7u: normalized = 1.01; break;
        case 8u: normalized = Limit<f64>::s_Infinity; break;
        default: normalized = Limit<f64>::s_QuietNaN; break;
        }
        SliderPlacement output = measured;
        EXPECT_FALSE(SliderLayout::Place(bounds, clip, invalid, normalized, output));
        ExpectPlacement(output, measured);
    }
}

TEST(UiSliderLayoutTests, PositiveDimensionsThatCollapseAtExtremeCoordinatesAreRejected){
    SliderMetrics metrics;
    ASSERT_TRUE(SliderLayout::Measure({}, {}, metrics));
    SliderPlacement output;
    const SliderPlacement before = output;
    const Rect bounds{ Limit<f32>::s_Max, 0.0f, 1.0f, 32.0f };
    EXPECT_FALSE(SliderLayout::Place(bounds, bounds, metrics, 0.5, output));
    ExpectPlacement(output, before);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

