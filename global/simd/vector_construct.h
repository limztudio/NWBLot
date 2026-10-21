// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_matrix.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE u32 NWB_SIMD_CALL VectorMoveMask(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return ((value.u[0] >> s_F32SignBitShift) & 1u) | (((value.u[1] >> s_F32SignBitShift) & 1u) << 1) | (((value.u[2] >> s_F32SignBitShift) & 1u) << 2) | (((value.u[3] >> s_F32SignBitShift) & 1u) << 3);
#elif defined(NWB_HAS_NEON64)
    const uint32x4_t bits = vshrq_n_u32(vreinterpretq_u32_f32(value), 31);
    const int32x4_t shifts = { 0, 1, 2, 3 };
    return vaddvq_u32(vshlq_u32(bits, shifts));
#elif defined(NWB_HAS_NEON)
    const uint32x4_t bits = vshrq_n_u32(vreinterpretq_u32_f32(value), 31);
    return vgetq_lane_u32(bits, 0) | (vgetq_lane_u32(bits, 1) << 1) | (vgetq_lane_u32(bits, 2) << 2) | (vgetq_lane_u32(bits, 3) << 3);
#else
    return static_cast<u32>(_mm_movemask_ps(value));
#endif
}

NWB_INLINE u32 NWB_SIMD_CALL VectorDoubleMoveMask(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_NEON64)
    const uint32x4_t highWords = vuzp2q_u32(BitCast<uint32x4_t>(SIMDVectorDoubleDetail::GetLowBits(value)), BitCast<uint32x4_t>(SIMDVectorDoubleDetail::GetHighBits(value)));
    const uint32x4_t bits = vshrq_n_u32(highWords, 31u);
    const int32x4_t shifts = { 0, 1, 2, 3 };
    return vaddvq_u32(vshlq_u32(bits, shifts));
#elif defined(NWB_HAS_AVX)
    return static_cast<u32>(_mm256_movemask_pd(BitCast<__m256d>(value)));
#elif defined(NWB_HAS_SSE2)
    return static_cast<u32>(_mm_movemask_pd(SIMDVectorDoubleDetail::GetLow(value)) | (_mm_movemask_pd(SIMDVectorDoubleDetail::GetHigh(value)) << 2u));
#else
    const SIMDVectorDoubleDetail::UIntVector bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(value) >> s_F64SignBitShift;
    return static_cast<u32>(bits[0] | (bits[1] << 1u) | (bits[2] << 2u) | (bits[3] << 3u));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorZero()noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(0.0f, 0.0f, 0.0f, 0.0f);
#elif defined(NWB_HAS_NEON)
    return vdupq_n_f32(0.0f);
#else
    return _mm_setzero_ps();
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleZero()noexcept{
#if defined(NWB_HAS_NEON64)
    const float64x2_t zero = vdupq_n_f64(0.0);
    return SIMDVectorDoubleDetail::FromPairs(zero, zero);
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_setzero_pd());
#elif defined(NWB_HAS_SSE2)
    const __m128d zero = _mm_setzero_pd();
    return SIMDVectorDoubleDetail::FromPairs(zero, zero);
#else
    return SIMDVectorDouble{};
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSet(f32 x, f32 y, f32 z, f32 w)noexcept{
    return SIMDConvertDetail::MakeF32(x, y, z, w);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSet(f64 x, f64 y, f64 z, f64 w)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(vsetq_lane_f64(y, vdupq_n_f64(x), 1), vsetq_lane_f64(w, vdupq_n_f64(z), 1));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_set_pd(w, z, y, x));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_set_pd(y, x), _mm_set_pd(w, z));
#else
    return SIMDVectorDouble{ x, y, z, w };
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetInt(u32 x, u32 y, u32 z, u32 w)noexcept{
    return SIMDConvertDetail::MakeU32(x, y, z, w);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetInt(u64 x, u64 y, u64 z, u64 w)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(vsetq_lane_u64(y, vdupq_n_u64(x), 1), vsetq_lane_u64(w, vdupq_n_u64(z), 1));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_set_epi64x(BitCast<i64>(w), BitCast<i64>(z), BitCast<i64>(y), BitCast<i64>(x)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromUIntPairs(_mm_set_epi64x(BitCast<i64>(y), BitCast<i64>(x)), _mm_set_epi64x(BitCast<i64>(w), BitCast<i64>(z)));
#else
    return BitCast<SIMDVectorDouble>(SIMDVectorDoubleDetail::UIntVector{ x, y, z, w });
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorReplicate(f32 value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(value, value, value, value);
#elif defined(NWB_HAS_NEON)
    return vdupq_n_f32(value);
#else
    return _mm_set1_ps(value);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReplicate(f64 value)noexcept{
#if defined(NWB_HAS_NEON64)
    const float64x2_t replicated = vdupq_n_f64(value);
    return SIMDVectorDoubleDetail::FromPairs(replicated, replicated);
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_set1_pd(value));
#elif defined(NWB_HAS_SSE2)
    const __m128d replicated = _mm_set1_pd(value);
    return SIMDVectorDoubleDetail::FromPairs(replicated, replicated);
#else
    return VectorDoubleSet(value, value, value, value);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorReplicatePtr(const f32 value)noexcept{
    return VectorReplicate(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReplicatePtr(const f64 value)noexcept{
    return VectorDoubleReplicate(value);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorReplicateInt(u32 value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(value, value, value, value);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vdupq_n_u32(value));
#else
    return _mm_castsi128_ps(_mm_set1_epi32(static_cast<i32>(value)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReplicateInt(u64 value)noexcept{
#if defined(NWB_HAS_NEON64)
    const uint64x2_t replicated = vdupq_n_u64(value);
    return SIMDVectorDoubleDetail::FromUIntPairs(replicated, replicated);
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_set1_epi64x(BitCast<i64>(value)));
#elif defined(NWB_HAS_SSE2)
    const __m128i replicated = _mm_set1_epi64x(BitCast<i64>(value));
    return SIMDVectorDoubleDetail::FromUIntPairs(replicated, replicated);
#else
    return SIMDConvertDoubleDetail::MakeU64(value, value, value, value);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorReplicateIntPtr(const u32 value)noexcept{
    return VectorReplicateInt(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReplicateIntPtr(const u64 value)noexcept{
    return VectorDoubleReplicateInt(value);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorTrueInt()noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(s_SELECT_1, s_SELECT_1, s_SELECT_1, s_SELECT_1);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_s32(vdupq_n_s32(-1));
#else
    return _mm_castsi128_ps(_mm_set1_epi32(-1));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleTrueInt()noexcept{
    return VectorDoubleReplicateInt(s_DoubleSELECT_1);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorFalseInt()noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(0u, 0u, 0u, 0u);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vdupq_n_u32(0));
#else
    return _mm_setzero_ps();
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleFalseInt()noexcept{
    return VectorDoubleZero();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSplatX(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::SplatLane<0u>(value); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatX(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::SplatLane<0u>(value); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSplatY(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::SplatLane<1u>(value); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatY(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::SplatLane<1u>(value); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSplatZ(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::SplatLane<2u>(value); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatZ(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::SplatLane<2u>(value); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSplatW(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::SplatLane<3u>(value); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatW(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::SplatLane<3u>(value); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSplatOne()noexcept{ return s_SIMDOne; }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatOne()noexcept{ return s_DoubleSIMDOne; }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSplatInfinity()noexcept{ return s_SIMDInfinity; }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatInfinity()noexcept{ return s_DoubleSIMDInfinity; }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSplatQNaN()noexcept{ return s_SIMDQNaN; }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatQNaN()noexcept{ return s_DoubleSIMDQNaN; }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSplatEpsilon()noexcept{ return s_SIMDEpsilon; }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatEpsilon()noexcept{ return s_DoubleSIMDEpsilon; }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSplatSignMask()noexcept{ return s_SIMDNegativeZero; }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatSignMask()noexcept{ return s_DoubleSIMDNegativeZero; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

