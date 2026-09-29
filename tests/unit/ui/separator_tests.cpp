// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_separator_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;

class UiSeparatorTests : public WidgetFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiSeparatorTests, HorizontalSeparatorUsesSkinThicknessAndContainerContentWidth){
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 20.0f, 30.0f, 160.0f, 100.0f }));
    ASSERT_TRUE(m_builder.separator("divider"));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    Rect divider;
    ASSERT_TRUE(skinQuad(snapshot, 4u, divider));
    EXPECT_FLOAT_EQ(divider.x, 23.0f);
    EXPECT_FLOAT_EQ(divider.y, 34.0f);
    EXPECT_FLOAT_EQ(divider.width, 152.0f);
    EXPECT_FLOAT_EQ(divider.height, 3.0f);
    EXPECT_EQ(target(id("divider", "panel")), nullptr);
    const HitTarget* apply = target(id("apply", "panel"));
    ASSERT_NE(apply, nullptr);
    EXPECT_FLOAT_EQ(apply->rectangle.y, 45.0f);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = InputKey::Tab }).keyboardConsumed);
    EXPECT_EQ(m_context.input().focus(), apply->id);
}

TEST_F(UiSeparatorTests, ExplicitHorizontalLengthAndThicknessRemainLogicalAtDpiScale){
    SeparatorOptions options;
    options.length = { LayoutSizePolicy::Fixed, 92.0f };
    options.thickness = 2.0f;
    ASSERT_TRUE(begin(1u, { 800.0f, 600.0f, 2.0f, 2.0f }));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 20.0f, 30.0f, 160.0f, 100.0f }));
    ASSERT_TRUE(m_builder.separator("divider", options));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    Rect divider;
    ASSERT_TRUE(skinQuad(snapshot, 4u, divider));
    EXPECT_FLOAT_EQ(divider.width, 92.0f);
    EXPECT_FLOAT_EQ(divider.height, 2.0f);
    EXPECT_FLOAT_EQ(snapshot.displayMetrics().pixelScaleX, 2.0f);
}

TEST_F(UiSeparatorTests, VerticalSeparatorStretchesAlongRowContentHeight){
    SeparatorOptions options;
    options.direction = SeparatorDirection::Vertical;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 20.0f, 30.0f, 160.0f, 100.0f }, LayoutDirection::Row));
    ASSERT_TRUE(m_builder.separator("divider", options));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    Rect divider;
    ASSERT_TRUE(skinQuad(snapshot, 4u, divider));
    EXPECT_FLOAT_EQ(divider.width, 2.0f);
    EXPECT_FLOAT_EQ(divider.height, 90.0f);
    const HitTarget* apply = target(id("apply", "panel"));
    ASSERT_NE(apply, nullptr);
    EXPECT_FLOAT_EQ(apply->rectangle.x, 33.0f);
}

TEST_F(UiSeparatorTests, MissingRegionRejectsCandidateWithoutReplacingAcceptedLayout){
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 0.0f, 0.0f, 160.0f, 100.0f }));
    ASSERT_TRUE(m_builder.separator("divider"));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    configureSkin("separator");
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 100.0f, 100.0f, 160.0f, 100.0f }));
    EXPECT_FALSE(m_builder.separator("divider"));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_NE(target(id("apply", "panel")), nullptr);
}

TEST_F(UiSeparatorTests, InvalidThicknessPoisonsBuildAndPreservesPreviousLayout){
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 0.0f, 0.0f, 160.0f, 100.0f }));
    ASSERT_TRUE(m_builder.separator("divider"));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    SeparatorOptions options;
    options.thickness = Limit<f32>::s_QuietNaN;
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 0.0f, 0.0f, 160.0f, 100.0f }));
    EXPECT_FALSE(m_builder.separator("divider", options));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiSeparatorTests, DuplicateKeyAndUnbalancedScopeRejectDeclarations){
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 0.0f, 0.0f, 160.0f, 100.0f }));
    ASSERT_TRUE(m_builder.separator("divider"));
    EXPECT_FALSE(m_builder.separator("divider"));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_FALSE(m_context.finishFrame());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

