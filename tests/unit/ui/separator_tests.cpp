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

