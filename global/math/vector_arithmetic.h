// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_compare.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Integer-bit rounding keeps binary64 range, signed zero, and ties independent of the floating-point rounding mode.
[[nodiscard]] NWB_INLINE f64 RoundToNearestScalar(const f64 value)noexcept{
    const u64 bits = BitCast<u64>(value);
    const u64 magnitude = bits & 0x7FFFFFFFFFFFFFFFu;
    const u64 sign = bits & 0x8000000000000000u;
    const u64 exponent = magnitude >> 52u;
    if(exponent == 0x7FFu && (magnitude & 0x000FFFFFFFFFFFFFu) != 0u)
        return value + value;
    if(exponent >= 1075u)
        return value;
    if(exponent < 1023u)
        return BitCast<f64>(sign | (magnitude > 0x3FE0000000000000u ? 0x3FF0000000000000u : 0u));

    const u64 unit = u64{ 1u } << (1075u - exponent);
    const u64 fraction = magnitude & (unit - 1u);
    u64 integer = magnitude & ~(unit - 1u);
    const u64 halfway = unit >> 1u;
    if(fraction > halfway || (fraction == halfway && (integer & unit) != 0u))
        integer += unit;
    return BitCast<f64>(sign | integer);
}

[[nodiscard]] NWB_INLINE f64 TruncateScalar(const f64 value)noexcept{
    const u64 bits = BitCast<u64>(value);
    const u64 magnitude = bits & 0x7FFFFFFFFFFFFFFFu;
    const u64 exponent = magnitude >> 52u;
    if(exponent == 0x7FFu && (magnitude & 0x000FFFFFFFFFFFFFu) != 0u)
        return value + value;
    if(exponent >= 1075u)
        return value;
    if(exponent < 1023u)
        return BitCast<f64>(bits & 0x8000000000000000u);
    return BitCast<f64>(bits & ~((u64{ 1u } << (1075u - exponent)) - 1u));
}

[[nodiscard]] NWB_INLINE f64 FloorRemainder(const f64 numerator, const f64 denominator)noexcept{
    const f64 remainder = FMod(numerator, denominator);
    if(remainder == 0.0)
        return CopySign(0.0, denominator);
    if(SignBit(remainder) != SignBit(denominator))
        return remainder + denominator;
    return remainder;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
    const uint64x2_t sign = vdupq_n_u64(0x8000000000000000u);
    return SIMDVectorDoubleDetail::FromUIntPairs(
        veorq_u64(SIMDVectorDoubleDetail::GetLowBits(value), sign),
        veorq_u64(SIMDVectorDoubleDetail::GetHighBits(value), sign)
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_xor_pd(BitCast<__m256d>(value), _mm256_set1_pd(-0.0)));
#elif defined(NWB_HAS_SSE2)
    const __m128d sign = _mm_set1_pd(-0.0);
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_xor_pd(SIMDVectorDoubleDetail::GetLow(value), sign),
        _mm_xor_pd(SIMDVectorDoubleDetail::GetHigh(value), sign)
    );
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
    return SIMDVectorDoubleDetail::FromPairs(
        vaddq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vaddq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_add_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_add_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        _mm_add_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#else
    return v0 + v1;
#endif
}

// Adds raw unsigned 32-bit lane representations with modulo arithmetic.
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorAddInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(
        VectorGetIntX(v0) + VectorGetIntX(v1),
        VectorGetIntY(v0) + VectorGetIntY(v1),
        VectorGetIntZ(v0) + VectorGetIntZ(v1),
        VectorGetIntW(v0) + VectorGetIntW(v1)
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vaddq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_add_epi32(_mm_castps_si128(v0), _mm_castps_si128(v1)));
#endif
}

// Adds raw unsigned 64-bit lane representations with modulo arithmetic.
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAddInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(
        vaddq_u64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)),
        vaddq_u64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1))
    );
#elif defined(NWB_HAS_AVX2)
    return BitCast<SIMDVectorDouble>(_mm256_add_epi64(BitCast<__m256i>(v0), BitCast<__m256i>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromUIntPairs(
        _mm_add_epi64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)),
        _mm_add_epi64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1))
    );
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
    return SIMDVectorDoubleDetail::FromPairs(
        vsubq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vsubq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_sub_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_sub_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        _mm_sub_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
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
    return SIMDVectorDoubleDetail::FromPairs(
        vmulq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vmulq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_mul_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_mul_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        _mm_mul_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
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
    return SIMDVectorDoubleDetail::FromPairs(
        vdivq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vdivq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_div_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_div_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        _mm_div_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
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
        _mm_fmadd_pd(
            SIMDVectorDoubleDetail::GetLow(v0),
            SIMDVectorDoubleDetail::GetLow(v1),
            SIMDVectorDoubleDetail::GetLow(v2)
        ),
        _mm_fmadd_pd(
            SIMDVectorDoubleDetail::GetHigh(v0),
            SIMDVectorDoubleDetail::GetHigh(v1),
            SIMDVectorDoubleDetail::GetHigh(v2)
        )
    );
#elif defined(NWB_HAS_FMA3) || defined(NWB_HAS_NEON)
    return __builtin_elementwise_fma(v0, v1, v2);
#else
    return VectorDoubleAdd(VectorDoubleMultiply(v0, v1), v2);
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
        _mm_fnmadd_pd(
            SIMDVectorDoubleDetail::GetLow(v0),
            SIMDVectorDoubleDetail::GetLow(v1),
            SIMDVectorDoubleDetail::GetLow(v2)
        ),
        _mm_fnmadd_pd(
            SIMDVectorDoubleDetail::GetHigh(v0),
            SIMDVectorDoubleDetail::GetHigh(v1),
            SIMDVectorDoubleDetail::GetHigh(v2)
        )
    );
#elif defined(NWB_HAS_FMA3) || defined(NWB_HAS_NEON)
    return __builtin_elementwise_fma(-v0, v1, v2);
#else
    return VectorDoubleSubtract(v2, VectorDoubleMultiply(v0, v1));
#endif
}

// Preserve the compiler's contraction policy for a source multiply-add expression instead of requiring an explicit FMA.
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMultiplyAddExpression(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(
        VectorGetX(v0) * VectorGetX(v1) + VectorGetX(v2),
        VectorGetY(v0) * VectorGetY(v1) + VectorGetY(v2),
        VectorGetZ(v0) * VectorGetZ(v1) + VectorGetZ(v2),
        VectorGetW(v0) * VectorGetW(v1) + VectorGetW(v2)
    );
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

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorNegativeMultiplySubtractExpression(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(
        VectorGetX(v2) - VectorGetX(v0) * VectorGetX(v1),
        VectorGetY(v2) - VectorGetY(v0) * VectorGetY(v1),
        VectorGetZ(v2) - VectorGetZ(v0) * VectorGetZ(v1),
        VectorGetW(v2) - VectorGetW(v0) * VectorGetW(v1)
    );
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMin(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) < VectorGetX(v1) ? VectorGetX(v0) : VectorGetX(v1), VectorGetY(v0) < VectorGetY(v1) ? VectorGetY(v0) : VectorGetY(v1), VectorGetZ(v0) < VectorGetZ(v1) ? VectorGetZ(v0) : VectorGetZ(v1), VectorGetW(v0) < VectorGetW(v1) ? VectorGetW(v0) : VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vminq_f32(v0, v1);
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
    return SIMDVectorDoubleDetail::FromPairs(
        vbslq_f64(vcltq_f64(low0, low1), low0, low1),
        vbslq_f64(vcltq_f64(high0, high1), high0, high1)
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_min_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_min_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        _mm_min_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#else
    return (v0 < v1) ? v0 : v1;
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMax(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) > VectorGetX(v1) ? VectorGetX(v0) : VectorGetX(v1), VectorGetY(v0) > VectorGetY(v1) ? VectorGetY(v0) : VectorGetY(v1), VectorGetZ(v0) > VectorGetZ(v1) ? VectorGetZ(v0) : VectorGetZ(v1), VectorGetW(v0) > VectorGetW(v1) ? VectorGetW(v0) : VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vmaxq_f32(v0, v1);
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
    return SIMDVectorDoubleDetail::FromPairs(
        vbslq_f64(vcgtq_f64(low0, low1), low0, low1),
        vbslq_f64(vcgtq_f64(high0, high1), high0, high1)
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_max_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_max_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        _mm_max_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
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
#else
    SIMDVectorSingle temp = _mm_hadd_ps(value, value);
    return _mm_hadd_ps(temp, temp);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSum(SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble xy = VectorDoubleAdd(VectorDoubleSplatX(value), VectorDoubleSplatY(value));
    const SIMDVectorDouble xyz = VectorDoubleAdd(xy, VectorDoubleSplatZ(value));
    return VectorDoubleAdd(xyz, VectorDoubleSplatW(value));
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

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3MinComponent(SIMDVectorSingle value)noexcept{
    return Vector4MinComponent(VectorSwizzle<0, 1, 2, 0>(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3MinComponent(SIMDVectorDouble value)noexcept{
    return VectorDouble4MinComponent(VectorDoubleSwizzle<0, 1, 2, 0>(value));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3MaxComponent(SIMDVectorSingle value)noexcept{
    return Vector4MaxComponent(VectorSwizzle<0, 1, 2, 0>(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3MaxComponent(SIMDVectorDouble value)noexcept{
    return VectorDouble4MaxComponent(VectorDoubleSwizzle<0, 1, 2, 0>(value));
}


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
#elif defined(NWB_HAS_SSE4)
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
    return SIMDVectorDoubleDetail::FromPairs(
        vsqrtq_f64(SIMDVectorDoubleDetail::GetLow(value)),
        vsqrtq_f64(SIMDVectorDoubleDetail::GetHigh(value))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_sqrt_pd(BitCast<__m256d>(value)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_sqrt_pd(SIMDVectorDoubleDetail::GetLow(value)),
        _mm_sqrt_pd(SIMDVectorDoubleDetail::GetHigh(value))
    );
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
#if defined(NWB_HAS_NEON)
    float32x4_t s0 = vrsqrteq_f32(value);
    float32x4_t p0 = vmulq_f32(value, s0);
    float32x4_t r0 = vrsqrtsq_f32(p0, s0);
    float32x4_t s1 = vmulq_f32(s0, r0);
    float32x4_t p1 = vmulq_f32(value, s1);
    float32x4_t r1 = vrsqrtsq_f32(p1, s1);
    return vmulq_f32(s1, r1);
#elif defined(NWB_HAS_SSE4)
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


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorRound(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_round_ps(value, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
#elif defined(NWB_HAS_NEON)
#if defined(NWB_HAS_NEON64)
    return vrndnq_f32(value);
#else
    const uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(value), vreinterpretq_u32_f32(s_SIMDNegativeZero));
    const float32x4_t magic = vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(s_SIMDNoFraction), sign));
    float32x4_t result = vaddq_f32(value, magic);
    result = vsubq_f32(result, magic);
    const uint32x4_t mask = vcleq_f32(vabsq_f32(value), s_SIMDNoFraction);
    return vbslq_f32(mask, result, value);
#endif
#else
    return SIMDConvertDetail::MakeF32(
        SIMDVectorDetail::RoundToNearest(value.f[0]),
        SIMDVectorDetail::RoundToNearest(value.f[1]),
        SIMDVectorDetail::RoundToNearest(value.f[2]),
        SIMDVectorDetail::RoundToNearest(value.f[3])
    );
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleRound(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(
        vrndnq_f64(SIMDVectorDoubleDetail::GetLow(value)),
        vrndnq_f64(SIMDVectorDoubleDetail::GetHigh(value))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_round_pd(BitCast<__m256d>(value), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
#elif defined(NWB_HAS_SSE4)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_round_pd(SIMDVectorDoubleDetail::GetLow(value), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC),
        _mm_round_pd(SIMDVectorDoubleDetail::GetHigh(value), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)
    );
#else
    return VectorDoubleSet(
        SIMDVectorDoubleDetail::RoundToNearestScalar(value[0]),
        SIMDVectorDoubleDetail::RoundToNearestScalar(value[1]),
        SIMDVectorDoubleDetail::RoundToNearestScalar(value[2]),
        SIMDVectorDoubleDetail::RoundToNearestScalar(value[3])
    );
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorTruncate(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_round_ps(value, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
#elif defined(NWB_HAS_NEON)
#if defined(NWB_HAS_NEON64)
    return vrndq_f32(value);
#else
    const uint32x4_t mask = vcltq_f32(vabsq_f32(value), s_SIMDNoFraction);
    const int32x4_t integer = vcvtq_s32_f32(value);
    const float32x4_t result = vcvtq_f32_s32(integer);
    return vbslq_f32(mask, result, value);
#endif
#else
    return SIMDConvertDetail::MakeU32(
        SIMDVectorDetail::TruncateBits(value.f[0]),
        SIMDVectorDetail::TruncateBits(value.f[1]),
        SIMDVectorDetail::TruncateBits(value.f[2]),
        SIMDVectorDetail::TruncateBits(value.f[3])
    );
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleTruncate(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(
        vrndq_f64(SIMDVectorDoubleDetail::GetLow(value)),
        vrndq_f64(SIMDVectorDoubleDetail::GetHigh(value))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_round_pd(BitCast<__m256d>(value), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC));
#elif defined(NWB_HAS_SSE4)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_round_pd(SIMDVectorDoubleDetail::GetLow(value), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC),
        _mm_round_pd(SIMDVectorDoubleDetail::GetHigh(value), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC)
    );
#else
    return VectorDoubleSet(
        SIMDVectorDoubleDetail::TruncateScalar(value[0]),
        SIMDVectorDoubleDetail::TruncateScalar(value[1]),
        SIMDVectorDoubleDetail::TruncateScalar(value[2]),
        SIMDVectorDoubleDetail::TruncateScalar(value[3])
    );
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorFloor(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_floor_ps(value);
#elif defined(NWB_HAS_NEON)
#if defined(NWB_HAS_NEON64)
    return vrndmq_f32(value);
#else
    const uint32x4_t mask = vcltq_f32(vabsq_f32(value), s_SIMDNoFraction);
    const int32x4_t integer = vcvtq_s32_f32(value);
    float32x4_t result = vcvtq_f32_s32(integer);
    const uint32x4_t largerMask = vcgtq_f32(result, value);
    const float32x4_t larger = vcvtq_f32_s32(vreinterpretq_s32_u32(largerMask));
    result = vaddq_f32(result, larger);
    return vbslq_f32(mask, result, value);
#endif
#else
    return SIMDConvertDetail::MakeF32(Floor(value.f[0]), Floor(value.f[1]), Floor(value.f[2]), Floor(value.f[3]));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleFloor(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(
        vrndmq_f64(SIMDVectorDoubleDetail::GetLow(value)),
        vrndmq_f64(SIMDVectorDoubleDetail::GetHigh(value))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_round_pd(BitCast<__m256d>(value), _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
#elif defined(NWB_HAS_SSE4)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_round_pd(SIMDVectorDoubleDetail::GetLow(value), _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC),
        _mm_round_pd(SIMDVectorDoubleDetail::GetHigh(value), _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC)
    );
#else
    return VectorDoubleSet(Floor(value[0]), Floor(value[1]), Floor(value[2]), Floor(value[3]));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorCeiling(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_ceil_ps(value);
#elif defined(NWB_HAS_NEON)
#if defined(NWB_HAS_NEON64)
    return vrndpq_f32(value);
#else
    const uint32x4_t mask = vcltq_f32(vabsq_f32(value), s_SIMDNoFraction);
    const int32x4_t integer = vcvtq_s32_f32(value);
    float32x4_t result = vcvtq_f32_s32(integer);
    const uint32x4_t smallerMask = vcltq_f32(result, value);
    const float32x4_t smaller = vcvtq_f32_s32(vreinterpretq_s32_u32(smallerMask));
    result = vsubq_f32(result, smaller);
    return vbslq_f32(mask, result, value);
#endif
#else
    return SIMDConvertDetail::MakeF32(Ceil(value.f[0]), Ceil(value.f[1]), Ceil(value.f[2]), Ceil(value.f[3]));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleCeiling(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(
        vrndpq_f64(SIMDVectorDoubleDetail::GetLow(value)),
        vrndpq_f64(SIMDVectorDoubleDetail::GetHigh(value))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_round_pd(BitCast<__m256d>(value), _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
#elif defined(NWB_HAS_SSE4)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_round_pd(SIMDVectorDoubleDetail::GetLow(value), _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC),
        _mm_round_pd(SIMDVectorDoubleDetail::GetHigh(value), _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC)
    );
#else
    return VectorDoubleSet(Ceil(value[0]), Ceil(value[1]), Ceil(value[2]), Ceil(value[3]));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMod(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    const SIMDVectorSingle quotient = VectorFloor(VectorDivide(v0, v1));
    return VectorNegativeMultiplySubtract(v1, quotient, v0);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMod(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return VectorDoubleSet(
        SIMDVectorDoubleDetail::FloorRemainder(v0[0], v1[0]),
        SIMDVectorDoubleDetail::FloorRemainder(v0[1], v1[1]),
        SIMDVectorDoubleDetail::FloorRemainder(v0[2], v1[2]),
        SIMDVectorDoubleDetail::FloorRemainder(v0[3], v1[3])
    );
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorModAngles(SIMDVectorSingle angles)noexcept{
    SIMDVectorSingle quotient = VectorMultiply(angles, s_SIMDReciprocalTwoPi);
    quotient = VectorRound(quotient);
    return VectorNegativeMultiplySubtract(s_SIMDTwoPi, quotient, angles);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleModAngles(SIMDVectorDouble angles)noexcept{
    return VectorDoubleSet(
        Remainder(angles[0], s_Double2PI),
        Remainder(angles[1], s_Double2PI),
        Remainder(angles[2], s_Double2PI),
        Remainder(angles[3], s_Double2PI)
    );
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorAddAngles(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    SIMDVectorSingle result = VectorAdd(v0, v1);
    SIMDVectorSingle mask = VectorLess(result, s_SIMDNegativePi);
    SIMDVectorSingle offset = VectorSelect(VectorZero(), s_SIMDTwoPi, mask);
    mask = VectorGreaterOrEqual(result, s_SIMDPi);
    offset = VectorSelect(offset, s_SIMDNegativeTwoPi, mask);
    return VectorAdd(result, offset);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAddAngles(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    SIMDVectorDouble result = VectorDoubleAdd(v0, v1);
    SIMDVectorDouble mask = VectorDoubleLess(result, s_DoubleSIMDNegativePi);
    SIMDVectorDouble offset = VectorDoubleSelect(VectorDoubleZero(), s_DoubleSIMDTwoPi, mask);
    mask = VectorDoubleGreaterOrEqual(result, s_DoubleSIMDPi);
    offset = VectorDoubleSelect(offset, s_DoubleSIMDNegativeTwoPi, mask);
    return VectorDoubleAdd(result, offset);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSubtractAngles(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    SIMDVectorSingle result = VectorSubtract(v0, v1);
    SIMDVectorSingle mask = VectorLess(result, s_SIMDNegativePi);
    SIMDVectorSingle offset = VectorSelect(VectorZero(), s_SIMDTwoPi, mask);
    mask = VectorGreaterOrEqual(result, s_SIMDPi);
    offset = VectorSelect(offset, s_SIMDNegativeTwoPi, mask);
    return VectorAdd(result, offset);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSubtractAngles(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    SIMDVectorDouble result = VectorDoubleSubtract(v0, v1);
    SIMDVectorDouble mask = VectorDoubleLess(result, s_DoubleSIMDNegativePi);
    SIMDVectorDouble offset = VectorDoubleSelect(VectorDoubleZero(), s_DoubleSIMDTwoPi, mask);
    mask = VectorDoubleGreaterOrEqual(result, s_DoubleSIMDPi);
    offset = VectorDoubleSelect(offset, s_DoubleSIMDNegativeTwoPi, mask);
    return VectorDoubleAdd(result, offset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

