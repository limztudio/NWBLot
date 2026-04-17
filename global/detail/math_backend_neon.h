// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#ifndef NWB_MATH_FRAGMENT_INCLUDE
#error Include global/math.h instead of including math_backend_neon.h directly.
#endif


#if defined(NWB_MATH_BACKEND_NEON)
namespace Neon{


using Float4Reg = float32x4_t;

#if defined(__aarch64__) || defined(_M_ARM64)
struct Double4Reg{
    float64x2_t lo;
    float64x2_t hi;
};
#else
struct Double4Reg{
    f64 lanes[4];
};
#endif


[[nodiscard]] NWB_INLINE Float4Reg SetZeroF32()noexcept{ return vdupq_n_f32(0.0f); }
[[nodiscard]] NWB_INLINE Float4Reg SetF32(const f32 x, const f32 y, const f32 z, const f32 w)noexcept{
    const f32 values[4] = { x, y, z, w };
    return vld1q_f32(values);
}
[[nodiscard]] NWB_INLINE Float4Reg LoadF32(NotNull<const f32*> values)noexcept{ return vld1q_f32(values.get()); }
NWB_INLINE void StoreF32(NotNull<f32*> outValues, const Float4Reg value)noexcept{ vst1q_f32(outValues.get(), value); }
[[nodiscard]] NWB_INLINE Float4Reg AddF32(const Float4Reg lhs, const Float4Reg rhs)noexcept{ return vaddq_f32(lhs, rhs); }
[[nodiscard]] NWB_INLINE Float4Reg SubF32(const Float4Reg lhs, const Float4Reg rhs)noexcept{ return vsubq_f32(lhs, rhs); }
[[nodiscard]] NWB_INLINE Float4Reg MulF32(const Float4Reg lhs, const Float4Reg rhs)noexcept{ return vmulq_f32(lhs, rhs); }
[[nodiscard]] NWB_INLINE Float4Reg DivF32(const Float4Reg lhs, const Float4Reg rhs)noexcept{
#if defined(__aarch64__) || defined(_M_ARM64)
    return vdivq_f32(lhs, rhs);
#else
    f32 lhsValues[4];
    f32 rhsValues[4];
    StoreF32(MakeNotNull(lhsValues), lhs);
    StoreF32(MakeNotNull(rhsValues), rhs);
    return SetF32(
        lhsValues[0] / rhsValues[0],
        lhsValues[1] / rhsValues[1],
        lhsValues[2] / rhsValues[2],
        lhsValues[3] / rhsValues[3]
    );
#endif
}
[[nodiscard]] NWB_INLINE Float4Reg MulAddF32(const Float4Reg lhs, const Float4Reg rhs, const Float4Reg addend)noexcept{
#if defined(__aarch64__) || defined(_M_ARM64)
    return vfmaq_f32(addend, lhs, rhs);
#else
    return AddF32(MulF32(lhs, rhs), addend);
#endif
}
[[nodiscard]] NWB_INLINE Float4Reg SplatXF32(const Float4Reg value)noexcept{ return vdupq_n_f32(vgetq_lane_f32(value, 0)); }
[[nodiscard]] NWB_INLINE Float4Reg SplatYF32(const Float4Reg value)noexcept{ return vdupq_n_f32(vgetq_lane_f32(value, 1)); }
[[nodiscard]] NWB_INLINE Float4Reg SplatZF32(const Float4Reg value)noexcept{ return vdupq_n_f32(vgetq_lane_f32(value, 2)); }
[[nodiscard]] NWB_INLINE Float4Reg SplatWF32(const Float4Reg value)noexcept{ return vdupq_n_f32(vgetq_lane_f32(value, 3)); }
[[nodiscard]] NWB_INLINE f32 ExtractXF32(const Float4Reg value)noexcept{ return vgetq_lane_f32(value, 0); }
[[nodiscard]] NWB_INLINE f32 ExtractYF32(const Float4Reg value)noexcept{ return vgetq_lane_f32(value, 1); }
[[nodiscard]] NWB_INLINE f32 ExtractZF32(const Float4Reg value)noexcept{ return vgetq_lane_f32(value, 2); }
[[nodiscard]] NWB_INLINE f32 ExtractWF32(const Float4Reg value)noexcept{ return vgetq_lane_f32(value, 3); }
[[nodiscard]] NWB_INLINE f32 Dot3F32(const Float4Reg lhs, const Float4Reg rhs)noexcept{
    const Float4Reg product = MulF32(lhs, rhs);
    return ExtractXF32(product) + ExtractYF32(product) + ExtractZF32(product);
}
[[nodiscard]] NWB_INLINE f32 Dot4F32(const Float4Reg lhs, const Float4Reg rhs)noexcept{
    const Float4Reg product = MulF32(lhs, rhs);
    return ExtractXF32(product) + ExtractYF32(product) + ExtractZF32(product) + ExtractWF32(product);
}
NWB_INLINE void Transpose4x4F32(Float4Reg& c0, Float4Reg& c1, Float4Reg& c2, Float4Reg& c3)noexcept{
    const float32x4x2_t t0 = vtrnq_f32(c0, c1);
    const float32x4x2_t t1 = vtrnq_f32(c2, c3);
    c0 = vcombine_f32(vget_low_f32(t0.val[0]), vget_low_f32(t1.val[0]));
    c1 = vcombine_f32(vget_low_f32(t0.val[1]), vget_low_f32(t1.val[1]));
    c2 = vcombine_f32(vget_high_f32(t0.val[0]), vget_high_f32(t1.val[0]));
    c3 = vcombine_f32(vget_high_f32(t0.val[1]), vget_high_f32(t1.val[1]));
}

#if defined(__aarch64__) || defined(_M_ARM64)
[[nodiscard]] NWB_INLINE Double4Reg SetZeroF64()noexcept{
    Double4Reg result{};
    result.lo = vdupq_n_f64(0.0);
    result.hi = vdupq_n_f64(0.0);
    return result;
}

[[nodiscard]] NWB_INLINE Double4Reg SetF64(const f64 x, const f64 y, const f64 z, const f64 w)noexcept{
    Double4Reg result{};
    const f64 loValues[2] = { x, y };
    const f64 hiValues[2] = { z, w };
    result.lo = vld1q_f64(loValues);
    result.hi = vld1q_f64(hiValues);
    return result;
}

[[nodiscard]] NWB_INLINE Double4Reg LoadF64(NotNull<const f64*> values)noexcept{
    Double4Reg result{};
    result.lo = vld1q_f64(values.get());
    result.hi = vld1q_f64(values.get() + 2);
    return result;
}

NWB_INLINE void StoreF64(NotNull<f64*> outValues, const Double4Reg& value)noexcept{
    vst1q_f64(outValues.get(), value.lo);
    vst1q_f64(outValues.get() + 2, value.hi);
}

[[nodiscard]] NWB_INLINE Double4Reg AddF64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{
    Double4Reg result{};
    result.lo = vaddq_f64(lhs.lo, rhs.lo);
    result.hi = vaddq_f64(lhs.hi, rhs.hi);
    return result;
}

[[nodiscard]] NWB_INLINE Double4Reg SubF64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{
    Double4Reg result{};
    result.lo = vsubq_f64(lhs.lo, rhs.lo);
    result.hi = vsubq_f64(lhs.hi, rhs.hi);
    return result;
}

[[nodiscard]] NWB_INLINE Double4Reg MulF64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{
    Double4Reg result{};
    result.lo = vmulq_f64(lhs.lo, rhs.lo);
    result.hi = vmulq_f64(lhs.hi, rhs.hi);
    return result;
}

[[nodiscard]] NWB_INLINE Double4Reg DivF64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{
    Double4Reg result{};
    result.lo = vdivq_f64(lhs.lo, rhs.lo);
    result.hi = vdivq_f64(lhs.hi, rhs.hi);
    return result;
}

[[nodiscard]] NWB_INLINE Double4Reg MulAddF64(const Double4Reg& lhs, const Double4Reg& rhs, const Double4Reg& addend)noexcept{
    Double4Reg result{};
    result.lo = vfmaq_f64(addend.lo, lhs.lo, rhs.lo);
    result.hi = vfmaq_f64(addend.hi, lhs.hi, rhs.hi);
    return result;
}

[[nodiscard]] NWB_INLINE Double4Reg SplatXF64(const Double4Reg& value)noexcept{ return SetF64(vgetq_lane_f64(value.lo, 0), vgetq_lane_f64(value.lo, 0), vgetq_lane_f64(value.lo, 0), vgetq_lane_f64(value.lo, 0)); }
[[nodiscard]] NWB_INLINE Double4Reg SplatYF64(const Double4Reg& value)noexcept{ return SetF64(vgetq_lane_f64(value.lo, 1), vgetq_lane_f64(value.lo, 1), vgetq_lane_f64(value.lo, 1), vgetq_lane_f64(value.lo, 1)); }
[[nodiscard]] NWB_INLINE Double4Reg SplatZF64(const Double4Reg& value)noexcept{ return SetF64(vgetq_lane_f64(value.hi, 0), vgetq_lane_f64(value.hi, 0), vgetq_lane_f64(value.hi, 0), vgetq_lane_f64(value.hi, 0)); }
[[nodiscard]] NWB_INLINE Double4Reg SplatWF64(const Double4Reg& value)noexcept{ return SetF64(vgetq_lane_f64(value.hi, 1), vgetq_lane_f64(value.hi, 1), vgetq_lane_f64(value.hi, 1), vgetq_lane_f64(value.hi, 1)); }
[[nodiscard]] NWB_INLINE f64 ExtractXF64(const Double4Reg& value)noexcept{ return vgetq_lane_f64(value.lo, 0); }
[[nodiscard]] NWB_INLINE f64 ExtractYF64(const Double4Reg& value)noexcept{ return vgetq_lane_f64(value.lo, 1); }
[[nodiscard]] NWB_INLINE f64 ExtractZF64(const Double4Reg& value)noexcept{ return vgetq_lane_f64(value.hi, 0); }
[[nodiscard]] NWB_INLINE f64 ExtractWF64(const Double4Reg& value)noexcept{ return vgetq_lane_f64(value.hi, 1); }
[[nodiscard]] NWB_INLINE f64 Dot3F64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{
    const Double4Reg product = MulF64(lhs, rhs);
    return ExtractXF64(product) + ExtractYF64(product) + ExtractZF64(product);
}
[[nodiscard]] NWB_INLINE f64 Dot4F64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{
    const Double4Reg product = MulF64(lhs, rhs);
    return ExtractXF64(product) + ExtractYF64(product) + ExtractZF64(product) + ExtractWF64(product);
}
NWB_INLINE void Transpose4x4F64(Double4Reg& c0, Double4Reg& c1, Double4Reg& c2, Double4Reg& c3)noexcept{
    const float64x2x2_t t0 = vtrnq_f64(c0.lo, c1.lo);
    const float64x2x2_t t1 = vtrnq_f64(c2.lo, c3.lo);
    const float64x2x2_t t2 = vtrnq_f64(c0.hi, c1.hi);
    const float64x2x2_t t3 = vtrnq_f64(c2.hi, c3.hi);
    c0.lo = t0.val[0];
    c0.hi = t1.val[0];
    c1.lo = t0.val[1];
    c1.hi = t1.val[1];
    c2.lo = t2.val[0];
    c2.hi = t3.val[0];
    c3.lo = t2.val[1];
    c3.hi = t3.val[1];
}
#else
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

[[nodiscard]] NWB_INLINE Double4Reg SplatXF64(const Double4Reg& value)noexcept{ return SetF64(value.lanes[0], value.lanes[0], value.lanes[0], value.lanes[0]); }
[[nodiscard]] NWB_INLINE Double4Reg SplatYF64(const Double4Reg& value)noexcept{ return SetF64(value.lanes[1], value.lanes[1], value.lanes[1], value.lanes[1]); }
[[nodiscard]] NWB_INLINE Double4Reg SplatZF64(const Double4Reg& value)noexcept{ return SetF64(value.lanes[2], value.lanes[2], value.lanes[2], value.lanes[2]); }
[[nodiscard]] NWB_INLINE Double4Reg SplatWF64(const Double4Reg& value)noexcept{ return SetF64(value.lanes[3], value.lanes[3], value.lanes[3], value.lanes[3]); }
[[nodiscard]] NWB_INLINE f64 ExtractXF64(const Double4Reg& value)noexcept{ return value.lanes[0]; }
[[nodiscard]] NWB_INLINE f64 ExtractYF64(const Double4Reg& value)noexcept{ return value.lanes[1]; }
[[nodiscard]] NWB_INLINE f64 ExtractZF64(const Double4Reg& value)noexcept{ return value.lanes[2]; }
[[nodiscard]] NWB_INLINE f64 ExtractWF64(const Double4Reg& value)noexcept{ return value.lanes[3]; }
[[nodiscard]] NWB_INLINE f64 Dot3F64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{ return lhs.lanes[0] * rhs.lanes[0] + lhs.lanes[1] * rhs.lanes[1] + lhs.lanes[2] * rhs.lanes[2]; }
[[nodiscard]] NWB_INLINE f64 Dot4F64(const Double4Reg& lhs, const Double4Reg& rhs)noexcept{ return lhs.lanes[0] * rhs.lanes[0] + lhs.lanes[1] * rhs.lanes[1] + lhs.lanes[2] * rhs.lanes[2] + lhs.lanes[3] * rhs.lanes[3]; }
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
#endif


}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

