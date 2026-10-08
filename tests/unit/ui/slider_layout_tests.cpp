// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/slider.h>
#include <impl/ecs_ui/toolkit/widgets/slider_style.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiSliderLayoutTests, EndpointThumbPositionsMeetTheExactPaddedBounds){
    const auto metrics = SliderLayout::Measure({}, {});
    ASSERT_TRUE(metrics);
    const Rect bounds{ 10.0f, 20.0f, 240.0f, 32.0f };
    const auto first = SliderLayout::Place(bounds, bounds, *metrics, 0.0);
    ASSERT_TRUE(first);
    const auto last = SliderLayout::Place(bounds, bounds, *metrics, 1.0);
    ASSERT_TRUE(last);
    EXPECT_FLOAT_EQ(first->thumb.x, first->travelBounds.x);
    EXPECT_FLOAT_EQ(last->thumb.x + last->thumb.width, last->travelBounds.x + last->travelBounds.width);
}

TEST(UiSliderLayoutTests, ClipChangesOnlyVisibilityAndPreservesAcceptedTravelGeometry){
    const auto metrics = SliderLayout::Measure({}, {});
    ASSERT_TRUE(metrics);
    const Rect bounds{ 10.0f, 20.0f, 240.0f, 32.0f };
    const auto full = SliderLayout::Place(bounds, bounds, *metrics, 0.25);
    ASSERT_TRUE(full);
    const auto clipped = SliderLayout::Place(bounds, { 50.0f, 25.0f, 100.0f, 10.0f }, *metrics, 0.25);
    ASSERT_TRUE(clipped);
    UiWidgetTests::ExpectRect(clipped->clip, { 50.0f, 25.0f, 100.0f, 10.0f });
    UiWidgetTests::ExpectRect(clipped->travelBounds, full->travelBounds);
    UiWidgetTests::ExpectRect(clipped->centerTravel, full->centerTravel);
    UiWidgetTests::ExpectRect(clipped->track, full->track);
    UiWidgetTests::ExpectRect(clipped->thumb, full->thumb);
}

TEST(UiSliderLayoutTests, TinyAreasBoundThumbDimensionsAndProduceZeroTravelSafely){
    const auto metrics = SliderLayout::Measure({}, {});
    ASSERT_TRUE(metrics);
    const Rect bounds{ 10.0f, 20.0f, 20.0f, 12.0f };
    const auto placement = SliderLayout::Place(bounds, bounds, *metrics, 0.5);
    ASSERT_TRUE(placement);
    UiWidgetTests::ExpectRect(placement->travelBounds, { 14.0f, 24.0f, 12.0f, 4.0f });
    UiWidgetTests::ExpectRect(placement->thumb, placement->travelBounds);
    EXPECT_FLOAT_EQ(placement->thumbExtent.x, 12.0f);
    EXPECT_FLOAT_EQ(placement->thumbExtent.y, 4.0f);
    EXPECT_FLOAT_EQ(placement->centerTravel.width, 0.0f);
    EXPECT_FLOAT_EQ(placement->track.width, 0.0f);
}

TEST(UiSliderLayoutTests, PaddingLargerThanTheAreaAndEmptyAreasRemainValid){
    const auto metrics = SliderLayout::Measure({}, {});
    ASSERT_TRUE(metrics);
    for(const Rect bounds : { Rect{ 10.0f, 20.0f, 4.0f, 3.0f }, Rect{ 10.0f, 20.0f, 0.0f, 0.0f } }){
        const auto placement = SliderLayout::Place(bounds, bounds, *metrics, 1.0);
        ASSERT_TRUE(placement);
        EXPECT_FLOAT_EQ(placement->travelBounds.width, 0.0f);
        EXPECT_FLOAT_EQ(placement->travelBounds.height, 0.0f);
        EXPECT_FLOAT_EQ(placement->thumb.width, 0.0f);
        EXPECT_FLOAT_EQ(placement->thumb.height, 0.0f);
        EXPECT_FLOAT_EQ(placement->centerTravel.width, 0.0f);
    }
}

TEST(UiSliderLayoutTests, InvalidOptionsStylesAndTintsRejectMetrics){
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
        EXPECT_FALSE(SliderLayout::Measure(options, style));
    }
}

TEST(UiSliderLayoutTests, OverflowingIntrinsicMetricsAreRejected){
    SliderStyle style;
    style.thumbExtent.x = Limit<f32>::s_Max;
    style.padding.left = Limit<f32>::s_Max;
    EXPECT_FALSE(SliderLayout::Measure({}, style));
}

TEST(UiSliderLayoutTests, InvalidProspectivePlacementRejectsGeometry){
    const auto metrics = SliderLayout::Measure({}, {});
    ASSERT_TRUE(metrics);
    const Rect normal{ 10.0f, 20.0f, 240.0f, 32.0f };
    for(u32 field = 0u; field < 10u; ++field){
        Rect bounds = normal;
        Rect clip = normal;
        SliderMetrics invalid = *metrics;
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
        EXPECT_FALSE(SliderLayout::Place(bounds, clip, invalid, normalized));
    }
}

TEST(UiSliderLayoutTests, PositiveDimensionsThatCollapseAtExtremeCoordinatesAreRejected){
    const auto metrics = SliderLayout::Measure({}, {});
    ASSERT_TRUE(metrics);
    const Rect bounds{ Limit<f32>::s_Max, 0.0f, 1.0f, 32.0f };
    EXPECT_FALSE(SliderLayout::Place(bounds, bounds, *metrics, 0.5));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

