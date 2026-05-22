// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/common.h>

#include <tests/test_context.h>

#include <global/simdmath.h>
#include <global/compile.h>
#include <global/limit.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_math_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;


#define NWB_MATH_TEST_CHECK NWB_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool NearlyEqual(const f32 lhs, const f32 rhs, const f32 epsilon = 0.00001f){
    return Abs(lhs - rhs) <= epsilon;
}

static bool NearlyEqual3(SIMDVector value, const f32 x, const f32 y, const f32 z){
    return
        NearlyEqual(VectorGetX(value), x)
        && NearlyEqual(VectorGetY(value), y)
        && NearlyEqual(VectorGetZ(value), z)
    ;
}

static bool NearlyEqual4(SIMDVector value, const f32 x, const f32 y, const f32 z, const f32 w, const f32 epsilon = 0.00001f){
    return
        NearlyEqual(VectorGetX(value), x, epsilon)
        && NearlyEqual(VectorGetY(value), y, epsilon)
        && NearlyEqual(VectorGetZ(value), z, epsilon)
        && NearlyEqual(VectorGetW(value), w, epsilon)
    ;
}


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_MATH_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("math", [](NWB::Tests::TestContext& context){
    __hidden_math_tests::TestVector2CrossOrientation(context);
    __hidden_math_tests::TestVector3CrossOrientation(context);
    __hidden_math_tests::TestVector3RotateQuarterTurn(context);
    __hidden_math_tests::TestVector4CrossBasisOrientation(context);
    __hidden_math_tests::TestVectorNamedScalarFunctions(context);
    __hidden_math_tests::TestRefractCriticalAngle(context);
    __hidden_math_tests::TestHalfFloatScalarConversion(context);
    __hidden_math_tests::TestHalfFloatBufferConversion(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

