// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>

#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/simdmath.h>
#include <global/compile.h>
#include <global/limit.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_math_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::NearlyEqual;
using NWB::Tests::NearlyEqual3;
using NWB::Tests::NearlyEqual4;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Math, CenteredOrthographicProjectionPreservesNegativeZeroTranslation){
    const SIMDMatrix projection = MatrixOrthographicOffCenterLH(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 11.0f);
    EXPECT_TRUE(SignBit(VectorGetW(projection.v[0])));
    EXPECT_TRUE(SignBit(VectorGetW(projection.v[1])));
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

TEST(Math, VectorIntegerAddKeepsPackedLaneArithmetic){
    const SIMDVector sum = VectorAddInt(
        VectorSetInt(Limit<u32>::s_Max, 4u, 0xFFFFFFFEu, 19u),
        VectorSetInt(1u, 7u, 3u, Limit<u32>::s_Max)
    );

    EXPECT_EQ(VectorGetIntX(sum), 0u);
    EXPECT_EQ(VectorGetIntY(sum), 11u);
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

TEST(Math, NormalizeOrHelpers){
    const SIMDVector fallback = VectorSet(0.0f, 1.0f, 0.0f, 1.0f);
    const f32 infinity = Limit<f32>::s_Infinity;

    EXPECT_TRUE(NearlyEqual4(Vector2NormalizeOr(VectorSet(3.0f, 4.0f, 7.0f, 9.0f), fallback, 0.0f), 0.6f, 0.8f, 0.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual3(Vector3NormalizeOr(VectorSet(0.0f, 0.0f, 0.0f, 5.0f), fallback, 0.0f), 0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual4(Vector4NormalizeOr(VectorSet(infinity, 0.0f, 0.0f, 0.0f), fallback, 0.0f), 0.0f, 1.0f, 0.0f, 1.0f));
}

TEST(Math, SdfHelpers){
    const SIMDVector boxHalfExtents = VectorSet(1.0f, 1.0f, 1.0f, 0.0f);

    EXPECT_TRUE(NearlyEqual(VectorGetX(SdfTests::Box(VectorSet(2.0f, 0.5f, -0.25f, 0.0f), boxHalfExtents)), 1.0f));
    EXPECT_TRUE(NearlyEqual(VectorGetX(SdfTests::Box(VectorSet(0.25f, 0.5f, 0.1f, 0.0f), boxHalfExtents)), -0.5f));

    EXPECT_TRUE(NearlyEqual3(
        SdfTests::BoxNormal(VectorSet(2.0f, 2.0f, 1.0f, 0.0f), boxHalfExtents, VectorSet(0.0f, 1.0f, 0.0f, 0.0f), 0.000001f),
        0.70710677f,
        0.70710677f,
        0.0f,
        0.00001f
    ));
    EXPECT_TRUE(NearlyEqual3(
        SdfTests::BoxNormal(VectorSet(0.2f, -0.9f, 0.1f, 0.0f), boxHalfExtents, VectorSet(0.0f, 1.0f, 0.0f, 0.0f), 0.000001f),
        0.0f,
        -1.0f,
        0.0f
    ));
    EXPECT_TRUE(NearlyEqual3(
        SdfTests::BoxNormal(VectorSet(0.9f, 0.9f, 0.2f, 0.0f), boxHalfExtents, VectorSet(0.0f, 1.0f, 0.0f, 0.0f), 0.000001f),
        1.0f,
        0.0f,
        0.0f
    ));

    const SIMDVector capsuleRadiusHalfHeight = VectorSet(0.5f, 1.5f, 0.0f, 0.0f);
    EXPECT_TRUE(NearlyEqual(VectorGetX(SdfTests::CapsuleY(VectorSet(0.0f, 2.5f, 0.0f, 0.0f), capsuleRadiusHalfHeight)), 0.5f));
    EXPECT_TRUE(NearlyEqual(VectorGetX(SdfTests::CapsuleY(VectorSet(0.25f, 0.0f, 0.0f, 0.0f), capsuleRadiusHalfHeight)), -0.25f));
    EXPECT_TRUE(NearlyEqual3(SdfTests::CapsuleYNormal(VectorSet(0.0f, 2.5f, 0.0f, 0.0f), capsuleRadiusHalfHeight, 0.000001f), 0.0f, 1.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual3(SdfTests::CapsuleYNormal(VectorSet(1.0f, 0.5f, 0.0f, 0.0f), capsuleRadiusHalfHeight, 0.000001f), 1.0f, 0.0f, 0.0f));
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
    BoundingSphere::createMerged(contained, larger, BoundingSphere(Float3U(1.0f, 0.0f, 0.0f), 1.0f));
    EXPECT_TRUE(NearlyEqual4(LoadFloat(contained.centerRadius), 0.0f, 0.0f, 0.0f, 3.0f));
}

TEST(Math, HalfFloatScalarConversion){
    EXPECT_EQ(ConvertFloatToHalf(0.0f), static_cast<Half>(0x0000u));
    EXPECT_EQ(ConvertFloatToHalf(-0.0f), static_cast<Half>(0x8000u));
    EXPECT_EQ(ConvertFloatToHalf(1.0f), static_cast<Half>(0x3c00u));
    EXPECT_EQ(ConvertFloatToHalf(-2.0f), static_cast<Half>(0xc000u));
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
    EXPECT_TRUE(NearlyEqual(ConvertHalfToFloat(static_cast<Half>(0x3c00u)), 1.0f));
    EXPECT_TRUE(NearlyEqual(ConvertHalfToFloat(static_cast<Half>(0xc000u)), -2.0f));
    EXPECT_TRUE(NearlyEqual(ConvertHalfToFloat(static_cast<Half>(0x7bffu)), 65504.0f));
    EXPECT_EQ(ConvertHalfToFloat(static_cast<Half>(0x7c00u)), Limit<f32>::s_Infinity);
    EXPECT_EQ(ConvertHalfToFloat(static_cast<Half>(0xfc00u)), -Limit<f32>::s_Infinity);
    EXPECT_TRUE(IsNaN(ConvertHalfToFloat(static_cast<Half>(0x7e00u))));
}

TEST(Math, UnsignedFloatConversion){
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(0.0f), 0u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(-0.0f), 0u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(-1.0f), 0u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(1.0f), 0x3c0u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(9.5367431640625e-7f), 0x001u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(6.103515625e-5f), 0x040u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(65536.0f), 0x7c0u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(-Limit<f32>::s_Infinity), 0u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<6u>(Limit<f32>::s_Infinity), 0x7c0u);
    EXPECT_EQ(ConvertFloatToUnsignedFloat<5u>(1.0f), 0x1e0u);

    const u32 quietNaN = ConvertFloatToUnsignedFloat<6u>(Limit<f32>::s_QuietNaN);
    EXPECT_EQ(quietNaN & 0x7c0u, 0x7c0u);
    EXPECT_NE(quietNaN & 0x03fu, 0u);
}

TEST(Math, HalfFloatBufferConversion){
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

    EXPECT_EQ(ConvertFloatBufferToHalf(packed, source, 6u), packed);
    EXPECT_EQ(packed[0], static_cast<Half>(0x0000u));
    EXPECT_EQ(packed[1], static_cast<Half>(0xbe00u));
    EXPECT_EQ(packed[2], static_cast<Half>(0x3c00u));
    EXPECT_EQ(packed[3], static_cast<Half>(0x7bffu));
    EXPECT_EQ(packed[4], static_cast<Half>(0x0001u));
    EXPECT_EQ(packed[5], static_cast<Half>(0x7c00u));

    EXPECT_EQ(ConvertHalfBufferToFloat(unpacked, packed, 6u), unpacked);
    EXPECT_TRUE(NearlyEqual(unpacked[0], 0.0f));
    EXPECT_TRUE(NearlyEqual(unpacked[1], -1.5f));
    EXPECT_TRUE(NearlyEqual(unpacked[2], 1.0f));
    EXPECT_TRUE(NearlyEqual(unpacked[3], 65504.0f));
    EXPECT_TRUE(NearlyEqual(unpacked[4], 5.9604644775390625e-8f));
    EXPECT_EQ(unpacked[5], Limit<f32>::s_Infinity);
}

TEST(Math, HalfFloat4Conversion){
    const Half4U packed = MakeHalf4U(0.0f, -1.5f, 65504.0f, Limit<f32>::s_Infinity);
    EXPECT_EQ(packed.raw[0], ConvertFloatToHalf(0.0f));
    EXPECT_EQ(packed.raw[1], ConvertFloatToHalf(-1.5f));
    EXPECT_EQ(packed.raw[2], ConvertFloatToHalf(65504.0f));
    EXPECT_EQ(packed.raw[3], ConvertFloatToHalf(Limit<f32>::s_Infinity));

    const Float4U unpacked = LoadHalf4U(packed);
    EXPECT_TRUE(NearlyEqual(unpacked.x, 0.0f));
    EXPECT_TRUE(NearlyEqual(unpacked.y, -1.5f));
    EXPECT_TRUE(NearlyEqual(unpacked.z, 65504.0f));
    EXPECT_EQ(unpacked.w, Limit<f32>::s_Infinity);

    const SIMDVector simdUnpacked = LoadHalf(packed);
    EXPECT_TRUE(NearlyEqual(VectorGetX(simdUnpacked), 0.0f));
    EXPECT_TRUE(NearlyEqual(VectorGetY(simdUnpacked), -1.5f));
    EXPECT_TRUE(NearlyEqual(VectorGetZ(simdUnpacked), 65504.0f));
    EXPECT_EQ(VectorGetW(simdUnpacked), Limit<f32>::s_Infinity);

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

TEST(Math, AffineMatrixQueriesAndDualQuaternion){
    constexpr f32 s_AffineEpsilon = 0.000001f;
    constexpr f32 s_DeterminantEpsilon = 0.000000000001f;
    constexpr f32 s_RigidEpsilon = 0.001f;

    const SIMDMatrix translation = MatrixTranslation(1.0f, 2.0f, 3.0f);
    EXPECT_TRUE(MatrixIsAffine(translation, s_AffineEpsilon));
    EXPECT_TRUE(NearlyEqual(MatrixLinearDeterminant(translation), 1.0f));
    EXPECT_TRUE(NearlyEqual4(MatrixLinearDeterminantV(translation), 1.0f, 1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(MatrixIsInvertibleAffine(translation, s_AffineEpsilon, s_DeterminantEpsilon));
    EXPECT_TRUE(MatrixIsRigidAffine(translation, s_AffineEpsilon, s_RigidEpsilon));

    SIMDVector real = VectorZero();
    SIMDVector dual = VectorZero();
    ASSERT_TRUE(MatrixTryBuildRigidDualQuaternion(
        translation,
        s_AffineEpsilon,
        s_RigidEpsilon,
        real,
        dual
    ));
    EXPECT_TRUE(NearlyEqual4(real, 0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_TRUE(NearlyEqual4(dual, 0.5f, 1.0f, 1.5f, 0.0f));

    const SIMDMatrix scaled = MatrixScaling(2.0f, 3.0f, 4.0f);
    EXPECT_TRUE(NearlyEqual(MatrixLinearDeterminant(scaled), 24.0f));
    EXPECT_TRUE(NearlyEqual4(MatrixLinearDeterminantV(scaled), 24.0f, 24.0f, 24.0f, 24.0f));
    EXPECT_TRUE(MatrixIsInvertibleAffine(scaled, s_AffineEpsilon, s_DeterminantEpsilon));
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

static Int2U MakeInt2Value(const i32 x, const i32 y){
    Int2U value = {};
    value.x = x;
    value.y = y;
    return value;
}

static Int3U MakeInt3Value(const i32 x, const i32 y, const i32 z){
    Int3U value = {};
    value.x = x;
    value.y = y;
    value.z = z;
    return value;
}

static Int4 MakeInt4Value(const i32 x, const i32 y, const i32 z, const i32 w){
    Int4 value = {};
    value.x = x;
    value.y = y;
    value.z = z;
    value.w = w;
    return value;
}

static Int4U MakeInt4UValue(const i32 x, const i32 y, const i32 z, const i32 w){
    Int4U value = {};
    value.x = x;
    value.y = y;
    value.z = z;
    value.w = w;
    return value;
}

static UInt2U MakeUInt2Value(const u32 x, const u32 y){
    UInt2U value = {};
    value.x = x;
    value.y = y;
    return value;
}

static UInt3U MakeUInt3Value(const u32 x, const u32 y, const u32 z){
    UInt3U value = {};
    value.x = x;
    value.y = y;
    value.z = z;
    return value;
}

static UInt4 MakeUInt4Value(const u32 x, const u32 y, const u32 z, const u32 w){
    UInt4 value = {};
    value.x = x;
    value.y = y;
    value.z = z;
    value.w = w;
    return value;
}

static UInt4U MakeUInt4UValue(const u32 x, const u32 y, const u32 z, const u32 w){
    UInt4U value = {};
    value.x = x;
    value.y = y;
    value.z = z;
    value.w = w;
    return value;
}

TEST(Math, MathStorageHashAndEquality){
    EXPECT_EQ(FloatHashBits(-0.0f), FloatHashBits(0.0f));

    const Half h1 = static_cast<Half>(1u);
    const Half h2 = static_cast<Half>(s_ExpectedDualCount);
    const Half h3 = static_cast<Half>(3u);
    const Half h4 = static_cast<Half>(4u);
    const Half h5 = static_cast<Half>(5u);
    CheckStorageHashMatchesEquality(Half2U(h1, h2), Half2U(h1, h2), Half2U(h1, h3));
    CheckStorageHashMatchesEquality(Half4U(h1, h2, h3, h4), Half4U(h1, h2, h3, h4), Half4U(h1, h2, h3, h5));
    CheckStorageHashMatchesEquality(Float4(-0.0f, 1.0f, 2.0f, 3.0f), Float4(0.0f, 1.0f, 2.0f, 3.0f), Float4(0.0f, 1.0f, 2.0f, 4.0f));
    CheckStorageHashMatchesEquality(MakeInt4Value(-1, 2, -3, 4), MakeInt4Value(-1, 2, -3, 4), MakeInt4Value(-1, 2, -3, 5));
    CheckStorageHashMatchesEquality(MakeUInt4Value(1u, s_ExpectedDualCount, 3u, 4u), MakeUInt4Value(1u, s_ExpectedDualCount, 3u, 4u), MakeUInt4Value(1u, s_ExpectedDualCount, 3u, 5u));
    CheckStorageHashMatchesEquality(Float3Int(-0.0f, 1.0f, 2.0f, -3), Float3Int(0.0f, 1.0f, 2.0f, -3), Float3Int(0.0f, 1.0f, 2.0f, -4));
    CheckStorageHashMatchesEquality(Float3UInt(-0.0f, 1.0f, 2.0f, 3u), Float3UInt(0.0f, 1.0f, 2.0f, 3u), Float3UInt(0.0f, 1.0f, 2.0f, 4u));
    CheckStorageHashMatchesEquality(Float2U(-0.0f, 1.0f), Float2U(0.0f, 1.0f), Float2U(0.0f, 2.0f));
    CheckStorageHashMatchesEquality(Float3U(-0.0f, 1.0f, 2.0f), Float3U(0.0f, 1.0f, 2.0f), Float3U(0.0f, 1.0f, 3.0f));
    CheckStorageHashMatchesEquality(Float4U(-0.0f, 1.0f, 2.0f, 3.0f), Float4U(0.0f, 1.0f, 2.0f, 3.0f), Float4U(0.0f, 1.0f, 2.0f, 4.0f));
    CheckStorageHashMatchesEquality(MakeInt2Value(-1, 2), MakeInt2Value(-1, 2), MakeInt2Value(-1, 3));
    CheckStorageHashMatchesEquality(MakeInt3Value(-1, 2, -3), MakeInt3Value(-1, 2, -3), MakeInt3Value(-1, 2, -4));
    CheckStorageHashMatchesEquality(MakeInt4UValue(-1, 2, -3, 4), MakeInt4UValue(-1, 2, -3, 4), MakeInt4UValue(-1, 2, -3, 5));
    CheckStorageHashMatchesEquality(MakeUInt2Value(1u, s_ExpectedDualCount), MakeUInt2Value(1u, s_ExpectedDualCount), MakeUInt2Value(1u, 3u));
    CheckStorageHashMatchesEquality(MakeUInt3Value(1u, s_ExpectedDualCount, 3u), MakeUInt3Value(1u, s_ExpectedDualCount, 3u), MakeUInt3Value(1u, s_ExpectedDualCount, 4u));
    CheckStorageHashMatchesEquality(MakeUInt4UValue(1u, s_ExpectedDualCount, 3u, 4u), MakeUInt4UValue(1u, s_ExpectedDualCount, 3u, 4u), MakeUInt4UValue(1u, s_ExpectedDualCount, 3u, 5u));

    Float33U float33 = {};
    Float34U float34u = {};
    Float44U float44u = {};
    Float34 float34 = {};
    Float44 float44 = {};
    for(usize i = 0u; i < 9u; ++i)
        float33.raw[i] = static_cast<f32>(i + 1u);
    for(usize i = 0u; i < 12u; ++i){
        float34u.raw[i] = static_cast<f32>(i + 1u);
        float34.raw[i] = static_cast<f32>(i + 1u);
    }
    for(usize i = 0u; i < 16u; ++i){
        float44u.raw[i] = static_cast<f32>(i + 1u);
        float44.raw[i] = static_cast<f32>(i + 1u);
    }

    Float33U different33 = float33;
    Float34U different34u = float34u;
    Float44U different44u = float44u;
    Float34 different34 = float34;
    Float44 different44 = float44;
    different33.raw[8u] = 10.0f;
    different34u.raw[11u] = 13.0f;
    different44u.raw[15u] = 17.0f;
    different34.raw[11u] = 13.0f;
    different44.raw[15u] = 17.0f;

    CheckStorageHashMatchesEquality(float33, float33, different33);
    CheckStorageHashMatchesEquality(float34u, float34u, different34u);
    CheckStorageHashMatchesEquality(float44u, float44u, different44u);
    CheckStorageHashMatchesEquality(float34, float34, different34);
    CheckStorageHashMatchesEquality(float44, float44, different44);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

