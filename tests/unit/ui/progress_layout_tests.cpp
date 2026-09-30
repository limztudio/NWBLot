// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/progress.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_progress_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;
using namespace Impl::Ui;


static UiSkinRegion NineSlice(){
    UiSkinRegion region;
    region.name = Name("progress.test");
    region.rectangle = { 0u, 0u, 24u, 24u };
    region.sliceInsets = { 6u, 6u, 6u, 6u };
    region.padding = { 8.0f, 8.0f, 8.0f, 8.0f };
    region.minimumWidth = 12.0f;
    region.minimumHeight = 12.0f;
    region.drawMode = UiSkinDrawMode::NineSlice;
    return region;
}

static void ExpectPoint(const Point& actual, const Point& expected){
    EXPECT_EQ(BitCast<u32>(actual.x), BitCast<u32>(expected.x));
    EXPECT_EQ(BitCast<u32>(actual.y), BitCast<u32>(expected.y));
}

static void ExpectPadding(const Insets& actual, const Insets& expected){
    EXPECT_EQ(BitCast<u32>(actual.left), BitCast<u32>(expected.left));
    EXPECT_EQ(BitCast<u32>(actual.top), BitCast<u32>(expected.top));
    EXPECT_EQ(BitCast<u32>(actual.right), BitCast<u32>(expected.right));
    EXPECT_EQ(BitCast<u32>(actual.bottom), BitCast<u32>(expected.bottom));
}

static void ExpectRect(const Rect& actual, const Rect& expected){
    EXPECT_EQ(BitCast<u32>(actual.x), BitCast<u32>(expected.x));
    EXPECT_EQ(BitCast<u32>(actual.y), BitCast<u32>(expected.y));
    EXPECT_EQ(BitCast<u32>(actual.width), BitCast<u32>(expected.width));
    EXPECT_EQ(BitCast<u32>(actual.height), BitCast<u32>(expected.height));
}

static void ExpectMetrics(const ProgressMetrics& actual, const ProgressMetrics& expected){
    ExpectPadding(actual.padding, expected.padding);
    ExpectPoint(actual.fillMinimum, expected.fillMinimum);
    ExpectPoint(actual.contentSize, expected.contentSize);
}

static void ExpectPlacement(const ProgressPlacement& actual, const ProgressPlacement& expected){
    ExpectRect(actual.bounds, expected.bounds);
    ExpectRect(actual.clip, expected.clip);
    ExpectRect(actual.content, expected.content);
    ExpectRect(actual.fillReveal, expected.fillReveal);
    ExpectRect(actual.fillCanvas, expected.fillCanvas);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiProgressLayoutTests, DefaultAtlasMetricsIncludeTrackPaddingAroundTheFillMinimum){
    const UiSkinRegion region = NineSlice();
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 1.0f, metrics));
    ExpectPadding(metrics.padding, { 8.0f, 8.0f, 8.0f, 8.0f });
    ExpectPoint(metrics.fillMinimum, { 12.0f, 12.0f });
    ExpectPoint(metrics.contentSize, { 28.0f, 32.0f });
}

TEST(UiProgressLayoutTests, ReferenceDensityScalesPixelSliceBordersAndLeavesLogicalPaddingUnchanged){
    UiSkinRegion region = NineSlice();
    region.minimumWidth = 0.0f;
    region.minimumHeight = 0.0f;
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 2.0f, metrics));
    ExpectPoint(metrics.fillMinimum, { 6.0f, 6.0f });
    ExpectPadding(metrics.padding, { 8.0f, 8.0f, 8.0f, 8.0f });
    ExpectPoint(metrics.contentSize, { 22.0f, 32.0f });
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 0.5f, metrics));
    ExpectPoint(metrics.fillMinimum, { 24.0f, 24.0f });
    ExpectPoint(metrics.contentSize, { 40.0f, 40.0f });
}

TEST(UiProgressLayoutTests, LogicalRegionMinimumsCanExceedSliceBordersWithoutDensityScaling){
    UiSkinRegion track = NineSlice();
    UiSkinRegion fill = NineSlice();
    track.minimumWidth = 80.0f;
    track.minimumHeight = 60.0f;
    fill.minimumWidth = 30.0f;
    fill.minimumHeight = 20.0f;
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, track, fill, 4.0f, metrics));
    ExpectPoint(metrics.fillMinimum, { 30.0f, 20.0f });
    ExpectPoint(metrics.contentSize, { 80.0f, 60.0f });
}

TEST(UiProgressLayoutTests, EffectivePaddingUsesEachMaximumAndRequestedHeightCanGrowTheTrack){
    UiSkinRegion track = NineSlice();
    const UiSkinRegion fill = NineSlice();
    track.padding = { 2.0f, 9.0f, 3.0f, 4.0f };
    ProgressStyle style;
    style.padding = { 5.0f, 1.0f, 7.0f, 10.0f };
    ProgressOptions options;
    options.height = 70.0f;
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure(options, style, track, fill, 1.0f, metrics));
    ExpectPadding(metrics.padding, { 5.0f, 9.0f, 7.0f, 10.0f });
    ExpectPoint(metrics.contentSize, { 24.0f, 70.0f });
}

TEST(UiProgressLayoutTests, FillMetadataPaddingDoesNotInsetTheProgressAmountTwice){
    const UiSkinRegion track = NineSlice();
    UiSkinRegion fill = NineSlice();
    fill.padding = { 100.0f, 200.0f, 300.0f, 400.0f };
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, track, fill, 1.0f, metrics));
    ExpectPadding(metrics.padding, { 8.0f, 8.0f, 8.0f, 8.0f });
    ExpectPoint(metrics.contentSize, { 28.0f, 32.0f });
}

TEST(UiProgressLayoutTests, SpriteRegionsUseTheirLogicalMinimumWithoutInventingSliceBorders){
    UiSkinRegion region;
    region.rectangle = { 0u, 0u, 200u, 100u };
    region.minimumWidth = 10.0f;
    region.minimumHeight = 5.0f;
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 2.0f, metrics));
    ExpectPadding(metrics.padding, {});
    ExpectPoint(metrics.fillMinimum, { 10.0f, 5.0f });
    ExpectPoint(metrics.contentSize, { 10.0f, 32.0f });
}

TEST(UiProgressLayoutTests, PlacementUsesPaddedContentAndPartialFillGeometry){
    const UiSkinRegion region = NineSlice();
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 1.0f, metrics));
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    ProgressPlacement placement;
    ASSERT_TRUE(ProgressLayout::Place(bounds, bounds, metrics, 0.25, placement));
    ExpectRect(placement.bounds, bounds);
    ExpectRect(placement.clip, bounds);
    ExpectRect(placement.content, { 18.0f, 28.0f, 184.0f, 16.0f });
    ExpectRect(placement.fillReveal, { 18.0f, 28.0f, 46.0f, 16.0f });
    ExpectRect(placement.fillCanvas, placement.fillReveal);
}

TEST(UiProgressLayoutTests, ZeroSkipsFillAndFullCopiesTheExactContentEndpoints){
    const UiSkinRegion region = NineSlice();
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 1.0f, metrics));
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    ProgressPlacement zero;
    ProgressPlacement full;
    ASSERT_TRUE(ProgressLayout::Place(bounds, bounds, metrics, 0.0, zero));
    ASSERT_TRUE(ProgressLayout::Place(bounds, bounds, metrics, 1.0, full));
    ExpectRect(zero.fillReveal, { 18.0f, 28.0f, 0.0f, 16.0f });
    ExpectRect(zero.fillCanvas, zero.fillReveal);
    ExpectRect(full.fillReveal, full.content);
    ExpectRect(full.fillCanvas, full.content);
    EXPECT_EQ(BitCast<u32>(full.fillReveal.x + full.fillReveal.width), BitCast<u32>(full.content.x + full.content.width));
}

TEST(UiProgressLayoutTests, FiniteFractionsOutsideTheUnitIntervalClampVisually){
    const UiSkinRegion region = NineSlice();
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 1.0f, metrics));
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    for(const f64 fraction : { -Limit<f64>::s_Max, -0.1, 1.1, Limit<f64>::s_Max }){
        ProgressPlacement placement;
        ASSERT_TRUE(ProgressLayout::Place(bounds, bounds, metrics, fraction, placement));
        if(fraction < 0.0){
            EXPECT_FLOAT_EQ(placement.fillReveal.width, 0.0f);
            EXPECT_FLOAT_EQ(placement.fillCanvas.width, 0.0f);
        }
        else{
            ExpectRect(placement.fillReveal, placement.content);
            ExpectRect(placement.fillCanvas, placement.content);
        }
    }
}

TEST(UiProgressLayoutTests, SmallFractionsRevealOnlyPartOfAMinimumSizedSkinCanvas){
    const UiSkinRegion region = NineSlice();
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 1.0f, metrics));
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    ProgressPlacement placement;
    ASSERT_TRUE(ProgressLayout::Place(bounds, bounds, metrics, 0.01, placement));
    EXPECT_FLOAT_EQ(placement.fillReveal.width, 1.84f);
    ExpectRect(placement.fillCanvas, { 18.0f, 28.0f, 12.0f, 16.0f });
    EXPECT_GT(placement.fillCanvas.width, placement.fillReveal.width);
    EXPECT_LT(placement.fillCanvas.width, placement.content.width);
}

TEST(UiProgressLayoutTests, AMinimumCanvasNeverExtendsPastContentNarrowerThanTheSkinMinimum){
    const UiSkinRegion region = NineSlice();
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 1.0f, metrics));
    const Rect bounds{ 10.0f, 20.0f, 20.0f, 32.0f };
    ProgressPlacement placement;
    ASSERT_TRUE(ProgressLayout::Place(bounds, bounds, metrics, 0.5, placement));
    ExpectRect(placement.content, { 18.0f, 28.0f, 4.0f, 16.0f });
    ExpectRect(placement.fillReveal, { 18.0f, 28.0f, 2.0f, 16.0f });
    ExpectRect(placement.fillCanvas, placement.content);
}

TEST(UiProgressLayoutTests, ClippingChangesVisibilityWithoutChangingTheProgressAmountOrCanvas){
    const UiSkinRegion region = NineSlice();
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 1.0f, metrics));
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    ProgressPlacement full;
    ProgressPlacement clipped;
    ASSERT_TRUE(ProgressLayout::Place(bounds, bounds, metrics, 0.25, full));
    ASSERT_TRUE(ProgressLayout::Place(bounds, { 40.0f, 24.0f, 60.0f, 12.0f }, metrics, 0.25, clipped));
    ExpectRect(clipped.clip, { 40.0f, 24.0f, 60.0f, 12.0f });
    ExpectRect(clipped.content, full.content);
    ExpectRect(clipped.fillReveal, full.fillReveal);
    ExpectRect(clipped.fillCanvas, full.fillCanvas);
}

TEST(UiProgressLayoutTests, ACompletelyDisjointClipProducesValidEmptyVisibility){
    const UiSkinRegion region = NineSlice();
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 1.0f, metrics));
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    ProgressPlacement placement;
    ASSERT_TRUE(ProgressLayout::Place(bounds, { 300.0f, 20.0f, 10.0f, 10.0f }, metrics, 0.5, placement));
    EXPECT_FLOAT_EQ(placement.clip.width, 0.0f);
    EXPECT_FLOAT_EQ(placement.fillReveal.width, 92.0f);
    EXPECT_FLOAT_EQ(placement.fillCanvas.width, 92.0f);
}

TEST(UiProgressLayoutTests, TinyAndEmptyAreasBoundPaddingWithoutNegativeContent){
    const UiSkinRegion region = NineSlice();
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 1.0f, metrics));
    for(const Rect bounds : { Rect{ 10.0f, 20.0f, 4.0f, 3.0f }, Rect{ 10.0f, 20.0f, 0.0f, 0.0f } }){
        ProgressPlacement placement;
        ASSERT_TRUE(ProgressLayout::Place(bounds, bounds, metrics, 0.5, placement));
        EXPECT_FLOAT_EQ(placement.content.width, 0.0f);
        EXPECT_FLOAT_EQ(placement.content.height, 0.0f);
        EXPECT_FLOAT_EQ(placement.fillReveal.width, 0.0f);
        EXPECT_FLOAT_EQ(placement.fillCanvas.width, 0.0f);
        EXPECT_LE(placement.content.x, bounds.x + bounds.width);
        EXPECT_LE(placement.content.y, bounds.y + bounds.height);
    }
}

TEST(UiProgressLayoutTests, PositiveSubnormalAndUnrepresentableRevealEdgesRemainValidEmptyFill){
    const UiSkinRegion region = NineSlice();
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 1.0f, metrics));
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    for(const f64 fraction : { BitCast<f64>(u64{ 1u }), 1.0e-15 }){
        ProgressPlacement placement;
        ASSERT_TRUE(ProgressLayout::Place(bounds, bounds, metrics, fraction, placement));
        EXPECT_FLOAT_EQ(placement.fillReveal.width, 0.0f);
        EXPECT_FLOAT_EQ(placement.fillCanvas.width, 0.0f);
        ExpectRect(placement.content, { 18.0f, 28.0f, 184.0f, 16.0f });
    }
}

TEST(UiProgressLayoutTests, RepresentableFloatSubnormalRevealIsPreservedAtTheOrigin){
    ProgressMetrics metrics;
    metrics.contentSize = { 1.0f, 1.0f };
    const Rect bounds{ 0.0f, 0.0f, 1.0f, 1.0f };
    ProgressPlacement placement;
    const f32 leastPositive = BitCast<f32>(u32{ 1u });
    ASSERT_TRUE(ProgressLayout::Place(bounds, bounds, metrics, leastPositive, placement));
    EXPECT_EQ(BitCast<u32>(placement.fillReveal.width), u32{ 1u });
    ExpectRect(placement.fillCanvas, placement.fillReveal);
}

TEST(UiProgressLayoutTests, ZeroIntrinsicMetricsAndEmptyBoundsAreValid){
    UiSkinRegion region;
    region.rectangle = { 0u, 0u, 1u, 1u };
    ProgressOptions options;
    options.height = 0.0f;
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure(options, {}, region, region, 1.0f, metrics));
    ExpectPoint(metrics.contentSize, {});
    ProgressPlacement placement;
    ASSERT_TRUE(ProgressLayout::Place({}, {}, metrics, 1.0, placement));
    ExpectRect(placement.content, {});
    ExpectRect(placement.fillReveal, {});
    ExpectRect(placement.fillCanvas, {});
}

TEST(UiProgressLayoutTests, InvalidOptionsPaddingAndTintsPreserveAllPreviouslyMeasuredMetrics){
    const UiSkinRegion region = NineSlice();
    ProgressMetrics measured;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 1.0f, measured));
    for(u32 field = 0u; field < 14u; ++field){
        ProgressOptions options;
        ProgressStyle style;
        switch(field){
        case 0u: options.width.policy = static_cast<LayoutSizePolicy::Enum>(3u); break;
        case 1u: options.width.value = 0.0f; break;
        case 2u: options.width.value = -1.0f; break;
        case 3u: options.width.value = Limit<f32>::s_Infinity; break;
        case 4u: options.height = -1.0f; break;
        case 5u: options.height = Limit<f32>::s_QuietNaN; break;
        case 6u: style.padding.left = -1.0f; break;
        case 7u: style.padding.bottom = Limit<f32>::s_Infinity; break;
        case 8u: style.trackTint.r = -1.0f; break;
        case 9u: style.trackTint.a = 1.01f; break;
        case 10u: style.fillTint.g = Limit<f32>::s_QuietNaN; break;
        case 11u: style.fillTint.a = -0.01f; break;
        case 12u: style.fillTint.b = Limit<f32>::s_Infinity; break;
        default: style.trackTint.a = Limit<f32>::s_QuietNaN; break;
        }
        ProgressMetrics output = measured;
        EXPECT_FALSE(ProgressLayout::Measure(options, style, region, region, 1.0f, output));
        ExpectMetrics(output, measured);
    }
}

TEST(UiProgressLayoutTests, InvalidRegionMetricsSlicesModesAndDensityRejectAtomically){
    const UiSkinRegion valid = NineSlice();
    ProgressMetrics measured;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, valid, valid, 1.0f, measured));
    for(u32 field = 0u; field < 15u; ++field){
        UiSkinRegion track = valid;
        UiSkinRegion fill = valid;
        f32 density = 1.0f;
        switch(field){
        case 0u: density = 0.0f; break;
        case 1u: density = -1.0f; break;
        case 2u: density = Limit<f32>::s_Infinity; break;
        case 3u: density = Limit<f32>::s_QuietNaN; break;
        case 4u: track.minimumWidth = -1.0f; break;
        case 5u: fill.minimumHeight = Limit<f32>::s_QuietNaN; break;
        case 6u: track.padding.left = -1.0f; break;
        case 7u: fill.padding.right = Limit<f32>::s_Infinity; break;
        case 8u: track.rectangle.width = 0u; break;
        case 9u: fill.rectangle.height = 0u; break;
        case 10u: track.sliceInsets.left = 25u; break;
        case 11u: fill.sliceInsets.bottom = 25u; break;
        case 12u: track.drawMode = static_cast<UiSkinDrawMode::Enum>(2u); break;
        case 13u: fill.drawMode = UiSkinDrawMode::Sprite; break;
        default: fill.minimumWidth = Limit<f32>::s_Infinity; break;
        }
        ProgressMetrics output = measured;
        EXPECT_FALSE(ProgressLayout::Measure({}, {}, track, fill, density, output));
        ExpectMetrics(output, measured);
    }
}

TEST(UiProgressLayoutTests, OverflowingLogicalSumsAndDensityScaledBordersPreserveMetrics){
    const UiSkinRegion valid = NineSlice();
    ProgressMetrics measured;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, valid, valid, 1.0f, measured));
    for(u32 field = 0u; field < 4u; ++field){
        UiSkinRegion region = valid;
        ProgressStyle style;
        f32 density = 1.0f;
        switch(field){
        case 0u:
            style.padding.left = Limit<f32>::s_Max;
            style.padding.right = Limit<f32>::s_Max;
            break;
        case 1u:
            region.minimumWidth = Limit<f32>::s_Max;
            region.padding.left = Limit<f32>::s_Max;
            break;
        case 2u:
            density = BitCast<f32>(u32{ 1u });
            break;
        default:
            region.rectangle.width = Limit<u32>::s_Max;
            region.sliceInsets.left = Limit<u32>::s_Max;
            region.sliceInsets.right = Limit<u32>::s_Max;
            break;
        }
        ProgressMetrics output = measured;
        EXPECT_FALSE(ProgressLayout::Measure({}, style, region, region, density, output));
        ExpectMetrics(output, measured);
    }
}

TEST(UiProgressLayoutTests, InvalidProspectiveRectanglesMetricsAndFractionsPreserveEveryOutputRectangle){
    const UiSkinRegion region = NineSlice();
    ProgressMetrics metrics;
    ASSERT_TRUE(ProgressLayout::Measure({}, {}, region, region, 1.0f, metrics));
    const Rect normal{ 10.0f, 20.0f, 200.0f, 32.0f };
    ProgressPlacement measured;
    ASSERT_TRUE(ProgressLayout::Place(normal, normal, metrics, 0.25, measured));
    for(u32 field = 0u; field < 13u; ++field){
        Rect bounds = normal;
        Rect clip = normal;
        ProgressMetrics invalid = metrics;
        f64 fraction = 0.5;
        switch(field){
        case 0u: bounds.x = Limit<f32>::s_QuietNaN; break;
        case 1u: bounds.width = -1.0f; break;
        case 2u: bounds.height = Limit<f32>::s_Infinity; break;
        case 3u: clip.y = Limit<f32>::s_Infinity; break;
        case 4u: clip.height = -1.0f; break;
        case 5u: invalid.padding.top = -1.0f; break;
        case 6u: invalid.fillMinimum.x = -1.0f; break;
        case 7u: invalid.fillMinimum.y = Limit<f32>::s_QuietNaN; break;
        case 8u: invalid.contentSize.x = 1.0f; break;
        case 9u: invalid.contentSize.y = Limit<f32>::s_Infinity; break;
        case 10u: fraction = Limit<f64>::s_Infinity; break;
        case 11u: fraction = -Limit<f64>::s_Infinity; break;
        default: fraction = Limit<f64>::s_QuietNaN; break;
        }
        ProgressPlacement output = measured;
        EXPECT_FALSE(ProgressLayout::Place(bounds, clip, invalid, fraction, output));
        ExpectPlacement(output, measured);
    }
}

TEST(UiProgressLayoutTests, FiniteExtremeCoordinatesRetainValidExactFullEndpoints){
    ProgressMetrics metrics;
    metrics.contentSize = { Limit<f32>::s_Max, 32.0f };
    const Rect bounds{ -Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 32.0f };
    ProgressPlacement placement;
    ASSERT_TRUE(ProgressLayout::Place(bounds, bounds, metrics, 1.0, placement));
    ExpectRect(placement.content, bounds);
    ExpectRect(placement.fillReveal, bounds);
    ExpectRect(placement.fillCanvas, bounds);
    ASSERT_TRUE(ProgressLayout::Place(bounds, bounds, metrics, 0.5, placement));
    EXPECT_FLOAT_EQ(placement.fillReveal.width, Limit<f32>::s_Max * 0.5f);
    EXPECT_FLOAT_EQ(placement.fillReveal.x, -Limit<f32>::s_Max);
}

TEST(UiProgressLayoutTests, OverflowingAndCollapsedPositiveInputEndpointsRejectAtomically){
    ProgressMetrics metrics;
    metrics.contentSize = { 1.0f, 1.0f };
    const Rect normal{ 0.0f, 0.0f, 20.0f, 10.0f };
    ProgressPlacement measured;
    ASSERT_TRUE(ProgressLayout::Place(normal, normal, metrics, 0.5, measured));
    for(const Rect invalid : {
        Rect{ Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 1.0f },
        Rect{ Limit<f32>::s_Max, 0.0f, 1.0f, 1.0f },
        Rect{ 0.0f, Limit<f32>::s_Max, 1.0f, 1.0f }
    }){
        ProgressPlacement output = measured;
        EXPECT_FALSE(ProgressLayout::Place(invalid, normal, metrics, 0.5, output));
        ExpectPlacement(output, measured);
        EXPECT_FALSE(ProgressLayout::Place(normal, invalid, metrics, 0.5, output));
        ExpectPlacement(output, measured);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

