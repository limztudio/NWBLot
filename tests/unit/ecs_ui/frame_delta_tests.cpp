// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/frame_delta.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_frame_delta_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiFrameDeltaTests, SkippedUpdatesAccrueUntilOnePaintAndAreNotReplayed){
    UiFrameDelta delta;
    delta.add(0.125f);
    delta.add(0.25f);
    delta.add(0.5f);
    EXPECT_FLOAT_EQ(delta.consume(), 0.875f);
    EXPECT_FLOAT_EQ(delta.consume(), 0.0f);
    delta.add(0.125f);
    EXPECT_FLOAT_EQ(delta.consume(), 0.125f);
}

TEST(UiFrameDeltaTests, InvalidUpdatesDoNotEraseRetainedTime){
    UiFrameDelta delta;
    delta.add(0.25f);
    delta.add(-3.0f);
    delta.add(Limit<f32>::s_QuietNaN);
    delta.add(Limit<f32>::s_Infinity);
    delta.add(-Limit<f32>::s_Infinity);
    EXPECT_FLOAT_EQ(delta.consume(), 0.25f);
}

TEST(UiFrameDeltaTests, LongStallSaturatesAtPaintDeltaLimit){
    UiFrameDelta delta;
    delta.add(Limit<f32>::s_Max);
    delta.add(Limit<f32>::s_Max);
    EXPECT_EQ(delta.consume(), Limit<f32>::s_Max);
    EXPECT_FLOAT_EQ(delta.consume(), 0.0f);
}

TEST(UiFrameDeltaTests, DisplayOrResourceResetDiscardsUnpaintedTime){
    UiFrameDelta delta;
    delta.add(0.5f);
    delta.clear();
    EXPECT_FLOAT_EQ(delta.consume(), 0.0f);
    delta.add(0.25f);
    EXPECT_FLOAT_EQ(delta.consume(), 0.25f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

