// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <CoolStuff/Testbed/fly_camera_input.h>
#include <CoolStuff/Testbed/fly_camera_motion.adhd.h>

#include <gtest/gtest.h>

#include <global/simdmath.h>

#include <limits>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_fly_camera_motion_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(FlyCameraMotion, PointerCaptureAndNonfiniteInputResetTheAnchor){
    const Testbed::FlyCameraMotion motion;
    b8 valid = true;
    f64 lastX = 12.0;
    f64 lastY = 24.0;
    f32 deltaX = 3.0f;
    f32 deltaY = 4.0f;
    motion.updateMousePosition(100.0, 200.0, true, true, valid, lastX, lastY, deltaX, deltaY);
    EXPECT_FALSE(valid);
    EXPECT_FLOAT_EQ(deltaX, 3.0f);
    EXPECT_FLOAT_EQ(deltaY, 4.0f);
    motion.updateMousePosition(30.0, 40.0, false, true, valid, lastX, lastY, deltaX, deltaY);
    EXPECT_TRUE(valid);
    EXPECT_DOUBLE_EQ(lastX, 30.0);
    EXPECT_DOUBLE_EQ(lastY, 40.0);
    EXPECT_FLOAT_EQ(deltaX, 3.0f);
    EXPECT_FLOAT_EQ(deltaY, 4.0f);
    motion.updateMousePosition(std::numeric_limits<f64>::quiet_NaN(), 40.0, false, true, valid, lastX, lastY, deltaX, deltaY);
    EXPECT_FALSE(valid);
    EXPECT_FLOAT_EQ(deltaX, 3.0f);
    EXPECT_FLOAT_EQ(deltaY, 4.0f);
}

TEST(FlyCameraMotion, CapturedKeyboardAndNonfiniteDeltaDoNotMove){
    const Testbed::FlyCameraMotion motion;
    const auto axes = motion.buildMoveAxes(true, true, false, true, false);
    EXPECT_FLOAT_EQ(VectorGetX(axes), 0.0f);
    EXPECT_FLOAT_EQ(VectorGetY(axes), 0.0f);
    const auto position = VectorSet(1.0f, 2.0f, 3.0f, 0.0f);
    const auto result = motion.resolvePosition(position, VectorSet(1.0f, 1.0f, 0.0f, 0.0f), VectorSet(0.0f, 0.0f, 0.0f, 1.0f), std::numeric_limits<f32>::infinity(), true);
    EXPECT_FLOAT_EQ(VectorGetX(result), 1.0f);
    EXPECT_FLOAT_EQ(VectorGetY(result), 2.0f);
    EXPECT_FLOAT_EQ(VectorGetZ(result), 3.0f);
}


TEST(FlyCameraInput, CompletedDragSurvivesCaptureLossButActiveDragDoesNot){
    Testbed::FlyCameraInput input;
    input.updateButton(false, true, true, false);
    input.updatePointer(10.0, 20.0, false);
    input.updatePointer(15.0, 17.0, false);
    input.updateButton(false, true, false, true);
    input.pointerCaptureLost();
    const auto completed = input.takePointerDelta(true);
    EXPECT_FLOAT_EQ(completed.x, 5.0f);
    EXPECT_FLOAT_EQ(completed.y, -3.0f);
    const auto empty = input.takePointerDelta(false);
    EXPECT_FLOAT_EQ(empty.x, 0.0f);
    EXPECT_FLOAT_EQ(empty.y, 0.0f);

    input.updateButton(false, true, true, false);
    input.updatePointer(5.0, 6.0, false);
    input.updatePointer(9.0, 8.0, false);
    input.pointerCaptureLost();
    const auto cancelled = input.takePointerDelta(false);
    EXPECT_FLOAT_EQ(cancelled.x, 0.0f);
    EXPECT_FLOAT_EQ(cancelled.y, 0.0f);
}

TEST(FlyCameraInput, InvalidKeyIndicesAndClearPreserveInputBounds){
    Testbed::FlyCameraInput input;
    input.setKeyState(-1, true);
    input.setKeyState(NWB::Core::Key::Menu + 1, true);
    EXPECT_FALSE(input.keyPressed(-1));
    EXPECT_FALSE(input.keyPressed(NWB::Core::Key::Menu + 1));
    input.setKeyState(NWB::Core::Key::W, true);
    input.setKeyState(NWB::Core::Key::Menu, true);
    EXPECT_TRUE(input.keyPressed(NWB::Core::Key::W));
    EXPECT_TRUE(input.keyPressed(NWB::Core::Key::Menu));
    input.clear();
    EXPECT_FALSE(input.keyPressed(NWB::Core::Key::W));
    EXPECT_FALSE(input.keyPressed(NWB::Core::Key::Menu));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


