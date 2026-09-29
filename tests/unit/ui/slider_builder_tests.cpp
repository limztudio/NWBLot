// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiSliderTests;

class UiSliderBuilderTests : public SliderFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiSliderBuilderTests, DeclarationPublishesItsResultAndOwnedPartsOnlyAfterScopeValidation){
    ASSERT_TRUE(declare(1u));
    EXPECT_FALSE(m_state.result().valid);
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    EXPECT_EQ(target(host()), nullptr);
    ASSERT_TRUE(finishPanel());
    EXPECT_TRUE(m_state.result().valid);
    EXPECT_FALSE(m_state.result().valueChanged);
    EXPECT_FALSE(m_state.result().dragging);
    EXPECT_EQ(target(host()), nullptr);
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(target(host()), nullptr);
    ASSERT_NE(target(track()), nullptr);
    ASSERT_NE(target(thumb()), nullptr);
    EXPECT_TRUE(target(host())->focusable);
    EXPECT_TRUE(target(host())->navigable);
    EXPECT_TRUE(target(host())->horizontalNavigation);
    EXPECT_FALSE(target(host())->activatable);
    EXPECT_FALSE(target(host())->scrollable);
    for(const WidgetId part : { track(), thumb() }){
        EXPECT_EQ(target(part)->owner, host());
        EXPECT_EQ(target(part)->control, target(host())->control);
        EXPECT_TRUE(target(part)->pointerGesture);
        EXPECT_FALSE(target(part)->focusable);
        EXPECT_FALSE(target(part)->activatable);
    }
    ExpectSliderRect(target(thumb())->rectangle, m_state.placement().thumb);
}

TEST_F(UiSliderBuilderTests, OneTabStopSkipsTrackAndThumbBetweenOrdinaryControls){
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 360.0f, 240.0f }));
    EXPECT_FALSE(m_builder.button("before", "Before"));
    ASSERT_TRUE(m_builder.slider("slider", m_state, options()));
    EXPECT_FALSE(m_builder.button("after", "After"));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    press(InputKey::Tab);
    EXPECT_EQ(m_context.input().focus(), id("before", "panel"));
    press(InputKey::Tab);
    EXPECT_EQ(m_context.input().focus(), host());
    press(InputKey::Tab);
    EXPECT_EQ(m_context.input().focus(), id("after", "panel"));
}

TEST_F(UiSliderBuilderTests, FiniteExternalValueOutsideTheRangeIsRetainedWhileItsThumbClampsVisually){
    ASSERT_TRUE(m_state.setValue(2.0));
    ASSERT_TRUE(accept(1u));
    EXPECT_DOUBLE_EQ(m_state.value(), 2.0);
    EXPECT_FALSE(m_state.result().valueChanged);
    const SliderPlacement& placement = m_state.placement();
    EXPECT_FLOAT_EQ(placement.thumb.x + placement.thumb.width * 0.5f,
        placement.centerTravel.x + placement.centerTravel.width);
    press(InputKey::Tab);
    press(InputKey::Home);
    ASSERT_TRUE(accept(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.0);
    EXPECT_TRUE(m_state.result().valueChanged);
}

TEST_F(UiSliderBuilderTests, IdleFramesPreserveSignedZeroBitsAndDoNotInventChangeNotifications){
    ASSERT_TRUE(m_state.setValue(BitCast<f64>(0x8000000000000000ull)));
    ASSERT_TRUE(accept(1u));
    EXPECT_EQ(BitCast<u64>(m_state.value()), 0x8000000000000000ull);
    ASSERT_TRUE(accept(2u));
    EXPECT_EQ(BitCast<u64>(m_state.value()), 0x8000000000000000ull);
    EXPECT_TRUE(m_state.result().valid);
    EXPECT_FALSE(m_state.result().valueChanged);
}

TEST_F(UiSliderBuilderTests, DisabledSliderKeepsAPointerBarrierWithoutApplyingQueuedInput){
    ASSERT_TRUE(accept(1u));
    press(InputKey::Tab);
    press(InputKey::Right);
    SliderOptions disabled = options();
    disabled.enabled = false;
    ASSERT_TRUE(accept(2u, disabled));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    EXPECT_FALSE(m_state.result().valueChanged);
    ASSERT_NE(target(host()), nullptr);
    EXPECT_FALSE(target(host())->enabled);
    EXPECT_FALSE(target(host())->focusable);
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, SliderCenter(m_state.placement().bounds) }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, SliderCenter(m_state.placement().bounds) }).pointerConsumed);
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(accept(3u, disabled));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
}

TEST_F(UiSliderBuilderTests, ConstantRangeIsValidAndNoninteractiveWithoutRewritingTheApplicationValue){
    SliderOptions constant = options();
    constant.minimum = 0.5;
    constant.maximum = 0.5;
    ASSERT_TRUE(accept(1u, constant));
    EXPECT_TRUE(m_state.result().valid);
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    ASSERT_NE(target(host()), nullptr);
    EXPECT_FALSE(target(host())->focusable);
    EXPECT_FALSE(target(host())->enabled);
    click(SliderCenter(m_state.placement().bounds));
    ASSERT_TRUE(accept(2u, constant));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    EXPECT_FALSE(m_state.result().valueChanged);
}

TEST_F(UiSliderBuilderTests, InvalidPolicyRejectsTheCandidateWithoutReplacingItsAcceptedLayout){
    ASSERT_TRUE(accept(1u));
    const SliderAcceptedFrame displayed = accepted();
    const SliderSnapshot before = m_state.snapshot();
    SliderOptions invalid = options();
    invalid.minimum = 2.0;
    EXPECT_FALSE(declare(2u, invalid));
    EXPECT_TRUE(m_context.failed());
    EXPECT_TRUE(m_state.matches(before));
    EXPECT_FALSE(m_state.result().valid);
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectAccepted(displayed);
}

TEST_F(UiSliderBuilderTests, FrozenSliderStyleOwnsItsMetricsThroughTheDeferredPaint){
    ASSERT_TRUE(declare(1u));
    m_builder.sliderStyle().thumbExtent = { 40.0f, 40.0f };
    m_builder.sliderStyle().trackHeight = 20.0f;
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_FLOAT_EQ(m_state.placement().thumb.width, 24.0f);
    EXPECT_FLOAT_EQ(m_state.placement().thumb.height, 24.0f);
    EXPECT_FLOAT_EQ(m_state.placement().track.height, 12.0f);
    const DrawSnapshot paint = m_paint.freeze();
    EXPECT_GT(regionQuads(paint, Name("slider.track")), 0u);
    EXPECT_GT(regionQuads(paint, Name("slider.thumb.normal")), 0u);
}

TEST_F(UiSliderBuilderTests, NextSuccessfulIdleFrameClearsThePreviousChangeDiagnostic){
    ASSERT_TRUE(accept(1u));
    press(InputKey::Tab);
    press(InputKey::Right);
    ASSERT_TRUE(accept(2u));
    EXPECT_TRUE(m_state.result().valueChanged);
    EXPECT_TRUE(m_state.result().focused);
    ASSERT_TRUE(accept(3u));
    EXPECT_TRUE(m_state.result().valid);
    EXPECT_FALSE(m_state.result().valueChanged);
    EXPECT_TRUE(m_state.result().focused);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

