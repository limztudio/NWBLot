// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider_fixture.h"

#include <global/simdmath.h>


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
    EXPECT_TRUE(send(key).commandConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.375);
    key.repeat = true;
    EXPECT_TRUE(send(key).commandConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.5);
    key.type = InputEventType::KeyUp;
    EXPECT_TRUE(send(key).commandConsumed);
}

TEST_F(UiSliderInputTests, TrackPressSeeksDuringFinalPaintAndReleaseOutsideAppliesTheFinalEndpoint){
    ASSERT_TRUE(accept(1u));
    const Point point = trackPoint(0.75, m_state);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = point }).pointerConsumed);
    ASSERT_TRUE(declare(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.25);
    EXPECT_FALSE(m_state.result().valid);
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.75);
    EXPECT_TRUE(m_state.result().valueChanged);
    EXPECT_TRUE(m_state.result().dragging);
    EXPECT_EQ(m_context.input().capture(), track());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 700.0f, 500.0f } }).pointerConsumed);
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
    const Point origin{ VectorGetX(VectorAdd(VectorReplicate(thumbBounds.x), VectorReplicate(3.0f))), UiWidgetTests::Center(thumbBounds).y };
    const SIMDVectorDouble stepped = VectorDoubleAdd(VectorDoubleReplicate(baseline), VectorDoubleReplicate(0.125));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = origin }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), VectorDoubleGetX(stepped));
    EXPECT_TRUE(m_state.result().dragging);
    EXPECT_EQ(m_context.input().capture(), thumb());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = origin }).pointerConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_DOUBLE_EQ(m_state.value(), VectorDoubleGetX(stepped));
    EXPECT_FALSE(m_state.result().valueChanged);
}

TEST_F(UiSliderInputTests, HeldThumbRepaintPreservesCaptureAndReturningToOriginRestoresExactBaselineBits){
    const f64 baseline = 0.1;
    ASSERT_TRUE(m_state.setValue(baseline));
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    const Point destination{ VectorGetX(VectorAdd(VectorReplicate(origin.x), VectorReplicate(50.0f))), origin.y };
    const f64 travel = m_state.placement().centerTravel.width;
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = origin }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = destination }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    const SIMDVectorDouble displacement = VectorDoubleSubtract(VectorDoubleReplicate(static_cast<f64>(destination.x)), VectorDoubleReplicate(static_cast<f64>(origin.x)));
    EXPECT_NEAR(m_state.value(), VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(baseline), VectorDoubleDivide(displacement, VectorDoubleReplicate(travel)))), 0.00000001);
    EXPECT_EQ(m_context.input().capture(), thumb());
    const ControlToken token = target(host())->control;
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(target(host())->control, token);
    EXPECT_EQ(m_context.input().capture(), thumb());
    EXPECT_TRUE(m_state.result().dragging);
    EXPECT_FALSE(m_state.result().valueChanged);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = origin }).pointerConsumed);
    ASSERT_TRUE(accept(4u));
    EXPECT_EQ(BitCast<u64>(m_state.value()), BitCast<u64>(baseline));
    EXPECT_TRUE(m_state.result().valueChanged);
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = origin }).pointerConsumed);
    ASSERT_TRUE(accept(5u));
    EXPECT_FALSE(m_state.result().dragging);
}

TEST_F(UiSliderInputTests, LatestCoalescedMoveAfterAKeyAppliesAfterTheCopiedKeySequence){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    const f64 travel = m_state.placement().centerTravel.width;
    const SIMDVectorSingle movedX = VectorAdd(VectorReplicate(origin.x), VectorSet(30.0f, 60.0f, 0.0f, 0.0f));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = origin }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { VectorGetX(movedX), origin.y } }).pointerConsumed);
    press(Core::Key::Right);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { VectorGetY(movedX), origin.y } }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_NEAR(m_state.value(), VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(0.25), VectorDoubleDivide(VectorDoubleReplicate(60.0), VectorDoubleReplicate(travel)))), 0.00000001);
    EXPECT_TRUE(m_state.result().dragging);
}

TEST_F(UiSliderInputTests, AKeyAfterTheLatestCoalescedMoveAppliesToTheDraggedValue){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    const f64 travel = m_state.placement().centerTravel.width;
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = origin }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { VectorGetX(VectorAdd(VectorReplicate(origin.x), VectorReplicate(60.0f))), origin.y } }).pointerConsumed);
    press(Core::Key::Right);
    ASSERT_TRUE(accept(2u));
    const SIMDVectorDouble dragged = VectorDoubleAdd(VectorDoubleReplicate(0.25), VectorDoubleDivide(VectorDoubleReplicate(60.0), VectorDoubleReplicate(travel)));
    EXPECT_NEAR(m_state.value(), VectorDoubleGetX(VectorDoubleAdd(dragged, VectorDoubleReplicate(0.125))), 0.00000001);
}

TEST_F(UiSliderInputTests, DragOutsideTravelSaturatesBothEndsWithoutLosingOwnedCapture){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = origin }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 790.0f, origin.y } }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 1.0);
    EXPECT_EQ(m_context.input().capture(), thumb());
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { 1.0f, origin.y } }).pointerConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.0);
    EXPECT_EQ(m_context.input().capture(), thumb());
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = { 1.0f, origin.y } }).pointerConsumed);
    ASSERT_TRUE(accept(4u));
    EXPECT_FALSE(m_state.result().dragging);
}

TEST_F(UiSliderInputTests, CaptureLossCancelsPendingMotionAndRetainsTheLastAppliedValue){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    const SIMDVectorSingle movedX = VectorAdd(VectorReplicate(origin.x), VectorSet(40.0f, 80.0f, 0.0f, 0.0f));
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = origin }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { VectorGetX(movedX), origin.y } }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    const u64 retained = BitCast<u64>(m_state.value());
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { VectorGetY(movedX), origin.y } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::PointerCaptureLost }).pointerConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(BitCast<u64>(m_state.value()), retained);
    EXPECT_FALSE(m_state.result().valueChanged);
    EXPECT_FALSE(m_state.result().dragging);
    EXPECT_FALSE(m_context.input().capture().valid());
}

TEST_F(UiSliderInputTests, FocusLossRetiresPendingDragAndHeldNavigationWithoutRollingBackAppliedInput){
    ASSERT_TRUE(accept(1u));
    const Point origin = thumbPoint();
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = origin }).pointerConsumed);
    press(Core::Key::Right);
    ASSERT_TRUE(accept(2u));
    EXPECT_DOUBLE_EQ(m_state.value(), 0.375);
    EXPECT_TRUE(send({ .type = InputEventType::PointerMove, .position = { VectorGetX(VectorAdd(VectorReplicate(origin.x), VectorReplicate(80.0f))), origin.y } }).pointerConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost }).pointerConsumed);
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained }).pointerConsumed);
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

