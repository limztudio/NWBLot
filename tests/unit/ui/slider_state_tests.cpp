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


TEST(UiSliderStateTests, DefaultStatesOwnDistinctIdentitiesAndInputLifetimes){
    SliderState first;
    SliderState second;
    EXPECT_NE(first.instanceGeneration(), 0u);
    EXPECT_NE(first.inputGeneration(), 0u);
    EXPECT_NE(first.instanceGeneration(), second.instanceGeneration());
    EXPECT_NE(first.inputGeneration(), second.inputGeneration());
    EXPECT_EQ(first.revision(), 1u);
    EXPECT_EQ(BitCast<u64>(first.value()), 0u);
    EXPECT_FALSE(first.result().valid);
    EXPECT_FLOAT_EQ(first.placement().bounds.width, 0.0f);
    EXPECT_FALSE(first.snapshot().press.valid());
    EXPECT_FALSE(first.snapshot().pressMoved);
}

TEST(UiSliderStateTests, ControlTokensProjectTheThreeIndependentLifetimeDomains){
    SliderState state;
    const ControlToken token = state.controlToken();
    EXPECT_EQ(token.instanceGeneration, state.inputGeneration());
    EXPECT_EQ(token.contentGeneration, state.admissionGeneration());
    EXPECT_EQ(token.contentRevision, state.instanceGeneration());
    EXPECT_TRUE(token.valid());
    ASSERT_TRUE(state.setValue(2.0));
    EXPECT_NE(state.controlToken().instanceGeneration, token.instanceGeneration);
    EXPECT_EQ(state.controlToken().contentGeneration, token.contentGeneration);
    EXPECT_EQ(state.controlToken().contentRevision, token.contentRevision);
}

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
        EXPECT_FALSE(state.result().valid);
        EXPECT_FLOAT_EQ(state.placement().bounds.width, 0.0f);
    }
}

TEST(UiSliderStateTests, ResetClearsSemanticAndDiagnosticStateEvenWhenAlreadyReset){
    SliderState state;
    ASSERT_TRUE(state.setValue(-0.0));
    const SliderSnapshot before = state.snapshot();
    state.reset();
    EXPECT_EQ(state.instanceGeneration(), before.instanceGeneration);
    EXPECT_NE(state.inputGeneration(), before.inputGeneration);
    EXPECT_GT(state.admissionGeneration(), before.admissionGeneration);
    EXPECT_EQ(state.revision(), before.revision + 1u);
    EXPECT_EQ(BitCast<u64>(state.value()), 0u);
    EXPECT_FALSE(state.result().valid);
    EXPECT_FALSE(state.snapshot().press.valid());
    EXPECT_FALSE(state.snapshot().pressMoved);
    const SliderSnapshot reset = state.snapshot();
    state.reset();
    EXPECT_NE(state.inputGeneration(), reset.inputGeneration);
    EXPECT_GT(state.admissionGeneration(), reset.admissionGeneration);
    EXPECT_EQ(state.revision(), reset.revision + 1u);
}

TEST(UiSliderStateTests, SnapshotMatchingCoversAllEpochsValueBitsAndPressIdentity){
    SliderState state;
    const SliderSnapshot before = state.snapshot();
    EXPECT_TRUE(state.matches(before));
    for(u32 field = 0u; field < 10u; ++field){
        SliderSnapshot changed = before;
        switch(field){
        case 0u: ++changed.instanceGeneration; break;
        case 1u: ++changed.inputGeneration; break;
        case 2u: ++changed.revision; break;
        case 3u: ++changed.admissionGeneration; break;
        case 4u: changed.valueBits = 0x8000000000000000ull; break;
        case 5u: changed.press.target = { 1u }; break;
        case 6u: ++changed.press.declarationGeneration; break;
        case 7u: ++changed.press.layoutGeneration; break;
        case 8u: ++changed.press.sequence; break;
        default: changed.pressMoved = true; break;
        }
        EXPECT_FALSE(state.matches(changed));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

