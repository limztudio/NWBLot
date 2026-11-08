// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/slider.h>

#include <global/simdmath.h>
#include <global/simplemath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_math_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;


TEST(UiSliderMathTests, OutOfRangeAndConstantSpansClampNormalization){
    Expected<f64> normalized = SliderBehavior::Normalize(10.0, 30.0, -100.0);
    ASSERT_TRUE(normalized);
    EXPECT_EQ(*normalized, 0.0);
    normalized = SliderBehavior::Normalize(10.0, 30.0, 100.0);
    ASSERT_TRUE(normalized);
    EXPECT_EQ(*normalized, 1.0);
    normalized = SliderBehavior::Normalize(10.0, 10.0, 100.0);
    ASSERT_TRUE(normalized);
    EXPECT_EQ(*normalized, 0.0);
}

TEST(UiSliderMathTests, OppositeExtremeBoundsNormalizeAndInterpolateWithoutAnOverflowingSpan){
    const f64 maximum = Limit<f64>::s_Max;
    const f64 minimum = -maximum;
    Expected<f64> value = SliderBehavior::Normalize(minimum, maximum, 0.0);
    ASSERT_TRUE(value);
    EXPECT_DOUBLE_EQ(*value, 0.5);
    value = SliderBehavior::Normalize(minimum, maximum, maximum * 0.5);
    ASSERT_TRUE(value);
    EXPECT_DOUBLE_EQ(*value, 0.75);
    value = SliderBehavior::Interpolate(minimum, maximum, 0.25);
    ASSERT_TRUE(value);
    EXPECT_TRUE(IsFinite(*value));
    EXPECT_DOUBLE_EQ(*value / maximum, -0.5);
    value = SliderBehavior::Interpolate(minimum, maximum, 0.75);
    ASSERT_TRUE(value);
    EXPECT_DOUBLE_EQ(*value / maximum, 0.5);
}

TEST(UiSliderMathTests, SameSignExtremeBoundsUseABoundedDifference){
    const f64 maximum = Limit<f64>::s_Max;
    // Lanes are the positive and negative bounds.
    const SIMDVectorDouble bounds = VectorDoubleXorInt(VectorDoubleSet(maximum, maximum, 0.0, 0.0), VectorDoubleAndInt(s_DoubleSIMDNegativeZero, s_DoubleSIMDMaskY));
    const SIMDVectorDouble threeQuarters = VectorDoubleMultiply(bounds, VectorDoubleReplicate(0.75));
    for(const b8 negative : { false, true }){
        const f64 minimum = negative ? VectorDoubleGetY(bounds) : VectorDoubleGetX(threeQuarters);
        const f64 upper = negative ? VectorDoubleGetY(threeQuarters) : maximum;
        const Expected<f64> value = SliderBehavior::Interpolate(minimum, upper, 0.5);
        ASSERT_TRUE(value);
        EXPECT_TRUE(IsFinite(*value));
        EXPECT_GE(*value, minimum);
        EXPECT_LE(*value, upper);
        const Expected<f64> normalized = SliderBehavior::Normalize(minimum, upper, *value);
        ASSERT_TRUE(normalized);
        EXPECT_NEAR(*normalized, 0.5, 1.0e-15);
    }
}

TEST(UiSliderMathTests, TinyAndAdjacentSpansStayInTheOrdinaryNormalizationPath){
    const f64 tiny = BitCast<f64>(1ull);
    Expected<f64> normalized = SliderBehavior::Normalize(-tiny, tiny, 0.0);
    ASSERT_TRUE(normalized);
    EXPECT_DOUBLE_EQ(*normalized, 0.5);
    normalized = SliderBehavior::Normalize(tiny, BitCast<f64>(2ull), tiny);
    ASSERT_TRUE(normalized);
    EXPECT_DOUBLE_EQ(*normalized, 0.0);
    const f64 lower = BitCast<f64>(0x3ff0000000000000ull);
    const f64 upper = BitCast<f64>(0x3ff0000000000001ull);
    normalized = SliderBehavior::Normalize(lower, upper, upper);
    ASSERT_TRUE(normalized);
    EXPECT_DOUBLE_EQ(*normalized, 1.0);
    const Expected<f64> value = SliderBehavior::Interpolate(lower, upper, 1.0);
    ASSERT_TRUE(value);
    EXPECT_EQ(BitCast<u64>(*value), BitCast<u64>(upper));
}

TEST(UiSliderMathTests, InterpolationPreservesExactEndpointAndSignedZeroBits){
    Expected<f64> value = SliderBehavior::Interpolate(-0.0, 1.0, 0.0);
    ASSERT_TRUE(value);
    EXPECT_EQ(BitCast<u64>(*value), 0x8000000000000000ull);
    value = SliderBehavior::Interpolate(-1.0, 0.0, 1.0);
    ASSERT_TRUE(value);
    EXPECT_EQ(BitCast<u64>(*value), 0u);
    value = SliderBehavior::Interpolate(-0.0, 0.0, 0.0);
    ASSERT_TRUE(value);
    EXPECT_EQ(BitCast<u64>(*value), 0x8000000000000000ull);
    value = SliderBehavior::Interpolate(-0.0, 0.0, 1.0);
    ASSERT_TRUE(value);
    EXPECT_EQ(BitCast<u64>(*value), 0u);
}

TEST(UiSliderMathTests, InvalidRangeValueOrNormalizedInputRejectsTheQuery){
    for(u32 field = 0u; field < 7u; ++field){
        f64 minimum = 0.0;
        f64 maximum = 1.0;
        f64 query = 0.5;
        switch(field){
        case 0u: minimum = 2.0; break;
        case 1u: minimum = Limit<f64>::s_QuietNaN; break;
        case 2u: maximum = Limit<f64>::s_Infinity; break;
        case 3u: query = Limit<f64>::s_Infinity; break;
        case 4u: query = Limit<f64>::s_QuietNaN; break;
        case 5u: query = -0.1; break;
        default: query = 1.1; break;
        }
        if(field < 5u){
            EXPECT_FALSE(SliderBehavior::Normalize(minimum, maximum, query));
        }
        EXPECT_FALSE(SliderBehavior::Interpolate(minimum, maximum, query));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


