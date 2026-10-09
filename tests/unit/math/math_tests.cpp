// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>

#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/simdmath.h>
#include <global/math/vector_double.h>
#include <global/compile.h>
#include <global/limit.h>
#include <global/simplemath.h>


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

NWB_NOINLINE SIMDVectorDouble DoubleMultiplyAddExpression(const SIMDVectorDouble first,
    const SIMDVectorDouble second, const SIMDVectorDouble addend
)noexcept{
    return first * second + addend;
}

NWB_NOINLINE SIMDVectorDouble DoubleMultiplyThenAdd(const SIMDVectorDouble first,
    const SIMDVectorDouble second, const SIMDVectorDouble addend
)noexcept{
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
    const SIMDVector lhs = VectorSet(first[0], first[1], first[2], first[3]);
    const SIMDVector rhs = VectorSet(second[0], second[1], second[2], second[3]);
    const SIMDVector sum = VectorMultiplyAddExpression(lhs, rhs, VectorSet(addend[0], addend[1], addend[2], addend[3]));
    const SIMDVector subtraction = VectorNegativeMultiplySubtractExpression(lhs, rhs,
        VectorSet(difference[0], difference[1], difference[2], difference[3]));
    for(usize lane = 0u; lane < 4u; ++lane){
        EXPECT_EQ(BitCast<u32>(VectorGetByIndex(sum, lane)),
            BitCast<u32>(LegacyMultiplyAddExpression(first[lane], second[lane], addend[lane]))) << lane;
        EXPECT_EQ(BitCast<u32>(VectorGetByIndex(subtraction, lane)),
            BitCast<u32>(LegacyNegativeMultiplySubtractExpression(first[lane], second[lane], difference[lane]))) << lane;
    }
}

TEST(Math, DoubleVectorRetainsPrecisionCancellationSubnormalsAndSignedZero){
    const SIMDVectorDouble precision = SIMDVectorDouble{ 0x1p+40, 0x1p-40 } + SIMDVectorDouble{ 1., 0x1p-80 };
    EXPECT_EQ(precision.x, 0x1.0000000001p+40);
    EXPECT_EQ(precision.y, 0x1.0000000001p-40);

    // Separately rounded products lose the 2^-54 residual; expression contraction follows the scalar compiler policy.
    const SIMDVectorDouble first{ 0x1.0000002p+0, -0x1.0000002p+0 };
    const SIMDVectorDouble second{ 0x1.ffffffcp-1, 0x1.ffffffcp-1 };
    const SIMDVectorDouble addend{ -1., 1. };
    const SIMDVectorDouble expression = DoubleMultiplyAddExpression(first, second, addend);
    const SIMDVectorDouble separate = DoubleMultiplyThenAdd(first, second, addend);
    for(usize lane = 0u; lane < 2u; ++lane){
        EXPECT_EQ(BitCast<u64>(expression[lane]),
            BitCast<u64>(LegacyDoubleMultiplyAddExpression(first[lane], second[lane], addend[lane]))) << lane;
        EXPECT_EQ(BitCast<u64>(separate[lane]),
            BitCast<u64>(LegacyDoubleMultiplyThenAdd(first[lane], second[lane], addend[lane]))) << lane;
    }

    const SIMDVectorDouble subnormal = SIMDVectorDouble{ BitCast<f64>(u64{ 1u }), BitCast<f64>(u64{ 3u }) }
        * SIMDVectorDouble{ 2., 0.5 };
    EXPECT_EQ(BitCast<u64>(subnormal.x), 2u);
    EXPECT_EQ(BitCast<u64>(subnormal.y), 2u);
    const SIMDVectorDouble signedZero = SIMDVectorDouble{ -0., 0. } * SIMDVectorDouble{ 1., -1. };
    EXPECT_TRUE(SignBit(signedZero.x));
    EXPECT_TRUE(SignBit(signedZero.y));
}

TEST(Math, DoubleVectorFloorAndCeilingPreserveBinary64BoundaryValues){
    const SIMDVectorDouble inputs[]{
        { 0.0, -0.0 },
        { BitCast<f64>(u64{ 1u }), -BitCast<f64>(u64{ 1u }) },
        { 0x1.fffffffffffffp-1, 0x1.0000000000001p+0 },
        { 0x1p+53, -0x1p+53 },
        { Limit<f64>::s_Infinity, -Limit<f64>::s_Infinity },
        { Limit<f64>::s_QuietNaN, -Limit<f64>::s_QuietNaN },
    };
    for(const SIMDVectorDouble input : inputs){
        const SIMDVectorDouble floor = VectorDoubleFloor(input);
        const SIMDVectorDouble ceiling = VectorDoubleCeiling(input);
        for(usize lane = 0u; lane < 2u; ++lane){
            if(IsNaN(input[lane])){
                EXPECT_TRUE(IsNaN(floor[lane]));
                EXPECT_TRUE(IsNaN(ceiling[lane]));
                continue;
            }
            EXPECT_EQ(BitCast<u64>(floor[lane]), BitCast<u64>(Floor(input[lane]))) << lane;
            EXPECT_EQ(BitCast<u64>(ceiling[lane]), BitCast<u64>(Ceil(input[lane]))) << lane;
        }
    }
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
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, planes);
    const Array<f32, 6u> centers = { 0.5f, 0.25f, 1.5f, 9.5f, 10.5f, 1.0f };
    const Array<ContainmentType::Enum, 6u> expected = {
        ContainmentType::Intersects, ContainmentType::Disjoint, ContainmentType::Contains,
        ContainmentType::Contains, ContainmentType::Intersects, ContainmentType::Contains,
    };
    for(usize index = 0u; index < centers.size(); ++index){
        const BoundingSphere sphere(Float3U(0.0f, 0.0f, centers[index]), index == centers.size() - 1u ? 0.0f : 0.5f);
        EXPECT_EQ(frustum.contains(sphere), expected[index]) << index;
        EXPECT_EQ(sphere.containedBy(planes[0], planes[1], planes[2], planes[3], planes[4], planes[5]), expected[index]) << index;
    }
}

TEST(Math, HyperbolicTangentSaturatesAndPreservesSignedZero){
    const f32 infinity = Limit<f32>::s_Infinity;
    EXPECT_TRUE(NearlyEqual4(
        VectorTanH(VectorSet(50.0f, -50.0f, infinity, -infinity)),
        1.0f,
        -1.0f,
        1.0f,
        -1.0f
    ));

    const SIMDVector signedZeroTanH = VectorTanH(VectorSet(-0.0f, 0.0f, -0.0f, 0.0f));
    EXPECT_TRUE(SignBit(VectorGetX(signedZeroTanH)));
    EXPECT_FALSE(SignBit(VectorGetY(signedZeroTanH)));
}

TEST(Math, VectorIntegerAddWrapsAtUnsignedLaneLimits){
    const SIMDVector sum = VectorAddInt(
        VectorSetInt(Limit<u32>::s_Max, 4u, 0xFFFFFFFEu, 19u),
        VectorSetInt(1u, 7u, 3u, Limit<u32>::s_Max)
    );

    EXPECT_EQ(VectorGetIntX(sum), 0u);
    EXPECT_EQ(VectorGetIntZ(sum), 1u);
    EXPECT_EQ(VectorGetIntW(sum), 18u);
}

TEST(Math, RefractCriticalAngle){
    const SIMDVector normal = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVector criticalIncident = VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector totalInternalIncident = VectorSet(0.866025404f, -0.5f, 0.0f, 0.0f);

    EXPECT_TRUE(NearlyEqual4(Vector2Refract(criticalIncident, normal, 1.0f), 1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual4(Vector3Refract(criticalIncident, normal, 1.0f), 1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual4(Vector4Refract(criticalIncident, normal, 1.0f), 1.0f, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual4(Vector3Refract(totalInternalIncident, normal, 2.0f), 0.0f, 0.0f, 0.0f, 0.0f));
}

TEST(Math, NormalizeOrUsesFallbackForZeroAndNonfiniteInput){
    const SIMDVector fallback = VectorSet(0.0f, 1.0f, 0.0f, 1.0f);
    const f32 infinity = Limit<f32>::s_Infinity;

    EXPECT_TRUE(NearlyEqual3(Vector3NormalizeOr(VectorSet(0.0f, 0.0f, 0.0f, 5.0f), fallback, 0.0f), 0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual4(Vector4NormalizeOr(VectorSet(infinity, 0.0f, 0.0f, 0.0f), fallback, 0.0f), 0.0f, 1.0f, 0.0f, 1.0f));
}

TEST(Math, TransformedBoundsRejectInvalidAndOverflowingInputs){
    const SIMDVector minimum = VectorSet(-1.0f, -2.0f, -3.0f, 0.0f);
    const SIMDVector maximum = VectorSet(1.0f, 2.0f, 3.0f, 0.0f);
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
    const SIMDVector boxHalfExtents = VectorSet(1.0f, 1.0f, 1.0f, 0.0f);


    EXPECT_TRUE(NearlyEqual3(
        SdfTests::BoxNormal(VectorSet(2.0f, 2.0f, 1.0f, 0.0f), boxHalfExtents, VectorSet(0.0f, 1.0f, 0.0f, 0.0f), 0.000001f),
        0.70710677f,
        0.70710677f,
        0.0f,
        0.00001f
    ));
    EXPECT_TRUE(NearlyEqual3(
        SdfTests::BoxNormal(VectorSet(0.9f, 0.9f, 0.2f, 0.0f), boxHalfExtents, VectorSet(0.0f, 1.0f, 0.0f, 0.0f), 0.000001f),
        1.0f,
        0.0f,
        0.0f
    ));
}

TEST(Math, DegeneratePlaneNormalUsesFallback){
    EXPECT_TRUE(NearlyEqual4(
        PlaneTests::FromPointNormal(
            VectorZero(),
            VectorSet(1.0f, 2.0f, 3.0f, 0.0f),
            VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
        ),
        0.0f,
        0.0f,
        1.0f,
        -3.0f
    ));
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
        const SIMDMatrix reconstructed = MatrixAffineTransformation(
            decomposition->scale, VectorZero(), decomposition->rotation, decomposition->translation
        );
        for(usize row = 0u; row < 4u; ++row){
            EXPECT_TRUE(NearlyEqual4(reconstructed.v[row],
                VectorGetX(matrices[index].v[row]), VectorGetY(matrices[index].v[row]),
                VectorGetZ(matrices[index].v[row]), VectorGetW(matrices[index].v[row]))) << row;
        }
    }

    const SIMDMatrix shear = MatrixSet(
        1.0f, 1.0f, 0.0f, 3.0f, 0.0f, 1.0f, 0.0f, -2.0f, 0.0f, 0.0f, 1.0f, 7.0f, 0.0f, 0.0f, 0.0f, 1.0f
    );
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
    f32 source[6] = {
        0.0f,
        -1.5f,
        1.0f,
        65504.0f,
        5.9604644775390625e-8f,
        Limit<f32>::s_Infinity
    };
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
    EXPECT_EQ(FloatHashBits(-0.0f), FloatHashBits(0.0f));
    CheckStorageHashMatchesEquality(Float4(-0.0f, 1.0f, 2.0f, 3.0f), Float4(0.0f, 1.0f, 2.0f, 3.0f), Float4(0.0f, 1.0f, 2.0f, 4.0f));
    CheckStorageHashMatchesEquality(Float3Int(-0.0f, 1.0f, 2.0f, -3), Float3Int(0.0f, 1.0f, 2.0f, -3), Float3Int(0.0f, 1.0f, 2.0f, -4));
    CheckStorageHashMatchesEquality(Float3UInt(-0.0f, 1.0f, 2.0f, 3u), Float3UInt(0.0f, 1.0f, 2.0f, 3u), Float3UInt(0.0f, 1.0f, 2.0f, 4u));
    CheckStorageHashMatchesEquality(Float2U(-0.0f, 1.0f), Float2U(0.0f, 1.0f), Float2U(0.0f, 2.0f));
    CheckStorageHashMatchesEquality(Float3U(-0.0f, 1.0f, 2.0f), Float3U(0.0f, 1.0f, 2.0f), Float3U(0.0f, 1.0f, 3.0f));
    CheckStorageHashMatchesEquality(Float4U(-0.0f, 1.0f, 2.0f, 3.0f), Float4U(0.0f, 1.0f, 2.0f, 3.0f), Float4U(0.0f, 1.0f, 2.0f, 4.0f));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

