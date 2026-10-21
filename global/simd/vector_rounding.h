// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_arithmetic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_SSE2) && !defined(NWB_HAS_SSE4)
namespace SIMDVectorArithmeticDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Rounding{
enum Enum : u8{
    Nearest,
    Truncate,
    Floor,
    Ceiling
};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<Rounding::Enum Mode>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL RoundFixed(SIMDVectorSingle value)noexcept{
    const __m128i bits = _mm_castps_si128(value);
    const __m128i magnitude = _mm_and_si128(bits, _mm_set1_epi32(static_cast<i32>(s_F32AbsoluteBitMask)));
    const __m128i sign = _mm_and_si128(bits, _mm_set1_epi32(static_cast<i32>(s_F32SignBitMask)));
    const __m128i integral = _mm_cmpgt_epi32(magnitude, _mm_set1_epi32(static_cast<i32>(s_F32FractionLimitBits)));
    __m128i result{};
    if constexpr(Mode == Rounding::Floor || Mode == Rounding::Ceiling){
        // Only finite values below 2^23 need conversion, preserving precision exceptions without invalid conversions.
        const __m128 safe = _mm_andnot_ps(_mm_castsi128_ps(integral), value);
        __m128i integer = _mm_cvttps_epi32(safe);
        const __m128 truncated = _mm_cvtepi32_ps(integer);
        if constexpr(Mode == Rounding::Floor)
            integer = _mm_add_epi32(integer, _mm_castps_si128(_mm_cmpgt_ps(truncated, safe)));
        else
            integer = _mm_sub_epi32(integer, _mm_castps_si128(_mm_cmplt_ps(truncated, safe)));
        result = _mm_castps_si128(_mm_cvtepi32_ps(integer));
    }
    else{
        // Integer masks preserve fixed nearest/truncate rounding and signed zero without raising precision exceptions.
        const __m128i isSmall = _mm_cmplt_epi32(magnitude, _mm_set1_epi32(0x3F800000));
        const __m128i exponent = _mm_srli_epi32(magnitude, 23);
        const __m128i shift = _mm_sub_epi32(_mm_set1_epi32(150), exponent);
        const __m128i mask = SIMDVectorDetail::MultiSllEpi32(_mm_set1_epi32(-1), shift);
        result = _mm_andnot_si128(isSmall, _mm_and_si128(magnitude, mask));

        if constexpr(Mode == Rounding::Nearest){
            const __m128i unit = _mm_sub_epi32(_mm_setzero_si128(), mask);
            const __m128i fraction = _mm_andnot_si128(mask, magnitude);
            const __m128i half = _mm_srli_epi32(unit, 1);
            const __m128i odd = _mm_cmpgt_epi32(_mm_and_si128(result, unit), _mm_setzero_si128());
            const __m128i increment = _mm_or_si128(_mm_cmpgt_epi32(fraction, half), _mm_and_si128(_mm_cmpeq_epi32(fraction, half), odd));
            const __m128i smallResult = _mm_and_si128(_mm_cmpgt_epi32(magnitude, _mm_set1_epi32(0x3F000000)), _mm_set1_epi32(0x3F800000));
            result = _mm_add_epi32(result, _mm_and_si128(increment, unit));
            result = _mm_or_si128(_mm_and_si128(isSmall, smallResult), _mm_andnot_si128(isSmall, result));
        }
    }

    result = _mm_or_si128(_mm_and_si128(integral, magnitude), _mm_andnot_si128(integral, result));
    result = _mm_or_si128(result, sign);
    const __m128i isNaN = _mm_cmpgt_epi32(magnitude, _mm_set1_epi32(static_cast<i32>(s_F32InfinityBits)));
    // Quiet only NaN lanes so large finite inputs cannot overflow during payload-preserving addition.
    const __m128 nan = _mm_and_ps(_mm_castsi128_ps(isNaN), value);
    const __m128 quietNaN = _mm_add_ps(nan, nan);
    return _mm_or_ps(_mm_and_ps(_mm_castsi128_ps(isNaN), quietNaN), _mm_andnot_ps(_mm_castsi128_ps(isNaN), _mm_castsi128_ps(result)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorRound(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_round_ps(value, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorArithmeticDetail::RoundFixed<SIMDVectorArithmeticDetail::Rounding::Nearest>(value);
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
    return SIMDConvertDetail::MakeF32(VectorScalarDetail::RoundToNearest(value.f[0]), VectorScalarDetail::RoundToNearest(value.f[1]), VectorScalarDetail::RoundToNearest(value.f[2]), VectorScalarDetail::RoundToNearest(value.f[3]));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleRound(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(vrndnq_f64(SIMDVectorDoubleDetail::GetLow(value)), vrndnq_f64(SIMDVectorDoubleDetail::GetHigh(value)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_round_pd(BitCast<__m256d>(value), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
#elif defined(NWB_HAS_SSE4)
    return SIMDVectorDoubleDetail::FromPairs(_mm_round_pd(SIMDVectorDoubleDetail::GetLow(value), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC), _mm_round_pd(SIMDVectorDoubleDetail::GetHigh(value), _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC));
#else
    return VectorDoubleSet(VectorScalarDetail::RoundToNearest(value[0]), VectorScalarDetail::RoundToNearest(value[1]), VectorScalarDetail::RoundToNearest(value[2]), VectorScalarDetail::RoundToNearest(value[3]));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorTruncate(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_round_ps(value, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorArithmeticDetail::RoundFixed<SIMDVectorArithmeticDetail::Rounding::Truncate>(value);
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
    return SIMDConvertDetail::MakeU32(VectorScalarDetail::TruncateBits(value.f[0]), VectorScalarDetail::TruncateBits(value.f[1]), VectorScalarDetail::TruncateBits(value.f[2]), VectorScalarDetail::TruncateBits(value.f[3]));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleTruncate(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(vrndq_f64(SIMDVectorDoubleDetail::GetLow(value)), vrndq_f64(SIMDVectorDoubleDetail::GetHigh(value)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_round_pd(BitCast<__m256d>(value), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC));
#elif defined(NWB_HAS_SSE4)
    return SIMDVectorDoubleDetail::FromPairs(_mm_round_pd(SIMDVectorDoubleDetail::GetLow(value), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC), _mm_round_pd(SIMDVectorDoubleDetail::GetHigh(value), _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC));
#else
    return VectorDoubleSet(BitCast<f64>(VectorScalarDetail::TruncateBits(value[0])), BitCast<f64>(VectorScalarDetail::TruncateBits(value[1])), BitCast<f64>(VectorScalarDetail::TruncateBits(value[2])), BitCast<f64>(VectorScalarDetail::TruncateBits(value[3])));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorFloor(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_floor_ps(value);
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorArithmeticDetail::RoundFixed<SIMDVectorArithmeticDetail::Rounding::Floor>(value);
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
    return SIMDVectorDoubleDetail::FromPairs(vrndmq_f64(SIMDVectorDoubleDetail::GetLow(value)), vrndmq_f64(SIMDVectorDoubleDetail::GetHigh(value)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_round_pd(BitCast<__m256d>(value), _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
#elif defined(NWB_HAS_SSE4)
    return SIMDVectorDoubleDetail::FromPairs(_mm_round_pd(SIMDVectorDoubleDetail::GetLow(value), _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC), _mm_round_pd(SIMDVectorDoubleDetail::GetHigh(value), _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC));
#else
    return VectorDoubleSet(Floor(value[0]), Floor(value[1]), Floor(value[2]), Floor(value[3]));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorCeiling(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_ceil_ps(value);
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorArithmeticDetail::RoundFixed<SIMDVectorArithmeticDetail::Rounding::Ceiling>(value);
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
    return SIMDVectorDoubleDetail::FromPairs(vrndpq_f64(SIMDVectorDoubleDetail::GetLow(value)), vrndpq_f64(SIMDVectorDoubleDetail::GetHigh(value)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_round_pd(BitCast<__m256d>(value), _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
#elif defined(NWB_HAS_SSE4)
    return SIMDVectorDoubleDetail::FromPairs(_mm_round_pd(SIMDVectorDoubleDetail::GetLow(value), _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC), _mm_round_pd(SIMDVectorDoubleDetail::GetHigh(value), _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC));
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
    // FMod stays libm per lane for exact results. The floor fix-up runs in packed lanes.
    const SIMDVectorDouble remainder = VectorDoubleSet(FMod(v0[0], v1[0]), FMod(v0[1], v1[1]), FMod(v0[2], v1[2]), FMod(v0[3], v1[3]));
    const SIMDVectorDouble zero = VectorDoubleEqual(remainder, VectorDoubleZero());
    // Only nonzero remainders whose sign differs from the divisor add the divisor; other lanes add 0 + 0.
    const SIMDVectorDouble wrap = VectorDoubleAndCInt(VectorDoubleNotEqualInt(VectorDoubleAndInt(VectorDoubleXorInt(remainder, v1), s_DoubleSIMDNegativeZero), VectorDoubleZero()), zero);
    const SIMDVectorDouble wrapped = VectorDoubleAdd(VectorDoubleAndInt(remainder, wrap), VectorDoubleAndInt(v1, wrap));
    return VectorDoubleSelect(VectorDoubleSelect(remainder, wrapped, wrap), VectorDoubleAndInt(v1, s_DoubleSIMDNegativeZero), zero);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorModAngles(SIMDVectorSingle angles)noexcept{
    SIMDVectorSingle quotient = VectorMultiply(angles, s_SIMDReciprocalTwoPi);
    quotient = VectorRound(quotient);
    return VectorNegativeMultiplySubtract(s_SIMDTwoPi, quotient, angles);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleModAngles(SIMDVectorDouble angles)noexcept{
    return VectorDoubleSet(Remainder(angles[0], s_Double2PI), Remainder(angles[1], s_Double2PI), Remainder(angles[2], s_Double2PI), Remainder(angles[3], s_Double2PI));
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

