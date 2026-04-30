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


static void TestVector2CrossMatchesGlslOrder(TestContext& context){
    const SIMDVector xAxis = VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector yAxis = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    const SIMDVector xy = Vector2Cross(xAxis, yAxis);
    const SIMDVector yx = Vector2Cross(yAxis, xAxis);

    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetX(xy), 1.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetY(xy), 1.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetX(yx), -1.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetY(yx), -1.0f));
}

static void TestVector3CrossMatchesGlslOrder(TestContext& context){
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

static void TestGlslNamedScalarFunctions(TestContext& context){
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

static void TestGlslRefractCriticalAngle(TestContext& context){
    const SIMDVector normal = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVector criticalIncident = VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector totalInternalIncident = VectorSet(0.866025404f, -0.5f, 0.0f, 0.0f);

    NWB_MATH_TEST_CHECK(context, NearlyEqual4(Vector2Refract(criticalIncident, normal, 1.0f), 1.0f, 0.0f, 0.0f, 0.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(Vector3Refract(criticalIncident, normal, 1.0f), 1.0f, 0.0f, 0.0f, 0.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(Vector4Refract(criticalIncident, normal, 1.0f), 1.0f, 0.0f, 0.0f, 0.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual4(Vector3Refract(totalInternalIncident, normal, 2.0f), 0.0f, 0.0f, 0.0f, 0.0f));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_MATH_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(const isize argc, tchar** argv, void*){
    static_cast<void>(argc);
    static_cast<void>(argv);

    return NWB::Tests::RunTestSuite("math", [](NWB::Tests::TestContext& context){
        __hidden_math_tests::TestVector2CrossMatchesGlslOrder(context);
        __hidden_math_tests::TestVector3CrossMatchesGlslOrder(context);
        __hidden_math_tests::TestVector3RotateQuarterTurn(context);
        __hidden_math_tests::TestVector4CrossBasisOrientation(context);
        __hidden_math_tests::TestGlslNamedScalarFunctions(context);
        __hidden_math_tests::TestGlslRefractCriticalAngle(context);
    });
}


#include <core/common/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

