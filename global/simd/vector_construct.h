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


NWB_INLINE f32 NWB_SIMD_CALL VectorGetX(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetLane<0u>(value); }

NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetX(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<0u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetXPtr(f32& out, SIMDVectorSingle value)noexcept{ out = VectorGetX(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetXPtr(f64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetX(value); }

NWB_INLINE f32 NWB_SIMD_CALL VectorGetY(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetLane<1u>(value); }

NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetY(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<1u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetYPtr(f32& out, SIMDVectorSingle value)noexcept{ out = VectorGetY(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetYPtr(f64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetY(value); }

NWB_INLINE f32 NWB_SIMD_CALL VectorGetZ(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetLane<2u>(value); }

NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetZ(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<2u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetZPtr(f32& out, SIMDVectorSingle value)noexcept{ out = VectorGetZ(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetZPtr(f64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetZ(value); }

NWB_INLINE f32 NWB_SIMD_CALL VectorGetW(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetLane<3u>(value); }

NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetW(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<3u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetWPtr(f32& out, SIMDVectorSingle value)noexcept{ out = VectorGetW(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetWPtr(f64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetW(value); }

NWB_INLINE f32 NWB_SIMD_CALL VectorGetByIndex(SIMDVectorSingle value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorGetX(value);
    case 1u: return VectorGetY(value);
    case 2u: return VectorGetZ(value);
    default: return VectorGetW(value);
    }
}

NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetByIndex(SIMDVectorDouble value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleGetX(value);
    case 1u: return VectorDoubleGetY(value);
    case 2u: return VectorDoubleGetZ(value);
    default: return VectorDoubleGetW(value);
    }
}

NWB_INLINE void NWB_SIMD_CALL VectorGetByIndexPtr(f32& out, SIMDVectorSingle value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    out = VectorGetByIndex(value, index);
}

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetByIndexPtr(f64& out, SIMDVectorDouble value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    out = VectorDoubleGetByIndex(value, index);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorGetIntX(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetIntLane<0u>(value); }

NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntX(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<0u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetIntXPtr(u32& out, SIMDVectorSingle value)noexcept{ out = VectorGetIntX(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntXPtr(u64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetIntX(value); }

NWB_INLINE u32 NWB_SIMD_CALL VectorGetIntY(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetIntLane<1u>(value); }

NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntY(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<1u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetIntYPtr(u32& out, SIMDVectorSingle value)noexcept{ out = VectorGetIntY(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntYPtr(u64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetIntY(value); }

NWB_INLINE u32 NWB_SIMD_CALL VectorGetIntZ(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetIntLane<2u>(value); }

NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntZ(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<2u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetIntZPtr(u32& out, SIMDVectorSingle value)noexcept{ out = VectorGetIntZ(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntZPtr(u64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetIntZ(value); }

NWB_INLINE u32 NWB_SIMD_CALL VectorGetIntW(SIMDVectorSingle value)noexcept{ return SIMDVectorDetail::GetIntLane<3u>(value); }

NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntW(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<3u>(value); }

NWB_INLINE void NWB_SIMD_CALL VectorGetIntWPtr(u32& out, SIMDVectorSingle value)noexcept{ out = VectorGetIntW(value); }

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntWPtr(u64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetIntW(value); }

NWB_INLINE u32 NWB_SIMD_CALL VectorGetIntByIndex(SIMDVectorSingle value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorGetIntX(value);
    case 1u: return VectorGetIntY(value);
    case 2u: return VectorGetIntZ(value);
    default: return VectorGetIntW(value);
    }
}

NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntByIndex(SIMDVectorDouble value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleGetIntX(value);
    case 1u: return VectorDoubleGetIntY(value);
    case 2u: return VectorDoubleGetIntZ(value);
    default: return VectorDoubleGetIntW(value);
    }
}

NWB_INLINE void NWB_SIMD_CALL VectorGetIntByIndexPtr(u32& out, SIMDVectorSingle value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    out = VectorGetIntByIndex(value, index);
}

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntByIndexPtr(u64& out, SIMDVectorDouble value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    out = VectorDoubleGetIntByIndex(value, index);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetX(SIMDVectorSingle value, f32 x)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.f[0] = x;
    return value;
#elif defined(NWB_HAS_NEON)
    return vsetq_lane_f32(x, value, 0);
#else
    return _mm_move_ss(value, _mm_set_ss(x));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetX(SIMDVectorDouble value, f64 x)noexcept{
    return SIMDVectorDoubleDetail::SetLane<0u>(value, x);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetXPtr(SIMDVectorSingle value, const f32 x)noexcept{
    return VectorSetX(value, x);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetXPtr(SIMDVectorDouble value, const f64 x)noexcept{
    return VectorDoubleSetX(value, x);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetY(SIMDVectorSingle value, f32 y)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.f[1] = y;
    return value;
#elif defined(NWB_HAS_NEON)
    return vsetq_lane_f32(y, value, 1);
#elif defined(NWB_HAS_SSE4)
    return _mm_insert_ps(value, _mm_set_ss(y), 0x10);
#else
    return SIMDVectorDetail::SetLane<1u>(value, y);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetY(SIMDVectorDouble value, f64 y)noexcept{
    return SIMDVectorDoubleDetail::SetLane<1u>(value, y);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetYPtr(SIMDVectorSingle value, const f32 y)noexcept{
    return VectorSetY(value, y);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetYPtr(SIMDVectorDouble value, const f64 y)noexcept{
    return VectorDoubleSetY(value, y);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetZ(SIMDVectorSingle value, f32 z)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.f[2] = z;
    return value;
#elif defined(NWB_HAS_NEON)
    return vsetq_lane_f32(z, value, 2);
#elif defined(NWB_HAS_SSE4)
    return _mm_insert_ps(value, _mm_set_ss(z), 0x20);
#else
    return SIMDVectorDetail::SetLane<2u>(value, z);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetZ(SIMDVectorDouble value, f64 z)noexcept{
    return SIMDVectorDoubleDetail::SetLane<2u>(value, z);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetZPtr(SIMDVectorSingle value, const f32 z)noexcept{
    return VectorSetZ(value, z);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetZPtr(SIMDVectorDouble value, const f64 z)noexcept{
    return VectorDoubleSetZ(value, z);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetW(SIMDVectorSingle value, f32 w)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.f[3] = w;
    return value;
#elif defined(NWB_HAS_NEON)
    return vsetq_lane_f32(w, value, 3);
#elif defined(NWB_HAS_SSE4)
    return _mm_insert_ps(value, _mm_set_ss(w), 0x30);
#else
    return SIMDVectorDetail::SetLane<3u>(value, w);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetW(SIMDVectorDouble value, f64 w)noexcept{
    return SIMDVectorDoubleDetail::SetLane<3u>(value, w);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetWPtr(SIMDVectorSingle value, const f32 w)noexcept{
    return VectorSetW(value, w);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetWPtr(SIMDVectorDouble value, const f64 w)noexcept{
    return VectorDoubleSetW(value, w);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetByIndex(SIMDVectorSingle value, f32 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorSetX(value, component);
    case 1u: return VectorSetY(value, component);
    case 2u: return VectorSetZ(value, component);
    default: return VectorSetW(value, component);
    }
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetByIndex(SIMDVectorDouble value, f64 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleSetX(value, component);
    case 1u: return VectorDoubleSetY(value, component);
    case 2u: return VectorDoubleSetZ(value, component);
    default: return VectorDoubleSetW(value, component);
    }
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetByIndexPtr(SIMDVectorSingle value, const f32 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    return VectorSetByIndex(value, component, index);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetByIndexPtr(SIMDVectorDouble value, const f64 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    return VectorDoubleSetByIndex(value, component, index);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntX(SIMDVectorSingle value, u32 x)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.u[0] = x;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(x, vreinterpretq_u32_f32(value), 0));
#else
    return _mm_move_ss(value, _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<i32>(x))));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntX(SIMDVectorDouble value, u64 x)noexcept{
    return SIMDVectorDoubleDetail::SetLane<0u>(value, BitCast<f64>(x));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntXPtr(SIMDVectorSingle value, const u32 x)noexcept{
    return VectorSetIntX(value, x);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntXPtr(SIMDVectorDouble value, const u64 x)noexcept{
    return VectorDoubleSetIntX(value, x);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntY(SIMDVectorSingle value, u32 y)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.u[1] = y;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(y, vreinterpretq_u32_f32(value), 1));
#elif defined(NWB_HAS_SSE4)
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(y), 1));
#else
    return SIMDVectorDetail::SetLane<1u>(value, BitCast<f32>(y));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntY(SIMDVectorDouble value, u64 y)noexcept{
    return SIMDVectorDoubleDetail::SetLane<1u>(value, BitCast<f64>(y));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntYPtr(SIMDVectorSingle value, const u32 y)noexcept{
    return VectorSetIntY(value, y);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntYPtr(SIMDVectorDouble value, const u64 y)noexcept{
    return VectorDoubleSetIntY(value, y);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntZ(SIMDVectorSingle value, u32 z)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.u[2] = z;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(z, vreinterpretq_u32_f32(value), 2));
#elif defined(NWB_HAS_SSE4)
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(z), 2));
#else
    return SIMDVectorDetail::SetLane<2u>(value, BitCast<f32>(z));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntZ(SIMDVectorDouble value, u64 z)noexcept{
    return SIMDVectorDoubleDetail::SetLane<2u>(value, BitCast<f64>(z));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntZPtr(SIMDVectorSingle value, const u32 z)noexcept{
    return VectorSetIntZ(value, z);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntZPtr(SIMDVectorDouble value, const u64 z)noexcept{
    return VectorDoubleSetIntZ(value, z);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntW(SIMDVectorSingle value, u32 w)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.u[3] = w;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(w, vreinterpretq_u32_f32(value), 3));
#elif defined(NWB_HAS_SSE4)
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(w), 3));
#else
    return SIMDVectorDetail::SetLane<3u>(value, BitCast<f32>(w));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntW(SIMDVectorDouble value, u64 w)noexcept{
    return SIMDVectorDoubleDetail::SetLane<3u>(value, BitCast<f64>(w));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntWPtr(SIMDVectorSingle value, const u32 w)noexcept{
    return VectorSetIntW(value, w);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntWPtr(SIMDVectorDouble value, const u64 w)noexcept{
    return VectorDoubleSetIntW(value, w);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntByIndex(SIMDVectorSingle value, u32 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorSetIntX(value, component);
    case 1u: return VectorSetIntY(value, component);
    case 2u: return VectorSetIntZ(value, component);
    default: return VectorSetIntW(value, component);
    }
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntByIndex(SIMDVectorDouble value, u64 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleSetIntX(value, component);
    case 1u: return VectorDoubleSetIntY(value, component);
    case 2u: return VectorDoubleSetIntZ(value, component);
    default: return VectorDoubleSetIntW(value, component);
    }
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSetIntByIndexPtr(SIMDVectorSingle value, const u32 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    return VectorSetIntByIndex(value, component, index);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntByIndexPtr(SIMDVectorDouble value, const u64 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    return VectorDoubleSetIntByIndex(value, component, index);
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


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle(SIMDVectorSingle value, u32 e0, u32 e1, u32 e2, u32 e3)noexcept{
    NWB_ASSERT(e0 < 4 && e1 < 4 && e2 < 4 && e3 < 4);
#if defined(NWB_HAS_NEON64)
    return vreinterpretq_f32_u8(vqtbl1q_u8(vreinterpretq_u8_f32(value), SIMDVectorDetail::PermuteByteIndices(e0, e1, e2, e3)));
#elif defined(NWB_HAS_NEON)
    static const u32 s_controlElement[4] = { 0x03020100u, 0x07060504u, 0x0B0A0908u, 0x0F0E0D0Cu, };

    uint8x8x2_t table{};
    table.val[0] = vreinterpret_u8_f32(vget_low_f32(value));
    table.val[1] = vreinterpret_u8_f32(vget_high_f32(value));

    uint32x2_t idx = vcreate_u32(static_cast<u64>(s_controlElement[e0]) | (static_cast<u64>(s_controlElement[e1]) << s_F32BitCount));
    const uint8x8_t lo = vtbl2_u8(table, vreinterpret_u8_u32(idx));

    idx = vcreate_u32(static_cast<u64>(s_controlElement[e2]) | (static_cast<u64>(s_controlElement[e3]) << s_F32BitCount));
    const uint8x8_t hi = vtbl2_u8(table, vreinterpret_u8_u32(idx));
    return vcombine_f32(vreinterpret_f32_u8(lo), vreinterpret_f32_u8(hi));
#elif defined(NWB_HAS_AVX)
    const __m128i control = _mm_set_epi32(static_cast<i32>(e3), static_cast<i32>(e2), static_cast<i32>(e1), static_cast<i32>(e0));
    return _mm_permutevar_ps(value, control);
#elif defined(NWB_HAS_SSE2)
    const __m128i control = _mm_set_epi32(static_cast<i32>(e3), static_cast<i32>(e2), static_cast<i32>(e1), static_cast<i32>(e0));
    return SIMDVectorDetail::SwizzleLanes(value, control);
#else
    return VectorSetInt(value.u[e0], value.u[e1], value.u[e2], value.u[e3]);
#endif
}

template<u32 E0, u32 E1, u32 E2, u32 E3>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle(SIMDVectorSingle value)noexcept{
    static_assert(E0 < 4 && E1 < 4 && E2 < 4 && E3 < 4);
#if defined(NWB_HAS_SCALAR) || defined(NWB_HAS_NEON)
    return VectorSwizzle(value, E0, E1, E2, E3);
#elif defined(NWB_HAS_AVX)
    return _mm_permute_ps(value, _MM_SHUFFLE(E3, E2, E1, E0));
#else
    return _mm_shuffle_ps(value, value, _MM_SHUFFLE(E3, E2, E1, E0));
#endif
}

template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<0, 1, 2, 3>(SIMDVectorSingle value)noexcept{ return value; }

#if defined(NWB_HAS_SSE4)
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<0, 1, 0, 1>(SIMDVectorSingle value)noexcept{ return _mm_movelh_ps(value, value); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<2, 3, 2, 3>(SIMDVectorSingle value)noexcept{ return _mm_movehl_ps(value, value); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<0, 0, 1, 1>(SIMDVectorSingle value)noexcept{ return _mm_unpacklo_ps(value, value); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<2, 2, 3, 3>(SIMDVectorSingle value)noexcept{ return _mm_unpackhi_ps(value, value); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<0, 0, 2, 2>(SIMDVectorSingle value)noexcept{ return _mm_moveldup_ps(value); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<1, 1, 3, 3>(SIMDVectorSingle value)noexcept{ return _mm_movehdup_ps(value); }
#endif

#if defined(NWB_HAS_AVX2)
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<0, 0, 0, 0>(SIMDVectorSingle value)noexcept{ return _mm_broadcastss_ps(value); }
#endif

#if defined(NWB_HAS_NEON)
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<0, 0, 0, 0>(SIMDVectorSingle value)noexcept{ return vdupq_lane_f32(vget_low_f32(value), 0); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<1, 1, 1, 1>(SIMDVectorSingle value)noexcept{ return vdupq_lane_f32(vget_low_f32(value), 1); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<2, 2, 2, 2>(SIMDVectorSingle value)noexcept{ return vdupq_lane_f32(vget_high_f32(value), 0); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<3, 3, 3, 3>(SIMDVectorSingle value)noexcept{ return vdupq_lane_f32(vget_high_f32(value), 1); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<1, 0, 3, 2>(SIMDVectorSingle value)noexcept{ return vrev64q_f32(value); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<0, 1, 0, 1>(SIMDVectorSingle value)noexcept{
    const float32x2_t temp = vget_low_f32(value);
    return vcombine_f32(temp, temp);
}
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<2, 3, 2, 3>(SIMDVectorSingle value)noexcept{
    const float32x2_t temp = vget_high_f32(value);
    return vcombine_f32(temp, temp);
}
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<1, 0, 1, 0>(SIMDVectorSingle value)noexcept{
    const float32x2_t temp = vrev64_f32(vget_low_f32(value));
    return vcombine_f32(temp, temp);
}
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<3, 2, 3, 2>(SIMDVectorSingle value)noexcept{
    const float32x2_t temp = vrev64_f32(vget_high_f32(value));
    return vcombine_f32(temp, temp);
}
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<0, 1, 3, 2>(SIMDVectorSingle value)noexcept{ return vcombine_f32(vget_low_f32(value), vrev64_f32(vget_high_f32(value))); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<1, 0, 2, 3>(SIMDVectorSingle value)noexcept{ return vcombine_f32(vrev64_f32(vget_low_f32(value)), vget_high_f32(value)); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<2, 3, 1, 0>(SIMDVectorSingle value)noexcept{ return vcombine_f32(vget_high_f32(value), vrev64_f32(vget_low_f32(value))); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<3, 2, 0, 1>(SIMDVectorSingle value)noexcept{ return vcombine_f32(vrev64_f32(vget_high_f32(value)), vget_low_f32(value)); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<3, 2, 1, 0>(SIMDVectorSingle value)noexcept{ return vcombine_f32(vrev64_f32(vget_high_f32(value)), vrev64_f32(vget_low_f32(value))); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<0, 0, 2, 2>(SIMDVectorSingle value)noexcept{ return vtrnq_f32(value, value).val[0]; }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<1, 1, 3, 3>(SIMDVectorSingle value)noexcept{ return vtrnq_f32(value, value).val[1]; }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<0, 0, 1, 1>(SIMDVectorSingle value)noexcept{ return vzipq_f32(value, value).val[0]; }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<2, 2, 3, 3>(SIMDVectorSingle value)noexcept{ return vzipq_f32(value, value).val[1]; }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<0, 2, 0, 2>(SIMDVectorSingle value)noexcept{ return vuzpq_f32(value, value).val[0]; }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<1, 3, 1, 3>(SIMDVectorSingle value)noexcept{ return vuzpq_f32(value, value).val[1]; }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<1, 2, 3, 0>(SIMDVectorSingle value)noexcept{ return vextq_f32(value, value, 1); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<2, 3, 0, 1>(SIMDVectorSingle value)noexcept{ return vextq_f32(value, value, 2); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle<3, 0, 1, 2>(SIMDVectorSingle value)noexcept{ return vextq_f32(value, value, 3); }
#endif

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSwizzle(SIMDVectorDouble value, u32 e0, u32 e1, u32 e2, u32 e3)noexcept{
    NWB_ASSERT(e0 < 4 && e1 < 4 && e2 < 4 && e3 < 4);
#if defined(NWB_HAS_NEON64)
    const uint8x16x2_t table = { BitCast<uint8x16_t>(SIMDVectorDoubleDetail::GetLow(value)), BitCast<uint8x16_t>(SIMDVectorDoubleDetail::GetHigh(value)) };
    return SIMDVectorDoubleDetail::FromUIntPairs(BitCast<uint64x2_t>(vqtbl2q_u8(table, SIMDVectorDoubleDetail::PermuteByteIndices(e0, e1))), BitCast<uint64x2_t>(vqtbl2q_u8(table, SIMDVectorDoubleDetail::PermuteByteIndices(e2, e3))));
#elif defined(NWB_HAS_AVX2)
    const __m256i indices = _mm256_set_epi32(
        static_cast<i32>(e3 * 2u + 1u),
        static_cast<i32>(e3 * 2u),
        static_cast<i32>(e2 * 2u + 1u),
        static_cast<i32>(e2 * 2u),
        static_cast<i32>(e1 * 2u + 1u),
        static_cast<i32>(e1 * 2u),
        static_cast<i32>(e0 * 2u + 1u),
        static_cast<i32>(e0 * 2u)
    );
    return BitCast<SIMDVectorDouble>(_mm256_permutevar8x32_epi32(BitCast<__m256i>(value), indices));
#elif defined(NWB_HAS_AVX)
    const __m256d native = BitCast<__m256d>(value);
    const __m256i indices = _mm256_set_epi64x(e3 << 1u, e2 << 1u, e1 << 1u, e0 << 1u);
    const __m256d samePairs = _mm256_permutevar_pd(native, indices);
    const __m256d oppositePairs = _mm256_permutevar_pd(_mm256_permute2f128_pd(native, native, 0x01), indices);
    const __m256d pairControl = _mm256_castsi256_pd(_mm256_set_epi64x(
        BitCast<i64>(static_cast<u64>((e3 & 2u) == 0u) << s_F64SignBitShift),
        BitCast<i64>(static_cast<u64>((e2 & 2u) == 0u) << s_F64SignBitShift),
        BitCast<i64>(static_cast<u64>((e1 & 2u) != 0u) << s_F64SignBitShift),
        BitCast<i64>(static_cast<u64>((e0 & 2u) != 0u) << s_F64SignBitShift)
    ));
    return BitCast<SIMDVectorDouble>(_mm256_blendv_pd(samePairs, oppositePairs, pairControl));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(SIMDVectorDoubleDetail::PermutePair(value, value, e0, e1), SIMDVectorDoubleDetail::PermutePair(value, value, e2, e3));
#else
    return VectorDoubleSetInt(BitCast<u64>(value[e0]), BitCast<u64>(value[e1]), BitCast<u64>(value[e2]), BitCast<u64>(value[e3]));
#endif
}

template<u32 E0, u32 E1, u32 E2, u32 E3>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSwizzle(SIMDVectorDouble value)noexcept{
    static_assert(E0 < 4u && E1 < 4u && E2 < 4u && E3 < 4u);
#if defined(NWB_HAS_AVX2)
    return BitCast<SIMDVectorDouble>(_mm256_permute4x64_pd(BitCast<__m256d>(value), _MM_SHUFFLE(E3, E2, E1, E0)));
#elif defined(NWB_HAS_NEON64) || defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(SIMDVectorDoubleDetail::PermutePair<E0, E1>(value, value), SIMDVectorDoubleDetail::PermutePair<E2, E3>(value, value));
#else
    return __builtin_shufflevector(value, value, E0, E1, E2, E3);
#endif
}

template<>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSwizzle<0, 1, 2, 3>(SIMDVectorDouble value)noexcept{ return value; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute(SIMDVectorSingle v0, SIMDVectorSingle v1, u32 x, u32 y, u32 z, u32 w)noexcept{
    NWB_ASSERT(x < 8 && y < 8 && z < 8 && w < 8);
#if defined(NWB_HAS_NEON64)
    const uint8x16x2_t table = { vreinterpretq_u8_f32(v0), vreinterpretq_u8_f32(v1) };
    return vreinterpretq_f32_u8(vqtbl2q_u8(table, SIMDVectorDetail::PermuteByteIndices(x, y, z, w)));
#elif defined(NWB_HAS_NEON)
    static const u32 s_controlElement[8] = { 0x03020100u, 0x07060504u, 0x0B0A0908u, 0x0F0E0D0Cu, 0x13121110u, 0x17161514u, 0x1B1A1918u, 0x1F1E1D1Cu, };

    uint8x8x4_t table{};
    table.val[0] = vreinterpret_u8_f32(vget_low_f32(v0));
    table.val[1] = vreinterpret_u8_f32(vget_high_f32(v0));
    table.val[2] = vreinterpret_u8_f32(vget_low_f32(v1));
    table.val[3] = vreinterpret_u8_f32(vget_high_f32(v1));

    uint32x2_t idx = vcreate_u32(static_cast<u64>(s_controlElement[x]) | (static_cast<u64>(s_controlElement[y]) << s_F32BitCount));
    const uint8x8_t lo = vtbl4_u8(table, vreinterpret_u8_u32(idx));

    idx = vcreate_u32(static_cast<u64>(s_controlElement[z]) | (static_cast<u64>(s_controlElement[w]) << s_F32BitCount));
    const uint8x8_t hi = vtbl4_u8(table, vreinterpret_u8_u32(idx));
    return vcombine_f32(vreinterpret_f32_u8(lo), vreinterpret_f32_u8(hi));
#elif defined(NWB_HAS_AVX)
    const __m128i controlVector = _mm_set_epi32(static_cast<i32>(w), static_cast<i32>(z), static_cast<i32>(y), static_cast<i32>(x));
    const __m128i select = _mm_cmpgt_epi32(controlVector, _mm_set1_epi32(3));
    const __m128i swizzle = _mm_and_si128(controlVector, _mm_set1_epi32(3));
    const SIMDVectorSingle a = _mm_permutevar_ps(v0, swizzle);
    const SIMDVectorSingle b = _mm_permutevar_ps(v1, swizzle);
    return _mm_or_ps(_mm_andnot_ps(_mm_castsi128_ps(select), a), _mm_and_ps(_mm_castsi128_ps(select), b));
#elif defined(NWB_HAS_SSE2)
    const __m128i control = _mm_set_epi32(static_cast<i32>(w), static_cast<i32>(z), static_cast<i32>(y), static_cast<i32>(x));
    const __m128i swizzle = _mm_and_si128(control, _mm_set1_epi32(3));
    const __m128 select = _mm_castsi128_ps(_mm_cmpgt_epi32(control, _mm_set1_epi32(3)));
    const __m128 a = SIMDVectorDetail::SwizzleLanes(v0, swizzle);
    const __m128 b = SIMDVectorDetail::SwizzleLanes(v1, swizzle);
    return _mm_or_ps(_mm_andnot_ps(select, a), _mm_and_ps(select, b));
#else
    return VectorSetInt(x < 4u ? v0.u[x] : v1.u[x - 4u], y < 4u ? v0.u[y] : v1.u[y - 4u], z < 4u ? v0.u[z] : v1.u[z - 4u], w < 4u ? v0.u[w] : v1.u[w - 4u]);
#endif
}

template<u32 X, u32 Y, u32 Z, u32 W>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    static_assert(X < 8 && Y < 8 && Z < 8 && W < 8);
#if defined(NWB_HAS_SCALAR) || defined(NWB_HAS_NEON)
    return VectorPermute(v0, v1, X, Y, Z, W);
#else
    constexpr int s_Shuffle = _MM_SHUFFLE(W & 3u, Z & 3u, Y & 3u, X & 3u);
    constexpr int s_Blend = (X >= 4u ? 0x1 : 0x0) | (Y >= 4u ? 0x2 : 0x0) | (Z >= 4u ? 0x4 : 0x0) | (W >= 4u ? 0x8 : 0x0);
#if defined(NWB_HAS_AVX)
    if constexpr(s_Blend == 0x0)
        return _mm_permute_ps(v0, s_Shuffle);
    else if constexpr(s_Blend == 0xF)
        return _mm_permute_ps(v1, s_Shuffle);
    else if constexpr(s_Blend == 0xC)
        return _mm_shuffle_ps(v0, v1, s_Shuffle);
    else if constexpr(s_Blend == 0x3)
        return _mm_shuffle_ps(v1, v0, s_Shuffle);
    else{
        const SIMDVectorSingle a = _mm_permute_ps(v0, s_Shuffle);
        const SIMDVectorSingle b = _mm_permute_ps(v1, s_Shuffle);
        return _mm_blend_ps(a, b, s_Blend);
    }
#else
    if constexpr(s_Blend == 0x0)
        return _mm_shuffle_ps(v0, v0, s_Shuffle);
    else if constexpr(s_Blend == 0xF)
        return _mm_shuffle_ps(v1, v1, s_Shuffle);
    else if constexpr(s_Blend == 0xC)
        return _mm_shuffle_ps(v0, v1, s_Shuffle);
    else if constexpr(s_Blend == 0x3)
        return _mm_shuffle_ps(v1, v0, s_Shuffle);
    else{
        const SIMDVectorSingle a = _mm_shuffle_ps(v0, v0, s_Shuffle);
        const SIMDVectorSingle b = _mm_shuffle_ps(v1, v1, s_Shuffle);
#if defined(NWB_HAS_SSE4)
        return _mm_blend_ps(a, b, s_Blend);
#else
        const __m128 select = _mm_castsi128_ps(_mm_set_epi32(W >= 4u ? -1 : 0, Z >= 4u ? -1 : 0, Y >= 4u ? -1 : 0, X >= 4u ? -1 : 0));
        return _mm_or_ps(_mm_andnot_ps(select, a), _mm_and_ps(select, b));
#endif
    }
#endif
#endif
}

template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 1, 2, 3>(SIMDVectorSingle v0, SIMDVectorSingle)noexcept{ return v0; }

template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<4, 5, 6, 7>(SIMDVectorSingle, SIMDVectorSingle v1)noexcept{ return v1; }

#if defined(NWB_HAS_SSE4)
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 1, 4, 5>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_movelh_ps(v0, v1); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<6, 7, 2, 3>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_movehl_ps(v0, v1); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 4, 1, 5>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_unpacklo_ps(v0, v1); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<2, 6, 3, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_unpackhi_ps(v0, v1); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<2, 3, 6, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_castpd_ps(_mm_unpackhi_pd(_mm_castps_pd(v0), _mm_castps_pd(v1))); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<4, 1, 2, 3>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0x1); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 5, 2, 3>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0x2); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<4, 5, 2, 3>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0x3); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 1, 6, 3>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0x4); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<4, 1, 6, 3>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0x5); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 5, 6, 3>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0x6); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<4, 5, 6, 3>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0x7); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 1, 2, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0x8); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<4, 1, 2, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0x9); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 5, 2, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0xA); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<4, 5, 2, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0xB); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 1, 6, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0xC); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<4, 1, 6, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0xD); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 5, 6, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return _mm_blend_ps(v0, v1, 0xE); }
#endif

#if defined(NWB_HAS_NEON)
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 1, 4, 5>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vget_low_f32(v0), vget_low_f32(v1)); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<1, 0, 4, 5>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vrev64_f32(vget_low_f32(v0)), vget_low_f32(v1)); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 1, 5, 4>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vget_low_f32(v0), vrev64_f32(vget_low_f32(v1))); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<1, 0, 5, 4>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vrev64_f32(vget_low_f32(v0)), vrev64_f32(vget_low_f32(v1))); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<2, 3, 6, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vget_high_f32(v0), vget_high_f32(v1)); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<3, 2, 6, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vrev64_f32(vget_high_f32(v0)), vget_high_f32(v1)); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<2, 3, 7, 6>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vget_high_f32(v0), vrev64_f32(vget_high_f32(v1))); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<3, 2, 7, 6>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vrev64_f32(vget_high_f32(v0)), vrev64_f32(vget_high_f32(v1))); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 1, 6, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vget_low_f32(v0), vget_high_f32(v1)); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<1, 0, 6, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vrev64_f32(vget_low_f32(v0)), vget_high_f32(v1)); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 1, 7, 6>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vget_low_f32(v0), vrev64_f32(vget_high_f32(v1))); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<1, 0, 7, 6>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vrev64_f32(vget_low_f32(v0)), vrev64_f32(vget_high_f32(v1))); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<3, 2, 4, 5>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vrev64_f32(vget_high_f32(v0)), vget_low_f32(v1)); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<2, 3, 5, 4>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vget_high_f32(v0), vrev64_f32(vget_low_f32(v1))); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<3, 2, 5, 4>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vcombine_f32(vrev64_f32(vget_high_f32(v0)), vrev64_f32(vget_low_f32(v1))); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 4, 2, 6>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vtrnq_f32(v0, v1).val[0]; }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<1, 5, 3, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vtrnq_f32(v0, v1).val[1]; }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 4, 1, 5>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vzipq_f32(v0, v1).val[0]; }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<2, 6, 3, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vzipq_f32(v0, v1).val[1]; }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<0, 2, 4, 6>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vuzpq_f32(v0, v1).val[0]; }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<1, 3, 5, 7>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vuzpq_f32(v0, v1).val[1]; }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<1, 2, 3, 4>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vextq_f32(v0, v1, 1); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<2, 3, 4, 5>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vextq_f32(v0, v1, 2); }
template<>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorPermute<3, 4, 5, 6>(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return vextq_f32(v0, v1, 3); }
#endif

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoublePermute(SIMDVectorDouble v0, SIMDVectorDouble v1, u32 x, u32 y, u32 z, u32 w)noexcept{
    NWB_ASSERT(x < 8 && y < 8 && z < 8 && w < 8);
#if defined(NWB_HAS_NEON64)
    const uint8x16x4_t table = {
        BitCast<uint8x16_t>(SIMDVectorDoubleDetail::GetLow(v0)), BitCast<uint8x16_t>(SIMDVectorDoubleDetail::GetHigh(v0)),
        BitCast<uint8x16_t>(SIMDVectorDoubleDetail::GetLow(v1)), BitCast<uint8x16_t>(SIMDVectorDoubleDetail::GetHigh(v1))
    };
    return SIMDVectorDoubleDetail::FromUIntPairs(BitCast<uint64x2_t>(vqtbl4q_u8(table, SIMDVectorDoubleDetail::PermuteByteIndices(x, y))), BitCast<uint64x2_t>(vqtbl4q_u8(table, SIMDVectorDoubleDetail::PermuteByteIndices(z, w))));
#elif defined(NWB_HAS_AVX)
    const __m256d first = BitCast<__m256d>(VectorDoubleSwizzle(v0, x & 3u, y & 3u, z & 3u, w & 3u));
    const __m256d second = BitCast<__m256d>(VectorDoubleSwizzle(v1, x & 3u, y & 3u, z & 3u, w & 3u));
    const __m256d sourceControl = _mm256_castsi256_pd(_mm256_set_epi64x(
        BitCast<i64>(static_cast<u64>((w & 4u) != 0u) << s_F64SignBitShift),
        BitCast<i64>(static_cast<u64>((z & 4u) != 0u) << s_F64SignBitShift),
        BitCast<i64>(static_cast<u64>((y & 4u) != 0u) << s_F64SignBitShift),
        BitCast<i64>(static_cast<u64>((x & 4u) != 0u) << s_F64SignBitShift)
    ));
    return BitCast<SIMDVectorDouble>(_mm256_blendv_pd(first, second, sourceControl));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(SIMDVectorDoubleDetail::PermutePair(v0, v1, x, y), SIMDVectorDoubleDetail::PermutePair(v0, v1, z, w));
#else
    return VectorDoubleSetInt(x < 4u ? BitCast<u64>(v0[x]) : BitCast<u64>(v1[x - 4u]), y < 4u ? BitCast<u64>(v0[y]) : BitCast<u64>(v1[y - 4u]), z < 4u ? BitCast<u64>(v0[z]) : BitCast<u64>(v1[z - 4u]), w < 4u ? BitCast<u64>(v0[w]) : BitCast<u64>(v1[w - 4u]));
#endif
}

template<u32 X, u32 Y, u32 Z, u32 W>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoublePermute(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    static_assert(X < 8u && Y < 8u && Z < 8u && W < 8u);
#if defined(NWB_HAS_AVX2)
    const __m256d first = _mm256_permute4x64_pd(BitCast<__m256d>(v0), _MM_SHUFFLE(W & 3u, Z & 3u, Y & 3u, X & 3u));
    const __m256d second = _mm256_permute4x64_pd(BitCast<__m256d>(v1), _MM_SHUFFLE(W & 3u, Z & 3u, Y & 3u, X & 3u));
    return BitCast<SIMDVectorDouble>(_mm256_blend_pd(first, second, ((X & 4u) >> 2u) | ((Y & 4u) >> 1u) | (Z & 4u) | ((W & 4u) << 1u)));
#elif defined(NWB_HAS_NEON64) || defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(SIMDVectorDoubleDetail::PermutePair<X, Y>(v0, v1), SIMDVectorDoubleDetail::PermutePair<Z, W>(v0, v1));
#else
    return __builtin_shufflevector(v0, v1, X, Y, Z, W);
#endif
}

template<>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoublePermute<0, 1, 2, 3>(SIMDVectorDouble v0, SIMDVectorDouble)noexcept{ return v0; }

template<>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoublePermute<4, 5, 6, 7>(SIMDVectorDouble, SIMDVectorDouble v1)noexcept{ return v1; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSelectControl(u32 selectX, u32 selectY, u32 selectZ, u32 selectW)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(selectX ? s_SELECT_1 : 0u, selectY ? s_SELECT_1 : 0u, selectZ ? s_SELECT_1 : 0u, selectW ? s_SELECT_1 : 0u);
#elif defined(NWB_HAS_NEON)
    const int32x2_t lo = vcreate_s32(static_cast<u64>(selectX) | (static_cast<u64>(selectY) << s_F32BitCount));
    const int32x2_t hi = vcreate_s32(static_cast<u64>(selectZ) | (static_cast<u64>(selectW) << s_F32BitCount));
    return vreinterpretq_f32_u32(vmvnq_u32(vceqq_s32(vcombine_s32(lo, hi), vdupq_n_s32(0))));
#else
    __m128i value = _mm_set_epi32(static_cast<i32>(selectW), static_cast<i32>(selectZ), static_cast<i32>(selectY), static_cast<i32>(selectX));
    const __m128i isZero = _mm_cmpeq_epi32(value, _mm_setzero_si128());
    return _mm_castsi128_ps(_mm_xor_si128(isZero, _mm_set1_epi32(-1)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSelectControl(u32 selectX, u32 selectY, u32 selectZ, u32 selectW)noexcept{
    return VectorDoubleSetInt(selectX ? s_DoubleSELECT_1 : 0u, selectY ? s_DoubleSELECT_1 : 0u, selectZ ? s_DoubleSELECT_1 : 0u, selectW ? s_DoubleSELECT_1 : 0u);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorAndInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) & VectorGetIntX(v1), VectorGetIntY(v0) & VectorGetIntY(v1), VectorGetIntZ(v0) & VectorGetIntZ(v1), VectorGetIntW(v0) & VectorGetIntW(v1));
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_and_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAndInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(vandq_u64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)), vandq_u64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_and_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_and_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), _mm_and_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#else
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits & v1Bits);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorOrInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) | VectorGetIntX(v1), VectorGetIntY(v0) | VectorGetIntY(v1), VectorGetIntZ(v0) | VectorGetIntZ(v1), VectorGetIntW(v0) | VectorGetIntW(v1));
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_or_si128(_mm_castps_si128(v0), _mm_castps_si128(v1)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleOrInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(vorrq_u64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)), vorrq_u64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_or_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_or_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), _mm_or_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#else
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits | v1Bits);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorXorInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) ^ VectorGetIntX(v1), VectorGetIntY(v0) ^ VectorGetIntY(v1), VectorGetIntZ(v0) ^ VectorGetIntZ(v1), VectorGetIntW(v0) ^ VectorGetIntW(v1));
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_xor_si128(_mm_castps_si128(v0), _mm_castps_si128(v1)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleXorInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(veorq_u64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)), veorq_u64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_xor_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_xor_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), _mm_xor_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#else
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits ^ v1Bits);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorAndCInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(v0.u[0] & ~v1.u[0], v0.u[1] & ~v1.u[1], v0.u[2] & ~v1.u[2], v0.u[3] & ~v1.u[3]);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_andnot_si128(_mm_castps_si128(v1), _mm_castps_si128(v0)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAndCInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(vbicq_u64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)), vbicq_u64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_andnot_pd(BitCast<__m256d>(v1), BitCast<__m256d>(v0)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_andnot_pd(SIMDVectorDoubleDetail::GetLow(v1), SIMDVectorDoubleDetail::GetLow(v0)), _mm_andnot_pd(SIMDVectorDoubleDetail::GetHigh(v1), SIMDVectorDoubleDetail::GetHigh(v0)));
#else
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits & ~v1Bits);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorNorInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(~(v0.u[0] | v1.u[0]), ~(v0.u[1] | v1.u[1]), ~(v0.u[2] | v1.u[2]), ~(v0.u[3] | v1.u[3]));
#elif defined(NWB_HAS_NEON)
    const uint32x4_t value = vorrq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1));
    return vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(s_SIMDNegOneMask), value));
#else
    const __m128i value = _mm_or_si128(_mm_castps_si128(v0), _mm_castps_si128(v1));
    return _mm_castsi128_ps(_mm_andnot_si128(value, s_SIMDNegOneMask));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNorInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return VectorDoubleXorInt(VectorDoubleOrInt(v0, v1), VectorDoubleTrueInt());
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSelect(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle control)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorOrInt(VectorAndCInt(v0, control), VectorAndInt(v1, control));
#elif defined(NWB_HAS_NEON)
    return vbslq_f32(vreinterpretq_u32_f32(control), v1, v0);
#else
    return _mm_or_ps(_mm_andnot_ps(control, v0), _mm_and_ps(v1, control));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSelect(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble control)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(
        vbslq_u64(SIMDVectorDoubleDetail::GetLowBits(control), SIMDVectorDoubleDetail::GetLowBits(v1), SIMDVectorDoubleDetail::GetLowBits(v0)),
        vbslq_u64(SIMDVectorDoubleDetail::GetHighBits(control), SIMDVectorDoubleDetail::GetHighBits(v1), SIMDVectorDoubleDetail::GetHighBits(v0))
    );
#elif defined(NWB_HAS_AVX)
    const __m256d mask = BitCast<__m256d>(control);
    return BitCast<SIMDVectorDouble>(_mm256_or_pd(_mm256_andnot_pd(mask, BitCast<__m256d>(v0)), _mm256_and_pd(mask, BitCast<__m256d>(v1))));
#elif defined(NWB_HAS_SSE2)
    const __m128d low = SIMDVectorDoubleDetail::GetLow(control);
    const __m128d high = SIMDVectorDoubleDetail::GetHigh(control);
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_or_pd(_mm_andnot_pd(low, SIMDVectorDoubleDetail::GetLow(v0)), _mm_and_pd(low, SIMDVectorDoubleDetail::GetLow(v1))),
        _mm_or_pd(_mm_andnot_pd(high, SIMDVectorDoubleDetail::GetHigh(v0)), _mm_and_pd(high, SIMDVectorDoubleDetail::GetHigh(v1)))
    );
#else
    return VectorDoubleOrInt(VectorDoubleAndCInt(v0, control), VectorDoubleAndInt(v1, control));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMergeXY(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(v0.u[0], v1.u[0], v0.u[1], v1.u[1]);
#elif defined(NWB_HAS_NEON)
    return vzipq_f32(v0, v1).val[0];
#else
    return _mm_unpacklo_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMergeXY(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return VectorDoublePermute<0, 4, 1, 5>(v0, v1);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMergeZW(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(v0.u[2], v1.u[2], v0.u[3], v1.u[3]);
#elif defined(NWB_HAS_NEON)
    return vzipq_f32(v0, v1).val[1];
#else
    return _mm_unpackhi_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMergeZW(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return VectorDoublePermute<2, 6, 3, 7>(v0, v1);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorShiftLeft(SIMDVectorSingle v0, SIMDVectorSingle v1, u32 elements)noexcept{
    NWB_ASSERT(elements < 4);
    switch(elements){
    case 0: return VectorPermute<0, 1, 2, 3>(v0, v1);
    case 1: return VectorPermute<1, 2, 3, 4>(v0, v1);
    case 2: return VectorPermute<2, 3, 4, 5>(v0, v1);
    default: return VectorPermute<3, 4, 5, 6>(v0, v1);
    }
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleShiftLeft(SIMDVectorDouble v0, SIMDVectorDouble v1, u32 elements)noexcept{
    NWB_ASSERT(elements < 4);
    switch(elements){
    case 0: return VectorDoublePermute<0, 1, 2, 3>(v0, v1);
    case 1: return VectorDoublePermute<1, 2, 3, 4>(v0, v1);
    case 2: return VectorDoublePermute<2, 3, 4, 5>(v0, v1);
    default: return VectorDoublePermute<3, 4, 5, 6>(v0, v1);
    }
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorRotateLeft(SIMDVectorSingle value, u32 elements)noexcept{
    NWB_ASSERT(elements < 4);
    switch(elements){
    case 0: return value;
    case 1: return VectorSwizzle<1, 2, 3, 0>(value);
    case 2: return VectorSwizzle<2, 3, 0, 1>(value);
    default: return VectorSwizzle<3, 0, 1, 2>(value);
    }
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleRotateLeft(SIMDVectorDouble value, u32 elements)noexcept{
    NWB_ASSERT(elements < 4);
    switch(elements){
    case 0: return value;
    case 1: return VectorDoubleSwizzle<1, 2, 3, 0>(value);
    case 2: return VectorDoubleSwizzle<2, 3, 0, 1>(value);
    default: return VectorDoubleSwizzle<3, 0, 1, 2>(value);
    }
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorRotateRight(SIMDVectorSingle value, u32 elements)noexcept{
    NWB_ASSERT(elements < 4);
    switch(elements){
    case 0: return value;
    case 1: return VectorSwizzle<3, 0, 1, 2>(value);
    case 2: return VectorSwizzle<2, 3, 0, 1>(value);
    default: return VectorSwizzle<1, 2, 3, 0>(value);
    }
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleRotateRight(SIMDVectorDouble value, u32 elements)noexcept{
    NWB_ASSERT(elements < 4);
    switch(elements){
    case 0: return value;
    case 1: return VectorDoubleSwizzle<3, 0, 1, 2>(value);
    case 2: return VectorDoubleSwizzle<2, 3, 0, 1>(value);
    default: return VectorDoubleSwizzle<1, 2, 3, 0>(value);
    }
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorInsert(SIMDVectorSingle vd, SIMDVectorSingle vs, u32 vsLeftRotateElements, u32 select0, u32 select1, u32 select2, u32 select3)noexcept{
    const SIMDVectorSingle control = VectorSelectControl(select0 & 1u, select1 & 1u, select2 & 1u, select3 & 1u);
    return VectorSelect(vd, VectorRotateLeft(vs, vsLeftRotateElements), control);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleInsert(SIMDVectorDouble vd, SIMDVectorDouble vs, u32 vsLeftRotateElements, u32 select0, u32 select1, u32 select2, u32 select3)noexcept{
    const SIMDVectorDouble control = VectorDoubleSelectControl(select0 & 1u, select1 & 1u, select2 & 1u, select3 & 1u);
    return VectorDoubleSelect(vd, VectorDoubleRotateLeft(vs, vsLeftRotateElements), control);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

