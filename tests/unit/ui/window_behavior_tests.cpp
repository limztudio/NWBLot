// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/window.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_window_behavior_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiWindowBehaviorTests, OverflowingMinimumRejectsInitializationWithoutPublishingModel){
    WindowState state;
    state.bounds = { 5.0f, 6.0f, 7.0f, 8.0f };
    WindowOptions options;
    options.initialBounds = { Limit<f32>::s_Max * 0.75f, 0.0f, 1.0f, 110.0f };
    WindowMetrics metrics;
    metrics.minimumSize = { Limit<f32>::s_Max * 0.75f, 110.0f };
    EXPECT_FALSE(WindowBehavior::initialize(state, options, metrics));
    EXPECT_FALSE(state.initialized);
    EXPECT_FLOAT_EQ(state.bounds.x, 5.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 6.0f);
    EXPECT_FLOAT_EQ(state.bounds.width, 7.0f);
    EXPECT_FLOAT_EQ(state.bounds.height, 8.0f);
}

TEST(UiWindowBehaviorTests, OverflowingMoveRejectsBoundsAndGestureBaselineAtomically){
    WindowState state;
    state.bounds = { 20.0f, 30.0f, 200.0f, 140.0f };
    state.initialized = true;
    PointerGesture gesture;
    gesture.id = { { 1u }, 1u, 1u, 1u };
    gesture.referenceRectangle = { Limit<f32>::s_Max * 0.75f, 30.0f, 200.0f, 140.0f };
    gesture.position.x = Limit<f32>::s_Max * 0.75f;
    EXPECT_FALSE(WindowBehavior::applyMove(state, gesture));
    EXPECT_FLOAT_EQ(state.bounds.x, 20.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 30.0f);
    EXPECT_EQ(state.moveGesture.id.sequence, 0u);
}

TEST(UiWindowBehaviorTests, OverflowingResizeRejectsBoundsAndGestureBaselineAtomically){
    WindowState state;
    state.bounds = { 20.0f, 30.0f, 200.0f, 140.0f };
    state.initialized = true;
    PointerGesture gesture;
    gesture.id = { { 1u }, 1u, 1u, 1u };
    gesture.referenceRectangle = { 0.0f, 30.0f, Limit<f32>::s_Max * 0.75f, 140.0f };
    gesture.position.x = Limit<f32>::s_Max * 0.75f;
    EXPECT_FALSE(WindowBehavior::applyResize(state, gesture, { 160.0f, 80.0f }));
    EXPECT_FLOAT_EQ(state.bounds.width, 200.0f);
    EXPECT_FLOAT_EQ(state.bounds.height, 140.0f);
    EXPECT_EQ(state.resizeGesture.id.sequence, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

