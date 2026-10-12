// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>

#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/simdmath.h>
#include <global/compile.h>
#include <global/limit.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>

#include <cfenv>
#include <fenv.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_math_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::NearlyEqual;
using NWB::Tests::NearlyEqual3;
using NWB::Tests::NearlyEqual4;


NWB_NOINLINE f32 LegacyMultiplyAddExpression(const f32 first, const f32 second, const f32 addend)noexcept{
    return first * second + addend;
}

NWB_NOINLINE f32 LegacyNegativeMultiplySubtractExpression(const f32 first, const f32 second, const f32 addend)noexcept{
    return addend - first * second;
}

NWB_NOINLINE f64 LegacyDoubleMultiplyAddExpression(const f64 first, const f64 second, const f64 addend)noexcept{
    return first * second + addend;
}

NWB_NOINLINE f64 LegacyDoubleMultiplyThenAdd(const f64 first, const f64 second, const f64 addend)noexcept{
    const f64 product = first * second;
    return product + addend;
}

NWB_NOINLINE SIMDVectorDouble DoubleMultiplyAddExpression(const SIMDVectorDouble first, const SIMDVectorDouble second, const SIMDVectorDouble addend)noexcept{
    return first * second + addend;
}

NWB_NOINLINE SIMDVectorDouble DoubleMultiplyThenAdd(const SIMDVectorDouble first, const SIMDVectorDouble second, const SIMDVectorDouble addend)noexcept{
    const SIMDVectorDouble product = first * second;
    return product + addend;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Math, MultiplyExpressionsPreserveCompilerRoundingPolicyAndSignedZero){
    // The first two products lose a 2^-26 residual when rounded separately; contraction retains it.
    const f32 first[]{ 0x1.0008p+0f, -0x1.0008p+0f, 0.0f, -0.0f };
    const f32 second[]{ 0x1.fffp-1f, 0x1.fffp-1f, -0.0f, 1.0f };
    const f32 addend[]{ -1.0f, 1.0f, -0.0f, -0.0f };
    const f32 difference[]{ 1.0f, -1.0f, -0.0f, 0.0f };
    const SIMDVectorSingle lhs = VectorSet(first[0], first[1], first[2], first[3]);
    const SIMDVectorSingle rhs = VectorSet(second[0], second[1], second[2], second[3]);
    const SIMDVectorSingle sum = VectorMultiplyAddExpression(lhs, rhs, VectorSet(addend[0], addend[1], addend[2], addend[3]));
    const SIMDVectorSingle subtraction = VectorNegativeMultiplySubtractExpression(lhs, rhs, VectorSet(difference[0], difference[1], difference[2], difference[3]));
    for(usize lane = 0u; lane < 4u; ++lane){
        EXPECT_EQ(BitCast<u32>(VectorGetByIndex(sum, lane)), BitCast<u32>(LegacyMultiplyAddExpression(first[lane], second[lane], addend[lane]))) << lane;
        EXPECT_EQ(BitCast<u32>(VectorGetByIndex(subtraction, lane)), BitCast<u32>(LegacyNegativeMultiplySubtractExpression(first[lane], second[lane], difference[lane]))) << lane;
    }
}

TEST(Math, DoubleVectorRetainsPrecisionCancellationSubnormalsAndSignedZero){
    const SIMDVectorDouble precision = SIMDVectorDouble{ 0x1p+40, 0x1p-40, -0x1p+40, -0x1p-40 } + SIMDVectorDouble{ 1., 0x1p-80, -1., -0x1p-80 };
    EXPECT_EQ(precision.x, 0x1.0000000001p+40);
    EXPECT_EQ(precision.y, 0x1.0000000001p-40);
    EXPECT_EQ(precision.z, -0x1.0000000001p+40);
    EXPECT_EQ(precision.w, -0x1.0000000001p-40);

    // Separately rounded products lose the 2^-54 residual; expression contraction follows the scalar compiler policy.
    const SIMDVectorDouble first{ 0x1.0000002p+0, -0x1.0000002p+0, 0.0, -0.0 };
    const SIMDVectorDouble second{ 0x1.ffffffcp-1, 0x1.ffffffcp-1, -0.0, 1.0 };
    const SIMDVectorDouble addend{ -1., 1., -0., -0. };
    const SIMDVectorDouble expression = DoubleMultiplyAddExpression(first, second, addend);
    const SIMDVectorDouble separate = DoubleMultiplyThenAdd(first, second, addend);
    const SIMDVectorDouble namedExpression = VectorDoubleMultiplyAddExpression(first, second, addend);
    for(usize lane = 0u; lane < 4u; ++lane){
        const u64 expectedExpression = BitCast<u64>(LegacyDoubleMultiplyAddExpression(first[lane], second[lane], addend[lane]));
        EXPECT_EQ(BitCast<u64>(expression[lane]), expectedExpression) << lane;
        EXPECT_EQ(BitCast<u64>(namedExpression[lane]), expectedExpression) << lane;
        EXPECT_EQ(BitCast<u64>(separate[lane]), BitCast<u64>(LegacyDoubleMultiplyThenAdd(first[lane], second[lane], addend[lane]))) << lane;
    }

    const SIMDVectorDouble subnormal = SIMDVectorDouble{ BitCast<f64>(u64{ 1u }), BitCast<f64>(u64{ 3u }), -BitCast<f64>(u64{ 1u }), -BitCast<f64>(u64{ 3u }) } * SIMDVectorDouble{ 2., 0.5, 2., 0.5 };
    EXPECT_EQ(BitCast<u64>(subnormal.x), 2u);
    EXPECT_EQ(BitCast<u64>(subnormal.y), 2u);
    EXPECT_EQ(BitCast<u64>(subnormal.z), 0x8000000000000002u);
    EXPECT_EQ(BitCast<u64>(subnormal.w), 0x8000000000000002u);
    const SIMDVectorDouble signedZero = SIMDVectorDouble{ -0., 0., -0., 0. } * SIMDVectorDouble{ 1., -1., -1., 1. };
    EXPECT_TRUE(SignBit(signedZero.x));
    EXPECT_TRUE(SignBit(signedZero.y));
    EXPECT_FALSE(SignBit(signedZero.z));
    EXPECT_FALSE(SignBit(signedZero.w));
}

TEST(Math, DoubleVectorFloorAndCeilingPreserveBinary64BoundaryValues){
    const SIMDVectorDouble inputs[]{
        { 0.0, -0.0, 0x1p-1022, -0x1p-1022 },
        { BitCast<f64>(u64{ 1u }), -BitCast<f64>(u64{ 1u }), BitCast<f64>(u64{ 3u }), -BitCast<f64>(u64{ 3u }) },
        { 0x1.fffffffffffffp-1, 0x1.0000000000001p+0, -0x1.fffffffffffffp-1, -0x1.0000000000001p+0 },
        { 0x1p+53, -0x1p+53, 0x1.fffffffffffffp+51, -0x1.fffffffffffffp+51 },
        { Limit<f64>::s_Infinity, -Limit<f64>::s_Infinity, Limit<f64>::s_Max, -Limit<f64>::s_Max },
        { Limit<f64>::s_QuietNaN, -Limit<f64>::s_QuietNaN, 0x1.0000000000001p+52, -0x1.0000000000001p+52 },
    };
    for(const SIMDVectorDouble input : inputs){
        const SIMDVectorDouble floor = VectorDoubleFloor(input);
        const SIMDVectorDouble ceiling = VectorDoubleCeiling(input);
        const SIMDVectorDouble truncate = VectorDoubleTruncate(input);
        for(usize lane = 0u; lane < 4u; ++lane){
            if(IsNaN(input[lane])){
                EXPECT_TRUE(IsNaN(floor[lane]));
                EXPECT_TRUE(IsNaN(ceiling[lane]));
                EXPECT_TRUE(IsNaN(truncate[lane]));
                continue;
            }
            EXPECT_EQ(BitCast<u64>(floor[lane]), BitCast<u64>(Floor(input[lane]))) << lane;
            EXPECT_EQ(BitCast<u64>(ceiling[lane]), BitCast<u64>(Ceil(input[lane]))) << lane;
            EXPECT_EQ(BitCast<u64>(truncate[lane]), BitCast<u64>(std::trunc(input[lane]))) << lane;
        }
    }
}

TEST(Math, DoubleVectorRoundPreservesTiesSignedZeroAndLargeIntegralLanes){
    const SIMDVectorDouble inputs[]{
        { 0.5, -0.5, 1.5, -1.5 },
        { 2.5, -2.5, 0x1.fffffffffffffp-2, -0x1.fffffffffffffp-2 },
        { 0x1p+53, -0x1p+53, 0x1.0000000000001p+52, -0x1.0000000000001p+52 },
        { -0.0, 0.0, Limit<f64>::s_Infinity, -Limit<f64>::s_Infinity },
    };
    const SIMDVectorDouble expected[]{
        { 0.0, -0.0, 2.0, -2.0 },
        { 2.0, -2.0, 0.0, -0.0 },
        { 0x1p+53, -0x1p+53, 0x1.0000000000001p+52, -0x1.0000000000001p+52 },
        { -0.0, 0.0, Limit<f64>::s_Infinity, -Limit<f64>::s_Infinity },
    };
    for(usize index = 0u; index < LengthOf(inputs); ++index){
        const SIMDVectorDouble rounded = VectorDoubleRound(inputs[index]);
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(BitCast<u64>(rounded[lane]), BitCast<u64>(expected[index][lane])) << index << ':' << lane;
    }
}

TEST(Math, DoubleFixedRoundingIgnoresAmbientDirectedModesAndKeepsNearestTiesAndSignedZero){
#pragma STDC FENV_ACCESS ON
    fenv_t originalEnvironment{};
    ASSERT_EQ(feholdexcept(&originalEnvironment), 0);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(fesetenv(&originalEnvironment) != 0)
            NWB_ASSERT(false);
    });

    const i32 roundingModes[]{ FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    volatile f64 runtimeInputs[][4]{
        { 0.5, -0.5, 1.5, -1.5 },
        { 0.25, -0.25, 2.5, -2.5 },
        { -0.0, 0.0, 0x1p+53, -0x1p+53 },
        { 0x1p-1074, -0x1p-1074, 0x1.fffffffffffffp+51, -0x1.fffffffffffffp+51 },
    };
    const SIMDVectorDouble expected[][4]{
        {
            { 0.0, -0.0, 2.0, -2.0 },
            { 0.0, -0.0, 2.0, -2.0 },
            { -0.0, 0.0, 0x1p+53, -0x1p+53 },
            { 0.0, -0.0, 0x1p+52, -0x1p+52 },
        },
        {
            { 0.0, -0.0, 1.0, -1.0 },
            { 0.0, -0.0, 2.0, -2.0 },
            { -0.0, 0.0, 0x1p+53, -0x1p+53 },
            { 0.0, -0.0, 0x1.ffffffffffffep+51, -0x1.ffffffffffffep+51 },
        },
        {
            { 0.0, -1.0, 1.0, -2.0 },
            { 0.0, -1.0, 2.0, -3.0 },
            { -0.0, 0.0, 0x1p+53, -0x1p+53 },
            { 0.0, -1.0, 0x1.ffffffffffffep+51, -0x1p+52 },
        },
        {
            { 1.0, -0.0, 2.0, -1.0 },
            { 1.0, -0.0, 3.0, -2.0 },
            { -0.0, 0.0, 0x1p+53, -0x1p+53 },
            { 1.0, -0.0, 0x1p+52, -0x1.ffffffffffffep+51 },
        },
    };
    for(const i32 mode : roundingModes){
        const i32 setStatus = fesetround(mode);
        EXPECT_EQ(setStatus, 0);
        if(setStatus != 0)
            continue;
        for(usize row = 0u; row < LengthOf(runtimeInputs); ++row){
            const SIMDVectorDouble input = VectorDoubleSet(runtimeInputs[row][0], runtimeInputs[row][1], runtimeInputs[row][2], runtimeInputs[row][3]);
            const SIMDVectorDouble results[]{ VectorDoubleRound(input), VectorDoubleTruncate(input), VectorDoubleFloor(input), VectorDoubleCeiling(input), };
            for(usize operation = 0u; operation < LengthOf(results); ++operation){
                for(usize lane = 0u; lane < 4u; ++lane)
                    EXPECT_EQ(BitCast<u64>(results[operation][lane]), BitCast<u64>(expected[operation][row][lane])) << mode << ':' << operation << ':' << row << ':' << lane;
            }
        }
    }
    const i32 restoreStatus = fesetenv(&originalEnvironment);
    EXPECT_EQ(restoreStatus, 0);
    if(restoreStatus == 0)
        restoreEnvironment.release();
}

TEST(Math, DoubleComparisonAndSelectionKeepAll64MaskBitsAndNaNOrdering){
    const SIMDVectorDouble lhs{ 0x1.0000000000001p+0, -0.0, Limit<f64>::s_QuietNaN, 0x1p+40 };
    const SIMDVectorDouble rhs{ 1.0, 0.0, Limit<f64>::s_QuietNaN, 0x1p+40 + 1.0 };
    const SIMDVectorDouble equal = VectorDoubleEqual(lhs, rhs);
    const SIMDVectorDouble greater = VectorDoubleGreater(lhs, rhs);
    const u64 equalBits[]{ 0u, Limit<u64>::s_Max, 0u, 0u };
    const u64 greaterBits[]{ Limit<u64>::s_Max, 0u, 0u, 0u };
    for(usize lane = 0u; lane < 4u; ++lane){
        EXPECT_EQ(BitCast<u64>(equal[lane]), equalBits[lane]) << lane;
        EXPECT_EQ(BitCast<u64>(greater[lane]), greaterBits[lane]) << lane;
    }

    const SIMDVectorDouble first = VectorDoubleSetInt(0x0123456789abcdefu, 0x8000000000000000u, 0x7ff8000000001234u, 1u);
    const SIMDVectorDouble second = VectorDoubleSetInt(0xfedcba9876543210u, 0u, 0x7ff800009abcdef0u, Limit<u64>::s_Max);
    const SIMDVectorDouble control = VectorDoubleSetInt(0xffffffff00000000u, 0x8000000000000000u, 0x00000000ffffffffu, 0u);
    const SIMDVectorDouble selected = VectorDoubleSelect(first, second, control);
    const u64 selectedBits[]{ 0xfedcba9889abcdefu, 0u, 0x7ff800009abcdef0u, 1u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(BitCast<u64>(selected[lane]), selectedBits[lane]) << lane;
}

TEST(Math, DoubleIntegerAdditionWrapsAcross64BitLaneBoundaries){
    const SIMDVectorDouble sum = VectorDoubleAddInt(VectorDoubleSetInt(Limit<u64>::s_Max, 0xffffffffu, 0x8000000000000000u, 0xfffffffffffffffeu), VectorDoubleSetInt(1u, 1u, 0x8000000000000000u, 3u));
    const u64 expected[]{ 0u, 0x100000000u, 0u, 1u };
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

TEST(Math, DoublePartialLaneDotAndCrossIgnoreNonfiniteInactiveLanesWithoutInvalidExceptions){
#pragma STDC FENV_ACCESS ON
    std::fenv_t originalEnvironment{};
    ASSERT_EQ(std::feholdexcept(&originalEnvironment), 0);
    const f64 lhsInactive[]{ Limit<f64>::s_Infinity, 0.0, Limit<f64>::s_QuietNaN, Limit<f64>::s_QuietNaN };
    const f64 rhsInactive[]{ 0.0, Limit<f64>::s_Infinity, 0.0, Limit<f64>::s_QuietNaN };
    for(usize index = 0u; index < LengthOf(lhsInactive); ++index){
        const SIMDVectorDouble lhs2{ 2.0, 3.0, lhsInactive[index], lhsInactive[index] };
        const SIMDVectorDouble rhs2{ 5.0, 7.0, rhsInactive[index], rhsInactive[index] };
        EXPECT_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
        const SIMDVectorDouble dot2 = VectorDouble2Dot(lhs2, rhs2);
        const int dot2Invalid = std::fetestexcept(FE_INVALID);
        EXPECT_EQ(dot2Invalid, 0) << index;
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(dot2[lane], 31.0) << index << ':' << lane;

        EXPECT_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
        const SIMDVectorDouble cross2 = VectorDouble2Cross(lhs2, rhs2);
        const int cross2Invalid = std::fetestexcept(FE_INVALID);
        EXPECT_EQ(cross2Invalid, 0) << index;
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(cross2[lane], -1.0) << index << ':' << lane;

        const SIMDVectorDouble lhs3{ 2.0, 3.0, 4.0, lhsInactive[index] };
        const SIMDVectorDouble rhs3{ 5.0, 7.0, 11.0, rhsInactive[index] };
        EXPECT_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
        const SIMDVectorDouble dot3 = VectorDouble3Dot(lhs3, rhs3);
        const int dot3Invalid = std::fetestexcept(FE_INVALID);
        EXPECT_EQ(dot3Invalid, 0) << index;
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(dot3[lane], 75.0) << index << ':' << lane;

        EXPECT_EQ(std::feclearexcept(FE_ALL_EXCEPT), 0);
        const SIMDVectorDouble cross3 = VectorDouble3Cross(lhs3, rhs3);
        const int cross3Invalid = std::fetestexcept(FE_INVALID);
        EXPECT_EQ(cross3Invalid, 0) << index;
        const SIMDVectorDouble cross3Expected{ 5.0, -2.0, -1.0, 0.0 };
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(cross3[lane], cross3Expected[lane]) << index << ':' << lane;
    }
    EXPECT_EQ(std::fesetenv(&originalEnvironment), 0);
}

TEST(Math, DoubleTranscendentalsHandleLargeArgumentsNonfiniteDomainsAndSignedZero){
    const SIMDVectorDouble angles{ 0x1p+40, -0x1p+100, 0x1p+900, -0.0 };
    SIMDVectorDouble sine{};
    SIMDVectorDouble cosine{};
    VectorDoubleSinCos(sine, cosine, angles);
    for(usize lane = 0u; lane < 4u; ++lane){
        EXPECT_NEAR(sine[lane], std::sin(angles[lane]), 2.0e-15) << lane;
        EXPECT_NEAR(cosine[lane], std::cos(angles[lane]), 2.0e-15) << lane;
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
    const u64 overflowBits[]{ 0u, 0u, 0x8000000000000000u, 0x8000000000000000u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(BitCast<u64>(overflow[lane]), overflowBits[lane]) << lane;

    const SIMDVectorDouble underflow = VectorDoubleMod(SIMDVectorDouble{ 0x1p-1074, -0x1p-1074, 0x1p-1074, -0x1p-1074 }, SIMDVectorDouble{ Limit<f64>::s_Max, Limit<f64>::s_Max, -Limit<f64>::s_Max, -Limit<f64>::s_Max });
    const SIMDVectorDouble underflowExpected{ 0x1p-1074, Limit<f64>::s_Max, -Limit<f64>::s_Max, -0x1p-1074 };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(underflow[lane], underflowExpected[lane]) << lane;

    const SIMDVectorDouble signs = VectorDoubleMod(SIMDVectorDouble{ -5.5, 5.5, -5.5, 5.5 }, SIMDVectorDouble{ 2.0, -2.0, -2.0, 2.0 });
    const SIMDVectorDouble signsExpected{ 0.5, -0.5, -1.5, 1.5 };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(signs[lane], signsExpected[lane]) << lane;
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
        EXPECT_EQ(BitCast<u64>(reduced[lane]), BitCast<u64>(std::remainder(huge[lane], s_Double2PI))) << lane;
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
    const SIMDVectorDouble invalid[]{
        { 0.0, 0.0, 0.0, 5.0 },
        { Limit<f64>::s_Infinity, 0.0, 0.0, 0.0 },
        { 0.0, Limit<f64>::s_QuietNaN, 0.0, 0.0 },
        { 0x1p-1074, 0.0, 0.0, 0.0 },
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
    EXPECT_NEAR(below.y, -std::sqrt(1.0 - belowIndex * belowIndex), 1.0e-20);
    EXPECT_LT(below.y, 0.0);
    const SIMDVectorDouble above = VectorDouble3Refract(incident, normal, 1.0 + 0x1p-40);
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(above[lane], 0.0) << lane;
}

TEST(Math, DoubleMatrixTransformsAndInversePreserveSubunitOffsetsAtLargeTranslations){
    constexpr f64 s_Base = 0x1p+40;
    const SIMDMatrixDouble matrix = MatrixDoubleMultiply(MatrixDoubleTranslation(s_Base + 0.25, -s_Base + 0.5, 3.0 + 0x1p-40), MatrixDoubleScaling(2.0, 4.0, 0.5));
    const SIMDVectorDouble point{ 0.25, -0.125, 0x1p-39, 1.0 };
    const SIMDVectorDouble transformed = VectorDouble3TransformCoord(point, matrix);
    EXPECT_EQ(transformed.x, s_Base + 0.75);
    EXPECT_EQ(transformed.y, -s_Base);
    EXPECT_EQ(transformed.z, 3.0 + 0x1p-39);
    EXPECT_EQ(transformed.w, 1.0);

    Double34 packed{};
    StoreDouble(matrix, packed);
    EXPECT_EQ(packed._14, s_Base + 0.25);
    EXPECT_EQ(packed._24, -s_Base + 0.5);
    EXPECT_EQ(packed._34, 3.0 + 0x1p-40);
    const SIMDVectorDouble fromPacked = VectorDouble3TransformCoord(point, LoadDouble(packed));
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(fromPacked[lane], transformed[lane]) << lane;

    SIMDVectorDouble determinant{};
    const SIMDMatrixDouble inverse = MatrixDoubleInverse(&determinant, matrix);
    EXPECT_EQ(determinant.x, 4.0);
    const SIMDVectorDouble restored = VectorDouble3TransformCoord(transformed, inverse);
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(restored[lane], point[lane]) << lane;
}

TEST(Math, DoubleDecompositionReconstructsNoncommutingRotationsWithNegativeScaleAndRejectsShear){
    const SIMDMatrixDouble rotation = MatrixDoubleMultiply(MatrixDoubleRotationX(0.5), MatrixDoubleMultiply(MatrixDoubleRotationY(-0.75), MatrixDoubleRotationZ(1.25)));
    const SIMDMatrixDouble matrix = MatrixDoubleMultiply(MatrixDoubleTranslation(0x1p+40 + 0.25, -7.0 + 0x1p-40, 11.0 + 0x1p-40), MatrixDoubleMultiply(rotation, MatrixDoubleScaling(-2.0, 3.0, 0.5)));
    const auto decomposition = MatrixDoubleDecompose(matrix);
    ASSERT_TRUE(decomposition);
    EXPECT_FALSE(QuaternionDoubleIsNaN(decomposition->rotation));
    const SIMDMatrixDouble reconstructed = MatrixDoubleAffineTransformation(decomposition->scale, VectorDoubleZero(), decomposition->rotation, decomposition->translation);
    for(usize row = 0u; row < 4u; ++row){
        for(usize lane = 0u; lane < 3u; ++lane)
            EXPECT_NEAR(reconstructed.v[row][lane], matrix.v[row][lane], 4.0e-15) << row << ':' << lane;
        EXPECT_EQ(reconstructed.v[row].w, matrix.v[row].w) << row;
    }
    SIMDMatrixDouble shear = MatrixDoubleIdentity();
    shear.v[0].y = 0.5;
    EXPECT_FALSE(MatrixDoubleDecompose(shear));
}

TEST(Math, DoubleAffineAdmissionRejectsSingularNonfiniteAndProjectiveMatrices){
    EXPECT_FALSE(MatrixDoubleIsInvertibleAffine(MatrixDoubleScaling(1.0, 1.0, 0x1p-81), 0.0, 0x1p-80));
    EXPECT_TRUE(MatrixDoubleIsInvertibleAffine(MatrixDoubleScaling(1.0, 1.0, 0x1p-79), 0.0, 0x1p-80));
    EXPECT_FALSE(MatrixDoubleIsInvertibleAffine(MatrixDoubleScaling(1.0, 1.0, 0.0), 0.0, 0.0));
    EXPECT_FALSE(MatrixDoubleIsAffine(MatrixDoubleTranslation(Limit<f64>::s_Infinity, 0.0, 0.0), 0.0));
    SIMDMatrixDouble projective = MatrixDoubleIdentity();
    projective.v[3] = SIMDVectorDouble{ 0.0, 0.0, 0x1p-40, 1.0 };
    EXPECT_FALSE(MatrixDoubleIsAffine(projective, 0x1p-41));
    EXPECT_TRUE(MatrixDoubleIsAffine(projective, 0x1p-39));
}

TEST(Math, DoubleQuaternionAntipodalInterpolationPreservesSmallDeviationFromRightAngle){
    const f64 angle = 0x1.921fb54442d18p+0 + 0x1p-40;
    const SIMDVectorDouble rotation = QuaternionDoubleRotationNormal(SIMDVectorDouble{ 0.0, 0.0, 1.0, 0.0 }, angle);
    const SIMDVectorDouble interpolated = QuaternionDoubleSlerp(rotation, VectorDoubleNegate(rotation), 0.5);
    EXPECT_FALSE(QuaternionDoubleIsNaN(interpolated));
    EXPECT_FALSE(QuaternionDoubleIsInfinite(interpolated));
    const SIMDVectorDouble rotated = VectorDouble3Rotate(SIMDVectorDouble{ 1.0, 0.0, 0.0, 0.0 }, interpolated);
    EXPECT_NEAR(rotated.x, std::cos(angle), 4.0e-16);
    EXPECT_NEAR(rotated.y, std::sin(angle), 4.0e-16);
    EXPECT_LT(rotated.x, -0x1p-41);
    const SIMDVectorDouble degenerateInverse = QuaternionDoubleInverse(VectorDoubleZero());
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(degenerateInverse[lane], 0.0) << lane;
}

TEST(Math, DoubleRigidQuaternionExtractionKeepsRowMatrixRotationAndDualTranslationDirection){
    // Transposing an M*v matrix during extraction reverses its nonzero rotation and corrupts the dual translation.
    const SIMDVectorDouble translation{ 7.0 + 0x1p-40, -11.0 + 0x1p-40, 5.0 + 0x1p-40, 0.0 };
    const SIMDMatrixDouble matrix = MatrixDoubleMultiply(MatrixDoubleTranslationFromVector(translation), MatrixDoubleRotationZ(0.75));
    const auto quaternion = MatrixDoubleTryBuildRigidRotationQuaternion(matrix, 0.0, 1.0e-12);
    ASSERT_TRUE(quaternion);
    const SIMDVectorDouble direction{ 2.0, -3.0, 4.0, 0.0 };
    const SIMDVectorDouble fromMatrix = VectorDouble3TransformNormal(direction, matrix);
    const SIMDVectorDouble fromQuaternion = VectorDouble3Rotate(direction, *quaternion);
    for(usize lane = 0u; lane < 3u; ++lane)
        EXPECT_NEAR(fromQuaternion[lane], fromMatrix[lane], 2.0e-15) << lane;

    const auto dualQuaternion = MatrixDoubleTryBuildRigidDualQuaternion(matrix, 0.0, 1.0e-12);
    ASSERT_TRUE(dualQuaternion);
    const SIMDVectorDouble extractedTranslation = VectorDoubleScale(QuaternionDoubleMultiply(dualQuaternion->dual, QuaternionDoubleConjugate(dualQuaternion->real)), 2.0);
    for(usize lane = 0u; lane < 3u; ++lane)
        EXPECT_NEAR(extractedTranslation[lane], translation[lane], 4.0e-15) << lane;

    const SIMDVectorDouble transformed = VectorDoubleAdd(VectorDouble3Rotate(direction, dualQuaternion->real), extractedTranslation);
    const SIMDVectorDouble expected = VectorDouble3Transform(direction, matrix);
    for(usize lane = 0u; lane < 3u; ++lane)
        EXPECT_NEAR(transformed[lane], expected[lane], 4.0e-15) << lane;
}

TEST(Math, DoubleSphereContainmentDistinguishesTangencyFromSubunitSeparationAtLargeCoordinates){
    constexpr f64 s_Base = 0x1p+40;
    const BoundingDoubleSphere sphere(Double3U(s_Base, 0.0, 0.0), 1.0);
    EXPECT_EQ(sphere.contains(SIMDVectorDouble{ s_Base + 1.0, 0.0, 0.0, 0.0 }), ContainmentType::Contains);
    EXPECT_EQ(sphere.contains(SIMDVectorDouble{ s_Base + 1.0 + 0x1p-10, 0.0, 0.0, 0.0 }), ContainmentType::Disjoint);
    const BoundingDoubleSphere point(Double3U(s_Base, 0.0, 0.0), 0.0);
    EXPECT_EQ(point.contains(SIMDVectorDouble{ s_Base + 0x1p-10, 0.0, 0.0, 0.0 }), ContainmentType::Disjoint);
}

TEST(Math, CenteredOrthographicProjectionPreservesNegativeZeroTranslation){
    const SIMDMatrix projection = MatrixOrthographicOffCenterLH(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 11.0f);
    EXPECT_TRUE(SignBit(VectorGetW(projection.v[0])));
    EXPECT_TRUE(SignBit(VectorGetW(projection.v[1])));
}

TEST(Math, FrustumSphereTangencyAndZeroRadiusKeepContainmentBoundary){
    BoundingFrustum frustum;
    frustum.nearPlane = 1.0f;
    frustum.farPlane = 10.0f;
    frustum.rightSlope = 1.0f;
    frustum.leftSlope = -1.0f;
    frustum.topSlope = 1.0f;
    frustum.bottomSlope = -1.0f;
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, planes);
    const Array<f32, 6u> centers = { 0.5f, 0.25f, 1.5f, 9.5f, 10.5f, 1.0f };
    const Array<ContainmentType::Enum, 6u> expected = { ContainmentType::Intersects, ContainmentType::Disjoint, ContainmentType::Contains, ContainmentType::Contains, ContainmentType::Intersects, ContainmentType::Contains, };
    for(usize index = 0u; index < centers.size(); ++index){
        const BoundingSphere sphere(Float3U(0.0f, 0.0f, centers[index]), index == centers.size() - 1u ? 0.0f : 0.5f);
        EXPECT_EQ(frustum.contains(sphere), expected[index]) << index;
        EXPECT_EQ(sphere.containedBy(planes[0], planes[1], planes[2], planes[3], planes[4], planes[5]), expected[index]) << index;
    }
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

TEST(Math, TransformedBoundsRejectInvalidAndOverflowingInputs){
    const SIMDVectorSingle minimum = VectorSet(-1.0f, -2.0f, -3.0f, 0.0f);
    const SIMDVectorSingle maximum = VectorSet(1.0f, 2.0f, 3.0f, 0.0f);
    EXPECT_FALSE(AabbTests::Transform(MatrixIdentity(), maximum, minimum));
    SIMDMatrix invalid = MatrixIdentity();
    invalid.v[0] = VectorSet(Limit<f32>::s_Infinity, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(AabbTests::Transform(invalid, minimum, maximum));
    const SIMDMatrix overflow = MatrixScaling(Limit<f32>::s_Max, Limit<f32>::s_Max, Limit<f32>::s_Max);
    EXPECT_FALSE(AabbTests::Transform(overflow, minimum, maximum));
    const auto point = AabbTests::Transform(MatrixIdentity(), VectorZero(), VectorZero());
    ASSERT_TRUE(point);
    EXPECT_TRUE(AabbTests::Valid(point->minBounds, point->maxBounds));
    EXPECT_TRUE(NearlyEqual3(point->minBounds, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual3(point->maxBounds, 0.0f, 0.0f, 0.0f));
}

TEST(Math, BoxNormalHandlesCornerAndEqualDistanceTie){
    const SIMDVectorSingle boxHalfExtents = VectorSet(1.0f, 1.0f, 1.0f, 0.0f);


    EXPECT_TRUE(NearlyEqual3(SdfTests::BoxNormal(VectorSet(2.0f, 2.0f, 1.0f, 0.0f), boxHalfExtents, VectorSet(0.0f, 1.0f, 0.0f, 0.0f), 0.000001f), 0.70710677f, 0.70710677f, 0.0f, 0.00001f));
    EXPECT_TRUE(NearlyEqual3(SdfTests::BoxNormal(VectorSet(0.9f, 0.9f, 0.2f, 0.0f), boxHalfExtents, VectorSet(0.0f, 1.0f, 0.0f, 0.0f), 0.000001f), 1.0f, 0.0f, 0.0f));
}

TEST(Math, DegeneratePlaneNormalUsesFallback){
    EXPECT_TRUE(NearlyEqual4(PlaneTests::FromPointNormal(VectorZero(), VectorSet(1.0f, 2.0f, 3.0f, 0.0f), VectorSet(0.0f, 0.0f, 1.0f, 0.0f)), 0.0f, 0.0f, 1.0f, -3.0f));
}

TEST(Math, MergingContainedSpherePreservesLargerBounds){
    BoundingSphere contained;
    const BoundingSphere larger(Float3U(0.0f, 0.0f, 0.0f), 3.0f);
    BoundingSphere::CreateMerged(contained, larger, BoundingSphere(Float3U(1.0f, 0.0f, 0.0f), 1.0f));
    EXPECT_TRUE(NearlyEqual4(LoadFloat(contained.centerRadius), 0.0f, 0.0f, 0.0f, 3.0f));
}

TEST(Math, MatrixDecompositionReconstructsCollapsedAxesWithNegativeBasisTies){
    const SIMDMatrix matrices[]{
        MatrixSet(-1.0f, 0.0f, 0.0f, 3.0f, -1.0f, 0.0f, 0.0f, -2.0f, -1.0f, 0.0f, 0.0f, 7.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        MatrixSet(0.0f, -1.0f, 0.0f, 3.0f, 0.0f, -1.0f, 0.0f, -2.0f, 0.0f, -1.0f, 0.0f, 7.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        MatrixSet(0.0f, 0.0f, -1.0f, 3.0f, 0.0f, 0.0f, -1.0f, -2.0f, 0.0f, 0.0f, -1.0f, 7.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        MatrixSet(0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, -2.0f, 0.0f, 0.0f, 0.0f, 7.0f, 0.0f, 0.0f, 0.0f, 1.0f),
    };
    for(usize index = 0u; index < LengthOf(matrices); ++index){
        SCOPED_TRACE(index);
        const auto decomposition = MatrixDecompose(matrices[index]);
        ASSERT_TRUE(decomposition);
        EXPECT_FALSE(Vector4IsNaN(decomposition->rotation));
        EXPECT_FALSE(Vector4IsInfinite(decomposition->rotation));
        const SIMDMatrix reconstructed = MatrixAffineTransformation(decomposition->scale, VectorZero(), decomposition->rotation, decomposition->translation);
        for(usize row = 0u; row < 4u; ++row){
            EXPECT_TRUE(NearlyEqual4(reconstructed.v[row], VectorGetX(matrices[index].v[row]), VectorGetY(matrices[index].v[row]), VectorGetZ(matrices[index].v[row]), VectorGetW(matrices[index].v[row]))) << row;
        }
    }

    const SIMDMatrix shear = MatrixSet(1.0f, 1.0f, 0.0f, 3.0f, 0.0f, 1.0f, 0.0f, -2.0f, 0.0f, 0.0f, 1.0f, 7.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_FALSE(MatrixDecompose(shear));
}

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
    f32 source[6] = { 0.0f, -1.5f, 1.0f, 65504.0f, 5.9604644775390625e-8f, Limit<f32>::s_Infinity };
    Half packed[6] = {};
    f32 unpacked[6] = {};

    ASSERT_EQ(ConvertFloatBufferToHalf(packed, source, 6u), packed);
    EXPECT_EQ(packed[3], static_cast<Half>(0x7bffu));
    EXPECT_EQ(packed[4], static_cast<Half>(0x0001u));
    EXPECT_EQ(packed[5], static_cast<Half>(0x7c00u));

    ASSERT_EQ(ConvertHalfBufferToFloat(unpacked, packed, 6u), unpacked);
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

TEST(Math, AffineQueriesRejectNonrigidSingularOverflowAndProjectiveMatrices){
    constexpr f32 s_AffineEpsilon = 0.000001f;
    constexpr f32 s_DeterminantEpsilon = 0.000000000001f;
    constexpr f32 s_RigidEpsilon = 0.001f;

    const SIMDMatrix scaled = MatrixScaling(2.0f, 3.0f, 4.0f);
    EXPECT_FALSE(MatrixIsRigidAffine(scaled, s_AffineEpsilon, s_RigidEpsilon));

    const SIMDMatrix nearSingular = MatrixScaling(1.0f, 1.0f, s_DeterminantEpsilon * 0.5f);
    EXPECT_FALSE(MatrixIsInvertibleAffine(nearSingular, s_AffineEpsilon, s_DeterminantEpsilon));

    const SIMDMatrix overflowed = MatrixScaling(s_MaxF32, s_MaxF32, s_MaxF32);
    EXPECT_TRUE(MatrixIsAffine(overflowed, s_AffineEpsilon));
    EXPECT_FALSE(MatrixIsInvertibleAffine(overflowed, s_AffineEpsilon, s_DeterminantEpsilon));

    SIMDMatrix nonAffine = MatrixIdentity();
    nonAffine.v[3] = VectorZero();
    EXPECT_FALSE(MatrixIsAffine(nonAffine, s_AffineEpsilon));
}

template<typename Value>
static void CheckStorageHashMatchesEquality(const Value& lhs, const Value& same, const Value& different){
    EXPECT_EQ(lhs, same);
    EXPECT_NE(lhs, different);
    EXPECT_EQ(Hasher<Value>{}(lhs), Hasher<Value>{}(same));
}

TEST(Math, SignedZeroStorageEqualityProducesEqualHashes){
    EXPECT_EQ(FloatingHashBits(-0.0f), FloatingHashBits(0.0f));
    CheckStorageHashMatchesEquality(Float4(-0.0f, 1.0f, 2.0f, 3.0f), Float4(0.0f, 1.0f, 2.0f, 3.0f), Float4(0.0f, 1.0f, 2.0f, 4.0f));
    CheckStorageHashMatchesEquality(Float3Int(-0.0f, 1.0f, 2.0f, -3), Float3Int(0.0f, 1.0f, 2.0f, -3), Float3Int(0.0f, 1.0f, 2.0f, -4));
    CheckStorageHashMatchesEquality(Float3UInt(-0.0f, 1.0f, 2.0f, 3u), Float3UInt(0.0f, 1.0f, 2.0f, 3u), Float3UInt(0.0f, 1.0f, 2.0f, 4u));
    CheckStorageHashMatchesEquality(Float2U(-0.0f, 1.0f), Float2U(0.0f, 1.0f), Float2U(0.0f, 2.0f));
    CheckStorageHashMatchesEquality(Float3U(-0.0f, 1.0f, 2.0f), Float3U(0.0f, 1.0f, 2.0f), Float3U(0.0f, 1.0f, 3.0f));
    CheckStorageHashMatchesEquality(Float4U(-0.0f, 1.0f, 2.0f, 3.0f), Float4U(0.0f, 1.0f, 2.0f, 3.0f), Float4U(0.0f, 1.0f, 2.0f, 4.0f));
}

template<typename Storage, usize LogicalCount, typename Source>
static void CheckDoubleStoresIgnoreInactiveLanesAndPreserveObjectPadding(const Source& source, const f64* expected){
    struct StorageRecord{
        Storage value;
        u64 neighbor[4];
    };
    StorageRecord record{};
    NWB_MEMSET(&record, 0xa5, sizeof(record));
    const u64 poison[]{ 0x7ff0000000000000u, 0x7ff8000000001234u, 0xfff0000000000000u };
    const usize logicalBytes = LogicalCount * sizeof(f64);
    NWB_MEMCPY(reinterpret_cast<u8*>(&record.value) + logicalBytes, sizeof(Storage) - logicalBytes, poison, sizeof(Storage) - logicalBytes);
    Array<u8, sizeof(StorageRecord)> original{};
    NWB_MEMCPY(original.data(), original.size(), &record, sizeof(record));
    for(const bool stream : { false, true }){
        NWB_MEMCPY(&record, sizeof(record), original.data(), original.size());
        if(stream){
            StreamDouble(source, record.value);
            StreamDoubleFence();
        }
        else
            StoreDouble(source, record.value);
        const u8* bytes = reinterpret_cast<const u8*>(&record);
        for(usize index = logicalBytes; index < sizeof(record); ++index)
            EXPECT_EQ(bytes[index], original[index]) << stream << ':' << index;
        for(usize lane = 0u; lane < LogicalCount; ++lane)
            EXPECT_EQ(BitCast<u64>(record.value.raw[lane]), BitCast<u64>(expected[lane])) << stream << ':' << lane;

        const auto loaded = LoadDouble(record.value);
        if constexpr(LogicalCount < 4u){
            for(usize lane = LogicalCount; lane < 4u; ++lane)
                EXPECT_EQ(BitCast<u64>(loaded[lane]), 0u) << stream << ':' << lane;
            const SIMDVectorDouble transformed = VectorDouble4Transform(loaded, MatrixDoubleIdentity());
            for(usize lane = 0u; lane < LogicalCount; ++lane)
                EXPECT_EQ(transformed[lane], expected[lane]) << stream << ':' << lane;
        }
        else{
            for(usize row = 0u; row < 3u; ++row){
                EXPECT_EQ(BitCast<u64>(loaded.v[row].w), 0u) << stream << ':' << row;
                for(usize lane = 0u; lane < 3u; ++lane)
                    EXPECT_EQ(BitCast<u64>(loaded.v[row][lane]), BitCast<u64>(expected[row * 3u + lane])) << stream << ':' << row << ':' << lane;
            }
            EXPECT_TRUE(VectorDouble4Equal(loaded.v[3], SIMDVectorDouble{ 0.0, 0.0, 0.0, 1.0 }));
        }
    }
}

template<typename Storage, usize LogicalCount>
static void CheckDoubleStorageHashIgnoresPaddingAndCanonicalizesSignedZero(){
    Storage lhs{};
    Storage rhs{};
    for(usize lane = 0u; lane < LogicalCount; ++lane){
        lhs.raw[lane] = lane == 0u ? -0.0 : 1.0 + 0x1p-52;
        rhs.raw[lane] = lane == 0u ? 0.0 : 1.0 + 0x1p-52;
    }
    const usize logicalBytes = LogicalCount * sizeof(f64);
    NWB_MEMSET(reinterpret_cast<u8*>(&lhs) + logicalBytes, 0x7f, sizeof(Storage) - logicalBytes);
    NWB_MEMSET(reinterpret_cast<u8*>(&rhs) + logicalBytes, 0xff, sizeof(Storage) - logicalBytes);
    EXPECT_EQ(lhs, rhs);
    EXPECT_EQ(Hasher<Storage>{}(lhs), Hasher<Storage>{}(rhs));
    rhs.raw[LogicalCount - 1u] = 1.0;
    EXPECT_NE(lhs, rhs);
}

TEST(Math, DoubleStorageHashesCanonicalizeSignedZeroAndRetainSubFloatResiduals){
    EXPECT_EQ(FloatingHashBits(-0.0), FloatingHashBits(0.0));
    EXPECT_NE(FloatingHashBits(1.0 + 0x1p-52), FloatingHashBits(1.0));
    CheckStorageHashMatchesEquality(Double4(-0.0, 1.0 + 0x1p-52, -0.0, 0x1p+40 + 1.0), Double4(0.0, 1.0 + 0x1p-52, 0.0, 0x1p+40 + 1.0), Double4(0.0, 1.0, 0.0, 0x1p+40 + 1.0));
    CheckStorageHashMatchesEquality(Double4U(-0.0, 1.0 + 0x1p-52, -0.0, 0x1p+40 + 1.0), Double4U(0.0, 1.0 + 0x1p-52, 0.0, 0x1p+40 + 1.0), Double4U(0.0, 1.0, 0.0, 0x1p+40 + 1.0));
}

TEST(Math, AlignedShortDoubleStoresAndStreamsPreservePoisonedPaddingAndNeighborStorage){
    const f64 pairExpected[]{ -0.0, 1.0 + 0x1p-52 };
    CheckDoubleStoresIgnoreInactiveLanesAndPreserveObjectPadding<Double2, 2u>(SIMDVectorDouble{ pairExpected[0], pairExpected[1], Limit<f64>::s_Infinity, Limit<f64>::s_QuietNaN }, pairExpected);
    const f64 tripleExpected[]{ -0.0, 1.0 + 0x1p-52, 0x1p+40 + 1.0 };
    CheckDoubleStoresIgnoreInactiveLanesAndPreserveObjectPadding<Double3, 3u>(SIMDVectorDouble{ tripleExpected[0], tripleExpected[1], tripleExpected[2], Limit<f64>::s_QuietNaN }, tripleExpected);
    const f64 matrixExpected[]{ -0.0, 1.0 + 0x1p-52, 0x1p+40 + 1.0, 2.0, -0.0, -0x1p+40 - 1.0, 0x1p-40, -3.0, 1.0 + 0x1p-52 };
    const SIMDMatrixDouble matrix = {
        SIMDVectorDouble{ matrixExpected[0], matrixExpected[1], matrixExpected[2], Limit<f64>::s_Infinity },
        SIMDVectorDouble{ matrixExpected[3], matrixExpected[4], matrixExpected[5], Limit<f64>::s_QuietNaN },
        SIMDVectorDouble{ matrixExpected[6], matrixExpected[7], matrixExpected[8], -Limit<f64>::s_Infinity },
        SIMDVectorDouble{ Limit<f64>::s_QuietNaN, Limit<f64>::s_Infinity, -Limit<f64>::s_Infinity, Limit<f64>::s_QuietNaN },
    };
    CheckDoubleStoresIgnoreInactiveLanesAndPreserveObjectPadding<Double33, 9u>(matrix, matrixExpected);
}

TEST(Math, AlignedShortDoubleAndCompactMatrixHashesIgnorePaddingWithSignedZeroAndBinary64Residuals){
    CheckDoubleStorageHashIgnoresPaddingAndCanonicalizesSignedZero<Double2, 2u>();
    CheckDoubleStorageHashIgnoresPaddingAndCanonicalizesSignedZero<Double3, 3u>();
    CheckDoubleStorageHashIgnoresPaddingAndCanonicalizesSignedZero<Double33, 9u>();
}

TEST(Math, DoubleTransformStreamsPreservePaddedStridesDuringAlignedUnalignedAndInPlaceConversion){
    struct AlignedRecord{
        Double3 value;
        u64 gap[4];
    };
    struct alignas(32) UnalignedRecord{
        u64 prefix;
        Double3U value;
        u64 gap[4];
    };
    AlignedRecord aligned[3]{};
    UnalignedRecord unaligned[3]{};
    NWB_MEMSET(aligned, 0xa5, sizeof(aligned));
    NWB_MEMSET(unaligned, 0x5a, sizeof(unaligned));
    const u64 poison = 0x7ff8000000001234u;
    for(usize index = 0u; index < LengthOf(aligned); ++index){
        StoreDouble(SIMDVectorDouble{ 0x1p+40 + 0.25 + 0.25 * static_cast<f64>(index), (static_cast<f64>(index) - 1.0) * 0x1p-40, 1.0 + 0x1p-52, Limit<f64>::s_Infinity }, aligned[index].value);
        NWB_MEMCPY(reinterpret_cast<u8*>(&aligned[index].value) + 3u * sizeof(f64), sizeof(aligned[index].value) - 3u * sizeof(f64), &poison, sizeof(poison));
    }
    Array<u8, sizeof(aligned)> alignedOriginal{};
    Array<u8, sizeof(unaligned)> unalignedOriginal{};
    NWB_MEMCPY(alignedOriginal.data(), alignedOriginal.size(), aligned, sizeof(aligned));
    NWB_MEMCPY(unalignedOriginal.data(), unalignedOriginal.size(), unaligned, sizeof(unaligned));
    const SIMDMatrixDouble matrix = MatrixDoubleTranslation(0.25, 0.0, 0.0);
    EXPECT_EQ(VectorDouble3TransformCoordStream(&unaligned[0].value, sizeof(UnalignedRecord), &aligned[0].value, sizeof(AlignedRecord), LengthOf(aligned), matrix), &unaligned[0].value);
    EXPECT_EQ(NWB_MEMCMP(aligned, alignedOriginal.data(), sizeof(aligned)), 0);
    for(usize index = 0u; index < LengthOf(aligned); ++index)
        EXPECT_EQ(unaligned[index].value.x, 0x1p+40 + 0.5 + 0.25 * static_cast<f64>(index)) << index;

    EXPECT_EQ(VectorDouble3TransformCoordStream(&unaligned[0].value, sizeof(UnalignedRecord), &unaligned[0].value, sizeof(UnalignedRecord), LengthOf(unaligned), matrix), &unaligned[0].value);
    EXPECT_EQ(VectorDouble3TransformCoordStream(&aligned[0].value, sizeof(AlignedRecord), &unaligned[0].value, sizeof(UnalignedRecord), LengthOf(aligned), matrix), &aligned[0].value);
    EXPECT_EQ(VectorDouble3TransformCoordStream(&aligned[0].value, sizeof(AlignedRecord), &aligned[0].value, sizeof(AlignedRecord), LengthOf(aligned), matrix), &aligned[0].value);
    for(usize index = 0u; index < LengthOf(aligned); ++index){
        EXPECT_EQ(aligned[index].value.x, 0x1p+40 + 1.25 + 0.25 * static_cast<f64>(index)) << index;
        EXPECT_EQ(unaligned[index].value.x, 0x1p+40 + 0.75 + 0.25 * static_cast<f64>(index)) << index;
        EXPECT_EQ(aligned[index].value.y, (static_cast<f64>(index) - 1.0) * 0x1p-40) << index;
        EXPECT_EQ(aligned[index].value.z, 1.0 + 0x1p-52) << index;
        const u8* alignedBytes = reinterpret_cast<const u8*>(&aligned[index]);
        const u8* unalignedBytes = reinterpret_cast<const u8*>(&unaligned[index]);
        for(usize offset = 3u * sizeof(f64); offset < sizeof(AlignedRecord); ++offset)
            EXPECT_EQ(alignedBytes[offset], alignedOriginal[index * sizeof(AlignedRecord) + offset]) << index << ':' << offset;
        for(usize offset = 0u; offset < sizeof(UnalignedRecord); ++offset){
            if(offset >= offsetof(UnalignedRecord, value) && offset < offsetof(UnalignedRecord, value) + sizeof(Double3U))
                continue;
            EXPECT_EQ(unalignedBytes[offset], unalignedOriginal[index * sizeof(UnalignedRecord) + offset]) << index << ':' << offset;
        }
    }

    BoundingDoubleBox bounds;
    BoundingDoubleBox::CreateFromPoints(bounds, LengthOf(aligned), &aligned[0].value, sizeof(AlignedRecord));
    EXPECT_EQ(bounds.center.x, 0x1p+40 + 1.5);
    EXPECT_EQ(bounds.center.y, 0.0);
    EXPECT_EQ(bounds.center.z, 1.0 + 0x1p-52);
    EXPECT_EQ(bounds.extents.x, 0.25);
    EXPECT_EQ(bounds.extents.y, 0x1p-40);
    EXPECT_EQ(bounds.extents.z, 0.0);
    EXPECT_EQ(bounds.contains(SIMDVectorDouble{ 0x1p+40 + 1.75 + 0x1p-10, 0.0, 1.0 + 0x1p-52, 0.0 }), ContainmentType::Disjoint);

    struct PairRecord{
        Double2 value;
        u64 gap[4];
    };
    PairRecord pairs[2]{};
    NWB_MEMSET(pairs, 0xa5, sizeof(pairs));
    for(usize index = 0u; index < LengthOf(pairs); ++index){
        StoreDouble(SIMDVectorDouble{ 0x1p+40 + 0.25, 1.0 + 0x1p-52, Limit<f64>::s_Infinity, Limit<f64>::s_QuietNaN }, pairs[index].value);
        NWB_MEMCPY(reinterpret_cast<u8*>(&pairs[index].value) + 2u * sizeof(f64), sizeof(pairs[index].value) - 2u * sizeof(f64), &poison, sizeof(poison));
    }
    Array<u8, sizeof(pairs)> pairOriginal{};
    NWB_MEMCPY(pairOriginal.data(), pairOriginal.size(), pairs, sizeof(pairs));
    EXPECT_EQ(VectorDouble2TransformCoordStream(&pairs[0].value, sizeof(PairRecord), &pairs[0].value, sizeof(PairRecord), LengthOf(pairs), matrix), &pairs[0].value);
    for(usize index = 0u; index < LengthOf(pairs); ++index){
        EXPECT_EQ(pairs[index].value.x, 0x1p+40 + 0.5) << index;
        EXPECT_EQ(pairs[index].value.y, 1.0 + 0x1p-52) << index;
        const u8* bytes = reinterpret_cast<const u8*>(&pairs[index]);
        for(usize offset = 2u * sizeof(f64); offset < sizeof(PairRecord); ++offset)
            EXPECT_EQ(bytes[offset], pairOriginal[index * sizeof(PairRecord) + offset]) << index << ':' << offset;
    }
}

TEST(Math, DoubleMinMaxRetainRightOperandNaNPayloadAndZeroSign){
    const u64 firstBits[]{ 0xfff8000012345678u, 0x8000000000000000u, 0u, 0x3ff8000000000000u };
    const u64 secondBits[]{ 0x7ff800009abcdef0u, 0u, 0x8000000000000000u, 0x7ff8000013579bdfu };
    const SIMDVectorDouble first = VectorDoubleSetInt(firstBits[0], firstBits[1], firstBits[2], firstBits[3]);
    const SIMDVectorDouble second = VectorDoubleSetInt(secondBits[0], secondBits[1], secondBits[2], secondBits[3]);
    const SIMDVectorDouble forward[]{ VectorDoubleMin(first, second), VectorDoubleMax(first, second) };
    const SIMDVectorDouble reverse[]{ VectorDoubleMin(second, first), VectorDoubleMax(second, first) };
    for(usize operation = 0u; operation < LengthOf(forward); ++operation){
        for(usize lane = 0u; lane < 4u; ++lane){
            EXPECT_EQ(VectorDoubleGetIntByIndex(forward[operation], lane), secondBits[lane]) << operation << ':' << lane;
            EXPECT_EQ(VectorDoubleGetIntByIndex(reverse[operation], lane), firstBits[lane]) << operation << ':' << lane;
        }
    }
}

TEST(Math, DoubleRawIntegerEqualityRequiresBoth32BitHalvesOfEveryLane){
    const u64 firstBits[]{ 0x0123456789abcdefu, 0x8000000000000000u, 0x7ff800009abcdef0u, 0xfedcba9876543210u };
    const u64 differences[]{ 0x0000000000000001u, 0x0000000100000000u };
    const SIMDVectorDouble first = VectorDoubleSetInt(firstBits[0], firstBits[1], firstBits[2], firstBits[3]);
    for(usize changedLane = 0u; changedLane < 4u; ++changedLane){
        for(const u64 difference : differences){
            const SIMDVectorDouble second = VectorDoubleSetIntByIndex(first, firstBits[changedLane] ^ difference, changedLane);
            const SIMDVectorDouble equality[]{ VectorDoubleEqualInt(first, second), VectorDoubleEqualInt(second, first) };
            for(const SIMDVectorDouble mask : equality){
                for(usize lane = 0u; lane < 4u; ++lane)
                    EXPECT_EQ(VectorDoubleGetIntByIndex(mask, lane), lane == changedLane ? 0u : Limit<u64>::s_Max) << changedLane << ':' << difference << ':' << lane;
            }
        }
    }
}

TEST(Math, DoubleSingleConversionsRetainSubnormalsAndSignsAndRoundHalfUlpTiesEven){
    volatile f64 runtimeInputs[][4]{
        { Limit<f64>::s_Max, -Limit<f64>::s_Max, -0.0, 0.0 },
        { 1.0 + 0x1p-24, 1.0 + 0x1.8p-23, -1.0 - 0x1p-24, -1.0 - 0x1.8p-23 },
        { 0x1p-150, 0x1.8p-149, -0x1p-150, -0x1.8p-149 },
    };
    const SIMDVectorSingle overflow = ConvertVectorDoubleToSingle(VectorDoubleSet(runtimeInputs[0][0], runtimeInputs[0][1], runtimeInputs[0][2], runtimeInputs[0][3]));
    const u32 overflowExpected[]{ 0x7f800000u, 0xff800000u, 0x80000000u, 0u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorGetIntByIndex(overflow, lane), overflowExpected[lane]) << lane;

    const u32 subnormalBits[]{ 1u, 0x80000001u, 0x007fffffu, 0x807fffffu };
    volatile u32 runtimeSubnormalBits[]{ 1u, 0x80000001u, 0x007fffffu, 0x807fffffu };
    const SIMDVectorSingle subnormals = VectorSetInt(runtimeSubnormalBits[0], runtimeSubnormalBits[1], runtimeSubnormalBits[2], runtimeSubnormalBits[3]);
    const SIMDVectorDouble widened = ConvertVectorSingleToDouble(subnormals);
    const f64 widenedExpected[]{ 0x1p-149, -0x1p-149, 0x1.fffffcp-127, -0x1.fffffcp-127 };
    const SIMDVectorSingle narrowed = ConvertVectorDoubleToSingle(widened);
    for(usize lane = 0u; lane < 4u; ++lane){
        EXPECT_EQ(VectorDoubleGetIntByIndex(widened, lane), BitCast<u64>(widenedExpected[lane])) << lane;
        EXPECT_EQ(VectorGetIntByIndex(narrowed, lane), subnormalBits[lane]) << lane;
    }

    volatile u32 runtimeZeroBits[]{ 0x80000000u, 0u, 0x80000000u, 0u };
    const SIMDVectorDouble signedZeros = ConvertVectorSingleToDouble(VectorSetInt(runtimeZeroBits[0], runtimeZeroBits[1], runtimeZeroBits[2], runtimeZeroBits[3]));
    const u64 zeroExpected[]{ 0x8000000000000000u, 0u, 0x8000000000000000u, 0u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorDoubleGetIntByIndex(signedZeros, lane), zeroExpected[lane]) << lane;

    const SIMDVectorSingle rounded = ConvertVectorDoubleToSingle(VectorDoubleSet(runtimeInputs[1][0], runtimeInputs[1][1], runtimeInputs[1][2], runtimeInputs[1][3]));
    const u32 roundedExpected[]{ 0x3f800000u, 0x3f800002u, 0xbf800000u, 0xbf800002u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorGetIntByIndex(rounded, lane), roundedExpected[lane]) << lane;

    const SIMDVectorSingle underflow = ConvertVectorDoubleToSingle(VectorDoubleSet(runtimeInputs[2][0], runtimeInputs[2][1], runtimeInputs[2][2], runtimeInputs[2][3]));
    const u32 underflowExpected[]{ 0u, 2u, 0x80000000u, 0x80000002u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorGetIntByIndex(underflow, lane), underflowExpected[lane]) << lane;
}

TEST(Math, DoubleRuntimePermutationsRetainNonfiniteAndRawBitsAcrossPairs){
    const SIMDVectorDouble first = VectorDoubleSetInt(0xfff8000012345678u, 0x8000000000000000u, 0x0123456789abcdefu, 0x7ff0000000000001u);
    const SIMDVectorDouble second = VectorDoubleSetInt(0u, 0x7ff800009abcdef0u, 0xfedcba9876543210u, Limit<u64>::s_Max);
    volatile u32 swizzleControls[][4]{ { 3u, 1u, 0u, 2u }, { 2u, 0u, 3u, 1u }, { 1u, 3u, 1u, 3u } };
    const u64 swizzleExpected[][4]{
        { 0x7ff0000000000001u, 0x8000000000000000u, 0xfff8000012345678u, 0x0123456789abcdefu },
        { 0x0123456789abcdefu, 0xfff8000012345678u, 0x7ff0000000000001u, 0x8000000000000000u },
        { 0x8000000000000000u, 0x7ff0000000000001u, 0x8000000000000000u, 0x7ff0000000000001u }
    };
    for(usize index = 0u; index < LengthOf(swizzleExpected); ++index){
        const SIMDVectorDouble result = VectorDoubleSwizzle(first, swizzleControls[index][0], swizzleControls[index][1], swizzleControls[index][2], swizzleControls[index][3]);
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(VectorDoubleGetIntByIndex(result, lane), swizzleExpected[index][lane]) << index << ':' << lane;
    }

    volatile u32 permuteControls[][4]{ { 7u, 0u, 6u, 1u }, { 3u, 4u, 2u, 5u }, { 5u, 3u, 4u, 0u } };
    const u64 permuteExpected[][4]{
        { Limit<u64>::s_Max, 0xfff8000012345678u, 0xfedcba9876543210u, 0x8000000000000000u },
        { 0x7ff0000000000001u, 0u, 0x0123456789abcdefu, 0x7ff800009abcdef0u },
        { 0x7ff800009abcdef0u, 0x7ff0000000000001u, 0u, 0xfff8000012345678u }
    };
    for(usize index = 0u; index < LengthOf(permuteExpected); ++index){
        const SIMDVectorDouble result = VectorDoublePermute(first, second, permuteControls[index][0], permuteControls[index][1], permuteControls[index][2], permuteControls[index][3]);
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(VectorDoubleGetIntByIndex(result, lane), permuteExpected[index][lane]) << index << ':' << lane;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

