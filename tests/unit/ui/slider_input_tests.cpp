// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiSliderTests;

class UiSliderInputTests : public SliderFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiSliderInputTests, PageKeysSaturateAtExactEndpoints){
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    press(Core::Key::PageUp);
    ASSERT_TRUE(accept(4u));
    EXPECT_DOUBLE_EQ(m_state.value(), 1.0);
    press(Core::Key::PageDown);
    ASSERT_TRUE(accept(5u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.0);
}

TEST_F(UiSliderInputTests, AcceptedKeyRepeatsRemainAddressedToTheSurvivingControlLifetime){
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    InputEvent key;
    key.type = InputEventType::KeyDown;
    key.key = Core::Key::Right;
    EXPECT_TRUE(send(key).keyboardConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.375);
    key.repeat = true;
    EXPECT_TRUE(send(key).keyboardConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.5);
    key.type = InputEventType::KeyUp;
    EXPECT_TRUE(send(key).keyboardConsumed);
}

TEST_F(UiSliderInputTests, TrackPressSeeksDuringFinalPaintAndReleaseOutsideAppliesTheFinalEndpoint){
    ASSERT_TRUE(accept(1u));
    const Point point = trackPoint(0.75, m_state);
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
    ASSERT_TRUE(declare(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    EXPECT_FALSE(m_state.result().valid);
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.75);
    EXPECT_TRUE(m_state.result().valueChanged);
    EXPECT_TRUE(m_state.result().dragging);
    EXPECT_EQ(m_context.input().capture(), track());
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, { 700.0f, 500.0f } }).pointerConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_DOUBLE_EQ(m_state.value(), 1.0);
    EXPECT_FALSE(m_state.result().dragging);
}

TEST_F(UiSliderInputTests, OffCenterThumbPressHasNoJumpAndZeroDeltaDoesNotOverwriteAnEarlierKey){
    const f64 baseline = 0.1;
    ASSERT_TRUE(m_state.setValue(baseline));
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    press(Core::Key::Right);
    const Rect thumbBounds = m_state.placement().thumb;
    const Point origin{ thumbBounds.x + 3.0f, thumbBounds.y + thumbBounds.height * 0.5f };
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), baseline + 0.125);
    EXPECT_TRUE(m_state.result().dragging);
    EXPECT_EQ(m_context.input().capture(), thumb());
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, origin }).pointerConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_DOUBLE_EQ(m_state.value(), baseline + 0.125);
    EXPECT_FALSE(m_state.result().valueChanged);
}

TEST_F(UiSliderInputTests, HeldThumbRepaintPreservesCaptureAndReturningToOriginRestoresExactBaselineBits){
    const f64 baseline = 0.1;
    ASSERT_TRUE(m_state.setValue(baseline));
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    const Point destination{ origin.x + 50.0f, origin.y };
    const f64 travel = m_state.placement().centerTravel.width;
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerMove, destination }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    const f64 displacement = static_cast<f64>(destination.x) - static_cast<f64>(origin.x);
    EXPECT_NEAR(m_state.value(), baseline + displacement / travel, 0.00000001);
    EXPECT_EQ(m_context.input().capture(), thumb());
    const ControlToken token = target(host())->control;
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(target(host())->control, token);
    EXPECT_EQ(m_context.input().capture(), thumb());
    EXPECT_TRUE(m_state.result().dragging);
    EXPECT_FALSE(m_state.result().valueChanged);
    EXPECT_TRUE(send({ InputEventType::PointerMove, origin }).pointerConsumed);
    ASSERT_TRUE(accept(4u));
    EXPECT_EQ(BitCast<u64>(m_state.value()), BitCast<u64>(baseline));
    EXPECT_TRUE(m_state.result().valueChanged);
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, origin }).pointerConsumed);
    ASSERT_TRUE(accept(5u));
    EXPECT_FALSE(m_state.result().dragging);
}

TEST_F(UiSliderInputTests, LatestCoalescedMoveAfterAKeyAppliesAfterTheCopiedKeySequence){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    const f64 travel = m_state.placement().centerTravel.width;
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { origin.x + 30.0f, origin.y } }).pointerConsumed);
    press(Core::Key::Right);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { origin.x + 60.0f, origin.y } }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_NEAR(m_state.value(), 0.25 + 60.0 / travel, 0.00000001);
    EXPECT_TRUE(m_state.result().dragging);
}

TEST_F(UiSliderInputTests, AKeyAfterTheLatestCoalescedMoveAppliesToTheDraggedValue){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    const f64 travel = m_state.placement().centerTravel.width;
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { origin.x + 60.0f, origin.y } }).pointerConsumed);
    press(Core::Key::Right);
    ASSERT_TRUE(accept(2u));
    EXPECT_NEAR(m_state.value(), 0.25 + 60.0 / travel + 0.125, 0.00000001);
}

TEST_F(UiSliderInputTests, DragOutsideTravelSaturatesBothEndsWithoutLosingOwnedCapture){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { 790.0f, origin.y } }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 1.0);
    EXPECT_EQ(m_context.input().capture(), thumb());
    EXPECT_TRUE(send({ InputEventType::PointerMove, { 1.0f, origin.y } }).pointerConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.0);
    EXPECT_EQ(m_context.input().capture(), thumb());
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, { 1.0f, origin.y } }).pointerConsumed);
    ASSERT_TRUE(accept(4u));
    EXPECT_FALSE(m_state.result().dragging);
}

TEST_F(UiSliderInputTests, CaptureLossCancelsPendingMotionAndRetainsTheLastAppliedValue){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { origin.x + 40.0f, origin.y } }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    const u64 retained = BitCast<u64>(m_state.value());
    EXPECT_TRUE(send({ InputEventType::PointerMove, { origin.x + 80.0f, origin.y } }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerCaptureLost }).pointerConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(BitCast<u64>(m_state.value()), retained);
    EXPECT_FALSE(m_state.result().valueChanged);
    EXPECT_FALSE(m_state.result().dragging);
    EXPECT_FALSE(m_context.input().capture().valid());
}

TEST_F(UiSliderInputTests, FocusLossRetiresPendingDragAndHeldNavigationWithoutRollingBackAppliedInput){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    press(Core::Key::Right);
    ASSERT_TRUE(accept(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.375);
    EXPECT_TRUE(send({ InputEventType::PointerMove, { origin.x + 80.0f, origin.y } }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::FocusLost }).pointerConsumed);
    EXPECT_FALSE(send({ InputEventType::FocusGained }).pointerConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.375);
    EXPECT_FALSE(m_state.result().dragging);
    EXPECT_FALSE(m_state.result().focused);
}

TEST_F(UiSliderInputTests, EnterAndWheelDoNotChangeTheSliderOrCreateActivationActions){
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    press(Core::Key::Enter);
    InputEvent wheel;
    wheel.type = InputEventType::PointerWheel;
    wheel.position = thumbPoint();
    wheel.scrollY = 3.0;
    EXPECT_TRUE(send(wheel).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    EXPECT_FALSE(m_state.result().valueChanged);
    EXPECT_TRUE(m_context.input().actions().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

