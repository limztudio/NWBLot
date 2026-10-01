// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_lifetime_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiSliderTests;

class UiSliderLifetimeTests : public SliderFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiSliderLifetimeTests, IdenticalPublicValueIntentRetiresAlreadyCopiedKeyboardAndThumbInput){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    press(Core::Key::Right);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { origin.x + 50.0f, origin.y } }).pointerConsumed);
    const u64 token = m_state.inputGeneration();
    ASSERT_TRUE(m_state.setValue(0.25));
    ASSERT_TRUE(accept(2u));
    EXPECT_NE(m_state.inputGeneration(), token);
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    EXPECT_FALSE(m_state.result().valueChanged);
    EXPECT_FALSE(m_state.result().dragging);
    EXPECT_FALSE(m_context.input().capture().valid());
}

TEST_F(UiSliderLifetimeTests, PublicValueAwayAndBackDuringTheLoanRejectsTheCandidateWithoutUndoingTheIntent){
    ASSERT_TRUE(accept(1u));
    const SliderAcceptedFrame displayed = accepted();
    const u64 token = m_state.inputGeneration();
    ASSERT_TRUE(declare(2u));
    ASSERT_TRUE(m_state.setValue(0.9));
    ASSERT_TRUE(m_state.setValue(0.25));
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_NE(m_state.inputGeneration(), token);
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    EXPECT_FALSE(m_state.result().valid);
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectAccepted(displayed);
}

TEST_F(UiSliderLifetimeTests, ChangingKeyStepRetiresAHeldKeyBeforeApplyingItsCopiedRepeat){
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    InputEvent held;
    held.type = InputEventType::KeyDown;
    held.key = Core::Key::Right;
    EXPECT_TRUE(send(held).keyboardConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.375);
    held.repeat = true;
    EXPECT_TRUE(send(held).keyboardConsumed);
    SliderOptions coarse = options();
    coarse.keyStep = 0.25;
    ASSERT_TRUE(accept(3u, coarse));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.375);
    EXPECT_FALSE(m_state.result().valueChanged);
    EXPECT_TRUE(send(held).keyboardConsumed);
    ASSERT_TRUE(accept(4u, coarse));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.375);
    held.type = InputEventType::KeyUp;
    EXPECT_TRUE(send(held).keyboardConsumed);
    press(Core::Key::Right);
    ASSERT_TRUE(accept(5u, coarse));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.625);
}

TEST_F(UiSliderLifetimeTests, ChangedRangeDropsTheOldDragAndRetainsAnAuthoritativeOutOfRangeValue){
    ASSERT_TRUE(m_state.setValue(0.75));
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { origin.x - 50.0f, origin.y } }).pointerConsumed);
    SliderOptions narrow = options();
    narrow.maximum = 0.5;
    ASSERT_TRUE(accept(2u, narrow));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.75);
    EXPECT_FALSE(m_state.result().valueChanged);
    EXPECT_FALSE(m_state.result().dragging);
    EXPECT_FALSE(m_context.input().capture().valid());
    const SliderPlacement& placement = m_state.placement();
    EXPECT_FLOAT_EQ(placement.thumb.x + placement.thumb.width * 0.5f,
        placement.centerTravel.x + placement.centerTravel.width);
}

TEST_F(UiSliderLifetimeTests, ChangedFinalTrackGeometryFencesTheCopiedDragBeforeApplication){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    const u64 admission = m_state.admissionGeneration();
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { origin.x + 60.0f, origin.y } }).pointerConsumed);
    SliderOptions wider = options();
    wider.width = { LayoutSizePolicy::Fixed, 340.0f };
    ASSERT_TRUE(accept(2u, wider, { 10.0f, 10.0f, 420.0f, 240.0f }));
    EXPECT_NE(m_state.admissionGeneration(), admission);
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    EXPECT_FALSE(m_state.result().valueChanged);
    EXPECT_FALSE(m_context.input().capture().valid());
}

TEST_F(UiSliderLifetimeTests, ChangedClipRetiresOldPointerGeometryEvenWhenTheFullTrackSizeIsUnchanged){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    const SliderPlacement before = m_state.placement();
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { origin.x + 30.0f, origin.y } }).pointerConsumed);
    ASSERT_TRUE(begin(2u, { 180.0f, 600.0f, 1.0f, 1.0f }));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 360.0f, 240.0f }));
    ASSERT_TRUE(m_builder.slider("slider", m_state, options()));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FLOAT_EQ(m_state.placement().track.width, before.track.width);
    EXPECT_LT(m_state.placement().clip.width, before.clip.width);
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    EXPECT_FALSE(m_context.input().capture().valid());
}

TEST_F(UiSliderLifetimeTests, ChangedThumbExtentRetiresTheOldGrabOffset){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { origin.x + 40.0f, origin.y } }).pointerConsumed);
    m_builder.sliderStyle().thumbExtent.x = 40.0f;
    ASSERT_TRUE(accept(2u));
    EXPECT_FLOAT_EQ(m_state.placement().thumb.width, 40.0f);
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    EXPECT_FALSE(m_state.result().dragging);
    EXPECT_FALSE(m_context.input().capture().valid());
}

TEST_F(UiSliderLifetimeTests, AStateCannotBeAliasedByTwoDeclarationsInTheSameLiveScope){
    ASSERT_TRUE(accept(1u));
    const SliderAcceptedFrame displayed = accepted();
    ASSERT_TRUE(declare(2u));
    EXPECT_FALSE(m_builder.slider("alias", m_state, options()));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_state.result().valid);
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectAccepted(displayed);
}

TEST_F(UiSliderLifetimeTests, LaterDeferredCallbackRejectsTheSliderCandidateAndPreservesItsExplicitApplicationValue){
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    const SliderAcceptedFrame displayed = accepted();
    press(Core::Key::Right);
    ASSERT_TRUE(declare(2u));
    m_laterSource.arm(SliderCallbackMutation::SetValue, &m_state);
    ASSERT_TRUE(sibling());
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_laterSource.m_mutations, 1u);
    EXPECT_DOUBLE_EQ(m_state.value(), 0.9);
    EXPECT_FALSE(m_state.result().valid);
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectAccepted(displayed);
}

TEST_F(UiSliderLifetimeTests, FinalWholeScopeValidationCannotPublishAnEarlierSliderBeforeALaterCallbackABA){
    ASSERT_TRUE(accept(1u));
    SliderState other;
    ASSERT_TRUE(other.setValue(0.5));
    ASSERT_TRUE(declare(2u));
    ASSERT_TRUE(m_builder.slider("other", other, options()));
    const u64 token = m_state.inputGeneration();
    m_laterSource.arm(SliderCallbackMutation::AwayAndBack, &m_state);
    ASSERT_TRUE(sibling());
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_NE(m_state.inputGeneration(), token);
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    EXPECT_FALSE(m_state.result().valid);
    EXPECT_FALSE(other.result().valid);
}

TEST_F(UiSliderLifetimeTests, DeferredCallbackBuilderResetCannotPublishStagedSliderDiagnostics){
    ASSERT_TRUE(accept(1u));
    const SliderAcceptedFrame displayed = accepted();
    ASSERT_TRUE(declare(2u));
    m_laterSource.arm(SliderCallbackMutation::ResetBuilder, nullptr, &m_builder);
    ASSERT_TRUE(sibling());
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_laterSource.m_mutations, 1u);
    EXPECT_FALSE(m_state.result().valid);
    expectAccepted(displayed);
}

TEST_F(UiSliderLifetimeTests, DeferredBalancedScopeReentryRejectsBothAttemptsAndEveryStagedResult){
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(declare(2u));
    m_laterSource.arm(SliderCallbackMutation::PanelBuilder, nullptr, &m_builder);
    ASSERT_TRUE(sibling());
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_laterSource.m_beginAccepted);
    EXPECT_FALSE(m_laterSource.m_endAccepted);
    EXPECT_EQ(m_laterSource.m_mutations, 1u);
    EXPECT_FALSE(m_state.result().valid);
}

TEST_F(UiSliderLifetimeTests, SuccessfulScopeEndReleasesTheValueLoanBeforeLaterFramePublication){
    ASSERT_TRUE(prepare(1u));
    EXPECT_TRUE(m_state.result().valid);
    ASSERT_TRUE(m_state.setValue(0.9));
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.9);
    EXPECT_FALSE(m_context.failed());
    ASSERT_NE(target(host()), nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

