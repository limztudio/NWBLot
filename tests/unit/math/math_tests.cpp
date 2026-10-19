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

TEST(Math, SinglePartialLaneDotIgnoresNonfiniteInactiveLanesWithoutInvalidExceptions){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    }
    );

    volatile u32 inactiveBits[][2]{ { 0x7f800000u, 0u }, { 0u, 0x7f800000u }, { 0x7fc01234u, 0u }, { 0x7f800001u, 0u } };
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
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    }
    );

    volatile u32 inactiveBits[][2]{ { 0x7f800000u, 0u }, { 0u, 0x7f800000u }, { 0x7fc01234u, 0u }, { 0x7f800001u, 0u }, { 0u, 0x7f800001u }, { 0x7f7fffffu, 0x7f7fffffu } };
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
        const f32 cross3Expected[]{ 5.0f, -2.0f, -1.0f };
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
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    }
    );
    const f64 lhsInactive[]{ Limit<f64>::s_Infinity, 0.0, Limit<f64>::s_QuietNaN, Limit<f64>::s_QuietNaN };
    const f64 rhsInactive[]{ 0.0, Limit<f64>::s_Infinity, 0.0, Limit<f64>::s_QuietNaN };
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
    volatile u32 domainBits[]{ 0x80000000u, 0x7f800000u, 0xbf800000u, 0x7fc01234u };
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
    const u64 overflowBits[]{ 0u, 0u, 0x8000000000000000u, 0x8000000000000000u };
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
    EXPECT_NEAR(below.y, -Sqrt(1.0 - belowIndex * belowIndex), 1.0e-20);
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
    EXPECT_NEAR(rotated.x, Cos(angle), 4.0e-16);
    EXPECT_NEAR(rotated.y, Sin(angle), 4.0e-16);
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
    const SIMDVectorDouble fromQuaternion = VectorDouble3Rotate(direction, quaternion->value);
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
            EXPECT_TRUE(Vector4LessOrEqual(VectorAbs(VectorSubtract(reconstructed.v[row], matrices[index].v[row])), VectorReplicate(NWB::Tests::s_DefaultNearlyEqualEpsilon))) << row;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

