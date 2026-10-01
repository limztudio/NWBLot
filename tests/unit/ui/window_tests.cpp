// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_window_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;

class UiWindowTests : public WidgetFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiWindowTests, ContentHeightFitsOnlyFirstUseAndPreservesLaterHostGeometry){
    WindowState state;
    WindowOptions options;
    options.initialBounds = { 18.0f, 22.0f, 240.0f, 400.0f };
    options.minimumSize = { 120.0f, 60.0f };
    options.contentHeightFirstUse = true;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Title", state, options));
    ASSERT_TRUE(m_builder.label("caption", "One label"));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    ASSERT_TRUE(finishWindow());
    const DrawSnapshot first = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    const HitTarget* apply = target(id("apply"));
    ASSERT_NE(apply, nullptr);
    EXPECT_FLOAT_EQ(state.bounds.height, apply->rectangle.y + apply->rectangle.height + 6.0f - state.bounds.y);
    EXPECT_LT(state.bounds.height, 400.0f);
    const f32 firstHeight = state.bounds.height;
    options.initialBounds = { 500.0f, 500.0f, 500.0f, 500.0f };
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Title", state, options));
    ASSERT_TRUE(m_builder.label("caption", "A changed label"));
    ASSERT_TRUE(m_builder.label("extra", "Another label"));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    ASSERT_TRUE(finishWindow());
    const DrawSnapshot second = m_paint.freeze();
    EXPECT_FLOAT_EQ(state.bounds.x, 18.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 22.0f);
    EXPECT_FLOAT_EQ(state.bounds.width, 240.0f);
    EXPECT_FLOAT_EQ(state.bounds.height, firstHeight);
    EXPECT_EQ(first.generation(), 1u);
    EXPECT_EQ(second.generation(), 2u);
    ASSERT_TRUE(m_context.commitFrame(2u));
}

TEST_F(UiWindowTests, CompletedDragBeforeCallbackMovesExactlyOnce){
    WindowState state;
    WindowOptions options;
    options.initialBounds = { 30.0f, 40.0f, 240.0f, 150.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Move", state, options));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    drag({ 130.0f, 55.0f }, { 165.0f, 81.0f });
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Move", state, options));
    EXPECT_FLOAT_EQ(state.bounds.x, 65.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 66.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_TRUE(begin(3u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Move", state, options));
    EXPECT_FLOAT_EQ(state.bounds.x, 65.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 66.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(3u));
}

TEST_F(UiWindowTests, ActiveDragRetainsBaselineAcrossAcceptedLayouts){
    WindowState state;
    WindowOptions options;
    options.initialBounds = { 30.0f, 40.0f, 240.0f, 150.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Move", state, options));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_TRUE(send({ InputEventType::PrimaryDown, { 130.0f, 55.0f } }).pointerConsumed);
    ASSERT_TRUE(send({ InputEventType::PointerMove, { 140.0f, 65.0f } }).pointerConsumed);
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Move", state, options));
    EXPECT_FLOAT_EQ(state.bounds.x, 40.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 50.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_TRUE(send({ InputEventType::PointerMove, { 160.0f, 80.0f } }).pointerConsumed);
    ASSERT_TRUE(send({ InputEventType::PrimaryUp, { 160.0f, 80.0f } }).pointerConsumed);
    ASSERT_TRUE(begin(3u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Move", state, options));
    EXPECT_FLOAT_EQ(state.bounds.x, 60.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 65.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(3u));
}

TEST_F(UiWindowTests, MultipleCompletedDragsUseTheDisplayedReferenceForEachPress){
    WindowState state;
    WindowOptions options;
    options.initialBounds = { 30.0f, 40.0f, 240.0f, 150.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Move", state, options));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    drag({ 130.0f, 55.0f }, { 140.0f, 60.0f });
    drag({ 130.0f, 55.0f }, { 150.0f, 65.0f });
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Move", state, options));
    EXPECT_FLOAT_EQ(state.bounds.x, 50.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 50.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(2u));
}

TEST_F(UiWindowTests, CompletedResizeRetainsPositionAndRunsOnce){
    WindowState state;
    WindowOptions options;
    options.initialBounds = { 30.0f, 40.0f, 240.0f, 150.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Resize", state, options));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    drag({ 262.0f, 182.0f }, { 297.0f, 197.0f });
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Resize", state, options));
    EXPECT_FLOAT_EQ(state.bounds.x, 30.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 40.0f);
    EXPECT_FLOAT_EQ(state.bounds.width, 275.0f);
    EXPECT_FLOAT_EQ(state.bounds.height, 165.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_TRUE(begin(3u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Resize", state, options));
    EXPECT_FLOAT_EQ(state.bounds.width, 275.0f);
    EXPECT_FLOAT_EQ(state.bounds.height, 165.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(3u));
}

TEST_F(UiWindowTests, ResizeMinimumDoesNotLoseOriginalPointerBaseline){
    WindowState state;
    WindowOptions options;
    options.initialBounds = { 30.0f, 40.0f, 240.0f, 150.0f };
    options.minimumSize = { 180.0f, 110.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Resize", state, options));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_TRUE(send({ InputEventType::PrimaryDown, { 262.0f, 182.0f } }).pointerConsumed);
    ASSERT_TRUE(send({ InputEventType::PointerMove, { 0.0f, 0.0f } }).pointerConsumed);
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Resize", state, options));
    EXPECT_FLOAT_EQ(state.bounds.width, 180.0f);
    EXPECT_FLOAT_EQ(state.bounds.height, 110.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_TRUE(send({ InputEventType::PrimaryUp, { 282.0f, 192.0f } }).pointerConsumed);
    ASSERT_TRUE(begin(3u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Resize", state, options));
    EXPECT_FLOAT_EQ(state.bounds.width, 260.0f);
    EXPECT_FLOAT_EQ(state.bounds.height, 160.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(3u));
}

TEST_F(UiWindowTests, CollapseKeepsHostSizeAndRequiresBalancedEnd){
    WindowState state;
    WindowOptions options;
    options.initialBounds = { 30.0f, 40.0f, 240.0f, 150.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Collapse", state, options));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    click({ 42.0f, 57.0f });
    ASSERT_TRUE(begin(2u));
    EXPECT_FALSE(m_builder.beginWindow("window", "Collapse", state, options));
    EXPECT_FALSE(m_context.failed());
    EXPECT_FALSE(m_builder.balanced());
    EXPECT_TRUE(state.collapsed);
    EXPECT_FLOAT_EQ(state.bounds.height, 150.0f);
    ASSERT_TRUE(finishWindow());
    EXPECT_TRUE(m_builder.balanced());
    const DrawSnapshot collapsed = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(target(id("apply")), nullptr);
    EXPECT_EQ(target(id("@window.resize")), nullptr);
    Rect skin;
    ASSERT_TRUE(skinQuad(collapsed, 0u, skin));
    EXPECT_FLOAT_EQ(skin.height, 34.0f);
    EXPECT_FALSE(send({ InputEventType::PrimaryDown, { 100.0f, 120.0f } }).pointerConsumed);
    EXPECT_FALSE(send({ InputEventType::PrimaryUp, { 100.0f, 120.0f } }).pointerConsumed);
}

TEST_F(UiWindowTests, KeyboardCollapseUsesTabFocusAndSuppressesRepeat){
    WindowState state;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Keyboard", state));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Tab }).keyboardConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Tab }).keyboardConsumed);
    EXPECT_EQ(m_context.input().focus(), id("@window.collapse"));
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Enter }).keyboardConsumed);
    InputEvent repeat;
    repeat.type = InputEventType::KeyDown;
    repeat.key = Core::Key::Enter;
    repeat.repeat = true;
    EXPECT_TRUE(send(repeat).keyboardConsumed);
    ASSERT_TRUE(begin(2u));
    EXPECT_FALSE(m_builder.beginWindow("window", "Keyboard", state));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_TRUE(state.collapsed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Enter }).keyboardConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Space }).keyboardConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Space }).keyboardConsumed);
    ASSERT_TRUE(begin(3u));
    EXPECT_TRUE(m_builder.beginWindow("window", "Keyboard", state));
    EXPECT_FALSE(state.collapsed);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(3u));
}

TEST_F(UiWindowTests, DisablingChromeCancelsPendingGesturesAndRestoresContent){
    WindowState state;
    WindowOptions options;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Disabled", state, options));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    drag({ 130.0f, 35.0f }, { 165.0f, 61.0f });
    state.collapsed = true;
    options.movable = false;
    options.resizable = false;
    options.collapsible = false;
    ASSERT_TRUE(begin(2u));
    EXPECT_TRUE(m_builder.beginWindow("window", "Disabled", state, options));
    EXPECT_FALSE(state.collapsed);
    EXPECT_FLOAT_EQ(state.bounds.x, 20.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 20.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(target(id("@window.collapse")), nullptr);
    EXPECT_EQ(target(id("@window.resize")), nullptr);
    ASSERT_NE(target(id("@window.title")), nullptr);
    EXPECT_FALSE(target(id("@window.title"))->pointerGesture);
}

TEST_F(UiWindowTests, FocusLossCancelsUnconsumedWindowDrag){
    WindowState state;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Focus", state));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_TRUE(send({ InputEventType::PrimaryDown, { 130.0f, 35.0f } }).pointerConsumed);
    ASSERT_TRUE(send({ InputEventType::PointerMove, { 180.0f, 80.0f } }).pointerConsumed);
    EXPECT_FALSE(send({ .type = InputEventType::FocusLost, .position = {} }).capture.valid());
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Focus", state));
    EXPECT_FLOAT_EQ(state.bounds.x, 20.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 20.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(2u));
}

TEST_F(UiWindowTests, ClientResizeKeepsTitleReachableAndClipsOversizedWindow){
    WindowState state;
    WindowOptions options;
    options.initialBounds = { 500.0f, 300.0f, 240.0f, 150.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Viewport", state, options));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_TRUE(begin(2u, { 100.0f, 80.0f, 2.0f, 2.0f }));
    ASSERT_TRUE(m_builder.beginWindow("window", "Viewport", state, options));
    EXPECT_FLOAT_EQ(state.bounds.x, 0.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 0.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(2u));
    const HitTarget* title = target(id("@window.title"));
    ASSERT_NE(title, nullptr);
    EXPECT_FLOAT_EQ(title->clip.width, 100.0f);
    EXPECT_LE(title->clip.height, 80.0f);
    EXPECT_FALSE(m_context.input().hitTest({ 150.0f, 15.0f }).valid());
}

TEST_F(UiWindowTests, ContentWidthFitsFirstUseWithAStableHostMinimum){
    WindowState state;
    WindowOptions options;
    options.initialBounds = { 30.0f, 40.0f, 400.0f, 140.0f };
    options.contentWidthFirstUse = true;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Short", state, options));
    ASSERT_TRUE(m_builder.label("caption", "Content"));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_FLOAT_EQ(state.bounds.width, 160.0f);
    EXPECT_FLOAT_EQ(state.bounds.height, 140.0f);
    options.initialBounds.width = 700.0f;
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Short", state, options));
    ASSERT_TRUE(m_builder.label("caption", "Changed content"));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FLOAT_EQ(state.bounds.width, 160.0f);
}

TEST_F(UiWindowTests, NewMoveAnchorsDisplayedGeometryWhenHostModelIsAhead){
    WindowState state;
    WindowOptions options;
    options.initialBounds = { 30.0f, 40.0f, 240.0f, 150.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Pending", state, options));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    state.bounds = { 300.0f, 200.0f, 400.0f, 300.0f };
    drag({ 130.0f, 55.0f }, { 145.0f, 70.0f });
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Pending", state, options));
    EXPECT_FLOAT_EQ(state.bounds.x, 45.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 55.0f);
    EXPECT_FLOAT_EQ(state.bounds.width, 400.0f);
    EXPECT_FLOAT_EQ(state.bounds.height, 300.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(2u));
}

TEST_F(UiWindowTests, NewResizeAnchorsDisplayedSizeWhenHostModelIsAhead){
    WindowState state;
    WindowOptions options;
    options.initialBounds = { 30.0f, 40.0f, 240.0f, 150.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Pending", state, options));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    state.bounds = { 300.0f, 200.0f, 400.0f, 300.0f };
    drag({ 262.0f, 182.0f }, { 287.0f, 202.0f });
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Pending", state, options));
    EXPECT_FLOAT_EQ(state.bounds.x, 300.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 200.0f);
    EXPECT_FLOAT_EQ(state.bounds.width, 265.0f);
    EXPECT_FLOAT_EQ(state.bounds.height, 170.0f);
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(2u));
}

TEST_F(UiWindowTests, MissingResizeFallbackRejectsAdmissionBeforeChangingHostState){
    WindowState state;
    state.bounds = { 5.0f, 6.0f, 7.0f, 8.0f };
    configureSkin("white");
    ASSERT_TRUE(begin(1u));
    EXPECT_FALSE(m_builder.beginWindow("window", "Skin", state));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(state.initialized);
    EXPECT_FLOAT_EQ(state.bounds.x, 5.0f);
    EXPECT_FLOAT_EQ(state.bounds.y, 6.0f);
    EXPECT_FLOAT_EQ(state.bounds.width, 7.0f);
    EXPECT_FLOAT_EQ(state.bounds.height, 8.0f);
    EXPECT_FALSE(m_context.finishFrame());
}

TEST_F(UiWindowTests, MissingRequiredSkinKeepsPreviousAcceptedTargets){
    WindowState state;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Skin", state));
    ASSERT_TRUE(finishWindow());
    ASSERT_TRUE(m_context.commitFrame(1u));
    configureSkin("window.title");
    ASSERT_TRUE(begin(2u));
    EXPECT_FALSE(m_builder.beginWindow("window", "Skin", state));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_NE(target(id("@window.title")), nullptr);
}

TEST_F(UiWindowTests, InvalidOptionsAndWrongEndRejectCandidatePublication){
    WindowState state;
    WindowOptions options;
    options.minimumSize.x = Limit<f32>::s_QuietNaN;
    ASSERT_TRUE(begin(1u));
    EXPECT_FALSE(m_builder.beginWindow("window", "Invalid", state, options));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    m_context.abandonFrame();
    m_builder.reset();
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Invalid", state));
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

