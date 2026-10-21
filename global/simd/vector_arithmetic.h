// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_compare.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorNegate(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(-VectorGetX(value), -VectorGetY(value), -VectorGetZ(value), -VectorGetW(value));
#elif defined(NWB_HAS_NEON)
    return vnegq_f32(value);
#else
    return _mm_xor_ps(value, s_SIMDNegativeZero);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNegate(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_NEON64)
    const uint64x2_t sign = vdupq_n_u64(s_F64SignBitMask);
    return SIMDVectorDoubleDetail::FromUIntPairs(veorq_u64(SIMDVectorDoubleDetail::GetLowBits(value), sign), veorq_u64(SIMDVectorDoubleDetail::GetHighBits(value), sign));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_xor_pd(BitCast<__m256d>(value), _mm256_set1_pd(-0.0)));
#elif defined(NWB_HAS_SSE2)
    const __m128d sign = _mm_set1_pd(-0.0);
    return SIMDVectorDoubleDetail::FromPairs(_mm_xor_pd(SIMDVectorDoubleDetail::GetLow(value), sign), _mm_xor_pd(SIMDVectorDoubleDetail::GetHigh(value), sign));
#else
    return -value;
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorAdd(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) + VectorGetX(v1), VectorGetY(v0) + VectorGetY(v1), VectorGetZ(v0) + VectorGetZ(v1), VectorGetW(v0) + VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vaddq_f32(v0, v1);
#else
    return _mm_add_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAdd(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(vaddq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), vaddq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_add_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_add_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), _mm_add_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#else
    return v0 + v1;
#endif
}

// Adds raw unsigned 32-bit lane representations with modulo arithmetic.
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorAddInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) + VectorGetIntX(v1), VectorGetIntY(v0) + VectorGetIntY(v1), VectorGetIntZ(v0) + VectorGetIntZ(v1), VectorGetIntW(v0) + VectorGetIntW(v1));
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vaddq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_add_epi32(_mm_castps_si128(v0), _mm_castps_si128(v1)));
#endif
}

// Adds raw unsigned 64-bit lane representations with modulo arithmetic.
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAddInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(vaddq_u64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)), vaddq_u64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1)));
#elif defined(NWB_HAS_AVX2)
    return BitCast<SIMDVectorDouble>(_mm256_add_epi64(BitCast<__m256i>(v0), BitCast<__m256i>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromUIntPairs(_mm_add_epi64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)), _mm_add_epi64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1)));
#else
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits + v1Bits);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSubtract(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) - VectorGetX(v1), VectorGetY(v0) - VectorGetY(v1), VectorGetZ(v0) - VectorGetZ(v1), VectorGetW(v0) - VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vsubq_f32(v0, v1);
#else
    return _mm_sub_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSubtract(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(vsubq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), vsubq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_sub_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_sub_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), _mm_sub_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#else
    return v0 - v1;
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMultiply(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) * VectorGetX(v1), VectorGetY(v0) * VectorGetY(v1), VectorGetZ(v0) * VectorGetZ(v1), VectorGetW(v0) * VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vmulq_f32(v0, v1);
#else
    return _mm_mul_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMultiply(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(vmulq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), vmulq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_mul_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_mul_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), _mm_mul_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#else
    return v0 * v1;
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorDivide(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) / VectorGetX(v1), VectorGetY(v0) / VectorGetY(v1), VectorGetZ(v0) / VectorGetZ(v1), VectorGetW(v0) / VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
#if defined(NWB_HAS_NEON64)
    return vdivq_f32(v0, v1);
#else
    float32x4_t reciprocal = vrecpeq_f32(v1);
    float32x4_t scale = vrecpsq_f32(reciprocal, v1);
    reciprocal = vmulq_f32(scale, reciprocal);
    scale = vrecpsq_f32(reciprocal, v1);
    reciprocal = vmulq_f32(scale, reciprocal);
    return vmulq_f32(v0, reciprocal);
#endif
#else
    return _mm_div_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleDivide(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(vdivq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), vdivq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_div_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_div_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), _mm_div_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#else
    return v0 / v1;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMultiplyAdd(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept{
#if defined(NWB_HAS_FMA3)
    return _mm_fmadd_ps(v0, v1, v2);
#elif defined(NWB_HAS_NEON)
    return vmlaq_f32(v2, v0, v1);
#else
    return VectorAdd(VectorMultiply(v0, v1), v2);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMultiplyAdd(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(
        vfmaq_f64(SIMDVectorDoubleDetail::GetLow(v2), SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vfmaq_f64(SIMDVectorDoubleDetail::GetHigh(v2), SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_FMA3) && defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_fmadd_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1), BitCast<__m256d>(v2)));
#elif defined(NWB_HAS_FMA3) && defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_fmadd_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1), SIMDVectorDoubleDetail::GetLow(v2)),
        _mm_fmadd_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1), SIMDVectorDoubleDetail::GetHigh(v2))
    );
#elif defined(NWB_HAS_FMA3) || defined(NWB_HAS_NEON)
    return __builtin_elementwise_fma(v0, v1, v2);
#else
    return VectorDoubleAdd(VectorDoubleMultiply(v0, v1), v2);
#endif
}

// Preserve the compiler's contraction policy for a source multiply-add expression instead of requiring an explicit FMA.
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMultiplyAddExpression(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) * VectorGetX(v1) + VectorGetX(v2), VectorGetY(v0) * VectorGetY(v1) + VectorGetY(v2), VectorGetZ(v0) * VectorGetZ(v1) + VectorGetZ(v2), VectorGetW(v0) * VectorGetW(v1) + VectorGetW(v2));
#else
    return v0 * v1 + v2;
#endif
}

// Preserve the compiler's contraction policy for a source multiply-add expression instead of requiring an explicit FMA.
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMultiplyAddExpression(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(
        SIMDVectorDoubleDetail::GetLow(v0) * SIMDVectorDoubleDetail::GetLow(v1)
            + SIMDVectorDoubleDetail::GetLow(v2),
        SIMDVectorDoubleDetail::GetHigh(v0) * SIMDVectorDoubleDetail::GetHigh(v1)
            + SIMDVectorDoubleDetail::GetHigh(v2)
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(BitCast<__m256d>(v0) * BitCast<__m256d>(v1) + BitCast<__m256d>(v2));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        SIMDVectorDoubleDetail::GetLow(v0) * SIMDVectorDoubleDetail::GetLow(v1)
            + SIMDVectorDoubleDetail::GetLow(v2),
        SIMDVectorDoubleDetail::GetHigh(v0) * SIMDVectorDoubleDetail::GetHigh(v1)
            + SIMDVectorDoubleDetail::GetHigh(v2)
    );
#else
    return v0 * v1 + v2;
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMultiplySubtract(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept{
#if defined(NWB_HAS_FMA3)
    return _mm_fmsub_ps(v0, v1, v2);
#elif defined(NWB_HAS_NEON64)
    return vfmaq_f32(vnegq_f32(v2), v0, v1);
#else
    return VectorSubtract(VectorMultiply(v0, v1), v2);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMultiplySubtract(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(
        vfmaq_f64(vnegq_f64(SIMDVectorDoubleDetail::GetLow(v2)), SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vfmaq_f64(vnegq_f64(SIMDVectorDoubleDetail::GetHigh(v2)), SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_FMA3) && defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_fmsub_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1), BitCast<__m256d>(v2)));
#elif defined(NWB_HAS_FMA3) && defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_fmsub_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1), SIMDVectorDoubleDetail::GetLow(v2)),
        _mm_fmsub_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1), SIMDVectorDoubleDetail::GetHigh(v2))
    );
#elif defined(NWB_HAS_FMA3) || defined(NWB_HAS_NEON)
    return __builtin_elementwise_fma(v0, v1, -v2);
#else
    return VectorDoubleSubtract(VectorDoubleMultiply(v0, v1), v2);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorNegativeMultiplySubtract(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept{
#if defined(NWB_HAS_FMA3)
    return _mm_fnmadd_ps(v0, v1, v2);
#elif defined(NWB_HAS_NEON)
    return vmlsq_f32(v2, v0, v1);
#else
    return VectorSubtract(v2, VectorMultiply(v0, v1));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNegativeMultiplySubtract(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(
        vfmsq_f64(SIMDVectorDoubleDetail::GetLow(v2), SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vfmsq_f64(SIMDVectorDoubleDetail::GetHigh(v2), SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_FMA3) && defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_fnmadd_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1), BitCast<__m256d>(v2)));
#elif defined(NWB_HAS_FMA3) && defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_fnmadd_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1), SIMDVectorDoubleDetail::GetLow(v2)),
        _mm_fnmadd_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1), SIMDVectorDoubleDetail::GetHigh(v2))
    );
#elif defined(NWB_HAS_FMA3) || defined(NWB_HAS_NEON)
    return __builtin_elementwise_fma(-v0, v1, v2);
#else
    return VectorDoubleSubtract(v2, VectorDoubleMultiply(v0, v1));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorNegativeMultiplySubtractExpression(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v2) - VectorGetX(v0) * VectorGetX(v1), VectorGetY(v2) - VectorGetY(v0) * VectorGetY(v1), VectorGetZ(v2) - VectorGetZ(v0) * VectorGetZ(v1), VectorGetW(v2) - VectorGetW(v0) * VectorGetW(v1));
#else
    return v2 - v0 * v1;
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNegativeMultiplySubtractExpression(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(
        SIMDVectorDoubleDetail::GetLow(v2)
            - SIMDVectorDoubleDetail::GetLow(v0) * SIMDVectorDoubleDetail::GetLow(v1),
        SIMDVectorDoubleDetail::GetHigh(v2)
            - SIMDVectorDoubleDetail::GetHigh(v0) * SIMDVectorDoubleDetail::GetHigh(v1)
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(BitCast<__m256d>(v2) - BitCast<__m256d>(v0) * BitCast<__m256d>(v1));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        SIMDVectorDoubleDetail::GetLow(v2)
            - SIMDVectorDoubleDetail::GetLow(v0) * SIMDVectorDoubleDetail::GetLow(v1),
        SIMDVectorDoubleDetail::GetHigh(v2)
            - SIMDVectorDoubleDetail::GetHigh(v0) * SIMDVectorDoubleDetail::GetHigh(v1)
    );
#else
    return v2 - v0 * v1;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorScale(SIMDVectorSingle value, f32 scale)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(value.f[0] * scale, value.f[1] * scale, value.f[2] * scale, value.f[3] * scale);
#elif defined(NWB_HAS_NEON)
    return vmulq_n_f32(value, scale);
#else
    return _mm_mul_ps(_mm_set1_ps(scale), value);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleScale(SIMDVectorDouble value, f64 scale)noexcept{
    return VectorDoubleMultiply(value, VectorDoubleReplicate(scale));
}

// Min/max select the second operand for equal or unordered lanes.
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMin(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(
        VectorGetX(v0) < VectorGetX(v1) ? VectorGetX(v0) : VectorGetX(v1), VectorGetY(v0) < VectorGetY(v1) ? VectorGetY(v0) : VectorGetY(v1), VectorGetZ(v0) < VectorGetZ(v1) ? VectorGetZ(v0) : VectorGetZ(v1),
        VectorGetW(v0) < VectorGetW(v1) ? VectorGetW(v0) : VectorGetW(v1)
    );
#elif defined(NWB_HAS_NEON)
    return vbslq_f32(vcltq_f32(v0, v1), v0, v1);
#else
    return _mm_min_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMin(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    const float64x2_t low0 = SIMDVectorDoubleDetail::GetLow(v0);
    const float64x2_t low1 = SIMDVectorDoubleDetail::GetLow(v1);
    const float64x2_t high0 = SIMDVectorDoubleDetail::GetHigh(v0);
    const float64x2_t high1 = SIMDVectorDoubleDetail::GetHigh(v1);
    return SIMDVectorDoubleDetail::FromPairs(vbslq_f64(vcltq_f64(low0, low1), low0, low1), vbslq_f64(vcltq_f64(high0, high1), high0, high1));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_min_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_min_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), _mm_min_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#else
    return (v0 < v1) ? v0 : v1;
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMax(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(
        VectorGetX(v0) > VectorGetX(v1) ? VectorGetX(v0) : VectorGetX(v1), VectorGetY(v0) > VectorGetY(v1) ? VectorGetY(v0) : VectorGetY(v1), VectorGetZ(v0) > VectorGetZ(v1) ? VectorGetZ(v0) : VectorGetZ(v1),
        VectorGetW(v0) > VectorGetW(v1) ? VectorGetW(v0) : VectorGetW(v1)
    );
#elif defined(NWB_HAS_NEON)
    return vbslq_f32(vcgtq_f32(v0, v1), v0, v1);
#else
    return _mm_max_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMax(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    const float64x2_t low0 = SIMDVectorDoubleDetail::GetLow(v0);
    const float64x2_t low1 = SIMDVectorDoubleDetail::GetLow(v1);
    const float64x2_t high0 = SIMDVectorDoubleDetail::GetHigh(v0);
    const float64x2_t high1 = SIMDVectorDoubleDetail::GetHigh(v1);
    return SIMDVectorDoubleDetail::FromPairs(vbslq_f64(vcgtq_f64(low0, low1), low0, low1), vbslq_f64(vcgtq_f64(high0, high1), high0, high1));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_max_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_max_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), _mm_max_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#else
    return (v0 > v1) ? v0 : v1;
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorClamp(SIMDVectorSingle value, SIMDVectorSingle minValue, SIMDVectorSingle maxValue)noexcept{
    NWB_ASSERT((VectorMoveMask(VectorLessOrEqual(minValue, maxValue)) & VectorComponentMask::s_XYZW) == VectorComponentMask::s_XYZW);
    return VectorMin(maxValue, VectorMax(minValue, value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleClamp(SIMDVectorDouble value, SIMDVectorDouble minValue, SIMDVectorDouble maxValue)noexcept{
    NWB_ASSERT((VectorDoubleMoveMask(VectorDoubleLessOrEqual(minValue, maxValue)) & VectorDoubleComponentMask::s_DoubleXYZW) == VectorDoubleComponentMask::s_DoubleXYZW);
    return VectorDoubleMin(maxValue, VectorDoubleMax(minValue, value));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSaturate(SIMDVectorSingle value)noexcept{
    return VectorMin(VectorSplatOne(), VectorMax(VectorZero(), value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSaturate(SIMDVectorDouble value)noexcept{
    return VectorDoubleMin(VectorDoubleSplatOne(), VectorDoubleMax(VectorDoubleZero(), value));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorAbs(SIMDVectorSingle value)noexcept{
    return VectorAndInt(value, s_SIMDAbsMask);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAbs(SIMDVectorDouble value)noexcept{
    return VectorDoubleAndInt(value, s_DoubleSIMDAbsMask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSum(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(VectorGetX(value) + VectorGetY(value) + VectorGetZ(value) + VectorGetW(value));
#elif defined(NWB_HAS_NEON)
#if defined(NWB_HAS_NEON64)
    SIMDVectorSingle temp = vpaddq_f32(value, value);
    return vpaddq_f32(temp, temp);
#else
    float32x2_t lo = vget_low_f32(value);
    float32x2_t hi = vget_high_f32(value);
    lo = vadd_f32(lo, hi);
    lo = vpadd_f32(lo, lo);
    return vcombine_f32(lo, lo);
#endif
#elif defined(NWB_HAS_SSE4)
    SIMDVectorSingle temp = _mm_hadd_ps(value, value);
    return _mm_hadd_ps(temp, temp);
#else
    const __m128 paired = _mm_add_ps(value, _mm_shuffle_ps(value, value, _MM_SHUFFLE(2, 3, 0, 1)));
    return _mm_add_ps(_mm_shuffle_ps(paired, paired, _MM_SHUFFLE(0, 0, 0, 0)), _mm_shuffle_ps(paired, paired, _MM_SHUFFLE(2, 2, 2, 2)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSum(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_AVX2)
    return SIMDVectorDoubleDetail::Reduce4(value);
#else
    const SIMDVectorDouble xy = VectorDoubleAdd(VectorDoubleSplatX(value), VectorDoubleSplatY(value));
    const SIMDVectorDouble xyz = VectorDoubleAdd(xy, VectorDoubleSplatZ(value));
    return VectorDoubleAdd(xyz, VectorDoubleSplatW(value));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMergeX(SIMDVectorSingle x, SIMDVectorSingle y, SIMDVectorSingle z, SIMDVectorSingle w)noexcept{
    const SIMDVectorSingle xy = VectorPermute<0, 4, 1, 5>(x, y);
    const SIMDVectorSingle zw = VectorPermute<0, 4, 1, 5>(z, w);
    return VectorPermute<0, 1, 4, 5>(xy, zw);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMergeX(SIMDVectorDouble x, SIMDVectorDouble y, SIMDVectorDouble z, SIMDVectorDouble w)noexcept{
    const SIMDVectorDouble xy = VectorDoublePermute<0, 4, 1, 5>(x, y);
    const SIMDVectorDouble zw = VectorDoublePermute<0, 4, 1, 5>(z, w);
    return VectorDoublePermute<0, 1, 4, 5>(xy, zw);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4MinComponent(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle result = VectorMin(value, VectorSwizzle<2, 3, 0, 1>(value));
    result = VectorMin(result, VectorSwizzle<1, 0, 3, 2>(result));
    return result;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4MinComponent(SIMDVectorDouble value)noexcept{
    SIMDVectorDouble result = VectorDoubleMin(value, VectorDoubleSwizzle<2, 3, 0, 1>(value));
    result = VectorDoubleMin(result, VectorDoubleSwizzle<1, 0, 3, 2>(result));
    return result;
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3MinComponent(SIMDVectorSingle value)noexcept{
    return Vector4MinComponent(VectorSwizzle<0, 1, 2, 0>(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3MinComponent(SIMDVectorDouble value)noexcept{
    return VectorDouble4MinComponent(VectorDoubleSwizzle<0, 1, 2, 0>(value));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4MaxComponent(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle result = VectorMax(value, VectorSwizzle<2, 3, 0, 1>(value));
    result = VectorMax(result, VectorSwizzle<1, 0, 3, 2>(result));
    return result;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4MaxComponent(SIMDVectorDouble value)noexcept{
    SIMDVectorDouble result = VectorDoubleMax(value, VectorDoubleSwizzle<2, 3, 0, 1>(value));
    result = VectorDoubleMax(result, VectorDoubleSwizzle<1, 0, 3, 2>(result));
    return result;
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3MaxComponent(SIMDVectorSingle value)noexcept{
    return Vector4MaxComponent(VectorSwizzle<0, 1, 2, 0>(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3MaxComponent(SIMDVectorDouble value)noexcept{
    return VectorDouble4MaxComponent(VectorDoubleSwizzle<0, 1, 2, 0>(value));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

