// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector3.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool SIMDCALL Vector4Equal(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorEqual(v0, v1)) & VectorComponentMask::s_XYZW) == VectorComponentMask::s_XYZW; }
NWB_INLINE bool SIMDCALL Vector4EqualInt(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorEqualInt(v0, v1)) & VectorComponentMask::s_XYZW) == VectorComponentMask::s_XYZW; }
NWB_INLINE bool SIMDCALL Vector4NearEqual(SIMDVector v0, SIMDVector v1, SIMDVector epsilon)noexcept{ return (VectorMoveMask(VectorNearEqual(v0, v1, epsilon)) & VectorComponentMask::s_XYZW) == VectorComponentMask::s_XYZW; }
NWB_INLINE bool SIMDCALL Vector4NotEqual(SIMDVector v0, SIMDVector v1)noexcept{ return !Vector4Equal(v0, v1); }
NWB_INLINE bool SIMDCALL Vector4NotEqualInt(SIMDVector v0, SIMDVector v1)noexcept{ return !Vector4EqualInt(v0, v1); }
NWB_INLINE bool SIMDCALL Vector4Greater(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorGreater(v0, v1)) & VectorComponentMask::s_XYZW) == VectorComponentMask::s_XYZW; }
NWB_INLINE bool SIMDCALL Vector4GreaterOrEqual(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorGreaterOrEqual(v0, v1)) & VectorComponentMask::s_XYZW) == VectorComponentMask::s_XYZW; }
NWB_INLINE bool SIMDCALL Vector4Less(SIMDVector v0, SIMDVector v1)noexcept{ return Vector4Greater(v1, v0); }
NWB_INLINE bool SIMDCALL Vector4LessOrEqual(SIMDVector v0, SIMDVector v1)noexcept{ return Vector4GreaterOrEqual(v1, v0); }
NWB_INLINE bool SIMDCALL Vector4InBounds(SIMDVector value, SIMDVector bounds)noexcept{ return (VectorMoveMask(VectorInBounds(value, bounds)) & VectorComponentMask::s_XYZW) == VectorComponentMask::s_XYZW; }
NWB_INLINE bool SIMDCALL Vector4IsNaN(SIMDVector value)noexcept{ return (VectorMoveMask(VectorIsNaN(value)) & VectorComponentMask::s_XYZW) != 0; }
NWB_INLINE bool SIMDCALL Vector4IsInfinite(SIMDVector value)noexcept{ return (VectorMoveMask(VectorIsInfinite(value)) & VectorComponentMask::s_XYZW) != 0; }

NWB_INLINE u32 SIMDCALL Vector4EqualR(SIMDVector v0, SIMDVector v1)noexcept{ return VectorEqualR(v0, v1); }
NWB_INLINE u32 SIMDCALL Vector4EqualIntR(SIMDVector v0, SIMDVector v1)noexcept{ return VectorEqualIntR(v0, v1); }
NWB_INLINE u32 SIMDCALL Vector4GreaterR(SIMDVector v0, SIMDVector v1)noexcept{ return VectorGreaterR(v0, v1); }
NWB_INLINE u32 SIMDCALL Vector4GreaterOrEqualR(SIMDVector v0, SIMDVector v1)noexcept{ return VectorGreaterOrEqualR(v0, v1); }
NWB_INLINE u32 SIMDCALL Vector4InBoundsR(SIMDVector value, SIMDVector bounds)noexcept{ return VectorInBoundsR(value, bounds); }

NWB_INLINE SIMDVector SIMDCALL Vector4Dot(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(VectorGetX(v0) * VectorGetX(v1) + VectorGetY(v0) * VectorGetY(v1) + VectorGetZ(v0) * VectorGetZ(v1) + VectorGetW(v0) * VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    float32x4_t product = vmulq_f32(v0, v1);
    float32x2_t lo = vget_low_f32(product);
    float32x2_t hi = vget_high_f32(product);
    lo = vadd_f32(lo, hi);
    lo = vpadd_f32(lo, lo);
    return vcombine_f32(lo, lo);
#else
    return _mm_dp_ps(v0, v1, 0xFF);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4LengthSq(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_dp_ps(value, value, 0xFF);
#else
    return Vector4Dot(value, value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4ReciprocalLength(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector lengthSq = _mm_dp_ps(value, value, 0xFF);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    return _mm_div_ps(s_SIMDOne, length);
#else
    return VectorReciprocalSqrt(Vector4LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4ReciprocalLengthEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_rsqrt_ps(_mm_dp_ps(value, value, 0xFF));
#else
    return VectorReciprocalSqrtEst(Vector4LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4Length(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0xFF));
#else
    return VectorSqrt(Vector4LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4LengthEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0xFF));
#else
    return VectorSqrtEst(Vector4LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4Normalize(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    SIMDVector lengthSq = _mm_dp_ps(value, value, 0xFF);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    const SIMDVector zeroMask = _mm_cmpneq_ps(_mm_setzero_ps(), length);
    lengthSq = _mm_cmpneq_ps(lengthSq, s_SIMDInfinity);
    SIMDVector result = _mm_div_ps(value, length);
    result = _mm_and_ps(result, zeroMask);
    return _mm_or_ps(_mm_andnot_ps(lengthSq, s_SIMDQNaN), _mm_and_ps(result, lengthSq));
#else
    const SIMDVector lengthSq = Vector4LengthSq(value);
    const SIMDVector length = VectorSqrt(lengthSq);
    SIMDVector result = VectorDivide(value, length);
    result = VectorAndInt(result, VectorNotEqual(length, VectorZero()));
    return VectorSelect(s_SIMDQNaN, result, VectorNotEqualInt(lengthSq, s_SIMDInfinity));
#endif
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL Vector4NormalizeOr(SIMDVector value, SIMDVector fallback, const f32 minLengthSquared)noexcept{
    const SIMDVector lengthSquared = Vector4LengthSq(value);
    return SIMDVectorDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, VectorTrueInt());
}

NWB_INLINE SIMDVector SIMDCALL Vector4NormalizeEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector lengthSq = _mm_dp_ps(value, value, 0xFF);
    return _mm_mul_ps(value, _mm_rsqrt_ps(lengthSq));
#else
    return VectorMultiply(value, Vector4ReciprocalLengthEst(value));
#endif
}
NWB_INLINE SIMDVector SIMDCALL Vector4ClampLengthV(SIMDVector value, SIMDVector lengthMin, SIMDVector lengthMax)noexcept{
    NWB_ASSERT((VectorGetY(lengthMin) == VectorGetX(lengthMin)) && (VectorGetZ(lengthMin) == VectorGetX(lengthMin)) && (VectorGetW(lengthMin) == VectorGetX(lengthMin)));
    NWB_ASSERT((VectorGetY(lengthMax) == VectorGetX(lengthMax)) && (VectorGetZ(lengthMax) == VectorGetX(lengthMax)) && (VectorGetW(lengthMax) == VectorGetX(lengthMax)));
    NWB_ASSERT(Vector4GreaterOrEqual(lengthMin, s_SIMDZero));
    NWB_ASSERT(Vector4GreaterOrEqual(lengthMax, s_SIMDZero));
    NWB_ASSERT(Vector4GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVector lengthSq = Vector4LengthSq(value);
    return SIMDVectorDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax);
}

NWB_INLINE SIMDVector SIMDCALL Vector4ClampLength(SIMDVector value, f32 lengthMin, f32 lengthMax)noexcept{
    return Vector4ClampLengthV(value, VectorReplicate(lengthMin), VectorReplicate(lengthMax));
}
NWB_INLINE SIMDVector SIMDCALL Vector4Reflect(SIMDVector incident, SIMDVector normal)noexcept{
    SIMDVector result = Vector4Dot(incident, normal);
    result = VectorAdd(result, result);
    return VectorNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVector SIMDCALL Vector4RefractV(SIMDVector incident, SIMDVector normal, SIMDVector refractionIndex)noexcept{
    return SIMDVectorDetail::RefractV(incident, normal, refractionIndex, Vector4Dot(incident, normal));
}

NWB_INLINE SIMDVector SIMDCALL Vector4Refract(SIMDVector incident, SIMDVector normal, f32 refractionIndex)noexcept{ return Vector4RefractV(incident, normal, VectorReplicate(refractionIndex)); }
NWB_INLINE SIMDVector SIMDCALL Vector4Orthogonal(SIMDVector value)noexcept{ return VectorMultiply(VectorSwizzle<2, 3, 0, 1>(value), VectorSet(1.0f, 1.0f, -1.0f, -1.0f)); }
NWB_INLINE SIMDVector SIMDCALL Vector4AngleBetweenNormals(SIMDVector n0, SIMDVector n1)noexcept{ return VectorACos(VectorClamp(Vector4Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVector SIMDCALL Vector4AngleBetweenNormalsEst(SIMDVector n0, SIMDVector n1)noexcept{ return VectorACosEst(VectorClamp(Vector4Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVector SIMDCALL Vector4AngleBetweenVectors(SIMDVector v0, SIMDVector v1)noexcept{
    const SIMDVector reciprocalLength0 = Vector4ReciprocalLength(v0);
    const SIMDVector reciprocalLength1 = Vector4ReciprocalLength(v1);
    SIMDVector cosAngle = VectorMultiply(Vector4Dot(v0, v1), VectorMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorClamp(cosAngle, s_SIMDNegativeOne, s_SIMDOne);
    return VectorACos(cosAngle);
}

NWB_INLINE SIMDVector SIMDCALL Vector4Cross(SIMDVector v1, SIMDVector v2, SIMDVector v3)noexcept{
    SIMDVector result = VectorMultiply(VectorSwizzle<2, 3, 1, 2>(v2), VectorSwizzle<3, 2, 3, 1>(v3));
    SIMDVector temp = VectorSwizzle<3, 2, 3, 1>(v2);
    SIMDVector temp3 = VectorSwizzle<2, 3, 1, 2>(v3);
    result = VectorNegativeMultiplySubtract(temp, temp3, result);
    result = VectorMultiply(result, VectorSwizzle<1, 0, 0, 0>(v1));

    temp = VectorSwizzle<1, 3, 0, 2>(v2);
    temp3 = VectorSwizzle<3, 0, 3, 0>(v3);
    temp3 = VectorMultiply(temp3, temp);
    temp = VectorSwizzle<3, 0, 3, 0>(temp);
    SIMDVector temp1 = VectorSwizzle<1, 3, 0, 2>(v3);
    temp3 = VectorNegativeMultiplySubtract(temp, temp1, temp3);
    temp1 = VectorSwizzle<2, 2, 1, 1>(v1);
    result = VectorNegativeMultiplySubtract(temp1, temp3, result);

    temp = VectorSwizzle<1, 2, 0, 1>(v2);
    temp3 = VectorSwizzle<2, 0, 1, 0>(v3);
    temp3 = VectorMultiply(temp3, temp);
    temp = VectorSwizzle<1, 2, 0, 2>(temp);
    temp1 = VectorSwizzle<1, 2, 0, 1>(v3);
    temp3 = VectorNegativeMultiplySubtract(temp1, temp, temp3);
    temp1 = VectorSwizzle<3, 3, 3, 2>(v1);
    return VectorNegate(VectorMultiplyAdd(temp3, temp1, result));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

