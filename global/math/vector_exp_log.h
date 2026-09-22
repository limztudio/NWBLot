// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_arithmetic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL VectorExp2(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    __m128i iTrunc = _mm_cvttps_epi32(value);
    SIMDVector fTrunc = _mm_cvtepi32_ps(iTrunc);
    SIMDVector y = VectorSubtract(value, fTrunc);

    SIMDVector poly = VectorMultiplyAdd(s_SIMDExpEst7, y, s_SIMDExpEst6);
    poly = VectorMultiplyAdd(poly, y, s_SIMDExpEst5);
    poly = VectorMultiplyAdd(poly, y, s_SIMDExpEst4);
    poly = VectorMultiplyAdd(poly, y, s_SIMDExpEst3);
    poly = VectorMultiplyAdd(poly, y, s_SIMDExpEst2);
    poly = VectorMultiplyAdd(poly, y, s_SIMDExpEst1);
    poly = VectorMultiplyAdd(poly, y, s_SIMDOne);

    __m128i biased = _mm_add_epi32(iTrunc, s_SIMDExponentBias);
    biased = _mm_slli_epi32(biased, 23);
    SIMDVector result0 = VectorDivide(_mm_castsi128_ps(biased), poly);

    biased = _mm_add_epi32(iTrunc, s_SIMD253);
    biased = _mm_slli_epi32(biased, 23);
    SIMDVector result1 = VectorDivide(_mm_castsi128_ps(biased), poly);
    result1 = VectorMultiply(s_SIMDMinNormal, result1);

    __m128i comp = _mm_cmplt_epi32(_mm_castps_si128(value), s_SIMDBin128);
    __m128i select0 = _mm_and_si128(comp, _mm_castps_si128(result0));
    __m128i select1 = _mm_andnot_si128(comp, s_SIMDInfinity);
    __m128i result2 = _mm_or_si128(select0, select1);

    comp = _mm_cmplt_epi32(iTrunc, s_SIMDSubnormalExponent);
    select1 = _mm_and_si128(comp, _mm_castps_si128(result1));
    select0 = _mm_andnot_si128(comp, _mm_castps_si128(result0));
    __m128i result3 = _mm_or_si128(select0, select1);

    comp = _mm_cmplt_epi32(_mm_castps_si128(value), s_SIMDBinNeg150);
    select0 = _mm_and_si128(comp, result3);
    select1 = _mm_andnot_si128(comp, s_SIMDZero);
    __m128i result4 = _mm_or_si128(select0, select1);

    __m128i sign = _mm_and_si128(_mm_castps_si128(value), s_SIMDNegativeZero);
    comp = _mm_cmpeq_epi32(sign, s_SIMDNegativeZero);
    select0 = _mm_and_si128(comp, result4);
    select1 = _mm_andnot_si128(comp, result2);
    __m128i result5 = _mm_or_si128(select0, select1);

    __m128i t0 = _mm_and_si128(_mm_castps_si128(value), s_SIMDQNaNTest);
    __m128i t1 = _mm_and_si128(_mm_castps_si128(value), s_SIMDInfinity);
    t0 = _mm_cmpeq_epi32(t0, s_SIMDZero);
    t1 = _mm_cmpeq_epi32(t1, s_SIMDInfinity);
    __m128i isNaN = _mm_andnot_si128(t0, t1);

    select0 = _mm_and_si128(isNaN, s_SIMDQNaN);
    select1 = _mm_andnot_si128(isNaN, result5);
    return _mm_castsi128_ps(_mm_or_si128(select0, select1));
#elif defined(NWB_HAS_NEON)
    int32x4_t iTrunc = vcvtq_s32_f32(value);
    const float32x4_t fTrunc = vcvtq_f32_s32(iTrunc);
    const float32x4_t y = vsubq_f32(value, fTrunc);

    float32x4_t poly = vmlaq_f32(s_SIMDExpEst6, s_SIMDExpEst7, y);
    poly = vmlaq_f32(s_SIMDExpEst5, poly, y);
    poly = vmlaq_f32(s_SIMDExpEst4, poly, y);
    poly = vmlaq_f32(s_SIMDExpEst3, poly, y);
    poly = vmlaq_f32(s_SIMDExpEst2, poly, y);
    poly = vmlaq_f32(s_SIMDExpEst1, poly, y);
    poly = vmlaq_f32(s_SIMDOne, poly, y);

    int32x4_t biased = vaddq_s32(iTrunc, s_SIMDExponentBias);
    biased = vshlq_n_s32(biased, 23);
    float32x4_t result0 = VectorDivide(vreinterpretq_f32_s32(biased), poly);

    biased = vaddq_s32(iTrunc, s_SIMD253);
    biased = vshlq_n_s32(biased, 23);
    float32x4_t result1 = VectorDivide(vreinterpretq_f32_s32(biased), poly);
    result1 = vmulq_f32(s_SIMDMinNormal, result1);

    uint32x4_t comp = vcltq_s32(vreinterpretq_s32_f32(value), s_SIMDBin128);
    const float32x4_t result2 = vbslq_f32(comp, result0, s_SIMDInfinity);

    comp = vcltq_s32(iTrunc, s_SIMDSubnormalExponent);
    const float32x4_t result3 = vbslq_f32(comp, result1, result0);

    comp = vcltq_s32(vreinterpretq_s32_f32(value), s_SIMDBinNeg150);
    const float32x4_t result4 = vbslq_f32(comp, result3, s_SIMDZero);

    const int32x4_t sign = vandq_s32(vreinterpretq_s32_f32(value), s_SIMDNegativeZero);
    comp = vceqq_s32(sign, s_SIMDNegativeZero);
    const float32x4_t result5 = vbslq_f32(comp, result4, result2);

    uint32x4_t t0 = vandq_u32(vreinterpretq_u32_f32(value), s_SIMDQNaNTest);
    uint32x4_t t1 = vandq_u32(vreinterpretq_u32_f32(value), s_SIMDInfinity);
    t0 = vceqq_u32(t0, s_SIMDZero);
    t1 = vceqq_u32(t1, s_SIMDInfinity);
    const uint32x4_t isNaN = vbicq_u32(t1, t0);

    return vbslq_f32(isNaN, s_SIMDQNaN, result5);
#else
    return VectorSet(Exp2(VectorGetX(value)), Exp2(VectorGetY(value)), Exp2(VectorGetZ(value)), Exp2(VectorGetW(value)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorExp10(SIMDVector value)noexcept{
    return VectorExp2(VectorMultiply(s_SIMDLg10, value));
}

NWB_INLINE SIMDVector SIMDCALL VectorExpE(SIMDVector value)noexcept{
    return VectorExp2(VectorMultiply(s_SIMDLgE, value));
}

NWB_INLINE SIMDVector SIMDCALL VectorExp(SIMDVector value)noexcept{ return VectorExpE(value); }

NWB_INLINE SIMDVector SIMDCALL VectorLog2(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    __m128i rawBiased = _mm_and_si128(_mm_castps_si128(value), s_SIMDInfinity);
    __m128i trailing = _mm_and_si128(_mm_castps_si128(value), s_SIMDQNaNTest);
    const __m128i isExponentZero = _mm_cmpeq_epi32(s_SIMDZero, rawBiased);

    const __m128i biased = _mm_srli_epi32(rawBiased, 23);
    const __m128i exponentNormal = _mm_sub_epi32(biased, s_SIMDExponentBias);
    const __m128i trailingNormal = trailing;

    const __m128i leading = SIMDVectorDetail::GetLeadingBit(trailing);
    const __m128i shift = _mm_sub_epi32(s_SIMDNumTrailing, leading);
    const __m128i exponentSubnormal = _mm_sub_epi32(s_SIMDSubnormalExponent, shift);
    __m128i trailingSubnormal = SIMDVectorDetail::MultiSllEpi32(trailing, shift);
    trailingSubnormal = _mm_and_si128(trailingSubnormal, s_SIMDQNaNTest);

    __m128i select0 = _mm_and_si128(isExponentZero, exponentSubnormal);
    __m128i select1 = _mm_andnot_si128(isExponentZero, exponentNormal);
    const __m128i exponent = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isExponentZero, trailingSubnormal);
    select1 = _mm_andnot_si128(isExponentZero, trailingNormal);
    const __m128i mantissa = _mm_or_si128(select0, select1);

    const __m128i normalized = _mm_or_si128(s_SIMDOne, mantissa);
    const SIMDVector y = _mm_sub_ps(_mm_castsi128_ps(normalized), s_SIMDOne);

    SIMDVector log2 = VectorMultiplyAdd(s_SIMDLogEst7, y, s_SIMDLogEst6);
    log2 = VectorMultiplyAdd(log2, y, s_SIMDLogEst5);
    log2 = VectorMultiplyAdd(log2, y, s_SIMDLogEst4);
    log2 = VectorMultiplyAdd(log2, y, s_SIMDLogEst3);
    log2 = VectorMultiplyAdd(log2, y, s_SIMDLogEst2);
    log2 = VectorMultiplyAdd(log2, y, s_SIMDLogEst1);
    log2 = VectorMultiplyAdd(log2, y, s_SIMDLogEst0);
    log2 = VectorMultiplyAdd(log2, y, _mm_cvtepi32_ps(exponent));

    __m128i isInfinite = _mm_and_si128(_mm_castps_si128(value), s_SIMDAbsMask);
    isInfinite = _mm_cmpeq_epi32(isInfinite, s_SIMDInfinity);

    const __m128i isGreaterZero = _mm_cmpgt_epi32(_mm_castps_si128(value), s_SIMDZero);
    const __m128i isNotFinite = _mm_cmpgt_epi32(_mm_castps_si128(value), s_SIMDInfinity);
    const __m128i isPositive = _mm_andnot_si128(isNotFinite, isGreaterZero);

    __m128i isZero = _mm_and_si128(_mm_castps_si128(value), s_SIMDAbsMask);
    isZero = _mm_cmpeq_epi32(isZero, s_SIMDZero);

    __m128i t0 = _mm_and_si128(_mm_castps_si128(value), s_SIMDQNaNTest);
    __m128i t1 = _mm_and_si128(_mm_castps_si128(value), s_SIMDInfinity);
    t0 = _mm_cmpeq_epi32(t0, s_SIMDZero);
    t1 = _mm_cmpeq_epi32(t1, s_SIMDInfinity);
    const __m128i isNaN = _mm_andnot_si128(t0, t1);

    select0 = _mm_and_si128(isInfinite, s_SIMDInfinity);
    select1 = _mm_andnot_si128(isInfinite, _mm_castps_si128(log2));
    __m128i result = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isZero, s_SIMDNegInfinity);
    select1 = _mm_andnot_si128(isZero, s_SIMDNegQNaN);
    const __m128i nonPositive = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isPositive, result);
    select1 = _mm_andnot_si128(isPositive, nonPositive);
    result = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isNaN, s_SIMDQNaN);
    select1 = _mm_andnot_si128(isNaN, result);
    return _mm_castsi128_ps(_mm_or_si128(select0, select1));
#elif defined(NWB_HAS_NEON)
    const int32x4_t rawBiased = vandq_s32(vreinterpretq_s32_f32(value), s_SIMDInfinity);
    const int32x4_t trailing = vandq_s32(vreinterpretq_s32_f32(value), s_SIMDQNaNTest);
    const uint32x4_t isExponentZero = vceqq_s32(vreinterpretq_s32_f32(s_SIMDZero), rawBiased);

    const int32x4_t biased = vshrq_n_s32(rawBiased, 23);
    const int32x4_t exponentNormal = vsubq_s32(biased, s_SIMDExponentBias);
    const int32x4_t trailingNormal = trailing;

    const int32x4_t leading = SIMDVectorDetail::GetLeadingBit(trailing);
    const int32x4_t shift = vsubq_s32(s_SIMDNumTrailing, leading);
    const int32x4_t exponentSubnormal = vsubq_s32(s_SIMDSubnormalExponent, shift);
    int32x4_t trailingSubnormal = vshlq_s32(trailing, shift);
    trailingSubnormal = vandq_s32(trailingSubnormal, s_SIMDQNaNTest);

    const int32x4_t exponent = vbslq_s32(isExponentZero, exponentSubnormal, exponentNormal);
    const int32x4_t mantissa = vbslq_s32(isExponentZero, trailingSubnormal, trailingNormal);

    const int32x4_t normalized = vorrq_s32(vreinterpretq_s32_f32(s_SIMDOne), mantissa);
    const float32x4_t y = vsubq_f32(vreinterpretq_f32_s32(normalized), s_SIMDOne);

    float32x4_t log2 = vmlaq_f32(s_SIMDLogEst6, s_SIMDLogEst7, y);
    log2 = vmlaq_f32(s_SIMDLogEst5, log2, y);
    log2 = vmlaq_f32(s_SIMDLogEst4, log2, y);
    log2 = vmlaq_f32(s_SIMDLogEst3, log2, y);
    log2 = vmlaq_f32(s_SIMDLogEst2, log2, y);
    log2 = vmlaq_f32(s_SIMDLogEst1, log2, y);
    log2 = vmlaq_f32(s_SIMDLogEst0, log2, y);
    log2 = vmlaq_f32(vcvtq_f32_s32(exponent), log2, y);

    uint32x4_t isInfinite = vandq_u32(vreinterpretq_u32_f32(value), s_SIMDAbsMask);
    isInfinite = vceqq_u32(isInfinite, s_SIMDInfinity);

    const uint32x4_t isGreaterZero = vcgtq_f32(value, s_SIMDZero);
    const uint32x4_t isNotFinite = vcgtq_f32(value, s_SIMDInfinity);
    const uint32x4_t isPositive = vbicq_u32(isGreaterZero, isNotFinite);

    uint32x4_t isZero = vandq_u32(vreinterpretq_u32_f32(value), s_SIMDAbsMask);
    isZero = vceqq_u32(isZero, s_SIMDZero);

    uint32x4_t t0 = vandq_u32(vreinterpretq_u32_f32(value), s_SIMDQNaNTest);
    uint32x4_t t1 = vandq_u32(vreinterpretq_u32_f32(value), s_SIMDInfinity);
    t0 = vceqq_u32(t0, s_SIMDZero);
    t1 = vceqq_u32(t1, s_SIMDInfinity);
    const uint32x4_t isNaN = vbicq_u32(t1, t0);

    float32x4_t result = vbslq_f32(isInfinite, s_SIMDInfinity, log2);
    const float32x4_t nonPositive = vbslq_f32(isZero, s_SIMDNegInfinity, s_SIMDNegQNaN);
    result = vbslq_f32(isPositive, result, nonPositive);
    return vbslq_f32(isNaN, s_SIMDQNaN, result);
#else
    return VectorSet(Log2(VectorGetX(value)), Log2(VectorGetY(value)), Log2(VectorGetZ(value)), Log2(VectorGetW(value)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorLog10(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4) || defined(NWB_HAS_NEON)
    return VectorMultiply(s_SIMDInvLg10, VectorLog2(value));
#else
    return VectorSet(Log10(VectorGetX(value)), Log10(VectorGetY(value)), Log10(VectorGetZ(value)), Log10(VectorGetW(value)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorLogE(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4) || defined(NWB_HAS_NEON)
    return VectorMultiply(s_SIMDInvLgE, VectorLog2(value));
#else
    return VectorSet(Log(VectorGetX(value)), Log(VectorGetY(value)), Log(VectorGetZ(value)), Log(VectorGetW(value)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorLog(SIMDVector value)noexcept{ return VectorLogE(value); }

NWB_INLINE SIMDVector SIMDCALL VectorPow(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(Pow(VectorGetX(v0), VectorGetX(v1)), Pow(VectorGetY(v0), VectorGetY(v1)), Pow(VectorGetZ(v0), VectorGetZ(v1)), Pow(VectorGetW(v0), VectorGetW(v1)));
#elif defined(NWB_HAS_NEON)
    return SIMDConvertDetail::MakeF32(
        Pow(vgetq_lane_f32(v0, 0), vgetq_lane_f32(v1, 0)),
        Pow(vgetq_lane_f32(v0, 1), vgetq_lane_f32(v1, 1)),
        Pow(vgetq_lane_f32(v0, 2), vgetq_lane_f32(v1, 2)),
        Pow(vgetq_lane_f32(v0, 3), vgetq_lane_f32(v1, 3))
    );
#else
    return VectorSet(
        Pow(VectorGetX(v0), VectorGetX(v1)),
        Pow(VectorGetY(v0), VectorGetY(v1)),
        Pow(VectorGetZ(v0), VectorGetZ(v1)),
        Pow(VectorGetW(v0), VectorGetW(v1))
    );
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

