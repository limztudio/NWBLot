// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/slider.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_state_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;


TEST(UiSliderStateTests, IdenticalFinitePublicValuesRetireCopiedInputAndAdvanceRevision){
    SliderState state;
    ASSERT_TRUE(state.setValue(2.0));
    const SliderSnapshot before = state.snapshot();
    ASSERT_TRUE(state.setValue(2.0));
    EXPECT_EQ(state.value(), 2.0);
    EXPECT_EQ(state.instanceGeneration(), before.instanceGeneration);
    EXPECT_NE(state.inputGeneration(), before.inputGeneration);
    EXPECT_EQ(state.revision(), before.revision + 1u);
    EXPECT_EQ(state.admissionGeneration(), before.admissionGeneration);
    EXPECT_FALSE(state.matches(before));
}

TEST(UiSliderStateTests, PublicValuesPreserveSignedZeroBitsAndRetireIdenticalSignedZeroIntents){
    SliderState state;
    ASSERT_TRUE(state.setValue(-0.0));
    const SliderSnapshot negative = state.snapshot();
    EXPECT_EQ(negative.valueBits, 0x8000000000000000ull);
    ASSERT_TRUE(state.setValue(-0.0));
    EXPECT_EQ(BitCast<u64>(state.value()), negative.valueBits);
    EXPECT_NE(state.inputGeneration(), negative.inputGeneration);
    ASSERT_TRUE(state.setValue(0.0));
    EXPECT_EQ(BitCast<u64>(state.value()), 0u);
    EXPECT_EQ(state.revision(), negative.revision + 2u);
}

TEST(UiSliderStateTests, ValueAwayAndBackCannotRestoreTheOldSnapshot){
    SliderState state;
    ASSERT_TRUE(state.setValue(2.0));
    const SliderSnapshot before = state.snapshot();
    ASSERT_TRUE(state.setValue(3.0));
    ASSERT_TRUE(state.setValue(2.0));
    EXPECT_EQ(BitCast<u64>(state.value()), before.valueBits);
    EXPECT_NE(state.inputGeneration(), before.inputGeneration);
    EXPECT_EQ(state.revision(), before.revision + 2u);
    EXPECT_FALSE(state.matches(before));
}

TEST(UiSliderStateTests, NonfinitePublicValuesAreRejectedAtomically){
    SliderState state;
    ASSERT_TRUE(state.setValue(-0.0));
    const SliderSnapshot before = state.snapshot();
    for(const f64 invalid : { Limit<f64>::s_QuietNaN, Limit<f64>::s_Infinity, -Limit<f64>::s_Infinity }){
        EXPECT_FALSE(state.setValue(invalid));
        EXPECT_TRUE(state.matches(before));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

