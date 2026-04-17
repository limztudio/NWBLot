// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#ifndef NWB_MATH_FRAGMENT_INCLUDE
#error Include global/math.h instead of including math_backend_scalar.h directly.
#endif


namespace Scalar{


struct Float4Reg{
    f32 lanes[4];
};

struct Double4Reg{
    f64 lanes[4];
};


[[nodiscard]] NWB_INLINE Float4Reg SetZeroF32()noexcept{
    Float4Reg result{};
    result.lanes[0] = 0.0f;
    result.lanes[1] = 0.0f;
    result.lanes[2] = 0.0f;
    result.lanes[3] = 0.0f;
    return result;
}

[[nodiscard]] NWB_INLINE Float4Reg SetF32(const f32 x, const f32 y, const f32 z, const f32 w)noexcept{
    Float4Reg result{};
    result.lanes[0] = x;
    result.lanes[1] = y;
    result.lanes[2] = z;
    result.lanes[3] = w;
    return result;
}

[[nodiscard]] NWB_INLINE Float4Reg LoadF32(NotNull<const f32*> values)noexcept{
    return SetF32(values.get()[0], values.get()[1], values.get()[2], values.get()[3]);
}

NWB_INLINE void StoreF32(NotNull<f32*> outValues, const Float4Reg& value)noexcept{
    outValues.get()[0] = value.lanes[0];
    outValues.get()[1] = value.lanes[1];
    outValues.get()[2] = value.lanes[2];
    outValues.get()[3] = value.lanes[3];
}

[[nodiscard]] NWB_INLINE Float4Reg AddF32(const Float4Reg& lhs, const Float4Reg& rhs)noexcept{
    return SetF32(
        lhs.lanes[0] + rhs.lanes[0],
        lhs.lanes[1] + rhs.lanes[1],
        lhs.lanes[2] + rhs.lanes[2],
        lhs.lanes[3] + rhs.lanes[3]
    );
}

[[nodiscard]] NWB_INLINE Float4Reg SubF32(const Float4Reg& lhs, const Float4Reg& rhs)noexcept{
    return SetF32(
        lhs.lanes[0] - rhs.lanes[0],
        lhs.lanes[1] - rhs.lanes[1],
        lhs.lanes[2] - rhs.lanes[2],
        lhs.lanes[3] - rhs.lanes[3]
    );
}

[[nodiscard]] NWB_INLINE Float4Reg MulF32(const Float4Reg& lhs, const Float4Reg& rhs)noexcept{
    return SetF32(
        lhs.lanes[0] * rhs.lanes[0],
        lhs.lanes[1] * rhs.lanes[1],
        lhs.lanes[2] * rhs.lanes[2],
        lhs.lanes[3] * rhs.lanes[3]
    );
}

[[nodiscard]] NWB_INLINE Float4Reg DivF32(const Float4Reg& lhs, const Float4Reg& rhs)noexcept{
    return SetF32(
        lhs.lanes[0] / rhs.lanes[0],
        lhs.lanes[1] / rhs.lanes[1],
        lhs.lanes[2] / rhs.lanes[2],
        lhs.lanes[3] / rhs.lanes[3]
    );
}

[[nodiscard]] NWB_INLINE Float4Reg MulAddF32(const Float4Reg& lhs, const Float4Reg& rhs, const Float4Reg& addend)noexcept{
    return AddF32(MulF32(lhs, rhs), addend);
}

[[nodiscard]] NWB_INLINE Float4Reg SplatXF32(const Float4Reg& value)noexcept{
    return SetF32(value.lanes[0], value.lanes[0], value.lanes[0], value.lanes[0]);
}

[[nodiscard]] NWB_INLINE Float4Reg SplatYF32(const Float4Reg& value)noexcept{
    return SetF32(value.lanes[1], value.lanes[1], value.lanes[1], value.lanes[1]);
}

[[nodiscard]] NWB_INLINE Float4Reg SplatZF32(const Float4Reg& value)noexcept{
    return SetF32(value.lanes[2], value.lanes[2], value.lanes[2], value.lanes[2]);
}

[[nodiscard]] NWB_INLINE Float4Reg SplatWF32(const Float4Reg& value)noexcept{
    return SetF32(value.lanes[3], value.lanes[3], value.lanes[3], value.lanes[3]);
}

[[nodiscard]] NWB_INLINE f32 ExtractXF32(const Float4Reg& value)noexcept{
    return value.lanes[0];
}

[[nodiscard]] NWB_INLINE f32 ExtractYF32(const Float4Reg& value)noexcept{
    return value.lanes[1];
}

[[nodiscard]] NWB_INLINE f32 ExtractZF32(const Float4Reg& value)noexcept{
    return value.lanes[2];
}

[[nodiscard]] NWB_INLINE f32 ExtractWF32(const Float4Reg& value)noexcept{
    return value.lanes[3];
}

[[nodiscard]] NWB_INLINE f32 Dot3F32(const Float4Reg& lhs, const Float4Reg& rhs)noexcept{
    return lhs.lanes[0] * rhs.lanes[0] + lhs.lanes[1] * rhs.lanes[1] + lhs.lanes[2] * rhs.lanes[2];
}

[[nodiscard]] NWB_INLINE f32 Dot4F32(const Float4Reg& lhs, const Float4Reg& rhs)noexcept{
    return lhs.lanes[0] * rhs.lanes[0]
        + lhs.lanes[1] * rhs.lanes[1]
        + lhs.lanes[2] * rhs.lanes[2]
        + lhs.lanes[3] * rhs.lanes[3];
}

NWB_INLINE void Transpose4x4F32(Float4Reg& c0, Float4Reg& c1, Float4Reg& c2, Float4Reg& c3)noexcept{
    const Float4Reg oldC0 = c0;
    const Float4Reg oldC1 = c1;
    const Float4Reg oldC2 = c2;
    const Float4Reg oldC3 = c3;
    c0 = SetF32(oldC0.lanes[0], oldC1.lanes[0], oldC2.lanes[0], oldC3.lanes[0]);
    c1 = SetF32(oldC0.lanes[1], oldC1.lanes[1], oldC2.lanes[1], oldC3.lanes[1]);
    c2 = SetF32(oldC0.lanes[2], oldC1.lanes[2], oldC2.lanes[2], oldC3.lanes[2]);
    c3 = SetF32(oldC0.lanes[3], oldC1.lanes[3], oldC2.lanes[3], oldC3.lanes[3]);
}


[[nodiscard]] NWB_INLINE Double4Reg SetZeroF64()noexcept{
    Double4Reg result{};
    result.lanes[0] = 0.0;
    result.lanes[1] = 0.0;
    result.lanes[2] = 0.0;
    result.lanes[3] = 0.0;
    return result;
}

[[nodiscard]] NWB_INLINE Double4Reg SetF64(const f64 x, const f64 y, const f64 z, const f64 w)noexcept{
    Double4Reg result{};
    result.lanes[0] = x;
    result.lanes[1] = y;
    result.lanes[2] = z;
    result.lanes[3] = w;
    return result;
}

[[nodiscard]] NWB_INLINE Double4Reg LoadF64(NotNull<const f64*> values)noexcept{
    return SetF64(values.get()[0], values.get()[1], values.get()[2], values.get()[3]);
}

NWB_INLINE void StoreF64(NotNull<f64*> outValues, const Double4Reg& value)noexcept{
    outValues.get()[0] = value.lanes[0];
    outValues.get()[1] = value.lanes[1];
    outValues.get()[2] = value.lanes[2];
    outValues.get()[3] = value.lanes[3];
}

[[nodiscard]] NWB_INLINE Double4Reg AddF64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{
    return SetF64(
        lhs.lanes[0] + rhs.lanes[0],
        lhs.lanes[1] + rhs.lanes[1],
        lhs.lanes[2] + rhs.lanes[2],
        lhs.lanes[3] + rhs.lanes[3]
    );
}

[[nodiscard]] NWB_INLINE Double4Reg SubF64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{
    return SetF64(
        lhs.lanes[0] - rhs.lanes[0],
        lhs.lanes[1] - rhs.lanes[1],
        lhs.lanes[2] - rhs.lanes[2],
        lhs.lanes[3] - rhs.lanes[3]
    );
}

[[nodiscard]] NWB_INLINE Double4Reg MulF64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{
    return SetF64(
        lhs.lanes[0] * rhs.lanes[0],
        lhs.lanes[1] * rhs.lanes[1],
        lhs.lanes[2] * rhs.lanes[2],
        lhs.lanes[3] * rhs.lanes[3]
    );
}

[[nodiscard]] NWB_INLINE Double4Reg DivF64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{
    return SetF64(
        lhs.lanes[0] / rhs.lanes[0],
        lhs.lanes[1] / rhs.lanes[1],
        lhs.lanes[2] / rhs.lanes[2],
        lhs.lanes[3] / rhs.lanes[3]
    );
}

[[nodiscard]] NWB_INLINE Double4Reg MulAddF64(const Double4Reg& lhs, const Double4Reg& rhs, const Double4Reg& addend)noexcept{
    return AddF64(MulF64(lhs, rhs), addend);
}

[[nodiscard]] NWB_INLINE Double4Reg SplatXF64(const Double4Reg& value)noexcept{
    return SetF64(value.lanes[0], value.lanes[0], value.lanes[0], value.lanes[0]);
}

[[nodiscard]] NWB_INLINE Double4Reg SplatYF64(const Double4Reg& value)noexcept{
    return SetF64(value.lanes[1], value.lanes[1], value.lanes[1], value.lanes[1]);
}

[[nodiscard]] NWB_INLINE Double4Reg SplatZF64(const Double4Reg& value)noexcept{
    return SetF64(value.lanes[2], value.lanes[2], value.lanes[2], value.lanes[2]);
}

[[nodiscard]] NWB_INLINE Double4Reg SplatWF64(const Double4Reg& value)noexcept{
    return SetF64(value.lanes[3], value.lanes[3], value.lanes[3], value.lanes[3]);
}

[[nodiscard]] NWB_INLINE f64 ExtractXF64(const Double4Reg& value)noexcept{
    return value.lanes[0];
}

[[nodiscard]] NWB_INLINE f64 ExtractYF64(const Double4Reg& value)noexcept{
    return value.lanes[1];
}

[[nodiscard]] NWB_INLINE f64 ExtractZF64(const Double4Reg& value)noexcept{
    return value.lanes[2];
}

[[nodiscard]] NWB_INLINE f64 ExtractWF64(const Double4Reg& value)noexcept{
    return value.lanes[3];
}

[[nodiscard]] NWB_INLINE f64 Dot3F64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{
    return lhs.lanes[0] * rhs.lanes[0] + lhs.lanes[1] * rhs.lanes[1] + lhs.lanes[2] * rhs.lanes[2];
}

[[nodiscard]] NWB_INLINE f64 Dot4F64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{
    return lhs.lanes[0] * rhs.lanes[0]
        + lhs.lanes[1] * rhs.lanes[1]
        + lhs.lanes[2] * rhs.lanes[2]
        + lhs.lanes[3] * rhs.lanes[3];
}

NWB_INLINE void Transpose4x4F64(Double4Reg& c0, Double4Reg& c1, Double4Reg& c2, Double4Reg& c3)noexcept{
    const Double4Reg oldC0 = c0;
    const Double4Reg oldC1 = c1;
    const Double4Reg oldC2 = c2;
    const Double4Reg oldC3 = c3;
    c0 = SetF64(oldC0.lanes[0], oldC1.lanes[0], oldC2.lanes[0], oldC3.lanes[0]);
    c1 = SetF64(oldC0.lanes[1], oldC1.lanes[1], oldC2.lanes[1], oldC3.lanes[1]);
    c2 = SetF64(oldC0.lanes[2], oldC1.lanes[2], oldC2.lanes[2], oldC3.lanes[2]);
    c3 = SetF64(oldC0.lanes[3], oldC1.lanes[3], oldC2.lanes[3], oldC3.lanes[3]);
}


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

