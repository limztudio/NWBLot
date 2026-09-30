// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

static void ExpectRect(const Rect& actual, const Rect& expected){
    EXPECT_EQ(BitCast<u32>(actual.x), BitCast<u32>(expected.x));
    EXPECT_EQ(BitCast<u32>(actual.y), BitCast<u32>(expected.y));
    EXPECT_EQ(BitCast<u32>(actual.width), BitCast<u32>(expected.width));
    EXPECT_EQ(BitCast<u32>(actual.height), BitCast<u32>(expected.height));
}

static void ExpectPlacement(const ImagePlacement& actual, const ImagePlacement& expected){
    ExpectRect(actual.bounds, expected.bounds);
    ExpectRect(actual.clip, expected.clip);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiImageLayoutTests, NaturalPixelExtentUsesDensityWithoutImplicitRegionPadding){
    UiSkinRegion region = Sprite();
    region.padding = { 5.0f, 10.0f, 15.0f, 20.0f };
    ImageMetrics metrics;
    ASSERT_TRUE(ImageLayout::Measure({}, region, 2.0f, metrics));
    ExpectMetrics(metrics, { { 100.0f, 50.0f } });
    ASSERT_TRUE(ImageLayout::Measure({}, region, 0.5f, metrics));
    ExpectMetrics(metrics, { { 400.0f, 200.0f } });
}

TEST(UiImageLayoutTests, NineSliceMinimumsRemainLogicalAndCanExceedTheNaturalExtent){
    UiSkinRegion region = Sprite();
    region.drawMode = UiSkinDrawMode::NineSlice;
    region.sliceInsets = { 20u, 10u, 30u, 15u };
    region.minimumWidth = 120.0f;
    region.minimumHeight = 30.0f;
    ImageMetrics metrics;
    ASSERT_TRUE(ImageLayout::Measure({}, region, 2.0f, metrics));
    ExpectMetrics(metrics, { { 120.0f, 50.0f } });
    region.minimumHeight = 80.0f;
    ASSERT_TRUE(ImageLayout::Measure({}, region, 2.0f, metrics));
    ExpectMetrics(metrics, { { 120.0f, 80.0f } });
}

TEST(UiImageLayoutTests, FixedAndStretchPoliciesPreserveIntrinsicMetricsForTheLayoutTree){
    const UiSkinRegion region = Sprite();
    ImageOptions options;
    options.width = { LayoutSizePolicy::Fixed, 20.0f };
    options.height = { LayoutSizePolicy::Stretch, 2.0f };
    options.tint = { 2.0f, 0.0f, 0.5f, 0.0f };
    ImageMetrics metrics;
    ASSERT_TRUE(ImageLayout::Measure(options, region, 2.0f, metrics));
    ExpectMetrics(metrics, { { 100.0f, 50.0f } });
    options.height = { LayoutSizePolicy::Fixed, 0.0f };
    ASSERT_TRUE(ImageLayout::Measure(options, region, 2.0f, metrics));
    ExpectMetrics(metrics, { { 100.0f, 50.0f } });
}

TEST(UiImageLayoutTests, PlacementKeepsTheFullImageBoundsWhileIntersectingVisibility){
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 100.0f };
    ImagePlacement placement;
    ASSERT_TRUE(ImageLayout::Place(bounds, { 50.0f, 0.0f, 100.0f, 80.0f }, placement));
    ExpectRect(placement.bounds, bounds);
    ExpectRect(placement.clip, { 50.0f, 20.0f, 100.0f, 60.0f });
    ASSERT_TRUE(ImageLayout::Place(bounds, { 0.0f, 0.0f, 500.0f, 500.0f }, placement));
    ExpectRect(placement.clip, bounds);
}

TEST(UiImageLayoutTests, EmptyAndDisjointClipsRemainValidAtFiniteNegativeOrigins){
    const Rect bounds{ -100.0f, -50.0f, 200.0f, 100.0f };
    ImagePlacement placement;
    ASSERT_TRUE(ImageLayout::Place(bounds, { 150.0f, 0.0f, 20.0f, 20.0f }, placement));
    ExpectRect(placement.bounds, bounds);
    ExpectRect(placement.clip, { 150.0f, 0.0f, 0.0f, 20.0f });
    ASSERT_TRUE(ImageLayout::Place({ -10.0f, -20.0f, 0.0f, 0.0f }, bounds, placement));
    ExpectRect(placement.bounds, { -10.0f, -20.0f, 0.0f, 0.0f });
    ExpectRect(placement.clip, placement.bounds);
}

TEST(UiImageLayoutTests, SubnormalPositiveBoundsAndExtremeFiniteEndpointsRemainValid){
    const f32 tiny = BitCast<f32>(u32{ 1u });
    const Rect small{ 0.0f, 0.0f, tiny, tiny };
    ImagePlacement placement;
    ASSERT_TRUE(ImageLayout::Place(small, small, placement));
    ExpectRect(placement.bounds, small);
    ExpectRect(placement.clip, small);
    const Rect large{ -Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 1.0f };
    ASSERT_TRUE(ImageLayout::Place(large, large, placement));
    ExpectRect(placement.bounds, large);
    ExpectRect(placement.clip, large);
}

TEST(UiImageLayoutTests, InvalidLayoutSizesTintsAndDensityPreservePreviouslyMeasuredMetrics){
    const UiSkinRegion region = Sprite();
    ImageMetrics measured;
    ASSERT_TRUE(ImageLayout::Measure({}, region, 2.0f, measured));
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
        ImageMetrics output = measured;
        EXPECT_FALSE(ImageLayout::Measure(options, region, density, output));
        ExpectMetrics(output, measured);
    }
}

TEST(UiImageLayoutTests, InvalidRegionExtentMinimumsAndSliceMetadataRejectAtomically){
    const UiSkinRegion valid = Sprite();
    ImageMetrics measured;
    ASSERT_TRUE(ImageLayout::Measure({}, valid, 2.0f, measured));
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
        ImageMetrics output = measured;
        EXPECT_FALSE(ImageLayout::Measure({}, region, 2.0f, output));
        ExpectMetrics(output, measured);
    }
    UiSkinRegion region = valid;
    region.drawMode = UiSkinDrawMode::NineSlice;
    region.rectangle.width = Limit<u32>::s_Max;
    region.sliceInsets.left = Limit<u32>::s_Max;
    region.sliceInsets.right = Limit<u32>::s_Max;
    ImageMetrics output = measured;
    EXPECT_FALSE(ImageLayout::Measure({}, region, 2.0f, output));
    ExpectMetrics(output, measured);
}

TEST(UiImageLayoutTests, UnrepresentableNaturalExtentRejectsWithoutChangingMetrics){
    const UiSkinRegion region = Sprite();
    ImageMetrics measured;
    ASSERT_TRUE(ImageLayout::Measure({}, region, 2.0f, measured));
    ImageMetrics output = measured;
    EXPECT_FALSE(ImageLayout::Measure({}, region, BitCast<f32>(u32{ 1u }), output));
    ExpectMetrics(output, measured);
}

TEST(UiImageLayoutTests, InvalidAndCollapsedProspectiveEndpointsPreserveBothOutputRectangles){
    const Rect normal{ 10.0f, 20.0f, 200.0f, 100.0f };
    ImagePlacement measured;
    ASSERT_TRUE(ImageLayout::Place(normal, normal, measured));
    for(const Rect invalid : {
        Rect{ Limit<f32>::s_QuietNaN, 0.0f, 1.0f, 1.0f },
        Rect{ 0.0f, Limit<f32>::s_Infinity, 1.0f, 1.0f },
        Rect{ 0.0f, 0.0f, -1.0f, 1.0f },
        Rect{ 0.0f, 0.0f, 1.0f, -1.0f },
        Rect{ Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 1.0f },
        Rect{ Limit<f32>::s_Max, 0.0f, 1.0f, 1.0f },
        Rect{ 0.0f, Limit<f32>::s_Max, 1.0f, 1.0f }
    }){
        ImagePlacement output = measured;
        EXPECT_FALSE(ImageLayout::Place(invalid, normal, output));
        ExpectPlacement(output, measured);
        EXPECT_FALSE(ImageLayout::Place(normal, invalid, output));
        ExpectPlacement(output, measured);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

