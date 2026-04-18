// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/compile.h>
#include <global/math.h>
#include <global/matrix_math.h>
#include <global/sh_math.h>

#include <cmath>
#include <cstring>
#include <limits>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_math_tests{


static constexpr f32 s_FloatEpsilon = 1.0e-4f;
static constexpr f32 s_FloatLooseEpsilon = 2.5e-3f;
static constexpr f64 s_DoubleEpsilon = 1.0e-9;


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

    void checkNearFloat(const f32 lhs, const f32 rhs, const f32 epsilon, const char* expression, const char* file, const int line){
        const f32 delta = static_cast<f32>(std::fabs(lhs - rhs));
        const f32 scale = Max(Max(static_cast<f32>(1.0f), static_cast<f32>(std::fabs(lhs))), static_cast<f32>(std::fabs(rhs)));
        checkTrue(delta <= epsilon * scale, expression, file, line);
    }

    void checkNearDouble(const f64 lhs, const f64 rhs, const f64 epsilon, const char* expression, const char* file, const int line){
        const f64 delta = std::fabs(lhs - rhs);
        const f64 scale = Max(Max(static_cast<f64>(1.0), std::fabs(lhs)), std::fabs(rhs));
        checkTrue(delta <= epsilon * scale, expression, file, line);
    }
};


struct FloatRows4x4{
    f32 m[4][4];
};

struct DoubleColumns4x4{
    f64 v[16];
};

struct DoubleRows4x4{
    f64 m[4][4];
};


[[nodiscard]] static SimdVector MakeVector(const f32 x, const f32 y, const f32 z, const f32 w)noexcept{
    return SourceMath::VectorSet(x, y, z, w);
}

[[nodiscard]] static DoubleSimdVector MakeDoubleVector(const f64 x, const f64 y, const f64 z, const f64 w)noexcept{
    return DoubleSimdVectorSet(x, y, z, w);
}

[[nodiscard]] static SourceMath::Float4 StoreVector(const SimdVector value)noexcept{
    SourceMath::Float4 result{};
    SourceMath::StoreFloat4(&result, value);
    return result;
}

[[nodiscard]] static Double4Data StoreDoubleVector(const DoubleSimdVector& value)noexcept{
    Double4Data result{};
    StoreDouble4Data(result, value);
    return result;
}

struct RawUInt4{
    u32 v[4] = {};
};

[[nodiscard]] static RawUInt4 StoreRawVector(const SimdVector value)noexcept{
    RawUInt4 result{};
    SourceMath::StoreInt4(result.v, value);
    return result;
}

[[nodiscard]] static i32 AsSInt(const u32 value)noexcept{
    i32 result = 0;
    NWB_MEMCPY(&result, sizeof(result), &value, sizeof(value));
    return result;
}

[[nodiscard]] static FloatRows4x4 StoreMatrixRows(MatrixArg matrix)noexcept{
    FloatRows4x4 result{};
    Float4Data column0{};
    Float4Data column1{};
    Float4Data column2{};
    Float4Data column3{};
    SourceMath::StoreFloat4(&column0, matrix[0]);
    SourceMath::StoreFloat4(&column1, matrix[1]);
    SourceMath::StoreFloat4(&column2, matrix[2]);
    SourceMath::StoreFloat4(&column3, matrix[3]);

    result.m[0][0] = column0.x;
    result.m[0][1] = column1.x;
    result.m[0][2] = column2.x;
    result.m[0][3] = column3.x;

    result.m[1][0] = column0.y;
    result.m[1][1] = column1.y;
    result.m[1][2] = column2.y;
    result.m[1][3] = column3.y;

    result.m[2][0] = column0.z;
    result.m[2][1] = column1.z;
    result.m[2][2] = column2.z;
    result.m[2][3] = column3.z;

    result.m[3][0] = column0.w;
    result.m[3][1] = column1.w;
    result.m[3][2] = column2.w;
    result.m[3][3] = column3.w;

    return result;
}

[[nodiscard]] static DoubleRows4x4 StoreDoubleMatrixRows(DoubleMatrixArg matrix)noexcept{
    DoubleRows4x4 result{};
    Double4Data column0{};
    Double4Data column1{};
    Double4Data column2{};
    Double4Data column3{};
    StoreDouble4Data(column0, matrix[0]);
    StoreDouble4Data(column1, matrix[1]);
    StoreDouble4Data(column2, matrix[2]);
    StoreDouble4Data(column3, matrix[3]);

    result.m[0][0] = column0.x;
    result.m[0][1] = column1.x;
    result.m[0][2] = column2.x;
    result.m[0][3] = column3.x;

    result.m[1][0] = column0.y;
    result.m[1][1] = column1.y;
    result.m[1][2] = column2.y;
    result.m[1][3] = column3.y;

    result.m[2][0] = column0.z;
    result.m[2][1] = column1.z;
    result.m[2][2] = column2.z;
    result.m[2][3] = column3.z;

    result.m[3][0] = column0.w;
    result.m[3][1] = column1.w;
    result.m[3][2] = column2.w;
    result.m[3][3] = column3.w;

    return result;
}

[[nodiscard]] static FloatRows4x4 StoreNativeMatrixRows(const Float4x4& matrix)noexcept{
    f32 columns[16]{};
    StoreFloat4x4(MakeNotNull(columns), matrix);

    FloatRows4x4 result{};
    for(usize column = 0; column < 4; ++column){
        for(usize row = 0; row < 4; ++row)
            result.m[row][column] = columns[column * 4 + row];
    }
    return result;
}

[[nodiscard]] static DoubleColumns4x4 StoreNativeMatrixColumns(const Double4x4& matrix)noexcept{
    DoubleColumns4x4 result{};
    StoreDouble4x4(MakeNotNull(result.v), matrix);
    return result;
}

[[nodiscard]] static Float4 LoadNativeFloat4(const f32 x, const f32 y, const f32 z, const f32 w)noexcept{
    const f32 values[4] = { x, y, z, w };
    return LoadFloat4(MakeNotNull(values));
}

[[nodiscard]] static Double4 LoadNativeDouble4(const f64 x, const f64 y, const f64 z, const f64 w)noexcept{
    const f64 values[4] = { x, y, z, w };
    return LoadDouble4(MakeNotNull(values));
}

[[nodiscard]] static FloatRows4x4 MultiplyRows(const FloatRows4x4& lhs, const FloatRows4x4& rhs)noexcept{
    FloatRows4x4 result{};
    for(usize row = 0; row < 4; ++row){
        for(usize column = 0; column < 4; ++column){
            f32 value = 0.0f;
            for(usize i = 0; i < 4; ++i)
                value += lhs.m[row][i] * rhs.m[i][column];

            result.m[row][column] = value;
        }
    }
    return result;
}

[[nodiscard]] static DoubleRows4x4 MultiplyDoubleRows(const DoubleRows4x4& lhs, const DoubleRows4x4& rhs)noexcept{
    DoubleRows4x4 result{};
    for(usize row = 0; row < 4; ++row){
        for(usize column = 0; column < 4; ++column){
            f64 value = 0.0;
            for(usize i = 0; i < 4; ++i)
                value += lhs.m[row][i] * rhs.m[i][column];

            result.m[row][column] = value;
        }
    }
    return result;
}

[[nodiscard]] static FloatRows4x4 TransposeRows(const FloatRows4x4& value)noexcept{
    FloatRows4x4 result{};
    for(usize row = 0; row < 4; ++row){
        for(usize column = 0; column < 4; ++column)
            result.m[row][column] = value.m[column][row];
    }
    return result;
}

[[nodiscard]] static DoubleRows4x4 TransposeDoubleRows(const DoubleRows4x4& value)noexcept{
    DoubleRows4x4 result{};
    for(usize row = 0; row < 4; ++row){
        for(usize column = 0; column < 4; ++column)
            result.m[row][column] = value.m[column][row];
    }
    return result;
}

[[nodiscard]] static SourceMath::Float4 TransformRows(const FloatRows4x4& matrix, const f32 x, const f32 y, const f32 z, const f32 w)noexcept{
    SourceMath::Float4 result{};
    result.x = matrix.m[0][0] * x + matrix.m[0][1] * y + matrix.m[0][2] * z + matrix.m[0][3] * w;
    result.y = matrix.m[1][0] * x + matrix.m[1][1] * y + matrix.m[1][2] * z + matrix.m[1][3] * w;
    result.z = matrix.m[2][0] * x + matrix.m[2][1] * y + matrix.m[2][2] * z + matrix.m[2][3] * w;
    result.w = matrix.m[3][0] * x + matrix.m[3][1] * y + matrix.m[3][2] * z + matrix.m[3][3] * w;
    return result;
}

[[nodiscard]] static Double4Data TransformDoubleRows(
    const DoubleRows4x4& matrix,
    const f64 x,
    const f64 y,
    const f64 z,
    const f64 w
)noexcept{
    Double4Data result{};
    result.x = matrix.m[0][0] * x + matrix.m[0][1] * y + matrix.m[0][2] * z + matrix.m[0][3] * w;
    result.y = matrix.m[1][0] * x + matrix.m[1][1] * y + matrix.m[1][2] * z + matrix.m[1][3] * w;
    result.z = matrix.m[2][0] * x + matrix.m[2][1] * y + matrix.m[2][2] * z + matrix.m[2][3] * w;
    result.w = matrix.m[3][0] * x + matrix.m[3][1] * y + matrix.m[3][2] * z + matrix.m[3][3] * w;
    return result;
}

static void CheckFloat4Near(
    TestContext& context,
    const SourceMath::Float4& value,
    const f32 x,
    const f32 y,
    const f32 z,
    const f32 w,
    const f32 epsilon,
    const char* file,
    const int line
){
    context.checkNearFloat(value.x, x, epsilon, "x", file, line);
    context.checkNearFloat(value.y, y, epsilon, "y", file, line);
    context.checkNearFloat(value.z, z, epsilon, "z", file, line);
    context.checkNearFloat(value.w, w, epsilon, "w", file, line);
}

static void CheckDouble4Near(
    TestContext& context,
    const Double4Data& value,
    const f64 x,
    const f64 y,
    const f64 z,
    const f64 w,
    const f64 epsilon,
    const char* file,
    const int line
){
    context.checkNearDouble(value.x, x, epsilon, "x", file, line);
    context.checkNearDouble(value.y, y, epsilon, "y", file, line);
    context.checkNearDouble(value.z, z, epsilon, "z", file, line);
    context.checkNearDouble(value.w, w, epsilon, "w", file, line);
}

static void CheckDouble2Near(
    TestContext& context,
    const Double2Data& value,
    const f64 x,
    const f64 y,
    const f64 epsilon,
    const char* file,
    const int line
){
    context.checkNearDouble(value.x, x, epsilon, "x", file, line);
    context.checkNearDouble(value.y, y, epsilon, "y", file, line);
}

static void CheckDouble3Near(
    TestContext& context,
    const Double3Data& value,
    const f64 x,
    const f64 y,
    const f64 z,
    const f64 epsilon,
    const char* file,
    const int line
){
    context.checkNearDouble(value.x, x, epsilon, "x", file, line);
    context.checkNearDouble(value.y, y, epsilon, "y", file, line);
    context.checkNearDouble(value.z, z, epsilon, "z", file, line);
}

static void CheckVectorNear(
    TestContext& context,
    const SimdVector value,
    const f32 x,
    const f32 y,
    const f32 z,
    const f32 w,
    const f32 epsilon,
    const char* file,
    const int line
){
    CheckFloat4Near(context, StoreVector(value), x, y, z, w, epsilon, file, line);
}

static void CheckDoubleVectorNear(
    TestContext& context,
    const DoubleSimdVector& value,
    const f64 x,
    const f64 y,
    const f64 z,
    const f64 w,
    const f64 epsilon,
    const char* file,
    const int line
){
    CheckDouble4Near(context, StoreDoubleVector(value), x, y, z, w, epsilon, file, line);
}

static void CheckRawUInt4Equal(
    TestContext& context,
    const RawUInt4& value,
    const u32 x,
    const u32 y,
    const u32 z,
    const u32 w,
    const char* file,
    const int line
){
    context.checkTrue(value.v[0] == x, "raw u32 x", file, line);
    context.checkTrue(value.v[1] == y, "raw u32 y", file, line);
    context.checkTrue(value.v[2] == z, "raw u32 z", file, line);
    context.checkTrue(value.v[3] == w, "raw u32 w", file, line);
}

static void CheckRawSInt4Equal(
    TestContext& context,
    const RawUInt4& value,
    const i32 x,
    const i32 y,
    const i32 z,
    const i32 w,
    const char* file,
    const int line
){
    context.checkTrue(AsSInt(value.v[0]) == x, "raw i32 x", file, line);
    context.checkTrue(AsSInt(value.v[1]) == y, "raw i32 y", file, line);
    context.checkTrue(AsSInt(value.v[2]) == z, "raw i32 z", file, line);
    context.checkTrue(AsSInt(value.v[3]) == w, "raw i32 w", file, line);
}

static void CheckVectorFinite(TestContext& context, const SimdVector value, const char* expression, const char* file, const int line){
    const SourceMath::Float4 stored = StoreVector(value);
    context.checkTrue(
        std::isfinite(stored.x) &&
        std::isfinite(stored.y) &&
        std::isfinite(stored.z) &&
        std::isfinite(stored.w),
        expression,
        file,
        line
    );
}

static void CheckMatrixRowsNear(
    TestContext& context,
    const FloatRows4x4& value,
    const FloatRows4x4& expected,
    const f32 epsilon,
    const char* file,
    const int line
){
    for(usize row = 0; row < 4; ++row){
        for(usize column = 0; column < 4; ++column)
            context.checkNearFloat(value.m[row][column], expected.m[row][column], epsilon, "matrix", file, line);
    }
}

static void CheckMatrix3x4Near(
    TestContext& context,
    const Float3x4Data& value,
    const Float3x4Data& expected,
    const f32 epsilon,
    const char* file,
    const int line
){
    for(usize row = 0; row < 3; ++row){
        for(usize column = 0; column < 4; ++column)
            context.checkNearFloat(value.m[row][column], expected.m[row][column], epsilon, "matrix3x4", file, line);
    }
}

static void CheckDoubleMatrix3x4Near(
    TestContext& context,
    const Double3x4Data& value,
    const Double3x4Data& expected,
    const f64 epsilon,
    const char* file,
    const int line
){
    for(usize row = 0; row < 3; ++row){
        for(usize column = 0; column < 4; ++column)
            context.checkNearDouble(value.m[row][column], expected.m[row][column], epsilon, "double matrix3x4", file, line);
    }
}

static void CheckDoubleMatrixRowsNear(
    TestContext& context,
    const DoubleRows4x4& value,
    const DoubleRows4x4& expected,
    const f64 epsilon,
    const char* file,
    const int line
){
    for(usize row = 0; row < 4; ++row){
        for(usize column = 0; column < 4; ++column)
            context.checkNearDouble(value.m[row][column], expected.m[row][column], epsilon, "double matrix", file, line);
    }
}

static void CheckQuaternionEquivalent(
    TestContext& context,
    const SimdVector lhs,
    const SimdVector rhs,
    const f32 epsilon,
    const char* file,
    const int line
){
    const SourceMath::Float4 lhsStored = StoreVector(lhs);
    const SourceMath::Float4 rhsStored = StoreVector(rhs);

    const f32 directDelta =
        std::fabs(lhsStored.x - rhsStored.x) +
        std::fabs(lhsStored.y - rhsStored.y) +
        std::fabs(lhsStored.z - rhsStored.z) +
        std::fabs(lhsStored.w - rhsStored.w);
    const f32 negatedDelta =
        std::fabs(lhsStored.x + rhsStored.x) +
        std::fabs(lhsStored.y + rhsStored.y) +
        std::fabs(lhsStored.z + rhsStored.z) +
        std::fabs(lhsStored.w + rhsStored.w);

    context.checkTrue(Min(directDelta, negatedDelta) <= epsilon * 4.0f, "quaternion equivalent", file, line);
}

static void CheckDoubleQuaternionEquivalent(
    TestContext& context,
    const DoubleSimdVector& lhs,
    const DoubleSimdVector& rhs,
    const f64 epsilon,
    const char* file,
    const int line
){
    const Double4Data lhsStored = StoreDoubleVector(lhs);
    const Double4Data rhsStored = StoreDoubleVector(rhs);

    const f64 directDelta =
        std::fabs(lhsStored.x - rhsStored.x) +
        std::fabs(lhsStored.y - rhsStored.y) +
        std::fabs(lhsStored.z - rhsStored.z) +
        std::fabs(lhsStored.w - rhsStored.w);
    const f64 negatedDelta =
        std::fabs(lhsStored.x + rhsStored.x) +
        std::fabs(lhsStored.y + rhsStored.y) +
        std::fabs(lhsStored.z + rhsStored.z) +
        std::fabs(lhsStored.w + rhsStored.w);

    context.checkTrue(Min(directDelta, negatedDelta) <= epsilon * 4.0, "double quaternion equivalent", file, line);
}

static void FillSequence(f32* values, const usize count, const f32 scale, const f32 bias){
    for(usize i = 0; i < count; ++i)
        values[i] = bias + scale * static_cast<f32>(i + 1);
}

template<typename T>
[[nodiscard]] static NotNull<const T*> MakeConstNotNull(const T* ptr)noexcept{
    return NotNull<const T*>(ptr);
}

[[nodiscard]] static SimdMatrix Compose(MatrixArg lhs, MatrixArg rhs)noexcept{
    return MatrixMultiply(lhs, rhs);
}

[[nodiscard]] static DoubleSimdMatrix ComposeDouble(DoubleMatrixArg lhs, DoubleMatrixArg rhs)noexcept{
    return DoubleSimdMatrixMultiply(lhs, rhs);
}

static void TestNativeFloatMath(TestContext& context){
    const f32 rawVector[4] = { 1.0f, -2.0f, 3.0f, -4.0f };
    const Float4 vector = LoadFloat4(MakeNotNull(rawVector));
    f32 storedVector[4]{};
    StoreFloat4(MakeNotNull(storedVector), vector);
    context.checkNearFloat(storedVector[0], rawVector[0], s_FloatEpsilon, "StoreFloat4 x", __FILE__, __LINE__);
    context.checkNearFloat(storedVector[1], rawVector[1], s_FloatEpsilon, "StoreFloat4 y", __FILE__, __LINE__);
    context.checkNearFloat(storedVector[2], rawVector[2], s_FloatEpsilon, "StoreFloat4 z", __FILE__, __LINE__);
    context.checkNearFloat(storedVector[3], rawVector[3], s_FloatEpsilon, "StoreFloat4 w", __FILE__, __LINE__);

    context.checkNearFloat(GetX(vector), 1.0f, s_FloatEpsilon, "GetX", __FILE__, __LINE__);
    context.checkNearFloat(GetY(vector), -2.0f, s_FloatEpsilon, "GetY", __FILE__, __LINE__);
    context.checkNearFloat(GetZ(vector), 3.0f, s_FloatEpsilon, "GetZ", __FILE__, __LINE__);
    context.checkNearFloat(GetW(vector), -4.0f, s_FloatEpsilon, "GetW", __FILE__, __LINE__);

    const Float4 other = LoadNativeFloat4(-5.0f, 6.0f, -7.0f, 8.0f);
    CheckFloat4Near(context, Float4Data(GetX(vector + other), GetY(vector + other), GetZ(vector + other), GetW(vector + other)), -4.0f, 4.0f, -4.0f, 4.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, Float4Data(GetX(vector - other), GetY(vector - other), GetZ(vector - other), GetW(vector - other)), 6.0f, -8.0f, 10.0f, -12.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, Float4Data(GetX(vector * other), GetY(vector * other), GetZ(vector * other), GetW(vector * other)), -5.0f, -12.0f, -21.0f, -32.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, Float4Data(GetX(other / 2.0f), GetY(other / 2.0f), GetZ(other / 2.0f), GetW(other / 2.0f)), -2.5f, 3.0f, -3.5f, 4.0f, s_FloatEpsilon, __FILE__, __LINE__);

    context.checkNearFloat(Dot3(vector, other), -38.0f, s_FloatEpsilon, "Dot3", __FILE__, __LINE__);
    context.checkNearFloat(Dot4(vector, other), -70.0f, s_FloatEpsilon, "Dot4", __FILE__, __LINE__);

    const Float4 cross = Cross3(vector, other);
    CheckFloat4Near(context, Float4Data(GetX(cross), GetY(cross), GetZ(cross), GetW(cross)), -4.0f, -8.0f, -4.0f, 0.0f, s_FloatEpsilon, __FILE__, __LINE__);

    context.checkNearFloat(Length3(vector), std::sqrt(14.0f), s_FloatEpsilon, "Length3", __FILE__, __LINE__);
    const Float4 normalized = Normalize3(LoadNativeFloat4(0.0f, 3.0f, 4.0f, 7.0f));
    CheckFloat4Near(context, Float4Data(GetX(normalized), GetY(normalized), GetZ(normalized), GetW(normalized)), 0.0f, 0.6f, 0.8f, 7.0f, s_FloatEpsilon, __FILE__, __LINE__);

    const f32 halfPi = 1.57079632679f;
    const Float4 nativeRx = TransformDirection(Float4x4::RotationX(halfPi), LoadNativeFloat4(0.0f, 1.0f, 0.0f, 0.0f));
    const Float4 nativeRy = TransformDirection(Float4x4::RotationY(halfPi), LoadNativeFloat4(0.0f, 0.0f, 1.0f, 0.0f));
    const Float4 nativeRz = TransformDirection(Float4x4::RotationZ(halfPi), LoadNativeFloat4(1.0f, 0.0f, 0.0f, 0.0f));
    CheckFloat4Near(context, Float4Data(GetX(nativeRx), GetY(nativeRx), GetZ(nativeRx), GetW(nativeRx)), 0.0f, 0.0f, 1.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, Float4Data(GetX(nativeRy), GetY(nativeRy), GetZ(nativeRy), GetW(nativeRy)), 1.0f, 0.0f, 0.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, Float4Data(GetX(nativeRz), GetY(nativeRz), GetZ(nativeRz), GetW(nativeRz)), 0.0f, 1.0f, 0.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const Float4x4 composed = Float4x4::Translation(4.0f, -5.0f, 6.0f) * Float4x4::RotationZ(0.5f) * Float4x4::Scale(2.0f, 3.0f, 4.0f);
    const Float4 transformedPoint = TransformPoint(composed, LoadNativeFloat4(1.0f, 2.0f, 3.0f, 1.0f));
    const FloatRows4x4 rows = StoreNativeMatrixRows(composed);
    const Float4Data expectedPoint = TransformRows(rows, 1.0f, 2.0f, 3.0f, 1.0f);
    CheckFloat4Near(context, Float4Data(GetX(transformedPoint), GetY(transformedPoint), GetZ(transformedPoint), GetW(transformedPoint)), expectedPoint.x, expectedPoint.y, expectedPoint.z, expectedPoint.w, s_FloatEpsilon, __FILE__, __LINE__);

    const Float4 transformedDirection = TransformDirection(composed, LoadNativeFloat4(1.0f, 2.0f, 3.0f, 9.0f));
    const Float4Data expectedDirection = TransformRows(rows, 1.0f, 2.0f, 3.0f, 0.0f);
    CheckFloat4Near(context, Float4Data(GetX(transformedDirection), GetY(transformedDirection), GetZ(transformedDirection), GetW(transformedDirection)), expectedDirection.x, expectedDirection.y, expectedDirection.z, expectedDirection.w, s_FloatEpsilon, __FILE__, __LINE__);

    const Float4x4 transposed = Transpose(composed);
    const FloatRows4x4 transposedRows = StoreNativeMatrixRows(transposed);
    CheckMatrixRowsNear(context, transposedRows, TransposeRows(rows), s_FloatEpsilon, __FILE__, __LINE__);

    const SimdMatrix simdComposed = Compose(Compose(MatrixTranslation(4.0f, -5.0f, 6.0f), MatrixRotationZ(0.5f)), MatrixScaling(2.0f, 3.0f, 4.0f));
    CheckMatrixRowsNear(context, rows, StoreMatrixRows(simdComposed), s_FloatLooseEpsilon, __FILE__, __LINE__);

    const Float4x4 nativeOrderA = Float4x4::Translation(3.0f, 4.0f, 5.0f) * Float4x4::RotationY(0.25f);
    const Float4x4 nativeOrderB = Float4x4::RotationY(0.25f) * Float4x4::Translation(3.0f, 4.0f, 5.0f);
    const Float4 nativeOrderPointA = TransformPoint(nativeOrderA, LoadNativeFloat4(1.0f, 0.0f, 0.0f, 1.0f));
    const Float4 nativeOrderPointB = TransformPoint(nativeOrderB, LoadNativeFloat4(1.0f, 0.0f, 0.0f, 1.0f));
    const Float4Data orderPointA = Float4Data(GetX(nativeOrderPointA), GetY(nativeOrderPointA), GetZ(nativeOrderPointA), GetW(nativeOrderPointA));
    const Float4Data orderPointB = Float4Data(GetX(nativeOrderPointB), GetY(nativeOrderPointB), GetZ(nativeOrderPointB), GetW(nativeOrderPointB));
    context.checkTrue(
        std::fabs(orderPointA.x - orderPointB.x) > 1.0e-3f ||
        std::fabs(orderPointA.y - orderPointB.y) > 1.0e-3f ||
        std::fabs(orderPointA.z - orderPointB.z) > 1.0e-3f,
        "native matrix order is non-commutative",
        __FILE__,
        __LINE__
    );

    const f32 rawColumns[16] = {
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    };
    const Float4x4 loadedMatrix = LoadFloat4x4(MakeNotNull(rawColumns));
    f32 roundtripColumns[16]{};
    StoreFloat4x4(MakeNotNull(roundtripColumns), loadedMatrix);
    for(usize i = 0; i < 16; ++i)
        context.checkNearFloat(roundtripColumns[i], rawColumns[i], s_FloatEpsilon, "StoreFloat4x4", __FILE__, __LINE__);

    const SimdMatrix rawCtorMatrix = SimdMatrix(SourceMath::Matrix(rawColumns));
    const FloatRows4x4 rawCtorRows = StoreMatrixRows(rawCtorMatrix);
    context.checkNearFloat(rawCtorRows.m[0][0], 1.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r00", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[0][1], 5.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r01", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[0][2], 9.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r02", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[0][3], 13.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r03", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[1][0], 2.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r10", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[1][1], 6.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r11", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[1][2], 10.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r12", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[1][3], 14.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r13", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[2][0], 3.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r20", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[2][1], 7.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r21", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[2][2], 11.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r22", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[2][3], 15.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r23", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[3][0], 4.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r30", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[3][1], 8.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r31", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[3][2], 12.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r32", __FILE__, __LINE__);
    context.checkNearFloat(rawCtorRows.m[3][3], 16.0f, s_FloatEpsilon, "SourceMath::Matrix raw column ctor r33", __FILE__, __LINE__);
}

static void TestNativeDoubleMath(TestContext& context){
    const Double4 lhs = LoadNativeDouble4(1.0, 2.0, 3.0, 4.0);
    const Double4 rhs = LoadNativeDouble4(-2.0, 5.0, -6.0, 8.0);
    f64 storedLhs[4]{};
    StoreDouble4(MakeNotNull(storedLhs), lhs);
    context.checkNearDouble(storedLhs[0], 1.0, s_DoubleEpsilon, "StoreDouble4 x", __FILE__, __LINE__);
    context.checkNearDouble(storedLhs[1], 2.0, s_DoubleEpsilon, "StoreDouble4 y", __FILE__, __LINE__);
    context.checkNearDouble(storedLhs[2], 3.0, s_DoubleEpsilon, "StoreDouble4 z", __FILE__, __LINE__);
    context.checkNearDouble(storedLhs[3], 4.0, s_DoubleEpsilon, "StoreDouble4 w", __FILE__, __LINE__);

    context.checkNearDouble(Dot3(lhs, rhs), -10.0, s_DoubleEpsilon, "Dot3(double)", __FILE__, __LINE__);
    context.checkNearDouble(Dot4(lhs, rhs), 22.0, s_DoubleEpsilon, "Dot4(double)", __FILE__, __LINE__);

    const Double4 normalized = Normalize3(LoadNativeDouble4(2.0, 0.0, 0.0, 9.0));
    context.checkNearDouble(GetX(normalized), 1.0, s_DoubleEpsilon, "Normalize3 x", __FILE__, __LINE__);
    context.checkNearDouble(GetY(normalized), 0.0, s_DoubleEpsilon, "Normalize3 y", __FILE__, __LINE__);
    context.checkNearDouble(GetZ(normalized), 0.0, s_DoubleEpsilon, "Normalize3 z", __FILE__, __LINE__);
    context.checkNearDouble(GetW(normalized), 9.0, s_DoubleEpsilon, "Normalize3 w", __FILE__, __LINE__);

    const f64 halfPi = 1.57079632679489661923;
    const Double4 nativeRx = TransformDirection(Double4x4::RotationX(halfPi), LoadNativeDouble4(0.0, 1.0, 0.0, 0.0));
    const Double4 nativeRy = TransformDirection(Double4x4::RotationY(halfPi), LoadNativeDouble4(0.0, 0.0, 1.0, 0.0));
    const Double4 nativeRz = TransformDirection(Double4x4::RotationZ(halfPi), LoadNativeDouble4(1.0, 0.0, 0.0, 0.0));
    context.checkNearDouble(GetX(nativeRx), 0.0, s_DoubleEpsilon, "native rx x", __FILE__, __LINE__);
    context.checkNearDouble(GetY(nativeRx), 0.0, s_DoubleEpsilon, "native rx y", __FILE__, __LINE__);
    context.checkNearDouble(GetZ(nativeRx), 1.0, s_DoubleEpsilon, "native rx z", __FILE__, __LINE__);
    context.checkNearDouble(GetX(nativeRy), 1.0, s_DoubleEpsilon, "native ry x", __FILE__, __LINE__);
    context.checkNearDouble(GetY(nativeRy), 0.0, s_DoubleEpsilon, "native ry y", __FILE__, __LINE__);
    context.checkNearDouble(GetZ(nativeRy), 0.0, s_DoubleEpsilon, "native ry z", __FILE__, __LINE__);
    context.checkNearDouble(GetX(nativeRz), 0.0, s_DoubleEpsilon, "native rz x", __FILE__, __LINE__);
    context.checkNearDouble(GetY(nativeRz), 1.0, s_DoubleEpsilon, "native rz y", __FILE__, __LINE__);
    context.checkNearDouble(GetZ(nativeRz), 0.0, s_DoubleEpsilon, "native rz z", __FILE__, __LINE__);

    const Double4x4 matrix = Double4x4::Translation(1.0, -2.0, 3.0) * Double4x4::Scale(2.0, 3.0, 4.0);
    const Double4 transformed = TransformPoint(matrix, LoadNativeDouble4(2.0, 3.0, 4.0, 1.0));
    context.checkNearDouble(GetX(transformed), 5.0, s_DoubleEpsilon, "TransformPoint x", __FILE__, __LINE__);
    context.checkNearDouble(GetY(transformed), 7.0, s_DoubleEpsilon, "TransformPoint y", __FILE__, __LINE__);
    context.checkNearDouble(GetZ(transformed), 19.0, s_DoubleEpsilon, "TransformPoint z", __FILE__, __LINE__);
    context.checkNearDouble(GetW(transformed), 1.0, s_DoubleEpsilon, "TransformPoint w", __FILE__, __LINE__);

    const DoubleColumns4x4 columns = StoreNativeMatrixColumns(matrix);
    const Double4x4 roundtrip = LoadDouble4x4(MakeNotNull(columns.v));
    const DoubleColumns4x4 storedAgain = StoreNativeMatrixColumns(roundtrip);
    for(usize i = 0; i < 16; ++i)
        context.checkNearDouble(storedAgain.v[i], columns.v[i], s_DoubleEpsilon, "StoreDouble4x4", __FILE__, __LINE__);
}

static void TestSourceMathDoubleSimd(TestContext& context){
    const DoubleSimdVector lhs = MakeDoubleVector(1.0, 2.0, 3.0, 4.0);
    const DoubleSimdVector rhs = MakeDoubleVector(-2.0, 5.0, -6.0, 8.0);
    CheckDoubleVectorNear(context, lhs, 1.0, 2.0, 3.0, 4.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleVectorNear(context, DoubleSimdVectorAdd(lhs, rhs), -1.0, 7.0, -3.0, 12.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleVectorNear(
        context,
        DoubleSimdVectorSubtract(lhs, rhs),
        3.0,
        -3.0,
        9.0,
        -4.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVectorMultiply(lhs, rhs),
        -2.0,
        10.0,
        -18.0,
        32.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVectorDivide(rhs, MakeDoubleVector(2.0, 5.0, -3.0, 4.0)),
        -1.0,
        1.0,
        2.0,
        2.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVectorMultiplyAdd(lhs, rhs, DoubleSimdVectorReplicate(1.0)),
        -1.0,
        11.0,
        -17.0,
        33.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(context, DoubleSimdVectorNegate(lhs), -1.0, -2.0, -3.0, -4.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleVectorNear(context, DoubleSimdVectorScale(lhs, 0.5), 0.5, 1.0, 1.5, 2.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleVectorNear(
        context,
        DoubleSimdVectorSqrt(MakeDoubleVector(1.0, 4.0, 9.0, 16.0)),
        1.0,
        2.0,
        3.0,
        4.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVectorAbs(MakeDoubleVector(-1.0, 2.0, -3.0, 4.0)),
        1.0,
        2.0,
        3.0,
        4.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    const f64 scalar = 12.5;
    f64 scalarOut = 0.0;
    StoreDoubleData(scalarOut, LoadDoubleData(scalar));
    context.checkNearDouble(scalarOut, scalar, s_DoubleEpsilon, "LoadDoubleData/StoreDoubleData", __FILE__, __LINE__);
    const Double2Data double2Input(9.0, -10.0);
    const Double3Data double3Input(11.0, -12.0, 13.0);
    const Double4Data double4Input(14.0, -15.0, 16.0, -17.0);
    const AlignedDouble2Data alignedDouble2Input(18.0, -19.0);
    const AlignedDouble3Data alignedDouble3Input(20.0, -21.0, 22.0);
    const AlignedDouble4Data alignedDouble4Input(23.0, -24.0, 25.0, -26.0);
    CheckDoubleVectorNear(context, LoadDouble2Data(double2Input), 9.0, -10.0, 0.0, 0.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleVectorNear(context, LoadDouble3Data(double3Input), 11.0, -12.0, 13.0, 0.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleVectorNear(
        context,
        LoadDouble4Data(double4Input),
        14.0,
        -15.0,
        16.0,
        -17.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        LoadDouble2AData(alignedDouble2Input),
        18.0,
        -19.0,
        0.0,
        0.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        LoadDouble3AData(alignedDouble3Input),
        20.0,
        -21.0,
        22.0,
        0.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        LoadDouble4AData(alignedDouble4Input),
        23.0,
        -24.0,
        25.0,
        -26.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    Double2Data double2Output{};
    Double3Data double3Output{};
    AlignedDouble2Data alignedDouble2Output{};
    AlignedDouble3Data alignedDouble3Output{};
    AlignedDouble4Data alignedDouble4Output{};
    StoreDouble2Data(double2Output, lhs);
    StoreDouble3Data(double3Output, lhs);
    StoreDouble2AData(alignedDouble2Output, lhs);
    StoreDouble3AData(alignedDouble3Output, lhs);
    StoreDouble4AData(alignedDouble4Output, lhs);
    CheckDouble2Near(context, double2Output, 1.0, 2.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDouble3Near(context, double3Output, 1.0, 2.0, 3.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDouble2Near(context, alignedDouble2Output, 1.0, 2.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDouble3Near(context, alignedDouble3Output, 1.0, 2.0, 3.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDouble4Near(context, alignedDouble4Output, 1.0, 2.0, 3.0, 4.0, s_DoubleEpsilon, __FILE__, __LINE__);
    context.checkNearDouble(
        DoubleSimdVectorGetByIndex(lhs, 2),
        3.0,
        s_DoubleEpsilon,
        "DoubleSimdVectorGetByIndex",
        __FILE__,
        __LINE__
    );
    context.checkNearDouble(DoubleSimdVectorGetW(lhs), 4.0, s_DoubleEpsilon, "DoubleSimdVectorGetW", __FILE__, __LINE__);
    CheckDoubleVectorNear(context, DoubleSimdVectorSplatX(lhs), 1.0, 1.0, 1.0, 1.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleVectorNear(context, DoubleSimdVectorSplatY(lhs), 2.0, 2.0, 2.0, 2.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleVectorNear(context, DoubleSimdVectorSplatZ(lhs), 3.0, 3.0, 3.0, 3.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleVectorNear(context, DoubleSimdVectorSplatW(lhs), 4.0, 4.0, 4.0, 4.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector2Dot(lhs, rhs),
        8.0,
        8.0,
        8.0,
        8.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector2Cross(lhs, rhs),
        0.0,
        0.0,
        9.0,
        0.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector2LengthSq(MakeDoubleVector(3.0, 4.0, 0.0, 0.0)),
        25.0,
        25.0,
        25.0,
        25.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector2Length(MakeDoubleVector(3.0, 4.0, 0.0, 0.0)),
        5.0,
        5.0,
        5.0,
        5.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector2Normalize(MakeDoubleVector(3.0, 4.0, 0.0, 0.0)),
        0.6,
        0.8,
        0.0,
        0.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector3Dot(lhs, rhs),
        -10.0,
        -10.0,
        -10.0,
        -10.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector4Dot(lhs, rhs),
        22.0,
        22.0,
        22.0,
        22.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector3Cross(lhs, rhs),
        -27.0,
        0.0,
        9.0,
        0.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector3LengthSq(MakeDoubleVector(0.0, 3.0, 4.0, 9.0)),
        25.0,
        25.0,
        25.0,
        25.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector3Normalize(MakeDoubleVector(0.0, 3.0, 4.0, 0.0)),
        0.0,
        0.6,
        0.8,
        0.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector4LengthSq(MakeDoubleVector(1.0, 2.0, 2.0, 4.0)),
        25.0,
        25.0,
        25.0,
        25.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector4Length(MakeDoubleVector(1.0, 2.0, 2.0, 4.0)),
        5.0,
        5.0,
        5.0,
        5.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector4Normalize(MakeDoubleVector(1.0, 2.0, 2.0, 4.0)),
        0.2,
        0.4,
        0.4,
        0.8,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    const f64 halfPi = 1.57079632679489661923;
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector4Transform(MakeDoubleVector(0.0, 1.0, 0.0, 0.0), DoubleSimdMatrixRotationX(halfPi)),
        0.0,
        0.0,
        1.0,
        0.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector4Transform(MakeDoubleVector(0.0, 0.0, 1.0, 0.0), DoubleSimdMatrixRotationY(halfPi)),
        1.0,
        0.0,
        0.0,
        0.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector4Transform(MakeDoubleVector(1.0, 0.0, 0.0, 0.0), DoubleSimdMatrixRotationZ(halfPi)),
        0.0,
        1.0,
        0.0,
        0.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    const DoubleSimdMatrix matrixA = ComposeDouble(
        ComposeDouble(DoubleSimdMatrixTranslation(4.0, -5.0, 6.0), DoubleSimdMatrixRotationZ(0.4)),
        DoubleSimdMatrixScaling(2.0, 3.0, 4.0)
    );
    const DoubleSimdMatrix matrixB = ComposeDouble(
        DoubleSimdMatrixTranslation(-2.0, 1.0, 0.5),
        DoubleSimdMatrixRotationY(-0.3)
    );
    const DoubleRows4x4 rowsA = StoreDoubleMatrixRows(matrixA);
    const DoubleRows4x4 rowsB = StoreDoubleMatrixRows(matrixB);
    const DoubleRows4x4 expectedSetRows = { {
        { 1.0, 2.0, 3.0, 4.0 },
        { 5.0, 6.0, 7.0, 8.0 },
        { 9.0, 10.0, 11.0, 12.0 },
        { 13.0, 14.0, 15.0, 16.0 },
    } };
    const DoubleRows4x4 expectedZeroRows = {};
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixZero()),
        expectedZeroRows,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixSet(
            1.0, 2.0, 3.0, 4.0,
            5.0, 6.0, 7.0, 8.0,
            9.0, 10.0, 11.0, 12.0,
            13.0, 14.0, 15.0, 16.0
        )),
        expectedSetRows,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixSetColumns(
            MakeDoubleVector(1.0, 5.0, 9.0, 13.0),
            MakeDoubleVector(2.0, 6.0, 10.0, 14.0),
            MakeDoubleVector(3.0, 7.0, 11.0, 15.0),
            MakeDoubleVector(4.0, 8.0, 12.0, 16.0)
        )),
        expectedSetRows,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    context.checkTrue(DoubleSimdMatrixIsIdentity(DoubleSimdMatrixIdentity()), "DoubleSimdMatrixIsIdentity", __FILE__, __LINE__);
    context.checkTrue(!DoubleSimdMatrixIsIdentity(DoubleSimdMatrixZero()), "!DoubleSimdMatrixIsIdentity", __FILE__, __LINE__);
    context.checkTrue(DoubleSimdMatrixIsNaN(DoubleSimdMatrixSet(
        std::numeric_limits<f64>::quiet_NaN(), 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    )), "DoubleSimdMatrixIsNaN", __FILE__, __LINE__);
    context.checkTrue(DoubleSimdMatrixIsInfinite(DoubleSimdMatrixSet(
        std::numeric_limits<f64>::infinity(), 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    )), "DoubleSimdMatrixIsInfinite", __FILE__, __LINE__);
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixTranslationFromVector(MakeDoubleVector(4.0, -5.0, 6.0, 0.0))),
        StoreDoubleMatrixRows(DoubleSimdMatrixTranslation(4.0, -5.0, 6.0)),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixScalingFromVector(MakeDoubleVector(2.0, 3.0, 4.0, 0.0))),
        StoreDoubleMatrixRows(DoubleSimdMatrixScaling(2.0, 3.0, 4.0)),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixRotationNormal(MakeDoubleVector(0.0, 0.0, 1.0, 0.0), halfPi)),
        StoreDoubleMatrixRows(DoubleSimdMatrixRotationAxis(MakeDoubleVector(0.0, 0.0, 2.0, 0.0), halfPi)),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixMultiply(matrixA, matrixB)),
        MultiplyDoubleRows(rowsA, rowsB),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixTranspose(matrixA)),
        TransposeDoubleRows(rowsA),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixMultiplyTranspose(matrixA, matrixB)),
        TransposeDoubleRows(MultiplyDoubleRows(rowsA, rowsB)),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    const DoubleRows4x4 tensorRows = StoreDoubleMatrixRows(DoubleSimdMatrixVectorTensorProduct(lhs, rhs));
    const DoubleRows4x4 expectedTensor = { {
        { -2.0, 5.0, -6.0, 8.0 },
        { -4.0, 10.0, -12.0, 16.0 },
        { -6.0, 15.0, -18.0, 24.0 },
        { -8.0, 20.0, -24.0, 32.0 },
    } };
    CheckDoubleMatrixRowsNear(context, tensorRows, expectedTensor, s_DoubleEpsilon, __FILE__, __LINE__);

    const Double4Data transformed2 = StoreDoubleVector(
        DoubleSimdVector2Transform(MakeDoubleVector(1.0, 2.0, 0.0, 1.0), matrixA)
    );
    const Double4Data expected2 = TransformDoubleRows(rowsA, 1.0, 2.0, 0.0, 1.0);
    CheckDouble4Near(
        context,
        transformed2,
        expected2.x,
        expected2.y,
        expected2.z,
        expected2.w,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDouble4Near(
        context,
        StoreDoubleVector(DoubleSimdVector2TransformCoord(MakeDoubleVector(1.0, 2.0, 0.0, 1.0), matrixA)),
        expected2.x / expected2.w,
        expected2.y / expected2.w,
        expected2.z / expected2.w,
        1.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    const Double4Data transformed2Normal = StoreDoubleVector(
        DoubleSimdVector2TransformNormal(MakeDoubleVector(1.0, 2.0, 0.0, 0.0), matrixA)
    );
    const Double4Data expected2Normal = TransformDoubleRows(rowsA, 1.0, 2.0, 0.0, 0.0);
    CheckDouble4Near(
        context,
        transformed2Normal,
        expected2Normal.x,
        expected2Normal.y,
        expected2Normal.z,
        expected2Normal.w,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    const Double4Data transformedPoint = StoreDoubleVector(
        DoubleSimdVector3TransformCoord(MakeDoubleVector(1.0, 2.0, 3.0, 1.0), matrixA)
    );
    const Double4Data expectedPoint = TransformDoubleRows(rowsA, 1.0, 2.0, 3.0, 1.0);
    CheckDouble4Near(
        context,
        StoreDoubleVector(DoubleSimdVector3Transform(MakeDoubleVector(1.0, 2.0, 3.0, 1.0), matrixA)),
        expectedPoint.x,
        expectedPoint.y,
        expectedPoint.z,
        expectedPoint.w,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDouble4Near(
        context,
        transformedPoint,
        expectedPoint.x,
        expectedPoint.y,
        expectedPoint.z,
        expectedPoint.w,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    const Double4Data transformedNormal = StoreDoubleVector(
        DoubleSimdVector3TransformNormal(MakeDoubleVector(1.0, 2.0, 3.0, 0.0), matrixA)
    );
    const Double4Data expectedNormal = TransformDoubleRows(rowsA, 1.0, 2.0, 3.0, 0.0);
    CheckDouble4Near(
        context,
        transformedNormal,
        expectedNormal.x,
        expectedNormal.y,
        expectedNormal.z,
        expectedNormal.w,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    const Double2Data streamInput2[2] = {
        Double2Data(1.0, 2.0),
        Double2Data(-3.0, 4.0),
    };
    Double4Data transformed2Stream[2]{};
    DoubleSimdVector2TransformStream(
        MakeNotNull(&transformed2Stream[0]),
        sizeof(Double4Data),
        MakeNotNull(&streamInput2[0]),
        sizeof(Double2Data),
        2,
        matrixA
    );
    for(usize index = 0; index < 2; ++index){
        const Double4Data expected = TransformDoubleRows(rowsA, streamInput2[index].x, streamInput2[index].y, 0.0, 1.0);
        CheckDouble4Near(
            context,
            transformed2Stream[index],
            expected.x,
            expected.y,
            expected.z,
            expected.w,
            s_DoubleEpsilon,
            __FILE__,
            __LINE__
        );
    }

    Double2Data transformed2CoordStream[2]{};
    Double2Data transformed2NormalStream[2]{};
    DoubleSimdVector2TransformCoordStream(
        MakeNotNull(&transformed2CoordStream[0]),
        sizeof(Double2Data),
        MakeNotNull(&streamInput2[0]),
        sizeof(Double2Data),
        2,
        matrixA
    );
    DoubleSimdVector2TransformNormalStream(
        MakeNotNull(&transformed2NormalStream[0]),
        sizeof(Double2Data),
        MakeNotNull(&streamInput2[0]),
        sizeof(Double2Data),
        2,
        matrixA
    );
    for(usize index = 0; index < 2; ++index){
        const Double4Data expectedCoord = TransformDoubleRows(rowsA, streamInput2[index].x, streamInput2[index].y, 0.0, 1.0);
        const Double4Data expectedNormalStream = TransformDoubleRows(
            rowsA,
            streamInput2[index].x,
            streamInput2[index].y,
            0.0,
            0.0
        );
        CheckDouble2Near(
            context,
            transformed2CoordStream[index],
            expectedCoord.x / expectedCoord.w,
            expectedCoord.y / expectedCoord.w,
            s_DoubleEpsilon,
            __FILE__,
            __LINE__
        );
        CheckDouble2Near(
            context,
            transformed2NormalStream[index],
            expectedNormalStream.x,
            expectedNormalStream.y,
            s_DoubleEpsilon,
            __FILE__,
            __LINE__
        );
    }

    const Double3Data streamInput3[2] = {
        Double3Data(1.0, 2.0, 3.0),
        Double3Data(-3.0, 4.0, -5.0),
    };
    Double4Data transformed3Stream[2]{};
    Double3Data transformed3CoordStream[2]{};
    Double3Data transformed3NormalStream[2]{};
    DoubleSimdVector3TransformStream(
        MakeNotNull(&transformed3Stream[0]),
        sizeof(Double4Data),
        MakeNotNull(&streamInput3[0]),
        sizeof(Double3Data),
        2,
        matrixA
    );
    DoubleSimdVector3TransformCoordStream(
        MakeNotNull(&transformed3CoordStream[0]),
        sizeof(Double3Data),
        MakeNotNull(&streamInput3[0]),
        sizeof(Double3Data),
        2,
        matrixA
    );
    DoubleSimdVector3TransformNormalStream(
        MakeNotNull(&transformed3NormalStream[0]),
        sizeof(Double3Data),
        MakeNotNull(&streamInput3[0]),
        sizeof(Double3Data),
        2,
        matrixA
    );
    for(usize index = 0; index < 2; ++index){
        const Double4Data expectedTransform = TransformDoubleRows(
            rowsA,
            streamInput3[index].x,
            streamInput3[index].y,
            streamInput3[index].z,
            1.0
        );
        const Double4Data expectedNormalStream = TransformDoubleRows(
            rowsA,
            streamInput3[index].x,
            streamInput3[index].y,
            streamInput3[index].z,
            0.0
        );
        CheckDouble4Near(
            context,
            transformed3Stream[index],
            expectedTransform.x,
            expectedTransform.y,
            expectedTransform.z,
            expectedTransform.w,
            s_DoubleEpsilon,
            __FILE__,
            __LINE__
        );
        CheckDouble3Near(
            context,
            transformed3CoordStream[index],
            expectedTransform.x / expectedTransform.w,
            expectedTransform.y / expectedTransform.w,
            expectedTransform.z / expectedTransform.w,
            s_DoubleEpsilon,
            __FILE__,
            __LINE__
        );
        CheckDouble3Near(
            context,
            transformed3NormalStream[index],
            expectedNormalStream.x,
            expectedNormalStream.y,
            expectedNormalStream.z,
            s_DoubleEpsilon,
            __FILE__,
            __LINE__
        );
    }

    const Double4Data streamInput4[2] = {
        Double4Data(1.0, 2.0, 3.0, 1.0),
        Double4Data(-3.0, 4.0, -5.0, 0.5),
    };
    Double4Data transformed4Stream[2]{};
    DoubleSimdVector4TransformStream(
        MakeNotNull(&transformed4Stream[0]),
        sizeof(Double4Data),
        MakeNotNull(&streamInput4[0]),
        sizeof(Double4Data),
        2,
        matrixA
    );
    for(usize index = 0; index < 2; ++index){
        const Double4Data expected = TransformDoubleRows(
            rowsA,
            streamInput4[index].x,
            streamInput4[index].y,
            streamInput4[index].z,
            streamInput4[index].w
        );
        CheckDouble4Near(
            context,
            transformed4Stream[index],
            expected.x,
            expected.y,
            expected.z,
            expected.w,
            s_DoubleEpsilon,
            __FILE__,
            __LINE__
        );
    }

    const DoubleSimdVector determinant = DoubleSimdMatrixDeterminant(matrixA);
    context.checkNearDouble(
        StoreDoubleVector(determinant).x,
        24.0,
        s_DoubleEpsilon,
        "DoubleSimdMatrixDeterminant",
        __FILE__,
        __LINE__
    );

    DoubleSimdVector inverseDeterminant = DoubleSimdVectorZero();
    const DoubleSimdMatrix inverse = DoubleSimdMatrixInverse(&inverseDeterminant, matrixA);
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixMultiply(matrixA, inverse)),
        StoreDoubleMatrixRows(DoubleSimdMatrixIdentity()),
        1.0e-8,
        __FILE__,
        __LINE__
    );
    context.checkNearDouble(
        StoreDoubleVector(inverseDeterminant).x,
        24.0,
        s_DoubleEpsilon,
        "DoubleSimdMatrixInverse determinant",
        __FILE__,
        __LINE__
    );

    const DoubleSimdVector angles = MakeDoubleVector(0.3, -0.2, 0.5, 0.0);
    const DoubleSimdMatrix rpy = DoubleSimdMatrixRotationRollPitchYaw(
        DoubleSimdVectorGetX(angles),
        DoubleSimdVectorGetY(angles),
        DoubleSimdVectorGetZ(angles)
    );
    const DoubleSimdMatrix manualRpy = ComposeDouble(
        ComposeDouble(DoubleSimdMatrixRotationZ(0.5), DoubleSimdMatrixRotationX(0.3)),
        DoubleSimdMatrixRotationY(-0.2)
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(rpy),
        StoreDoubleMatrixRows(manualRpy),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixRotationRollPitchYawFromVector(angles)),
        StoreDoubleMatrixRows(rpy),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    const DoubleSimdVector quaternion = DoubleSimdQuaternionRotationRollPitchYaw(0.3, -0.2, 0.5);
    CheckDoubleVectorNear(
        context,
        DoubleSimdQuaternionIdentity(),
        0.0,
        0.0,
        0.0,
        1.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdQuaternionConjugate(MakeDoubleVector(0.1, -0.2, 0.3, 0.4)),
        -0.1,
        0.2,
        -0.3,
        0.4,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdQuaternionMultiply(DoubleSimdQuaternionIdentity(), quaternion),
        StoreDoubleVector(quaternion).x,
        StoreDoubleVector(quaternion).y,
        StoreDoubleVector(quaternion).z,
        StoreDoubleVector(quaternion).w,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdQuaternionNormalize(MakeDoubleVector(0.0, 0.0, 0.0, 2.0)),
        0.0,
        0.0,
        0.0,
        1.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleQuaternionEquivalent(
        context,
        DoubleSimdQuaternionRotationNormal(MakeDoubleVector(0.0, 0.0, 1.0, 0.0), halfPi),
        DoubleSimdQuaternionRotationAxis(MakeDoubleVector(0.0, 0.0, 2.0, 0.0), halfPi),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleQuaternionEquivalent(
        context,
        DoubleSimdQuaternionRotationRollPitchYawFromVector(angles),
        quaternion,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(rpy),
        StoreDoubleMatrixRows(DoubleSimdMatrixRotationQuaternion(quaternion)),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleQuaternionEquivalent(
        context,
        quaternion,
        DoubleSimdQuaternionRotationMatrix(DoubleSimdMatrixRotationQuaternion(quaternion)),
        1.0e-8,
        __FILE__,
        __LINE__
    );

    DoubleSimdVector outScale = DoubleSimdVectorZero();
    DoubleSimdVector outRotation = DoubleSimdVectorZero();
    DoubleSimdVector outTranslation = DoubleSimdVectorZero();
    context.checkTrue(
        DoubleSimdMatrixDecompose(outScale, outRotation, outTranslation, matrixA),
        "DoubleSimdMatrixDecompose",
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(context, outScale, 2.0, 3.0, 4.0, 0.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleVectorNear(context, outTranslation, 4.0, -5.0, 6.0, 1.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixAffineTransformation(
            outScale,
            DoubleSimdVectorZero(),
            outRotation,
            outTranslation
        )),
        rowsA,
        1.0e-8,
        __FILE__,
        __LINE__
    );

    const DoubleRows4x4 reflected = StoreDoubleMatrixRows(
        DoubleSimdMatrixReflect(MakeDoubleVector(0.0, 1.0, 0.0, 0.0))
    );
    const DoubleRows4x4 expectedReflect = { {
        { 1.0, 0.0, 0.0, 0.0 },
        { 0.0, -1.0, 0.0, 0.0 },
        { 0.0, 0.0, 1.0, 0.0 },
        { 0.0, 0.0, 0.0, 1.0 },
    } };
    CheckDoubleMatrixRowsNear(context, reflected, expectedReflect, s_DoubleEpsilon, __FILE__, __LINE__);

    const DoubleSimdMatrix shadow = DoubleSimdMatrixShadow(
        MakeDoubleVector(0.0, 1.0, 0.0, 0.0),
        MakeDoubleVector(0.0, -1.0, 0.0, 0.0)
    );
    const Double4Data shadowedRaw = StoreDoubleVector(
        DoubleSimdVector4Transform(MakeDoubleVector(2.0, 3.0, 4.0, 1.0), shadow)
    );
    CheckDouble4Near(
        context,
        Double4Data(
            shadowedRaw.x / shadowedRaw.w,
            shadowedRaw.y / shadowedRaw.w,
            shadowedRaw.z / shadowedRaw.w,
            1.0
        ),
        2.0,
        0.0,
        4.0,
        1.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    DoubleSimdVector planeDeterminant = DoubleSimdVectorZero();
    const DoubleSimdMatrix translatedPlaneInverse = DoubleSimdMatrixInverse(
        &planeDeterminant,
        DoubleSimdMatrixTranslation(0.0, 5.0, 0.0)
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdPlaneTransform(
            MakeDoubleVector(0.0, 1.0, 0.0, 0.0),
            DoubleSimdMatrixTranspose(translatedPlaneInverse)
        ),
        0.0,
        1.0,
        0.0,
        -5.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    const Double4Data planeStreamInput[2] = {
        Double4Data(0.0, 1.0, 0.0, 0.0),
        Double4Data(1.0, 0.0, 0.0, -2.0),
    };
    Double4Data planeStreamOutput[2]{};
    DoubleSimdPlaneTransformStream(
        MakeNotNull(&planeStreamOutput[0]),
        sizeof(Double4Data),
        MakeNotNull(&planeStreamInput[0]),
        sizeof(Double4Data),
        2,
        DoubleSimdMatrixTranspose(translatedPlaneInverse)
    );
    CheckDouble4Near(context, planeStreamOutput[0], 0.0, 1.0, 0.0, -5.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDouble4Near(context, planeStreamOutput[1], 1.0, 0.0, 0.0, -2.0, s_DoubleEpsilon, __FILE__, __LINE__);
    CheckDoubleVectorNear(
        context,
        DoubleSimdPlaneDot(MakeDoubleVector(0.0, 2.0, 0.0, -4.0), MakeDoubleVector(0.0, 3.0, 0.0, 1.0)),
        2.0,
        2.0,
        2.0,
        2.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdPlaneNormalize(MakeDoubleVector(0.0, 2.0, 0.0, -4.0)),
        0.0,
        1.0,
        0.0,
        -2.0,
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    const DoubleSimdVector zero = DoubleSimdVectorZero();
    const DoubleSimdVector scale2d = MakeDoubleVector(2.0, 3.0, 1.0, 0.0);
    const DoubleSimdVector translation2d = MakeDoubleVector(4.0, -5.0, 0.0, 0.0);
    const f64 rotation = 0.25;
    const DoubleSimdMatrix helper2d = DoubleSimdMatrixTransformation2D(
        zero,
        0.0,
        scale2d,
        zero,
        rotation,
        translation2d
    );
    const DoubleSimdMatrix manual2d = ComposeDouble(
        ComposeDouble(
            DoubleSimdMatrixTranslation(4.0, -5.0, 0.0),
            DoubleSimdMatrixRotationZ(rotation)
        ),
        DoubleSimdMatrixScaling(2.0, 3.0, 1.0)
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(helper2d),
        StoreDoubleMatrixRows(manual2d),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixAffineTransformation2D(scale2d, zero, rotation, translation2d)),
        StoreDoubleMatrixRows(manual2d),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    const DoubleSimdVector scalingOrigin2d = MakeDoubleVector(-2.0, 1.0, 0.0, 1.0);
    const DoubleSimdVector rotationOrigin2d = MakeDoubleVector(3.0, -4.0, 0.0, 1.0);
    const f64 scalingOrientation2d = -0.35;
    const DoubleSimdMatrix helper2dComplex = DoubleSimdMatrixTransformation2D(
        scalingOrigin2d,
        scalingOrientation2d,
        scale2d,
        rotationOrigin2d,
        rotation,
        translation2d
    );
    const DoubleSimdMatrix manual2dComplex = ComposeDouble(
        ComposeDouble(
            ComposeDouble(
                ComposeDouble(
                    ComposeDouble(
                        ComposeDouble(
                            ComposeDouble(
                                DoubleSimdMatrixTranslation(4.0, -5.0, 0.0),
                                DoubleSimdMatrixTranslation(3.0, -4.0, 0.0)
                            ),
                            DoubleSimdMatrixRotationZ(rotation)
                        ),
                        DoubleSimdMatrixTranslation(-3.0, 4.0, 0.0)
                    ),
                    DoubleSimdMatrixTranslation(-2.0, 1.0, 0.0)
                ),
                DoubleSimdMatrixRotationZ(scalingOrientation2d)
            ),
            DoubleSimdMatrixScaling(2.0, 3.0, 1.0)
        ),
        ComposeDouble(
            DoubleSimdMatrixTranspose(DoubleSimdMatrixRotationZ(scalingOrientation2d)),
            DoubleSimdMatrixTranslation(2.0, -1.0, 0.0)
        )
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(helper2dComplex),
        StoreDoubleMatrixRows(manual2dComplex),
        1.0e-8,
        __FILE__,
        __LINE__
    );
    const DoubleSimdVector complex2dPoint = MakeDoubleVector(-1.5, 0.75, 0.0, 1.0);
    const Double4Data complex2dManualPoint = StoreDoubleVector(
        DoubleSimdVector2TransformCoord(complex2dPoint, manual2dComplex)
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector2TransformCoord(complex2dPoint, helper2dComplex),
        complex2dManualPoint.x,
        complex2dManualPoint.y,
        complex2dManualPoint.z,
        complex2dManualPoint.w,
        1.0e-8,
        __FILE__,
        __LINE__
    );
    const DoubleSimdMatrix affine2dComplex = DoubleSimdMatrixAffineTransformation2D(
        scale2d,
        rotationOrigin2d,
        rotation,
        translation2d
    );
    const DoubleSimdMatrix affine2dComplexManual = ComposeDouble(
        ComposeDouble(
            ComposeDouble(
                ComposeDouble(
                    DoubleSimdMatrixTranslation(4.0, -5.0, 0.0),
                    DoubleSimdMatrixTranslation(3.0, -4.0, 0.0)
                ),
                DoubleSimdMatrixRotationZ(rotation)
            ),
            DoubleSimdMatrixTranslation(-3.0, 4.0, 0.0)
        ),
        DoubleSimdMatrixScaling(2.0, 3.0, 1.0)
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(affine2dComplex),
        StoreDoubleMatrixRows(affine2dComplexManual),
        1.0e-8,
        __FILE__,
        __LINE__
    );

    const DoubleSimdVector scaling = MakeDoubleVector(1.5, 2.0, 2.5, 0.0);
    const DoubleSimdVector rotationQuaternion = DoubleSimdQuaternionRotationRollPitchYaw(0.2, -0.3, 0.4);
    const DoubleSimdVector translation = MakeDoubleVector(7.0, -8.0, 9.0, 1.0);
    const DoubleSimdMatrix helper3d = DoubleSimdMatrixTransformation(
        zero,
        DoubleSimdQuaternionIdentity(),
        scaling,
        zero,
        rotationQuaternion,
        translation
    );
    const DoubleSimdMatrix manual3d = ComposeDouble(
        ComposeDouble(
            DoubleSimdMatrixTranslation(7.0, -8.0, 9.0),
            DoubleSimdMatrixRotationQuaternion(rotationQuaternion)
        ),
        DoubleSimdMatrixScaling(1.5, 2.0, 2.5)
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(helper3d),
        StoreDoubleMatrixRows(manual3d),
        1.0e-8,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixAffineTransformation(scaling, zero, rotationQuaternion, translation)),
        StoreDoubleMatrixRows(manual3d),
        1.0e-8,
        __FILE__,
        __LINE__
    );

    const DoubleSimdVector scalingOrigin3d = MakeDoubleVector(-2.0, 1.0, 0.5, 1.0);
    const DoubleSimdVector scalingOrientation3d = DoubleSimdQuaternionRotationRollPitchYaw(-0.15, 0.45, -0.2);
    const DoubleSimdVector rotationOrigin3d = MakeDoubleVector(3.0, -4.0, 5.0, 1.0);
    const DoubleSimdMatrix helper3dComplex = DoubleSimdMatrixTransformation(
        scalingOrigin3d,
        scalingOrientation3d,
        scaling,
        rotationOrigin3d,
        rotationQuaternion,
        translation
    );
    const DoubleSimdMatrix manual3dComplex = ComposeDouble(
        ComposeDouble(
            ComposeDouble(
                ComposeDouble(
                    ComposeDouble(
                        ComposeDouble(
                            ComposeDouble(
                                DoubleSimdMatrixTranslation(7.0, -8.0, 9.0),
                                DoubleSimdMatrixTranslation(3.0, -4.0, 5.0)
                            ),
                            DoubleSimdMatrixRotationQuaternion(rotationQuaternion)
                        ),
                        DoubleSimdMatrixTranslation(-3.0, 4.0, -5.0)
                    ),
                    DoubleSimdMatrixTranslation(-2.0, 1.0, 0.5)
                ),
                DoubleSimdMatrixRotationQuaternion(scalingOrientation3d)
            ),
            DoubleSimdMatrixScaling(1.5, 2.0, 2.5)
        ),
        ComposeDouble(
            DoubleSimdMatrixTranspose(DoubleSimdMatrixRotationQuaternion(scalingOrientation3d)),
            DoubleSimdMatrixTranslation(2.0, -1.0, -0.5)
        )
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(helper3dComplex),
        StoreDoubleMatrixRows(manual3dComplex),
        1.0e-8,
        __FILE__,
        __LINE__
    );
    const DoubleSimdVector complex3dPoint = MakeDoubleVector(-1.25, 0.5, 2.75, 1.0);
    const Double4Data complex3dManualPoint = StoreDoubleVector(
        DoubleSimdVector3TransformCoord(complex3dPoint, manual3dComplex)
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector3TransformCoord(complex3dPoint, helper3dComplex),
        complex3dManualPoint.x,
        complex3dManualPoint.y,
        complex3dManualPoint.z,
        complex3dManualPoint.w,
        1.0e-8,
        __FILE__,
        __LINE__
    );
    const DoubleSimdMatrix affine3dComplex = DoubleSimdMatrixAffineTransformation(
        scaling,
        rotationOrigin3d,
        rotationQuaternion,
        translation
    );
    const DoubleSimdMatrix affine3dComplexManual = ComposeDouble(
        ComposeDouble(
            ComposeDouble(
                ComposeDouble(
                    DoubleSimdMatrixTranslation(7.0, -8.0, 9.0),
                    DoubleSimdMatrixTranslation(3.0, -4.0, 5.0)
                ),
                DoubleSimdMatrixRotationQuaternion(rotationQuaternion)
            ),
            DoubleSimdMatrixTranslation(-3.0, 4.0, -5.0)
        ),
        DoubleSimdMatrixScaling(1.5, 2.0, 2.5)
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(affine3dComplex),
        StoreDoubleMatrixRows(affine3dComplexManual),
        1.0e-8,
        __FILE__,
        __LINE__
    );

    const DoubleSimdVector eye = MakeDoubleVector(3.0, 4.0, -5.0, 1.0);
    const DoubleSimdVector focus = MakeDoubleVector(6.0, 5.0, 2.0, 1.0);
    const DoubleSimdVector up = MakeDoubleVector(0.0, 1.0, 0.0, 0.0);
    const DoubleSimdVector direction = DoubleSimdVectorSubtract(focus, eye);
    const DoubleSimdMatrix lookAt = DoubleSimdMatrixLookAtLH(eye, focus, up);
    const DoubleSimdMatrix lookTo = DoubleSimdMatrixLookToLH(eye, direction, up);
    const DoubleSimdMatrix lookAtRh = DoubleSimdMatrixLookAtRH(eye, focus, up);
    const DoubleSimdMatrix lookToRh = DoubleSimdMatrixLookToRH(eye, direction, up);
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(lookAt),
        StoreDoubleMatrixRows(lookTo),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(lookAtRh),
        StoreDoubleMatrixRows(lookToRh),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    const f64 focusDistance = StoreDoubleVector(DoubleSimdVector3Length(direction)).x;
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector3TransformCoord(eye, lookAt),
        0.0,
        0.0,
        0.0,
        1.0,
        1.0e-8,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector3TransformCoord(focus, lookAt),
        0.0,
        0.0,
        focusDistance,
        1.0,
        1.0e-8,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector3TransformCoord(focus, lookAtRh),
        0.0,
        0.0,
        -focusDistance,
        1.0,
        1.0e-8,
        __FILE__,
        __LINE__
    );

    const f64 nearZ = 0.5;
    const f64 farZ = 50.0;
    const f64 viewWidth = 8.0;
    const f64 viewHeight = 6.0;
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixPerspectiveLH(viewWidth, viewHeight, nearZ, farZ)),
        StoreDoubleMatrixRows(DoubleSimdMatrixPerspectiveOffCenterLH(
            -viewWidth * 0.5,
            viewWidth * 0.5,
            -viewHeight * 0.5,
            viewHeight * 0.5,
            nearZ,
            farZ
        )),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixPerspectiveRH(viewWidth, viewHeight, nearZ, farZ)),
        StoreDoubleMatrixRows(DoubleSimdMatrixPerspectiveOffCenterRH(
            -viewWidth * 0.5,
            viewWidth * 0.5,
            -viewHeight * 0.5,
            viewHeight * 0.5,
            nearZ,
            farZ
        )),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixOrthographicLH(viewWidth, viewHeight, nearZ, farZ)),
        StoreDoubleMatrixRows(DoubleSimdMatrixOrthographicOffCenterLH(
            -viewWidth * 0.5,
            viewWidth * 0.5,
            -viewHeight * 0.5,
            viewHeight * 0.5,
            nearZ,
            farZ
        )),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );
    CheckDoubleMatrixRowsNear(
        context,
        StoreDoubleMatrixRows(DoubleSimdMatrixOrthographicRH(viewWidth, viewHeight, nearZ, farZ)),
        StoreDoubleMatrixRows(DoubleSimdMatrixOrthographicOffCenterRH(
            -viewWidth * 0.5,
            viewWidth * 0.5,
            -viewHeight * 0.5,
            viewHeight * 0.5,
            nearZ,
            farZ
        )),
        s_DoubleEpsilon,
        __FILE__,
        __LINE__
    );

    const DoubleSimdMatrix perspective = DoubleSimdMatrixPerspectiveFovLH(1.1, 16.0 / 9.0, nearZ, farZ);
    const DoubleSimdMatrix perspectiveRh = DoubleSimdMatrixPerspectiveFovRH(1.1, 16.0 / 9.0, nearZ, farZ);
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector3TransformCoord(MakeDoubleVector(0.0, 0.0, nearZ, 1.0), perspective),
        0.0,
        0.0,
        0.0,
        1.0,
        1.0e-8,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector3TransformCoord(MakeDoubleVector(0.0, 0.0, farZ, 1.0), perspective),
        0.0,
        0.0,
        1.0,
        1.0,
        1.0e-8,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector3TransformCoord(MakeDoubleVector(0.0, 0.0, -nearZ, 1.0), perspectiveRh),
        0.0,
        0.0,
        0.0,
        1.0,
        1.0e-8,
        __FILE__,
        __LINE__
    );
    CheckDoubleVectorNear(
        context,
        DoubleSimdVector3TransformCoord(MakeDoubleVector(0.0, 0.0, -farZ, 1.0), perspectiveRh),
        0.0,
        0.0,
        1.0,
        1.0,
        1.0e-8,
        __FILE__,
        __LINE__
    );

    const DoubleSimdMatrix world = DoubleSimdMatrixTranslation(1.0, -2.0, 3.0);
    const DoubleSimdMatrix view = DoubleSimdMatrixLookAtLH(
        MakeDoubleVector(0.0, 0.0, -10.0, 1.0),
        MakeDoubleVector(0.0, 0.0, 0.0, 1.0),
        up
    );
    const DoubleSimdVector sourcePoint = MakeDoubleVector(2.0, 3.0, 4.0, 1.0);
    const DoubleSimdVector projected = DoubleSimdVector3Project(
        sourcePoint,
        10.0,
        20.0,
        800.0,
        600.0,
        0.0,
        1.0,
        perspective,
        view,
        world
    );
    const DoubleSimdVector unprojected = DoubleSimdVector3Unproject(
        projected,
        10.0,
        20.0,
        800.0,
        600.0,
        0.0,
        1.0,
        perspective,
        view,
        world
    );
    CheckDoubleVectorNear(context, unprojected, 2.0, 3.0, 4.0, 1.0, 1.0e-8, __FILE__, __LINE__);

    const Double3Data projectStreamInput[1] = {
        Double3Data(2.0, 3.0, 4.0),
    };
    Double3Data projectStreamOutput[1]{};
    DoubleSimdVector3ProjectStream(
        MakeNotNull(&projectStreamOutput[0]),
        sizeof(Double3Data),
        MakeNotNull(&projectStreamInput[0]),
        sizeof(Double3Data),
        1,
        10.0,
        20.0,
        800.0,
        600.0,
        0.0,
        1.0,
        perspective,
        view,
        world
    );
    const Double4Data projectedStored = StoreDoubleVector(projected);
    CheckDouble3Near(
        context,
        projectStreamOutput[0],
        projectedStored.x,
        projectedStored.y,
        projectedStored.z,
        1.0e-8,
        __FILE__,
        __LINE__
    );

    Double3Data unprojectStreamOutput[1]{};
    DoubleSimdVector3UnprojectStream(
        MakeNotNull(&unprojectStreamOutput[0]),
        sizeof(Double3Data),
        MakeNotNull(static_cast<const Double3Data*>(&projectStreamOutput[0])),
        sizeof(Double3Data),
        1,
        10.0,
        20.0,
        800.0,
        600.0,
        0.0,
        1.0,
        perspective,
        view,
        world
    );
    CheckDouble3Near(context, unprojectStreamOutput[0], 2.0, 3.0, 4.0, 1.0e-8, __FILE__, __LINE__);
}

static void TestMatrixBoundary(TestContext& context){
    const Float3x4Data original3x4(
        1.0f, 2.0f, 3.0f, 10.0f,
        4.0f, 5.0f, 6.0f, 11.0f,
        7.0f, 8.0f, 9.0f, 12.0f
    );
    const SimdMatrix matrix3x4 = LoadFloat3x4(original3x4);
    Float3x4Data roundtrip3x4{};
    StoreFloat3x4(roundtrip3x4, matrix3x4);
    CheckMatrix3x4Near(context, roundtrip3x4, original3x4, s_FloatEpsilon, __FILE__, __LINE__);

    const AlignedFloat3x4Data aligned3x4(
        3.0f, 1.0f, 4.0f, 7.0f,
        1.0f, 5.0f, 9.0f, 8.0f,
        2.0f, 6.0f, 5.0f, 9.0f
    );
    const SimdMatrix alignedMatrix3x4 = LoadFloat3x4A(aligned3x4);
    AlignedFloat3x4Data alignedRoundtrip3x4{};
    StoreFloat3x4A(alignedRoundtrip3x4, alignedMatrix3x4);
    CheckMatrix3x4Near(context, alignedRoundtrip3x4, aligned3x4, s_FloatEpsilon, __FILE__, __LINE__);

    const Double3x4Data originalDouble3x4(
        1.0, 2.0, 3.0, 10.0,
        4.0, 5.0, 6.0, 11.0,
        7.0, 8.0, 9.0, 12.0
    );
    const DoubleSimdMatrix doubleMatrix3x4 = LoadDouble3x4(originalDouble3x4);
    Double3x4Data roundtripDouble3x4{};
    StoreDouble3x4(roundtripDouble3x4, doubleMatrix3x4);
    CheckDoubleMatrix3x4Near(context, roundtripDouble3x4, originalDouble3x4, s_DoubleEpsilon, __FILE__, __LINE__);

    const AlignedDouble3x4Data alignedDouble3x4(
        3.0, 1.0, 4.0, 7.0,
        1.0, 5.0, 9.0, 8.0,
        2.0, 6.0, 5.0, 9.0
    );
    const DoubleSimdMatrix alignedDoubleMatrix3x4 = LoadDouble3x4A(alignedDouble3x4);
    AlignedDouble3x4Data alignedRoundtripDouble3x4{};
    StoreDouble3x4A(alignedRoundtripDouble3x4, alignedDoubleMatrix3x4);
    CheckDoubleMatrix3x4Near(context, alignedRoundtripDouble3x4, alignedDouble3x4, s_DoubleEpsilon, __FILE__, __LINE__);
}

static void TestMatrixBuilders(TestContext& context){
    const FloatRows4x4 identity = StoreMatrixRows(MatrixIdentity());
    const FloatRows4x4 expectedIdentity = { {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    } };
    CheckMatrixRowsNear(context, identity, expectedIdentity, s_FloatEpsilon, __FILE__, __LINE__);

    const SimdMatrix setMatrix = MatrixSet(
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    );
    const FloatRows4x4 expectedSet = { {
        { 1.0f, 2.0f, 3.0f, 4.0f },
        { 5.0f, 6.0f, 7.0f, 8.0f },
        { 9.0f, 10.0f, 11.0f, 12.0f },
        { 13.0f, 14.0f, 15.0f, 16.0f },
    } };
    CheckMatrixRowsNear(context, StoreMatrixRows(setMatrix), expectedSet, s_FloatEpsilon, __FILE__, __LINE__);

    const FloatRows4x4 translation = StoreMatrixRows(MatrixTranslation(4.0f, -5.0f, 6.0f));
    const FloatRows4x4 expectedTranslation = { {
        { 1.0f, 0.0f, 0.0f, 4.0f },
        { 0.0f, 1.0f, 0.0f, -5.0f },
        { 0.0f, 0.0f, 1.0f, 6.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    } };
    CheckMatrixRowsNear(context, translation, expectedTranslation, s_FloatEpsilon, __FILE__, __LINE__);
    CheckMatrixRowsNear(context, StoreMatrixRows(::MatrixTranslationFromVector(MakeVector(4.0f, -5.0f, 6.0f, 1.0f))), expectedTranslation, s_FloatEpsilon, __FILE__, __LINE__);

    const FloatRows4x4 scaling = StoreMatrixRows(MatrixScaling(2.0f, 3.0f, 4.0f));
    const FloatRows4x4 expectedScaling = { {
        { 2.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 3.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 4.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    } };
    CheckMatrixRowsNear(context, scaling, expectedScaling, s_FloatEpsilon, __FILE__, __LINE__);
    CheckMatrixRowsNear(context, StoreMatrixRows(::MatrixScalingFromVector(MakeVector(2.0f, 3.0f, 4.0f, 1.0f))), expectedScaling, s_FloatEpsilon, __FILE__, __LINE__);

    const f32 halfPi = 1.57079632679f;
    const Float4Data rxResult = StoreVector(Vector4Transform(MakeVector(0.0f, 1.0f, 0.0f, 0.0f), MatrixRotationX(halfPi)));
    CheckFloat4Near(context, rxResult, 0.0f, 0.0f, 1.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const Float4Data ryResult = StoreVector(Vector4Transform(MakeVector(0.0f, 0.0f, 1.0f, 0.0f), MatrixRotationY(halfPi)));
    CheckFloat4Near(context, ryResult, 1.0f, 0.0f, 0.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const Float4Data rzResult = StoreVector(Vector4Transform(MakeVector(1.0f, 0.0f, 0.0f, 0.0f), MatrixRotationZ(halfPi)));
    CheckFloat4Near(context, rzResult, 0.0f, 1.0f, 0.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SimdVector angles = MakeVector(0.3f, -0.2f, 0.5f, 0.0f);
    const SimdMatrix rpyFromScalars = MatrixRotationRollPitchYaw(0.3f, -0.2f, 0.5f);
    const SimdMatrix rpyFromVector = ::MatrixRotationRollPitchYawFromVector(angles);
    CheckMatrixRowsNear(context, StoreMatrixRows(rpyFromScalars), StoreMatrixRows(rpyFromVector), s_FloatEpsilon, __FILE__, __LINE__);
    CheckMatrixRowsNear(
        context,
        StoreMatrixRows(rpyFromScalars),
        StoreMatrixRows(Compose(Compose(MatrixRotationZ(0.5f), MatrixRotationX(0.3f)), MatrixRotationY(-0.2f))),
        s_FloatLooseEpsilon,
        __FILE__,
        __LINE__
    );

    const SimdVector quaternion = SourceMath::QuaternionRotationRollPitchYaw(0.3f, -0.2f, 0.5f);
    CheckMatrixRowsNear(context, StoreMatrixRows(rpyFromScalars), StoreMatrixRows(::MatrixRotationQuaternion(quaternion)), s_FloatEpsilon, __FILE__, __LINE__);
    const SimdVector rotatedByQuaternion = SourceMath::Vector3Rotate(MakeVector(1.0f, -2.0f, 3.0f, 0.0f), quaternion);
    const Float4Data rotatedByMatrix = StoreVector(Vector3TransformNormal(MakeVector(1.0f, -2.0f, 3.0f, 0.0f), ::MatrixRotationQuaternion(quaternion)));
    CheckFloat4Near(context, StoreVector(rotatedByQuaternion), rotatedByMatrix.x, rotatedByMatrix.y, rotatedByMatrix.z, rotatedByMatrix.w, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(SourceMath::Vector3InverseRotate(rotatedByQuaternion, quaternion)), 1.0f, -2.0f, 3.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SimdVector axis = MakeVector(1.0f, 2.0f, 3.0f, 0.0f);
    CheckMatrixRowsNear(
        context,
        StoreMatrixRows(::MatrixRotationNormal(SourceMath::Vector3Normalize(axis), 0.7f)),
        StoreMatrixRows(::MatrixRotationAxis(axis, 0.7f)),
        s_FloatLooseEpsilon,
        __FILE__,
        __LINE__
    );

    const FloatRows4x4 reflected = StoreMatrixRows(::MatrixReflect(MakeVector(0.0f, 1.0f, 0.0f, 0.0f)));
    const FloatRows4x4 expectedReflect = { {
        { 1.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, -1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 1.0f },
    } };
    CheckMatrixRowsNear(context, reflected, expectedReflect, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SimdMatrix shadow = ::MatrixShadow(MakeVector(0.0f, 1.0f, 0.0f, 0.0f), MakeVector(0.0f, -1.0f, 0.0f, 0.0f));
    const Float4Data shadowedRaw = StoreVector(Vector4Transform(MakeVector(2.0f, 3.0f, 4.0f, 1.0f), shadow));
    CheckFloat4Near(
        context,
        Float4Data(shadowedRaw.x / shadowedRaw.w, shadowedRaw.y / shadowedRaw.w, shadowedRaw.z / shadowedRaw.w, 1.0f),
        2.0f,
        0.0f,
        4.0f,
        1.0f,
        s_FloatLooseEpsilon,
        __FILE__,
        __LINE__
    );

    const SimdVector eye = MakeVector(3.0f, 4.0f, -5.0f, 1.0f);
    const SimdVector focus = MakeVector(6.0f, 5.0f, 2.0f, 1.0f);
    const SimdVector up = MakeVector(0.0f, 1.0f, 0.0f, 0.0f);
    const SimdVector direction = SourceMath::VectorSubtract(focus, eye);
    const SimdMatrix lookAtLh = ::MatrixLookAtLH(eye, focus, up);
    const SimdMatrix lookToLh = ::MatrixLookToLH(eye, direction, up);
    const SimdMatrix lookAtRh = ::MatrixLookAtRH(eye, focus, up);
    const SimdMatrix lookToRh = ::MatrixLookToRH(eye, direction, up);
    CheckMatrixRowsNear(context, StoreMatrixRows(lookAtLh), StoreMatrixRows(lookToLh), s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckMatrixRowsNear(context, StoreMatrixRows(lookAtRh), StoreMatrixRows(lookToRh), s_FloatLooseEpsilon, __FILE__, __LINE__);
    const f32 focusDistance = StoreVector(SourceMath::Vector3Length(direction)).x;
    CheckFloat4Near(context, StoreVector(Vector3TransformCoord(eye, lookAtLh)), 0.0f, 0.0f, 0.0f, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(Vector3TransformCoord(focus, lookAtLh)), 0.0f, 0.0f, focusDistance, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(Vector3TransformCoord(eye, lookAtRh)), 0.0f, 0.0f, 0.0f, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(Vector3TransformCoord(focus, lookAtRh)), 0.0f, 0.0f, -focusDistance, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const f32 nearZ = 0.5f;
    const f32 farZ = 50.0f;
    const f32 viewWidth = 8.0f;
    const f32 viewHeight = 6.0f;
    CheckMatrixRowsNear(
        context,
        StoreMatrixRows(MatrixPerspectiveLH(viewWidth, viewHeight, nearZ, farZ)),
        StoreMatrixRows(MatrixPerspectiveOffCenterLH(-viewWidth * 0.5f, viewWidth * 0.5f, -viewHeight * 0.5f, viewHeight * 0.5f, nearZ, farZ)),
        s_FloatLooseEpsilon,
        __FILE__,
        __LINE__
    );
    CheckMatrixRowsNear(
        context,
        StoreMatrixRows(MatrixPerspectiveRH(viewWidth, viewHeight, nearZ, farZ)),
        StoreMatrixRows(MatrixPerspectiveOffCenterRH(-viewWidth * 0.5f, viewWidth * 0.5f, -viewHeight * 0.5f, viewHeight * 0.5f, nearZ, farZ)),
        s_FloatLooseEpsilon,
        __FILE__,
        __LINE__
    );
    CheckMatrixRowsNear(
        context,
        StoreMatrixRows(MatrixOrthographicLH(viewWidth, viewHeight, nearZ, farZ)),
        StoreMatrixRows(MatrixOrthographicOffCenterLH(-viewWidth * 0.5f, viewWidth * 0.5f, -viewHeight * 0.5f, viewHeight * 0.5f, nearZ, farZ)),
        s_FloatLooseEpsilon,
        __FILE__,
        __LINE__
    );
    CheckMatrixRowsNear(
        context,
        StoreMatrixRows(MatrixOrthographicRH(viewWidth, viewHeight, nearZ, farZ)),
        StoreMatrixRows(MatrixOrthographicOffCenterRH(-viewWidth * 0.5f, viewWidth * 0.5f, -viewHeight * 0.5f, viewHeight * 0.5f, nearZ, farZ)),
        s_FloatLooseEpsilon,
        __FILE__,
        __LINE__
    );

    const f32 fovY = 1.1f;
    const f32 aspect = 16.0f / 9.0f;
    const f32 fovHeight = 2.0f * nearZ * std::tan(fovY * 0.5f);
    const f32 fovWidth = fovHeight * aspect;
    CheckMatrixRowsNear(
        context,
        StoreMatrixRows(MatrixPerspectiveFovLH(fovY, aspect, nearZ, farZ)),
        StoreMatrixRows(MatrixPerspectiveLH(fovWidth, fovHeight, nearZ, farZ)),
        s_FloatLooseEpsilon,
        __FILE__,
        __LINE__
    );
    CheckMatrixRowsNear(
        context,
        StoreMatrixRows(MatrixPerspectiveFovRH(fovY, aspect, nearZ, farZ)),
        StoreMatrixRows(MatrixPerspectiveRH(fovWidth, fovHeight, nearZ, farZ)),
        s_FloatLooseEpsilon,
        __FILE__,
        __LINE__
    );

    const SimdMatrix perspectiveLh = MatrixPerspectiveLH(viewWidth, viewHeight, nearZ, farZ);
    const SimdMatrix perspectiveRh = MatrixPerspectiveRH(viewWidth, viewHeight, nearZ, farZ);
    const SimdMatrix orthographicLh = MatrixOrthographicLH(viewWidth, viewHeight, nearZ, farZ);
    const SimdMatrix orthographicRh = MatrixOrthographicRH(viewWidth, viewHeight, nearZ, farZ);
    CheckFloat4Near(context, StoreVector(Vector3TransformCoord(MakeVector(0.0f, 0.0f, nearZ, 1.0f), perspectiveLh)), 0.0f, 0.0f, 0.0f, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(Vector3TransformCoord(MakeVector(0.0f, 0.0f, farZ, 1.0f), perspectiveLh)), 0.0f, 0.0f, 1.0f, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(Vector3TransformCoord(MakeVector(0.0f, 0.0f, -nearZ, 1.0f), perspectiveRh)), 0.0f, 0.0f, 0.0f, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(Vector3TransformCoord(MakeVector(0.0f, 0.0f, -farZ, 1.0f), perspectiveRh)), 0.0f, 0.0f, 1.0f, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(Vector3TransformCoord(MakeVector(0.0f, 0.0f, nearZ, 1.0f), orthographicLh)), 0.0f, 0.0f, 0.0f, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(Vector3TransformCoord(MakeVector(0.0f, 0.0f, farZ, 1.0f), orthographicLh)), 0.0f, 0.0f, 1.0f, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(Vector3TransformCoord(MakeVector(0.0f, 0.0f, -nearZ, 1.0f), orthographicRh)), 0.0f, 0.0f, 0.0f, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(Vector3TransformCoord(MakeVector(0.0f, 0.0f, -farZ, 1.0f), orthographicRh)), 0.0f, 0.0f, 1.0f, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
}

static void TestMatrixRelations(TestContext& context){
    const SimdMatrix matrixA = Compose(Compose(MatrixTranslation(4.0f, -5.0f, 6.0f), MatrixRotationZ(0.4f)), MatrixScaling(2.0f, 3.0f, 4.0f));
    const SimdMatrix matrixB = Compose(MatrixTranslation(-2.0f, 1.0f, 0.5f), MatrixRotationY(-0.3f));

    const FloatRows4x4 rowsA = StoreMatrixRows(matrixA);
    const FloatRows4x4 rowsB = StoreMatrixRows(matrixB);
    const FloatRows4x4 expectedProduct = MultiplyRows(rowsA, rowsB);

    const SimdMatrix multiplied = MatrixMultiply(matrixA, matrixB);
    CheckMatrixRowsNear(context, StoreMatrixRows(multiplied), expectedProduct, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SimdMatrix multipliedTranspose = MatrixMultiplyTranspose(matrixA, matrixB);
    CheckMatrixRowsNear(context, StoreMatrixRows(multipliedTranspose), TransposeRows(expectedProduct), s_FloatLooseEpsilon, __FILE__, __LINE__);

    CheckMatrixRowsNear(context, StoreMatrixRows(MatrixTranspose(MatrixTranspose(matrixA))), rowsA, s_FloatEpsilon, __FILE__, __LINE__);

    const SimdVector determinantVector = MatrixDeterminant(matrixA);
    const Float4Data determinantStored = StoreVector(determinantVector);
    context.checkNearFloat(determinantStored.x, 24.0f, s_FloatLooseEpsilon, "determinant", __FILE__, __LINE__);

    SimdVector inverseDeterminant = SourceMath::VectorZero();
    const SimdMatrix inverse = MatrixInverse(&inverseDeterminant, matrixA);
    CheckMatrixRowsNear(context, StoreMatrixRows(MatrixMultiply(matrixA, inverse)), StoreMatrixRows(MatrixIdentity()), 2.0e-3f, __FILE__, __LINE__);
    context.checkNearFloat(StoreVector(inverseDeterminant).x, determinantStored.x, s_FloatLooseEpsilon, "inverse determinant", __FILE__, __LINE__);

    const SimdVector tensorA = MakeVector(1.0f, 2.0f, 3.0f, 4.0f);
    const SimdVector tensorB = MakeVector(-1.0f, 5.0f, 7.0f, 9.0f);
    const FloatRows4x4 tensorRows = StoreMatrixRows(::MatrixVectorTensorProduct(tensorA, tensorB));
    const FloatRows4x4 expectedTensor = { {
        { -1.0f, 5.0f, 7.0f, 9.0f },
        { -2.0f, 10.0f, 14.0f, 18.0f },
        { -3.0f, 15.0f, 21.0f, 27.0f },
        { -4.0f, 20.0f, 28.0f, 36.0f },
    } };
    CheckMatrixRowsNear(context, tensorRows, expectedTensor, s_FloatEpsilon, __FILE__, __LINE__);

    SimdVector outScale = SourceMath::VectorZero();
    SimdVector outRotation = SourceMath::VectorZero();
    SimdVector outTranslation = SourceMath::VectorZero();
    context.checkTrue(MatrixDecompose(outScale, outRotation, outTranslation, matrixA), "MatrixDecompose", __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(outScale), 2.0f, 3.0f, 4.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, StoreVector(outTranslation), 4.0f, -5.0f, 6.0f, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SimdMatrix reconstructed = ::MatrixAffineTransformation(outScale, SourceMath::VectorZero(), outRotation, outTranslation);
    CheckMatrixRowsNear(context, StoreMatrixRows(reconstructed), rowsA, 2.0e-3f, __FILE__, __LINE__);
}

static void TestMatrixTransformationHelpers(TestContext& context){
    const SimdVector zero = SourceMath::VectorZero();
    const SimdVector scale2d = MakeVector(2.0f, 3.0f, 1.0f, 0.0f);
    const SimdVector translation2d = MakeVector(4.0f, -5.0f, 0.0f, 0.0f);
    const f32 rotation = 0.25f;

    const SimdMatrix helper2d = ::MatrixTransformation2D(zero, 0.0f, scale2d, zero, rotation, translation2d);
    const SimdMatrix manual2d = Compose(Compose(MatrixTranslation(4.0f, -5.0f, 0.0f), MatrixRotationZ(rotation)), MatrixScaling(2.0f, 3.0f, 1.0f));
    const Float4Data point2d = StoreVector(Vector2TransformCoord(MakeVector(1.0f, 2.0f, 0.0f, 1.0f), helper2d));
    const Float4Data point2dManual = StoreVector(Vector2TransformCoord(MakeVector(1.0f, 2.0f, 0.0f, 1.0f), manual2d));
    CheckFloat4Near(context, point2d, point2dManual.x, point2dManual.y, point2dManual.z, point2dManual.w, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SimdMatrix affine2d = ::MatrixAffineTransformation2D(scale2d, zero, rotation, translation2d);
    const Float4Data affine2dPoint = StoreVector(Vector2TransformCoord(MakeVector(1.0f, 2.0f, 0.0f, 1.0f), affine2d));
    CheckFloat4Near(context, affine2dPoint, point2dManual.x, point2dManual.y, point2dManual.z, point2dManual.w, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SimdVector scalingOrigin2d = MakeVector(-2.0f, 1.0f, 0.0f, 1.0f);
    const SimdVector rotationOrigin2d = MakeVector(3.0f, -4.0f, 0.0f, 1.0f);
    const f32 scalingOrientation2d = -0.35f;
    const SimdMatrix helper2dComplex = ::MatrixTransformation2D(
        scalingOrigin2d,
        scalingOrientation2d,
        scale2d,
        rotationOrigin2d,
        rotation,
        translation2d
    );
    const SimdMatrix manual2dComplex = Compose(
        Compose(
            Compose(
                Compose(
                    Compose(
                        Compose(
                            Compose(
                                MatrixTranslation(4.0f, -5.0f, 0.0f),
                                MatrixTranslation(3.0f, -4.0f, 0.0f)
                            ),
                            MatrixRotationZ(rotation)
                        ),
                        MatrixTranslation(-3.0f, 4.0f, 0.0f)
                    ),
                    MatrixTranslation(-2.0f, 1.0f, 0.0f)
                ),
                MatrixRotationZ(scalingOrientation2d)
            ),
            MatrixScaling(2.0f, 3.0f, 1.0f)
        ),
        Compose(
            MatrixTranspose(MatrixRotationZ(scalingOrientation2d)),
            MatrixTranslation(2.0f, -1.0f, 0.0f)
        )
    );
    CheckMatrixRowsNear(context, StoreMatrixRows(helper2dComplex), StoreMatrixRows(manual2dComplex), s_FloatLooseEpsilon, __FILE__, __LINE__);
    const Float4Data complex2dPoint = StoreVector(Vector2TransformCoord(MakeVector(-1.5f, 0.75f, 0.0f, 1.0f), helper2dComplex));
    const Float4Data complex2dManualPoint = StoreVector(Vector2TransformCoord(MakeVector(-1.5f, 0.75f, 0.0f, 1.0f), manual2dComplex));
    CheckFloat4Near(context, complex2dPoint, complex2dManualPoint.x, complex2dManualPoint.y, complex2dManualPoint.z, complex2dManualPoint.w, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SimdMatrix affine2dComplex = ::MatrixAffineTransformation2D(scale2d, rotationOrigin2d, rotation, translation2d);
    const SimdMatrix affine2dComplexManual = Compose(
        Compose(
            Compose(
                Compose(
                    MatrixTranslation(4.0f, -5.0f, 0.0f),
                    MatrixTranslation(3.0f, -4.0f, 0.0f)
                ),
                MatrixRotationZ(rotation)
            ),
            MatrixTranslation(-3.0f, 4.0f, 0.0f)
        ),
        MatrixScaling(2.0f, 3.0f, 1.0f)
    );
    CheckMatrixRowsNear(context, StoreMatrixRows(affine2dComplex), StoreMatrixRows(affine2dComplexManual), s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SimdVector scaling = MakeVector(1.5f, 2.0f, 2.5f, 0.0f);
    const SimdVector rotationQuaternion = SourceMath::QuaternionRotationRollPitchYaw(0.2f, -0.3f, 0.4f);
    const SimdVector translation = MakeVector(7.0f, -8.0f, 9.0f, 1.0f);
    const SimdMatrix helper3d = ::MatrixTransformation(zero, SourceMath::QuaternionIdentity(), scaling, zero, rotationQuaternion, translation);
    const SimdMatrix affine3d = ::MatrixAffineTransformation(scaling, zero, rotationQuaternion, translation);
    const SimdMatrix manual3d = Compose(Compose(MatrixTranslation(7.0f, -8.0f, 9.0f), ::MatrixRotationQuaternion(rotationQuaternion)), MatrixScaling(1.5f, 2.0f, 2.5f));
    const Float4Data helperPoint = StoreVector(Vector3TransformCoord(MakeVector(1.0f, 2.0f, 3.0f, 1.0f), helper3d));
    const Float4Data affinePoint = StoreVector(Vector3TransformCoord(MakeVector(1.0f, 2.0f, 3.0f, 1.0f), affine3d));
    const Float4Data manualPoint = StoreVector(Vector3TransformCoord(MakeVector(1.0f, 2.0f, 3.0f, 1.0f), manual3d));
    CheckFloat4Near(context, helperPoint, manualPoint.x, manualPoint.y, manualPoint.z, manualPoint.w, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckFloat4Near(context, affinePoint, manualPoint.x, manualPoint.y, manualPoint.z, manualPoint.w, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SimdVector scalingOrigin3d = MakeVector(-2.0f, 1.0f, 0.5f, 1.0f);
    const SimdVector scalingOrientation3d = SourceMath::QuaternionRotationRollPitchYaw(-0.15f, 0.45f, -0.2f);
    const SimdVector rotationOrigin3d = MakeVector(3.0f, -4.0f, 5.0f, 1.0f);
    const SimdMatrix helper3dComplex = ::MatrixTransformation(
        scalingOrigin3d,
        scalingOrientation3d,
        scaling,
        rotationOrigin3d,
        rotationQuaternion,
        translation
    );
    const SimdMatrix manual3dComplex = Compose(
        Compose(
            Compose(
                Compose(
                    Compose(
                        Compose(
                            Compose(
                                MatrixTranslation(7.0f, -8.0f, 9.0f),
                                MatrixTranslation(3.0f, -4.0f, 5.0f)
                            ),
                            ::MatrixRotationQuaternion(rotationQuaternion)
                        ),
                        MatrixTranslation(-3.0f, 4.0f, -5.0f)
                    ),
                    MatrixTranslation(-2.0f, 1.0f, 0.5f)
                ),
                ::MatrixRotationQuaternion(scalingOrientation3d)
            ),
            MatrixScaling(1.5f, 2.0f, 2.5f)
        ),
        Compose(
            MatrixTranspose(::MatrixRotationQuaternion(scalingOrientation3d)),
            MatrixTranslation(2.0f, -1.0f, -0.5f)
        )
    );
    CheckMatrixRowsNear(context, StoreMatrixRows(helper3dComplex), StoreMatrixRows(manual3dComplex), 3.0e-3f, __FILE__, __LINE__);
    const Float4Data complex3dPoint = StoreVector(Vector3TransformCoord(MakeVector(-1.25f, 0.5f, 2.75f, 1.0f), helper3dComplex));
    const Float4Data complex3dManualPoint = StoreVector(Vector3TransformCoord(MakeVector(-1.25f, 0.5f, 2.75f, 1.0f), manual3dComplex));
    CheckFloat4Near(context, complex3dPoint, complex3dManualPoint.x, complex3dManualPoint.y, complex3dManualPoint.z, complex3dManualPoint.w, 3.0e-3f, __FILE__, __LINE__);

    const SimdMatrix affine3dComplex = ::MatrixAffineTransformation(scaling, rotationOrigin3d, rotationQuaternion, translation);
    const SimdMatrix affine3dComplexManual = Compose(
        Compose(
            Compose(
                Compose(
                    MatrixTranslation(7.0f, -8.0f, 9.0f),
                    MatrixTranslation(3.0f, -4.0f, 5.0f)
                ),
                ::MatrixRotationQuaternion(rotationQuaternion)
            ),
            MatrixTranslation(-3.0f, 4.0f, -5.0f)
        ),
        MatrixScaling(1.5f, 2.0f, 2.5f)
    );
    CheckMatrixRowsNear(context, StoreMatrixRows(affine3dComplex), StoreMatrixRows(affine3dComplexManual), 3.0e-3f, __FILE__, __LINE__);
}

static void TestMatrixTransforms(TestContext& context){
    const SimdMatrix matrix = Compose(Compose(MatrixTranslation(4.0f, -5.0f, 6.0f), MatrixRotationZ(0.5f)), MatrixScaling(2.0f, 3.0f, 4.0f));
    const FloatRows4x4 rows = StoreMatrixRows(matrix);

    const Float4Data v2 = StoreVector(Vector2Transform(MakeVector(1.0f, 2.0f, 0.0f, 1.0f), matrix));
    const Float4Data expectedV2 = TransformRows(rows, 1.0f, 2.0f, 0.0f, 1.0f);
    CheckFloat4Near(context, v2, expectedV2.x, expectedV2.y, expectedV2.z, expectedV2.w, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const Float4Data v2Coord = StoreVector(Vector2TransformCoord(MakeVector(1.0f, 2.0f, 0.0f, 1.0f), matrix));
    CheckFloat4Near(context, v2Coord, expectedV2.x / expectedV2.w, expectedV2.y / expectedV2.w, expectedV2.z / expectedV2.w, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const Float4Data v2Normal = StoreVector(Vector2TransformNormal(MakeVector(1.0f, 2.0f, 0.0f, 0.0f), matrix));
    const Float4Data expectedV2Normal = TransformRows(rows, 1.0f, 2.0f, 0.0f, 0.0f);
    CheckFloat4Near(context, v2Normal, expectedV2Normal.x, expectedV2Normal.y, 0.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const Float4Data v3 = StoreVector(Vector3Transform(MakeVector(1.0f, 2.0f, 3.0f, 1.0f), matrix));
    const Float4Data expectedV3 = TransformRows(rows, 1.0f, 2.0f, 3.0f, 1.0f);
    CheckFloat4Near(context, v3, expectedV3.x, expectedV3.y, expectedV3.z, expectedV3.w, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const Float4Data v3Coord = StoreVector(Vector3TransformCoord(MakeVector(1.0f, 2.0f, 3.0f, 1.0f), matrix));
    CheckFloat4Near(context, v3Coord, expectedV3.x / expectedV3.w, expectedV3.y / expectedV3.w, expectedV3.z / expectedV3.w, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const Float4Data v3Normal = StoreVector(Vector3TransformNormal(MakeVector(1.0f, 2.0f, 3.0f, 0.0f), matrix));
    const Float4Data expectedV3Normal = TransformRows(rows, 1.0f, 2.0f, 3.0f, 0.0f);
    CheckFloat4Near(context, v3Normal, expectedV3Normal.x, expectedV3Normal.y, expectedV3Normal.z, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const Float4Data v4 = StoreVector(Vector4Transform(MakeVector(1.0f, 2.0f, 3.0f, 1.0f), matrix));
    CheckFloat4Near(context, v4, expectedV3.x, expectedV3.y, expectedV3.z, expectedV3.w, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SimdMatrix translated = MatrixTranslation(0.0f, 0.0f, 5.0f);
    SimdVector determinant = SourceMath::VectorZero();
    const SimdMatrix inverseTranspose = MatrixTranspose(MatrixInverse(&determinant, translated));
    const Float4Data transformedPlane = StoreVector(PlaneTransform(MakeVector(0.0f, 0.0f, 1.0f, 0.0f), inverseTranspose));
    CheckFloat4Near(context, transformedPlane, 0.0f, 0.0f, 1.0f, -5.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    context.checkTrue(MatrixIsIdentity(MatrixIdentity()), "MatrixIsIdentity", __FILE__, __LINE__);
    context.checkTrue(!MatrixIsIdentity(matrix), "!MatrixIsIdentity", __FILE__, __LINE__);
    context.checkTrue(MatrixIsNaN(MatrixSet(
        std::numeric_limits<f32>::quiet_NaN(), 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    )), "MatrixIsNaN", __FILE__, __LINE__);
    context.checkTrue(MatrixIsInfinite(MatrixSet(
        std::numeric_limits<f32>::infinity(), 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    )), "MatrixIsInfinite", __FILE__, __LINE__);

    const SimdVector sourceQuaternion = SourceMath::QuaternionRotationRollPitchYaw(0.25f, -0.35f, 0.45f);
    const SimdVector reconstructedQuaternion = QuaternionRotationMatrix(::MatrixRotationQuaternion(sourceQuaternion));
    CheckQuaternionEquivalent(context, sourceQuaternion, reconstructedQuaternion, 2.0e-3f, __FILE__, __LINE__);
}

static void TestMatrixProjectAndStreams(TestContext& context){
    const SimdMatrix world = Compose(MatrixTranslation(2.0f, -1.0f, 5.0f), MatrixRotationY(0.25f));
    const SimdMatrix view = ::MatrixLookAtLH(
        MakeVector(0.0f, 0.0f, -10.0f, 1.0f),
        MakeVector(0.0f, 0.0f, 0.0f, 1.0f),
        MakeVector(0.0f, 1.0f, 0.0f, 0.0f)
    );
    const SimdMatrix projection = MatrixPerspectiveFovLH(1.1f, 16.0f / 9.0f, 0.1f, 100.0f);

    const SimdVector originalPoint = MakeVector(1.5f, -0.25f, 2.0f, 1.0f);
    const SimdVector projected = Vector3Project(originalPoint, 100.0f, 50.0f, 1280.0f, 720.0f, 0.0f, 1.0f, projection, view, world);
    const SimdVector unprojected = Vector3Unproject(projected, 100.0f, 50.0f, 1280.0f, 720.0f, 0.0f, 1.0f, projection, view, world);
    CheckFloat4Near(context, StoreVector(unprojected), 1.5f, -0.25f, 2.0f, 1.0f, 5.0e-3f, __FILE__, __LINE__);

    const Float2Data input2[3] = {
        Float2Data(1.0f, 2.0f),
        Float2Data(-3.0f, 4.0f),
        Float2Data(5.0f, -6.0f)
    };
    Float4Data output2Transform[3]{};
    Float2Data output2Coord[3]{};
    Float2Data output2Normal[3]{};
    Vector2TransformStream(MakeNotNull(output2Transform), sizeof(Float4Data), MakeNotNull(input2), sizeof(Float2Data), 3, world);
    Vector2TransformCoordStream(MakeNotNull(output2Coord), sizeof(Float2Data), MakeNotNull(input2), sizeof(Float2Data), 3, world);
    Vector2TransformNormalStream(MakeNotNull(output2Normal), sizeof(Float2Data), MakeNotNull(input2), sizeof(Float2Data), 3, world);
    for(usize i = 0; i < 3; ++i){
        const SimdVector input = MakeVector(input2[i].x, input2[i].y, 0.0f, 1.0f);
        const Float4Data singleTransform = StoreVector(Vector2Transform(input, world));
        const Float4Data singleCoord = StoreVector(Vector2TransformCoord(input, world));
        const Float4Data singleNormal = StoreVector(Vector2TransformNormal(input, world));
        CheckFloat4Near(context, output2Transform[i], singleTransform.x, singleTransform.y, singleTransform.z, singleTransform.w, s_FloatLooseEpsilon, __FILE__, __LINE__);
        context.checkNearFloat(output2Coord[i].x, singleCoord.x, s_FloatLooseEpsilon, "Vector2TransformCoordStream x", __FILE__, __LINE__);
        context.checkNearFloat(output2Coord[i].y, singleCoord.y, s_FloatLooseEpsilon, "Vector2TransformCoordStream y", __FILE__, __LINE__);
        context.checkNearFloat(output2Normal[i].x, singleNormal.x, s_FloatLooseEpsilon, "Vector2TransformNormalStream x", __FILE__, __LINE__);
        context.checkNearFloat(output2Normal[i].y, singleNormal.y, s_FloatLooseEpsilon, "Vector2TransformNormalStream y", __FILE__, __LINE__);
    }

    alignas(32) const Float2Data packedInput2[4] = {
        Float2Data(1.0f, 2.0f),
        Float2Data(-3.0f, 4.0f),
        Float2Data(5.0f, -6.0f),
        Float2Data(-8.0f, 9.0f)
    };
    alignas(32) Float4Data packedOutput2Transform[4]{};
    alignas(32) Float2Data packedOutput2Coord[4]{};
    alignas(32) Float2Data packedOutput2Normal[4]{};
    struct PaddedFloat4{
        Float4Data value{};
        f32 padding[4]{};
    };
    struct PaddedFloat2{
        Float2Data value{};
        f32 padding[2]{};
    };
    alignas(32) PaddedFloat4 stridedOutput2Transform[4]{};
    alignas(32) PaddedFloat2 stridedOutput2Coord[4]{};
    alignas(32) PaddedFloat2 stridedOutput2Normal[4]{};
    Vector2TransformStream(
        MakeNotNull(packedOutput2Transform),
        sizeof(Float4Data),
        MakeConstNotNull(packedInput2),
        sizeof(Float2Data),
        4,
        world
    );
    Vector2TransformCoordStream(
        MakeNotNull(packedOutput2Coord),
        sizeof(Float2Data),
        MakeConstNotNull(packedInput2),
        sizeof(Float2Data),
        4,
        world
    );
    Vector2TransformNormalStream(
        MakeNotNull(packedOutput2Normal),
        sizeof(Float2Data),
        MakeConstNotNull(packedInput2),
        sizeof(Float2Data),
        4,
        world
    );
    Vector2TransformStream(
        MakeNotNull(&stridedOutput2Transform[0].value),
        sizeof(PaddedFloat4),
        MakeConstNotNull(packedInput2),
        sizeof(Float2Data),
        4,
        world
    );
    Vector2TransformCoordStream(
        MakeNotNull(&stridedOutput2Coord[0].value),
        sizeof(PaddedFloat2),
        MakeConstNotNull(packedInput2),
        sizeof(Float2Data),
        4,
        world
    );
    Vector2TransformNormalStream(
        MakeNotNull(&stridedOutput2Normal[0].value),
        sizeof(PaddedFloat2),
        MakeConstNotNull(packedInput2),
        sizeof(Float2Data),
        4,
        world
    );
    for(usize i = 0; i < 4; ++i){
        const SimdVector input = MakeVector(packedInput2[i].x, packedInput2[i].y, 0.0f, 1.0f);
        const Float4Data singleTransform = StoreVector(Vector2Transform(input, world));
        const Float4Data singleCoord = StoreVector(Vector2TransformCoord(input, world));
        const Float4Data singleNormal = StoreVector(Vector2TransformNormal(input, world));
        CheckFloat4Near(
            context,
            packedOutput2Transform[i],
            singleTransform.x,
            singleTransform.y,
            singleTransform.z,
            singleTransform.w,
            s_FloatLooseEpsilon,
            __FILE__,
            __LINE__
        );
        CheckFloat4Near(
            context,
            stridedOutput2Transform[i].value,
            singleTransform.x,
            singleTransform.y,
            singleTransform.z,
            singleTransform.w,
            s_FloatLooseEpsilon,
            __FILE__,
            __LINE__
        );
        context.checkNearFloat(packedOutput2Coord[i].x, singleCoord.x, s_FloatLooseEpsilon, "Packed Vector2TransformCoordStream x", __FILE__, __LINE__);
        context.checkNearFloat(packedOutput2Coord[i].y, singleCoord.y, s_FloatLooseEpsilon, "Packed Vector2TransformCoordStream y", __FILE__, __LINE__);
        context.checkNearFloat(packedOutput2Normal[i].x, singleNormal.x, s_FloatLooseEpsilon, "Packed Vector2TransformNormalStream x", __FILE__, __LINE__);
        context.checkNearFloat(packedOutput2Normal[i].y, singleNormal.y, s_FloatLooseEpsilon, "Packed Vector2TransformNormalStream y", __FILE__, __LINE__);
        context.checkNearFloat(stridedOutput2Coord[i].value.x, singleCoord.x, s_FloatLooseEpsilon, "Strided Vector2TransformCoordStream x", __FILE__, __LINE__);
        context.checkNearFloat(stridedOutput2Coord[i].value.y, singleCoord.y, s_FloatLooseEpsilon, "Strided Vector2TransformCoordStream y", __FILE__, __LINE__);
        context.checkNearFloat(stridedOutput2Normal[i].value.x, singleNormal.x, s_FloatLooseEpsilon, "Strided Vector2TransformNormalStream x", __FILE__, __LINE__);
        context.checkNearFloat(stridedOutput2Normal[i].value.y, singleNormal.y, s_FloatLooseEpsilon, "Strided Vector2TransformNormalStream y", __FILE__, __LINE__);
    }

    const Float3Data input3[3] = {
        Float3Data(1.0f, 2.0f, 3.0f),
        Float3Data(-3.0f, 4.0f, -5.0f),
        Float3Data(5.0f, -6.0f, 7.0f)
    };
    Float4Data output3Transform[3]{};
    Float3Data output3Coord[3]{};
    Float3Data output3Normal[3]{};
    Float3Data output3Project[3]{};
    Float3Data output3Unproject[3]{};
    Vector3TransformStream(MakeNotNull(output3Transform), sizeof(Float4Data), MakeNotNull(input3), sizeof(Float3Data), 3, world);
    Vector3TransformCoordStream(MakeNotNull(output3Coord), sizeof(Float3Data), MakeNotNull(input3), sizeof(Float3Data), 3, world);
    Vector3TransformNormalStream(MakeNotNull(output3Normal), sizeof(Float3Data), MakeNotNull(input3), sizeof(Float3Data), 3, world);
    Vector3ProjectStream(MakeNotNull(output3Project), sizeof(Float3Data), MakeNotNull(input3), sizeof(Float3Data), 3, 100.0f, 50.0f, 1280.0f, 720.0f, 0.0f, 1.0f, projection, view, world);
    Vector3UnprojectStream(MakeNotNull(output3Unproject), sizeof(Float3Data), MakeConstNotNull(output3Project), sizeof(Float3Data), 3, 100.0f, 50.0f, 1280.0f, 720.0f, 0.0f, 1.0f, projection, view, world);
    for(usize i = 0; i < 3; ++i){
        const SimdVector input = MakeVector(input3[i].x, input3[i].y, input3[i].z, 1.0f);
        const Float4Data singleTransform = StoreVector(Vector3Transform(input, world));
        const Float4Data singleCoord = StoreVector(Vector3TransformCoord(input, world));
        const Float4Data singleNormal = StoreVector(Vector3TransformNormal(input, world));
        const Float4Data singleProject = StoreVector(Vector3Project(input, 100.0f, 50.0f, 1280.0f, 720.0f, 0.0f, 1.0f, projection, view, world));
        CheckFloat4Near(context, output3Transform[i], singleTransform.x, singleTransform.y, singleTransform.z, singleTransform.w, s_FloatLooseEpsilon, __FILE__, __LINE__);
        context.checkNearFloat(output3Coord[i].x, singleCoord.x, s_FloatLooseEpsilon, "Vector3TransformCoordStream x", __FILE__, __LINE__);
        context.checkNearFloat(output3Coord[i].y, singleCoord.y, s_FloatLooseEpsilon, "Vector3TransformCoordStream y", __FILE__, __LINE__);
        context.checkNearFloat(output3Coord[i].z, singleCoord.z, s_FloatLooseEpsilon, "Vector3TransformCoordStream z", __FILE__, __LINE__);
        context.checkNearFloat(output3Normal[i].x, singleNormal.x, s_FloatLooseEpsilon, "Vector3TransformNormalStream x", __FILE__, __LINE__);
        context.checkNearFloat(output3Normal[i].y, singleNormal.y, s_FloatLooseEpsilon, "Vector3TransformNormalStream y", __FILE__, __LINE__);
        context.checkNearFloat(output3Normal[i].z, singleNormal.z, s_FloatLooseEpsilon, "Vector3TransformNormalStream z", __FILE__, __LINE__);
        context.checkNearFloat(output3Project[i].x, singleProject.x, s_FloatLooseEpsilon, "Vector3ProjectStream x", __FILE__, __LINE__);
        context.checkNearFloat(output3Project[i].y, singleProject.y, s_FloatLooseEpsilon, "Vector3ProjectStream y", __FILE__, __LINE__);
        context.checkNearFloat(output3Project[i].z, singleProject.z, s_FloatLooseEpsilon, "Vector3ProjectStream z", __FILE__, __LINE__);
        context.checkNearFloat(output3Unproject[i].x, input3[i].x, 5.0e-3f, "Vector3UnprojectStream x", __FILE__, __LINE__);
        context.checkNearFloat(output3Unproject[i].y, input3[i].y, 5.0e-3f, "Vector3UnprojectStream y", __FILE__, __LINE__);
        context.checkNearFloat(output3Unproject[i].z, input3[i].z, 5.0e-3f, "Vector3UnprojectStream z", __FILE__, __LINE__);
    }

    alignas(32) const Float3Data packedInput3[4] = {
        Float3Data(1.0f, 2.0f, 3.0f),
        Float3Data(-3.0f, 4.0f, -5.0f),
        Float3Data(5.0f, -6.0f, 7.0f),
        Float3Data(-8.0f, 9.0f, 10.0f)
    };
    alignas(32) Float4Data packedOutput3Transform[4]{};
    alignas(32) Float3Data packedOutput3Coord[4]{};
    alignas(32) Float3Data packedOutput3Normal[4]{};
    Vector3TransformStream(
        MakeNotNull(packedOutput3Transform),
        sizeof(Float4Data),
        MakeConstNotNull(packedInput3),
        sizeof(Float3Data),
        4,
        world
    );
    Vector3TransformCoordStream(
        MakeNotNull(packedOutput3Coord),
        sizeof(Float3Data),
        MakeConstNotNull(packedInput3),
        sizeof(Float3Data),
        4,
        world
    );
    Vector3TransformNormalStream(
        MakeNotNull(packedOutput3Normal),
        sizeof(Float3Data),
        MakeConstNotNull(packedInput3),
        sizeof(Float3Data),
        4,
        world
    );
    for(usize i = 0; i < 4; ++i){
        const SimdVector input = MakeVector(packedInput3[i].x, packedInput3[i].y, packedInput3[i].z, 1.0f);
        const Float4Data singleTransform = StoreVector(Vector3Transform(input, world));
        const Float4Data singleCoord = StoreVector(Vector3TransformCoord(input, world));
        const Float4Data singleNormal = StoreVector(Vector3TransformNormal(input, world));
        CheckFloat4Near(
            context,
            packedOutput3Transform[i],
            singleTransform.x,
            singleTransform.y,
            singleTransform.z,
            singleTransform.w,
            s_FloatLooseEpsilon,
            __FILE__,
            __LINE__
        );
        context.checkNearFloat(packedOutput3Coord[i].x, singleCoord.x, s_FloatLooseEpsilon, "Packed Vector3TransformCoordStream x", __FILE__, __LINE__);
        context.checkNearFloat(packedOutput3Coord[i].y, singleCoord.y, s_FloatLooseEpsilon, "Packed Vector3TransformCoordStream y", __FILE__, __LINE__);
        context.checkNearFloat(packedOutput3Coord[i].z, singleCoord.z, s_FloatLooseEpsilon, "Packed Vector3TransformCoordStream z", __FILE__, __LINE__);
        context.checkNearFloat(packedOutput3Normal[i].x, singleNormal.x, s_FloatLooseEpsilon, "Packed Vector3TransformNormalStream x", __FILE__, __LINE__);
        context.checkNearFloat(packedOutput3Normal[i].y, singleNormal.y, s_FloatLooseEpsilon, "Packed Vector3TransformNormalStream y", __FILE__, __LINE__);
        context.checkNearFloat(packedOutput3Normal[i].z, singleNormal.z, s_FloatLooseEpsilon, "Packed Vector3TransformNormalStream z", __FILE__, __LINE__);
    }

    const Float4Data input4[3] = {
        Float4Data(1.0f, 2.0f, 3.0f, 1.0f),
        Float4Data(-3.0f, 4.0f, -5.0f, 1.0f),
        Float4Data(5.0f, -6.0f, 7.0f, 1.0f)
    };
    Float4Data output4Transform[3]{};
    Vector4TransformStream(MakeNotNull(output4Transform), sizeof(Float4Data), MakeNotNull(input4), sizeof(Float4Data), 3, world);
    for(usize i = 0; i < 3; ++i){
        const SimdVector input = MakeVector(input4[i].x, input4[i].y, input4[i].z, input4[i].w);
        const Float4Data singleTransform = StoreVector(Vector4Transform(input, world));
        CheckFloat4Near(context, output4Transform[i], singleTransform.x, singleTransform.y, singleTransform.z, singleTransform.w, s_FloatLooseEpsilon, __FILE__, __LINE__);
    }

    const Float4Data planeInput[2] = {
        Float4Data(0.0f, 1.0f, 0.0f, 0.0f),
        Float4Data(0.0f, 0.0f, 1.0f, 0.0f)
    };
    Float4Data planeOutput[2]{};
    SimdVector determinant = SourceMath::VectorZero();
    const SimdMatrix planeItm = MatrixTranspose(MatrixInverse(&determinant, MatrixTranslation(3.0f, 4.0f, 5.0f)));
    PlaneTransformStream(MakeNotNull(planeOutput), sizeof(Float4Data), MakeNotNull(planeInput), sizeof(Float4Data), 2, planeItm);
    for(usize i = 0; i < 2; ++i){
        const SimdVector input = MakeVector(planeInput[i].x, planeInput[i].y, planeInput[i].z, planeInput[i].w);
        const Float4Data singleTransform = StoreVector(PlaneTransform(input, planeItm));
        CheckFloat4Near(context, planeOutput[i], singleTransform.x, singleTransform.y, singleTransform.z, singleTransform.w, s_FloatLooseEpsilon, __FILE__, __LINE__);
    }
}

static void TestSourceMathConversionsAndLoads(TestContext& context){
    context.checkTrue(SourceMath::VerifyCPUSupport(), "VerifyCPUSupport", __FILE__, __LINE__);

    const u32 rawScalarInt = 0x12345678u;
    u32 storedScalarInt = 0;
    SourceMath::StoreInt(&storedScalarInt, SourceMath::LoadInt(&rawScalarInt));
    context.checkTrue(storedScalarInt == rawScalarInt, "StoreInt", __FILE__, __LINE__);

    const f32 rawScalarFloat = -3.25f;
    f32 storedScalarFloat = 0.0f;
    SourceMath::StoreFloat(&storedScalarFloat, SourceMath::LoadFloat(&rawScalarFloat));
    context.checkNearFloat(storedScalarFloat, rawScalarFloat, s_FloatEpsilon, "StoreFloat", __FILE__, __LINE__);

    const u32 rawInt2[2] = { 3u, 7u };
    u32 storedInt2[2] = {};
    SourceMath::StoreInt2(storedInt2, SourceMath::LoadInt2(rawInt2));
    context.checkTrue(storedInt2[0] == rawInt2[0], "StoreInt2 x", __FILE__, __LINE__);
    context.checkTrue(storedInt2[1] == rawInt2[1], "StoreInt2 y", __FILE__, __LINE__);

    alignas(16) u32 rawInt4A[4] = { 9u, 11u, 13u, 15u };
    u32 storedInt4A[4] = {};
    SourceMath::StoreInt4A(storedInt4A, SourceMath::LoadInt4A(rawInt4A));
    for(usize i = 0; i < 4; ++i)
        context.checkTrue(storedInt4A[i] == rawInt4A[i], "StoreInt4A", __FILE__, __LINE__);

    const SourceMath::Float2 float2(1.25f, -2.5f);
    SourceMath::Float2 storedFloat2{};
    SourceMath::StoreFloat2(&storedFloat2, SourceMath::LoadFloat2(&float2));
    context.checkNearFloat(storedFloat2.x, float2.x, s_FloatEpsilon, "StoreFloat2 x", __FILE__, __LINE__);
    context.checkNearFloat(storedFloat2.y, float2.y, s_FloatEpsilon, "StoreFloat2 y", __FILE__, __LINE__);

    const SourceMath::Float3 float3(2.0f, -4.0f, 6.0f);
    SourceMath::Float3 storedFloat3{};
    SourceMath::StoreFloat3(&storedFloat3, SourceMath::LoadFloat3(&float3));
    context.checkNearFloat(storedFloat3.x, float3.x, s_FloatEpsilon, "StoreFloat3 x", __FILE__, __LINE__);
    context.checkNearFloat(storedFloat3.y, float3.y, s_FloatEpsilon, "StoreFloat3 y", __FILE__, __LINE__);
    context.checkNearFloat(storedFloat3.z, float3.z, s_FloatEpsilon, "StoreFloat3 z", __FILE__, __LINE__);

    const SourceMath::Float4 float4(1.0f, -2.0f, 3.0f, -4.0f);
    SourceMath::Float4 storedFloat4{};
    SourceMath::StoreFloat4(&storedFloat4, SourceMath::LoadFloat4(&float4));
    CheckFloat4Near(context, storedFloat4, float4.x, float4.y, float4.z, float4.w, s_FloatEpsilon, __FILE__, __LINE__);

    alignas(16) SourceMath::AlignedFloat2 alignedFloat2(6.5f, -7.5f);
    SourceMath::AlignedFloat2 storedAlignedFloat2{};
    SourceMath::StoreFloat2A(&storedAlignedFloat2, SourceMath::LoadFloat2A(&alignedFloat2));
    context.checkNearFloat(storedAlignedFloat2.x, alignedFloat2.x, s_FloatEpsilon, "StoreFloat2A x", __FILE__, __LINE__);
    context.checkNearFloat(storedAlignedFloat2.y, alignedFloat2.y, s_FloatEpsilon, "StoreFloat2A y", __FILE__, __LINE__);

    alignas(16) SourceMath::AlignedFloat3 alignedFloat3(-1.5f, 2.5f, -3.5f);
    SourceMath::AlignedFloat3 storedAlignedFloat3{};
    SourceMath::StoreFloat3A(&storedAlignedFloat3, SourceMath::LoadFloat3A(&alignedFloat3));
    context.checkNearFloat(storedAlignedFloat3.x, alignedFloat3.x, s_FloatEpsilon, "StoreFloat3A x", __FILE__, __LINE__);
    context.checkNearFloat(storedAlignedFloat3.y, alignedFloat3.y, s_FloatEpsilon, "StoreFloat3A y", __FILE__, __LINE__);
    context.checkNearFloat(storedAlignedFloat3.z, alignedFloat3.z, s_FloatEpsilon, "StoreFloat3A z", __FILE__, __LINE__);

    alignas(16) SourceMath::AlignedFloat4 alignedFloat4(8.0f, -6.0f, 4.0f, -2.0f);
    SourceMath::AlignedFloat4 storedAlignedFloat4{};
    SourceMath::StoreFloat4A(&storedAlignedFloat4, SourceMath::LoadFloat4A(&alignedFloat4));
    CheckFloat4Near(context, storedAlignedFloat4, alignedFloat4.x, alignedFloat4.y, alignedFloat4.z, alignedFloat4.w, s_FloatEpsilon, __FILE__, __LINE__);

    const SourceMath::Int2 sint2(-3, 7);
    SourceMath::Int2 storedSInt2{};
    SourceMath::StoreSInt2(&storedSInt2, SourceMath::LoadSInt2(&sint2));
    context.checkTrue(storedSInt2.x == sint2.x, "StoreSInt2 x", __FILE__, __LINE__);
    context.checkTrue(storedSInt2.y == sint2.y, "StoreSInt2 y", __FILE__, __LINE__);

    const SourceMath::UInt2 uint2(5u, 9u);
    SourceMath::UInt2 storedUInt2{};
    SourceMath::StoreUInt2(&storedUInt2, SourceMath::LoadUInt2(&uint2));
    context.checkTrue(storedUInt2.x == uint2.x, "StoreUInt2 x", __FILE__, __LINE__);
    context.checkTrue(storedUInt2.y == uint2.y, "StoreUInt2 y", __FILE__, __LINE__);

    const SourceMath::Int3 sint3(-4, 8, -12);
    SourceMath::Int3 storedSInt3{};
    SourceMath::StoreSInt3(&storedSInt3, SourceMath::LoadSInt3(&sint3));
    context.checkTrue(storedSInt3.x == sint3.x, "StoreSInt3 x", __FILE__, __LINE__);
    context.checkTrue(storedSInt3.y == sint3.y, "StoreSInt3 y", __FILE__, __LINE__);
    context.checkTrue(storedSInt3.z == sint3.z, "StoreSInt3 z", __FILE__, __LINE__);

    const SourceMath::UInt3 uint3(10u, 20u, 30u);
    SourceMath::UInt3 storedUInt3{};
    SourceMath::StoreUInt3(&storedUInt3, SourceMath::LoadUInt3(&uint3));
    context.checkTrue(storedUInt3.x == uint3.x, "StoreUInt3 x", __FILE__, __LINE__);
    context.checkTrue(storedUInt3.y == uint3.y, "StoreUInt3 y", __FILE__, __LINE__);
    context.checkTrue(storedUInt3.z == uint3.z, "StoreUInt3 z", __FILE__, __LINE__);

    const SourceMath::Int4 sint4(-1, 2, -3, 4);
    SourceMath::Int4 storedSInt4{};
    SourceMath::StoreSInt4(&storedSInt4, SourceMath::LoadSInt4(&sint4));
    context.checkTrue(storedSInt4.x == sint4.x, "StoreSInt4 x", __FILE__, __LINE__);
    context.checkTrue(storedSInt4.y == sint4.y, "StoreSInt4 y", __FILE__, __LINE__);
    context.checkTrue(storedSInt4.z == sint4.z, "StoreSInt4 z", __FILE__, __LINE__);
    context.checkTrue(storedSInt4.w == sint4.w, "StoreSInt4 w", __FILE__, __LINE__);

    const SourceMath::UInt4 uint4(100u, 200u, 300u, 400u);
    SourceMath::UInt4 storedUInt4{};
    SourceMath::StoreUInt4(&storedUInt4, SourceMath::LoadUInt4(&uint4));
    context.checkTrue(storedUInt4.x == uint4.x, "StoreUInt4 x", __FILE__, __LINE__);
    context.checkTrue(storedUInt4.y == uint4.y, "StoreUInt4 y", __FILE__, __LINE__);
    context.checkTrue(storedUInt4.z == uint4.z, "StoreUInt4 z", __FILE__, __LINE__);
    context.checkTrue(storedUInt4.w == uint4.w, "StoreUInt4 w", __FILE__, __LINE__);

    const RawUInt4 convertedSigned = StoreRawVector(SourceMath::ConvertVectorFloatToInt(MakeVector(1.5f, -2.0f, 3.25f, 4.75f), 1));
    CheckRawSInt4Equal(context, convertedSigned, 3, -4, 6, 9, __FILE__, __LINE__);

    CheckVectorNear(
        context,
        SourceMath::ConvertVectorIntToFloat(SourceMath::VectorSetInt(static_cast<u32>(-8), 16u, 24u, 32u), 3),
        -1.0f,
        2.0f,
        3.0f,
        4.0f,
        s_FloatEpsilon,
        __FILE__,
        __LINE__
    );

    CheckVectorNear(
        context,
        SourceMath::ConvertVectorUIntToFloat(SourceMath::VectorSetInt(8u, 16u, 24u, 32u), 3),
        1.0f,
        2.0f,
        3.0f,
        4.0f,
        s_FloatEpsilon,
        __FILE__,
        __LINE__
    );

    const RawUInt4 convertedUnsigned = StoreRawVector(SourceMath::ConvertVectorFloatToUInt(MakeVector(-1.0f, 2.5f, 3.25f, 4.75f), 1));
    CheckRawUInt4Equal(context, convertedUnsigned, 0u, 5u, 6u, 9u, __FILE__, __LINE__);
}

static void TestSourceMathGeneralVectors(TestContext& context){
    const SimdVector base = MakeVector(1.0f, 2.0f, 3.0f, 4.0f);
    const SimdVector other = MakeVector(5.0f, 6.0f, 7.0f, 8.0f);

    CheckVectorNear(context, SourceMath::VectorZero(), 0.0f, 0.0f, 0.0f, 0.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorReplicate(2.5f), 2.5f, 2.5f, 2.5f, 2.5f, s_FloatEpsilon, __FILE__, __LINE__);
    const f32 replicatedValue = -7.0f;
    CheckVectorNear(context, SourceMath::VectorReplicatePtr(&replicatedValue), -7.0f, -7.0f, -7.0f, -7.0f, s_FloatEpsilon, __FILE__, __LINE__);

    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorSetInt(1u, 2u, 3u, 4u)), 1u, 2u, 3u, 4u, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorReplicateInt(0x11223344u)), 0x11223344u, 0x11223344u, 0x11223344u, 0x11223344u, __FILE__, __LINE__);
    const u32 replicatedIntValue = 0x55667788u;
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorReplicateIntPtr(&replicatedIntValue)), replicatedIntValue, replicatedIntValue, replicatedIntValue, replicatedIntValue, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorTrueInt()), 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorFalseInt()), 0u, 0u, 0u, 0u, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorSplatX(base), 1.0f, 1.0f, 1.0f, 1.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorSplatY(base), 2.0f, 2.0f, 2.0f, 2.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorSplatZ(base), 3.0f, 3.0f, 3.0f, 3.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorSplatW(base), 4.0f, 4.0f, 4.0f, 4.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorSplatOne(), 1.0f, 1.0f, 1.0f, 1.0f, s_FloatEpsilon, __FILE__, __LINE__);
    context.checkTrue(std::isinf(StoreVector(SourceMath::VectorSplatInfinity()).x), "VectorSplatInfinity", __FILE__, __LINE__);
    context.checkTrue(std::isnan(StoreVector(SourceMath::VectorSplatQNaN()).x), "VectorSplatQNaN", __FILE__, __LINE__);
    context.checkTrue(StoreVector(SourceMath::VectorSplatEpsilon()).x > 0.0f, "VectorSplatEpsilon", __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorSplatSignMask()), 0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u, __FILE__, __LINE__);

    context.checkNearFloat(SourceMath::VectorGetByIndex(base, 2), 3.0f, s_FloatEpsilon, "VectorGetByIndex", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(base), 1.0f, s_FloatEpsilon, "VectorGetX", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetY(base), 2.0f, s_FloatEpsilon, "VectorGetY", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetZ(base), 3.0f, s_FloatEpsilon, "VectorGetZ", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetW(base), 4.0f, s_FloatEpsilon, "VectorGetW", __FILE__, __LINE__);

    f32 indexedFloat = 0.0f;
    SourceMath::VectorGetByIndexPtr(&indexedFloat, base, 1);
    context.checkNearFloat(indexedFloat, 2.0f, s_FloatEpsilon, "VectorGetByIndexPtr", __FILE__, __LINE__);

    const SimdVector integerVector = SourceMath::VectorSetInt(9u, 10u, 11u, 12u);
    context.checkTrue(SourceMath::VectorGetIntByIndex(integerVector, 3) == 12u, "VectorGetIntByIndex", __FILE__, __LINE__);
    context.checkTrue(SourceMath::VectorGetIntX(integerVector) == 9u, "VectorGetIntX", __FILE__, __LINE__);
    context.checkTrue(SourceMath::VectorGetIntY(integerVector) == 10u, "VectorGetIntY", __FILE__, __LINE__);
    context.checkTrue(SourceMath::VectorGetIntZ(integerVector) == 11u, "VectorGetIntZ", __FILE__, __LINE__);
    context.checkTrue(SourceMath::VectorGetIntW(integerVector) == 12u, "VectorGetIntW", __FILE__, __LINE__);

    u32 indexedInt = 0u;
    SourceMath::VectorGetIntByIndexPtr(&indexedInt, integerVector, 2);
    context.checkTrue(indexedInt == 11u, "VectorGetIntByIndexPtr", __FILE__, __LINE__);

    const f32 insertedFloat = -9.0f;
    const SimdVector floatSet = SourceMath::VectorSetWPtr(
        SourceMath::VectorSetZ(
            SourceMath::VectorSetY(
                SourceMath::VectorSetX(
                    SourceMath::VectorSetByIndex(base, 8.0f, 1),
                    -1.0f
                ),
                -2.0f
            ),
            -3.0f
        ),
        &insertedFloat
    );
    CheckVectorNear(context, floatSet, -1.0f, -2.0f, -3.0f, -9.0f, s_FloatEpsilon, __FILE__, __LINE__);

    const u32 insertedInt = 0xdeadbeefu;
    const SimdVector intSet = SourceMath::VectorSetIntWPtr(
        SourceMath::VectorSetIntZ(
            SourceMath::VectorSetIntY(
                SourceMath::VectorSetIntX(
                    SourceMath::VectorSetIntByIndex(integerVector, 42u, 1),
                    77u
                ),
                88u
            ),
            99u
        ),
        &insertedInt
    );
    CheckRawUInt4Equal(context, StoreRawVector(intSet), 77u, 88u, 99u, insertedInt, __FILE__, __LINE__);

    CheckVectorNear(context, SourceMath::VectorSwizzle(base, 3, 2, 1, 0), 4.0f, 3.0f, 2.0f, 1.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorPermute(base, other, 0, 5, 2, 7), 1.0f, 6.0f, 3.0f, 8.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorSelectControl(0, 1, 0, 1)), 0u, 0xffffffffu, 0u, 0xffffffffu, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorSelect(base, other, SourceMath::VectorSelectControl(0, 1, 0, 1)), 1.0f, 6.0f, 3.0f, 8.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorMergeXY(base, other), 1.0f, 5.0f, 2.0f, 6.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorMergeZW(base, other), 3.0f, 7.0f, 4.0f, 8.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorShiftLeft(base, other, 1), 2.0f, 3.0f, 4.0f, 5.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorRotateLeft(base, 1), 2.0f, 3.0f, 4.0f, 1.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorRotateRight(base, 1), 4.0f, 1.0f, 2.0f, 3.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorInsert(base, other, 1, 0, 1, 1, 0), 1.0f, 7.0f, 8.0f, 4.0f, s_FloatEpsilon, __FILE__, __LINE__);

    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorEqual(base, MakeVector(1.0f, 0.0f, 3.0f, 9.0f))), 0xffffffffu, 0u, 0xffffffffu, 0u, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorNearEqual(base, MakeVector(1.0f + 1.0e-5f, 2.0f - 1.0e-5f, 3.0f, 4.0f), SourceMath::VectorReplicate(1.0e-4f))), 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorGreater(base, MakeVector(0.0f, 3.0f, 2.0f, 5.0f))), 0xffffffffu, 0u, 0xffffffffu, 0u, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorLess(base, MakeVector(2.0f, 3.0f, 4.0f, 5.0f))), 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorInBounds(MakeVector(0.5f, -1.0f, 2.0f, -3.0f), MakeVector(1.0f, 1.0f, 2.0f, 3.0f))), 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorIsNaN(MakeVector(std::numeric_limits<f32>::quiet_NaN(), 0.0f, std::numeric_limits<f32>::quiet_NaN(), 1.0f))), 0xffffffffu, 0u, 0xffffffffu, 0u, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorIsInfinite(MakeVector(std::numeric_limits<f32>::infinity(), 0.0f, -std::numeric_limits<f32>::infinity(), 1.0f))), 0xffffffffu, 0u, 0xffffffffu, 0u, __FILE__, __LINE__);

    u32 cr = 0u;
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorEqualR(&cr, base, base)), 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, __FILE__, __LINE__);
    context.checkTrue(SourceMath::ComparisonAllTrue(cr), "VectorEqualR all true", __FILE__, __LINE__);

    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorGreaterR(&cr, base, MakeVector(0.0f, 3.0f, 2.0f, 5.0f))), 0xffffffffu, 0u, 0xffffffffu, 0u, __FILE__, __LINE__);
    context.checkTrue(SourceMath::ComparisonMixed(cr), "VectorGreaterR mixed", __FILE__, __LINE__);

    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorInBoundsR(&cr, MakeVector(0.5f, -1.0f, 2.0f, -3.0f), MakeVector(1.0f, 1.0f, 2.0f, 3.0f))), 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, __FILE__, __LINE__);
    context.checkTrue(SourceMath::ComparisonAllInBounds(cr), "VectorInBoundsR all in bounds", __FILE__, __LINE__);

    CheckVectorNear(context, SourceMath::VectorMin(base, other), 1.0f, 2.0f, 3.0f, 4.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorMax(base, other), 5.0f, 6.0f, 7.0f, 8.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorRound(MakeVector(1.2f, 1.7f, -1.2f, -1.7f)), 1.0f, 2.0f, -1.0f, -2.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorTruncate(MakeVector(1.9f, -1.9f, 2.1f, -2.1f)), 1.0f, -1.0f, 2.0f, -2.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorFloor(MakeVector(1.9f, -1.1f, 2.1f, -2.9f)), 1.0f, -2.0f, 2.0f, -3.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorCeiling(MakeVector(1.1f, -1.9f, 2.1f, -2.1f)), 2.0f, -1.0f, 3.0f, -2.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorClamp(MakeVector(-2.0f, -0.5f, 0.5f, 2.0f), MakeVector(-1.0f, -1.0f, -1.0f, -1.0f), MakeVector(1.0f, 1.0f, 1.0f, 1.0f)), -1.0f, -0.5f, 0.5f, 1.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorSaturate(MakeVector(-1.0f, 0.25f, 0.75f, 2.0f)), 0.0f, 0.25f, 0.75f, 1.0f, s_FloatEpsilon, __FILE__, __LINE__);

    const SimdVector bitLhs = SourceMath::VectorSetInt(0x0f0f0f0fu, 0xaaaaaaaau, 0x12345678u, 0xffffffffu);
    const SimdVector bitRhs = SourceMath::VectorSetInt(0xf0f0f0f0u, 0x55555555u, 0x00ff00ffu, 0x0f0f0f0fu);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorAndInt(bitLhs, bitRhs)), 0u, 0u, 0x00340078u, 0x0f0f0f0fu, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorAndCInt(bitLhs, bitRhs)), 0x0f0f0f0fu, 0xaaaaaaaau, 0x12005600u, 0xf0f0f0f0u, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorOrInt(bitLhs, bitRhs)), 0xffffffffu, 0xffffffffu, 0x12ff56ffu, 0xffffffffu, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorNorInt(bitLhs, bitRhs)), 0u, 0u, 0xed00a900u, 0u, __FILE__, __LINE__);
    CheckRawUInt4Equal(context, StoreRawVector(SourceMath::VectorXorInt(bitLhs, bitRhs)), 0xffffffffu, 0xffffffffu, 0x12cb5687u, 0xf0f0f0f0u, __FILE__, __LINE__);

    CheckVectorNear(context, SourceMath::VectorNegate(base), -1.0f, -2.0f, -3.0f, -4.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorAdd(base, other), 6.0f, 8.0f, 10.0f, 12.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorSum(base), 10.0f, 10.0f, 10.0f, 10.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorSubtract(other, base), 4.0f, 4.0f, 4.0f, 4.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorMultiply(base, other), 5.0f, 12.0f, 21.0f, 32.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorMultiplyAdd(base, other, MakeVector(1.0f, 1.0f, 1.0f, 1.0f)), 6.0f, 13.0f, 22.0f, 33.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorDivide(other, base), 5.0f, 3.0f, 7.0f / 3.0f, 2.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorNegativeMultiplySubtract(base, other, MakeVector(100.0f, 100.0f, 100.0f, 100.0f)), 95.0f, 88.0f, 79.0f, 68.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorScale(base, 0.5f), 0.5f, 1.0f, 1.5f, 2.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorReciprocal(MakeVector(2.0f, 4.0f, 8.0f, 16.0f)), 0.5f, 0.25f, 0.125f, 0.0625f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorSqrt(MakeVector(1.0f, 4.0f, 9.0f, 16.0f)), 1.0f, 2.0f, 3.0f, 4.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorReciprocalSqrt(MakeVector(1.0f, 4.0f, 9.0f, 16.0f)), 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 2.0e-3f, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorAbs(MakeVector(-1.0f, 2.0f, -3.0f, 4.0f)), 1.0f, 2.0f, 3.0f, 4.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorMod(MakeVector(5.5f, 7.25f, -5.5f, -7.25f), MakeVector(2.0f, 2.0f, 2.0f, 2.0f)), 1.5f, 1.25f, -1.5f, -1.25f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorSin(MakeVector(0.0f, SourceMath::ConvertToRadians(30.0f), SourceMath::ConvertToRadians(90.0f), SourceMath::ConvertToRadians(-30.0f))), 0.0f, 0.5f, 1.0f, -0.5f, 2.0e-3f, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorCos(MakeVector(0.0f, SourceMath::ConvertToRadians(60.0f), SourceMath::ConvertToRadians(90.0f), SourceMath::ConvertToRadians(180.0f))), 1.0f, 0.5f, 0.0f, -1.0f, 2.0e-3f, __FILE__, __LINE__);

    SimdVector sine{};
    SimdVector cosine{};
    SourceMath::VectorSinCos(&sine, &cosine, MakeVector(0.0f, SourceMath::ConvertToRadians(30.0f), SourceMath::ConvertToRadians(60.0f), SourceMath::ConvertToRadians(90.0f)));
    CheckVectorNear(context, sine, 0.0f, 0.5f, 0.8660254f, 1.0f, 3.0e-3f, __FILE__, __LINE__);
    CheckVectorNear(context, cosine, 1.0f, 0.8660254f, 0.5f, 0.0f, 3.0e-3f, __FILE__, __LINE__);

    CheckVectorNear(context, SourceMath::VectorExp2(MakeVector(0.0f, 1.0f, 2.0f, 3.0f)), 1.0f, 2.0f, 4.0f, 8.0f, 3.0e-3f, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorLog2(MakeVector(1.0f, 2.0f, 4.0f, 8.0f)), 0.0f, 1.0f, 2.0f, 3.0f, 3.0e-3f, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorPow(MakeVector(2.0f, 3.0f, 4.0f, 5.0f), MakeVector(3.0f, 2.0f, 0.5f, 1.0f)), 8.0f, 9.0f, 2.0f, 5.0f, 3.0e-3f, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorLerp(base, other, 0.25f), 2.0f, 3.0f, 4.0f, 5.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorLerpV(base, other, MakeVector(0.25f, 0.5f, 0.75f, 1.0f)), 2.0f, 4.0f, 6.0f, 8.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorHermite(base, MakeVector(9.0f, 9.0f, 9.0f, 9.0f), other, MakeVector(-3.0f, -3.0f, -3.0f, -3.0f), 0.0f), 1.0f, 2.0f, 3.0f, 4.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorHermite(base, MakeVector(9.0f, 9.0f, 9.0f, 9.0f), other, MakeVector(-3.0f, -3.0f, -3.0f, -3.0f), 1.0f), 5.0f, 6.0f, 7.0f, 8.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorCatmullRom(MakeVector(-1.0f, -1.0f, -1.0f, -1.0f), base, other, MakeVector(9.0f, 9.0f, 9.0f, 9.0f), 0.0f), 1.0f, 2.0f, 3.0f, 4.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorCatmullRom(MakeVector(-1.0f, -1.0f, -1.0f, -1.0f), base, other, MakeVector(9.0f, 9.0f, 9.0f, 9.0f), 1.0f), 5.0f, 6.0f, 7.0f, 8.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorBaryCentric(base, other, MakeVector(9.0f, 10.0f, 11.0f, 12.0f), 0.0f, 0.0f), 1.0f, 2.0f, 3.0f, 4.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorBaryCentric(base, other, MakeVector(9.0f, 10.0f, 11.0f, 12.0f), 1.0f, 0.0f), 5.0f, 6.0f, 7.0f, 8.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::VectorBaryCentric(base, other, MakeVector(9.0f, 10.0f, 11.0f, 12.0f), 0.0f, 1.0f), 9.0f, 10.0f, 11.0f, 12.0f, s_FloatEpsilon, __FILE__, __LINE__);
}

static void TestSourceMathVectorFamilies(TestContext& context){
    const SimdVector vector2 = MakeVector(3.0f, 4.0f, 0.0f, 0.0f);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector2Length(vector2)), 5.0f, s_FloatLooseEpsilon, "Vector2Length", __FILE__, __LINE__);
    const SourceMath::Float4 normalized2 = StoreVector(SourceMath::Vector2Normalize(vector2));
    context.checkNearFloat(normalized2.x, 0.6f, s_FloatLooseEpsilon, "Vector2Normalize x", __FILE__, __LINE__);
    context.checkNearFloat(normalized2.y, 0.8f, s_FloatLooseEpsilon, "Vector2Normalize y", __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::Vector2Cross(MakeVector(1.0f, 0.0f, 0.0f, 0.0f), MakeVector(0.0f, 1.0f, 0.0f, 0.0f)), 1.0f, 1.0f, 1.0f, 1.0f, s_FloatEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::Vector2Reflect(MakeVector(1.0f, -1.0f, 0.0f, 0.0f), MakeVector(0.0f, 1.0f, 0.0f, 0.0f)), 1.0f, 1.0f, 0.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector2AngleBetweenNormals(MakeVector(1.0f, 0.0f, 0.0f, 0.0f), MakeVector(0.0f, 1.0f, 0.0f, 0.0f))), SourceMath::MATH_PIDIV2, 3.0e-3f, "Vector2AngleBetweenNormals", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector2LinePointDistance(MakeVector(0.0f, 0.0f, 0.0f, 0.0f), MakeVector(2.0f, 0.0f, 0.0f, 0.0f), MakeVector(1.0f, 1.0f, 0.0f, 0.0f))), 1.0f, s_FloatLooseEpsilon, "Vector2LinePointDistance", __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::Vector2IntersectLine(MakeVector(0.0f, 0.0f, 0.0f, 0.0f), MakeVector(2.0f, 0.0f, 0.0f, 0.0f), MakeVector(1.0f, -1.0f, 0.0f, 0.0f), MakeVector(1.0f, 1.0f, 0.0f, 0.0f)), 1.0f, 0.0f, 0.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    const SimdVector orthogonal2 = SourceMath::Vector2Orthogonal(MakeVector(2.0f, 3.0f, 0.0f, 0.0f));
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector2Dot(orthogonal2, MakeVector(2.0f, 3.0f, 0.0f, 0.0f))), 0.0f, s_FloatLooseEpsilon, "Vector2Orthogonal", __FILE__, __LINE__);

    const SimdVector vector3 = MakeVector(1.0f, 2.0f, 2.0f, 0.0f);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector3Length(vector3)), 3.0f, s_FloatLooseEpsilon, "Vector3Length", __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::Vector3Cross(MakeVector(1.0f, 0.0f, 0.0f, 0.0f), MakeVector(0.0f, 1.0f, 0.0f, 0.0f)), 0.0f, 0.0f, 1.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::Vector3Reflect(MakeVector(1.0f, -1.0f, 0.0f, 0.0f), MakeVector(0.0f, 1.0f, 0.0f, 0.0f)), 1.0f, 1.0f, 0.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector3AngleBetweenNormals(MakeVector(1.0f, 0.0f, 0.0f, 0.0f), MakeVector(0.0f, 1.0f, 0.0f, 0.0f))), SourceMath::MATH_PIDIV2, 3.0e-3f, "Vector3AngleBetweenNormals", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector3LinePointDistance(MakeVector(0.0f, 0.0f, 0.0f, 0.0f), MakeVector(0.0f, 0.0f, 2.0f, 0.0f), MakeVector(3.0f, 4.0f, 1.0f, 0.0f))), 5.0f, s_FloatLooseEpsilon, "Vector3LinePointDistance", __FILE__, __LINE__);
    const SimdVector orthogonal3 = SourceMath::Vector3Orthogonal(MakeVector(2.0f, 3.0f, 4.0f, 0.0f));
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector3Dot(orthogonal3, MakeVector(2.0f, 3.0f, 4.0f, 0.0f))), 0.0f, 3.0e-3f, "Vector3Orthogonal", __FILE__, __LINE__);
    SimdVector parallel{};
    SimdVector perpendicular{};
    SourceMath::Vector3ComponentsFromNormal(&parallel, &perpendicular, MakeVector(1.0f, 2.0f, 3.0f, 0.0f), MakeVector(0.0f, 1.0f, 0.0f, 0.0f));
    CheckVectorNear(context, parallel, 0.0f, 2.0f, 0.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, perpendicular, 1.0f, 0.0f, 3.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SimdVector vector4 = MakeVector(1.0f, 2.0f, 2.0f, 0.0f);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector4Dot(MakeVector(1.0f, 2.0f, 3.0f, 4.0f), MakeVector(5.0f, 6.0f, 7.0f, 8.0f))), 70.0f, s_FloatLooseEpsilon, "Vector4Dot", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector4Length(vector4)), 3.0f, s_FloatLooseEpsilon, "Vector4Length", __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::Vector4Reflect(MakeVector(1.0f, -1.0f, 0.0f, 0.0f), MakeVector(0.0f, 1.0f, 0.0f, 0.0f)), 1.0f, 1.0f, 0.0f, 0.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector4AngleBetweenNormals(MakeVector(1.0f, 0.0f, 0.0f, 0.0f), MakeVector(0.0f, 1.0f, 0.0f, 0.0f))), SourceMath::MATH_PIDIV2, 3.0e-3f, "Vector4AngleBetweenNormals", __FILE__, __LINE__);
    const SimdVector orthogonal4 = SourceMath::Vector4Orthogonal(MakeVector(2.0f, 3.0f, 4.0f, 5.0f));
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector4Dot(orthogonal4, MakeVector(2.0f, 3.0f, 4.0f, 5.0f))), 0.0f, 3.0e-3f, "Vector4Orthogonal", __FILE__, __LINE__);

    const SimdVector cross4 = SourceMath::Vector4Cross(
        MakeVector(1.0f, 0.0f, 0.0f, 0.0f),
        MakeVector(0.0f, 1.0f, 0.0f, 0.0f),
        MakeVector(0.0f, 0.0f, 1.0f, 0.0f)
    );
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector4Dot(cross4, MakeVector(1.0f, 0.0f, 0.0f, 0.0f))), 0.0f, s_FloatLooseEpsilon, "Vector4Cross dot0", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector4Dot(cross4, MakeVector(0.0f, 1.0f, 0.0f, 0.0f))), 0.0f, s_FloatLooseEpsilon, "Vector4Cross dot1", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::Vector4Dot(cross4, MakeVector(0.0f, 0.0f, 1.0f, 0.0f))), 0.0f, s_FloatLooseEpsilon, "Vector4Cross dot2", __FILE__, __LINE__);
}

static void TestSourceMathPlaneQuaternionColor(TestContext& context){
    const SimdVector identityQuaternion = SourceMath::QuaternionIdentity();
    CheckVectorNear(context, identityQuaternion, 0.0f, 0.0f, 0.0f, 1.0f, s_FloatEpsilon, __FILE__, __LINE__);
    context.checkTrue(SourceMath::QuaternionIsIdentity(identityQuaternion), "QuaternionIsIdentity", __FILE__, __LINE__);

    const SimdVector rotationQuaternion = SourceMath::QuaternionRotationAxis(MakeVector(0.0f, 0.0f, 1.0f, 0.0f), SourceMath::MATH_PIDIV2);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::QuaternionLength(rotationQuaternion)), 1.0f, s_FloatLooseEpsilon, "QuaternionLength", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::QuaternionDot(rotationQuaternion, rotationQuaternion)), 1.0f, s_FloatLooseEpsilon, "QuaternionDot", __FILE__, __LINE__);
    CheckQuaternionEquivalent(context, SourceMath::QuaternionConjugate(rotationQuaternion), SourceMath::QuaternionInverse(rotationQuaternion), 2.0e-3f, __FILE__, __LINE__);
    CheckQuaternionEquivalent(context, SourceMath::QuaternionMultiply(rotationQuaternion, SourceMath::QuaternionInverse(rotationQuaternion)), identityQuaternion, 2.0e-3f, __FILE__, __LINE__);
    CheckQuaternionEquivalent(context, SourceMath::QuaternionExp(SourceMath::QuaternionLn(rotationQuaternion)), rotationQuaternion, 3.0e-3f, __FILE__, __LINE__);

    const SimdVector halfRotation = SourceMath::QuaternionSlerp(identityQuaternion, rotationQuaternion, 0.5f);
    CheckVectorNear(
        context,
        SourceMath::Vector3Rotate(MakeVector(1.0f, 0.0f, 0.0f, 0.0f), halfRotation),
        0.70710677f,
        0.70710677f,
        0.0f,
        0.0f,
        3.0e-3f,
        __FILE__,
        __LINE__
    );

    SimdVector squadA{};
    SimdVector squadB{};
    SimdVector squadC{};
    SourceMath::QuaternionSquadSetup(&squadA, &squadB, &squadC, identityQuaternion, rotationQuaternion, rotationQuaternion, rotationQuaternion);
    CheckVectorFinite(context, squadA, "QuaternionSquadSetup A", __FILE__, __LINE__);
    CheckVectorFinite(context, squadB, "QuaternionSquadSetup B", __FILE__, __LINE__);
    CheckVectorFinite(context, squadC, "QuaternionSquadSetup C", __FILE__, __LINE__);
    CheckQuaternionEquivalent(context, SourceMath::QuaternionSquad(rotationQuaternion, rotationQuaternion, rotationQuaternion, rotationQuaternion, 0.25f), rotationQuaternion, 2.0e-3f, __FILE__, __LINE__);
    CheckQuaternionEquivalent(context, SourceMath::QuaternionBaryCentric(rotationQuaternion, rotationQuaternion, rotationQuaternion, 0.2f, 0.3f), rotationQuaternion, 2.0e-3f, __FILE__, __LINE__);
    context.checkTrue(SourceMath::QuaternionIsNaN(MakeVector(std::numeric_limits<f32>::quiet_NaN(), 0.0f, 0.0f, 1.0f)), "QuaternionIsNaN", __FILE__, __LINE__);
    context.checkTrue(SourceMath::QuaternionIsInfinite(MakeVector(std::numeric_limits<f32>::infinity(), 0.0f, 0.0f, 1.0f)), "QuaternionIsInfinite", __FILE__, __LINE__);

    const SimdVector plane = SourceMath::PlaneFromPointNormal(MakeVector(0.0f, 0.0f, 5.0f, 1.0f), MakeVector(0.0f, 0.0f, 1.0f, 0.0f));
    CheckVectorNear(context, plane, 0.0f, 0.0f, 1.0f, -5.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    context.checkTrue(SourceMath::PlaneEqual(plane, plane), "PlaneEqual", __FILE__, __LINE__);
    context.checkTrue(SourceMath::PlaneNearEqual(plane, SourceMath::VectorScale(plane, 2.0f), SourceMath::VectorReplicate(1.0e-3f)), "PlaneNearEqual", __FILE__, __LINE__);
    context.checkTrue(SourceMath::PlaneNotEqual(plane, MakeVector(0.0f, 1.0f, 0.0f, 0.0f)), "PlaneNotEqual", __FILE__, __LINE__);
    context.checkTrue(SourceMath::PlaneIsNaN(MakeVector(std::numeric_limits<f32>::quiet_NaN(), 0.0f, 0.0f, 0.0f)), "PlaneIsNaN", __FILE__, __LINE__);
    context.checkTrue(SourceMath::PlaneIsInfinite(MakeVector(std::numeric_limits<f32>::infinity(), 0.0f, 0.0f, 0.0f)), "PlaneIsInfinite", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::PlaneDotCoord(plane, MakeVector(0.0f, 0.0f, 5.0f, 1.0f))), 0.0f, s_FloatLooseEpsilon, "PlaneDotCoord on plane", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::PlaneDotCoord(plane, MakeVector(0.0f, 0.0f, 0.0f, 1.0f))), -5.0f, s_FloatLooseEpsilon, "PlaneDotCoord origin", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::PlaneDotNormal(plane, MakeVector(0.0f, 0.0f, 3.0f, 0.0f))), 3.0f, s_FloatLooseEpsilon, "PlaneDotNormal", __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::PlaneNormalize(MakeVector(0.0f, 0.0f, 2.0f, -10.0f)), 0.0f, 0.0f, 1.0f, -5.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::PlaneIntersectLine(plane, MakeVector(0.0f, 0.0f, 0.0f, 1.0f), MakeVector(0.0f, 0.0f, 10.0f, 1.0f)), 0.0f, 0.0f, 5.0f, 1.0f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    SimdVector planeLinePoint0{};
    SimdVector planeLinePoint1{};
    SourceMath::PlaneIntersectPlane(&planeLinePoint0, &planeLinePoint1, MakeVector(1.0f, 0.0f, 0.0f, 0.0f), MakeVector(0.0f, 1.0f, 0.0f, 0.0f));
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::PlaneDotCoord(MakeVector(1.0f, 0.0f, 0.0f, 0.0f), planeLinePoint0)), 0.0f, s_FloatLooseEpsilon, "PlaneIntersectPlane p0 plane0", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::PlaneDotCoord(MakeVector(0.0f, 1.0f, 0.0f, 0.0f), planeLinePoint0)), 0.0f, s_FloatLooseEpsilon, "PlaneIntersectPlane p0 plane1", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::PlaneDotCoord(MakeVector(1.0f, 0.0f, 0.0f, 0.0f), planeLinePoint1)), 0.0f, s_FloatLooseEpsilon, "PlaneIntersectPlane p1 plane0", __FILE__, __LINE__);
    context.checkNearFloat(SourceMath::VectorGetX(SourceMath::PlaneDotCoord(MakeVector(0.0f, 1.0f, 0.0f, 0.0f), planeLinePoint1)), 0.0f, s_FloatLooseEpsilon, "PlaneIntersectPlane p1 plane1", __FILE__, __LINE__);

    const SimdVector color = MakeVector(0.25f, 0.5f, 0.75f, 0.8f);
    CheckVectorNear(context, SourceMath::ColorNegative(color), 0.75f, 0.5f, 0.25f, 0.8f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::ColorModulate(color, MakeVector(2.0f, 0.5f, 1.0f, 0.25f)), 0.5f, 0.25f, 0.75f, 0.2f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::ColorAdjustSaturation(color, 1.0f), 0.25f, 0.5f, 0.75f, 0.8f, s_FloatLooseEpsilon, __FILE__, __LINE__);
    CheckVectorNear(context, SourceMath::ColorAdjustContrast(color, 1.0f), 0.25f, 0.5f, 0.75f, 0.8f, s_FloatLooseEpsilon, __FILE__, __LINE__);

    const SourceMath::Float4 rgbFromHsl = StoreVector(SourceMath::ColorHSLToRGB(SourceMath::ColorRGBToHSL(color)));
    CheckFloat4Near(context, rgbFromHsl, 0.25f, 0.5f, 0.75f, 0.8f, 4.0e-3f, __FILE__, __LINE__);

    const SourceMath::Float4 rgbFromHsv = StoreVector(SourceMath::ColorHSVToRGB(SourceMath::ColorRGBToHSV(color)));
    CheckFloat4Near(context, rgbFromHsv, 0.25f, 0.5f, 0.75f, 0.8f, 4.0e-3f, __FILE__, __LINE__);

    const SourceMath::Float4 rgbFromYuv = StoreVector(SourceMath::ColorYUVToRGB(SourceMath::ColorRGBToYUV(color)));
    CheckFloat4Near(context, rgbFromYuv, 0.25f, 0.5f, 0.75f, 0.8f, 1.0e-2f, __FILE__, __LINE__);

    const SourceMath::Float4 rgbFromYuvHd = StoreVector(SourceMath::ColorYUVToRGB_HD(SourceMath::ColorRGBToYUV_HD(color)));
    CheckFloat4Near(context, rgbFromYuvHd, 0.25f, 0.5f, 0.75f, 0.8f, 1.0e-2f, __FILE__, __LINE__);

    const SourceMath::Float4 rgbFromYuvUhd = StoreVector(SourceMath::ColorYUVToRGB_UHD(SourceMath::ColorRGBToYUV_UHD(color)));
    CheckFloat4Near(context, rgbFromYuvUhd, 0.25f, 0.5f, 0.75f, 0.8f, 1.0e-2f, __FILE__, __LINE__);

    const SourceMath::Float4 rgbFromXyz = StoreVector(SourceMath::ColorXYZToRGB(SourceMath::ColorRGBToXYZ(color)));
    CheckFloat4Near(context, rgbFromXyz, 0.25f, 0.5f, 0.75f, 0.8f, 1.5e-2f, __FILE__, __LINE__);

    const SourceMath::Float4 srgbRoundTrip = StoreVector(SourceMath::ColorRGBToSRGB(SourceMath::ColorSRGBToRGB(color)));
    CheckFloat4Near(context, srgbRoundTrip, 0.25f, 0.5f, 0.75f, 0.8f, 4.0e-3f, __FILE__, __LINE__);

    const SourceMath::Float4 xyzSrgbRoundTrip = StoreVector(SourceMath::ColorXYZToSRGB(SourceMath::ColorSRGBToXYZ(color)));
    CheckFloat4Near(context, xyzSrgbRoundTrip, 0.25f, 0.5f, 0.75f, 0.8f, 1.5e-2f, __FILE__, __LINE__);

    CheckVectorNear(
        context,
        SourceMath::FresnelTerm(SourceMath::VectorReplicate(1.0f), SourceMath::VectorReplicate(1.5f)),
        0.04f,
        0.04f,
        0.04f,
        0.04f,
        4.0e-3f,
        __FILE__,
        __LINE__
    );
}

static void TestShMath(TestContext& context){
    context.checkTrue(!SH::IsValidOrder(1), "!SH::IsValidOrder(1)", __FILE__, __LINE__);
    context.checkTrue(SH::IsValidOrder(2), "SH::IsValidOrder(2)", __FILE__, __LINE__);
    context.checkTrue(SH::IsValidOrder(6), "SH::IsValidOrder(6)", __FILE__, __LINE__);
    context.checkTrue(!SH::IsValidOrder(7), "!SH::IsValidOrder(7)", __FILE__, __LINE__);
    context.checkTrue(SH::CoefficientCount(6) == 36, "SH::CoefficientCount", __FILE__, __LINE__);

    f32 direction[36]{};
    context.checkTrue(SH::EvalDirection(MakeNotNull(direction), 6, MakeVector(0.0f, 0.0f, 1.0f, 0.0f)), "SH::EvalDirection", __FILE__, __LINE__);
    context.checkTrue(direction[0] != 0.0f, "direction[0] != 0", __FILE__, __LINE__);

    f32 sequenceA[36]{};
    f32 sequenceB[36]{};
    FillSequence(sequenceA, 36, 0.25f, -1.0f);
    FillSequence(sequenceB, 36, -0.15f, 2.0f);

    f32 added[36]{};
    f32 scaled[36]{};
    context.checkTrue(SH::Add(MakeNotNull(added), 6, MakeConstNotNull(sequenceA), MakeConstNotNull(sequenceB)), "SH::Add", __FILE__, __LINE__);
    context.checkTrue(SH::Scale(MakeNotNull(scaled), 6, MakeConstNotNull(sequenceA), 3.0f), "SH::Scale", __FILE__, __LINE__);
    for(usize i = 0; i < 36; ++i){
        context.checkNearFloat(added[i], sequenceA[i] + sequenceB[i], s_FloatEpsilon, "SH add", __FILE__, __LINE__);
        context.checkNearFloat(scaled[i], sequenceA[i] * 3.0f, s_FloatEpsilon, "SH scale", __FILE__, __LINE__);
    }

    f32 manualDot = 0.0f;
    for(usize i = 0; i < 36; ++i)
        manualDot += sequenceA[i] * sequenceB[i];
    context.checkNearFloat(SH::Dot(6, MakeConstNotNull(sequenceA), MakeConstNotNull(sequenceB)), manualDot, s_FloatLooseEpsilon, "SH::Dot", __FILE__, __LINE__);

    f32 rotatedIdentity[36]{};
    context.checkTrue(SH::Rotate(MakeNotNull(rotatedIdentity), 6, MatrixIdentity(), MakeConstNotNull(sequenceA)), "SH::Rotate identity", __FILE__, __LINE__);
    for(usize i = 0; i < 36; ++i)
        context.checkNearFloat(rotatedIdentity[i], sequenceA[i], s_FloatLooseEpsilon, "SH rotate identity", __FILE__, __LINE__);

    f32 rotatedZ[36]{};
    f32 rotatedMatrix[36]{};
    const SimdMatrix zRotation = MatrixRotationZ(0.37f);
    context.checkTrue(SH::RotateZ(MakeNotNull(rotatedZ), 6, 0.37f, MakeConstNotNull(sequenceA)), "SH::RotateZ", __FILE__, __LINE__);
    context.checkTrue(SH::Rotate(MakeNotNull(rotatedMatrix), 6, zRotation, MakeConstNotNull(sequenceA)), "SH::Rotate matrix", __FILE__, __LINE__);
    for(usize i = 0; i < 36; ++i)
        context.checkNearFloat(rotatedZ[i], rotatedMatrix[i], 2.0e-3f, "SH rotate Z vs matrix", __FILE__, __LINE__);

    for(usize order = 2; order <= 6; ++order){
        const usize coefficientCount = SH::CoefficientCount(order);
        f32 generic[36]{};
        context.checkTrue(SH::Multiply(MakeNotNull(generic), order, MakeConstNotNull(sequenceA), MakeConstNotNull(sequenceB)), "SH::Multiply", __FILE__, __LINE__);

        f32 specialized[36]{};
        bool success = false;
        switch(order){
        case 2: success = SH::Multiply2(MakeNotNull(specialized), MakeConstNotNull(sequenceA), MakeConstNotNull(sequenceB)); break;
        case 3: success = SH::Multiply3(MakeNotNull(specialized), MakeConstNotNull(sequenceA), MakeConstNotNull(sequenceB)); break;
        case 4: success = SH::Multiply4(MakeNotNull(specialized), MakeConstNotNull(sequenceA), MakeConstNotNull(sequenceB)); break;
        case 5: success = SH::Multiply5(MakeNotNull(specialized), MakeConstNotNull(sequenceA), MakeConstNotNull(sequenceB)); break;
        case 6: success = SH::Multiply6(MakeNotNull(specialized), MakeConstNotNull(sequenceA), MakeConstNotNull(sequenceB)); break;
        default: break;
        }
        context.checkTrue(success, "SH::MultiplyN", __FILE__, __LINE__);
        for(usize i = 0; i < coefficientCount; ++i)
            context.checkNearFloat(generic[i], specialized[i], 3.0e-3f, "SH multiply generic vs specialized", __FILE__, __LINE__);
    }

    f32 lightR[36]{};
    f32 lightG[36]{};
    f32 lightB[36]{};
    context.checkTrue(SH::EvalDirectionalLight(3, MakeVector(0.0f, 0.0f, 1.0f, 0.0f), MakeVector(0.0f, 0.0f, 0.0f, 0.0f), MakeNotNull(lightR), lightG, lightB), "SH::EvalDirectionalLight", __FILE__, __LINE__);
    context.checkTrue(SH::EvalSphericalLight(3, MakeVector(0.0f, 0.0f, 1.0f, 1.0f), 1.0f, MakeVector(0.0f, 0.0f, 0.0f, 0.0f), MakeNotNull(lightR), lightG, lightB), "SH::EvalSphericalLight", __FILE__, __LINE__);
    context.checkTrue(SH::EvalConeLight(3, MakeVector(0.0f, 0.0f, 1.0f, 0.0f), 0.5f, MakeVector(0.0f, 0.0f, 0.0f, 0.0f), MakeNotNull(lightR), lightG, lightB), "SH::EvalConeLight", __FILE__, __LINE__);
    context.checkTrue(SH::EvalHemisphereLight(3, MakeVector(0.0f, 1.0f, 0.0f, 0.0f), MakeVector(0.0f, 0.0f, 0.0f, 0.0f), MakeVector(0.0f, 0.0f, 0.0f, 0.0f), MakeNotNull(lightR), lightG, lightB), "SH::EvalHemisphereLight", __FILE__, __LINE__);
    for(usize i = 0; i < SH::CoefficientCount(3); ++i){
        context.checkNearFloat(lightR[i], 0.0f, s_FloatLooseEpsilon, "SH zero light R", __FILE__, __LINE__);
        context.checkNearFloat(lightG[i], 0.0f, s_FloatLooseEpsilon, "SH zero light G", __FILE__, __LINE__);
        context.checkNearFloat(lightB[i], 0.0f, s_FloatLooseEpsilon, "SH zero light B", __FILE__, __LINE__);
    }
}


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(){
    using namespace __hidden_math_tests;

    TestContext context;
    TestNativeFloatMath(context);
    TestNativeDoubleMath(context);
    TestSourceMathDoubleSimd(context);
    TestMatrixBoundary(context);
    TestMatrixBuilders(context);
    TestMatrixRelations(context);
    TestMatrixTransformationHelpers(context);
    TestMatrixTransforms(context);
    TestMatrixProjectAndStreams(context);
    TestSourceMathConversionsAndLoads(context);
    TestSourceMathGeneralVectors(context);
    TestSourceMathVectorFamilies(context);
    TestSourceMathPlaneQuaternionColor(context);
    TestShMath(context);

    if(context.failed != 0){
        NWB_CERR << "math tests failed: " << context.failed << " of " << (context.passed + context.failed) << '\n';
        return 1;
    }

    NWB_COUT << "math tests passed: " << context.passed << '\n';
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

