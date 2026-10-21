// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_arithmetic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_SSE2)
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL ReciprocalSqrtReplicated(SIMDVectorDouble value)noexcept{
    const __m128d input = GetLow(value);
    const __m128d root = _mm_sqrt_sd(input, input);
    const __m128d reciprocal = _mm_div_sd(_mm_set_sd(1.0), root);
    const __m128d result = _mm_unpacklo_pd(reciprocal, reciprocal);
    return FromPairs(result, result);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorReciprocal(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_NEON)
#if defined(NWB_HAS_NEON64)
    return vdivq_f32(vdupq_n_f32(1.0f), value);
#else
    float32x4_t reciprocal = vrecpeq_f32(value);
    float32x4_t scale = vrecpsq_f32(reciprocal, value);
    reciprocal = vmulq_f32(scale, reciprocal);
    scale = vrecpsq_f32(reciprocal, value);
    return vmulq_f32(scale, reciprocal);
#endif
#elif defined(NWB_HAS_SSE2)
    return _mm_div_ps(s_SIMDOne, value);
#else
    return VectorDivide(VectorSplatOne(), value);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReciprocal(SIMDVectorDouble value)noexcept{
    return VectorDoubleDivide(VectorDoubleSplatOne(), value);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorReciprocalEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReciprocal(value);
#elif defined(NWB_HAS_NEON)
    return vrecpeq_f32(value);
#else
    return _mm_rcp_ps(value);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReciprocalEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocal(value);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSqrt(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(Sqrt(VectorGetX(value)), Sqrt(VectorGetY(value)), Sqrt(VectorGetZ(value)), Sqrt(VectorGetW(value)));
#elif defined(NWB_HAS_NEON64)
    return vsqrtq_f32(value);
#elif defined(NWB_HAS_NEON)
    float32x4_t s0 = vrsqrteq_f32(value);
    float32x4_t p0 = vmulq_f32(value, s0);
    float32x4_t r0 = vrsqrtsq_f32(p0, s0);
    float32x4_t s1 = vmulq_f32(s0, r0);
    float32x4_t p1 = vmulq_f32(value, s1);
    float32x4_t r1 = vrsqrtsq_f32(p1, s1);
    float32x4_t s2 = vmulq_f32(s1, r1);
    float32x4_t p2 = vmulq_f32(value, s2);
    float32x4_t r2 = vrsqrtsq_f32(p2, s2);
    float32x4_t result = vmulq_f32(value, vmulq_f32(s2, r2));
    const SIMDVectorSingle select = VectorEqualInt(VectorEqualInt(value, s_SIMDInfinity), VectorEqual(value, VectorZero()));
    return VectorSelect(value, result, select);
#else
    return _mm_sqrt_ps(value);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSqrt(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(vsqrtq_f64(SIMDVectorDoubleDetail::GetLow(value)), vsqrtq_f64(SIMDVectorDoubleDetail::GetHigh(value)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_sqrt_pd(BitCast<__m256d>(value)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_sqrt_pd(SIMDVectorDoubleDetail::GetLow(value)), _mm_sqrt_pd(SIMDVectorDoubleDetail::GetHigh(value)));
#else
    return VectorDoubleSet(Sqrt(value[0]), Sqrt(value[1]), Sqrt(value[2]), Sqrt(value[3]));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSqrtEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_NEON)
    float32x4_t s0 = vrsqrteq_f32(value);
    float32x4_t p0 = vmulq_f32(value, s0);
    float32x4_t r0 = vrsqrtsq_f32(p0, s0);
    SIMDVectorSingle result = vmulq_f32(value, vmulq_f32(s0, r0));
    const SIMDVectorSingle select = VectorEqualInt(VectorEqualInt(value, s_SIMDInfinity), VectorEqual(value, VectorZero()));
    return VectorSelect(value, result, select);
#else
    return VectorSqrt(value);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSqrtEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrt(value);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorReciprocalSqrt(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_NEON64)
    return vdivq_f32(vdupq_n_f32(1.0f), vsqrtq_f32(value));
#elif defined(NWB_HAS_NEON)
    float32x4_t s0 = vrsqrteq_f32(value);
    float32x4_t p0 = vmulq_f32(value, s0);
    float32x4_t r0 = vrsqrtsq_f32(p0, s0);
    float32x4_t s1 = vmulq_f32(s0, r0);
    float32x4_t p1 = vmulq_f32(value, s1);
    float32x4_t r1 = vrsqrtsq_f32(p1, s1);
    return vmulq_f32(s1, r1);
#elif defined(NWB_HAS_SSE2)
    return _mm_div_ps(s_SIMDOne, _mm_sqrt_ps(value));
#else
    return VectorReciprocal(VectorSqrt(value));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReciprocalSqrt(SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocal(VectorDoubleSqrt(value));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorReciprocalSqrtEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReciprocalSqrt(value);
#elif defined(NWB_HAS_NEON)
    return vrsqrteq_f32(value);
#else
    return _mm_rsqrt_ps(value);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReciprocalSqrtEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocalSqrt(value);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

