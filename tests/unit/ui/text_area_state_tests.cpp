// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/text_area.h>

#include <global/bit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_state_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiTextAreaStateTests, ViewportIntentAwayAndBackRetiresAnEarlierLoan){
    TextAreaState state;
    ASSERT_TRUE(state.scrollTo({ 20.0f, 30.0f }));
    const u64 accepted = state.revision();
    ASSERT_TRUE(state.scrollTo({ 40.0f, 60.0f }));
    ASSERT_TRUE(state.scrollTo({ 20.0f, 30.0f }));
    EXPECT_GT(state.revision(), accepted);
    EXPECT_FLOAT_EQ(state.scroll().x, 20.0f);
    EXPECT_FLOAT_EQ(state.scroll().y, 30.0f);
    const u64 identical = state.revision();
    ASSERT_TRUE(state.scrollTo(state.scroll()));
    EXPECT_GT(state.revision(), identical);
}

TEST(UiTextAreaStateTests, InvalidViewportPreservesBothAxesAndBorrowEpoch){
    TextAreaState state;
    ASSERT_TRUE(state.scrollTo({ 20.0f, 30.0f }));
    const u64 accepted = state.revision();
    const f32 infinity = BitCast<f32>(0x7f800000u);
    const f32 nan = BitCast<f32>(0x7fc00000u);
    const Point invalid[]{ { -1.0f, 0.0f }, { 0.0f, -1.0f }, { infinity, 0.0f }, { 0.0f, infinity }, { nan, nan } };
    for(const Point point : invalid){
        EXPECT_FALSE(state.scrollTo(point));
        EXPECT_EQ(state.revision(), accepted);
        EXPECT_FLOAT_EQ(state.scroll().x, 20.0f);
        EXPECT_FLOAT_EQ(state.scroll().y, 30.0f);
    }
}

TEST(UiTextAreaStateTests, ResetRetiresExternalAndNavigationEpochsWithoutChangingIdentity){
    TextAreaState state;
    const u64 identity = state.instanceGeneration();
    ASSERT_TRUE(state.scrollTo({ 20.0f, 30.0f }));
    const u64 accepted = state.revision();
    const auto navigation = state.navigation().snapshot();
    state.reset();
    EXPECT_EQ(state.instanceGeneration(), identity);
    EXPECT_GT(state.revision(), accepted);
    EXPECT_FALSE(state.navigation().matches(navigation));
    EXPECT_FALSE(state.focused());
    EXPECT_FLOAT_EQ(state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(state.scroll().y, 0.0f);
    const u64 empty = state.revision();
    state.reset();
    EXPECT_GT(state.revision(), empty);
}

TEST(UiTextAreaStateTests, IndependentImmovableOwnersDoNotShareNavigationIdentity){
    TextAreaState first;
    TextAreaState second;
    EXPECT_NE(first.instanceGeneration(), 0u);
    EXPECT_NE(first.instanceGeneration(), second.instanceGeneration());
    EXPECT_FALSE(second.navigation().matches(first.navigation().snapshot()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

