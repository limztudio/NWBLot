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


TEST(Math, RawFloatLaneHashKeepsSignedZeroAndNanPayloads){
    const Float4 values{ BitCast<f32>(0x80000000u), BitCast<f32>(0x7fc01234u), BitCast<f32>(0x00000000u), BitCast<f32>(0xffc05678u) };
    usize actual = 17u;
    usize expected = actual;
    expected = HashCombineValue(expected, 0x80000000u);
    expected = HashCombineValue(expected, 0x7fc01234u);
    expected = HashCombineValue(expected, 0x00000000u);
    expected = HashCombineValue(expected, 0xffc05678u);
    HashCombineF32x4(LoadFloat(values), actual);
    EXPECT_EQ(actual, expected);
}

TEST(Math, DoubleComparisonAndSelectionKeepAll64MaskBitsAndNaNOrdering){
    const SIMDVectorDouble lhs{ 0x1.0000000000001p+0, -0.0, Limit<f64>::s_QuietNaN, 0x1p+40 };
    const SIMDVectorDouble rhs{ 1.0, 0.0, Limit<f64>::s_QuietNaN, 0x1p+40 + 1.0 };
    const SIMDVectorDouble equal = VectorDoubleEqual(lhs, rhs);
    const SIMDVectorDouble greater = VectorDoubleGreater(lhs, rhs);
    const Array<u64, 4u> equalBits = { 0u, Limit<u64>::s_Max, 0u, 0u };
    const Array<u64, 4u> greaterBits = { Limit<u64>::s_Max, 0u, 0u, 0u };
    for(usize lane = 0u; lane < 4u; ++lane){
        EXPECT_EQ(BitCast<u64>(equal[lane]), equalBits[lane]) << lane;
        EXPECT_EQ(BitCast<u64>(greater[lane]), greaterBits[lane]) << lane;
    }

    const SIMDVectorDouble first = VectorDoubleSetInt(0x0123456789abcdefu, 0x8000000000000000u, 0x7ff8000000001234u, 1u);
    const SIMDVectorDouble second = VectorDoubleSetInt(0xfedcba9876543210u, 0u, 0x7ff800009abcdef0u, Limit<u64>::s_Max);
    const SIMDVectorDouble control = VectorDoubleSetInt(0xffffffff00000000u, 0x8000000000000000u, 0x00000000ffffffffu, 0u);
    const SIMDVectorDouble selected = VectorDoubleSelect(first, second, control);
    const Array<u64, 4u> selectedBits = { 0xfedcba9889abcdefu, 0u, 0x7ff800009abcdef0u, 1u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(BitCast<u64>(selected[lane]), selectedBits[lane]) << lane;
}

TEST(Math, DoubleIntegerAdditionWrapsAcross64BitLaneBoundaries){
    const SIMDVectorDouble sum = VectorDoubleAddInt(VectorDoubleSetInt(Limit<u64>::s_Max, 0xffffffffu, 0x8000000000000000u, 0xfffffffffffffffeu), VectorDoubleSetInt(1u, 1u, 0x8000000000000000u, 3u));
    const Array<u64, 4u> expected = { 0u, 0x100000000u, 0u, 1u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(BitCast<u64>(sum[lane]), expected[lane]) << lane;
}

TEST(Math, DoubleDotAndNearlyParallelCrossRetainResidualBeyondFloatPrecision){
    const SIMDVectorDouble dot = VectorDouble3Dot(SIMDVectorDouble{ 0x1p+40 + 1.0, -0x1p+40, 0.0, Limit<f64>::s_QuietNaN }, SIMDVectorDouble{ 1.0, 1.0, 1.0, Limit<f64>::s_Infinity });
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(dot[lane], 1.0) << lane;

    const SIMDVectorDouble cross = VectorDouble3Cross(SIMDVectorDouble{ 1.0, 1.0 + 0x1p-40, 0.0, Limit<f64>::s_QuietNaN }, SIMDVectorDouble{ 1.0, 1.0, 0.0, Limit<f64>::s_Infinity });
    EXPECT_EQ(cross.z, -0x1p-40);
    EXPECT_EQ(BitCast<u64>(cross.w), 0u);
}

TEST(Math, SinglePartialLaneDotIgnoresNonfiniteInactiveLanesWithoutInvalidExceptions){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    NWB::Tests::FloatingPointEnvironmentGuard restoreEnvironment(*originalEnvironment);

    Array<Array<volatile u32, 2u>, 4u> inactiveBits = {{ { 0x7f800000u, 0u }, { 0u, 0x7f800000u }, { 0x7fc01234u, 0u }, { 0x7f800001u, 0u } }};
    for(usize index = 0u; index < LengthOf(inactiveBits); ++index){
        const f32 inactiveLeft = BitCast<f32>(inactiveBits[index][0]);
        const f32 inactiveRight = BitCast<f32>(inactiveBits[index][1]);
        const SIMDVectorSingle lhs2 = VectorSet(2.0f, 3.0f, inactiveLeft, inactiveLeft);
        const SIMDVectorSingle rhs2 = VectorSet(5.0f, 7.0f, inactiveRight, inactiveRight);
        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        const SIMDVectorSingle dot2 = Vector2Dot(lhs2, rhs2);
        const i32 dot2Invalid = TestFloatingPointExceptions(FE_INVALID);
        EXPECT_EQ(dot2Invalid, 0) << index;
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(VectorGetByIndex(dot2, lane), 31.0f) << index << ':' << lane;

        const SIMDVectorSingle lhs3 = VectorSet(2.0f, 3.0f, 4.0f, inactiveLeft);
        const SIMDVectorSingle rhs3 = VectorSet(5.0f, 7.0f, 11.0f, inactiveRight);
        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        const SIMDVectorSingle dot3 = Vector3Dot(lhs3, rhs3);
        const i32 dot3Invalid = TestFloatingPointExceptions(FE_INVALID);
        EXPECT_EQ(dot3Invalid, 0) << index;
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(VectorGetByIndex(dot3, lane), 75.0f) << index << ':' << lane;
    }
}

TEST(Math, SinglePartialLaneCrossIgnoresNonfiniteAndOverflowingInactiveLanes){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    NWB::Tests::FloatingPointEnvironmentGuard restoreEnvironment(*originalEnvironment);

    Array<Array<volatile u32, 2u>, 6u> inactiveBits = {{ { 0x7f800000u, 0u }, { 0u, 0x7f800000u }, { 0x7fc01234u, 0u }, { 0x7f800001u, 0u }, { 0u, 0x7f800001u }, { 0x7f7fffffu, 0x7f7fffffu } }};
    for(usize index = 0u; index < LengthOf(inactiveBits); ++index){
        const SIMDVectorSingle lhs2 = VectorSetInt(0x40000000u, 0x40400000u, inactiveBits[index][0], inactiveBits[index][0]);
        const SIMDVectorSingle rhs2 = VectorSetInt(0x40a00000u, 0x40e00000u, inactiveBits[index][1], inactiveBits[index][1]);
        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        const SIMDVectorSingle cross2 = Vector2Cross(lhs2, rhs2);
        const i32 cross2Exceptions = TestFloatingPointExceptions(FE_INVALID | FE_OVERFLOW);
        EXPECT_EQ(cross2Exceptions, 0) << index;
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(VectorGetByIndex(cross2, lane), -1.0f) << index << ':' << lane;

        const SIMDVectorSingle lhs3 = VectorSetInt(0x40000000u, 0x40400000u, 0x40800000u, inactiveBits[index][0]);
        const SIMDVectorSingle rhs3 = VectorSetInt(0x40a00000u, 0x40e00000u, 0x41300000u, inactiveBits[index][1]);
        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        const SIMDVectorSingle cross3 = Vector3Cross(lhs3, rhs3);
        const i32 cross3Exceptions = TestFloatingPointExceptions(FE_INVALID | FE_OVERFLOW);
        EXPECT_EQ(cross3Exceptions, 0) << index;
        const Array<f32, 3u> cross3Expected = { 5.0f, -2.0f, -1.0f };
        for(usize lane = 0u; lane < 3u; ++lane)
            EXPECT_EQ(VectorGetByIndex(cross3, lane), cross3Expected[lane]) << index << ':' << lane;
        EXPECT_EQ(VectorGetIntW(cross3), 0u) << index;
    }

    // An active infinite component must not leak into the zeroed inactive product lanes through the XY swap.
    const SIMDVectorSingle lhs = VectorSet(2.0f, 3.0f, 0.0f, 0.0f);
    const SIMDVectorSingle rhs = VectorSet(Limit<f32>::s_Infinity, 7.0f, 0.0f, 0.0f);
    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const SIMDVectorSingle cross = Vector2Cross(lhs, rhs);
    const i32 crossExceptions = TestFloatingPointExceptions(FE_INVALID | FE_OVERFLOW);
    EXPECT_EQ(crossExceptions, 0);
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorGetByIndex(cross, lane), -Limit<f32>::s_Infinity) << lane;
}

TEST(Math, DoublePartialLaneDotAndCrossIgnoreNonfiniteInactiveLanesWithoutInvalidExceptions){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    NWB::Tests::FloatingPointEnvironmentGuard restoreEnvironment(*originalEnvironment);
    const Array<f64, 4u> lhsInactive = { Limit<f64>::s_Infinity, 0.0, Limit<f64>::s_QuietNaN, Limit<f64>::s_QuietNaN };
    const Array<f64, 4u> rhsInactive = { 0.0, Limit<f64>::s_Infinity, 0.0, Limit<f64>::s_QuietNaN };
    for(usize index = 0u; index < LengthOf(lhsInactive); ++index){
        const SIMDVectorDouble lhs2{ 2.0, 3.0, lhsInactive[index], lhsInactive[index] };
        const SIMDVectorDouble rhs2{ 5.0, 7.0, rhsInactive[index], rhsInactive[index] };
        EXPECT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        const SIMDVectorDouble dot2 = VectorDouble2Dot(lhs2, rhs2);
        const i32 dot2Invalid = TestFloatingPointExceptions(FE_INVALID);
        EXPECT_EQ(dot2Invalid, 0) << index;
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(dot2[lane], 31.0) << index << ':' << lane;

        EXPECT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        const SIMDVectorDouble cross2 = VectorDouble2Cross(lhs2, rhs2);
        const i32 cross2Invalid = TestFloatingPointExceptions(FE_INVALID);
        EXPECT_EQ(cross2Invalid, 0) << index;
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(cross2[lane], -1.0) << index << ':' << lane;

        const SIMDVectorDouble lhs3{ 2.0, 3.0, 4.0, lhsInactive[index] };
        const SIMDVectorDouble rhs3{ 5.0, 7.0, 11.0, rhsInactive[index] };
        EXPECT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        const SIMDVectorDouble dot3 = VectorDouble3Dot(lhs3, rhs3);
        const i32 dot3Invalid = TestFloatingPointExceptions(FE_INVALID);
        EXPECT_EQ(dot3Invalid, 0) << index;
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(dot3[lane], 75.0) << index << ':' << lane;

        EXPECT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        const SIMDVectorDouble cross3 = VectorDouble3Cross(lhs3, rhs3);
        const i32 cross3Invalid = TestFloatingPointExceptions(FE_INVALID);
        EXPECT_EQ(cross3Invalid, 0) << index;
        const SIMDVectorDouble cross3Expected{ 5.0, -2.0, -1.0, 0.0 };
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(cross3[lane], cross3Expected[lane]) << index << ':' << lane;
    }
}

TEST(Math, SingleLog2KeepsSubnormalExponentsAndZeroInfinityNaNDomains){
    for(u32 bitIndex = 0u; bitIndex < 23u; ++bitIndex){
        volatile u32 subnormalBits = 1u << bitIndex;
        const SIMDVectorSingle input = VectorSetInt(subnormalBits, 0x007fffffu, 0x00800000u, 0x3f800000u);
        const SIMDVectorSingle logarithm = VectorLog2(input);
        EXPECT_EQ(VectorGetX(logarithm), static_cast<f32>(static_cast<i32>(bitIndex) - 149)) << bitIndex;
        EXPECT_NEAR(VectorGetY(logarithm), -126.0f, 0.00004f) << bitIndex;
        EXPECT_EQ(VectorGetZ(logarithm), -126.0f) << bitIndex;
        EXPECT_EQ(VectorGetW(logarithm), 0.0f) << bitIndex;
    }
    Array<volatile u32, 4u> domainBits = { 0x80000000u, 0x7f800000u, 0xbf800000u, 0x7fc01234u };
    const SIMDVectorSingle special = VectorLog2(VectorSetInt(domainBits[0], domainBits[1], domainBits[2], domainBits[3]));
    EXPECT_EQ(VectorGetIntX(special), 0xff800000u);
    EXPECT_EQ(VectorGetIntY(special), 0x7f800000u);
    EXPECT_TRUE(IsNaN(VectorGetZ(special)));
    EXPECT_TRUE(IsNaN(VectorGetW(special)));
}

TEST(Math, DoubleTranscendentalsHandleLargeArgumentsNonfiniteDomainsAndSignedZero){
    const SIMDVectorDouble angles{ 0x1p+40, -0x1p+100, 0x1p+900, -0.0 };
    SIMDVectorDouble sine{};
    SIMDVectorDouble cosine{};
    VectorDoubleSinCos(sine, cosine, angles);
    for(usize lane = 0u; lane < 4u; ++lane){
        EXPECT_NEAR(sine[lane], Sin(angles[lane]), 2.0e-15) << lane;
        EXPECT_NEAR(cosine[lane], Cos(angles[lane]), 2.0e-15) << lane;
    }
    EXPECT_TRUE(SignBit(sine.w));

    const SIMDVectorDouble invalidSine = VectorDoubleSin(SIMDVectorDouble{ Limit<f64>::s_Infinity, -Limit<f64>::s_Infinity, Limit<f64>::s_QuietNaN, -0.0 });
    EXPECT_TRUE(IsNaN(invalidSine.x));
    EXPECT_TRUE(IsNaN(invalidSine.y));
    EXPECT_TRUE(IsNaN(invalidSine.z));
    EXPECT_TRUE(SignBit(invalidSine.w));

    const SIMDVectorDouble logarithm = VectorDoubleLog2(SIMDVectorDouble{ -1.0, 0.0, 0x1p-1074, Limit<f64>::s_Infinity });
    EXPECT_TRUE(IsNaN(logarithm.x));
    EXPECT_EQ(logarithm.y, -Limit<f64>::s_Infinity);
    EXPECT_EQ(logarithm.z, -1074.0);
    EXPECT_EQ(logarithm.w, Limit<f64>::s_Infinity);
    const SIMDVectorDouble exponential = VectorDoubleExp2(SIMDVectorDouble{ 1024.0, -1074.0, -1075.0, Limit<f64>::s_QuietNaN });
    EXPECT_EQ(exponential.x, Limit<f64>::s_Infinity);
    EXPECT_EQ(BitCast<u64>(exponential.y), 1u);
    EXPECT_EQ(BitCast<u64>(exponential.z), 0u);
    EXPECT_TRUE(IsNaN(exponential.w));

    const SIMDVectorDouble tangent = VectorDoubleTanH(SIMDVectorDouble{ Limit<f64>::s_Infinity, -Limit<f64>::s_Infinity, -0.0, 0.0 });
    EXPECT_EQ(tangent.x, 1.0);
    EXPECT_EQ(tangent.y, -1.0);
    EXPECT_TRUE(SignBit(tangent.z));
    EXPECT_FALSE(SignBit(tangent.w));
}

TEST(Math, DoubleModAvoidsQuotientOverflowAndUnderflowWhileKeepingDivisorSign){
    const SIMDVectorDouble overflow = VectorDoubleMod(SIMDVectorDouble{ Limit<f64>::s_Max, -Limit<f64>::s_Max, Limit<f64>::s_Max, -Limit<f64>::s_Max }, SIMDVectorDouble{ 0x1p-1000, 0x1p-1000, -0x1p-1000, -0x1p-1000 });
    const Array<u64, 4u> overflowBits = { 0u, 0u, 0x8000000000000000u, 0x8000000000000000u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(BitCast<u64>(overflow[lane]), overflowBits[lane]) << lane;

    const SIMDVectorDouble underflow = VectorDoubleMod(SIMDVectorDouble{ 0x1p-1074, -0x1p-1074, 0x1p-1074, -0x1p-1074 }, SIMDVectorDouble{ Limit<f64>::s_Max, Limit<f64>::s_Max, -Limit<f64>::s_Max, -Limit<f64>::s_Max });
    const SIMDVectorDouble underflowExpected{ 0x1p-1074, Limit<f64>::s_Max, -Limit<f64>::s_Max, -0x1p-1074 };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(underflow[lane], underflowExpected[lane]) << lane;

    const SIMDVectorDouble exceptional = VectorDoubleMod(SIMDVectorDouble{ Limit<f64>::s_Infinity, Limit<f64>::s_QuietNaN, 1.0, Limit<f64>::s_Max }, SIMDVectorDouble{ 1.0, 1.0, 0.0, Limit<f64>::s_Infinity });
    EXPECT_TRUE(IsNaN(exceptional.x));
    EXPECT_TRUE(IsNaN(exceptional.y));
    EXPECT_TRUE(IsNaN(exceptional.z));
    EXPECT_EQ(exceptional.w, Limit<f64>::s_Max);
}

TEST(Math, DoubleModAnglesKeepsLargeFiniteArgumentsAndTiesEvenAtPiBoundaries){
    const SIMDVectorDouble huge{ Limit<f64>::s_Max, -Limit<f64>::s_Max, 0x1p+900, -0x1p+900 };
    const SIMDVectorDouble reduced = VectorDoubleModAngles(huge);
    for(usize lane = 0u; lane < 4u; ++lane){
        EXPECT_TRUE(IsFinite(reduced[lane])) << lane;
        EXPECT_EQ(BitCast<u64>(reduced[lane]), BitCast<u64>(Remainder(huge[lane], s_Double2PI))) << lane;
        EXPECT_LE(Abs(reduced[lane]), s_DoublePI) << lane;
    }
    const SIMDVectorDouble ties = VectorDoubleModAngles(SIMDVectorDouble{ s_DoublePI, -s_DoublePI, 3.0 * s_DoublePI, -3.0 * s_DoublePI });
    const SIMDVectorDouble tiesExpected{ s_DoublePI, -s_DoublePI, -s_DoublePI, s_DoublePI };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(ties[lane], tiesExpected[lane]) << lane;

    const SIMDVectorDouble exceptional = VectorDoubleModAngles(SIMDVectorDouble{ Limit<f64>::s_Infinity, -Limit<f64>::s_Infinity, Limit<f64>::s_QuietNaN, -0.0 });
    EXPECT_TRUE(IsNaN(exceptional.x));
    EXPECT_TRUE(IsNaN(exceptional.y));
    EXPECT_TRUE(IsNaN(exceptional.z));
    EXPECT_TRUE(SignBit(exceptional.w));
}

TEST(Math, DoubleNormalizationRejectsDegenerateNonfiniteAndUnderflowedLengths){
    const SIMDVectorDouble fallback{ 0.0, 1.0, 0.0, 9.0 };
    alignas(SIMDVectorDouble) const Array<SIMDVectorDouble, 4u> invalid = {
        SIMDVectorDouble{ 0.0, 0.0, 0.0, 5.0 },
        SIMDVectorDouble{ Limit<f64>::s_Infinity, 0.0, 0.0, 0.0 },
        SIMDVectorDouble{ 0.0, Limit<f64>::s_QuietNaN, 0.0, 0.0 },
        SIMDVectorDouble{ 0x1p-1074, 0.0, 0.0, 0.0 },
    };
    for(const SIMDVectorDouble input : invalid){
        EXPECT_FALSE(VectorDouble3TryNormalize(input));
        const SIMDVectorDouble normalized = VectorDouble3NormalizeOr(input, fallback, 0.0);
        EXPECT_EQ(normalized.x, 0.0);
        EXPECT_EQ(normalized.y, 1.0);
        EXPECT_EQ(normalized.z, 0.0);
        EXPECT_EQ(normalized.w, 0.0);
    }
    const SIMDVectorDouble belowThreshold = VectorDouble3NormalizeOr(SIMDVectorDouble{ 0x1p-41, 0.0, 0.0, 0.0 }, fallback, 0x1p-80);
    EXPECT_EQ(belowThreshold.y, 1.0);
    const SIMDVectorDouble aboveThreshold = VectorDouble3NormalizeOr(SIMDVectorDouble{ 0x1p-39, 0.0, 0.0, 0.0 }, fallback, 0x1p-80);
    EXPECT_EQ(aboveThreshold.x, 1.0);
    EXPECT_EQ(aboveThreshold.y, 0.0);
}

TEST(Math, DoubleRefractionDistinguishesCriticalAngleOnEitherSideOfBinary64Threshold){
    const SIMDVectorDouble incident{ 1.0, 0.0, 0.0, 0.0 };
    const SIMDVectorDouble normal{ 0.0, 1.0, 0.0, 0.0 };
    const SIMDVectorDouble critical = VectorDouble3Refract(incident, normal, 1.0);
    EXPECT_EQ(critical.x, 1.0);
    EXPECT_EQ(critical.y, 0.0);
    const f64 belowIndex = 1.0 - 0x1p-40;
    const SIMDVectorDouble below = VectorDouble3Refract(incident, normal, belowIndex);
    EXPECT_EQ(below.x, belowIndex);
    EXPECT_NEAR(below.y, -Sqrt(1.0 - belowIndex * belowIndex), 1.0e-20);
    EXPECT_LT(below.y, 0.0);
    const SIMDVectorDouble above = VectorDouble3Refract(incident, normal, 1.0 + 0x1p-40);
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(above[lane], 0.0) << lane;
}

TEST(Math, HyperbolicTangentSaturatesAndPreservesSignedZero){
    const f32 infinity = Limit<f32>::s_Infinity;
    EXPECT_TRUE(NearlyEqual4(VectorTanH(VectorSet(50.0f, -50.0f, infinity, -infinity)), 1.0f, -1.0f, 1.0f, -1.0f));

    const SIMDVectorSingle signedZeroTanH = VectorTanH(VectorSet(-0.0f, 0.0f, -0.0f, 0.0f));
    EXPECT_TRUE(SignBit(VectorGetX(signedZeroTanH)));
    EXPECT_FALSE(SignBit(VectorGetY(signedZeroTanH)));
}

TEST(Math, VectorIntegerAddWrapsAtUnsignedLaneLimits){
    const SIMDVectorSingle sum = VectorAddInt(VectorSetInt(Limit<u32>::s_Max, 4u, 0xFFFFFFFEu, 19u), VectorSetInt(1u, 7u, 3u, Limit<u32>::s_Max));

    EXPECT_EQ(VectorGetIntX(sum), 0u);
    EXPECT_EQ(VectorGetIntZ(sum), 1u);
    EXPECT_EQ(VectorGetIntW(sum), 18u);
}

TEST(Math, RefractCriticalAngle){
    const SIMDVectorSingle normal = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVectorSingle criticalIncident = VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVectorSingle totalInternalIncident = VectorSet(0.866025404f, -0.5f, 0.0f, 0.0f);

    EXPECT_TRUE(NearlyEqual4(Vector2Refract(criticalIncident, normal, 1.0f), 1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual4(Vector3Refract(criticalIncident, normal, 1.0f), 1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual4(Vector4Refract(criticalIncident, normal, 1.0f), 1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual4(Vector3Refract(totalInternalIncident, normal, 2.0f), 0.0f, 0.0f, 0.0f, 0.0f));
}

TEST(Math, NormalizeOrUsesFallbackForZeroAndNonfiniteInput){
    const SIMDVectorSingle fallback = VectorSet(0.0f, 1.0f, 0.0f, 1.0f);
    const f32 infinity = Limit<f32>::s_Infinity;

    EXPECT_TRUE(NearlyEqual3(Vector3NormalizeOr(VectorSet(0.0f, 0.0f, 0.0f, 5.0f), fallback, 0.0f), 0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual4(Vector4NormalizeOr(VectorSet(infinity, 0.0f, 0.0f, 0.0f), fallback, 0.0f), 0.0f, 1.0f, 0.0f, 1.0f));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

