// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <global/floating_point_environment.h>
#include <global/simdmath.h>
#include <global/compile.h>
#include <global/limit.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_math_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::NearlyEqual;
using NWB::Tests::NearlyEqual3;
using NWB::Tests::NearlyEqual4;


TEST(Math, HalfFloatScalarPreservesSignedZeroLimitsSubnormalsAndTies){
    EXPECT_EQ(ConvertFloatToHalf(0.0f), static_cast<Half>(0x0000u));
    EXPECT_EQ(ConvertFloatToHalf(-0.0f), static_cast<Half>(0x8000u));
    EXPECT_EQ(ConvertFloatToHalf(65504.0f), static_cast<Half>(0x7bffu));
    EXPECT_EQ(ConvertFloatToHalf(Limit<f32>::s_Infinity), static_cast<Half>(0x7c00u));
    EXPECT_EQ(ConvertFloatToHalf(-Limit<f32>::s_Infinity), static_cast<Half>(0xfc00u));

    const Half quietNaN = ConvertFloatToHalf(Limit<f32>::s_QuietNaN);
    EXPECT_EQ((quietNaN & static_cast<Half>(0x7c00u)), static_cast<Half>(0x7c00u));
    EXPECT_NE((quietNaN & static_cast<Half>(0x03ffu)), static_cast<Half>(0u));

    EXPECT_EQ(ConvertFloatToHalf(5.9604644775390625e-8f), static_cast<Half>(0x0001u));
    EXPECT_EQ(ConvertFloatToHalf(6.103515625e-5f), static_cast<Half>(0x0400u));
    EXPECT_EQ(ConvertFloatToHalf(1.00048828125f), static_cast<Half>(0x3c00u));

    EXPECT_TRUE(NearlyEqual(ConvertHalfToFloat(static_cast<Half>(0x0000u)), 0.0f));
    EXPECT_TRUE(SignBit(ConvertHalfToFloat(static_cast<Half>(0x8000u))));
    EXPECT_TRUE(NearlyEqual(ConvertHalfToFloat(static_cast<Half>(0x7bffu)), 65504.0f));
    EXPECT_EQ(ConvertHalfToFloat(static_cast<Half>(0x7c00u)), Limit<f32>::s_Infinity);
    EXPECT_EQ(ConvertHalfToFloat(static_cast<Half>(0xfc00u)), -Limit<f32>::s_Infinity);
    EXPECT_TRUE(IsNaN(ConvertHalfToFloat(static_cast<Half>(0x7e00u))));
}

TEST(Math, UnsignedFloatClampsNegativeOverflowAndNonfiniteInput){
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(0.0f), 0u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(-0.0f), 0u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(-1.0f), 0u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(9.5367431640625e-7f), 0x001u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(6.103515625e-5f), 0x040u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(65536.0f), 0x7c0u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(-Limit<f32>::s_Infinity), 0u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(Limit<f32>::s_Infinity), 0x7c0u);

    const u32 quietNaN = ConvertFloatToUnsignedFloat<6u>(Limit<f32>::s_QuietNaN);
    EXPECT_EQ(quietNaN & 0x7c0u, 0x7c0u);
    EXPECT_NE(quietNaN & 0x03fu, 0u);
}

TEST(Math, PartialHalfFloatBuffersPreserveSubnormalsAndNonfiniteInput){
    Array<f32, 6u> source = { 0.0f, -1.5f, 1.0f, 65504.0f, 5.9604644775390625e-8f, Limit<f32>::s_Infinity };
    Array<Half, 6u> packed = {};
    Array<f32, 6u> unpacked = {};

    ASSERT_EQ(ConvertFloatBufferToHalf(MakeNotNull(packed.data()), MakeNotNull(source.data()), 6u).get(), packed.data());
    EXPECT_EQ(packed[3], static_cast<Half>(0x7bffu));
    EXPECT_EQ(packed[4], static_cast<Half>(0x0001u));
    EXPECT_EQ(packed[5], static_cast<Half>(0x7c00u));

    ASSERT_EQ(ConvertHalfBufferToFloat(MakeNotNull(unpacked.data()), MakeNotNull(packed.data()), 6u).get(), unpacked.data());
    EXPECT_TRUE(NearlyEqual(unpacked[3], 65504.0f));
    EXPECT_TRUE(NearlyEqual(unpacked[4], 5.9604644775390625e-8f));
    EXPECT_EQ(unpacked[5], Limit<f32>::s_Infinity);
}

TEST(Math, HalfFloat4PreservesSignedZeroSubnormalNaNAndInfinity){
    const Half4U specialPacked = MakeHalf4U(-0.0f, 5.9604644775390625e-8f, Limit<f32>::s_QuietNaN, -Limit<f32>::s_Infinity);
    EXPECT_EQ(specialPacked.raw[0], static_cast<Half>(0x8000u));
    EXPECT_EQ(specialPacked.raw[1], static_cast<Half>(0x0001u));
    EXPECT_EQ((specialPacked.raw[2] & static_cast<Half>(0x7c00u)), static_cast<Half>(0x7c00u));
    EXPECT_NE((specialPacked.raw[2] & static_cast<Half>(0x03ffu)), static_cast<Half>(0u));
    EXPECT_EQ(specialPacked.raw[3], static_cast<Half>(0xfc00u));

    const Float4U specialUnpacked = LoadHalf4U(specialPacked);
    EXPECT_TRUE(SignBit(specialUnpacked.x));
    EXPECT_TRUE(NearlyEqual(specialUnpacked.y, 5.9604644775390625e-8f));
    EXPECT_TRUE(IsNaN(specialUnpacked.z));
    EXPECT_EQ(specialUnpacked.w, -Limit<f32>::s_Infinity);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

