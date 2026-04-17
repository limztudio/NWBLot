// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compile.h"
#include "not_null.h"
#include "simplemath.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_MATH_FORCE_SCALAR) && (defined(NWB_MATH_FORCE_AVX2) || defined(NWB_MATH_FORCE_NEON))
#error Only one NWB_MATH_FORCE_* backend override may be enabled.
#endif

#if defined(NWB_MATH_FORCE_AVX2) && defined(NWB_MATH_FORCE_NEON)
#error Only one NWB_MATH_FORCE_* backend override may be enabled.
#endif

#if defined(NWB_MATH_FORCE_AVX2)
#define NWB_MATH_BACKEND_AVX2 1
#elif defined(NWB_MATH_FORCE_NEON)
#define NWB_MATH_BACKEND_NEON 1
#elif defined(NWB_MATH_FORCE_SCALAR)
#define NWB_MATH_BACKEND_SCALAR 1
#elif (defined(__AVX2__) || defined(_M_AVX2)) \
    && (defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86))
#define NWB_MATH_BACKEND_AVX2 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64) || defined(_M_ARM)
#define NWB_MATH_BACKEND_NEON 1
#else
#define NWB_MATH_BACKEND_SCALAR 1
#endif

#if defined(NWB_MATH_BACKEND_AVX2)
#include <immintrin.h>
#elif defined(NWB_MATH_BACKEND_NEON)
#if defined(_MSC_VER) && defined(_M_ARM64)
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MathBackend{

#define NWB_MATH_FRAGMENT_INCLUDE 1
#include "detail/math_backend_scalar.h"
#include "detail/math_backend_avx2.h"
#include "detail/math_backend_neon.h"
#undef NWB_MATH_FRAGMENT_INCLUDE


#if defined(NWB_MATH_BACKEND_AVX2)
namespace Current = Avx2;
#elif defined(NWB_MATH_BACKEND_NEON)
namespace Current = Neon;
#else
namespace Current = Scalar;
#endif


}


struct Float4{
    MathBackend::Current::Float4Reg m_value;

    Float4()noexcept
        : m_value(MathBackend::Current::SetZeroF32())
    {
    }
    explicit Float4(const MathBackend::Current::Float4Reg value)noexcept
        : m_value(value)
    {
    }
    Float4(const f32 x, const f32 y, const f32 z, const f32 w)noexcept
        : m_value(MathBackend::Current::SetF32(x, y, z, w))
    {
    }

    [[nodiscard]] static Float4 Zero()noexcept{ return Float4(0.0f, 0.0f, 0.0f, 0.0f); }
};

struct Double4{
    MathBackend::Current::Double4Reg m_value;

    Double4()noexcept
        : m_value(MathBackend::Current::SetZeroF64())
    {
    }
    explicit Double4(const MathBackend::Current::Double4Reg value)noexcept
        : m_value(value)
    {
    }
    Double4(const f64 x, const f64 y, const f64 z, const f64 w)noexcept
        : m_value(MathBackend::Current::SetF64(x, y, z, w))
    {
    }

    [[nodiscard]] static Double4 Zero()noexcept{ return Double4(0.0, 0.0, 0.0, 0.0); }
};

// Column-oriented 4x4 matrix. `c3` is the translation column under NWB's
// column-vector convention.
struct Float4x4{
    Float4 c0;
    Float4 c1;
    Float4 c2;
    Float4 c3;

    Float4x4()noexcept = default;
    Float4x4(const Float4& column0, const Float4& column1, const Float4& column2, const Float4& column3)noexcept
        : c0(column0)
        , c1(column1)
        , c2(column2)
        , c3(column3)
    {
    }

    [[nodiscard]] static Float4x4 Identity()noexcept{
        return Float4x4(
            Float4(1.0f, 0.0f, 0.0f, 0.0f),
            Float4(0.0f, 1.0f, 0.0f, 0.0f),
            Float4(0.0f, 0.0f, 1.0f, 0.0f),
            Float4(0.0f, 0.0f, 0.0f, 1.0f)
        );
    }

    [[nodiscard]] static Float4x4 Translation(const f32 x, const f32 y, const f32 z)noexcept{
        return Float4x4(
            Float4(1.0f, 0.0f, 0.0f, 0.0f),
            Float4(0.0f, 1.0f, 0.0f, 0.0f),
            Float4(0.0f, 0.0f, 1.0f, 0.0f),
            Float4(x, y, z, 1.0f)
        );
    }

    [[nodiscard]] static Float4x4 Scale(const f32 x, const f32 y, const f32 z)noexcept{
        return Float4x4(
            Float4(x, 0.0f, 0.0f, 0.0f),
            Float4(0.0f, y, 0.0f, 0.0f),
            Float4(0.0f, 0.0f, z, 0.0f),
            Float4(0.0f, 0.0f, 0.0f, 1.0f)
        );
    }

    [[nodiscard]] static Float4x4 RotationX(const f32 angle)noexcept{
        const f32 sinAngle = Sin(angle);
        const f32 cosAngle = Cos(angle);
        return Float4x4(
            Float4(1.0f, 0.0f, 0.0f, 0.0f),
            Float4(0.0f, cosAngle, sinAngle, 0.0f),
            Float4(0.0f, -sinAngle, cosAngle, 0.0f),
            Float4(0.0f, 0.0f, 0.0f, 1.0f)
        );
    }

    [[nodiscard]] static Float4x4 RotationY(const f32 angle)noexcept{
        const f32 sinAngle = Sin(angle);
        const f32 cosAngle = Cos(angle);
        return Float4x4(
            Float4(cosAngle, 0.0f, -sinAngle, 0.0f),
            Float4(0.0f, 1.0f, 0.0f, 0.0f),
            Float4(sinAngle, 0.0f, cosAngle, 0.0f),
            Float4(0.0f, 0.0f, 0.0f, 1.0f)
        );
    }

    [[nodiscard]] static Float4x4 RotationZ(const f32 angle)noexcept{
        const f32 sinAngle = Sin(angle);
        const f32 cosAngle = Cos(angle);
        return Float4x4(
            Float4(cosAngle, sinAngle, 0.0f, 0.0f),
            Float4(-sinAngle, cosAngle, 0.0f, 0.0f),
            Float4(0.0f, 0.0f, 1.0f, 0.0f),
            Float4(0.0f, 0.0f, 0.0f, 1.0f)
        );
    }
};

// Column-oriented 4x4 matrix. `c3` is the translation column under NWB's
// column-vector convention.
struct Double4x4{
    Double4 c0;
    Double4 c1;
    Double4 c2;
    Double4 c3;

    Double4x4()noexcept = default;
    Double4x4(const Double4& column0, const Double4& column1, const Double4& column2, const Double4& column3)noexcept
        : c0(column0)
        , c1(column1)
        , c2(column2)
        , c3(column3)
    {
    }

    [[nodiscard]] static Double4x4 Identity()noexcept{
        return Double4x4(
            Double4(1.0, 0.0, 0.0, 0.0),
            Double4(0.0, 1.0, 0.0, 0.0),
            Double4(0.0, 0.0, 1.0, 0.0),
            Double4(0.0, 0.0, 0.0, 1.0)
        );
    }

    [[nodiscard]] static Double4x4 Translation(const f64 x, const f64 y, const f64 z)noexcept{
        return Double4x4(
            Double4(1.0, 0.0, 0.0, 0.0),
            Double4(0.0, 1.0, 0.0, 0.0),
            Double4(0.0, 0.0, 1.0, 0.0),
            Double4(x, y, z, 1.0)
        );
    }

    [[nodiscard]] static Double4x4 Scale(const f64 x, const f64 y, const f64 z)noexcept{
        return Double4x4(
            Double4(x, 0.0, 0.0, 0.0),
            Double4(0.0, y, 0.0, 0.0),
            Double4(0.0, 0.0, z, 0.0),
            Double4(0.0, 0.0, 0.0, 1.0)
        );
    }

    [[nodiscard]] static Double4x4 RotationX(const f64 angle)noexcept{
        const f64 sinAngle = Sin(angle);
        const f64 cosAngle = Cos(angle);
        return Double4x4(
            Double4(1.0, 0.0, 0.0, 0.0),
            Double4(0.0, cosAngle, sinAngle, 0.0),
            Double4(0.0, -sinAngle, cosAngle, 0.0),
            Double4(0.0, 0.0, 0.0, 1.0)
        );
    }

    [[nodiscard]] static Double4x4 RotationY(const f64 angle)noexcept{
        const f64 sinAngle = Sin(angle);
        const f64 cosAngle = Cos(angle);
        return Double4x4(
            Double4(cosAngle, 0.0, -sinAngle, 0.0),
            Double4(0.0, 1.0, 0.0, 0.0),
            Double4(sinAngle, 0.0, cosAngle, 0.0),
            Double4(0.0, 0.0, 0.0, 1.0)
        );
    }

    [[nodiscard]] static Double4x4 RotationZ(const f64 angle)noexcept{
        const f64 sinAngle = Sin(angle);
        const f64 cosAngle = Cos(angle);
        return Double4x4(
            Double4(cosAngle, sinAngle, 0.0, 0.0),
            Double4(-sinAngle, cosAngle, 0.0, 0.0),
            Double4(0.0, 0.0, 1.0, 0.0),
            Double4(0.0, 0.0, 0.0, 1.0)
        );
    }
};


[[nodiscard]] NWB_INLINE Float4 LoadFloat4(NotNull<const f32*> values)noexcept{
    return Float4(MathBackend::Current::LoadF32(values));
}

[[nodiscard]] NWB_INLINE Double4 LoadDouble4(NotNull<const f64*> values)noexcept{
    return Double4(MathBackend::Current::LoadF64(values));
}

// Raw pointer matrix loads in this lightweight layer use four contiguous
// columns: `[c0 | c1 | c2 | c3]`. For compact affine boundaries, use the
// `matrix_math.h` `Float3x4` helpers.
[[nodiscard]] NWB_INLINE Float4x4 LoadFloat4x4(NotNull<const f32*> values)noexcept{
    return Float4x4(
        LoadFloat4(MakeNotNull(values.get())),
        LoadFloat4(MakeNotNull(values.get() + 4)),
        LoadFloat4(MakeNotNull(values.get() + 8)),
        LoadFloat4(MakeNotNull(values.get() + 12))
    );
}

// Raw pointer matrix loads in this lightweight layer use four contiguous
// columns: `[c0 | c1 | c2 | c3]`. For compact affine boundaries, use the
// `matrix_math.h` `Float3x4` helpers.
[[nodiscard]] NWB_INLINE Double4x4 LoadDouble4x4(NotNull<const f64*> values)noexcept{
    return Double4x4(
        LoadDouble4(MakeNotNull(values.get())),
        LoadDouble4(MakeNotNull(values.get() + 4)),
        LoadDouble4(MakeNotNull(values.get() + 8)),
        LoadDouble4(MakeNotNull(values.get() + 12))
    );
}

NWB_INLINE void StoreFloat4(NotNull<f32*> outValues, const Float4& value)noexcept{
    MathBackend::Current::StoreF32(outValues, value.m_value);
}

NWB_INLINE void StoreDouble4(NotNull<f64*> outValues, const Double4& value)noexcept{
    MathBackend::Current::StoreF64(outValues, value.m_value);
}

// Raw pointer matrix stores in this lightweight layer write four contiguous
// columns: `[c0 | c1 | c2 | c3]`. For compact affine boundaries, use the
// `matrix_math.h` `Float3x4` helpers.
NWB_INLINE void StoreFloat4x4(NotNull<f32*> outValues, const Float4x4& value)noexcept{
    StoreFloat4(MakeNotNull(outValues.get()), value.c0);
    StoreFloat4(MakeNotNull(outValues.get() + 4), value.c1);
    StoreFloat4(MakeNotNull(outValues.get() + 8), value.c2);
    StoreFloat4(MakeNotNull(outValues.get() + 12), value.c3);
}

// Raw pointer matrix stores in this lightweight layer write four contiguous
// columns: `[c0 | c1 | c2 | c3]`. For compact affine boundaries, use the
// `matrix_math.h` `Float3x4` helpers.
NWB_INLINE void StoreDouble4x4(NotNull<f64*> outValues, const Double4x4& value)noexcept{
    StoreDouble4(MakeNotNull(outValues.get()), value.c0);
    StoreDouble4(MakeNotNull(outValues.get() + 4), value.c1);
    StoreDouble4(MakeNotNull(outValues.get() + 8), value.c2);
    StoreDouble4(MakeNotNull(outValues.get() + 12), value.c3);
}


[[nodiscard]] NWB_INLINE Float4 operator+(const Float4& lhs, const Float4& rhs)noexcept{
    return Float4(MathBackend::Current::AddF32(lhs.m_value, rhs.m_value));
}

[[nodiscard]] NWB_INLINE Float4 operator-(const Float4& lhs, const Float4& rhs)noexcept{
    return Float4(MathBackend::Current::SubF32(lhs.m_value, rhs.m_value));
}

[[nodiscard]] NWB_INLINE Float4 operator*(const Float4& lhs, const Float4& rhs)noexcept{
    return Float4(MathBackend::Current::MulF32(lhs.m_value, rhs.m_value));
}

[[nodiscard]] NWB_INLINE Float4 operator/(const Float4& lhs, const Float4& rhs)noexcept{
    return Float4(MathBackend::Current::DivF32(lhs.m_value, rhs.m_value));
}
[[nodiscard]] NWB_INLINE Float4 operator-(const Float4& value)noexcept{ return Float4(0.0f, 0.0f, 0.0f, 0.0f) - value; }
[[nodiscard]] NWB_INLINE Float4 operator*(const Float4& lhs, const f32 rhs)noexcept{ return lhs * Float4(rhs, rhs, rhs, rhs); }
[[nodiscard]] NWB_INLINE Float4 operator*(const f32 lhs, const Float4& rhs)noexcept{ return Float4(lhs, lhs, lhs, lhs) * rhs; }
[[nodiscard]] NWB_INLINE Float4 operator/(const Float4& lhs, const f32 rhs)noexcept{ return lhs / Float4(rhs, rhs, rhs, rhs); }

[[nodiscard]] NWB_INLINE Double4 operator+(const Double4& lhs, const Double4& rhs)noexcept{
    return Double4(MathBackend::Current::AddF64(lhs.m_value, rhs.m_value));
}

[[nodiscard]] NWB_INLINE Double4 operator-(const Double4& lhs, const Double4& rhs)noexcept{
    return Double4(MathBackend::Current::SubF64(lhs.m_value, rhs.m_value));
}

[[nodiscard]] NWB_INLINE Double4 operator*(const Double4& lhs, const Double4& rhs)noexcept{
    return Double4(MathBackend::Current::MulF64(lhs.m_value, rhs.m_value));
}

[[nodiscard]] NWB_INLINE Double4 operator/(const Double4& lhs, const Double4& rhs)noexcept{
    return Double4(MathBackend::Current::DivF64(lhs.m_value, rhs.m_value));
}
[[nodiscard]] NWB_INLINE Double4 operator-(const Double4& value)noexcept{ return Double4(0.0, 0.0, 0.0, 0.0) - value; }
[[nodiscard]] NWB_INLINE Double4 operator*(const Double4& lhs, const f64 rhs)noexcept{
    return lhs * Double4(rhs, rhs, rhs, rhs);
}

[[nodiscard]] NWB_INLINE Double4 operator*(const f64 lhs, const Double4& rhs)noexcept{
    return Double4(lhs, lhs, lhs, lhs) * rhs;
}

[[nodiscard]] NWB_INLINE Double4 operator/(const Double4& lhs, const f64 rhs)noexcept{
    return lhs / Double4(rhs, rhs, rhs, rhs);
}


[[nodiscard]] NWB_INLINE f32 GetX(const Float4& value)noexcept{ return MathBackend::Current::ExtractXF32(value.m_value); }
[[nodiscard]] NWB_INLINE f32 GetY(const Float4& value)noexcept{ return MathBackend::Current::ExtractYF32(value.m_value); }
[[nodiscard]] NWB_INLINE f32 GetZ(const Float4& value)noexcept{ return MathBackend::Current::ExtractZF32(value.m_value); }
[[nodiscard]] NWB_INLINE f32 GetW(const Float4& value)noexcept{ return MathBackend::Current::ExtractWF32(value.m_value); }

[[nodiscard]] NWB_INLINE f64 GetX(const Double4& value)noexcept{ return MathBackend::Current::ExtractXF64(value.m_value); }
[[nodiscard]] NWB_INLINE f64 GetY(const Double4& value)noexcept{ return MathBackend::Current::ExtractYF64(value.m_value); }
[[nodiscard]] NWB_INLINE f64 GetZ(const Double4& value)noexcept{ return MathBackend::Current::ExtractZF64(value.m_value); }
[[nodiscard]] NWB_INLINE f64 GetW(const Double4& value)noexcept{ return MathBackend::Current::ExtractWF64(value.m_value); }


[[nodiscard]] NWB_INLINE f32 Dot3(const Float4& lhs, const Float4& rhs)noexcept{
    return MathBackend::Current::Dot3F32(lhs.m_value, rhs.m_value);
}

[[nodiscard]] NWB_INLINE f32 Dot4(const Float4& lhs, const Float4& rhs)noexcept{
    return MathBackend::Current::Dot4F32(lhs.m_value, rhs.m_value);
}

[[nodiscard]] NWB_INLINE f64 Dot3(const Double4& lhs, const Double4& rhs)noexcept{
    return MathBackend::Current::Dot3F64(lhs.m_value, rhs.m_value);
}

[[nodiscard]] NWB_INLINE f64 Dot4(const Double4& lhs, const Double4& rhs)noexcept{
    return MathBackend::Current::Dot4F64(lhs.m_value, rhs.m_value);
}


[[nodiscard]] NWB_INLINE Float4 Cross3(const Float4& lhs, const Float4& rhs)noexcept{
    const f32 lhsX = GetX(lhs);
    const f32 lhsY = GetY(lhs);
    const f32 lhsZ = GetZ(lhs);
    const f32 rhsX = GetX(rhs);
    const f32 rhsY = GetY(rhs);
    const f32 rhsZ = GetZ(rhs);
    return Float4(
        lhsY * rhsZ - lhsZ * rhsY,
        lhsZ * rhsX - lhsX * rhsZ,
        lhsX * rhsY - lhsY * rhsX,
        0.0f
    );
}

[[nodiscard]] NWB_INLINE Double4 Cross3(const Double4& lhs, const Double4& rhs)noexcept{
    const f64 lhsX = GetX(lhs);
    const f64 lhsY = GetY(lhs);
    const f64 lhsZ = GetZ(lhs);
    const f64 rhsX = GetX(rhs);
    const f64 rhsY = GetY(rhs);
    const f64 rhsZ = GetZ(rhs);
    return Double4(
        lhsY * rhsZ - lhsZ * rhsY,
        lhsZ * rhsX - lhsX * rhsZ,
        lhsX * rhsY - lhsY * rhsX,
        0.0
    );
}


[[nodiscard]] NWB_INLINE f32 Length3(const Float4& value)noexcept{ return Sqrt(Dot3(value, value)); }
[[nodiscard]] NWB_INLINE f64 Length3(const Double4& value)noexcept{ return Sqrt(Dot3(value, value)); }

[[nodiscard]] NWB_INLINE Float4 Normalize3(const Float4& value)noexcept{
    const f32 length = Length3(value);
    const f32 x = GetX(value);
    const f32 y = GetY(value);
    const f32 z = GetZ(value);
    const f32 w = GetW(value);
    if(length <= 0.0f)
        return Float4(0.0f, 0.0f, 0.0f, w);

    return Float4(
        x / length,
        y / length,
        z / length,
        w
    );
}

[[nodiscard]] NWB_INLINE Double4 Normalize3(const Double4& value)noexcept{
    const f64 length = Length3(value);
    const f64 x = GetX(value);
    const f64 y = GetY(value);
    const f64 z = GetZ(value);
    const f64 w = GetW(value);
    if(length <= 0.0)
        return Double4(0.0, 0.0, 0.0, w);

    return Double4(
        x / length,
        y / length,
        z / length,
        w
    );
}


[[nodiscard]] NWB_INLINE Float4 Transform(const Float4x4& matrix, const Float4& vector)noexcept{
    auto result = MathBackend::Current::MulF32(matrix.c0.m_value, MathBackend::Current::SplatXF32(vector.m_value));
    result = MathBackend::Current::MulAddF32(matrix.c1.m_value, MathBackend::Current::SplatYF32(vector.m_value), result);
    result = MathBackend::Current::MulAddF32(matrix.c2.m_value, MathBackend::Current::SplatZF32(vector.m_value), result);
    result = MathBackend::Current::MulAddF32(matrix.c3.m_value, MathBackend::Current::SplatWF32(vector.m_value), result);
    return Float4(result);
}

[[nodiscard]] NWB_INLINE Double4 Transform(const Double4x4& matrix, const Double4& vector)noexcept{
    auto result = MathBackend::Current::MulF64(matrix.c0.m_value, MathBackend::Current::SplatXF64(vector.m_value));
    result = MathBackend::Current::MulAddF64(matrix.c1.m_value, MathBackend::Current::SplatYF64(vector.m_value), result);
    result = MathBackend::Current::MulAddF64(matrix.c2.m_value, MathBackend::Current::SplatZF64(vector.m_value), result);
    result = MathBackend::Current::MulAddF64(matrix.c3.m_value, MathBackend::Current::SplatWF64(vector.m_value), result);
    return Double4(result);
}

[[nodiscard]] NWB_INLINE Float4 TransformPoint(const Float4x4& matrix, const Float4& point)noexcept{
    return Transform(matrix, Float4(GetX(point), GetY(point), GetZ(point), 1.0f));
}

[[nodiscard]] NWB_INLINE Double4 TransformPoint(const Double4x4& matrix, const Double4& point)noexcept{
    return Transform(matrix, Double4(GetX(point), GetY(point), GetZ(point), 1.0));
}

[[nodiscard]] NWB_INLINE Float4 TransformDirection(const Float4x4& matrix, const Float4& direction)noexcept{
    return Transform(matrix, Float4(GetX(direction), GetY(direction), GetZ(direction), 0.0f));
}

[[nodiscard]] NWB_INLINE Double4 TransformDirection(const Double4x4& matrix, const Double4& direction)noexcept{
    return Transform(matrix, Double4(GetX(direction), GetY(direction), GetZ(direction), 0.0));
}


[[nodiscard]] NWB_INLINE Float4x4 operator*(const Float4x4& lhs, const Float4x4& rhs)noexcept{
    return Float4x4(
        Transform(lhs, rhs.c0),
        Transform(lhs, rhs.c1),
        Transform(lhs, rhs.c2),
        Transform(lhs, rhs.c3)
    );
}

[[nodiscard]] NWB_INLINE Double4x4 operator*(const Double4x4& lhs, const Double4x4& rhs)noexcept{
    return Double4x4(
        Transform(lhs, rhs.c0),
        Transform(lhs, rhs.c1),
        Transform(lhs, rhs.c2),
        Transform(lhs, rhs.c3)
    );
}

[[nodiscard]] NWB_INLINE Float4 operator*(const Float4x4& matrix, const Float4& vector)noexcept{
    return Transform(matrix, vector);
}

[[nodiscard]] NWB_INLINE Double4 operator*(const Double4x4& matrix, const Double4& vector)noexcept{
    return Transform(matrix, vector);
}


[[nodiscard]] NWB_INLINE Float4x4 Transpose(const Float4x4& matrix)noexcept{
    auto c0 = matrix.c0.m_value;
    auto c1 = matrix.c1.m_value;
    auto c2 = matrix.c2.m_value;
    auto c3 = matrix.c3.m_value;
    MathBackend::Current::Transpose4x4F32(c0, c1, c2, c3);
    return Float4x4(
        Float4(c0),
        Float4(c1),
        Float4(c2),
        Float4(c3)
    );
}

[[nodiscard]] NWB_INLINE Double4x4 Transpose(const Double4x4& matrix)noexcept{
    auto c0 = matrix.c0.m_value;
    auto c1 = matrix.c1.m_value;
    auto c2 = matrix.c2.m_value;
    auto c3 = matrix.c3.m_value;
    MathBackend::Current::Transpose4x4F64(c0, c1, c2, c3);
    return Double4x4(
        Double4(c0),
        Double4(c1),
        Double4(c2),
        Double4(c3)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

