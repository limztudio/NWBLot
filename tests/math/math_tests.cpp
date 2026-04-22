// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/simdmath.h>
#include <global/compile.h>

#include <cmath>
#include <iostream>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_math_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TestContext{
    u32 passed = 0;
    u32 failed = 0;

    void checkTrue(const bool condition, const char* expression, const char* file, const int line){
        if(condition){
            ++passed;
            return;
        }

        ++failed;
        NWB_CERR << file << '(' << line << "): check failed: " << expression << '\n';
    }
};


#define NWB_MATH_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool NearlyEqual(const f32 lhs, const f32 rhs, const f32 epsilon = 0.00001f){
    return std::fabs(lhs - rhs) <= epsilon;
}

static bool NearlyEqual3(SIMDVector value, const f32 x, const f32 y, const f32 z){
    return NearlyEqual(VectorGetX(value), x)
        && NearlyEqual(VectorGetY(value), y)
        && NearlyEqual(VectorGetZ(value), z)
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

static void TestVector3RotateStillMatchesSimpleMath(TestContext& context){
    const AlignedFloat4Data rotation = SimpleMath::QuaternionRotationAxis(
        0.0f,
        0.0f,
        1.0f,
        SimpleMath::ConvertToRadians(90.0f)
    );
    const SIMDVector value = VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector rotated = Vector3Rotate(value, LoadFloat(rotation));
    const AlignedFloat3Data simpleRotated = SimpleMath::Vector3Rotate(Float3Data(1.0f, 0.0f, 0.0f), rotation);

    NWB_MATH_TEST_CHECK(context, NearlyEqual3(rotated, 0.0f, 1.0f, 0.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetX(rotated), simpleRotated.x));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetY(rotated), simpleRotated.y));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetZ(rotated), simpleRotated.z));
}

static void TestVector4CrossBasisOrientation(TestContext& context){
    const SIMDVector xAxis = VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector yAxis = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVector zAxis = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    const SIMDVector result = Vector4Cross(xAxis, yAxis, zAxis);

    NWB_MATH_TEST_CHECK(context, NearlyEqual3(result, 0.0f, 0.0f, 0.0f));
    NWB_MATH_TEST_CHECK(context, NearlyEqual(VectorGetW(result), 1.0f));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef NWB_MATH_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int EntryPoint(const isize argc, tchar** argv, void*){
    (void)argc;
    (void)argv;

    __hidden_math_tests::TestContext context;
    __hidden_math_tests::TestVector2CrossMatchesGlslOrder(context);
    __hidden_math_tests::TestVector3CrossMatchesGlslOrder(context);
    __hidden_math_tests::TestVector3RotateStillMatchesSimpleMath(context);
    __hidden_math_tests::TestVector4CrossBasisOrientation(context);

    if(context.failed != 0){
        NWB_CERR << "math tests failed: " << context.failed << " of " << (context.passed + context.failed) << '\n';
        return -1;
    }

    NWB_COUT << "math tests passed: " << context.passed << '\n';
    return 0;
}


#include <global/application_entry.h>

NWB_DEFINE_APPLICATION_ENTRY_POINT(EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
