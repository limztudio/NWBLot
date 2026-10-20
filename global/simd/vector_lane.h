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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_XY = 0x3u;
inline constexpr u32 s_XYZ = 0x7u;
inline constexpr u32 s_XYZW = 0xFu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Active component masks for vector helpers that consume only a subset of SIMD lanes.
namespace VectorDoubleComponentMask{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_DoubleXY = 0x3u;
inline constexpr u32 s_DoubleXYZ = 0x7u;
inline constexpr u32 s_DoubleXYZW = 0xFu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_RoundHalfBias = 0.5f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VectorScalarDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
NWB_INLINE T* StridePointer(T* pointer, usize stride, usize index)noexcept{
    using ByteT = Conditional_T<IsConst_V<T>, const u8, u8>;
    return reinterpret_cast<T*>(reinterpret_cast<ByteT*>(pointer) + stride * index);
}

template<typename T> requires(IsIntegral_V<T>)
NWB_INLINE T ComparisonMaskR(T mask, T activeMask)noexcept{
    mask &= activeMask;
    if(mask == activeMask)
        return static_cast<T>(s_CRMASK_CR6TRUE);
    if(mask == 0)
        return static_cast<T>(s_CRMASK_CR6FALSE);
    return T{ 0 };
}

template<typename T> requires(IsIntegral_V<T>)
NWB_INLINE T BoundsMaskR(T mask, T activeMask)noexcept{
    return (mask & activeMask) == activeMask ? static_cast<T>(s_CRMASK_CR6BOUNDS) : T{ 0 };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T> requires(IsSame_V<T, f32> || IsSame_V<T, f64>)
[[nodiscard]] NWB_INLINE T RoundToNearest(T value)noexcept{
    if constexpr(IsSame_V<T, f32>){
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
    else{
        // Integer-bit rounding preserves binary64 range, signed zero, and ties independently of the floating-point rounding mode.
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
}

template<typename T> requires(IsSame_V<T, f32> || IsSame_V<T, f64>)
[[nodiscard]] NWB_INLINE auto TruncateBits(T value)noexcept{
    if constexpr(IsSame_V<T, f32>){
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
    else{
        const u64 bits = BitCast<u64>(value);
        const u64 magnitude = bits & 0x7FFFFFFFFFFFFFFFu;
        const u64 exponent = magnitude >> 52u;
        if(exponent == 0x7FFu && (magnitude & 0x000FFFFFFFFFFFFFu) != 0u)
            return BitCast<u64>(value + value);
        if(exponent >= 1075u)
            return bits;
        if(exponent < 1023u)
            return bits & 0x8000000000000000u;
        return bits & ~((u64{ 1u } << (1075u - exponent)) - 1u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ByteIndexLaneScale = 0x04040404u;
inline constexpr u32 s_ByteIndexLaneOffsets = 0x03020100u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_AVX2)
NWB_INLINE __m128i MultiSrlEpi32(__m128i value, __m128i count)noexcept{ return _mm_srlv_epi32(value, count); }

#endif

NWB_INLINE __m128i MultiSllEpi32(__m128i value, __m128i count)noexcept{
#if defined(NWB_HAS_AVX2)
    return _mm_sllv_epi32(value, count);
#else
    __m128i v = _mm_shuffle_epi32(value, _MM_SHUFFLE(0, 0, 0, 0));
    __m128i c = _mm_and_si128(_mm_shuffle_epi32(count, _MM_SHUFFLE(0, 0, 0, 0)), s_SIMDMaskX);
    const __m128i r0 = _mm_sll_epi32(v, c);

    v = _mm_shuffle_epi32(value, _MM_SHUFFLE(1, 1, 1, 1));
    c = _mm_and_si128(_mm_shuffle_epi32(count, _MM_SHUFFLE(1, 1, 1, 1)), s_SIMDMaskX);
    const __m128i r1 = _mm_sll_epi32(v, c);

    v = _mm_shuffle_epi32(value, _MM_SHUFFLE(2, 2, 2, 2));
    c = _mm_and_si128(_mm_shuffle_epi32(count, _MM_SHUFFLE(2, 2, 2, 2)), s_SIMDMaskX);
    const __m128i r2 = _mm_sll_epi32(v, c);

    v = _mm_shuffle_epi32(value, _MM_SHUFFLE(3, 3, 3, 3));
    c = _mm_and_si128(_mm_shuffle_epi32(count, _MM_SHUFFLE(3, 3, 3, 3)), s_SIMDMaskX);
    const __m128i r3 = _mm_sll_epi32(v, c);

    const __m128 r01 = _mm_shuffle_ps(_mm_castsi128_ps(r0), _mm_castsi128_ps(r1), _MM_SHUFFLE(0, 0, 0, 0));
    const __m128 r23 = _mm_shuffle_ps(_mm_castsi128_ps(r2), _mm_castsi128_ps(r3), _MM_SHUFFLE(0, 0, 0, 0));
    return _mm_castps_si128(_mm_shuffle_ps(r01, r23, _MM_SHUFFLE(2, 0, 2, 0)));
#endif
}

NWB_INLINE __m128i GetLeadingBit(__m128i value)noexcept{
    const __m128i mask0000FFFF = _mm_set1_epi32(0x0000FFFF);
    const __m128i mask000000FF = _mm_set1_epi32(0x000000FF);
    const __m128i mask0000000F = _mm_set1_epi32(0x0000000F);
    const __m128i mask00000003 = _mm_set1_epi32(0x00000003);

    __m128i c = _mm_cmpgt_epi32(value, mask0000FFFF);
    __m128i b = _mm_srli_epi32(c, 31);
    __m128i r = _mm_slli_epi32(b, 4);
#if defined(NWB_HAS_AVX2)
    value = MultiSrlEpi32(value, r);
#else
    value = _mm_or_si128(_mm_and_si128(c, _mm_srli_epi32(value, 16)), _mm_andnot_si128(c, value));
#endif

    c = _mm_cmpgt_epi32(value, mask000000FF);
    b = _mm_srli_epi32(c, 31);
    __m128i s = _mm_slli_epi32(b, 3);
#if defined(NWB_HAS_AVX2)
    value = MultiSrlEpi32(value, s);
#else
    value = _mm_or_si128(_mm_and_si128(c, _mm_srli_epi32(value, 8)), _mm_andnot_si128(c, value));
#endif
    r = _mm_or_si128(r, s);

    c = _mm_cmpgt_epi32(value, mask0000000F);
    b = _mm_srli_epi32(c, 31);
    s = _mm_slli_epi32(b, 2);
#if defined(NWB_HAS_AVX2)
    value = MultiSrlEpi32(value, s);
#else
    value = _mm_or_si128(_mm_and_si128(c, _mm_srli_epi32(value, 4)), _mm_andnot_si128(c, value));
#endif
    r = _mm_or_si128(r, s);

    c = _mm_cmpgt_epi32(value, mask00000003);
    b = _mm_srli_epi32(c, 31);
    s = _mm_slli_epi32(b, 1);
#if defined(NWB_HAS_AVX2)
    value = MultiSrlEpi32(value, s);
#else
    value = _mm_or_si128(_mm_and_si128(c, _mm_srli_epi32(value, 2)), _mm_andnot_si128(c, value));
#endif
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_NEON64)
NWB_INLINE uint8x16_t PermuteByteIndices(u32 x, u32 y, u32 z, u32 w)noexcept{
    const uint32x4_t lanes = vreinterpretq_u32_f32(SIMDConvertDetail::MakeU32(x, y, z, w));
    return vreinterpretq_u8_u32(vaddq_u32(vmulq_n_u32(lanes, s_ByteIndexLaneScale), vdupq_n_u32(s_ByteIndexLaneOffsets)));
}
#endif

#if defined(NWB_HAS_SSE2)
template<u32 Lane>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SetLane(SIMDVectorSingle value, f32 component)noexcept{
    static_assert(Lane < 4u);
    if constexpr(Lane == 0u)
        return _mm_move_ss(value, _mm_set_ss(component));
    else{
        constexpr int s_Shuffle = _MM_SHUFFLE(Lane == 3u ? 0u : 3u, Lane == 2u ? 0u : 2u, Lane == 1u ? 0u : 1u, Lane);
        const __m128 swapped = _mm_shuffle_ps(value, value, s_Shuffle);
        const __m128 changed = _mm_move_ss(swapped, _mm_set_ss(component));
        return _mm_shuffle_ps(changed, changed, s_Shuffle);
    }
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SwizzleLanes(SIMDVectorSingle value, __m128i control)noexcept{
#if defined(NWB_HAS_SSE4)
    const __m128i byteOffsets = _mm_mullo_epi32(control, _mm_set1_epi32(static_cast<i32>(s_ByteIndexLaneScale)));
    const __m128i byteIndices = _mm_add_epi32(byteOffsets, _mm_set1_epi32(static_cast<i32>(s_ByteIndexLaneOffsets)));
    return _mm_castsi128_ps(_mm_shuffle_epi8(_mm_castps_si128(value), byteIndices));
#else
    const __m128 x = _mm_and_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(control, _mm_setzero_si128())), _mm_shuffle_ps(value, value, _MM_SHUFFLE(0, 0, 0, 0)));
    const __m128 y = _mm_and_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(control, _mm_set1_epi32(1))), _mm_shuffle_ps(value, value, _MM_SHUFFLE(1, 1, 1, 1)));
    const __m128 z = _mm_and_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(control, _mm_set1_epi32(2))), _mm_shuffle_ps(value, value, _MM_SHUFFLE(2, 2, 2, 2)));
    const __m128 w = _mm_and_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(control, _mm_set1_epi32(3))), _mm_shuffle_ps(value, value, _MM_SHUFFLE(3, 3, 3, 3)));
    return _mm_or_ps(_mm_or_ps(x, y), _mm_or_ps(z, w));
#endif
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<u32 Lane>
NWB_INLINE f32 NWB_SIMD_CALL GetLane(SIMDVectorSingle value)noexcept{
    static_assert(Lane < 4u);
#if defined(NWB_HAS_SCALAR)
    return value.f[Lane];
#elif defined(NWB_HAS_NEON)
    return vgetq_lane_f32(value, Lane);
#else
    if constexpr(Lane == 0u)
        return _mm_cvtss_f32(value);
    else{
#if defined(NWB_HAS_AVX)
        return _mm_cvtss_f32(_mm_permute_ps(value, _MM_SHUFFLE(Lane, Lane, Lane, Lane)));
#else
        return _mm_cvtss_f32(_mm_shuffle_ps(value, value, _MM_SHUFFLE(Lane, Lane, Lane, Lane)));
#endif
    }
#endif
}

template<u32 Lane>
NWB_INLINE u32 NWB_SIMD_CALL GetIntLane(SIMDVectorSingle value)noexcept{
    static_assert(Lane < 4u);
#if defined(NWB_HAS_SCALAR)
    return value.u[Lane];
#elif defined(NWB_HAS_NEON)
    return vgetq_lane_u32(vreinterpretq_u32_f32(value), Lane);
#else
    if constexpr(Lane == 0u)
        return static_cast<u32>(_mm_cvtsi128_si32(_mm_castps_si128(value)));
    else{
#if defined(NWB_HAS_SSE4)
        return static_cast<u32>(_mm_extract_epi32(_mm_castps_si128(value), Lane));
#else
        return static_cast<u32>(_mm_cvtsi128_si32(_mm_shuffle_epi32(_mm_castps_si128(value), _MM_SHUFFLE(Lane, Lane, Lane, Lane))));
#endif
    }
#endif
}

template<u32 Lane>
NWB_INLINE void NWB_SIMD_CALL StoreLane(f32& out, SIMDVectorSingle value)noexcept{
    static_assert(Lane < 4u);
    out = GetLane<Lane>(value);
}

template<u32 Lane>
NWB_INLINE void NWB_SIMD_CALL StoreIntLane(u32& out, SIMDVectorSingle value)noexcept{
    static_assert(Lane < 4u);
    out = GetIntLane<Lane>(value);
}

template<u32 Lane>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SplatLane(SIMDVectorSingle value)noexcept{
    static_assert(Lane < 4u);
#if defined(NWB_HAS_SCALAR)
    const f32 laneValue = value.f[Lane];
    return SIMDConvertDetail::MakeF32(laneValue, laneValue, laneValue, laneValue);
#elif defined(NWB_HAS_NEON)
    if constexpr(Lane < 2u)
        return vdupq_lane_f32(vget_low_f32(value), Lane);
    else
        return vdupq_lane_f32(vget_high_f32(value), Lane - 2u);
#elif defined(NWB_HAS_AVX2)
    if constexpr(Lane == 0u)
        return _mm_broadcastss_ps(value);
    else
        return _mm_permute_ps(value, _MM_SHUFFLE(Lane, Lane, Lane, Lane));
#elif defined(NWB_HAS_AVX)
    return _mm_permute_ps(value, _MM_SHUFFLE(Lane, Lane, Lane, Lane));
#else
    return _mm_shuffle_ps(value, value, _MM_SHUFFLE(Lane, Lane, Lane, Lane));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using UIntVector = u64 __attribute__((ext_vector_type(4)));

inline constexpr u64 s_DoubleByteIndexLaneScale = 0x0808080808080808ull;
inline constexpr u64 s_DoubleByteIndexLaneOffsets = 0x0706050403020100ull;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_NEON64) || defined(NWB_HAS_SSE2)
template<u32 Lane>
NWB_INLINE NativeDoublePair NWB_SIMD_CALL GetPermutePair(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    static_assert(Lane < 8u);
    if constexpr(Lane < 2u)
        return GetLow(v0);
    else if constexpr(Lane < 4u)
        return GetHigh(v0);
    else if constexpr(Lane < 6u)
        return GetLow(v1);
    else
        return GetHigh(v1);
}

template<u32 X, u32 Y>
NWB_INLINE NativeDoublePair NWB_SIMD_CALL PermutePair(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    static_assert(X < 8u && Y < 8u);
    const NativeDoublePair x = GetPermutePair<X>(v0, v1);
    const NativeDoublePair y = GetPermutePair<Y>(v0, v1);
#if defined(NWB_HAS_NEON64)
    if constexpr((X & 1u) == 0u && (Y & 1u) == 0u)
        return vzip1q_f64(x, y);
    else if constexpr((X & 1u) != 0u && (Y & 1u) != 0u)
        return vzip2q_f64(x, y);
    else if constexpr((X & 1u) != 0u)
        return vextq_f64(x, y, 1);
    else
        return vcombine_f64(vget_low_f64(x), vget_high_f64(y));
#else
    return _mm_shuffle_pd(x, y, (X & 1u) | ((Y & 1u) << 1u));
#endif
}
#endif

#if defined(NWB_HAS_NEON64)
NWB_INLINE uint8x16_t PermuteByteIndices(u32 x, u32 y)noexcept{
    const uint64x2_t offsets = vsetq_lane_u64(static_cast<u64>(y) * s_DoubleByteIndexLaneScale, vdupq_n_u64(static_cast<u64>(x) * s_DoubleByteIndexLaneScale), 1);
    const uint8x8_t laneBytes = vcreate_u8(s_DoubleByteIndexLaneOffsets);
    return vaddq_u8(vreinterpretq_u8_u64(offsets), vcombine_u8(laneBytes, laneBytes));
}
#elif defined(NWB_HAS_SSE2)
NWB_INLINE NativeDoublePair NWB_SIMD_CALL PermutePair(SIMDVectorDouble v0, SIMDVectorDouble v1, u32 x, u32 y)noexcept{
    const SIMDVectorDouble xVector = x < 4u ? v0 : v1;
    const SIMDVectorDouble yVector = y < 4u ? v0 : v1;
    NativeDoublePair xPair = (x & 2u) == 0u ? GetLow(xVector) : GetHigh(xVector);
    NativeDoublePair yPair = (y & 2u) == 0u ? GetLow(yVector) : GetHigh(yVector);
    if((x & 1u) != 0u)
        xPair = _mm_unpackhi_pd(xPair, xPair);
    if((y & 1u) != 0u)
        yPair = _mm_unpackhi_pd(yPair, yPair);
    return _mm_unpacklo_pd(xPair, yPair);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<u32 Lane>
NWB_INLINE f64 NWB_SIMD_CALL GetLane(SIMDVectorDouble value)noexcept{
    static_assert(Lane < 4u);
#if defined(NWB_HAS_NEON64)
    if constexpr(Lane < 2u)
        return vgetq_lane_f64(GetLow(value), Lane);
    else
        return vgetq_lane_f64(GetHigh(value), Lane - 2u);
#elif defined(NWB_HAS_SSE2)
    const NativeDoublePair pair = Lane < 2u ? GetLow(value) : GetHigh(value);
    if constexpr((Lane & 1u) == 0u)
        return _mm_cvtsd_f64(pair);
    else
        return _mm_cvtsd_f64(_mm_unpackhi_pd(pair, pair));
#else
    return value[Lane];
#endif
}

template<u32 Lane>
NWB_INLINE u64 NWB_SIMD_CALL GetIntLane(SIMDVectorDouble value)noexcept{
    static_assert(Lane < 4u);
#if defined(NWB_HAS_NEON64)
    if constexpr(Lane < 2u)
        return vgetq_lane_u64(GetLowBits(value), Lane);
    else
        return vgetq_lane_u64(GetHighBits(value), Lane - 2u);
#elif defined(NWB_HAS_SSE2)
    const NativeUIntPair pair = Lane < 2u ? GetLowBits(value) : GetHighBits(value);
    if constexpr((Lane & 1u) == 0u)
        return BitCast<u64>(_mm_cvtsi128_si64(pair));
    else
        return BitCast<u64>(_mm_cvtsi128_si64(_mm_unpackhi_epi64(pair, pair)));
#else
    return BitCast<u64>(value[Lane]);
#endif
}

template<u32 Lane>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SetLane(SIMDVectorDouble value, f64 component)noexcept{
    static_assert(Lane < 4u);
#if defined(NWB_HAS_NEON64)
    if constexpr(Lane < 2u)
        return FromPairs(vsetq_lane_f64(component, GetLow(value), Lane), GetHigh(value));
    else
        return FromPairs(GetLow(value), vsetq_lane_f64(component, GetHigh(value), Lane - 2u));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_blend_pd(BitCast<__m256d>(value), _mm256_set1_pd(component), 1u << Lane));
#elif defined(NWB_HAS_SSE2)
    const NativeDoublePair pair = Lane < 2u ? GetLow(value) : GetHigh(value);
    const NativeDoublePair scalar = _mm_set_sd(component);
    const NativeDoublePair result = (Lane & 1u) == 0u ? _mm_move_sd(pair, scalar) : _mm_unpacklo_pd(pair, scalar);
    if constexpr(Lane < 2u)
        return FromPairs(result, GetHigh(value));
    else
        return FromPairs(GetLow(value), result);
#else
    value[Lane] = component;
    return value;
#endif
}

template<u32 Lane>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SplatLane(SIMDVectorDouble value)noexcept{
    static_assert(Lane < 4u);
#if defined(NWB_HAS_NEON64)
    const NativeDoublePair pair = Lane < 2u ? GetLow(value) : GetHigh(value);
    const NativeDoublePair result = vdupq_laneq_f64(pair, Lane & 1u);
    return FromPairs(result, result);
#elif defined(NWB_HAS_AVX2)
    return BitCast<SIMDVectorDouble>(_mm256_permute4x64_pd(BitCast<__m256d>(value), _MM_SHUFFLE(Lane, Lane, Lane, Lane)));
#elif defined(NWB_HAS_AVX)
    const __m256d pairs = _mm256_permute2f128_pd(BitCast<__m256d>(value), BitCast<__m256d>(value), Lane < 2u ? 0x00 : 0x11);
    return BitCast<SIMDVectorDouble>(_mm256_permute_pd(pairs, (Lane & 1u) == 0u ? 0x0 : 0xF));
#elif defined(NWB_HAS_SSE2)
    const NativeDoublePair pair = Lane < 2u ? GetLow(value) : GetHigh(value);
    const NativeDoublePair result = _mm_shuffle_pd(pair, pair, (Lane & 1u) == 0u ? 0x0 : 0x3);
    return FromPairs(result, result);
#else
    return __builtin_shufflevector(value, value, Lane, Lane, Lane, Lane);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_AVX2)
// Preserve sequential reduction order and broadcast only after the final scalar-lane addition.
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL Reduce3(SIMDVectorDouble value)noexcept{
    const __m128d low = GetLow(value), high = GetHigh(value);
    const __m128d xy = _mm_add_sd(low, _mm_unpackhi_pd(low, low));
    const __m128d xyz = _mm_add_sd(xy, high);
    const __m128d replicated = _mm_unpacklo_pd(xyz, xyz);
    return FromPairs(replicated, replicated);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL Reduce4(SIMDVectorDouble value)noexcept{
    const __m128d low = GetLow(value), high = GetHigh(value);
    const __m128d xy = _mm_add_sd(low, _mm_unpackhi_pd(low, low));
    const __m128d xyz = _mm_add_sd(xy, high);
    const __m128d xyzw = _mm_add_sd(xyz, _mm_unpackhi_pd(high, high));
    const __m128d replicated = _mm_unpacklo_pd(xyzw, xyzw);
    return FromPairs(replicated, replicated);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

