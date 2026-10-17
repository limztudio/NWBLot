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


namespace __hidden_vector_rounding_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::NearlyEqual;
using NWB::Tests::NearlyEqual3;
using NWB::Tests::NearlyEqual4;


NWB_NOINLINE f32 LegacyMultiplyAddExpression(const f32 first, const f32 second, const f32 addend)noexcept{
    return first * second + addend;
}

NWB_NOINLINE f64 LegacyDoubleMultiplyAddExpression(const f64 first, const f64 second, const f64 addend)noexcept{
    return first * second + addend;
}

NWB_NOINLINE f32 LegacyNegativeMultiplySubtractExpression(const f32 first, const f32 second, const f32 addend)noexcept{
    return addend - first * second;
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
    // Runtime inputs give vector and scalar paths the same contraction context on targets without FMA.
    // The first two products lose a 2^-26 residual when rounded separately; contraction retains it.
    volatile f32 first[]{ 0x1.0008p+0f, -0x1.0008p+0f, 0.0f, -0.0f };
    volatile f32 second[]{ 0x1.fffp-1f, 0x1.fffp-1f, -0.0f, 1.0f };
    volatile f32 addend[]{ -1.0f, 1.0f, -0.0f, -0.0f };
    volatile f32 difference[]{ 1.0f, -1.0f, -0.0f, 0.0f };
    const SIMDVectorSingle lhs = VectorSet(first[0], first[1], first[2], first[3]);
    const SIMDVectorSingle rhs = VectorSet(second[0], second[1], second[2], second[3]);
    const SIMDVectorSingle sum = VectorMultiplyAddExpression(lhs, rhs, VectorSet(addend[0], addend[1], addend[2], addend[3]));
    const SIMDVectorSingle subtraction = VectorNegativeMultiplySubtractExpression(lhs, rhs, VectorSet(difference[0], difference[1], difference[2], difference[3]));
    for(usize lane = 0u; lane < 4u; ++lane){
        EXPECT_EQ(BitCast<u32>(VectorGetByIndex(sum, lane)), BitCast<u32>(LegacyMultiplyAddExpression(first[lane], second[lane], addend[lane]))) << lane;
        EXPECT_EQ(BitCast<u32>(VectorGetByIndex(subtraction, lane)), BitCast<u32>(LegacyNegativeMultiplySubtractExpression(first[lane], second[lane], difference[lane]))) << lane;
    }
}

TEST(Math, MinMaxReturnSecondOperandForSignedZeroAndUnorderedLanes){
    volatile u32 singleBits[][4]{
        { 0u, 0x80000000u, 0x7FC12345u, 0x40E00000u },
        { 0x80000000u, 0u, 0x40800000u, 0x7FC56789u },
        { 0x7FC12345u, 0xFFC12345u, 0x7F800000u, 0xFF800000u },
        { 0x7FC56789u, 0xFFC56789u, 0x7F800000u, 0xFF800000u }
    };
    volatile u64 doubleBits[][4]{
        { 0u, 0x8000000000000000u, 0x7FF8123456789ABCu, 0x401C000000000000u },
        { 0x8000000000000000u, 0u, 0x4010000000000000u, 0x7FF8ABCDEF012345u },
        { 0x7FF8123456789ABCu, 0xFFF8123456789ABCu, 0x7FF0000000000000u, 0xFFF0000000000000u },
        { 0x7FF8ABCDEF012345u, 0xFFF8ABCDEF012345u, 0x7FF0000000000000u, 0xFFF0000000000000u }
    };
    for(usize pair = 0u; pair < LengthOf(singleBits); pair += 2u){
        for(const bool reverse : { false, true }){
            const usize firstRow = pair + (reverse ? 1u : 0u);
            const usize secondRow = pair + (reverse ? 0u : 1u);
            const SIMDVectorSingle first = VectorSetInt(singleBits[firstRow][0], singleBits[firstRow][1], singleBits[firstRow][2], singleBits[firstRow][3]);
            const SIMDVectorSingle second = VectorSetInt(singleBits[secondRow][0], singleBits[secondRow][1], singleBits[secondRow][2], singleBits[secondRow][3]);
            const SIMDVectorSingle minimum = VectorMin(first, second);
            const SIMDVectorSingle maximum = VectorMax(first, second);
            const SIMDVectorDouble firstDouble = VectorDoubleSetInt(doubleBits[firstRow][0], doubleBits[firstRow][1], doubleBits[firstRow][2], doubleBits[firstRow][3]);
            const SIMDVectorDouble secondDouble = VectorDoubleSetInt(doubleBits[secondRow][0], doubleBits[secondRow][1], doubleBits[secondRow][2], doubleBits[secondRow][3]);
            const SIMDVectorDouble minimumDouble = VectorDoubleMin(firstDouble, secondDouble);
            const SIMDVectorDouble maximumDouble = VectorDoubleMax(firstDouble, secondDouble);
            for(usize lane = 0u; lane < 4u; ++lane){
                EXPECT_EQ(VectorGetIntByIndex(minimum, lane), singleBits[secondRow][lane]) << pair << ':' << reverse << ':' << lane;
                EXPECT_EQ(VectorGetIntByIndex(maximum, lane), singleBits[secondRow][lane]) << pair << ':' << reverse << ':' << lane;
                EXPECT_EQ(VectorDoubleGetIntByIndex(minimumDouble, lane), doubleBits[secondRow][lane]) << pair << ':' << reverse << ':' << lane;
                EXPECT_EQ(VectorDoubleGetIntByIndex(maximumDouble, lane), doubleBits[secondRow][lane]) << pair << ':' << reverse << ':' << lane;
            }
        }
    }
}

TEST(Math, SingleSquareRootsPreserveSignedZeroSubnormalsAndInfinityWithoutInvalidExceptions){
#if defined(NWB_HAS_NEON) && !defined(NWB_HAS_NEON64)
    GTEST_SKIP() << "This case requires native vector square root and division.";
#else
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    }
    );

    volatile u32 runtimeBits[]{ 0u, 0x80000000u, 0x7f800000u, 2u };
    const SIMDVectorSingle input = VectorSetInt(runtimeBits[0], runtimeBits[1], runtimeBits[2], runtimeBits[3]);
    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const SIMDVectorSingle root = VectorSqrt(input);
    const i32 rootInvalid = TestFloatingPointExceptions(FE_INVALID);
    EXPECT_EQ(rootInvalid, 0);
    const u32 expectedRoot[]{ 0u, 0x80000000u, 0x7f800000u, BitCast<u32>(0x1p-74f) };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorGetIntByIndex(root, lane), expectedRoot[lane]) << lane;

    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const SIMDVectorSingle reciprocalRoot = VectorReciprocalSqrt(input);
    const i32 reciprocalInvalid = TestFloatingPointExceptions(FE_INVALID);
    EXPECT_EQ(reciprocalInvalid, 0);
    const u32 expectedReciprocal[]{ 0x7f800000u, 0xff800000u, 0u, BitCast<u32>(0x1p+74f) };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorGetIntByIndex(reciprocalRoot, lane), expectedReciprocal[lane]) << lane;
#endif
}

TEST(Math, DoubleVectorRetainsPrecisionCancellationSubnormalsAndSignedZero){
    const SIMDVectorDouble precision = SIMDVectorDouble{ 0x1p+40, 0x1p-40, -0x1p+40, -0x1p-40 } + SIMDVectorDouble{ 1., 0x1p-80, -1., -0x1p-80 };
    EXPECT_EQ(precision.x, 0x1.0000000001p+40);
    EXPECT_EQ(precision.y, 0x1.0000000001p-40);
    EXPECT_EQ(precision.z, -0x1.0000000001p+40);
    EXPECT_EQ(precision.w, -0x1.0000000001p-40);

    // Separately rounded products lose the 2^-54 residual; expression contraction follows the scalar compiler policy.
    volatile f64 runtimeInputs[][4]{
        { 0x1.0000002p+0, -0x1.0000002p+0, 0.0, -0.0 },
        { 0x1.ffffffcp-1, 0x1.ffffffcp-1, -0.0, 1.0 },
        { -1.0, 1.0, -0.0, -0.0 },
    };
    const SIMDVectorDouble first = VectorDoubleSet(runtimeInputs[0][0], runtimeInputs[0][1], runtimeInputs[0][2], runtimeInputs[0][3]);
    const SIMDVectorDouble second = VectorDoubleSet(runtimeInputs[1][0], runtimeInputs[1][1], runtimeInputs[1][2], runtimeInputs[1][3]);
    const SIMDVectorDouble addend = VectorDoubleSet(runtimeInputs[2][0], runtimeInputs[2][1], runtimeInputs[2][2], runtimeInputs[2][3]);
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
            EXPECT_EQ(BitCast<u64>(truncate[lane]), BitCast<u64>(Truncate(input[lane]))) << lane;
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

TEST(Math, SingleFixedRoundingPreservesTiesSignedZeroAndSpecialValuesAcrossAmbientModes){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    }
    );

    const i32 modes[]{ FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    volatile f32 runtimeInputs[][4]{
        { 0.5f, -0.5f, 1.5f, -1.5f },
        { 0.25f, -0.25f, 2.5f, -2.5f },
        { -0.0f, 0.0f, 0x1p+23f, -0x1p+23f },
        { 0x1p-149f, -0x1p-149f, 0x1.fffffep+22f, -0x1.fffffep+22f },
    };
    const f32 expected[][4][4]{
        {
            { 0.0f, -0.0f, 2.0f, -2.0f },
            { 0.0f, -0.0f, 2.0f, -2.0f },
            { -0.0f, 0.0f, 0x1p+23f, -0x1p+23f },
            { 0.0f, -0.0f, 0x1p+23f, -0x1p+23f },
        },
        {
            { 0.0f, -0.0f, 1.0f, -1.0f },
            { 0.0f, -0.0f, 2.0f, -2.0f },
            { -0.0f, 0.0f, 0x1p+23f, -0x1p+23f },
            { 0.0f, -0.0f, 0x1.fffffcp+22f, -0x1.fffffcp+22f },
        },
        {
            { 0.0f, -1.0f, 1.0f, -2.0f },
            { 0.0f, -1.0f, 2.0f, -3.0f },
            { -0.0f, 0.0f, 0x1p+23f, -0x1p+23f },
            { 0.0f, -1.0f, 0x1.fffffcp+22f, -0x1p+23f },
        },
        {
            { 1.0f, -0.0f, 2.0f, -1.0f },
            { 1.0f, -0.0f, 3.0f, -2.0f },
            { -0.0f, 0.0f, 0x1p+23f, -0x1p+23f },
            { 1.0f, -0.0f, 0x1p+23f, -0x1.fffffcp+22f },
        },
    };
    volatile u32 specialBits[]{ 0x7f800000u, 0xff800000u, 0x7fc01234u, 0xffc05678u };
    for(const i32 mode : modes){
        ASSERT_TRUE(SetFloatingPointRoundingMode(mode));
        for(usize row = 0u; row < LengthOf(runtimeInputs); ++row){
            const SIMDVectorSingle input = VectorSet(runtimeInputs[row][0], runtimeInputs[row][1], runtimeInputs[row][2], runtimeInputs[row][3]);
            const SIMDVectorSingle results[]{ VectorRound(input), VectorTruncate(input), VectorFloor(input), VectorCeiling(input) };
            for(usize operation = 0u; operation < LengthOf(results); ++operation){
                for(usize lane = 0u; lane < 4u; ++lane)
                    EXPECT_EQ(VectorGetIntByIndex(results[operation], lane), BitCast<u32>(expected[operation][row][lane])) << mode << ':' << operation << ':' << row << ':' << lane;
            }
        }
        const SIMDVectorSingle special = VectorSetInt(specialBits[0], specialBits[1], specialBits[2], specialBits[3]);
        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        const SIMDVectorSingle specialResults[]{ VectorRound(special), VectorTruncate(special), VectorFloor(special), VectorCeiling(special) };
        const i32 invalid = TestFloatingPointExceptions(FE_INVALID);
        EXPECT_EQ(invalid, 0) << mode;
        for(usize operation = 0u; operation < LengthOf(specialResults); ++operation){
            for(usize lane = 0u; lane < 4u; ++lane)
                EXPECT_EQ(VectorGetIntByIndex(specialResults[operation], lane), specialBits[lane]) << mode << ':' << operation << ':' << lane;
        }
    }
}

TEST(Math, DoubleFixedRoundingIgnoresAmbientDirectedModesAndKeepsNearestTiesAndSignedZero){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    }
    );

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
        const auto setStatus = SetFloatingPointRoundingMode(mode);
        EXPECT_TRUE(setStatus);
        if(!setStatus)
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
    const auto restoreStatus = SetFloatingPointEnvironment(*originalEnvironment);
    EXPECT_TRUE(restoreStatus);
    if(restoreStatus)
        restoreEnvironment.release();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

