// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector3.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool NWB_SIMD_CALL Vector4Equal(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return SIMDVectorDetail::AllComparisonMask(VectorEqual(v0, v1)); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble4Equal(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::AllComparisonMask<VectorDoubleComponentMask::s_DoubleXYZW>(VectorDoubleEqual(v0, v1)); }

NWB_INLINE bool NWB_SIMD_CALL Vector4EqualInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return SIMDVectorDetail::AllComparisonMask(VectorEqualInt(v0, v1)); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble4EqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::AllComparisonMask<VectorDoubleComponentMask::s_DoubleXYZW>(VectorDoubleEqualInt(v0, v1)); }

NWB_INLINE bool NWB_SIMD_CALL Vector4NearEqual(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle epsilon)noexcept{ return SIMDVectorDetail::AllComparisonMask(VectorNearEqual(v0, v1, epsilon)); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble4NearEqual(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble epsilon)noexcept{
    return SIMDVectorDoubleDetail::AllComparisonMask<VectorDoubleComponentMask::s_DoubleXYZW>(VectorDoubleNearEqual(v0, v1, epsilon));
}

NWB_INLINE bool NWB_SIMD_CALL Vector4NotEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return !Vector4Equal(v0, v1); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble4NotEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return !VectorDouble4Equal(v0, v1); }

NWB_INLINE bool NWB_SIMD_CALL Vector4NotEqualInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return !Vector4EqualInt(v0, v1); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble4NotEqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return !VectorDouble4EqualInt(v0, v1); }

NWB_INLINE bool NWB_SIMD_CALL Vector4Greater(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return SIMDVectorDetail::AllComparisonMask(VectorGreater(v0, v1)); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble4Greater(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::AllComparisonMask<VectorDoubleComponentMask::s_DoubleXYZW>(VectorDoubleGreater(v0, v1)); }

NWB_INLINE bool NWB_SIMD_CALL Vector4GreaterOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return SIMDVectorDetail::AllComparisonMask(VectorGreaterOrEqual(v0, v1)); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble4GreaterOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return SIMDVectorDoubleDetail::AllComparisonMask<VectorDoubleComponentMask::s_DoubleXYZW>(VectorDoubleGreaterOrEqual(v0, v1));
}

NWB_INLINE bool NWB_SIMD_CALL Vector4Less(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return Vector4Greater(v1, v0); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble4Less(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDouble4Greater(v1, v0); }

NWB_INLINE bool NWB_SIMD_CALL Vector4LessOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return Vector4GreaterOrEqual(v1, v0); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble4LessOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDouble4GreaterOrEqual(v1, v0); }

NWB_INLINE bool NWB_SIMD_CALL Vector4InBounds(SIMDVectorSingle value, SIMDVectorSingle bounds)noexcept{ return SIMDVectorDetail::AllComparisonMask(VectorInBounds(value, bounds)); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble4InBounds(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{
    return SIMDVectorDoubleDetail::AllComparisonMask<VectorDoubleComponentMask::s_DoubleXYZW>(VectorDoubleInBounds(value, bounds));
}

NWB_INLINE bool NWB_SIMD_CALL Vector4IsNaN(SIMDVectorSingle value)noexcept{ return (VectorMoveMask(VectorIsNaN(value)) & VectorComponentMask::s_XYZW) != 0; }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble4IsNaN(SIMDVectorDouble value)noexcept{ return (VectorDoubleMoveMask(VectorDoubleIsNaN(value)) & VectorDoubleComponentMask::s_DoubleXYZW) != 0; }

NWB_INLINE bool NWB_SIMD_CALL Vector4IsInfinite(SIMDVectorSingle value)noexcept{ return (VectorMoveMask(VectorIsInfinite(value)) & VectorComponentMask::s_XYZW) != 0; }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble4IsInfinite(SIMDVectorDouble value)noexcept{ return (VectorDoubleMoveMask(VectorDoubleIsInfinite(value)) & VectorDoubleComponentMask::s_DoubleXYZW) != 0; }

NWB_INLINE u32 NWB_SIMD_CALL Vector4EqualR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return VectorEqualR(v0, v1); }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble4EqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDoubleEqualR(v0, v1); }

NWB_INLINE u32 NWB_SIMD_CALL Vector4EqualIntR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return VectorEqualIntR(v0, v1); }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble4EqualIntR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDoubleEqualIntR(v0, v1); }

NWB_INLINE u32 NWB_SIMD_CALL Vector4GreaterR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return VectorGreaterR(v0, v1); }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble4GreaterR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDoubleGreaterR(v0, v1); }

NWB_INLINE u32 NWB_SIMD_CALL Vector4GreaterOrEqualR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return VectorGreaterOrEqualR(v0, v1); }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble4GreaterOrEqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDoubleGreaterOrEqualR(v0, v1); }

NWB_INLINE u32 NWB_SIMD_CALL Vector4InBoundsR(SIMDVectorSingle value, SIMDVectorSingle bounds)noexcept{ return VectorInBoundsR(value, bounds); }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble4InBoundsR(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{ return VectorDoubleInBoundsR(value, bounds); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4Dot(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(VectorGetX(v0) * VectorGetX(v1) + VectorGetY(v0) * VectorGetY(v1) + VectorGetZ(v0) * VectorGetZ(v1) + VectorGetW(v0) * VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    float32x4_t product = vmulq_f32(v0, v1);
    float32x2_t lo = vget_low_f32(product);
    float32x2_t hi = vget_high_f32(product);
    lo = vadd_f32(lo, hi);
    lo = vpadd_f32(lo, lo);
    return vcombine_f32(lo, lo);
#elif defined(NWB_HAS_SSE4)
    return _mm_dp_ps(v0, v1, 0xFF);
#elif defined(NWB_HAS_SSE2)
    const SIMDVectorSingle product = _mm_mul_ps(v0, v1);
    SIMDVectorSingle result = _mm_add_ss(product, _mm_shuffle_ps(product, product, _MM_SHUFFLE(1, 1, 1, 1)));
    result = _mm_add_ss(result, _mm_shuffle_ps(product, product, _MM_SHUFFLE(2, 2, 2, 2)));
    result = _mm_add_ss(result, _mm_shuffle_ps(product, product, _MM_SHUFFLE(3, 3, 3, 3)));
    return _mm_shuffle_ps(result, result, _MM_SHUFFLE(0, 0, 0, 0));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Dot(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_AVX2)
    return SIMDVectorDoubleDetail::Reduce4(VectorDoubleMultiply(v0, v1));
#else
    const SIMDVectorDouble product = VectorDoubleMultiply(v0, v1);
    SIMDVectorDouble result = VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
    result = VectorDoubleAdd(result, VectorDoubleSplatZ(product));
    result = VectorDoubleAdd(result, VectorDoubleSplatW(product));
    return result;
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4LengthSq(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_dp_ps(value, value, 0xFF);
#else
    return Vector4Dot(value, value);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4LengthSq(SIMDVectorDouble value)noexcept{
    return VectorDouble4Dot(value, value);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4ReciprocalLength(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVectorSingle lengthSq = _mm_dp_ps(value, value, 0xFF);
    const SIMDVectorSingle length = _mm_sqrt_ps(lengthSq);
    return _mm_div_ps(s_SIMDOne, length);
#else
    return VectorReciprocalSqrt(Vector4LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4ReciprocalLength(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_SSE2) && !defined(NWB_HAS_AVX)
    return SIMDVectorDoubleDetail::ReciprocalSqrtReplicated(VectorDouble4LengthSq(value));
#else
    return VectorDoubleReciprocalSqrt(VectorDouble4LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4ReciprocalLengthEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_rsqrt_ps(_mm_dp_ps(value, value, 0xFF));
#else
    return VectorReciprocalSqrtEst(Vector4LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4ReciprocalLengthEst(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_SSE2) && !defined(NWB_HAS_AVX)
    return SIMDVectorDoubleDetail::ReciprocalSqrtReplicated(VectorDouble4LengthSq(value));
#else
    return VectorDoubleReciprocalSqrtEst(VectorDouble4LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4Length(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0xFF));
#else
    return VectorSqrt(Vector4LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Length(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrt(VectorDouble4LengthSq(value));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4LengthEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0xFF));
#else
    return VectorSqrtEst(Vector4LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4LengthEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrtEst(VectorDouble4LengthSq(value));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4Normalize(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    SIMDVectorSingle lengthSq = _mm_dp_ps(value, value, 0xFF);
    const SIMDVectorSingle length = _mm_sqrt_ps(lengthSq);
    const SIMDVectorSingle zeroMask = _mm_cmpneq_ps(_mm_setzero_ps(), length);
    lengthSq = _mm_cmpneq_ps(lengthSq, s_SIMDInfinity);
    SIMDVectorSingle result = _mm_div_ps(value, length);
    result = _mm_and_ps(result, zeroMask);
    return _mm_or_ps(_mm_andnot_ps(lengthSq, s_SIMDQNaN), _mm_and_ps(result, lengthSq));
#else
    const SIMDVectorSingle lengthSq = Vector4LengthSq(value);
    const SIMDVectorSingle length = VectorSqrt(lengthSq);
    SIMDVectorSingle result = VectorDivide(value, length);
    result = VectorAndInt(result, VectorNotEqual(length, VectorZero()));
    return VectorSelect(s_SIMDQNaN, result, VectorNotEqualInt(lengthSq, s_SIMDInfinity));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Normalize(SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble lengthSq = VectorDouble4LengthSq(value);
    const SIMDVectorDouble length = VectorDoubleSqrt(lengthSq);
    SIMDVectorDouble result = VectorDoubleDivide(value, length);
    result = VectorDoubleAndInt(result, VectorDoubleNotEqual(length, VectorDoubleZero()));
    return VectorDoubleSelect(s_DoubleSIMDQNaN, result, VectorDoubleNotEqualInt(lengthSq, s_DoubleSIMDInfinity));
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4NormalizeOr(SIMDVectorSingle value, SIMDVectorSingle fallback, const f32 minLengthSquared)noexcept{
    const SIMDVectorSingle lengthSquared = Vector4LengthSq(value);
    return SIMDVectorDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, VectorTrueInt());
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4NormalizeOr(SIMDVectorDouble value, SIMDVectorDouble fallback, const f64 minLengthSquared)noexcept{
    const SIMDVectorDouble lengthSquared = VectorDouble4LengthSq(value);
    return SIMDVectorDoubleDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, VectorDoubleTrueInt());
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4NormalizeEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVectorSingle lengthSq = _mm_dp_ps(value, value, 0xFF);
    return _mm_mul_ps(value, _mm_rsqrt_ps(lengthSq));
#else
    return VectorMultiply(value, Vector4ReciprocalLengthEst(value));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4NormalizeEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleMultiply(value, VectorDouble4ReciprocalLengthEst(value));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4ClampLengthV(SIMDVectorSingle value, SIMDVectorSingle lengthMin, SIMDVectorSingle lengthMax)noexcept{
    NWB_ASSERT((VectorGetY(lengthMin) == VectorGetX(lengthMin)) && (VectorGetZ(lengthMin) == VectorGetX(lengthMin)) && (VectorGetW(lengthMin) == VectorGetX(lengthMin)));
    NWB_ASSERT((VectorGetY(lengthMax) == VectorGetX(lengthMax)) && (VectorGetZ(lengthMax) == VectorGetX(lengthMax)) && (VectorGetW(lengthMax) == VectorGetX(lengthMax)));
    NWB_ASSERT(Vector4GreaterOrEqual(lengthMin, s_SIMDZero));
    NWB_ASSERT(Vector4GreaterOrEqual(lengthMax, s_SIMDZero));
    NWB_ASSERT(Vector4GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVectorSingle lengthSq = Vector4LengthSq(value);
    return SIMDVectorDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4ClampLengthV(SIMDVectorDouble value, SIMDVectorDouble lengthMin, SIMDVectorDouble lengthMax)noexcept{
    NWB_ASSERT((VectorDoubleGetY(lengthMin) == VectorDoubleGetX(lengthMin)) && (VectorDoubleGetZ(lengthMin) == VectorDoubleGetX(lengthMin)) && (VectorDoubleGetW(lengthMin) == VectorDoubleGetX(lengthMin)));
    NWB_ASSERT((VectorDoubleGetY(lengthMax) == VectorDoubleGetX(lengthMax)) && (VectorDoubleGetZ(lengthMax) == VectorDoubleGetX(lengthMax)) && (VectorDoubleGetW(lengthMax) == VectorDoubleGetX(lengthMax)));
    NWB_ASSERT(VectorDouble4GreaterOrEqual(lengthMin, s_DoubleSIMDZero));
    NWB_ASSERT(VectorDouble4GreaterOrEqual(lengthMax, s_DoubleSIMDZero));
    NWB_ASSERT(VectorDouble4GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVectorDouble lengthSq = VectorDouble4LengthSq(value);
    return SIMDVectorDoubleDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4ClampLength(SIMDVectorSingle value, f32 lengthMin, f32 lengthMax)noexcept{
    return Vector4ClampLengthV(value, VectorReplicate(lengthMin), VectorReplicate(lengthMax));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4ClampLength(SIMDVectorDouble value, f64 lengthMin, f64 lengthMax)noexcept{
    return VectorDouble4ClampLengthV(value, VectorDoubleReplicate(lengthMin), VectorDoubleReplicate(lengthMax));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4Reflect(SIMDVectorSingle incident, SIMDVectorSingle normal)noexcept{
    SIMDVectorSingle result = Vector4Dot(incident, normal);
    result = VectorAdd(result, result);
    return VectorNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Reflect(SIMDVectorDouble incident, SIMDVectorDouble normal)noexcept{
    SIMDVectorDouble result = VectorDouble4Dot(incident, normal);
    result = VectorDoubleAdd(result, result);
    return VectorDoubleNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4RefractV(SIMDVectorSingle incident, SIMDVectorSingle normal, SIMDVectorSingle refractionIndex)noexcept{
    return SIMDVectorDetail::RefractV(incident, normal, refractionIndex, Vector4Dot(incident, normal));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4RefractV(SIMDVectorDouble incident, SIMDVectorDouble normal, SIMDVectorDouble refractionIndex)noexcept{
    return SIMDVectorDoubleDetail::RefractV(incident, normal, refractionIndex, VectorDouble4Dot(incident, normal));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4Refract(SIMDVectorSingle incident, SIMDVectorSingle normal, f32 refractionIndex)noexcept{ return Vector4RefractV(incident, normal, VectorReplicate(refractionIndex)); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Refract(SIMDVectorDouble incident, SIMDVectorDouble normal, f64 refractionIndex)noexcept{ return VectorDouble4RefractV(incident, normal, VectorDoubleReplicate(refractionIndex)); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4Orthogonal(SIMDVectorSingle value)noexcept{ return VectorMultiply(VectorSwizzle<2, 3, 0, 1>(value), VectorSet(1.0f, 1.0f, -1.0f, -1.0f)); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Orthogonal(SIMDVectorDouble value)noexcept{ return VectorDoubleMultiply(VectorDoubleSwizzle<2, 3, 0, 1>(value), VectorDoubleSet(1.0, 1.0, -1.0, -1.0)); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4AngleBetweenNormals(SIMDVectorSingle n0, SIMDVectorSingle n1)noexcept{ return VectorACos(VectorClamp(Vector4Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4AngleBetweenNormals(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{ return SIMDVectorDoubleDetail::ACosReplicated(VectorDoubleClamp(VectorDouble4Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne)); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4AngleBetweenNormalsEst(SIMDVectorSingle n0, SIMDVectorSingle n1)noexcept{ return VectorACosEst(VectorClamp(Vector4Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4AngleBetweenNormalsEst(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{ return SIMDVectorDoubleDetail::ACosReplicated(VectorDoubleClamp(VectorDouble4Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne)); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4AngleBetweenVectors(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    const SIMDVectorSingle reciprocalLength0 = Vector4ReciprocalLength(v0);
    const SIMDVectorSingle reciprocalLength1 = Vector4ReciprocalLength(v1);
    SIMDVectorSingle cosAngle = VectorMultiply(Vector4Dot(v0, v1), VectorMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorClamp(cosAngle, s_SIMDNegativeOne, s_SIMDOne);
    return VectorACos(cosAngle);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4AngleBetweenVectors(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDouble reciprocalLength0 = VectorDouble4ReciprocalLength(v0);
    const SIMDVectorDouble reciprocalLength1 = VectorDouble4ReciprocalLength(v1);
    SIMDVectorDouble cosAngle = VectorDoubleMultiply(VectorDouble4Dot(v0, v1), VectorDoubleMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorDoubleClamp(cosAngle, s_DoubleSIMDNegativeOne, s_DoubleSIMDOne);
    return SIMDVectorDoubleDetail::ACosReplicated(cosAngle);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4Cross(SIMDVectorSingle v1, SIMDVectorSingle v2, SIMDVectorSingle v3)noexcept{
    SIMDVectorSingle result = VectorMultiply(VectorSwizzle<2, 3, 1, 2>(v2), VectorSwizzle<3, 2, 3, 1>(v3));
    SIMDVectorSingle temp = VectorSwizzle<3, 2, 3, 1>(v2);
    SIMDVectorSingle temp3 = VectorSwizzle<2, 3, 1, 2>(v3);
    result = VectorNegativeMultiplySubtract(temp, temp3, result);
    result = VectorMultiply(result, VectorSwizzle<1, 0, 0, 0>(v1));

    temp = VectorSwizzle<1, 3, 0, 2>(v2);
    temp3 = VectorSwizzle<3, 0, 3, 0>(v3);
    temp3 = VectorMultiply(temp3, temp);
    temp = VectorSwizzle<3, 0, 3, 0>(temp);
    SIMDVectorSingle temp1 = VectorSwizzle<1, 3, 0, 2>(v3);
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

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Cross(SIMDVectorDouble v1, SIMDVectorDouble v2, SIMDVectorDouble v3)noexcept{
    SIMDVectorDouble result = VectorDoubleMultiply(VectorDoubleSwizzle<2, 3, 1, 2>(v2), VectorDoubleSwizzle<3, 2, 3, 1>(v3));
    SIMDVectorDouble temp = VectorDoubleSwizzle<3, 2, 3, 1>(v2);
    SIMDVectorDouble temp3 = VectorDoubleSwizzle<2, 3, 1, 2>(v3);
    result = VectorDoubleNegativeMultiplySubtract(temp, temp3, result);
    result = VectorDoubleMultiply(result, VectorDoubleSwizzle<1, 0, 0, 0>(v1));

    temp = VectorDoubleSwizzle<1, 3, 0, 2>(v2);
    temp3 = VectorDoubleSwizzle<3, 0, 3, 0>(v3);
    temp3 = VectorDoubleMultiply(temp3, temp);
    temp = VectorDoubleSwizzle<3, 0, 3, 0>(temp);
    SIMDVectorDouble temp1 = VectorDoubleSwizzle<1, 3, 0, 2>(v3);
    temp3 = VectorDoubleNegativeMultiplySubtract(temp, temp1, temp3);
    temp1 = VectorDoubleSwizzle<2, 2, 1, 1>(v1);
    result = VectorDoubleNegativeMultiplySubtract(temp1, temp3, result);

    temp = VectorDoubleSwizzle<1, 2, 0, 1>(v2);
    temp3 = VectorDoubleSwizzle<2, 0, 1, 0>(v3);
    temp3 = VectorDoubleMultiply(temp3, temp);
    temp = VectorDoubleSwizzle<1, 2, 0, 2>(temp);
    temp1 = VectorDoubleSwizzle<1, 2, 0, 1>(v3);
    temp3 = VectorDoubleNegativeMultiplySubtract(temp1, temp, temp3);
    temp1 = VectorDoubleSwizzle<3, 3, 3, 2>(v1);
    return VectorDoubleNegate(VectorDoubleMultiplyAdd(temp3, temp1, result));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

