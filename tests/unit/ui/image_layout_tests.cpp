// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/image.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_image_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;
using namespace Impl::Ui;


static UiSkinRegion Sprite(){
    UiSkinRegion region;
    region.name = Name("image.test");
    region.rectangle = { 10u, 20u, 200u, 100u };
    return region;
}

static void ExpectMetrics(const ImageMetrics& actual, const ImageMetrics& expected){
    EXPECT_EQ(BitCast<u32>(actual.contentSize.x), BitCast<u32>(expected.contentSize.x));
    EXPECT_EQ(BitCast<u32>(actual.contentSize.y), BitCast<u32>(expected.contentSize.y));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiImageLayoutTests, NineSliceMinimumsRemainLogicalAndCanExceedTheNaturalExtent){
    UiSkinRegion region = Sprite();
    region.drawMode = UiSkinDrawMode::NineSlice;
    region.sliceInsets = { 20u, 10u, 30u, 15u };
    region.minimumWidth = 120.0f;
    region.minimumHeight = 30.0f;
    ImageMetrics metrics;
    const auto metricsResult = ImageLayout::Measure({}, region, 2.0f);
    ASSERT_TRUE(metricsResult);
    metrics = *metricsResult;
    ExpectMetrics(metrics, { { 120.0f, 50.0f } });
    region.minimumHeight = 80.0f;
    const auto metricsResult2 = ImageLayout::Measure({}, region, 2.0f);
    ASSERT_TRUE(metricsResult2);
    metrics = *metricsResult2;
    ExpectMetrics(metrics, { { 120.0f, 80.0f } });
}

TEST(UiImageLayoutTests, EmptyAndDisjointClipsRemainValidAtFiniteNegativeOrigins){
    const Rect bounds{ -100.0f, -50.0f, 200.0f, 100.0f };
    ImagePlacement placement;
    const auto placementResult = ImageLayout::Place(bounds, { 150.0f, 0.0f, 20.0f, 20.0f });
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    UiWidgetTests::ExpectRectExact(placement.bounds, bounds);
    UiWidgetTests::ExpectRectExact(placement.clip, { 150.0f, 0.0f, 0.0f, 20.0f });
    const auto placementResult2 = ImageLayout::Place({ -10.0f, -20.0f, 0.0f, 0.0f }, bounds);
    ASSERT_TRUE(placementResult2);
    placement = *placementResult2;
    UiWidgetTests::ExpectRectExact(placement.bounds, { -10.0f, -20.0f, 0.0f, 0.0f });
    UiWidgetTests::ExpectRectExact(placement.clip, placement.bounds);
}

TEST(UiImageLayoutTests, SubnormalPositiveBoundsAndExtremeFiniteEndpointsRemainValid){
    const f32 tiny = BitCast<f32>(u32{ 1u });
    const Rect small{ 0.0f, 0.0f, tiny, tiny };
    ImagePlacement placement;
    const auto placementResult = ImageLayout::Place(small, small);
    ASSERT_TRUE(placementResult);
    placement = *placementResult;
    UiWidgetTests::ExpectRectExact(placement.bounds, small);
    UiWidgetTests::ExpectRectExact(placement.clip, small);
    const Rect large{ -Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 1.0f };
    const auto placementResult2 = ImageLayout::Place(large, large);
    ASSERT_TRUE(placementResult2);
    placement = *placementResult2;
    UiWidgetTests::ExpectRectExact(placement.bounds, large);
    UiWidgetTests::ExpectRectExact(placement.clip, large);
}

TEST(UiImageLayoutTests, InvalidLayoutSizesTintsAndDensityRejectMeasurement){
    const UiSkinRegion region = Sprite();
    const auto measuredResult = ImageLayout::Measure({}, region, 2.0f);
    ASSERT_TRUE(measuredResult);
    for(u32 field = 0u; field < 15u; ++field){
        ImageOptions options;
        f32 density = 2.0f;
        switch(field){
        case 0u: options.width.policy = static_cast<LayoutSizePolicy::Enum>(3u); break;
        case 1u: options.height.policy = static_cast<LayoutSizePolicy::Enum>(3u); break;
        case 2u: options.width.value = -1.0f; break;
        case 3u: options.height = { LayoutSizePolicy::Stretch, 0.0f }; break;
        case 4u: options.width.value = Limit<f32>::s_QuietNaN; break;
        case 5u: options.height.value = Limit<f32>::s_Infinity; break;
        case 6u: options.tint.r = -1.0f; break;
        case 7u: options.tint.g = Limit<f32>::s_Infinity; break;
        case 8u: options.tint.b = Limit<f32>::s_QuietNaN; break;
        case 9u: options.tint.a = 1.01f; break;
        case 10u: options.tint.a = -0.01f; break;
        case 11u: density = 0.0f; break;
        case 12u: density = -1.0f; break;
        case 13u: density = Limit<f32>::s_Infinity; break;
        default: density = Limit<f32>::s_QuietNaN; break;
        }
        EXPECT_FALSE(ImageLayout::Measure(options, region, density));
    }
}

TEST(UiImageLayoutTests, InvalidRegionExtentMinimumsAndSliceMetadataRejectMeasurement){
    const UiSkinRegion valid = Sprite();
    const auto measuredResult = ImageLayout::Measure({}, valid, 2.0f);
    ASSERT_TRUE(measuredResult);
    for(u32 field = 0u; field < 8u; ++field){
        UiSkinRegion region = valid;
        switch(field){
        case 0u: region.rectangle.width = 0u; break;
        case 1u: region.rectangle.height = 0u; break;
        case 2u: region.minimumWidth = -1.0f; break;
        case 3u: region.minimumHeight = Limit<f32>::s_QuietNaN; break;
        case 4u: region.minimumWidth = Limit<f32>::s_Infinity; break;
        case 5u: region.drawMode = static_cast<UiSkinDrawMode::Enum>(2u); break;
        case 6u: region.sliceInsets.left = 1u; break;
        default:
            region.drawMode = UiSkinDrawMode::NineSlice;
            region.sliceInsets.bottom = 101u;
            break;
        }
        EXPECT_FALSE(ImageLayout::Measure({}, region, 2.0f));
    }
    UiSkinRegion region = valid;
    region.drawMode = UiSkinDrawMode::NineSlice;
    region.rectangle.width = Limit<u32>::s_Max;
    region.sliceInsets.left = Limit<u32>::s_Max;
    region.sliceInsets.right = Limit<u32>::s_Max;
    EXPECT_FALSE(ImageLayout::Measure({}, region, 2.0f));
}

TEST(UiImageLayoutTests, UnrepresentableNaturalExtentRejectsMeasurement){
    const UiSkinRegion region = Sprite();
    const auto measuredResult = ImageLayout::Measure({}, region, 2.0f);
    ASSERT_TRUE(measuredResult);
    EXPECT_FALSE(ImageLayout::Measure({}, region, BitCast<f32>(u32{ 1u })));
}

TEST(UiImageLayoutTests, InvalidAndCollapsedProspectiveEndpointsRejectPlacement){
    const Rect normal{ 10.0f, 20.0f, 200.0f, 100.0f };
    const auto measuredResult = ImageLayout::Place(normal, normal);
    ASSERT_TRUE(measuredResult);
    for(const Rect invalid : {
        Rect{ Limit<f32>::s_QuietNaN, 0.0f, 1.0f, 1.0f },
        Rect{ 0.0f, Limit<f32>::s_Infinity, 1.0f, 1.0f },
        Rect{ 0.0f, 0.0f, -1.0f, 1.0f },
        Rect{ 0.0f, 0.0f, 1.0f, -1.0f },
        Rect{ Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 1.0f },
        Rect{ Limit<f32>::s_Max, 0.0f, 1.0f, 1.0f },
        Rect{ 0.0f, Limit<f32>::s_Max, 1.0f, 1.0f }
    }){
        EXPECT_FALSE(ImageLayout::Place(invalid, normal));
        EXPECT_FALSE(ImageLayout::Place(normal, invalid));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

