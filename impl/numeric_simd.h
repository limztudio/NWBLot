// Runtime numeric boundaries that retain the scalar expression's precision and comparison policy.
#pragma once

#include <global/math/vector_arithmetic.h>
#include <global/math/vector_double.h>

namespace NumericSimd{

[[nodiscard]] NWB_INLINE f32 Add(const f32 a, const f32 b)noexcept{ return VectorGetX(VectorAdd(VectorReplicate(a), VectorReplicate(b))); }
[[nodiscard]] NWB_INLINE f32 Subtract(const f32 a, const f32 b)noexcept{ return VectorGetX(VectorSubtract(VectorReplicate(a), VectorReplicate(b))); }
[[nodiscard]] NWB_INLINE f32 Multiply(const f32 a, const f32 b)noexcept{ return VectorGetX(VectorMultiply(VectorReplicate(a), VectorReplicate(b))); }
[[nodiscard]] NWB_INLINE f32 Divide(const f32 a, const f32 b)noexcept{
#if defined(NWB_HAS_NEON) && !defined(__aarch64__) && !defined(_M_ARM64)
    // ARM32's vector backend estimates the reciprocal; numeric admission needs the original rounded quotient.
    return a / b;
#else
    return VectorGetX(VectorDivide(VectorReplicate(a), VectorReplicate(b)));
#endif
}
[[nodiscard]] NWB_INLINE f32 Abs(const f32 value)noexcept{
    const SIMDVector lane = VectorReplicate(value);
    return VectorGetX(VectorSelect(lane, VectorNegate(lane), VectorLess(lane, VectorZero())));
}
[[nodiscard]] NWB_INLINE f32 Min(const f32 a, const f32 b)noexcept{
    const SIMDVector first = VectorReplicate(a), second = VectorReplicate(b);
    return VectorGetX(VectorSelect(second, first, VectorLess(first, second)));
}
[[nodiscard]] NWB_INLINE f32 Max(const f32 a, const f32 b)noexcept{
    const SIMDVector first = VectorReplicate(a), second = VectorReplicate(b);
    return VectorGetX(VectorSelect(second, first, VectorGreater(first, second)));
}
[[nodiscard]] NWB_INLINE f32 Clamp(const f32 value, const f32 minimum, const f32 maximum)noexcept{
    const SIMDVector lane = VectorReplicate(value), lo = VectorReplicate(minimum), hi = VectorReplicate(maximum);
    if(VectorGetIntX(VectorGreater(lane, lo)) == 0u)
        return minimum;
    return VectorGetX(VectorSelect(lane, hi, VectorGreater(lane, hi)));
}

[[nodiscard]] NWB_INLINE f64 Add(const f64 a, const f64 b)noexcept{ return (SIMDVectorDouble{ a, a } + SIMDVectorDouble{ b, b }).x; }
[[nodiscard]] NWB_INLINE f64 Subtract(const f64 a, const f64 b)noexcept{ return (SIMDVectorDouble{ a, a } - SIMDVectorDouble{ b, b }).x; }
[[nodiscard]] NWB_INLINE f64 Multiply(const f64 a, const f64 b)noexcept{ return (SIMDVectorDouble{ a, a } * SIMDVectorDouble{ b, b }).x; }
[[nodiscard]] NWB_INLINE f64 Divide(const f64 a, const f64 b)noexcept{ return (SIMDVectorDouble{ a, a } / SIMDVectorDouble{ b, b }).x; }
[[nodiscard]] NWB_INLINE f64 Negate(const f64 value)noexcept{ return (-SIMDVectorDouble{ value, value }).x; }
[[nodiscard]] NWB_INLINE f64 Abs(const f64 value)noexcept{
    const SIMDVectorDouble lane{ value, value };
    return ((lane < SIMDVectorDouble{}) ? -lane : lane).x;
}
[[nodiscard]] NWB_INLINE f64 Min(const f64 a, const f64 b)noexcept{
    const SIMDVectorDouble first{ a, a }, second{ b, b };
    return ((first < second) ? first : second).x;
}
[[nodiscard]] NWB_INLINE f64 Max(const f64 a, const f64 b)noexcept{
    const SIMDVectorDouble first{ a, a }, second{ b, b };
    return ((first > second) ? first : second).x;
}
[[nodiscard]] NWB_INLINE f64 Clamp(const f64 value, const f64 minimum, const f64 maximum)noexcept{
    const SIMDVectorDouble lane{ value, value }, lo{ minimum, minimum }, hi{ maximum, maximum };
    if((lane > lo).x == 0)
        return minimum;
    return ((lane > hi) ? hi : lane).x;
}

}
