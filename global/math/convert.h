// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../bit.h"
#include "type.h"
#include "constant.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace HalfConvertDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// IEEE-754 binary32/binary16 layout; scalar fallback documents the shared bit contract.
inline constexpr u32 s_F32SignBitMask = 0x80000000u;
inline constexpr u32 s_F32AbsoluteBitMask = 0x7fffffffu;
inline constexpr u32 s_F32InfinityBits = 0x7f800000u;
inline constexpr u32 s_F32MantissaBitCount = 23u;
inline constexpr u32 s_F32ImplicitMantissaBit = 0x800000u;
inline constexpr u32 s_F32MantissaBitMask = 0x7fffffu;
inline constexpr u32 s_F32ToF16OverflowBits = 0x47800000u;
inline constexpr u32 s_F32ToF16UnderflowBits = 0x33000000u;
inline constexpr u32 s_F32ToF16MinNormalBits = 0x38800000u;
inline constexpr u32 s_F32ToF16NormalExponentAdjustment = 0xc8000000u;
inline constexpr u32 s_F32ToF16SubnormalShiftBase = 125u;
inline constexpr u32 s_F32ToF16SignBitShift = 16u;
inline constexpr u32 s_F32ToF16MantissaShift = 13u;
inline constexpr u32 s_F16SignBitMask = 0x8000u;
inline constexpr u32 s_F16ExponentBitMask = 0x7c00u;
inline constexpr u32 s_F16ExponentFieldMask = 0x1fu;
inline constexpr u32 s_F16MantissaBitCount = 10u;
inline constexpr u32 s_F16MantissaBitMask = 0x03ffu;
inline constexpr u32 s_F16ImplicitMantissaBit = 0x0400u;
inline constexpr u32 s_F16QuietNanBit = 0x0200u;
inline constexpr u32 s_F16NormalRoundingBias = 0x0fffu;
inline constexpr i32 s_F32ToF16ExponentBiasDelta = 112;
inline constexpr i32 s_F16InfinityF32Exponent = static_cast<i32>(s_F16ExponentFieldMask) + s_F32ToF16ExponentBiasDelta;
inline constexpr i32 s_F16ZeroF32Exponent = -s_F32ToF16ExponentBiasDelta;
inline constexpr usize s_F16CVectorElementCount = 4u;
inline constexpr usize s_F16CVectorElementShift = 2u;
inline constexpr int s_F16CNearestRoundingControl = 0;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE Half FloatToHalfScalar(const f32 value)noexcept{
    u32 bits = BitCast<u32>(value);
    const u32 sign = (bits & s_F32SignBitMask) >> s_F32ToF16SignBitShift;
    bits &= s_F32AbsoluteBitMask;

    u32 result = 0u;
    if(bits >= s_F32ToF16OverflowBits){
        result = s_F16ExponentBitMask | ((bits > s_F32InfinityBits) ? (s_F16QuietNanBit | ((bits >> s_F32ToF16MantissaShift) & s_F16MantissaBitMask)) : 0u);
    }
    else if(bits <= s_F32ToF16UnderflowBits){
        result = 0u;
    }
    else if(bits < s_F32ToF16MinNormalBits){
        const u32 shift = s_F32ToF16SubnormalShiftBase - (bits >> s_F32MantissaBitCount);
        bits = s_F32ImplicitMantissaBit | (bits & s_F32MantissaBitMask);
        result = bits >> (shift + 1u);
        const u32 sticky = (bits & ((1u << shift) - 1u)) != 0u ? 1u : 0u;
        result += (result | sticky) & ((bits >> shift) & 1u);
    }
    else{
        bits += s_F32ToF16NormalExponentAdjustment;
        result = ((bits + s_F16NormalRoundingBias + ((bits >> s_F32ToF16MantissaShift) & 1u)) >> s_F32ToF16MantissaShift) & (s_F16SignBitMask - 1u);
    }

    return static_cast<Half>(result | sign);
}

[[nodiscard]] NWB_INLINE f32 HalfToFloatScalar(const Half value)noexcept{
    u32 mantissa = static_cast<u32>(value & s_F16MantissaBitMask);
    i32 exponent = static_cast<i32>(value & s_F16ExponentBitMask);
    if(exponent == static_cast<i32>(s_F16ExponentBitMask)){
        exponent = s_F16InfinityF32Exponent;
    }
    else if(exponent != 0){
        exponent = static_cast<i32>((value >> s_F16MantissaBitCount) & s_F16ExponentFieldMask);
    }
    else if(mantissa != 0u){
        exponent = 1;
        do{
            --exponent;
            mantissa <<= 1u;
        }while((mantissa & s_F16ImplicitMantissaBit) == 0u);
        mantissa &= s_F16MantissaBitMask;
    }
    else{
        exponent = s_F16ZeroF32Exponent;
    }

    const u32 result = ((static_cast<u32>(value) & s_F16SignBitMask) << s_F32ToF16SignBitShift)
        | (static_cast<u32>(exponent + s_F32ToF16ExponentBiasDelta) << s_F32MantissaBitCount)
        | (mantissa << s_F32ToF16MantissaShift);
    return BitCast<f32>(result);
}

#if defined(NWB_HAS_F16C)
[[nodiscard]] NWB_INLINE Half FloatToHalfF16C(const f32 value)noexcept{
    const __m128 floatValue = _mm_set_ss(value);
    const __m128i halfValue = _mm_cvtps_ph(floatValue, s_F16CNearestRoundingControl);
    return static_cast<Half>(_mm_cvtsi128_si32(halfValue));
}

[[nodiscard]] NWB_INLINE f32 HalfToFloatF16C(const Half value)noexcept{
    const __m128i halfValue = _mm_cvtsi32_si128(static_cast<int>(value));
    const __m128 floatValue = _mm_cvtph_ps(halfValue);
    return _mm_cvtss_f32(floatValue);
}

NWB_INLINE Half* FloatBufferToHalfF16C(Half* outHalfBuffer, const f32* floatBuffer, const usize count)noexcept{
    usize i = 0u;
    const usize vectorCount = count >> s_F16CVectorElementShift;
    for(usize vectorIndex = 0u; vectorIndex < vectorCount; ++vectorIndex){
        const __m128 floatValue = _mm_loadu_ps(floatBuffer + i);
        const __m128i halfValue = _mm_cvtps_ph(floatValue, s_F16CNearestRoundingControl);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(outHalfBuffer + i), halfValue);
        i += s_F16CVectorElementCount;
    }

    for(; i < count; ++i)
        outHalfBuffer[i] = FloatToHalfF16C(floatBuffer[i]);
    return outHalfBuffer;
}

NWB_INLINE f32* HalfBufferToFloatF16C(f32* outFloatBuffer, const Half* halfBuffer, const usize count)noexcept{
    usize i = 0u;
    const usize vectorCount = count >> s_F16CVectorElementShift;
    for(usize vectorIndex = 0u; vectorIndex < vectorCount; ++vectorIndex){
        const __m128i halfValue = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(halfBuffer + i));
        const __m128 floatValue = _mm_cvtph_ps(halfValue);
        _mm_storeu_ps(outFloatBuffer + i, floatValue);
        i += s_F16CVectorElementCount;
    }

    for(; i < count; ++i)
        outFloatBuffer[i] = HalfToFloatF16C(halfBuffer[i]);
    return outFloatBuffer;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UnsignedFloatConvertDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_F32SignBitMask = 0x80000000u;
inline constexpr u32 s_F32AbsoluteBitMask = 0x7fffffffu;
inline constexpr u32 s_F32InfinityBits = 0x7f800000u;
inline constexpr u32 s_F32MantissaBitCount = 23u;
inline constexpr u32 s_F32ExponentBias = 127u;
inline constexpr u32 s_F32ImplicitMantissaBit = 0x800000u;
inline constexpr u32 s_F32MantissaBitMask = 0x7fffffu;
inline constexpr u32 s_ExponentFieldMask = 0x1fu;
inline constexpr u32 s_ExponentBitCount = 5u;
inline constexpr u32 s_ExponentBias = 15u;
inline constexpr u32 s_MaxMantissaBitCount = 10u;
inline constexpr u32 s_OverflowBits = 0x47800000u;
inline constexpr u32 s_MinNormalBits = 0x38800000u;
inline constexpr u32 s_NormalRoundingBiasBits = 0xc8000000u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDConvertDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL MakeF32(f32 x, f32 y, f32 z, f32 w)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDVectorSingle result{};
    result.f[0] = x;
    result.f[1] = y;
    result.f[2] = z;
    result.f[3] = w;
    return result;
#elif defined(NWB_HAS_NEON)
    float32x4_t result = vdupq_n_f32(x);
    result = vsetq_lane_f32(y, result, 1);
    result = vsetq_lane_f32(z, result, 2);
    return vsetq_lane_f32(w, result, 3);
#elif defined(NWB_HAS_SSE4)
    return _mm_set_ps(w, z, y, x);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL MakeU32(u32 x, u32 y, u32 z, u32 w)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDVectorSingle result{};
    result.u[0] = x;
    result.u[1] = y;
    result.u[2] = z;
    result.u[3] = w;
    return result;
#elif defined(NWB_HAS_NEON)
    uint32x4_t result = vdupq_n_u32(x);
    result = vsetq_lane_u32(y, result, 1);
    result = vsetq_lane_u32(z, result, 2);
    return vreinterpretq_f32_u32(vsetq_lane_u32(w, result, 3));
#elif defined(NWB_HAS_SSE4)
    return _mm_castsi128_ps(_mm_set_epi32(static_cast<i32>(w), static_cast<i32>(z), static_cast<i32>(y), static_cast<i32>(x)));
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreF32(f32* out, SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SCALAR)
    out[0] = value.f[0];
    out[1] = value.f[1];
    out[2] = value.f[2];
    out[3] = value.f[3];
#elif defined(NWB_HAS_NEON)
    vst1q_f32(out, value);
#elif defined(NWB_HAS_SSE4)
    _mm_storeu_ps(out, value);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreU32(u32* out, SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SCALAR)
    out[0] = value.u[0];
    out[1] = value.u[1];
    out[2] = value.u[2];
    out[3] = value.u[3];
#elif defined(NWB_HAS_NEON)
    vst1q_u32(out, vreinterpretq_u32_f32(value));
#elif defined(NWB_HAS_SSE4)
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), _mm_castps_si128(value));
#endif
}

#if defined(NWB_HAS_SSE4)
NWB_INLINE void NWB_SIMD_CALL StoreInt3Bits(SIMDVectorSingle value, i32* out)noexcept{
    _mm_store_sd(reinterpret_cast<f64*>(out), _mm_castps_pd(value));
    out[2] = _mm_extract_ps(value, 2);
}

NWB_INLINE void NWB_SIMD_CALL StoreUInt3Bits(SIMDVectorSingle value, u32* out)noexcept{
    _mm_store_sd(reinterpret_cast<f64*>(out), _mm_castps_pd(value));
    out[2] = static_cast<u32>(_mm_extract_ps(value, 2));
}
#endif

template<typename secondRow>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloat3Components(const secondRow& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return MakeF32(src.x, src.y, src.z, 0.0f);
#elif defined(NWB_HAS_NEON)
    float32x2_t xy = vld1_f32(&src.x);
    float32x2_t z0 = vdup_n_f32(0.0f);
    z0 = vld1_lane_f32(&src.z, z0, 0);
    return vcombine_f32(xy, z0);
#elif defined(NWB_HAS_SSE4)
    __m128 xy = _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(&src.x)));
    __m128 z = _mm_load_ss(&src.z);
    return _mm_movelh_ps(xy, z);
#endif
}

template<typename secondRow>
NWB_INLINE void NWB_SIMD_CALL StoreFloat3Components(SIMDVectorSingle src, secondRow& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.f[0];
    dst.y = src.f[1];
    dst.z = src.f[2];
#elif defined(NWB_HAS_NEON)
    float32x2_t xy = vget_low_f32(src);
    vst1_f32(&dst.x, xy);
    vst1q_lane_f32(&dst.z, src, 2);
#elif defined(NWB_HAS_SSE4)
    _mm_store_sd(reinterpret_cast<f64*>(&dst.x), _mm_castps_pd(src));
    _mm_store_ss(&dst.z, _mm_shuffle_ps(src, src, _MM_SHUFFLE(2, 2, 2, 2)));
#endif
}

#if defined(NWB_HAS_SCALAR)
template<typename secondRow>
NWB_INLINE void StoreFloat34Scalar(SIMDMatrix src, secondRow& dst)noexcept{
    dst._11 = src._11;
    dst._12 = src._12;
    dst._13 = src._13;
    dst._14 = src._14;

    dst._21 = src._21;
    dst._22 = src._22;
    dst._23 = src._23;
    dst._24 = src._24;

    dst._31 = src._31;
    dst._32 = src._32;
    dst._33 = src._33;
    dst._34 = src._34;
}

template<typename secondRow>
NWB_INLINE void StoreFloat44Scalar(SIMDMatrix src, secondRow& dst)noexcept{
    StoreFloat34Scalar(src, dst);

    dst._41 = src._41;
    dst._42 = src._42;
    dst._43 = src._43;
    dst._44 = src._44;
}
#endif

template<typename secondRow>
NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat34Scalar(const secondRow& src)noexcept{
    SIMDMatrix result{};
    result.v[0] = MakeF32(src._11, src._12, src._13, src._14);
    result.v[1] = MakeF32(src._21, src._22, src._23, src._24);
    result.v[2] = MakeF32(src._31, src._32, src._33, src._34);
    result.v[3] = s_SIMDIdentityR3;
    return result;
}

template<typename secondRow>
NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat44Scalar(const secondRow& src)noexcept{
    SIMDMatrix result = LoadFloat34Scalar(src);
    result.v[3] = MakeF32(src._41, src._42, src._43, src._44);
    return result;
}

#if defined(NWB_HAS_NEON)
template<typename secondRow>
NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat34Neon(const secondRow& src)noexcept{
    SIMDMatrix result;
    result.v[0] = vld1q_f32(&src._11);
    result.v[1] = vld1q_f32(&src._21);
    result.v[2] = vld1q_f32(&src._31);
    result.v[3] = s_SIMDIdentityR3;
    return result;
}

template<typename secondRow>
NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat44Neon(const secondRow& src)noexcept{
    SIMDMatrix result = LoadFloat34Neon(src);
    result.v[3] = vld1q_f32(&src._41);
    return result;
}

#endif

#if defined(NWB_HAS_SSE4)
template<bool Aligned>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadMatrixRow4(const f32* row)noexcept{
    if constexpr(Aligned)
        return _mm_load_ps(row);
    else
        return _mm_loadu_ps(row);
}

template<bool Aligned, typename secondRow>
NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat34Sse(const secondRow& src)noexcept{
    SIMDMatrix result;
    result.v[0] = LoadMatrixRow4<Aligned>(&src._11);
    result.v[1] = LoadMatrixRow4<Aligned>(&src._21);
    result.v[2] = LoadMatrixRow4<Aligned>(&src._31);
    result.v[3] = s_SIMDIdentityR3;
    return result;
}

template<bool Aligned, typename secondRow>
NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat44Sse(const secondRow& src)noexcept{
    SIMDMatrix result = LoadFloat34Sse<Aligned>(src);
    result.v[3] = LoadMatrixRow4<Aligned>(&src._41);
    return result;
}
#endif

#if defined(NWB_HAS_SCALAR)
template<typename secondRow>
NWB_INLINE void NWB_SIMD_CALL StoreInt4Scalar(SIMDVectorSingle src, secondRow& dst)noexcept{
    dst.x = static_cast<i32>(src.u[0]);
    dst.y = static_cast<i32>(src.u[1]);
    dst.z = static_cast<i32>(src.u[2]);
    dst.w = static_cast<i32>(src.u[3]);
}
#endif

#if defined(NWB_HAS_SSE4)
template<bool Aligned>
NWB_INLINE void NWB_SIMD_CALL StoreInt4Sse(SIMDVectorSingle src, i32* dst)noexcept{
    if constexpr(Aligned)
        _mm_store_si128(reinterpret_cast<__m128i*>(dst), _mm_castps_si128(src));
    else
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_castps_si128(src));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDConvertDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL MakeF64(f64 x, f64 y, f64 z, f64 w)noexcept{
#if defined(NWB_HAS_NEON64)
    const float64x2_t low = vsetq_lane_f64(y, vdupq_n_f64(x), 1);
    const float64x2_t high = vsetq_lane_f64(w, vdupq_n_f64(z), 1);
    return SIMDVectorDoubleDetail::FromPairs(low, high);
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_setr_pd(x, y, z, w));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_setr_pd(x, y), _mm_setr_pd(z, w));
#else
    return SIMDVectorDouble{ x, y, z, w };
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL MakeU64(u64 x, u64 y, u64 z, u64 w)noexcept{
#if defined(NWB_HAS_NEON64)
    const uint64x2_t low = vsetq_lane_u64(y, vdupq_n_u64(x), 1);
    const uint64x2_t high = vsetq_lane_u64(w, vdupq_n_u64(z), 1);
    return SIMDVectorDoubleDetail::FromUIntPairs(low, high);
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_set_epi64x(BitCast<i64>(w), BitCast<i64>(z), BitCast<i64>(y), BitCast<i64>(x)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromUIntPairs(_mm_set_epi64x(BitCast<i64>(y), BitCast<i64>(x)), _mm_set_epi64x(BitCast<i64>(w), BitCast<i64>(z)));
#else
    return SIMDVectorDouble{ BitCast<f64>(x), BitCast<f64>(y), BitCast<f64>(z), BitCast<f64>(w) };
#endif
}

template<usize Count>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleComponents(const f64* src)noexcept{
    static_assert(Count >= 1u && Count <= 4u);
#if defined(NWB_HAS_NEON64)
    float64x2_t low = vdupq_n_f64(0.0);
    if constexpr(Count >= 2u)
        low = vld1q_f64(src);
    else
        low = vld1q_lane_f64(src, low, 0);
    float64x2_t high = vdupq_n_f64(0.0);
    if constexpr(Count == 4u)
        high = vld1q_f64(src + 2u);
    else if constexpr(Count == 3u)
        high = vld1q_lane_f64(src + 2u, high, 0);
    return SIMDVectorDoubleDetail::FromPairs(low, high);
#elif defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_AVX)
    if constexpr(Count == 4u)
        return BitCast<SIMDVectorDouble>(_mm256_loadu_pd(src));
    else
#endif
    {
        __m128d low;
        if constexpr(Count >= 2u)
            low = _mm_loadu_pd(src);
        else
            low = _mm_load_sd(src);
        __m128d high = _mm_setzero_pd();
        if constexpr(Count == 4u)
            high = _mm_loadu_pd(src + 2u);
        else if constexpr(Count == 3u)
            high = _mm_load_sd(src + 2u);
        return SIMDVectorDoubleDetail::FromPairs(low, high);
    }
#else
    return MakeF64(src[0], Count >= 2u ? src[1] : 0.0, Count >= 3u ? src[2] : 0.0, Count == 4u ? src[3] : 0.0);
#endif
}

template<usize Count>
NWB_INLINE void NWB_SIMD_CALL StoreDoubleComponents(SIMDVectorDouble src, f64* dst)noexcept{
    static_assert(Count >= 1u && Count <= 4u);
#if defined(NWB_HAS_NEON64)
    if constexpr(Count >= 2u)
        vst1q_f64(dst, SIMDVectorDoubleDetail::GetLow(src));
    else
        vst1q_lane_f64(dst, SIMDVectorDoubleDetail::GetLow(src), 0);
    if constexpr(Count == 4u)
        vst1q_f64(dst + 2u, SIMDVectorDoubleDetail::GetHigh(src));
    else if constexpr(Count == 3u)
        vst1q_lane_f64(dst + 2u, SIMDVectorDoubleDetail::GetHigh(src), 0);
#elif defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_AVX)
    if constexpr(Count == 4u)
        _mm256_storeu_pd(dst, BitCast<__m256d>(src));
    else
#endif
    {
        if constexpr(Count >= 2u)
            _mm_storeu_pd(dst, SIMDVectorDoubleDetail::GetLow(src));
        else
            _mm_store_sd(dst, SIMDVectorDoubleDetail::GetLow(src));
        if constexpr(Count == 4u)
            _mm_storeu_pd(dst + 2u, SIMDVectorDoubleDetail::GetHigh(src));
        else if constexpr(Count == 3u)
            _mm_store_sd(dst + 2u, SIMDVectorDoubleDetail::GetHigh(src));
    }
#else
    for(usize component = 0u; component < Count; ++component)
        dst[component] = src[component];
#endif
}

template<usize Count, typename IntegerT>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadIntComponents(const IntegerT* src)noexcept{
    static_assert(Count >= 1u && Count <= 4u);
    static_assert(IsSame_V<IntegerT, i64> || IsSame_V<IntegerT, u64>);
#if defined(NWB_HAS_NEON64)
    const u64* bits = reinterpret_cast<const u64*>(src);
    uint64x2_t low = vdupq_n_u64(0u);
    if constexpr(Count >= 2u)
        low = vld1q_u64(bits);
    else
        low = vld1q_lane_u64(bits, low, 0);
    uint64x2_t high = vdupq_n_u64(0u);
    if constexpr(Count == 4u)
        high = vld1q_u64(bits + 2u);
    else if constexpr(Count == 3u)
        high = vld1q_lane_u64(bits + 2u, high, 0);
    return SIMDVectorDoubleDetail::FromUIntPairs(low, high);
#elif defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_AVX)
    if constexpr(Count == 4u)
        return BitCast<SIMDVectorDouble>(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(src)));
    else
#endif
    {
        __m128i low;
        if constexpr(Count >= 2u)
            low = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src));
        else
            low = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(src));
        __m128i high = _mm_setzero_si128();
        if constexpr(Count == 4u)
            high = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + 2u));
        else if constexpr(Count == 3u)
            high = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(src + 2u));
        return SIMDVectorDoubleDetail::FromUIntPairs(low, high);
    }
#else
    return MakeU64(BitCast<u64>(src[0]), Count >= 2u ? BitCast<u64>(src[1]) : 0u, Count >= 3u ? BitCast<u64>(src[2]) : 0u, Count == 4u ? BitCast<u64>(src[3]) : 0u);
#endif
}

template<usize Count, typename IntegerT>
NWB_INLINE void NWB_SIMD_CALL StoreIntComponents(SIMDVectorDouble src, IntegerT* dst)noexcept{
    static_assert(Count >= 1u && Count <= 4u);
    static_assert(IsSame_V<IntegerT, i64> || IsSame_V<IntegerT, u64>);
#if defined(NWB_HAS_NEON64)
    u64* bits = reinterpret_cast<u64*>(dst);
    if constexpr(Count >= 2u)
        vst1q_u64(bits, SIMDVectorDoubleDetail::GetLowBits(src));
    else
        vst1q_lane_u64(bits, SIMDVectorDoubleDetail::GetLowBits(src), 0);
    if constexpr(Count == 4u)
        vst1q_u64(bits + 2u, SIMDVectorDoubleDetail::GetHighBits(src));
    else if constexpr(Count == 3u)
        vst1q_lane_u64(bits + 2u, SIMDVectorDoubleDetail::GetHighBits(src), 0);
#elif defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_AVX)
    if constexpr(Count == 4u)
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst), BitCast<__m256i>(src));
    else
#endif
    {
        if constexpr(Count >= 2u)
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), SIMDVectorDoubleDetail::GetLowBits(src));
        else
            _mm_storel_epi64(reinterpret_cast<__m128i*>(dst), SIMDVectorDoubleDetail::GetLowBits(src));
        if constexpr(Count == 4u)
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + 2u), SIMDVectorDoubleDetail::GetHighBits(src));
        else if constexpr(Count == 3u)
            _mm_storel_epi64(reinterpret_cast<__m128i*>(dst + 2u), SIMDVectorDoubleDetail::GetHighBits(src));
    }
#else
    for(usize component = 0u; component < Count; ++component)
        dst[component] = BitCast<IntegerT>(src[component]);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble2Components(SIMDVectorDouble src, f64* dst)noexcept{
#if defined(NWB_HAS_NEON64)
    __builtin_nontemporal_store(SIMDVectorDoubleDetail::GetLow(src), reinterpret_cast<float64x2_t*>(dst));
#elif defined(NWB_HAS_SSE2)
    _mm_stream_pd(dst, SIMDVectorDoubleDetail::GetLow(src));
#else
    StoreDoubleComponents<2u>(src, dst);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StreamDoubleHighComponent(SIMDVectorDouble src, f64* dst)noexcept{
#if defined(NWB_HAS_NEON64)
    __builtin_nontemporal_store(vgetq_lane_f64(SIMDVectorDoubleDetail::GetHigh(src), 0), dst);
#elif defined(NWB_HAS_SSE2)
    _mm_stream_si64(reinterpret_cast<long long*>(dst), _mm_cvtsi128_si64(SIMDVectorDoubleDetail::GetHighBits(src)));
#else
    *dst = src[2];
#endif
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble4Components(SIMDVectorDouble src, f64* dst)noexcept{
#if defined(NWB_HAS_NEON64)
    __builtin_nontemporal_store(SIMDVectorDoubleDetail::GetLow(src), reinterpret_cast<float64x2_t*>(dst));
    __builtin_nontemporal_store(SIMDVectorDoubleDetail::GetHigh(src), reinterpret_cast<float64x2_t*>(dst + 2u));
#elif defined(NWB_HAS_AVX)
    _mm256_stream_pd(dst, BitCast<__m256d>(src));
#elif defined(NWB_HAS_SSE2)
    _mm_stream_pd(dst, SIMDVectorDoubleDetail::GetLow(src));
    _mm_stream_pd(dst + 2u, SIMDVectorDoubleDetail::GetHigh(src));
#else
    StoreDoubleComponents<4u>(src, dst);
#endif
}

template<typename MatrixT>
NWB_INLINE void NWB_SIMD_CALL StoreDouble33Rows(SIMDMatrixDouble src, MatrixT& dst)noexcept{
    for(usize row = 0u; row < 3u; ++row)
        StoreDoubleComponents<3u>(src.v[row], dst.raw + row * 3u);
}

template<typename MatrixT>
NWB_INLINE void NWB_SIMD_CALL StoreDouble34Rows(SIMDMatrixDouble src, MatrixT& dst)noexcept{
    for(usize row = 0u; row < 3u; ++row)
        StoreDoubleComponents<4u>(src.v[row], dst.raw + row * 4u);
}

template<typename MatrixT>
NWB_INLINE void NWB_SIMD_CALL StoreDouble44Rows(SIMDMatrixDouble src, MatrixT& dst)noexcept{
    for(usize row = 0u; row < 4u; ++row)
        StoreDoubleComponents<4u>(src.v[row], dst.raw + row * 4u);
}

template<typename MatrixT>
NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble33Rows(const MatrixT& src)noexcept{
    SIMDMatrixDouble result{};
    for(usize row = 0u; row < 3u; ++row)
        result.v[row] = LoadDoubleComponents<3u>(src.raw + row * 3u);
    result.v[3] = s_DoubleSIMDIdentityR3;
    return result;
}

template<typename MatrixT>
NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble34Rows(const MatrixT& src)noexcept{
    SIMDMatrixDouble result{};
    for(usize row = 0u; row < 3u; ++row)
        result.v[row] = LoadDoubleComponents<4u>(src.raw + row * 4u);
    result.v[3] = s_DoubleSIMDIdentityR3;
    return result;
}

template<typename MatrixT>
NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble44Rows(const MatrixT& src)noexcept{
    SIMDMatrixDouble result{};
    for(usize row = 0u; row < 4u; ++row)
        result.v[row] = LoadDoubleComponents<4u>(src.raw + row * 4u);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<u32 MANTISSA_BIT_COUNT>
[[nodiscard]] NWB_INLINE u32 ConvertFloatToUnsignedFloat(const f32 value)noexcept{
    static_assert(MANTISSA_BIT_COUNT > 0u, "Unsigned floating-point values need a mantissa");
    static_assert(MANTISSA_BIT_COUNT <= UnsignedFloatConvertDetail::s_MaxMantissaBitCount, "Unsupported unsigned floating-point mantissa width");

    u32 bits = BitCast<u32>(value);
    const u32 absBits = bits & UnsignedFloatConvertDetail::s_F32AbsoluteBitMask;
    const u32 targetInf = UnsignedFloatConvertDetail::s_ExponentFieldMask << MANTISSA_BIT_COUNT;
    const u32 targetMantissaMask = (1u << MANTISSA_BIT_COUNT) - 1u;

    if(absBits > UnsignedFloatConvertDetail::s_F32InfinityBits){
        const u32 payload = (absBits >> (UnsignedFloatConvertDetail::s_F32MantissaBitCount - MANTISSA_BIT_COUNT)) & targetMantissaMask;
        return targetInf | (payload != 0u ? payload : 1u);
    }
    if((bits & UnsignedFloatConvertDetail::s_F32SignBitMask) != 0u || absBits == 0u)
        return 0u;

    bits = absBits;
    if(bits >= UnsignedFloatConvertDetail::s_OverflowBits)
        return targetInf;

    const u32 zeroThreshold = (
        UnsignedFloatConvertDetail::s_F32ExponentBias
        - UnsignedFloatConvertDetail::s_ExponentBias
        - MANTISSA_BIT_COUNT
    ) << UnsignedFloatConvertDetail::s_F32MantissaBitCount;
    if(bits <= zeroThreshold)
        return 0u;

    if(bits < UnsignedFloatConvertDetail::s_MinNormalBits){
        const u32 sourceExponent = bits >> UnsignedFloatConvertDetail::s_F32MantissaBitCount;
        const u32 roundShift = (UnsignedFloatConvertDetail::s_F32ExponentBias - 1u)
            - sourceExponent
            + (UnsignedFloatConvertDetail::s_MaxMantissaBitCount - MANTISSA_BIT_COUNT);
        bits = UnsignedFloatConvertDetail::s_F32ImplicitMantissaBit | (bits & UnsignedFloatConvertDetail::s_F32MantissaBitMask);
        u32 result = bits >> roundShift;
        const u32 sticky = (bits & ((1u << (roundShift - 1u)) - 1u)) != 0u ? 1u : 0u;
        result += (result | sticky) & ((bits >> (roundShift - 1u)) & 1u);
        return result;
    }

    const u32 shift = UnsignedFloatConvertDetail::s_F32MantissaBitCount - MANTISSA_BIT_COUNT;
    bits += UnsignedFloatConvertDetail::s_NormalRoundingBiasBits;
    return (
        (bits + ((1u << (shift - 1u)) - 1u) + ((bits >> shift) & 1u)) >> shift
    ) & ((1u << (UnsignedFloatConvertDetail::s_ExponentBitCount + MANTISSA_BIT_COUNT)) - 1u);
}

[[nodiscard]] NWB_INLINE Half ConvertFloatToHalf(const f32 value)noexcept{
#if defined(NWB_HAS_F16C)
    return HalfConvertDetail::FloatToHalfF16C(value);
#else
    return HalfConvertDetail::FloatToHalfScalar(value);
#endif
}

[[nodiscard]] NWB_INLINE f32 ConvertHalfToFloat(const Half value)noexcept{
#if defined(NWB_HAS_F16C)
    return HalfConvertDetail::HalfToFloatF16C(value);
#else
    return HalfConvertDetail::HalfToFloatScalar(value);
#endif
}

NWB_INLINE Half* ConvertFloatBufferToHalf(Half* outHalfBuffer, const f32* floatBuffer, const usize count)noexcept{
#if defined(NWB_HAS_F16C)
    return HalfConvertDetail::FloatBufferToHalfF16C(outHalfBuffer, floatBuffer, count);
#else
    for(usize i = 0u; i < count; ++i)
        outHalfBuffer[i] = ConvertFloatToHalf(floatBuffer[i]);
    return outHalfBuffer;
#endif
}

NWB_INLINE f32* ConvertHalfBufferToFloat(f32* outFloatBuffer, const Half* halfBuffer, const usize count)noexcept{
#if defined(NWB_HAS_F16C)
    return HalfConvertDetail::HalfBufferToFloatF16C(outFloatBuffer, halfBuffer, count);
#else
    for(usize i = 0u; i < count; ++i)
        outFloatBuffer[i] = ConvertHalfToFloat(halfBuffer[i]);
    return outFloatBuffer;
#endif
}

[[nodiscard]] NWB_INLINE Half2U MakeHalf2U(const f32 x, const f32 y)noexcept{
    return Half2U(ConvertFloatToHalf(x), ConvertFloatToHalf(y));
}

[[nodiscard]] NWB_INLINE Half4U MakeHalf4U(const f32 x, const f32 y, const f32 z, const f32 w)noexcept{
    const Float4U values(x, y, z, w);
    Half4U result;
    ConvertFloatBufferToHalf(result.raw, values.raw, HalfConvertDetail::s_F16CVectorElementCount);
    return result;
}

[[nodiscard]] NWB_INLINE Float2U LoadHalf2U(const Half2U& value)noexcept{
    return Float2U(ConvertHalfToFloat(value.x), ConvertHalfToFloat(value.y));
}

[[nodiscard]] NWB_INLINE Float4U LoadHalf4U(const Half4U& value)noexcept{
    Float4U result{};
    ConvertHalfBufferToFloat(result.raw, value.raw, HalfConvertDetail::s_F16CVectorElementCount);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL ConvertVectorSingleToDouble(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(vcvt_f64_f32(vget_low_f32(value)), vcvt_f64_f32(vget_high_f32(value)));
#elif defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_SCALAR)
    const __m128 lanes = _mm_loadu_ps(value.f);
#else
    const __m128 lanes = value;
#endif
#if defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_cvtps_pd(lanes));
#else
    return SIMDVectorDoubleDetail::FromPairs(_mm_cvtps_pd(lanes), _mm_cvtps_pd(_mm_movehl_ps(lanes, lanes)));
#endif
#elif defined(NWB_HAS_NEON)
    return SIMDConvertDoubleDetail::MakeF64(vgetq_lane_f32(value, 0), vgetq_lane_f32(value, 1), vgetq_lane_f32(value, 2), vgetq_lane_f32(value, 3));
#else
    return SIMDConvertDoubleDetail::MakeF64(value.f[0], value.f[1], value.f[2], value.f[3]);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL ConvertVectorDoubleToSingle(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_NEON64)
    return vcombine_f32(vcvt_f32_f64(SIMDVectorDoubleDetail::GetLow(value)), vcvt_f32_f64(SIMDVectorDoubleDetail::GetHigh(value)));
#elif defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_AVX)
    const __m128 lanes = _mm256_cvtpd_ps(BitCast<__m256d>(value));
#else
    const __m128 lanes = _mm_movelh_ps(_mm_cvtpd_ps(SIMDVectorDoubleDetail::GetLow(value)), _mm_cvtpd_ps(SIMDVectorDoubleDetail::GetHigh(value)));
#endif
#if defined(NWB_HAS_SCALAR)
    SIMDVectorSingle result{};
    _mm_storeu_ps(result.f, lanes);
    return result;
#else
    return lanes;
#endif
#else
    return SIMDConvertDetail::MakeF32(static_cast<f32>(value[0]), static_cast<f32>(value[1]), static_cast<f32>(value[2]), static_cast<f32>(value[3]));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadHalf(const Half2U& value)noexcept{
#if defined(NWB_HAS_F16C)
    return _mm_cvtph_ps(_mm_cvtsi32_si128(static_cast<i32>(value.packed)));
#else
    return SIMDConvertDetail::MakeF32(ConvertHalfToFloat(value.x), ConvertHalfToFloat(value.y), 0.0f, 0.0f);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadHalf(const Half4U& value)noexcept{
#if defined(NWB_HAS_F16C)
    const __m128i halfValue = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(value.raw));
    return _mm_cvtph_ps(halfValue);
#else
    return SIMDConvertDetail::MakeF32(
        ConvertHalfToFloat(value.x),
        ConvertHalfToFloat(value.y),
        ConvertHalfToFloat(value.z),
        ConvertHalfToFloat(value.w)
    );
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Half2U& src)noexcept{
    return SIMDConvertDoubleDetail::MakeF64(ConvertHalfToFloat(src.x), ConvertHalfToFloat(src.y), 0.0, 0.0);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Half4U& src)noexcept{
    return SIMDConvertDoubleDetail::MakeF64(ConvertHalfToFloat(src.x), ConvertHalfToFloat(src.y), ConvertHalfToFloat(src.z), ConvertHalfToFloat(src.w));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// SIMD types are calculation values; persistent data uses typed layouts via these boundaries.
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloat(const Float4& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(src.x, src.y, src.z, src.w);
#elif defined (NWB_HAS_NEON)
    return vld1q_f32(src.raw);
#elif defined(NWB_HAS_SSE4)
    return _mm_load_ps(src.raw);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double4& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<4u>(src.raw);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloat(f32 src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(src, 0.0f, 0.0f, 0.0f);
#elif defined (NWB_HAS_NEON)
    uint32x4_t zero = vdupq_n_u32(0);
    return vreinterpretq_f32_u32(vld1q_lane_u32(&src, zero, 0));
#elif defined(NWB_HAS_SSE4)
    return _mm_load_ss(&src);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(f64 src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<1u>(&src);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloat(const Float2U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(src.x, src.y, 0.0f, 0.0f);
#elif defined (NWB_HAS_NEON)
    float32x2_t x = vld1_f32(src.raw);
    float32x2_t zero = vdup_n_f32(0);
    return vcombine_f32(x, zero);
#elif defined(NWB_HAS_SSE4)
    return _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(src.raw)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double2U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<2u>(src.raw);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloat(const Float3U& src)noexcept{
    return SIMDConvertDetail::LoadFloat3Components(src);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double3U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<3u>(src.raw);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloat(const Float4U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(src.x, src.y, src.z, src.w);
#elif defined (NWB_HAS_NEON)
    return vld1q_f32(src.raw);
#elif defined(NWB_HAS_SSE4)
    return _mm_loadu_ps(src.raw);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double4U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<4u>(src.raw);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double2& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<2u>(src.raw);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double3& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<3u>(src.raw);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloatInt(const Float3Int& src)noexcept{
    return SIMDConvertDetail::LoadFloat3Components(src);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Double3Int& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<3u>(&src.x);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloatInt(const Float3UInt& src)noexcept{
    return SIMDConvertDetail::LoadFloat3Components(src);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Double3UInt& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<3u>(&src.x);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat(const Float34& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::LoadFloat34Scalar(src);
#elif defined (NWB_HAS_NEON)
    return SIMDConvertDetail::LoadFloat34Neon(src);
#elif defined(NWB_HAS_SSE4)
    return SIMDConvertDetail::LoadFloat34Sse<true>(src);
#endif
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double34& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble34Rows(src);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat(const Float44& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::LoadFloat44Scalar(src);
#elif defined (NWB_HAS_NEON)
    return SIMDConvertDetail::LoadFloat44Neon(src);
#elif defined(NWB_HAS_SSE4)
    return SIMDConvertDetail::LoadFloat44Sse<true>(src);
#endif
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double44& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble44Rows(src);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat(const Float33U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDMatrix result{};
    result.v[0] = SIMDConvertDetail::MakeF32(src._11, src._12, src._13, 0.0f);
    result.v[1] = SIMDConvertDetail::MakeF32(src._21, src._22, src._23, 0.0f);
    result.v[2] = SIMDConvertDetail::MakeF32(src._31, src._32, src._33, 0.0f);
    result.v[3] = s_SIMDIdentityR3;
    return result;
#elif defined (NWB_HAS_NEON)
    float32x4_t v0 = vld1q_f32(&src.m[0][0]);
    float32x4_t v1 = vld1q_f32(&src.m[1][1]);
    float32x2_t v2 = vcreate_f32(static_cast<u64>(BitCast<u32>(src.m[2][2])));
    float32x4_t secondRow = vextq_f32(v0, v1, 3);

    SIMDMatrix matrix;
    matrix.v[0] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(v0), s_SIMDMask3));
    matrix.v[1] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(secondRow), s_SIMDMask3));
    matrix.v[2] = vcombine_f32(vget_high_f32(v1), v2);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
#elif defined(NWB_HAS_SSE4)
    __m128 zero = _mm_setzero_ps();

    __m128 firstValues = _mm_loadu_ps(&src.m[0][0]);
    __m128 middleValues = _mm_loadu_ps(&src.m[1][1]);
    __m128 lastValue = _mm_load_ss(&src.m[2][2]);

    __m128 firstHighValues = _mm_unpackhi_ps(firstValues, zero);
    __m128 middleLowValues = _mm_unpacklo_ps(middleValues, zero);
    __m128 shuffledLastValue = _mm_shuffle_ps(lastValue, middleLowValues, _MM_SHUFFLE(0, 1, 0, 0));
    __m128 secondRowTail = _mm_movehl_ps(middleLowValues, shuffledLastValue);
    __m128 secondRowHead = _mm_movehl_ps(zero, firstHighValues);

    SIMDMatrix matrix;
    matrix.v[0] = _mm_movelh_ps(firstValues, firstHighValues);
    matrix.v[1] = _mm_add_ps(secondRowTail, secondRowHead);
    matrix.v[2] = _mm_shuffle_ps(middleValues, lastValue, _MM_SHUFFLE(1, 0, 3, 2));
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
#endif
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double33U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble33Rows(src);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat(const Float34U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::LoadFloat34Scalar(src);
#elif defined (NWB_HAS_NEON)
    return SIMDConvertDetail::LoadFloat34Neon(src);
#elif defined(NWB_HAS_SSE4)
    return SIMDConvertDetail::LoadFloat34Sse<false>(src);
#endif
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double34U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble34Rows(src);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat(const Float44U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::LoadFloat44Scalar(src);
#elif defined (NWB_HAS_NEON)
    return SIMDConvertDetail::LoadFloat44Neon(src);
#elif defined(NWB_HAS_SSE4)
    return SIMDConvertDetail::LoadFloat44Sse<false>(src);
#endif
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double44U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble44Rows(src);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double33& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble33Rows(src);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const Int4& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(static_cast<u32>(src.x), static_cast<u32>(src.y), static_cast<u32>(src.z), static_cast<u32>(src.w));
#elif defined (NWB_HAS_NEON)
    return vreinterpretq_f32_s32(vld1q_s32(src.raw));
#elif defined(NWB_HAS_SSE4)
    __m128i vector = _mm_load_si128(reinterpret_cast<const __m128i*>(src.raw));
    return _mm_castsi128_ps(vector);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Int64x4& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<4u>(src.raw);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(i32 src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(static_cast<u32>(src), 0, 0, 0);
#elif defined (NWB_HAS_NEON)
    int32x4_t v = vdupq_n_s32(0);
    v = vsetq_lane_s32(src, v, 0);
    return vreinterpretq_f32_s32(v);
#elif defined(NWB_HAS_SSE4)
    return _mm_castsi128_ps(_mm_cvtsi32_si128(src));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(i64 src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<1u>(&src);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const Int2U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(static_cast<u32>(src.x), static_cast<u32>(src.y), 0, 0);
#elif defined (NWB_HAS_NEON)
    int32x2_t x = vld1_s32(src.raw);
    int32x2_t zero = vdup_n_s32(0);
    return vreinterpretq_f32_s32(vcombine_s32(x, zero));
#elif defined(NWB_HAS_SSE4)
    return _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(src.raw)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Int64x2U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<2u>(src.raw);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const Int3U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(static_cast<u32>(src.x), static_cast<u32>(src.y), static_cast<u32>(src.z), 0);
#elif defined (NWB_HAS_NEON)
    int32x2_t x = vld1_s32(src.raw);
    int32x2_t zero = vdup_n_s32(0);
    int32x2_t y = vld1_lane_s32(src.raw + 2, zero, 0);
    return vreinterpretq_f32_s32(vcombine_s32(x, y));
#elif defined(NWB_HAS_SSE4)
    __m128 xy = _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(src.raw)));
    __m128 z = _mm_load_ss(reinterpret_cast<const f32*>(src.raw + 2));
    return _mm_movelh_ps(xy, z);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Int64x3U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<3u>(src.raw);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const Int4U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(static_cast<u32>(src.x), static_cast<u32>(src.y), static_cast<u32>(src.z), static_cast<u32>(src.w));
#elif defined (NWB_HAS_NEON)
    return vreinterpretq_f32_s32(vld1q_s32(src.raw));
#elif defined(NWB_HAS_SSE4)
    __m128i vector = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src.raw));
    return _mm_castsi128_ps(vector);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Int64x4U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<4u>(src.raw);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const UInt4& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(src.x, src.y, src.z, src.w);
#elif defined (NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vld1q_u32(src.raw));
#elif defined(NWB_HAS_SSE4)
    __m128i vector = _mm_load_si128(reinterpret_cast<const __m128i*>(src.raw));
    return _mm_castsi128_ps(vector);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const UInt64x4& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<4u>(src.raw);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(u32 src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(src, 0, 0, 0);
#elif defined (NWB_HAS_NEON)
    uint32x4_t v = vdupq_n_u32(0);
    v = vsetq_lane_u32(src, v, 0);
    return vreinterpretq_f32_u32(v);
#elif defined(NWB_HAS_SSE4)
    return _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<i32>(src)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(u64 src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<1u>(&src);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const UInt2U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(src.x, src.y, 0, 0);
#elif defined (NWB_HAS_NEON)
    uint32x2_t x = vld1_u32(src.raw);
    uint32x2_t zero = vdup_n_u32(0);
    return vreinterpretq_f32_u32(vcombine_u32(x, zero));
#elif defined(NWB_HAS_SSE4)
    return _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(src.raw)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const UInt64x2U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<2u>(src.raw);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const UInt3U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(src.x, src.y, src.z, 0);
#elif defined (NWB_HAS_NEON)
    uint32x2_t x = vld1_u32(src.raw);
    uint32x2_t zero = vdup_n_u32(0);
    uint32x2_t y = vld1_lane_u32(src.raw + 2, zero, 0);
    uint32x4_t v = vcombine_u32(x, y);
    return vreinterpretq_f32_u32(v);
#elif defined(NWB_HAS_SSE4)
    __m128 xy = _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(src.raw)));
    __m128 z = _mm_load_ss(reinterpret_cast<const f32*>(src.raw + 2));
    return _mm_movelh_ps(xy, z);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const UInt64x3U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<3u>(src.raw);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const UInt4U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(src.x, src.y, src.z, src.w);
#elif defined (NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vld1q_u32(src.raw));
#elif defined(NWB_HAS_SSE4)
    __m128i vector = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src.raw));
    return _mm_castsi128_ps(vector);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const UInt64x4U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<4u>(src.raw);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDVectorSingle src, Float4& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.f[0];
    dst.y = src.f[1];
    dst.z = src.f[2];
    dst.w = src.f[3];
#elif defined (NWB_HAS_NEON)
    vst1q_f32(dst.raw, src);
#elif defined(NWB_HAS_SSE4)
    _mm_store_ps(dst.raw, src);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double4& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<4u>(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDVectorSingle src, f32& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst = src.f[0];
#elif defined (NWB_HAS_NEON)
    vst1q_lane_f32(&dst, src, 0);
#elif defined(NWB_HAS_SSE4)
    _mm_store_ss(&dst, src);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, f64& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<1u>(src, &dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDVectorSingle src, Float2U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.f[0];
    dst.y = src.f[1];
#elif defined (NWB_HAS_NEON)
    float32x2_t lowValues = vget_low_f32(src);
    vst1_f32(dst.raw, lowValues);
#elif defined(NWB_HAS_SSE4)
    _mm_store_sd(reinterpret_cast<f64*>(&dst), _mm_castps_pd(src));
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double2U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<2u>(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDVectorSingle src, Float3U& dst)noexcept{
    SIMDConvertDetail::StoreFloat3Components(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double3U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<3u>(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDVectorSingle src, Float4U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.f[0];
    dst.y = src.f[1];
    dst.z = src.f[2];
    dst.w = src.f[3];
#elif defined (NWB_HAS_NEON)
    vst1q_f32(dst.raw, src);
#elif defined(NWB_HAS_SSE4)
    _mm_storeu_ps(dst.raw, src);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double4U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<4u>(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double2& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<2u>(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double3& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<3u>(src, dst.raw);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL StoreFloatInt(SIMDVectorSingle src, i32 w, Float3Int& dst)noexcept{
    SIMDConvertDetail::StoreFloat3Components(src, dst);
    dst.w = w;
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, i64 w, Double3Int& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<3u>(src, &dst.x);
    dst.w = w;
}

NWB_INLINE void NWB_SIMD_CALL StoreFloatInt(SIMDVectorSingle src, u32 w, Float3UInt& dst)noexcept{
    SIMDConvertDetail::StoreFloat3Components(src, dst);
    dst.w = w;
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, u64 w, Double3UInt& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<3u>(src, &dst.x);
    dst.w = w;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDMatrix src, Float34& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreFloat34Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_f32(&dst._11, src.v[0]);
    vst1q_f32(&dst._21, src.v[1]);
    vst1q_f32(&dst._31, src.v[2]);
#elif defined(NWB_HAS_SSE4)
    _mm_store_ps(&dst._11, src.v[0]);
    _mm_store_ps(&dst._21, src.v[1]);
    _mm_store_ps(&dst._31, src.v[2]);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double34& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble34Rows(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDMatrix src, Float44& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreFloat44Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_f32(&dst._11, src.v[0]);
    vst1q_f32(&dst._21, src.v[1]);
    vst1q_f32(&dst._31, src.v[2]);
    vst1q_f32(&dst._41, src.v[3]);
#elif defined(NWB_HAS_SSE4)
    _mm_store_ps(&dst._11, src.v[0]);
    _mm_store_ps(&dst._21, src.v[1]);
    _mm_store_ps(&dst._31, src.v[2]);
    _mm_store_ps(&dst._41, src.v[3]);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double44& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble44Rows(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDMatrix src, Float33U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst._11 = src._11;
    dst._12 = src._12;
    dst._13 = src._13;

    dst._21 = src._21;
    dst._22 = src._22;
    dst._23 = src._23;

    dst._31 = src._31;
    dst._32 = src._32;
    dst._33 = src._33;
#elif defined (NWB_HAS_NEON)
    float32x4_t shiftedValues = vextq_f32(src.v[0], src.v[1], 1);
    float32x4_t packedValues = vbslq_f32(s_SIMDMask3, src.v[0], shiftedValues);
    vst1q_f32(&dst.m[0][0], packedValues);

    shiftedValues = vextq_f32(src.v[1], src.v[1], 1);
    packedValues = vcombine_f32(vget_low_f32(shiftedValues), vget_low_f32(src.v[2]));
    vst1q_f32(&dst.m[1][1], packedValues);

    vst1q_lane_f32(&dst.m[2][2], src.v[2], 2);
#elif defined(NWB_HAS_SSE4)
    SIMDVectorSingle vTemp1 = src.v[0];
    SIMDVectorSingle vTemp2 = src.v[1];
    SIMDVectorSingle vTemp3 = src.v[2];
    SIMDVectorSingle vWork = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(0, 0, 2, 2));
    vTemp1 = _mm_shuffle_ps(vTemp1, vWork, _MM_SHUFFLE(2, 0, 1, 0));
    _mm_storeu_ps(&dst.m[0][0], vTemp1);
    vTemp2 = _mm_shuffle_ps(vTemp2, vTemp3, _MM_SHUFFLE(1, 0, 2, 1));
    _mm_storeu_ps(&dst.m[1][1], vTemp2);
    vTemp3 = _mm_shuffle_ps(vTemp3, vTemp3, _MM_SHUFFLE(2, 2, 2, 2));
    _mm_store_ss(&dst.m[2][2], vTemp3);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double33U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble33Rows(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDMatrix src, Float34U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreFloat34Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_f32(&dst._11, src.v[0]);
    vst1q_f32(&dst._21, src.v[1]);
    vst1q_f32(&dst._31, src.v[2]);
#elif defined(NWB_HAS_SSE4)
    _mm_storeu_ps(&dst._11, src.v[0]);
    _mm_storeu_ps(&dst._21, src.v[1]);
    _mm_storeu_ps(&dst._31, src.v[2]);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double34U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble34Rows(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDMatrix src, Float44U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreFloat44Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_f32(&dst._11, src.v[0]);
    vst1q_f32(&dst._21, src.v[1]);
    vst1q_f32(&dst._31, src.v[2]);
    vst1q_f32(&dst._41, src.v[3]);
#elif defined(NWB_HAS_SSE4)
    _mm_storeu_ps(&dst._11, src.v[0]);
    _mm_storeu_ps(&dst._21, src.v[1]);
    _mm_storeu_ps(&dst._31, src.v[2]);
    _mm_storeu_ps(&dst._41, src.v[3]);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double44U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble44Rows(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double33& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble33Rows(src, dst);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, Int4& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreInt4Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_s32(dst.raw, vreinterpretq_s32_f32(src));
#elif defined(NWB_HAS_SSE4)
    SIMDConvertDetail::StoreInt4Sse<true>(src, dst.raw);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, Int64x4& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<4u>(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, i32& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst = static_cast<i32>(src.u[0]);
#elif defined (NWB_HAS_NEON)
    vst1q_lane_s32(&dst, vreinterpretq_s32_f32(src), 0);
#elif defined(NWB_HAS_SSE4)
    _mm_store_ss(reinterpret_cast<f32*>(&dst), src);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, i64& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<1u>(src, &dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, Int2U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = static_cast<i32>(src.u[0]);
    dst.y = static_cast<i32>(src.u[1]);
#elif defined (NWB_HAS_NEON)
    vst1_s32(dst.raw, vget_low_s32(vreinterpretq_s32_f32(src)));
#elif defined(NWB_HAS_SSE4)
    _mm_store_sd(reinterpret_cast<f64*>(dst.raw), _mm_castps_pd(src));
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, Int64x2U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<2u>(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, Int3U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = static_cast<i32>(src.u[0]);
    dst.y = static_cast<i32>(src.u[1]);
    dst.z = static_cast<i32>(src.u[2]);
#elif defined (NWB_HAS_NEON)
    int32x4_t value = vreinterpretq_s32_f32(src);
    vst1_s32(dst.raw, vget_low_s32(value));
    vst1q_lane_s32(dst.raw + 2, value, 2);
#elif defined(NWB_HAS_SSE4)
    SIMDConvertDetail::StoreInt3Bits(src, dst.raw);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, Int64x3U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<3u>(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, Int4U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreInt4Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_s32(dst.raw, vreinterpretq_s32_f32(src));
#elif defined(NWB_HAS_SSE4)
    SIMDConvertDetail::StoreInt4Sse<false>(src, dst.raw);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, Int64x4U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<4u>(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, UInt4& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.u[0];
    dst.y = src.u[1];
    dst.z = src.u[2];
    dst.w = src.u[3];
#elif defined (NWB_HAS_NEON)
    vst1q_u32(dst.raw, vreinterpretq_u32_f32(src));
#elif defined(NWB_HAS_SSE4)
    _mm_store_si128(reinterpret_cast<__m128i*>(dst.raw), _mm_castps_si128(src));
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, UInt64x4& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<4u>(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, u32& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst = src.u[0];
#elif defined (NWB_HAS_NEON)
    vst1q_lane_u32(&dst, vreinterpretq_u32_f32(src), 0);
#elif defined(NWB_HAS_SSE4)
    _mm_store_ss(reinterpret_cast<f32*>(&dst), src);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, u64& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<1u>(src, &dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, UInt2U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.u[0];
    dst.y = src.u[1];
#elif defined (NWB_HAS_NEON)
    vst1_u32(dst.raw, vget_low_u32(vreinterpretq_u32_f32(src)));
#elif defined(NWB_HAS_SSE4)
    _mm_store_sd(reinterpret_cast<f64*>(dst.raw), _mm_castps_pd(src));
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, UInt64x2U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<2u>(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, UInt3U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.u[0];
    dst.y = src.u[1];
    dst.z = src.u[2];
#elif defined (NWB_HAS_NEON)
    uint32x4_t value = vreinterpretq_u32_f32(src);
    vst1_u32(dst.raw, vget_low_u32(value));
    vst1q_lane_u32(dst.raw + 2, value, 2);
#elif defined(NWB_HAS_SSE4)
    SIMDConvertDetail::StoreUInt3Bits(src, dst.raw);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, UInt64x3U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<3u>(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, UInt4U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.u[0];
    dst.y = src.u[1];
    dst.z = src.u[2];
    dst.w = src.u[3];
#elif defined (NWB_HAS_NEON)
    vst1q_u32(dst.raw, vreinterpretq_u32_f32(src));
#elif defined(NWB_HAS_SSE4)
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst.raw), _mm_castps_si128(src));
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, UInt64x4U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<4u>(src, dst.raw);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void StreamFloatFence()noexcept{
#if defined(NWB_HAS_SSE4)
    _mm_sfence();
#endif
}

NWB_INLINE void StreamDoubleFence()noexcept{
#if defined(NWB_HAS_SSE2)
    _mm_sfence();
#elif defined(NWB_HAS_NEON)
    __builtin_arm_dmb(0xB);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL StreamFloat(SIMDVectorSingle src, Float4& dst)noexcept{
#if defined(NWB_HAS_SSE4)
    _mm_stream_ps(dst.raw, src);
#else
    StoreFloat(src, dst);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDVectorDouble src, Double4& dst)noexcept{
    SIMDConvertDoubleDetail::StreamDouble4Components(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDVectorDouble src, Double2& dst)noexcept{
    SIMDConvertDoubleDetail::StreamDouble2Components(src, dst.raw);
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDVectorDouble src, Double3& dst)noexcept{
    SIMDConvertDoubleDetail::StreamDouble2Components(src, dst.raw);
    SIMDConvertDoubleDetail::StreamDoubleHighComponent(src, &dst.z);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL StreamFloat(SIMDMatrix src, Float34& dst)noexcept{
#if defined(NWB_HAS_SSE4)
    _mm_stream_ps(&dst._11, src.v[0]);
    _mm_stream_ps(&dst._21, src.v[1]);
    _mm_stream_ps(&dst._31, src.v[2]);
#else
    StoreFloat(src, dst);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDMatrixDouble src, Double34& dst)noexcept{
    for(usize row = 0u; row < 3u; ++row)
        SIMDConvertDoubleDetail::StreamDouble4Components(src.v[row], dst.raw + row * 4u);
}

NWB_INLINE void NWB_SIMD_CALL StreamFloat(SIMDMatrix src, Float44& dst)noexcept{
#if defined(NWB_HAS_SSE4)
    _mm_stream_ps(&dst._11, src.v[0]);
    _mm_stream_ps(&dst._21, src.v[1]);
    _mm_stream_ps(&dst._31, src.v[2]);
    _mm_stream_ps(&dst._41, src.v[3]);
#else
    StoreFloat(src, dst);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDMatrixDouble src, Double44& dst)noexcept{
    for(usize row = 0u; row < 4u; ++row)
        SIMDConvertDoubleDetail::StreamDouble4Components(src.v[row], dst.raw + row * 4u);
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDMatrixDouble src, Double33& dst)noexcept{
    // Pack the nine logical components into aligned chunks without touching the trailing padding.
#if defined(NWB_HAS_NEON64)
    const float64x2_t highFirst = vzip1q_f64(SIMDVectorDoubleDetail::GetHigh(src.v[0]), SIMDVectorDoubleDetail::GetLow(src.v[1]));
    const float64x2_t lowSecond = vextq_f64(SIMDVectorDoubleDetail::GetLow(src.v[1]), SIMDVectorDoubleDetail::GetHigh(src.v[1]), 1);
    const SIMDVectorDouble first = SIMDVectorDoubleDetail::FromPairs(SIMDVectorDoubleDetail::GetLow(src.v[0]), highFirst);
    const SIMDVectorDouble second = SIMDVectorDoubleDetail::FromPairs(lowSecond, SIMDVectorDoubleDetail::GetLow(src.v[2]));
#elif defined(NWB_HAS_SSE2)
    const __m128d highFirst = _mm_unpacklo_pd(SIMDVectorDoubleDetail::GetHigh(src.v[0]), SIMDVectorDoubleDetail::GetLow(src.v[1]));
    const __m128d lowSecond = _mm_shuffle_pd(SIMDVectorDoubleDetail::GetLow(src.v[1]), SIMDVectorDoubleDetail::GetHigh(src.v[1]), 1);
    const SIMDVectorDouble first = SIMDVectorDoubleDetail::FromPairs(SIMDVectorDoubleDetail::GetLow(src.v[0]), highFirst);
    const SIMDVectorDouble second = SIMDVectorDoubleDetail::FromPairs(lowSecond, SIMDVectorDoubleDetail::GetLow(src.v[2]));
#else
    const SIMDVectorDouble first = SIMDConvertDoubleDetail::MakeF64(src.v[0][0], src.v[0][1], src.v[0][2], src.v[1][0]);
    const SIMDVectorDouble second = SIMDConvertDoubleDetail::MakeF64(src.v[1][1], src.v[1][2], src.v[2][0], src.v[2][1]);
#endif
    SIMDConvertDoubleDetail::StreamDouble4Components(first, dst.raw);
    SIMDConvertDoubleDetail::StreamDouble4Components(second, dst.raw + 4u);
    SIMDConvertDoubleDetail::StreamDoubleHighComponent(src.v[2], &dst._33);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

