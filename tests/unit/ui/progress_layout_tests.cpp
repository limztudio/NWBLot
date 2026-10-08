// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiProgressLayoutTests, LogicalRegionMinimumsCanExceedSliceBordersWithoutDensityScaling){
    UiSkinRegion track = NineSlice();
    UiSkinRegion fill = NineSlice();
    track.minimumWidth = 80.0f;
    track.minimumHeight = 60.0f;
    fill.minimumWidth = 30.0f;
    fill.minimumHeight = 20.0f;
    auto metrics = ProgressLayout::Measure({}, {}, track, fill, 4.0f);
    ASSERT_TRUE(metrics);
    ExpectPoint(metrics->fillMinimum, { 30.0f, 20.0f });
    ExpectPoint(metrics->contentSize, { 80.0f, 60.0f });
}

TEST(UiProgressLayoutTests, FillMetadataPaddingDoesNotInsetTheProgressAmountTwice){
    const UiSkinRegion track = NineSlice();
    UiSkinRegion fill = NineSlice();
    fill.padding = { 100.0f, 200.0f, 300.0f, 400.0f };
    auto metrics = ProgressLayout::Measure({}, {}, track, fill, 1.0f);
    ASSERT_TRUE(metrics);
    ExpectPadding(metrics->padding, { 8.0f, 8.0f, 8.0f, 8.0f });
    ExpectPoint(metrics->contentSize, { 28.0f, 32.0f });
}

TEST(UiProgressLayoutTests, ZeroSkipsFillAndFullCopiesTheExactContentEndpoints){
    const UiSkinRegion region = NineSlice();
    auto metrics = ProgressLayout::Measure({}, {}, region, region, 1.0f);
    ASSERT_TRUE(metrics);
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    auto zero = ProgressLayout::Place(bounds, bounds, *metrics, 0.0);
    ASSERT_TRUE(zero);
    auto full = ProgressLayout::Place(bounds, bounds, *metrics, 1.0);
    ASSERT_TRUE(full);
    UiWidgetTests::ExpectRectExact(zero->fillReveal, { 18.0f, 28.0f, 0.0f, 16.0f });
    UiWidgetTests::ExpectRectExact(zero->fillCanvas, zero->fillReveal);
    UiWidgetTests::ExpectRectExact(full->fillReveal, full->content);
    UiWidgetTests::ExpectRectExact(full->fillCanvas, full->content);
    EXPECT_EQ(BitCast<u32>(full->fillReveal.x + full->fillReveal.width), BitCast<u32>(full->content.x + full->content.width));
}

TEST(UiProgressLayoutTests, FiniteFractionsOutsideTheUnitIntervalClampVisually){
    const UiSkinRegion region = NineSlice();
    auto metrics = ProgressLayout::Measure({}, {}, region, region, 1.0f);
    ASSERT_TRUE(metrics);
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    for(const f64 fraction : { -Limit<f64>::s_Max, -0.1, 1.1, Limit<f64>::s_Max }){
        auto placement = ProgressLayout::Place(bounds, bounds, *metrics, fraction);
        ASSERT_TRUE(placement);
        if(fraction < 0.0){
            EXPECT_FLOAT_EQ(placement->fillReveal.width, 0.0f);
            EXPECT_FLOAT_EQ(placement->fillCanvas.width, 0.0f);
        }
        else{
            UiWidgetTests::ExpectRectExact(placement->fillReveal, placement->content);
            UiWidgetTests::ExpectRectExact(placement->fillCanvas, placement->content);
        }
    }
}

TEST(UiProgressLayoutTests, SmallFractionsRevealOnlyPartOfAMinimumSizedSkinCanvas){
    const UiSkinRegion region = NineSlice();
    auto metrics = ProgressLayout::Measure({}, {}, region, region, 1.0f);
    ASSERT_TRUE(metrics);
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    auto placement = ProgressLayout::Place(bounds, bounds, *metrics, 0.01);
    ASSERT_TRUE(placement);
    EXPECT_FLOAT_EQ(placement->fillReveal.width, 1.84f);
    UiWidgetTests::ExpectRectExact(placement->fillCanvas, { 18.0f, 28.0f, 12.0f, 16.0f });
    EXPECT_GT(placement->fillCanvas.width, placement->fillReveal.width);
    EXPECT_LT(placement->fillCanvas.width, placement->content.width);
}

TEST(UiProgressLayoutTests, AMinimumCanvasNeverExtendsPastContentNarrowerThanTheSkinMinimum){
    const UiSkinRegion region = NineSlice();
    auto metrics = ProgressLayout::Measure({}, {}, region, region, 1.0f);
    ASSERT_TRUE(metrics);
    const Rect bounds{ 10.0f, 20.0f, 20.0f, 32.0f };
    auto placement = ProgressLayout::Place(bounds, bounds, *metrics, 0.5);
    ASSERT_TRUE(placement);
    UiWidgetTests::ExpectRectExact(placement->content, { 18.0f, 28.0f, 4.0f, 16.0f });
    UiWidgetTests::ExpectRectExact(placement->fillReveal, { 18.0f, 28.0f, 2.0f, 16.0f });
    UiWidgetTests::ExpectRectExact(placement->fillCanvas, placement->content);
}

TEST(UiProgressLayoutTests, ClippingChangesVisibilityWithoutChangingTheProgressAmountOrCanvas){
    const UiSkinRegion region = NineSlice();
    auto metrics = ProgressLayout::Measure({}, {}, region, region, 1.0f);
    ASSERT_TRUE(metrics);
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    auto full = ProgressLayout::Place(bounds, bounds, *metrics, 0.25);
    ASSERT_TRUE(full);
    auto clipped = ProgressLayout::Place(bounds, { 40.0f, 24.0f, 60.0f, 12.0f }, *metrics, 0.25);
    ASSERT_TRUE(clipped);
    UiWidgetTests::ExpectRectExact(clipped->clip, { 40.0f, 24.0f, 60.0f, 12.0f });
    UiWidgetTests::ExpectRectExact(clipped->content, full->content);
    UiWidgetTests::ExpectRectExact(clipped->fillReveal, full->fillReveal);
    UiWidgetTests::ExpectRectExact(clipped->fillCanvas, full->fillCanvas);
}

TEST(UiProgressLayoutTests, ACompletelyDisjointClipProducesValidEmptyVisibility){
    const UiSkinRegion region = NineSlice();
    auto metrics = ProgressLayout::Measure({}, {}, region, region, 1.0f);
    ASSERT_TRUE(metrics);
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    auto placement = ProgressLayout::Place(bounds, { 300.0f, 20.0f, 10.0f, 10.0f }, *metrics, 0.5);
    ASSERT_TRUE(placement);
    EXPECT_FLOAT_EQ(placement->clip.width, 0.0f);
    EXPECT_FLOAT_EQ(placement->fillReveal.width, 92.0f);
    EXPECT_FLOAT_EQ(placement->fillCanvas.width, 92.0f);
}

TEST(UiProgressLayoutTests, TinyAndEmptyAreasBoundPaddingWithoutNegativeContent){
    const UiSkinRegion region = NineSlice();
    auto metrics = ProgressLayout::Measure({}, {}, region, region, 1.0f);
    ASSERT_TRUE(metrics);
    for(const Rect bounds : { Rect{ 10.0f, 20.0f, 4.0f, 3.0f }, Rect{ 10.0f, 20.0f, 0.0f, 0.0f } }){
        auto placement = ProgressLayout::Place(bounds, bounds, *metrics, 0.5);
        ASSERT_TRUE(placement);
        EXPECT_FLOAT_EQ(placement->content.width, 0.0f);
        EXPECT_FLOAT_EQ(placement->content.height, 0.0f);
        EXPECT_FLOAT_EQ(placement->fillReveal.width, 0.0f);
        EXPECT_FLOAT_EQ(placement->fillCanvas.width, 0.0f);
        EXPECT_LE(placement->content.x, bounds.x + bounds.width);
        EXPECT_LE(placement->content.y, bounds.y + bounds.height);
    }
}

TEST(UiProgressLayoutTests, PositiveSubnormalAndUnrepresentableRevealEdgesRemainValidEmptyFill){
    const UiSkinRegion region = NineSlice();
    auto metrics = ProgressLayout::Measure({}, {}, region, region, 1.0f);
    ASSERT_TRUE(metrics);
    const Rect bounds{ 10.0f, 20.0f, 200.0f, 32.0f };
    for(const f64 fraction : { BitCast<f64>(u64{ 1u }), 1.0e-15 }){
        auto placement = ProgressLayout::Place(bounds, bounds, *metrics, fraction);
        ASSERT_TRUE(placement);
        EXPECT_FLOAT_EQ(placement->fillReveal.width, 0.0f);
        EXPECT_FLOAT_EQ(placement->fillCanvas.width, 0.0f);
        UiWidgetTests::ExpectRectExact(placement->content, { 18.0f, 28.0f, 184.0f, 16.0f });
    }
}

TEST(UiProgressLayoutTests, RepresentableFloatSubnormalRevealIsPreservedAtTheOrigin){
    ProgressMetrics metrics;
    metrics.contentSize = { 1.0f, 1.0f };
    const Rect bounds{ 0.0f, 0.0f, 1.0f, 1.0f };
    const f32 leastPositive = BitCast<f32>(u32{ 1u });
    auto placement = ProgressLayout::Place(bounds, bounds, metrics, leastPositive);
    ASSERT_TRUE(placement);
    EXPECT_EQ(BitCast<u32>(placement->fillReveal.width), u32{ 1u });
    UiWidgetTests::ExpectRectExact(placement->fillCanvas, placement->fillReveal);
}

TEST(UiProgressLayoutTests, ZeroIntrinsicMetricsAndEmptyBoundsAreValid){
    UiSkinRegion region;
    region.rectangle = { 0u, 0u, 1u, 1u };
    ProgressOptions options;
    options.height = 0.0f;
    auto metrics = ProgressLayout::Measure(options, {}, region, region, 1.0f);
    ASSERT_TRUE(metrics);
    ExpectPoint(metrics->contentSize, {});
    auto placement = ProgressLayout::Place({}, {}, *metrics, 1.0);
    ASSERT_TRUE(placement);
    UiWidgetTests::ExpectRectExact(placement->content, {});
    UiWidgetTests::ExpectRectExact(placement->fillReveal, {});
    UiWidgetTests::ExpectRectExact(placement->fillCanvas, {});
}

TEST(UiProgressLayoutTests, InvalidOptionsPaddingAndTintsRejectMetrics){
    const UiSkinRegion region = NineSlice();
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
        EXPECT_FALSE(ProgressLayout::Measure(options, style, region, region, 1.0f));
    }
}

TEST(UiProgressLayoutTests, InvalidRegionMetricsSlicesModesAndDensityRejectMetrics){
    const UiSkinRegion valid = NineSlice();
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
        EXPECT_FALSE(ProgressLayout::Measure({}, {}, track, fill, density));
    }
}

TEST(UiProgressLayoutTests, OverflowingLogicalSumsAndDensityScaledBordersRejectMetrics){
    const UiSkinRegion valid = NineSlice();
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
        EXPECT_FALSE(ProgressLayout::Measure({}, style, region, region, density));
    }
}

TEST(UiProgressLayoutTests, InvalidProspectiveRectanglesMetricsAndFractionsRejectPlacement){
    const UiSkinRegion region = NineSlice();
    auto metrics = ProgressLayout::Measure({}, {}, region, region, 1.0f);
    ASSERT_TRUE(metrics);
    const Rect normal{ 10.0f, 20.0f, 200.0f, 32.0f };
    for(u32 field = 0u; field < 13u; ++field){
        Rect bounds = normal;
        Rect clip = normal;
        ProgressMetrics invalid = *metrics;
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
        EXPECT_FALSE(ProgressLayout::Place(bounds, clip, invalid, fraction));
    }
}

TEST(UiProgressLayoutTests, FiniteExtremeCoordinatesRetainValidExactFullEndpoints){
    ProgressMetrics metrics;
    metrics.contentSize = { Limit<f32>::s_Max, 32.0f };
    const Rect bounds{ -Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 32.0f };
    auto placement = ProgressLayout::Place(bounds, bounds, metrics, 1.0);
    ASSERT_TRUE(placement);
    UiWidgetTests::ExpectRectExact(placement->content, bounds);
    UiWidgetTests::ExpectRectExact(placement->fillReveal, bounds);
    UiWidgetTests::ExpectRectExact(placement->fillCanvas, bounds);
    placement = ProgressLayout::Place(bounds, bounds, metrics, 0.5);
    ASSERT_TRUE(placement);
    EXPECT_FLOAT_EQ(placement->fillReveal.width, Limit<f32>::s_Max * 0.5f);
    EXPECT_FLOAT_EQ(placement->fillReveal.x, -Limit<f32>::s_Max);
}

TEST(UiProgressLayoutTests, OverflowingAndCollapsedPositiveInputEndpointsRejectPlacement){
    ProgressMetrics metrics;
    metrics.contentSize = { 1.0f, 1.0f };
    const Rect normal{ 0.0f, 0.0f, 20.0f, 10.0f };
    for(const Rect invalid : {
        Rect{ Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 1.0f },
        Rect{ Limit<f32>::s_Max, 0.0f, 1.0f, 1.0f },
        Rect{ 0.0f, Limit<f32>::s_Max, 1.0f, 1.0f }
    }){
        EXPECT_FALSE(ProgressLayout::Place(invalid, normal, metrics, 0.5));
        EXPECT_FALSE(ProgressLayout::Place(normal, invalid, metrics, 0.5));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

