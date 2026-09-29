// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ui/widgets/radio_group.h>
#include <impl/ui/widgets/radio_group_style.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;


[[nodiscard]] static RadioGroupChoices Choices(const u32 count){
    RadioGroupChoices choices;
    choices.sourceGeneration = 41u;
    choices.sourceRevision = 7u;
    choices.count = count;
    for(u32 index = 0u; index < count && index < s_RadioGroupMaxChoices; ++index)
        choices.rows[index] = { index + 1u, index % 2u == 0u };
    return choices;
}

static void ExpectRectangle(const Rect& actual, const Rect& expected){
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.width, expected.width);
    EXPECT_FLOAT_EQ(actual.height, expected.height);
}

static void ExpectMetrics(const RadioGroupMetrics& actual, const RadioGroupMetrics& expected){
    EXPECT_FLOAT_EQ(actual.rowHeight, expected.rowHeight);
    EXPECT_FLOAT_EQ(actual.indicatorExtent, expected.indicatorExtent);
    EXPECT_FLOAT_EQ(actual.gap, expected.gap);
    EXPECT_FLOAT_EQ(actual.rowGap, expected.rowGap);
    EXPECT_FLOAT_EQ(actual.markInset, expected.markInset);
    EXPECT_FLOAT_EQ(actual.padding.left, expected.padding.left);
    EXPECT_FLOAT_EQ(actual.padding.top, expected.padding.top);
    EXPECT_FLOAT_EQ(actual.padding.right, expected.padding.right);
    EXPECT_FLOAT_EQ(actual.padding.bottom, expected.padding.bottom);
    EXPECT_FLOAT_EQ(actual.contentSize.x, expected.contentSize.x);
    EXPECT_FLOAT_EQ(actual.contentSize.y, expected.contentSize.y);
    EXPECT_EQ(actual.count, expected.count);
}

static void ExpectPlacement(const RadioGroupPlacement& actual, const RadioGroupPlacement& expected){
    ExpectRectangle(actual.bounds, expected.bounds);
    ExpectRectangle(actual.clip, expected.clip);
    ExpectRectangle(actual.content, expected.content);
    EXPECT_EQ(actual.count, expected.count);
    for(u32 index = 0u; index < s_RadioGroupMaxChoices; ++index){
        EXPECT_EQ(actual.rows[index].key, expected.rows[index].key);
        EXPECT_EQ(actual.rows[index].enabled, expected.rows[index].enabled);
        ExpectRectangle(actual.rows[index].rectangle, expected.rows[index].rectangle);
        ExpectRectangle(actual.rows[index].clip, expected.rows[index].clip);
        ExpectRectangle(actual.rows[index].indicator, expected.rows[index].indicator);
        ExpectRectangle(actual.rows[index].mark, expected.rows[index].mark);
        ExpectRectangle(actual.rows[index].textClip, expected.rows[index].textClip);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiRadioGroupLayoutTests, DefaultMetricsMeasureTheEntireGroupIncludingPaddingAndGaps){
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, {}, metrics));
    EXPECT_FLOAT_EQ(metrics.rowHeight, 32.0f);
    EXPECT_FLOAT_EQ(metrics.indicatorExtent, 24.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.x, 140.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 112.0f);
    EXPECT_EQ(metrics.count, 3u);
}

TEST(UiRadioGroupLayoutTests, EffectiveRowsFitBothLabelsAndStableIndicatorMetrics){
    RadioGroupStyle style;
    style.indicatorExtent = 40.0f;
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(2u, { 100.0f, 50.0f }, {}, style, metrics));
    EXPECT_FLOAT_EQ(metrics.rowHeight, 50.0f);
    EXPECT_FLOAT_EQ(metrics.indicatorExtent, 40.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.x, 156.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 112.0f);
}

TEST(UiRadioGroupLayoutTests, PlacementSeparatesRowsIndicatorsMarksAndLabelClips){
    const RadioGroupChoices choices = Choices(3u);
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, {}, metrics));
    RadioGroupPlacement placement;
    const Rect bounds{ 10.0f, 20.0f, 140.0f, 112.0f };
    ASSERT_TRUE(RadioGroupLayout::Place(bounds, bounds, choices, metrics, placement));
    ExpectRectangle(placement.content, { 14.0f, 24.0f, 132.0f, 104.0f });
    ExpectRectangle(placement.rows[0u].rectangle, { 14.0f, 24.0f, 132.0f, 32.0f });
    ExpectRectangle(placement.rows[1u].rectangle, { 14.0f, 60.0f, 132.0f, 32.0f });
    ExpectRectangle(placement.rows[2u].rectangle, { 14.0f, 96.0f, 132.0f, 32.0f });
    ExpectRectangle(placement.rows[0u].indicator, { 14.0f, 28.0f, 24.0f, 24.0f });
    ExpectRectangle(placement.rows[0u].mark, { 21.2f, 35.2f, 9.6f, 9.6f });
    ExpectRectangle(placement.rows[0u].textClip, { 46.0f, 24.0f, 100.0f, 32.0f });
    EXPECT_EQ(placement.rows[1u].key, 2u);
    EXPECT_FALSE(placement.rows[1u].enabled);
}

TEST(UiRadioGroupLayoutTests, InheritedClipRestrictsEachRowAndItsLabel){
    const RadioGroupChoices choices = Choices(3u);
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, {}, metrics));
    RadioGroupPlacement placement;
    ASSERT_TRUE(RadioGroupLayout::Place(
        { 10.0f, 20.0f, 140.0f, 112.0f }, { 30.0f, 30.0f, 70.0f, 80.0f }, choices, metrics, placement
    ));
    ExpectRectangle(placement.clip, { 30.0f, 30.0f, 70.0f, 80.0f });
    ExpectRectangle(placement.rows[0u].clip, { 30.0f, 30.0f, 70.0f, 26.0f });
    ExpectRectangle(placement.rows[0u].textClip, { 46.0f, 30.0f, 54.0f, 26.0f });
    ExpectRectangle(placement.rows[2u].clip, { 30.0f, 96.0f, 70.0f, 14.0f });
}

TEST(UiRadioGroupLayoutTests, FullChoiceBoundProducesAllRowsWithoutVirtualization){
    const RadioGroupChoices choices = Choices(s_RadioGroupMaxChoices);
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(choices.count, { 100.0f, 16.0f }, {}, {}, metrics));
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 2308.0f);
    RadioGroupPlacement placement;
    ASSERT_TRUE(RadioGroupLayout::Place(
        { 0.0f, 0.0f, 140.0f, 2308.0f }, { 0.0f, 0.0f, 140.0f, 2308.0f }, choices, metrics, placement
    ));
    EXPECT_EQ(placement.count, 64u);
    EXPECT_EQ(placement.rows[63u].key, 64u);
    EXPECT_FLOAT_EQ(placement.rows[63u].rectangle.y, 2272.0f);
    EXPECT_FLOAT_EQ(placement.rows[63u].rectangle.height, 32.0f);
}

TEST(UiRadioGroupLayoutTests, EmptyGroupHasOnlyPaddingAndNoChoiceGeometry){
    const RadioGroupChoices choices = Choices(0u);
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(0u, {}, {}, {}, metrics));
    EXPECT_FLOAT_EQ(metrics.contentSize.x, 8.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 8.0f);
    RadioGroupPlacement placement;
    ASSERT_TRUE(RadioGroupLayout::Place({ 0.0f, 0.0f, 8.0f, 8.0f }, { 0.0f, 0.0f, 8.0f, 8.0f }, choices, metrics, placement));
    EXPECT_EQ(placement.count, 0u);
    ExpectRectangle(placement.content, { 4.0f, 4.0f, 0.0f, 0.0f });
    EXPECT_EQ(placement.rows[0u].key, 0u);
}

TEST(UiRadioGroupLayoutTests, NarrowBoundsShrinkTheIndicatorAndLeaveAnEmptyLabelArea){
    const RadioGroupChoices choices = Choices(1u);
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(1u, { 100.0f, 16.0f }, {}, {}, metrics));
    RadioGroupPlacement placement;
    ASSERT_TRUE(RadioGroupLayout::Place(
        { 0.0f, 0.0f, 20.0f, 40.0f }, { 0.0f, 0.0f, 20.0f, 40.0f }, choices, metrics, placement
    ));
    ExpectRectangle(placement.rows[0u].indicator, { 4.0f, 14.0f, 12.0f, 12.0f });
    EXPECT_FLOAT_EQ(placement.rows[0u].mark.width, 4.8f);
    EXPECT_FLOAT_EQ(placement.rows[0u].textClip.width, 0.0f);
}

TEST(UiRadioGroupLayoutTests, TinyAndZeroBoundsSafelyClipEveryChoice){
    const RadioGroupChoices choices = Choices(2u);
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(2u, { 100.0f, 16.0f }, {}, {}, metrics));
    RadioGroupPlacement placement;
    ASSERT_TRUE(RadioGroupLayout::Place({ 0.0f, 0.0f, 2.0f, 2.0f }, { 0.0f, 0.0f, 2.0f, 2.0f }, choices, metrics, placement));
    EXPECT_EQ(placement.count, 2u);
    EXPECT_FLOAT_EQ(placement.rows[0u].indicator.width, 0.0f);
    EXPECT_FLOAT_EQ(placement.rows[0u].clip.height, 0.0f);
    ASSERT_TRUE(RadioGroupLayout::Place({}, {}, choices, metrics, placement));
    EXPECT_EQ(placement.count, 2u);
    EXPECT_FLOAT_EQ(placement.rows[1u].textClip.width, 0.0f);
    EXPECT_FLOAT_EQ(placement.rows[1u].textClip.height, 0.0f);
}

TEST(UiRadioGroupLayoutTests, RepeatedPurePlacementPreservesAllMetricsAndGeometry){
    const RadioGroupChoices choices = Choices(3u);
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, {}, metrics));
    const RadioGroupMetrics beforeMetrics = metrics;
    RadioGroupPlacement placement;
    const Rect bounds{ 0.0f, 0.0f, 140.0f, 112.0f };
    ASSERT_TRUE(RadioGroupLayout::Place(bounds, bounds, choices, metrics, placement));
    const RadioGroupPlacement before = placement;
    ASSERT_TRUE(RadioGroupLayout::Place(bounds, bounds, choices, metrics, placement));
    ExpectPlacement(placement, before);
    ExpectMetrics(metrics, beforeMetrics);
}

TEST(UiRadioGroupLayoutTests, InvalidOptionsOrChoiceCountsPreserveMeasuredOutput){
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, {}, metrics));
    const RadioGroupMetrics before = metrics;
    RadioGroupOptions options;
    options.rowHeight = 31.0f;
    EXPECT_FALSE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, options, {}, metrics));
    ExpectMetrics(metrics, before);
    options.rowHeight = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, options, {}, metrics));
    ExpectMetrics(metrics, before);
    EXPECT_FALSE(RadioGroupLayout::Measure(65u, { 100.0f, 16.0f }, {}, {}, metrics));
    ExpectMetrics(metrics, before);
    EXPECT_FALSE(RadioGroupLayout::Measure(3u, { -1.0f, 16.0f }, {}, {}, metrics));
    ExpectMetrics(metrics, before);
}

TEST(UiRadioGroupLayoutTests, InvalidStyleMetricsAndTintsPreserveMeasuredOutput){
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, {}, metrics));
    const RadioGroupMetrics before = metrics;
    for(u32 mode = 0u; mode < 8u; ++mode){
        RadioGroupStyle style;
        switch(mode){
        case 0u: style.padding.top = -1.0f; break;
        case 1u: style.rowGap = Limit<f32>::s_Infinity; break;
        case 2u: style.indicatorExtent = 0.0f; break;
        case 3u: style.gap = -1.0f; break;
        case 4u: style.markInset = 0.51f; break;
        case 5u: style.hoverTint.r = Limit<f32>::s_QuietNaN; break;
        case 6u: style.pressedTint.g = -1.0f; break;
        case 7u: style.disabledTint.a = 1.1f; break;
        }
        EXPECT_FALSE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, style, metrics));
        ExpectMetrics(metrics, before);
    }
}

TEST(UiRadioGroupLayoutTests, OverflowingContentHeightRejectsWithoutReplacingMetrics){
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, {}, metrics));
    const RadioGroupMetrics before = metrics;
    RadioGroupOptions options;
    options.rowHeight = Limit<f32>::s_Max;
    EXPECT_FALSE(RadioGroupLayout::Measure(64u, { 100.0f, 16.0f }, options, {}, metrics));
    ExpectMetrics(metrics, before);
}

TEST(UiRadioGroupLayoutTests, InvalidAndUnrepresentableBoundsPreservePlacement){
    const RadioGroupChoices choices = Choices(3u);
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, {}, metrics));
    RadioGroupPlacement placement;
    const Rect bounds{ 0.0f, 0.0f, 140.0f, 112.0f };
    ASSERT_TRUE(RadioGroupLayout::Place(bounds, bounds, choices, metrics, placement));
    const RadioGroupPlacement before = placement;
    const Rect invalid[]{
        { 0.0f, 0.0f, -1.0f, 112.0f }, { Limit<f32>::s_QuietNaN, 0.0f, 140.0f, 112.0f },
        { Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 112.0f }, { 0.0f, 1.0e20f, 140.0f, 112.0f },
    };
    for(const Rect& rectangle : invalid){
        EXPECT_FALSE(RadioGroupLayout::Place(rectangle, bounds, choices, metrics, placement));
        ExpectPlacement(placement, before);
    }
    EXPECT_FALSE(RadioGroupLayout::Place(bounds, { 0.0f, 0.0f, 140.0f, -1.0f }, choices, metrics, placement));
    ExpectPlacement(placement, before);
}

TEST(UiRadioGroupLayoutTests, MismatchedCountKeysOrMetricsPreservePlacement){
    RadioGroupChoices choices = Choices(3u);
    RadioGroupMetrics metrics;
    ASSERT_TRUE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, {}, metrics));
    RadioGroupPlacement placement;
    const Rect bounds{ 0.0f, 0.0f, 140.0f, 112.0f };
    ASSERT_TRUE(RadioGroupLayout::Place(bounds, bounds, choices, metrics, placement));
    const RadioGroupPlacement before = placement;
    choices.count = 2u;
    EXPECT_FALSE(RadioGroupLayout::Place(bounds, bounds, choices, metrics, placement));
    ExpectPlacement(placement, before);
    choices.count = 3u;
    choices.rows[1u].key = 1u;
    EXPECT_FALSE(RadioGroupLayout::Place(bounds, bounds, choices, metrics, placement));
    ExpectPlacement(placement, before);
    choices.rows[1u].key = 0u;
    EXPECT_FALSE(RadioGroupLayout::Place(bounds, bounds, choices, metrics, placement));
    ExpectPlacement(placement, before);
    choices.rows[1u].key = 2u;
    metrics.contentSize.y = 0.0f;
    EXPECT_FALSE(RadioGroupLayout::Place(bounds, bounds, choices, metrics, placement));
    ExpectPlacement(placement, before);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

