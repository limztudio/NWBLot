// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>

#include <tests/test_context.h>

#include <global/simdmath.h>
#include <global/compile.h>
#include <global/limit.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using NWB::Tests::NearlyEqual;
using NWB::Tests::NearlyEqual3;
using NWB::Tests::NearlyEqual4;


#define NWB_MATH_TEST_CHECK NWB_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestVector2CrossOrientation(TestContext& context){
    const SIMDVector xAxis = VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector yAxis = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    const SIMDVector xy = Vector2Cross(xAxis, yAxis);
    const SIMDVector yx = Vector2Cross(yAxis, xAxis);

    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetX(xy), 1.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetY(xy), 1.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetX(yx), -1.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetY(yx), -1.0f));
}

static void TestVector3CrossOrientation(TestContext& context){
    const SIMDVector xAxis = VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector yAxis = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVector zAxis = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);

    NWB_MATH_TEST_CHECK(context, NearlyEqual3(Vector3Cross(xAxis, yAxis), 0.0f, 0.0f, 1.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual3(Vector3Cross(yAxis, xAxis), 0.0f, 0.0f, -1.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetX(Vector3Dot(Vector3Cross(xAxis, yAxis), zAxis)), 1.0f));

    const SIMDVector a = VectorSet(2.0f, -3.0f, 5.0f, 0.0f);
    const SIMDVector b = VectorSet(-7.0f, 11.0f, 13.0f, 0.0f);
    NWB_MATH_TEST_CHECK(context, NearlyEqual3(Vector3Cross(a, b), -94.0f, -61.0f, 1.0f));
}

static void TestVector3RotateQuarterTurn(TestContext& context){
    const SIMDVector rotation = QuaternionRotationAxis(VectorSet(0.0f, 0.0f, 1.0f, 0.0f), s_PI * 0.5f);
    const SIMDVector value = VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector rotated = Vector3Rotate(value, rotation);

    NWB_MATH_TEST_CHECK(context, NearlyEqual3(rotated, 0.0f, 1.0f, 0.0f));
}

static void TestVector4CrossBasisOrientation(TestContext& context){
    const SIMDVector xAxis = VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector yAxis = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVector zAxis = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    const SIMDVector result = Vector4Cross(xAxis, yAxis, zAxis);

    NWB_MATH_TEST_CHECK(context, NearlyEqual3(result, 0.0f, 0.0f, 0.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetW(result), 1.0f));
}

static void TestVectorNamedScalarFunctions(TestContext& context){
    const SIMDVector modResult = VectorMod(
        VectorSet(5.5f, -5.5f, 5.5f, -5.5f),
        VectorSet(2.0f, 2.0f, -2.0f, -2.0f)
    );
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(modResult, 1.5f, 0.5f, -0.5f, -1.5f));

    const SIMDVector expInput = VectorSet(1.0f, 2.0f, -1.0f, 0.5f);
    const SIMDVector expResult = VectorExp(expInput);
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(
        expResult,
        Exp(1.0f),
        Exp(2.0f),
        Exp(-1.0f),
        Exp(0.5f),
        0.0005f
    ));

    const f32 e = Exp(1.0f);
    const SIMDVector logResult = VectorLog(VectorSet(e, e * e, 1.0f, 0.25f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(
        logResult,
        1.0f,
        2.0f,
        0.0f,
        Log(0.25f),
        0.0005f
    ));

    const SIMDVector hyperbolicInput = VectorSet(0.5f, -0.5f, 1.0f, -1.0f);
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(
        VectorSinH(hyperbolicInput),
        SinH(0.5f),
        SinH(-0.5f),
        SinH(1.0f),
        SinH(-1.0f),
        0.0005f
    ));
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(
        VectorCosH(hyperbolicInput),
        CosH(0.5f),
        CosH(-0.5f),
        CosH(1.0f),
        CosH(-1.0f),
        0.0005f
    ));
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(
        VectorTanH(hyperbolicInput),
        TanH(0.5f),
        TanH(-0.5f),
        TanH(1.0f),
        TanH(-1.0f),
        0.0005f
    ));

    const f32 infinity = Limit<f32>::s_Infinity;
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(
        VectorTanH(VectorSet(50.0f, -50.0f, infinity, -infinity)),
        1.0f,
        -1.0f,
        1.0f,
        -1.0f
    ));

    const SIMDVector signedZeroTanH = VectorTanH(VectorSet(-0.0f, 0.0f, -0.0f, 0.0f));
    NWB_MATH_TEST_CHECK(context, SignBit(VectorGetX(signedZeroTanH)));
    NWB_MATH_TEST_CHECK(context, !SignBit(VectorGetY(signedZeroTanH)));
}

static void TestRefractCriticalAngle(TestContext& context){
    const SIMDVector normal = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVector criticalIncident = VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector totalInternalIncident = VectorSet(0.866025404f, -0.5f, 0.0f, 0.0f);

    NWB_MATH_TEST_CHECK(context, NearlyEqual4(Vector2Refract(criticalIncident, normal, 1.0f), 1.0f, 0.0f, 0.0f, 0.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(Vector3Refract(criticalIncident, normal, 1.0f), 1.0f, 0.0f, 0.0f, 0.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(Vector4Refract(criticalIncident, normal, 1.0f), 1.0f, 0.0f, 0.0f, 0.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(Vector3Refract(totalInternalIncident, normal, 2.0f), 0.0f, 0.0f, 0.0f, 0.0f));
}

static void TestHalfFloatScalarConversion(TestContext& context){
    NWB_MATH_TEST_CHECK(context, ConvertFloatToHalf(0.0f) == static_cast<Half>(0x0000u));
    NWB_MATH_TEST_CHECK(context, ConvertFloatToHalf(-0.0f) == static_cast<Half>(0x8000u));
    NWB_MATH_TEST_CHECK(context, ConvertFloatToHalf(1.0f) == static_cast<Half>(0x3c00u));
    NWB_MATH_TEST_CHECK(context, ConvertFloatToHalf(-2.0f) == static_cast<Half>(0xc000u));
    NWB_MATH_TEST_CHECK(context, ConvertFloatToHalf(65504.0f) == static_cast<Half>(0x7bffu));
    NWB_MATH_TEST_CHECK(context, ConvertFloatToHalf(Limit<f32>::s_Infinity) == static_cast<Half>(0x7c00u));
    NWB_MATH_TEST_CHECK(context, ConvertFloatToHalf(-Limit<f32>::s_Infinity) == static_cast<Half>(0xfc00u));

    const Half quietNaN = ConvertFloatToHalf(Limit<f32>::s_QuietNaN);
    NWB_MATH_TEST_CHECK(context, (quietNaN & static_cast<Half>(0x7c00u)) == static_cast<Half>(0x7c00u));
    NWB_MATH_TEST_CHECK(context, (quietNaN & static_cast<Half>(0x03ffu)) != static_cast<Half>(0u));

    NWB_MATH_TEST_CHECK(context, ConvertFloatToHalf(5.9604644775390625e-8f) == static_cast<Half>(0x0001u));
    NWB_MATH_TEST_CHECK(context, ConvertFloatToHalf(6.103515625e-5f) == static_cast<Half>(0x0400u));
    NWB_MATH_TEST_CHECK(context, ConvertFloatToHalf(1.00048828125f) == static_cast<Half>(0x3c00u));

    NWB_MATH_TEST_CHECK(context, NearlyEqual(ConvertHalfToFloat(static_cast<Half>(0x0000u)), 0.0f));
    NWB_MATH_TEST_CHECK(context, SignBit(ConvertHalfToFloat(static_cast<Half>(0x8000u))));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(ConvertHalfToFloat(static_cast<Half>(0x3c00u)), 1.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(ConvertHalfToFloat(static_cast<Half>(0xc000u)), -2.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(ConvertHalfToFloat(static_cast<Half>(0x7bffu)), 65504.0f));
    NWB_MATH_TEST_CHECK(context, ConvertHalfToFloat(static_cast<Half>(0x7c00u)) == Limit<f32>::s_Infinity);
    NWB_MATH_TEST_CHECK(context, ConvertHalfToFloat(static_cast<Half>(0xfc00u)) == -Limit<f32>::s_Infinity);
    NWB_MATH_TEST_CHECK(context, IsNaN(ConvertHalfToFloat(static_cast<Half>(0x7e00u))));
}

static void TestHalfFloatBufferConversion(TestContext& context){
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

    NWB_MATH_TEST_CHECK(context, ConvertFloatBufferToHalf(packed, source, 6u) == packed);
    NWB_MATH_TEST_CHECK(context, packed[0] == static_cast<Half>(0x0000u));
    NWB_MATH_TEST_CHECK(context, packed[1] == static_cast<Half>(0xbe00u));
    NWB_MATH_TEST_CHECK(context, packed[2] == static_cast<Half>(0x3c00u));
    NWB_MATH_TEST_CHECK(context, packed[3] == static_cast<Half>(0x7bffu));
    NWB_MATH_TEST_CHECK(context, packed[4] == static_cast<Half>(0x0001u));
    NWB_MATH_TEST_CHECK(context, packed[5] == static_cast<Half>(0x7c00u));

    NWB_MATH_TEST_CHECK(context, ConvertHalfBufferToFloat(unpacked, packed, 6u) == unpacked);
    NWB_MATH_TEST_CHECK(context, NearlyEqual(unpacked[0], 0.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(unpacked[1], -1.5f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(unpacked[2], 1.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(unpacked[3], 65504.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(unpacked[4], 5.9604644775390625e-8f));
    NWB_MATH_TEST_CHECK(context, unpacked[5] == Limit<f32>::s_Infinity);
}

template<typename Value>
static void CheckStorageHashMatchesEquality(TestContext& context, const Value& lhs, const Value& same, const Value& different){
    NWB_MATH_TEST_CHECK(context, lhs == same);
    NWB_MATH_TEST_CHECK(context, lhs != different);
    NWB_MATH_TEST_CHECK(context, Hasher<Value>{}(lhs) == Hasher<Value>{}(same));
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

static void TestMathStorageHashAndEquality(TestContext& context){
    NWB_MATH_TEST_CHECK(context, FloatHashBits(-0.0f) == FloatHashBits(0.0f));

    const Half h1 = static_cast<Half>(1u);
    const Half h2 = static_cast<Half>(2u);
    const Half h3 = static_cast<Half>(3u);
    const Half h4 = static_cast<Half>(4u);
    const Half h5 = static_cast<Half>(5u);
    CheckStorageHashMatchesEquality(context, Half2U(h1, h2), Half2U(h1, h2), Half2U(h1, h3));
    CheckStorageHashMatchesEquality(context, Half4U(h1, h2, h3, h4), Half4U(h1, h2, h3, h4), Half4U(h1, h2, h3, h5));
    CheckStorageHashMatchesEquality(context, Float4(-0.0f, 1.0f, 2.0f, 3.0f), Float4(0.0f, 1.0f, 2.0f, 3.0f), Float4(0.0f, 1.0f, 2.0f, 4.0f));
    CheckStorageHashMatchesEquality(context, MakeInt4Value(-1, 2, -3, 4), MakeInt4Value(-1, 2, -3, 4), MakeInt4Value(-1, 2, -3, 5));
    CheckStorageHashMatchesEquality(context, MakeUInt4Value(1u, 2u, 3u, 4u), MakeUInt4Value(1u, 2u, 3u, 4u), MakeUInt4Value(1u, 2u, 3u, 5u));
    CheckStorageHashMatchesEquality(context, Float2U(-0.0f, 1.0f), Float2U(0.0f, 1.0f), Float2U(0.0f, 2.0f));
    CheckStorageHashMatchesEquality(context, Float3U(-0.0f, 1.0f, 2.0f), Float3U(0.0f, 1.0f, 2.0f), Float3U(0.0f, 1.0f, 3.0f));
    CheckStorageHashMatchesEquality(context, Float4U(-0.0f, 1.0f, 2.0f, 3.0f), Float4U(0.0f, 1.0f, 2.0f, 3.0f), Float4U(0.0f, 1.0f, 2.0f, 4.0f));
    CheckStorageHashMatchesEquality(context, MakeInt2Value(-1, 2), MakeInt2Value(-1, 2), MakeInt2Value(-1, 3));
    CheckStorageHashMatchesEquality(context, MakeInt3Value(-1, 2, -3), MakeInt3Value(-1, 2, -3), MakeInt3Value(-1, 2, -4));
    CheckStorageHashMatchesEquality(context, MakeInt4UValue(-1, 2, -3, 4), MakeInt4UValue(-1, 2, -3, 4), MakeInt4UValue(-1, 2, -3, 5));
    CheckStorageHashMatchesEquality(context, MakeUInt2Value(1u, 2u), MakeUInt2Value(1u, 2u), MakeUInt2Value(1u, 3u));
    CheckStorageHashMatchesEquality(context, MakeUInt3Value(1u, 2u, 3u), MakeUInt3Value(1u, 2u, 3u), MakeUInt3Value(1u, 2u, 4u));
    CheckStorageHashMatchesEquality(context, MakeUInt4UValue(1u, 2u, 3u, 4u), MakeUInt4UValue(1u, 2u, 3u, 4u), MakeUInt4UValue(1u, 2u, 3u, 5u));

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

    CheckStorageHashMatchesEquality(context, float33, float33, different33);
    CheckStorageHashMatchesEquality(context, float34u, float34u, different34u);
    CheckStorageHashMatchesEquality(context, float44u, float44u, different44u);
    CheckStorageHashMatchesEquality(context, float34, float34, different34);
    CheckStorageHashMatchesEquality(context, float44, float44, different44);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_MATH_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("math", [](NWB::Tests::TestContext& context){
    __hidden_tests::TestVector2CrossOrientation(context);
    __hidden_tests::TestVector3CrossOrientation(context);
    __hidden_tests::TestVector3RotateQuarterTurn(context);
    __hidden_tests::TestVector4CrossBasisOrientation(context);
    __hidden_tests::TestVectorNamedScalarFunctions(context);
    __hidden_tests::TestRefractCriticalAngle(context);
    __hidden_tests::TestHalfFloatScalarConversion(context);
    __hidden_tests::TestHalfFloatBufferConversion(context);
    __hidden_tests::TestMathStorageHashAndEquality(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

