// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_matrix.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE u32 SIMDCALL VectorMoveMask(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return ((value.u[0] >> 31) & 1u) | (((value.u[1] >> 31) & 1u) << 1) | (((value.u[2] >> 31) & 1u) << 2) | (((value.u[3] >> 31) & 1u) << 3);
#elif defined(NWB_HAS_NEON)
    const uint32x4_t bits = vshrq_n_u32(vreinterpretq_u32_f32(value), 31);
    return vgetq_lane_u32(bits, 0) | (vgetq_lane_u32(bits, 1) << 1) | (vgetq_lane_u32(bits, 2) << 2) | (vgetq_lane_u32(bits, 3) << 3);
#else
    return static_cast<u32>(_mm_movemask_ps(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorZero()noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(0.0f, 0.0f, 0.0f, 0.0f);
#elif defined(NWB_HAS_NEON)
    return vdupq_n_f32(0.0f);
#else
    return _mm_setzero_ps();
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSet(f32 x, f32 y, f32 z, f32 w)noexcept{
    return SIMDConvertDetail::MakeF32(x, y, z, w);
}

NWB_INLINE SIMDVector SIMDCALL VectorSetInt(u32 x, u32 y, u32 z, u32 w)noexcept{
    return SIMDConvertDetail::MakeU32(x, y, z, w);
}

NWB_INLINE SIMDVector SIMDCALL VectorReplicate(f32 value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(value, value, value, value);
#elif defined(NWB_HAS_NEON)
    return vdupq_n_f32(value);
#else
    return _mm_set1_ps(value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorReplicatePtr(const f32 value)noexcept{
    return VectorReplicate(value);
}

NWB_INLINE SIMDVector SIMDCALL VectorReplicateInt(u32 value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(value, value, value, value);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vdupq_n_u32(value));
#else
    return _mm_castsi128_ps(_mm_set1_epi32(static_cast<i32>(value)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorReplicateIntPtr(const u32 value)noexcept{
    return VectorReplicateInt(value);
}

NWB_INLINE SIMDVector SIMDCALL VectorTrueInt()noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_s32(vdupq_n_s32(-1));
#else
    return _mm_castsi128_ps(_mm_set1_epi32(-1));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorFalseInt()noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(0u, 0u, 0u, 0u);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vdupq_n_u32(0));
#else
    return _mm_setzero_ps();
#endif
}

NWB_INLINE f32 SIMDCALL VectorGetX(SIMDVector value)noexcept{ return SIMDVectorDetail::GetLane<0u>(value); }
NWB_INLINE f32 SIMDCALL VectorGetY(SIMDVector value)noexcept{ return SIMDVectorDetail::GetLane<1u>(value); }
NWB_INLINE f32 SIMDCALL VectorGetZ(SIMDVector value)noexcept{ return SIMDVectorDetail::GetLane<2u>(value); }
NWB_INLINE f32 SIMDCALL VectorGetW(SIMDVector value)noexcept{ return SIMDVectorDetail::GetLane<3u>(value); }

NWB_INLINE f32 SIMDCALL VectorGetByIndex(SIMDVector value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorGetX(value);
    case 1u: return VectorGetY(value);
    case 2u: return VectorGetZ(value);
    default: return VectorGetW(value);
    }
}

NWB_INLINE void SIMDCALL VectorGetByIndexPtr(f32& out, SIMDVector value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    out = VectorGetByIndex(value, index);
}

NWB_INLINE void SIMDCALL VectorGetXPtr(f32& out, SIMDVector value)noexcept{ out = VectorGetX(value); }
NWB_INLINE void SIMDCALL VectorGetYPtr(f32& out, SIMDVector value)noexcept{ out = VectorGetY(value); }
NWB_INLINE void SIMDCALL VectorGetZPtr(f32& out, SIMDVector value)noexcept{ out = VectorGetZ(value); }
NWB_INLINE void SIMDCALL VectorGetWPtr(f32& out, SIMDVector value)noexcept{ out = VectorGetW(value); }

NWB_INLINE u32 SIMDCALL VectorGetIntX(SIMDVector value)noexcept{ return SIMDVectorDetail::GetIntLane<0u>(value); }
NWB_INLINE u32 SIMDCALL VectorGetIntY(SIMDVector value)noexcept{ return SIMDVectorDetail::GetIntLane<1u>(value); }
NWB_INLINE u32 SIMDCALL VectorGetIntZ(SIMDVector value)noexcept{ return SIMDVectorDetail::GetIntLane<2u>(value); }
NWB_INLINE u32 SIMDCALL VectorGetIntW(SIMDVector value)noexcept{ return SIMDVectorDetail::GetIntLane<3u>(value); }

NWB_INLINE u32 SIMDCALL VectorGetIntByIndex(SIMDVector value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorGetIntX(value);
    case 1u: return VectorGetIntY(value);
    case 2u: return VectorGetIntZ(value);
    default: return VectorGetIntW(value);
    }
}

NWB_INLINE void SIMDCALL VectorGetIntByIndexPtr(u32& out, SIMDVector value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    out = VectorGetIntByIndex(value, index);
}

NWB_INLINE void SIMDCALL VectorGetIntXPtr(u32& out, SIMDVector value)noexcept{ out = VectorGetIntX(value); }
NWB_INLINE void SIMDCALL VectorGetIntYPtr(u32& out, SIMDVector value)noexcept{ out = VectorGetIntY(value); }
NWB_INLINE void SIMDCALL VectorGetIntZPtr(u32& out, SIMDVector value)noexcept{ out = VectorGetIntZ(value); }
NWB_INLINE void SIMDCALL VectorGetIntWPtr(u32& out, SIMDVector value)noexcept{ out = VectorGetIntW(value); }

NWB_INLINE SIMDVector SIMDCALL VectorSetX(SIMDVector value, f32 x)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.f[0] = x;
    return value;
#elif defined(NWB_HAS_NEON)
    return vsetq_lane_f32(x, value, 0);
#else
    return _mm_move_ss(value, _mm_set_ss(x));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetY(SIMDVector value, f32 y)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.f[1] = y;
    return value;
#elif defined(NWB_HAS_NEON)
    return vsetq_lane_f32(y, value, 1);
#else
    return _mm_insert_ps(value, _mm_set_ss(y), 0x10);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetZ(SIMDVector value, f32 z)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.f[2] = z;
    return value;
#elif defined(NWB_HAS_NEON)
    return vsetq_lane_f32(z, value, 2);
#else
    return _mm_insert_ps(value, _mm_set_ss(z), 0x20);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetW(SIMDVector value, f32 w)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.f[3] = w;
    return value;
#elif defined(NWB_HAS_NEON)
    return vsetq_lane_f32(w, value, 3);
#else
    return _mm_insert_ps(value, _mm_set_ss(w), 0x30);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetByIndex(SIMDVector value, f32 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorSetX(value, component);
    case 1u: return VectorSetY(value, component);
    case 2u: return VectorSetZ(value, component);
    default: return VectorSetW(value, component);
    }
}

NWB_INLINE SIMDVector SIMDCALL VectorSetByIndexPtr(SIMDVector value, const f32 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    return VectorSetByIndex(value, component, index);
}

NWB_INLINE SIMDVector SIMDCALL VectorSetXPtr(SIMDVector value, const f32 x)noexcept{
    return VectorSetX(value, x);
}

NWB_INLINE SIMDVector SIMDCALL VectorSetYPtr(SIMDVector value, const f32 y)noexcept{
    return VectorSetY(value, y);
}

NWB_INLINE SIMDVector SIMDCALL VectorSetZPtr(SIMDVector value, const f32 z)noexcept{
    return VectorSetZ(value, z);
}

NWB_INLINE SIMDVector SIMDCALL VectorSetWPtr(SIMDVector value, const f32 w)noexcept{
    return VectorSetW(value, w);
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntX(SIMDVector value, u32 x)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.u[0] = x;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(x, vreinterpretq_u32_f32(value), 0));
#else
    return _mm_move_ss(value, _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<i32>(x))));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntY(SIMDVector value, u32 y)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.u[1] = y;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(y, vreinterpretq_u32_f32(value), 1));
#else
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(y), 1));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntZ(SIMDVector value, u32 z)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.u[2] = z;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(z, vreinterpretq_u32_f32(value), 2));
#else
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(z), 2));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntW(SIMDVector value, u32 w)noexcept{
#if defined(NWB_HAS_SCALAR)
    value.u[3] = w;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vsetq_lane_u32(w, vreinterpretq_u32_f32(value), 3));
#else
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(w), 3));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntByIndex(SIMDVector value, u32 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorSetIntX(value, component);
    case 1u: return VectorSetIntY(value, component);
    case 2u: return VectorSetIntZ(value, component);
    default: return VectorSetIntW(value, component);
    }
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntByIndexPtr(SIMDVector value, const u32 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    return VectorSetIntByIndex(value, component, index);
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntXPtr(SIMDVector value, const u32 x)noexcept{
    return VectorSetIntX(value, x);
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntYPtr(SIMDVector value, const u32 y)noexcept{
    return VectorSetIntY(value, y);
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntZPtr(SIMDVector value, const u32 z)noexcept{
    return VectorSetIntZ(value, z);
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntWPtr(SIMDVector value, const u32 w)noexcept{
    return VectorSetIntW(value, w);
}

NWB_INLINE SIMDVector SIMDCALL VectorSplatX(SIMDVector value)noexcept{ return SIMDVectorDetail::SplatLane<0u>(value); }
NWB_INLINE SIMDVector SIMDCALL VectorSplatY(SIMDVector value)noexcept{ return SIMDVectorDetail::SplatLane<1u>(value); }
NWB_INLINE SIMDVector SIMDCALL VectorSplatZ(SIMDVector value)noexcept{ return SIMDVectorDetail::SplatLane<2u>(value); }
NWB_INLINE SIMDVector SIMDCALL VectorSplatW(SIMDVector value)noexcept{ return SIMDVectorDetail::SplatLane<3u>(value); }
NWB_INLINE SIMDVector SIMDCALL VectorSplatOne()noexcept{ return s_SIMDOne; }
NWB_INLINE SIMDVector SIMDCALL VectorSplatInfinity()noexcept{ return s_SIMDInfinity; }
NWB_INLINE SIMDVector SIMDCALL VectorSplatQNaN()noexcept{ return s_SIMDQNaN; }
NWB_INLINE SIMDVector SIMDCALL VectorSplatEpsilon()noexcept{ return s_SIMDEpsilon; }
NWB_INLINE SIMDVector SIMDCALL VectorSplatSignMask()noexcept{ return s_SIMDNegativeZero; }

NWB_INLINE SIMDVector SIMDCALL VectorSwizzle(SIMDVector value, u32 e0, u32 e1, u32 e2, u32 e3)noexcept{
    NWB_ASSERT(e0 < 4 && e1 < 4 && e2 < 4 && e3 < 4);
#if defined(NWB_HAS_NEON)
    static const u32 controlElement[4] = {
        0x03020100u,
        0x07060504u,
        0x0B0A0908u,
        0x0F0E0D0Cu,
    };

    uint8x8x2_t table{};
    table.val[0] = vreinterpret_u8_f32(vget_low_f32(value));
    table.val[1] = vreinterpret_u8_f32(vget_high_f32(value));

    uint32x2_t idx = vcreate_u32(static_cast<u64>(controlElement[e0]) | (static_cast<u64>(controlElement[e1]) << 32));
    const uint8x8_t lo = vtbl2_u8(table, vreinterpret_u8_u32(idx));

    idx = vcreate_u32(static_cast<u64>(controlElement[e2]) | (static_cast<u64>(controlElement[e3]) << 32));
    const uint8x8_t hi = vtbl2_u8(table, vreinterpret_u8_u32(idx));
    return vcombine_f32(vreinterpret_f32_u8(lo), vreinterpret_f32_u8(hi));
#elif defined(NWB_HAS_AVX2)
    const __m128i control = _mm_set_epi32(static_cast<i32>(e3), static_cast<i32>(e2), static_cast<i32>(e1), static_cast<i32>(e0));
    return _mm_permutevar_ps(value, control);
#elif defined(NWB_HAS_SSE4)
    return VectorSetInt(
        VectorGetIntByIndex(value, e0),
        VectorGetIntByIndex(value, e1),
        VectorGetIntByIndex(value, e2),
        VectorGetIntByIndex(value, e3)
    );
#else
    return VectorSetInt(value.u[e0], value.u[e1], value.u[e2], value.u[e3]);
#endif
}

template<u32 E0, u32 E1, u32 E2, u32 E3>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle(SIMDVector value)noexcept{
    static_assert(E0 < 4 && E1 < 4 && E2 < 4 && E3 < 4);
#if defined(NWB_HAS_SCALAR) || defined(NWB_HAS_NEON)
    return VectorSwizzle(value, E0, E1, E2, E3);
#elif defined(NWB_HAS_AVX2)
    return _mm_permute_ps(value, _MM_SHUFFLE(E3, E2, E1, E0));
#else
    return _mm_shuffle_ps(value, value, _MM_SHUFFLE(E3, E2, E1, E0));
#endif
}

template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<0, 1, 2, 3>(SIMDVector value)noexcept{ return value; }

#if defined(NWB_HAS_SSE4)
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<0, 1, 0, 1>(SIMDVector value)noexcept{ return _mm_movelh_ps(value, value); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<2, 3, 2, 3>(SIMDVector value)noexcept{ return _mm_movehl_ps(value, value); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<0, 0, 1, 1>(SIMDVector value)noexcept{ return _mm_unpacklo_ps(value, value); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<2, 2, 3, 3>(SIMDVector value)noexcept{ return _mm_unpackhi_ps(value, value); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<0, 0, 2, 2>(SIMDVector value)noexcept{ return _mm_moveldup_ps(value); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<1, 1, 3, 3>(SIMDVector value)noexcept{ return _mm_movehdup_ps(value); }
#endif

#if defined(__AVX2__) || defined(_M_AVX2)
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<0, 0, 0, 0>(SIMDVector value)noexcept{ return _mm_broadcastss_ps(value); }
#endif

#if defined(NWB_HAS_NEON)
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<0, 0, 0, 0>(SIMDVector value)noexcept{ return vdupq_lane_f32(vget_low_f32(value), 0); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<1, 1, 1, 1>(SIMDVector value)noexcept{ return vdupq_lane_f32(vget_low_f32(value), 1); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<2, 2, 2, 2>(SIMDVector value)noexcept{ return vdupq_lane_f32(vget_high_f32(value), 0); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<3, 3, 3, 3>(SIMDVector value)noexcept{ return vdupq_lane_f32(vget_high_f32(value), 1); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<1, 0, 3, 2>(SIMDVector value)noexcept{ return vrev64q_f32(value); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<0, 1, 0, 1>(SIMDVector value)noexcept{
    const float32x2_t temp = vget_low_f32(value);
    return vcombine_f32(temp, temp);
}
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<2, 3, 2, 3>(SIMDVector value)noexcept{
    const float32x2_t temp = vget_high_f32(value);
    return vcombine_f32(temp, temp);
}
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<1, 0, 1, 0>(SIMDVector value)noexcept{
    const float32x2_t temp = vrev64_f32(vget_low_f32(value));
    return vcombine_f32(temp, temp);
}
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<3, 2, 3, 2>(SIMDVector value)noexcept{
    const float32x2_t temp = vrev64_f32(vget_high_f32(value));
    return vcombine_f32(temp, temp);
}
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<0, 1, 3, 2>(SIMDVector value)noexcept{ return vcombine_f32(vget_low_f32(value), vrev64_f32(vget_high_f32(value))); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<1, 0, 2, 3>(SIMDVector value)noexcept{ return vcombine_f32(vrev64_f32(vget_low_f32(value)), vget_high_f32(value)); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<2, 3, 1, 0>(SIMDVector value)noexcept{ return vcombine_f32(vget_high_f32(value), vrev64_f32(vget_low_f32(value))); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<3, 2, 0, 1>(SIMDVector value)noexcept{ return vcombine_f32(vrev64_f32(vget_high_f32(value)), vget_low_f32(value)); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<3, 2, 1, 0>(SIMDVector value)noexcept{ return vcombine_f32(vrev64_f32(vget_high_f32(value)), vrev64_f32(vget_low_f32(value))); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<0, 0, 2, 2>(SIMDVector value)noexcept{ return vtrnq_f32(value, value).val[0]; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<1, 1, 3, 3>(SIMDVector value)noexcept{ return vtrnq_f32(value, value).val[1]; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<0, 0, 1, 1>(SIMDVector value)noexcept{ return vzipq_f32(value, value).val[0]; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<2, 2, 3, 3>(SIMDVector value)noexcept{ return vzipq_f32(value, value).val[1]; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<0, 2, 0, 2>(SIMDVector value)noexcept{ return vuzpq_f32(value, value).val[0]; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<1, 3, 1, 3>(SIMDVector value)noexcept{ return vuzpq_f32(value, value).val[1]; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<1, 2, 3, 0>(SIMDVector value)noexcept{ return vextq_f32(value, value, 1); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<2, 3, 0, 1>(SIMDVector value)noexcept{ return vextq_f32(value, value, 2); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorSwizzle<3, 0, 1, 2>(SIMDVector value)noexcept{ return vextq_f32(value, value, 3); }
#endif

NWB_INLINE SIMDVector SIMDCALL VectorPermute(SIMDVector v0, SIMDVector v1, u32 x, u32 y, u32 z, u32 w)noexcept{
    NWB_ASSERT(x < 8 && y < 8 && z < 8 && w < 8);
#if defined(NWB_HAS_NEON)
    static const u32 controlElement[8] = {
        0x03020100u,
        0x07060504u,
        0x0B0A0908u,
        0x0F0E0D0Cu,
        0x13121110u,
        0x17161514u,
        0x1B1A1918u,
        0x1F1E1D1Cu,
    };

    uint8x8x4_t table{};
    table.val[0] = vreinterpret_u8_f32(vget_low_f32(v0));
    table.val[1] = vreinterpret_u8_f32(vget_high_f32(v0));
    table.val[2] = vreinterpret_u8_f32(vget_low_f32(v1));
    table.val[3] = vreinterpret_u8_f32(vget_high_f32(v1));

    uint32x2_t idx = vcreate_u32(static_cast<u64>(controlElement[x]) | (static_cast<u64>(controlElement[y]) << 32));
    const uint8x8_t lo = vtbl4_u8(table, vreinterpret_u8_u32(idx));

    idx = vcreate_u32(static_cast<u64>(controlElement[z]) | (static_cast<u64>(controlElement[w]) << 32));
    const uint8x8_t hi = vtbl4_u8(table, vreinterpret_u8_u32(idx));
    return vcombine_f32(vreinterpret_f32_u8(lo), vreinterpret_f32_u8(hi));
#elif defined(NWB_HAS_AVX2)
    const __m128i controlVector = _mm_set_epi32(static_cast<i32>(w), static_cast<i32>(z), static_cast<i32>(y), static_cast<i32>(x));
    const __m128i select = _mm_cmpgt_epi32(controlVector, _mm_set1_epi32(3));
    const __m128i swizzle = _mm_and_si128(controlVector, _mm_set1_epi32(3));
    const SIMDVector a = _mm_permutevar_ps(v0, swizzle);
    const SIMDVector b = _mm_permutevar_ps(v1, swizzle);
    return _mm_or_ps(_mm_andnot_ps(_mm_castsi128_ps(select), a), _mm_and_ps(_mm_castsi128_ps(select), b));
#elif defined(NWB_HAS_SSE4)
    return VectorSetInt(
        x < 4u ? VectorGetIntByIndex(v0, x) : VectorGetIntByIndex(v1, x - 4u),
        y < 4u ? VectorGetIntByIndex(v0, y) : VectorGetIntByIndex(v1, y - 4u),
        z < 4u ? VectorGetIntByIndex(v0, z) : VectorGetIntByIndex(v1, z - 4u),
        w < 4u ? VectorGetIntByIndex(v0, w) : VectorGetIntByIndex(v1, w - 4u)
    );
#else
    return VectorSetInt(
        x < 4u ? v0.u[x] : v1.u[x - 4u],
        y < 4u ? v0.u[y] : v1.u[y - 4u],
        z < 4u ? v0.u[z] : v1.u[z - 4u],
        w < 4u ? v0.u[w] : v1.u[w - 4u]
    );
#endif
}

template<u32 X, u32 Y, u32 Z, u32 W>
NWB_INLINE SIMDVector SIMDCALL VectorPermute(SIMDVector v0, SIMDVector v1)noexcept{
    static_assert(X < 8 && Y < 8 && Z < 8 && W < 8);
#if defined(NWB_HAS_SCALAR) || defined(NWB_HAS_NEON)
    return VectorPermute(v0, v1, X, Y, Z, W);
#else
    constexpr int shuffle = _MM_SHUFFLE(W & 3u, Z & 3u, Y & 3u, X & 3u);
    constexpr int blend = (X >= 4u ? 0x1 : 0x0) | (Y >= 4u ? 0x2 : 0x0) | (Z >= 4u ? 0x4 : 0x0) | (W >= 4u ? 0x8 : 0x0);
#if defined(NWB_HAS_AVX2)
    if constexpr(blend == 0x0)
        return _mm_permute_ps(v0, shuffle);
    else if constexpr(blend == 0xF)
        return _mm_permute_ps(v1, shuffle);
    else if constexpr(blend == 0xC)
        return _mm_shuffle_ps(v0, v1, shuffle);
    else if constexpr(blend == 0x3)
        return _mm_shuffle_ps(v1, v0, shuffle);
    else{
        const SIMDVector a = _mm_permute_ps(v0, shuffle);
        const SIMDVector b = _mm_permute_ps(v1, shuffle);
        return _mm_blend_ps(a, b, blend);
    }
#else
    if constexpr(blend == 0x0)
        return _mm_shuffle_ps(v0, v0, shuffle);
    else if constexpr(blend == 0xF)
        return _mm_shuffle_ps(v1, v1, shuffle);
    else if constexpr(blend == 0xC)
        return _mm_shuffle_ps(v0, v1, shuffle);
    else if constexpr(blend == 0x3)
        return _mm_shuffle_ps(v1, v0, shuffle);
    else{
        const SIMDVector a = _mm_shuffle_ps(v0, v0, shuffle);
        const SIMDVector b = _mm_shuffle_ps(v1, v1, shuffle);
        return _mm_blend_ps(a, b, blend);
    }
#endif
#endif
}

template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 1, 2, 3>(SIMDVector v0, SIMDVector)noexcept{ return v0; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<4, 5, 6, 7>(SIMDVector, SIMDVector v1)noexcept{ return v1; }

#if defined(NWB_HAS_SSE4)
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 1, 4, 5>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_movelh_ps(v0, v1); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<6, 7, 2, 3>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_movehl_ps(v0, v1); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 4, 1, 5>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_unpacklo_ps(v0, v1); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<2, 6, 3, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_unpackhi_ps(v0, v1); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<2, 3, 6, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_castpd_ps(_mm_unpackhi_pd(_mm_castps_pd(v0), _mm_castps_pd(v1))); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<4, 1, 2, 3>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0x1); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 5, 2, 3>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0x2); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<4, 5, 2, 3>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0x3); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 1, 6, 3>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0x4); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<4, 1, 6, 3>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0x5); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 5, 6, 3>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0x6); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<4, 5, 6, 3>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0x7); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 1, 2, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0x8); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<4, 1, 2, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0x9); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 5, 2, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0xA); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<4, 5, 2, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0xB); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 1, 6, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0xC); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<4, 1, 6, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0xD); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 5, 6, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return _mm_blend_ps(v0, v1, 0xE); }
#endif

#if defined(NWB_HAS_NEON)
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 1, 4, 5>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vget_low_f32(v0), vget_low_f32(v1)); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<1, 0, 4, 5>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vrev64_f32(vget_low_f32(v0)), vget_low_f32(v1)); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 1, 5, 4>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vget_low_f32(v0), vrev64_f32(vget_low_f32(v1))); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<1, 0, 5, 4>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vrev64_f32(vget_low_f32(v0)), vrev64_f32(vget_low_f32(v1))); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<2, 3, 6, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vget_high_f32(v0), vget_high_f32(v1)); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<3, 2, 6, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vrev64_f32(vget_high_f32(v0)), vget_high_f32(v1)); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<2, 3, 7, 6>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vget_high_f32(v0), vrev64_f32(vget_high_f32(v1))); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<3, 2, 7, 6>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vrev64_f32(vget_high_f32(v0)), vrev64_f32(vget_high_f32(v1))); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 1, 6, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vget_low_f32(v0), vget_high_f32(v1)); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<1, 0, 6, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vrev64_f32(vget_low_f32(v0)), vget_high_f32(v1)); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 1, 7, 6>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vget_low_f32(v0), vrev64_f32(vget_high_f32(v1))); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<1, 0, 7, 6>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vrev64_f32(vget_low_f32(v0)), vrev64_f32(vget_high_f32(v1))); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<3, 2, 4, 5>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vrev64_f32(vget_high_f32(v0)), vget_low_f32(v1)); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<2, 3, 5, 4>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vget_high_f32(v0), vrev64_f32(vget_low_f32(v1))); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<3, 2, 5, 4>(SIMDVector v0, SIMDVector v1)noexcept{ return vcombine_f32(vrev64_f32(vget_high_f32(v0)), vrev64_f32(vget_low_f32(v1))); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 4, 2, 6>(SIMDVector v0, SIMDVector v1)noexcept{ return vtrnq_f32(v0, v1).val[0]; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<1, 5, 3, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return vtrnq_f32(v0, v1).val[1]; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 4, 1, 5>(SIMDVector v0, SIMDVector v1)noexcept{ return vzipq_f32(v0, v1).val[0]; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<2, 6, 3, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return vzipq_f32(v0, v1).val[1]; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<0, 2, 4, 6>(SIMDVector v0, SIMDVector v1)noexcept{ return vuzpq_f32(v0, v1).val[0]; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<1, 3, 5, 7>(SIMDVector v0, SIMDVector v1)noexcept{ return vuzpq_f32(v0, v1).val[1]; }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<1, 2, 3, 4>(SIMDVector v0, SIMDVector v1)noexcept{ return vextq_f32(v0, v1, 1); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<2, 3, 4, 5>(SIMDVector v0, SIMDVector v1)noexcept{ return vextq_f32(v0, v1, 2); }
template<>
NWB_INLINE SIMDVector SIMDCALL VectorPermute<3, 4, 5, 6>(SIMDVector v0, SIMDVector v1)noexcept{ return vextq_f32(v0, v1, 3); }
#endif

NWB_INLINE SIMDVector SIMDCALL VectorSelectControl(u32 selectX, u32 selectY, u32 selectZ, u32 selectW)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(selectX ? 0xFFFFFFFFu : 0u, selectY ? 0xFFFFFFFFu : 0u, selectZ ? 0xFFFFFFFFu : 0u, selectW ? 0xFFFFFFFFu : 0u);
#elif defined(NWB_HAS_NEON)
    const int32x2_t lo = vcreate_s32(static_cast<u64>(selectX) | (static_cast<u64>(selectY) << 32));
    const int32x2_t hi = vcreate_s32(static_cast<u64>(selectZ) | (static_cast<u64>(selectW) << 32));
    return vreinterpretq_f32_u32(vcgtq_s32(vcombine_s32(lo, hi), vreinterpretq_s32_f32(s_SIMDZero)));
#else
    __m128i value = _mm_set_epi32(static_cast<i32>(selectW), static_cast<i32>(selectZ), static_cast<i32>(selectY), static_cast<i32>(selectX));
    value = _mm_cmpgt_epi32(value, _mm_setzero_si128());
    return _mm_castsi128_ps(value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorAndInt(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) & VectorGetIntX(v1), VectorGetIntY(v0) & VectorGetIntY(v1), VectorGetIntZ(v0) & VectorGetIntZ(v1), VectorGetIntW(v0) & VectorGetIntW(v1));
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_and_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorOrInt(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) | VectorGetIntX(v1), VectorGetIntY(v0) | VectorGetIntY(v1), VectorGetIntZ(v0) | VectorGetIntZ(v1), VectorGetIntW(v0) | VectorGetIntW(v1));
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_or_si128(_mm_castps_si128(v0), _mm_castps_si128(v1)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorXorInt(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) ^ VectorGetIntX(v1), VectorGetIntY(v0) ^ VectorGetIntY(v1), VectorGetIntZ(v0) ^ VectorGetIntZ(v1), VectorGetIntW(v0) ^ VectorGetIntW(v1));
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_xor_si128(_mm_castps_si128(v0), _mm_castps_si128(v1)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorAndCInt(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(v0.u[0] & ~v1.u[0], v0.u[1] & ~v1.u[1], v0.u[2] & ~v1.u[2], v0.u[3] & ~v1.u[3]);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_andnot_si128(_mm_castps_si128(v1), _mm_castps_si128(v0)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorNorInt(SIMDVector v0, SIMDVector v1)noexcept{
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

NWB_INLINE SIMDVector SIMDCALL VectorSelect(SIMDVector v0, SIMDVector v1, SIMDVector control)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorOrInt(VectorAndCInt(v0, control), VectorAndInt(v1, control));
#elif defined(NWB_HAS_NEON)
    return vbslq_f32(vreinterpretq_u32_f32(control), v1, v0);
#else
    return _mm_or_ps(_mm_andnot_ps(control, v0), _mm_and_ps(v1, control));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMergeXY(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(v0.u[0], v1.u[0], v0.u[1], v1.u[1]);
#elif defined(NWB_HAS_NEON)
    return vzipq_f32(v0, v1).val[0];
#else
    return _mm_unpacklo_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMergeZW(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(v0.u[2], v1.u[2], v0.u[3], v1.u[3]);
#elif defined(NWB_HAS_NEON)
    return vzipq_f32(v0, v1).val[1];
#else
    return _mm_unpackhi_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorShiftLeft(SIMDVector v0, SIMDVector v1, u32 elements)noexcept{
    NWB_ASSERT(elements < 4);
    switch(elements){
    case 0: return VectorPermute<0, 1, 2, 3>(v0, v1);
    case 1: return VectorPermute<1, 2, 3, 4>(v0, v1);
    case 2: return VectorPermute<2, 3, 4, 5>(v0, v1);
    default: return VectorPermute<3, 4, 5, 6>(v0, v1);
    }
}

NWB_INLINE SIMDVector SIMDCALL VectorRotateLeft(SIMDVector value, u32 elements)noexcept{
    NWB_ASSERT(elements < 4);
    switch(elements){
    case 0: return value;
    case 1: return VectorSwizzle<1, 2, 3, 0>(value);
    case 2: return VectorSwizzle<2, 3, 0, 1>(value);
    default: return VectorSwizzle<3, 0, 1, 2>(value);
    }
}

NWB_INLINE SIMDVector SIMDCALL VectorRotateRight(SIMDVector value, u32 elements)noexcept{
    NWB_ASSERT(elements < 4);
    switch(elements){
    case 0: return value;
    case 1: return VectorSwizzle<3, 0, 1, 2>(value);
    case 2: return VectorSwizzle<2, 3, 0, 1>(value);
    default: return VectorSwizzle<1, 2, 3, 0>(value);
    }
}

NWB_INLINE SIMDVector SIMDCALL VectorInsert(SIMDVector vd, SIMDVector vs, u32 vsLeftRotateElements, u32 select0, u32 select1, u32 select2, u32 select3)noexcept{
    const SIMDVector control = VectorSelectControl(select0 & 1u, select1 & 1u, select2 & 1u, select3 & 1u);
    return VectorSelect(vd, VectorRotateLeft(vs, vsLeftRotateElements), control);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

