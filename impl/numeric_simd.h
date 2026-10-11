// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/math/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Runtime numeric boundaries retain the scalar expression's precision and comparison policy.
namespace NumericSimd{

[[nodiscard]] NWB_INLINE f32 Add(const f32 a, const f32 b)noexcept{ return VectorGetX(VectorAdd(VectorReplicate(a), VectorReplicate(b))); }
[[nodiscard]] NWB_INLINE f32 Subtract(const f32 a, const f32 b)noexcept{ return VectorGetX(VectorSubtract(VectorReplicate(a), VectorReplicate(b))); }
[[nodiscard]] NWB_INLINE f32 Multiply(const f32 a, const f32 b)noexcept{ return VectorGetX(VectorMultiply(VectorReplicate(a), VectorReplicate(b))); }
[[nodiscard]] NWB_INLINE f32 Divide(const f32 a, const f32 b)noexcept{
#if defined(NWB_HAS_NEON) && !defined(NWB_HAS_NEON64)
    // ARM32's vector backend estimates the reciprocal; numeric admission needs the original rounded quotient.
    return a / b;
#else
    return VectorGetX(VectorDivide(VectorReplicate(a), VectorReplicate(b)));
#endif
}
[[nodiscard]] NWB_INLINE f32 Abs(const f32 value)noexcept{
    const SIMDVectorSingle lane = VectorReplicate(value);
    return VectorGetX(VectorSelect(lane, VectorNegate(lane), VectorLess(lane, VectorZero())));
}
[[nodiscard]] NWB_INLINE f32 Min(const f32 a, const f32 b)noexcept{
    const SIMDVectorSingle first = VectorReplicate(a), second = VectorReplicate(b);
    return VectorGetX(VectorSelect(second, first, VectorLess(first, second)));
}
[[nodiscard]] NWB_INLINE f32 Max(const f32 a, const f32 b)noexcept{
    const SIMDVectorSingle first = VectorReplicate(a), second = VectorReplicate(b);
    return VectorGetX(VectorSelect(second, first, VectorGreater(first, second)));
}
[[nodiscard]] NWB_INLINE f32 Clamp(const f32 value, const f32 minimum, const f32 maximum)noexcept{
    const SIMDVectorSingle lane = VectorReplicate(value), lo = VectorReplicate(minimum), hi = VectorReplicate(maximum);
    if(VectorGetIntX(VectorGreater(lane, lo)) == 0u)
        return minimum;
    return VectorGetX(VectorSelect(lane, hi, VectorGreater(lane, hi)));
}

[[nodiscard]] NWB_INLINE f64 Add(const f64 a, const f64 b)noexcept{ return VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(a), VectorDoubleReplicate(b))); }
[[nodiscard]] NWB_INLINE f64 Subtract(const f64 a, const f64 b)noexcept{ return VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(a), VectorDoubleReplicate(b))); }
[[nodiscard]] NWB_INLINE f64 Multiply(const f64 a, const f64 b)noexcept{ return VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(a), VectorDoubleReplicate(b))); }
[[nodiscard]] NWB_INLINE f64 Divide(const f64 a, const f64 b)noexcept{ return VectorDoubleGetX(VectorDoubleDivide(VectorDoubleReplicate(a), VectorDoubleReplicate(b))); }
[[nodiscard]] NWB_INLINE f64 Negate(const f64 value)noexcept{ return VectorDoubleGetX(VectorDoubleNegate(VectorDoubleReplicate(value))); }
[[nodiscard]] NWB_INLINE f64 Abs(const f64 value)noexcept{
    const SIMDVectorDouble lane = VectorDoubleReplicate(value);
    return VectorDoubleGetX(VectorDoubleSelect(lane, VectorDoubleNegate(lane), VectorDoubleLess(lane, VectorDoubleZero())));
}
[[nodiscard]] NWB_INLINE f64 Min(const f64 a, const f64 b)noexcept{
    const SIMDVectorDouble first = VectorDoubleReplicate(a), second = VectorDoubleReplicate(b);
    return VectorDoubleGetX(VectorDoubleSelect(second, first, VectorDoubleLess(first, second)));
}
[[nodiscard]] NWB_INLINE f64 Max(const f64 a, const f64 b)noexcept{
    const SIMDVectorDouble first = VectorDoubleReplicate(a), second = VectorDoubleReplicate(b);
    return VectorDoubleGetX(VectorDoubleSelect(second, first, VectorDoubleGreater(first, second)));
}
[[nodiscard]] NWB_INLINE f64 Clamp(const f64 value, const f64 minimum, const f64 maximum)noexcept{
    const SIMDVectorDouble lane = VectorDoubleReplicate(value), lo = VectorDoubleReplicate(minimum), hi = VectorDoubleReplicate(maximum);
    if(VectorDoubleGetIntX(VectorDoubleGreater(lane, lo)) == 0u)
        return minimum;
    return VectorDoubleGetX(VectorDoubleSelect(lane, hi, VectorDoubleGreater(lane, hi)));
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

