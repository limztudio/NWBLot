// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../assert.h"
#include "../simplemath.h"
#include "macro.h"
#include "constant.h"
#include "convert.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Active component masks for vector helpers that consume only a subset of SIMD lanes.
namespace VectorComponentMask{
    inline constexpr u32 s_XY = 0x3u;
    inline constexpr u32 s_XYZ = 0x7u;
    inline constexpr u32 s_XYZW = 0xFu;
};


inline constexpr f32 s_RoundHalfBias = 0.5f;
inline constexpr f32 s_CosQuarticCoeff = 0.5f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
NWB_INLINE T* StridePointer(T* pointer, usize stride, usize index)noexcept{
    return reinterpret_cast<T*>(reinterpret_cast<u8*>(pointer) + stride * index);
}

template<typename T>
NWB_INLINE const T* StridePointer(const T* pointer, usize stride, usize index)noexcept{
    return reinterpret_cast<const T*>(reinterpret_cast<const u8*>(pointer) + stride * index);
}

NWB_INLINE u32 ComparisonMaskR(u32 mask, u32 activeMask)noexcept{
    mask &= activeMask;
    if(mask == activeMask)
        return s_CRMASK_CR6TRUE;
    if(mask == 0)
        return s_CRMASK_CR6FALSE;
    return 0;
}

NWB_INLINE u32 BoundsMaskR(u32 mask, u32 activeMask)noexcept{
    return (mask & activeMask) == activeMask ? s_CRMASK_CR6BOUNDS : 0;
}

NWB_INLINE f32 RoundToNearest(f32 value)noexcept{
    f32 integer = Floor(value);
    value -= integer;
    if(value < s_RoundHalfBias)
        return integer;
    if(value > s_RoundHalfBias)
        return integer + 1.0f;

    f32 intPart{};
    const f32 fractionalPart = ModF(integer * s_RoundHalfBias, &intPart);
    if(fractionalPart == 0.0f)
        return integer;
    return integer + 1.0f;
}

NWB_INLINE void ScalarSinCos(f32& outSin, f32& outCos, f32 value)noexcept{
    f32 quotient = s_1DIV2PI * value;
    if(value >= 0.0f)
        quotient = static_cast<f32>(static_cast<i32>(quotient + s_RoundHalfBias));
    else
        quotient = static_cast<f32>(static_cast<i32>(quotient - s_RoundHalfBias));

    f32 y = value - (s_2PI * quotient);
    f32 sign{};
    if(y > s_PIDIV2){
        y = s_PI - y;
        sign = -1.0f;
    }
    else if(y < -s_PIDIV2){
        y = -s_PI - y;
        sign = -1.0f;
    }
    else{
        sign = 1.0f;
    }

    const f32 y2 = y * y;
    outSin = (((((-2.3889859e-08f * y2 + 2.7525562e-06f) * y2 - 0.00019840874f) * y2 + 0.0083333310f) * y2 - 0.16666667f) * y2 + 1.0f) * y;
    outCos = sign * (((((-2.6051615e-07f * y2 + 2.4760495e-05f) * y2 - 0.0013888378f) * y2 + 0.041666638f) * y2 - s_CosQuarticCoeff) * y2 + 1.0f);
}

NWB_INLINE u32 TruncateBits(f32 value)noexcept{
    union{
        f32 f;
        u32 u;
    } result{};

    if(IsNaN(value)){
        result.u = 0x7FC00000u;
    }
    else if(Abs(value) < 8388608.0f){
        result.f = static_cast<f32>(static_cast<i32>(value));
    }
    else{
        result.f = value;
    }

    return result.u;
}

#if defined(NWB_HAS_SSE4)
struct SllEpi32 final{
    static NWB_INLINE __m128i Apply(__m128i value, __m128i count)noexcept{ return _mm_sll_epi32(value, count); }
};

struct SrlEpi32 final{
    static NWB_INLINE __m128i Apply(__m128i value, __m128i count)noexcept{ return _mm_srl_epi32(value, count); }
};

template<typename ShiftOp>
NWB_INLINE __m128i MultiShiftEpi32(__m128i value, __m128i count)noexcept{
    __m128i v = _mm_shuffle_epi32(value, _MM_SHUFFLE(0, 0, 0, 0));
    __m128i c = _mm_and_si128(_mm_shuffle_epi32(count, _MM_SHUFFLE(0, 0, 0, 0)), s_SIMDMaskX);
    const __m128i r0 = ShiftOp::Apply(v, c);

    v = _mm_shuffle_epi32(value, _MM_SHUFFLE(1, 1, 1, 1));
    c = _mm_and_si128(_mm_shuffle_epi32(count, _MM_SHUFFLE(1, 1, 1, 1)), s_SIMDMaskX);
    const __m128i r1 = ShiftOp::Apply(v, c);

    v = _mm_shuffle_epi32(value, _MM_SHUFFLE(2, 2, 2, 2));
    c = _mm_and_si128(_mm_shuffle_epi32(count, _MM_SHUFFLE(2, 2, 2, 2)), s_SIMDMaskX);
    const __m128i r2 = ShiftOp::Apply(v, c);

    v = _mm_shuffle_epi32(value, _MM_SHUFFLE(3, 3, 3, 3));
    c = _mm_and_si128(_mm_shuffle_epi32(count, _MM_SHUFFLE(3, 3, 3, 3)), s_SIMDMaskX);
    const __m128i r3 = ShiftOp::Apply(v, c);

    const __m128 r01 = _mm_shuffle_ps(_mm_castsi128_ps(r0), _mm_castsi128_ps(r1), _MM_SHUFFLE(0, 0, 0, 0));
    const __m128 r23 = _mm_shuffle_ps(_mm_castsi128_ps(r2), _mm_castsi128_ps(r3), _MM_SHUFFLE(0, 0, 0, 0));
    return _mm_castps_si128(_mm_shuffle_ps(r01, r23, _MM_SHUFFLE(2, 0, 2, 0)));
}

NWB_INLINE __m128i MultiSllEpi32(__m128i value, __m128i count)noexcept{
    return MultiShiftEpi32<SllEpi32>(value, count);
}

NWB_INLINE __m128i MultiSrlEpi32(__m128i value, __m128i count)noexcept{
    return MultiShiftEpi32<SrlEpi32>(value, count);
}

NWB_INLINE __m128i GetLeadingBit(__m128i value)noexcept{
    const __m128i mask0000FFFF = _mm_set1_epi32(0x0000FFFF);
    const __m128i mask000000FF = _mm_set1_epi32(0x000000FF);
    const __m128i mask0000000F = _mm_set1_epi32(0x0000000F);
    const __m128i mask00000003 = _mm_set1_epi32(0x00000003);

    __m128i c = _mm_cmpgt_epi32(value, mask0000FFFF);
    __m128i b = _mm_srli_epi32(c, 31);
    __m128i r = _mm_slli_epi32(b, 4);
    value = MultiSrlEpi32(value, r);

    c = _mm_cmpgt_epi32(value, mask000000FF);
    b = _mm_srli_epi32(c, 31);
    __m128i s = _mm_slli_epi32(b, 3);
    value = MultiSrlEpi32(value, s);
    r = _mm_or_si128(r, s);

    c = _mm_cmpgt_epi32(value, mask0000000F);
    b = _mm_srli_epi32(c, 31);
    s = _mm_slli_epi32(b, 2);
    value = MultiSrlEpi32(value, s);
    r = _mm_or_si128(r, s);

    c = _mm_cmpgt_epi32(value, mask00000003);
    b = _mm_srli_epi32(c, 31);
    s = _mm_slli_epi32(b, 1);
    value = MultiSrlEpi32(value, s);
    r = _mm_or_si128(r, s);

    s = _mm_srli_epi32(value, 1);
    return _mm_or_si128(r, s);
}
#endif

#if defined(NWB_HAS_NEON)
NWB_INLINE int32x4_t GetLeadingBit(int32x4_t value)noexcept{
    const uint32x4_t raw = vreinterpretq_u32_s32(value);
    const uint32x4_t isZero = vceqq_u32(raw, vdupq_n_u32(0));
    uint32x4_t leading = vsubq_u32(vdupq_n_u32(31), vclzq_u32(raw));
    leading = vbslq_u32(isZero, vdupq_n_u32(0), leading);
    return vreinterpretq_s32_u32(leading);
}
#endif

template<u32 Lane>
NWB_INLINE f32 SIMDCALL GetLane(SIMDVector value)noexcept{
    static_assert(Lane < 4u);
#if defined(NWB_HAS_SCALAR)
    return value.f[Lane];
#elif defined(NWB_HAS_NEON)
    return vgetq_lane_f32(value, Lane);
#else
    if constexpr(Lane == 0u)
        return _mm_cvtss_f32(value);
    else{
#if defined(NWB_HAS_AVX2)
        return _mm_cvtss_f32(_mm_permute_ps(value, _MM_SHUFFLE(Lane, Lane, Lane, Lane)));
#else
        return _mm_cvtss_f32(_mm_shuffle_ps(value, value, _MM_SHUFFLE(Lane, Lane, Lane, Lane)));
#endif
    }
#endif
}

template<u32 Lane>
NWB_INLINE u32 SIMDCALL GetIntLane(SIMDVector value)noexcept{
    static_assert(Lane < 4u);
#if defined(NWB_HAS_SCALAR)
    return value.u[Lane];
#elif defined(NWB_HAS_NEON)
    return vgetq_lane_u32(vreinterpretq_u32_f32(value), Lane);
#else
    if constexpr(Lane == 0u)
        return static_cast<u32>(_mm_cvtsi128_si32(_mm_castps_si128(value)));
    else
        return static_cast<u32>(_mm_extract_epi32(_mm_castps_si128(value), Lane));
#endif
}

template<u32 Lane>
NWB_INLINE void SIMDCALL StoreLane(f32& out, SIMDVector value)noexcept{
    static_assert(Lane < 4u);
    out = GetLane<Lane>(value);
}

template<u32 Lane>
NWB_INLINE void SIMDCALL StoreIntLane(u32& out, SIMDVector value)noexcept{
    static_assert(Lane < 4u);
    out = GetIntLane<Lane>(value);
}

template<u32 Lane>
NWB_INLINE SIMDVector SIMDCALL SplatLane(SIMDVector value)noexcept{
    static_assert(Lane < 4u);
#if defined(NWB_HAS_SCALAR)
    const f32 laneValue = value.f[Lane];
    return SIMDConvertDetail::MakeF32(laneValue, laneValue, laneValue, laneValue);
#elif defined(NWB_HAS_NEON)
    if constexpr(Lane < 2u)
        return vdupq_lane_f32(vget_low_f32(value), Lane);
    else
        return vdupq_lane_f32(vget_high_f32(value), Lane - 2u);
#elif defined(__AVX2__) || defined(_M_AVX2)
    if constexpr(Lane == 0u)
        return _mm_broadcastss_ps(value);
    else
        return _mm_permute_ps(value, _MM_SHUFFLE(Lane, Lane, Lane, Lane));
#elif defined(NWB_HAS_AVX2)
    return _mm_permute_ps(value, _MM_SHUFFLE(Lane, Lane, Lane, Lane));
#else
    return _mm_shuffle_ps(value, value, _MM_SHUFFLE(Lane, Lane, Lane, Lane));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

