// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_select.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSwizzle(SIMDVectorSingle value, u32 e0, u32 e1, u32 e2, u32 e3)noexcept{
    NWB_ASSERT(e0 < 4 && e1 < 4 && e2 < 4 && e3 < 4);
#if defined(NWB_HAS_NEON64)
    return vreinterpretq_f32_u8(vqtbl1q_u8(vreinterpretq_u8_f32(value), SIMDVectorDetail::PermuteByteIndices(e0, e1, e2, e3)));
#elif defined(NWB_HAS_NEON)
    static const u32 s_ControlElement[4] = { 0x03020100u, 0x07060504u, 0x0B0A0908u, 0x0F0E0D0Cu, };

    uint8x8x2_t table{};
    table.val[0] = vreinterpret_u8_f32(vget_low_f32(value));
    table.val[1] = vreinterpret_u8_f32(vget_high_f32(value));

    uint32x2_t idx = vcreate_u32(static_cast<u64>(s_ControlElement[e0]) | (static_cast<u64>(s_ControlElement[e1]) << s_F32BitCount));
    const uint8x8_t lo = vtbl2_u8(table, vreinterpret_u8_u32(idx));

    idx = vcreate_u32(static_cast<u64>(s_ControlElement[e2]) | (static_cast<u64>(s_ControlElement[e3]) << s_F32BitCount));
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
    static const u32 s_ControlElement[8] = { 0x03020100u, 0x07060504u, 0x0B0A0908u, 0x0F0E0D0Cu, 0x13121110u, 0x17161514u, 0x1B1A1918u, 0x1F1E1D1Cu, };

    uint8x8x4_t table{};
    table.val[0] = vreinterpret_u8_f32(vget_low_f32(v0));
    table.val[1] = vreinterpret_u8_f32(vget_high_f32(v0));
    table.val[2] = vreinterpret_u8_f32(vget_low_f32(v1));
    table.val[3] = vreinterpret_u8_f32(vget_high_f32(v1));

    uint32x2_t idx = vcreate_u32(static_cast<u64>(s_ControlElement[x]) | (static_cast<u64>(s_ControlElement[y]) << s_F32BitCount));
    const uint8x8_t lo = vtbl4_u8(table, vreinterpret_u8_u32(idx));

    idx = vcreate_u32(static_cast<u64>(s_ControlElement[z]) | (static_cast<u64>(s_ControlElement[w]) << s_F32BitCount));
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

