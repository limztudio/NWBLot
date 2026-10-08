// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/radio_group.h>
#include <impl/ecs_ui/toolkit/widgets/radio_group_style.h>

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiRadioGroupLayoutTests, InheritedClipRestrictsEachRowAndItsLabel){
    const RadioGroupChoices choices = Choices(3u);
    auto metrics = RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, {});
    ASSERT_TRUE(metrics);
    auto placement = RadioGroupLayout::Place({ 10.0f, 20.0f, 140.0f, 112.0f }, { 30.0f, 30.0f, 70.0f, 80.0f }, choices, *metrics);
    ASSERT_TRUE(placement);
    UiWidgetTests::ExpectRect(placement->clip, { 30.0f, 30.0f, 70.0f, 80.0f });
    UiWidgetTests::ExpectRect(placement->rows[0u].clip, { 30.0f, 30.0f, 70.0f, 26.0f });
    UiWidgetTests::ExpectRect(placement->rows[0u].textClip, { 46.0f, 30.0f, 54.0f, 26.0f });
    UiWidgetTests::ExpectRect(placement->rows[2u].clip, { 30.0f, 96.0f, 70.0f, 14.0f });
}

TEST(UiRadioGroupLayoutTests, FullChoiceBoundProducesAllRowsWithoutVirtualization){
    const RadioGroupChoices choices = Choices(s_RadioGroupMaxChoices);
    auto metrics = RadioGroupLayout::Measure(choices.count, { 100.0f, 16.0f }, {}, {});
    ASSERT_TRUE(metrics);
    EXPECT_FLOAT_EQ(metrics->contentSize.y, 2308.0f);
    auto placement = RadioGroupLayout::Place({ 0.0f, 0.0f, 140.0f, 2308.0f }, { 0.0f, 0.0f, 140.0f, 2308.0f }, choices, *metrics);
    ASSERT_TRUE(placement);
    EXPECT_EQ(placement->count, 64u);
    EXPECT_EQ(placement->rows[63u].key, 64u);
    EXPECT_FLOAT_EQ(placement->rows[63u].rectangle.y, 2272.0f);
    EXPECT_FLOAT_EQ(placement->rows[63u].rectangle.height, 32.0f);
}

TEST(UiRadioGroupLayoutTests, EmptyGroupHasOnlyPaddingAndNoChoiceGeometry){
    const RadioGroupChoices choices = Choices(0u);
    auto metrics = RadioGroupLayout::Measure(0u, {}, {}, {});
    ASSERT_TRUE(metrics);
    EXPECT_FLOAT_EQ(metrics->contentSize.x, 8.0f);
    EXPECT_FLOAT_EQ(metrics->contentSize.y, 8.0f);
    auto placement = RadioGroupLayout::Place({ 0.0f, 0.0f, 8.0f, 8.0f }, { 0.0f, 0.0f, 8.0f, 8.0f }, choices, *metrics);
    ASSERT_TRUE(placement);
    EXPECT_EQ(placement->count, 0u);
    UiWidgetTests::ExpectRect(placement->content, { 4.0f, 4.0f, 0.0f, 0.0f });
    EXPECT_EQ(placement->rows[0u].key, 0u);
}

TEST(UiRadioGroupLayoutTests, NarrowBoundsShrinkTheIndicatorAndLeaveAnEmptyLabelArea){
    const RadioGroupChoices choices = Choices(1u);
    auto metrics = RadioGroupLayout::Measure(1u, { 100.0f, 16.0f }, {}, {});
    ASSERT_TRUE(metrics);
    auto placement = RadioGroupLayout::Place({ 0.0f, 0.0f, 20.0f, 40.0f }, { 0.0f, 0.0f, 20.0f, 40.0f }, choices, *metrics);
    ASSERT_TRUE(placement);
    UiWidgetTests::ExpectRect(placement->rows[0u].indicator, { 4.0f, 14.0f, 12.0f, 12.0f });
    EXPECT_FLOAT_EQ(placement->rows[0u].mark.width, 4.8f);
    EXPECT_FLOAT_EQ(placement->rows[0u].textClip.width, 0.0f);
}

TEST(UiRadioGroupLayoutTests, TinyAndZeroBoundsSafelyClipEveryChoice){
    const RadioGroupChoices choices = Choices(2u);
    auto metrics = RadioGroupLayout::Measure(2u, { 100.0f, 16.0f }, {}, {});
    ASSERT_TRUE(metrics);
    auto placement = RadioGroupLayout::Place({ 0.0f, 0.0f, 2.0f, 2.0f }, { 0.0f, 0.0f, 2.0f, 2.0f }, choices, *metrics);
    ASSERT_TRUE(placement);
    EXPECT_EQ(placement->count, 2u);
    EXPECT_FLOAT_EQ(placement->rows[0u].indicator.width, 0.0f);
    EXPECT_FLOAT_EQ(placement->rows[0u].clip.height, 0.0f);
    placement = RadioGroupLayout::Place({}, {}, choices, *metrics);
    ASSERT_TRUE(placement);
    EXPECT_EQ(placement->count, 2u);
    EXPECT_FLOAT_EQ(placement->rows[1u].textClip.width, 0.0f);
    EXPECT_FLOAT_EQ(placement->rows[1u].textClip.height, 0.0f);
}

TEST(UiRadioGroupLayoutTests, InvalidOptionsOrChoiceCountsRejectMetrics){
    RadioGroupOptions options;
    options.rowHeight = 31.0f;
    EXPECT_FALSE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, options, {}));
    options.rowHeight = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, options, {}));
    EXPECT_FALSE(RadioGroupLayout::Measure(65u, { 100.0f, 16.0f }, {}, {}));
    EXPECT_FALSE(RadioGroupLayout::Measure(3u, { -1.0f, 16.0f }, {}, {}));
}

TEST(UiRadioGroupLayoutTests, InvalidStyleMetricsAndTintsRejectMetrics){
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
        EXPECT_FALSE(RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, style));
    }
}

TEST(UiRadioGroupLayoutTests, OverflowingContentHeightRejectsMetrics){
    RadioGroupOptions options;
    options.rowHeight = Limit<f32>::s_Max;
    EXPECT_FALSE(RadioGroupLayout::Measure(64u, { 100.0f, 16.0f }, options, {}));
}

TEST(UiRadioGroupLayoutTests, InvalidAndUnrepresentableBoundsRejectPlacement){
    const RadioGroupChoices choices = Choices(3u);
    auto metrics = RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, {});
    ASSERT_TRUE(metrics);
    const Rect bounds{ 0.0f, 0.0f, 140.0f, 112.0f };
    const Rect invalid[]{
        { 0.0f, 0.0f, -1.0f, 112.0f }, { Limit<f32>::s_QuietNaN, 0.0f, 140.0f, 112.0f },
        { Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 112.0f }, { 0.0f, 1.0e20f, 140.0f, 112.0f },
    };
    for(const Rect& rectangle : invalid){
        EXPECT_FALSE(RadioGroupLayout::Place(rectangle, bounds, choices, *metrics));
    }
    EXPECT_FALSE(RadioGroupLayout::Place(bounds, { 0.0f, 0.0f, 140.0f, -1.0f }, choices, *metrics));
}

TEST(UiRadioGroupLayoutTests, MismatchedCountKeysOrMetricsRejectPlacement){
    RadioGroupChoices choices = Choices(3u);
    auto metrics = RadioGroupLayout::Measure(3u, { 100.0f, 16.0f }, {}, {});
    ASSERT_TRUE(metrics);
    const Rect bounds{ 0.0f, 0.0f, 140.0f, 112.0f };
    choices.count = 2u;
    EXPECT_FALSE(RadioGroupLayout::Place(bounds, bounds, choices, *metrics));
    choices.count = 3u;
    choices.rows[1u].key = 1u;
    EXPECT_FALSE(RadioGroupLayout::Place(bounds, bounds, choices, *metrics));
    choices.rows[1u].key = 0u;
    EXPECT_FALSE(RadioGroupLayout::Place(bounds, bounds, choices, *metrics));
    choices.rows[1u].key = 2u;
    metrics->contentSize.y = 0.0f;
    EXPECT_FALSE(RadioGroupLayout::Place(bounds, bounds, choices, *metrics));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

