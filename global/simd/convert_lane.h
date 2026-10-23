// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../bit.h"
#include "type.h"
#include "constant.h"


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
#elif defined(NWB_HAS_SSE2)
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
#elif defined(NWB_HAS_SSE2)
    return _mm_castsi128_ps(_mm_set_epi32(static_cast<i32>(w), static_cast<i32>(z), static_cast<i32>(y), static_cast<i32>(x)));
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreF32(f32 (&out)[4], SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SCALAR)
    out[0] = value.f[0];
    out[1] = value.f[1];
    out[2] = value.f[2];
    out[3] = value.f[3];
#elif defined(NWB_HAS_NEON)
    vst1q_f32(out, value);
#elif defined(NWB_HAS_SSE2)
    _mm_storeu_ps(out, value);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreU32(u32 (&out)[4], SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SCALAR)
    out[0] = value.u[0];
    out[1] = value.u[1];
    out[2] = value.u[2];
    out[3] = value.u[3];
#elif defined(NWB_HAS_NEON)
    vst1q_u32(out, vreinterpretq_u32_f32(value));
#elif defined(NWB_HAS_SSE2)
    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), _mm_castps_si128(value));
#endif
}

#if defined(NWB_HAS_SSE2)
NWB_INLINE void NWB_SIMD_CALL StoreInt3Bits(SIMDVectorSingle value, i32 (&out)[3])noexcept{
    _mm_store_sd(reinterpret_cast<f64*>(out), _mm_castps_pd(value));
    out[2] = _mm_cvtsi128_si32(_mm_shuffle_epi32(_mm_castps_si128(value), _MM_SHUFFLE(2, 2, 2, 2)));
}

NWB_INLINE void NWB_SIMD_CALL StoreUInt3Bits(SIMDVectorSingle value, u32 (&out)[3])noexcept{
    _mm_store_sd(reinterpret_cast<f64*>(out), _mm_castps_pd(value));
    out[2] = static_cast<u32>(_mm_cvtsi128_si32(_mm_shuffle_epi32(_mm_castps_si128(value), _MM_SHUFFLE(2, 2, 2, 2))));
}
#endif

template<typename T>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloat3Components(const T& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return MakeF32(src.x, src.y, src.z, 0.0f);
#elif defined(NWB_HAS_NEON)
    float32x2_t xy = vld1_f32(&src.x);
    float32x2_t z0 = vdup_n_f32(0.0f);
    z0 = vld1_lane_f32(&src.z, z0, 0);
    return vcombine_f32(xy, z0);
#elif defined(NWB_HAS_SSE2)
    __m128 xy = _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(&src.x)));
    __m128 z = _mm_load_ss(&src.z);
    return _mm_movelh_ps(xy, z);
#endif
}

template<typename T>
NWB_INLINE void NWB_SIMD_CALL StoreFloat3Components(SIMDVectorSingle src, T& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.f[0];
    dst.y = src.f[1];
    dst.z = src.f[2];
#elif defined(NWB_HAS_NEON)
    float32x2_t xy = vget_low_f32(src);
    vst1_f32(&dst.x, xy);
    vst1q_lane_f32(&dst.z, src, 2);
#elif defined(NWB_HAS_SSE2)
    _mm_store_sd(reinterpret_cast<f64*>(&dst.x), _mm_castps_pd(src));
    _mm_store_ss(&dst.z, _mm_shuffle_ps(src, src, _MM_SHUFFLE(2, 2, 2, 2)));
#endif
}

#if defined(NWB_HAS_SCALAR)
template<typename T>
NWB_INLINE void StoreFloat34Scalar(SIMDMatrix src, T& dst)noexcept{
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

template<typename T>
NWB_INLINE void StoreFloat44Scalar(SIMDMatrix src, T& dst)noexcept{
    StoreFloat34Scalar(src, dst);

    dst._41 = src._41;
    dst._42 = src._42;
    dst._43 = src._43;
    dst._44 = src._44;
}
#endif

template<typename T>
NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat34Scalar(const T& src)noexcept{
    SIMDMatrix result{};
    result.v[0] = MakeF32(src._11, src._12, src._13, src._14);
    result.v[1] = MakeF32(src._21, src._22, src._23, src._24);
    result.v[2] = MakeF32(src._31, src._32, src._33, src._34);
    result.v[3] = s_SIMDIdentityR3;
    return result;
}

template<typename T>
NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat44Scalar(const T& src)noexcept{
    SIMDMatrix result = LoadFloat34Scalar(src);
    result.v[3] = MakeF32(src._41, src._42, src._43, src._44);
    return result;
}

#if defined(NWB_HAS_NEON)
template<typename T>
NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat34Neon(const T& src)noexcept{
    SIMDMatrix result;
    result.v[0] = vld1q_f32(&src._11);
    result.v[1] = vld1q_f32(&src._21);
    result.v[2] = vld1q_f32(&src._31);
    result.v[3] = s_SIMDIdentityR3;
    return result;
}

template<typename T>
NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat44Neon(const T& src)noexcept{
    SIMDMatrix result = LoadFloat34Neon(src);
    result.v[3] = vld1q_f32(&src._41);
    return result;
}

#endif

#if defined(NWB_HAS_SSE2)
template<bool Aligned>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadMatrixRow4(const f32& row)noexcept{
    if constexpr(Aligned)
        return _mm_load_ps((&row));
    else
        return _mm_loadu_ps((&row));
}

template<bool Aligned, typename T>
NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat34Sse(const T& src)noexcept{
    SIMDMatrix result;
    result.v[0] = LoadMatrixRow4<Aligned>(src._11);
    result.v[1] = LoadMatrixRow4<Aligned>(src._21);
    result.v[2] = LoadMatrixRow4<Aligned>(src._31);
    result.v[3] = s_SIMDIdentityR3;
    return result;
}

template<bool Aligned, typename T>
NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat44Sse(const T& src)noexcept{
    SIMDMatrix result = LoadFloat34Sse<Aligned>(src);
    result.v[3] = LoadMatrixRow4<Aligned>(src._41);
    return result;
}
#endif

#if defined(NWB_HAS_SCALAR)
template<typename T>
NWB_INLINE void NWB_SIMD_CALL StoreInt4Scalar(SIMDVectorSingle src, T& dst)noexcept{
    dst.x = static_cast<i32>(src.u[0]);
    dst.y = static_cast<i32>(src.u[1]);
    dst.z = static_cast<i32>(src.u[2]);
    dst.w = static_cast<i32>(src.u[3]);
}
#endif

#if defined(NWB_HAS_SSE2)
template<bool Aligned>
NWB_INLINE void NWB_SIMD_CALL StoreInt4Sse(SIMDVectorSingle src, i32 (&dst)[4])noexcept{
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
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleComponents(const f64& src)noexcept{
    static_assert(Count >= 1u && Count <= 4u);
#if defined(NWB_HAS_NEON64)
    float64x2_t low = vdupq_n_f64(0.0);
    if constexpr(Count >= 2u)
        low = vld1q_f64((&src));
    else
        low = vld1q_lane_f64((&src), low, 0);
    float64x2_t high = vdupq_n_f64(0.0);
    if constexpr(Count == 4u)
        high = vld1q_f64((&src) + 2u);
    else if constexpr(Count == 3u)
        high = vld1q_lane_f64((&src) + 2u, high, 0);
    return SIMDVectorDoubleDetail::FromPairs(low, high);
#elif defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_AVX)
    if constexpr(Count == 4u)
        return BitCast<SIMDVectorDouble>(_mm256_loadu_pd((&src)));
    else
#endif
    {
        __m128d low;
        if constexpr(Count >= 2u)
            low = _mm_loadu_pd((&src));
        else
            low = _mm_load_sd((&src));
        __m128d high = _mm_setzero_pd();
        if constexpr(Count == 4u)
            high = _mm_loadu_pd((&src) + 2u);
        else if constexpr(Count == 3u)
            high = _mm_load_sd((&src) + 2u);
        return SIMDVectorDoubleDetail::FromPairs(low, high);
    }
#else
    return MakeF64((&src)[0], Count >= 2u ? (&src)[1] : 0.0, Count >= 3u ? (&src)[2] : 0.0, Count == 4u ? (&src)[3] : 0.0);
#endif
}

template<usize Count>
NWB_INLINE void NWB_SIMD_CALL StoreDoubleComponents(SIMDVectorDouble src, f64& dst)noexcept{
    static_assert(Count >= 1u && Count <= 4u);
#if defined(NWB_HAS_NEON64)
    if constexpr(Count >= 2u)
        vst1q_f64((&dst), SIMDVectorDoubleDetail::GetLow(src));
    else
        vst1q_lane_f64((&dst), SIMDVectorDoubleDetail::GetLow(src), 0);
    if constexpr(Count == 4u)
        vst1q_f64((&dst) + 2u, SIMDVectorDoubleDetail::GetHigh(src));
    else if constexpr(Count == 3u)
        vst1q_lane_f64((&dst) + 2u, SIMDVectorDoubleDetail::GetHigh(src), 0);
#elif defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_AVX)
    if constexpr(Count == 4u)
        _mm256_storeu_pd((&dst), BitCast<__m256d>(src));
    else
#endif
    {
        if constexpr(Count >= 2u)
            _mm_storeu_pd((&dst), SIMDVectorDoubleDetail::GetLow(src));
        else
            _mm_store_sd((&dst), SIMDVectorDoubleDetail::GetLow(src));
        if constexpr(Count == 4u)
            _mm_storeu_pd((&dst) + 2u, SIMDVectorDoubleDetail::GetHigh(src));
        else if constexpr(Count == 3u)
            _mm_store_sd((&dst) + 2u, SIMDVectorDoubleDetail::GetHigh(src));
    }
#else
    for(usize component = 0u; component < Count; ++component)
        (&dst)[component] = src[component];
#endif
}

template<usize Count, typename IntegerT>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadIntComponents(const IntegerT& src)noexcept{
    static_assert(Count >= 1u && Count <= 4u);
    static_assert(IsSame_V<IntegerT, i64> || IsSame_V<IntegerT, u64>);
#if defined(NWB_HAS_NEON64)
    const NotNull<const u64*> bits(reinterpret_cast<const u64*>(&src));
    uint64x2_t low = vdupq_n_u64(0u);
    if constexpr(Count >= 2u)
        low = vld1q_u64(bits.get());
    else
        low = vld1q_lane_u64(bits.get(), low, 0);
    uint64x2_t high = vdupq_n_u64(0u);
    if constexpr(Count == 4u)
        high = vld1q_u64(bits.get() + 2u);
    else if constexpr(Count == 3u)
        high = vld1q_lane_u64(bits.get() + 2u, high, 0);
    return SIMDVectorDoubleDetail::FromUIntPairs(low, high);
#elif defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_AVX)
    if constexpr(Count == 4u)
        return BitCast<SIMDVectorDouble>(_mm256_loadu_si256(reinterpret_cast<const __m256i*>((&src))));
    else
#endif
    {
        __m128i low;
        if constexpr(Count >= 2u)
            low = _mm_loadu_si128(reinterpret_cast<const __m128i*>((&src)));
        else
            low = _mm_loadl_epi64(reinterpret_cast<const __m128i*>((&src)));
        __m128i high = _mm_setzero_si128();
        if constexpr(Count == 4u)
            high = _mm_loadu_si128(reinterpret_cast<const __m128i*>((&src) + 2u));
        else if constexpr(Count == 3u)
            high = _mm_loadl_epi64(reinterpret_cast<const __m128i*>((&src) + 2u));
        return SIMDVectorDoubleDetail::FromUIntPairs(low, high);
    }
#else
    return MakeU64(BitCast<u64>((&src)[0]), Count >= 2u ? BitCast<u64>((&src)[1]) : 0u, Count >= 3u ? BitCast<u64>((&src)[2]) : 0u, Count == 4u ? BitCast<u64>((&src)[3]) : 0u);
#endif
}

template<usize Count, typename IntegerT>
NWB_INLINE void NWB_SIMD_CALL StoreIntComponents(SIMDVectorDouble src, IntegerT& dst)noexcept{
    static_assert(Count >= 1u && Count <= 4u);
    static_assert(IsSame_V<IntegerT, i64> || IsSame_V<IntegerT, u64>);
#if defined(NWB_HAS_NEON64)
    const NotNull<u64*> bits(reinterpret_cast<u64*>(&dst));
    if constexpr(Count >= 2u)
        vst1q_u64(bits.get(), SIMDVectorDoubleDetail::GetLowBits(src));
    else
        vst1q_lane_u64(bits.get(), SIMDVectorDoubleDetail::GetLowBits(src), 0);
    if constexpr(Count == 4u)
        vst1q_u64(bits.get() + 2u, SIMDVectorDoubleDetail::GetHighBits(src));
    else if constexpr(Count == 3u)
        vst1q_lane_u64(bits.get() + 2u, SIMDVectorDoubleDetail::GetHighBits(src), 0);
#elif defined(NWB_HAS_SSE2)
#if defined(NWB_HAS_AVX)
    if constexpr(Count == 4u)
        _mm256_storeu_si256(reinterpret_cast<__m256i*>((&dst)), BitCast<__m256i>(src));
    else
#endif
    {
        if constexpr(Count >= 2u)
            _mm_storeu_si128(reinterpret_cast<__m128i*>((&dst)), SIMDVectorDoubleDetail::GetLowBits(src));
        else
            _mm_storel_epi64(reinterpret_cast<__m128i*>((&dst)), SIMDVectorDoubleDetail::GetLowBits(src));
        if constexpr(Count == 4u)
            _mm_storeu_si128(reinterpret_cast<__m128i*>((&dst) + 2u), SIMDVectorDoubleDetail::GetHighBits(src));
        else if constexpr(Count == 3u)
            _mm_storel_epi64(reinterpret_cast<__m128i*>((&dst) + 2u), SIMDVectorDoubleDetail::GetHighBits(src));
    }
#else
    for(usize component = 0u; component < Count; ++component)
        (&dst)[component] = BitCast<IntegerT>(src[component]);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble2Components(SIMDVectorDouble src, f64& dst)noexcept{
#if defined(NWB_HAS_NEON64)
    __builtin_nontemporal_store(SIMDVectorDoubleDetail::GetLow(src), reinterpret_cast<float64x2_t*>((&dst)));
#elif defined(NWB_HAS_SSE2)
    _mm_stream_pd((&dst), SIMDVectorDoubleDetail::GetLow(src));
#else
    StoreDoubleComponents<2u>(src, dst);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StreamDoubleHighComponent(SIMDVectorDouble src, f64& dst)noexcept{
#if defined(NWB_HAS_NEON64)
    __builtin_nontemporal_store(vgetq_lane_f64(SIMDVectorDoubleDetail::GetHigh(src), 0), (&dst));
#elif defined(NWB_HAS_SSE2)
    _mm_stream_si64(reinterpret_cast<long long*>((&dst)), _mm_cvtsi128_si64(SIMDVectorDoubleDetail::GetHighBits(src)));
#else
    dst = src[2];
#endif
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble4Components(SIMDVectorDouble src, f64& dst)noexcept{
#if defined(NWB_HAS_NEON64)
    __builtin_nontemporal_store(SIMDVectorDoubleDetail::GetLow(src), reinterpret_cast<float64x2_t*>((&dst)));
    __builtin_nontemporal_store(SIMDVectorDoubleDetail::GetHigh(src), reinterpret_cast<float64x2_t*>((&dst) + 2u));
#elif defined(NWB_HAS_AVX)
    _mm256_stream_pd((&dst), BitCast<__m256d>(src));
#elif defined(NWB_HAS_SSE2)
    _mm_stream_pd((&dst), SIMDVectorDoubleDetail::GetLow(src));
    _mm_stream_pd((&dst) + 2u, SIMDVectorDoubleDetail::GetHigh(src));
#else
    StoreDoubleComponents<4u>(src, dst);
#endif
}

template<typename MatrixT>
NWB_INLINE void NWB_SIMD_CALL StoreDouble33Rows(SIMDMatrixDouble src, MatrixT& dst)noexcept{
    for(usize row = 0u; row < 3u; ++row)
        StoreDoubleComponents<3u>(src.v[row], dst.raw[row * 3u]);
}

template<typename MatrixT>
NWB_INLINE void NWB_SIMD_CALL StoreDouble34Rows(SIMDMatrixDouble src, MatrixT& dst)noexcept{
    for(usize row = 0u; row < 3u; ++row)
        StoreDoubleComponents<4u>(src.v[row], dst.raw[row * 4u]);
}

template<typename MatrixT>
NWB_INLINE void NWB_SIMD_CALL StoreDouble44Rows(SIMDMatrixDouble src, MatrixT& dst)noexcept{
    for(usize row = 0u; row < 4u; ++row)
        StoreDoubleComponents<4u>(src.v[row], dst.raw[row * 4u]);
}

template<typename MatrixT>
NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble33Rows(const MatrixT& src)noexcept{
    SIMDMatrixDouble result{};
    for(usize row = 0u; row < 3u; ++row)
        result.v[row] = LoadDoubleComponents<3u>(src.raw[row * 3u]);
    result.v[3] = s_DoubleSIMDIdentityR3;
    return result;
}

template<typename MatrixT>
NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble34Rows(const MatrixT& src)noexcept{
    SIMDMatrixDouble result{};
    for(usize row = 0u; row < 3u; ++row)
        result.v[row] = LoadDoubleComponents<4u>(src.raw[row * 4u]);
    result.v[3] = s_DoubleSIMDIdentityR3;
    return result;
}

template<typename MatrixT>
NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble44Rows(const MatrixT& src)noexcept{
    SIMDMatrixDouble result{};
    for(usize row = 0u; row < 4u; ++row)
        result.v[row] = LoadDoubleComponents<4u>(src.raw[row * 4u]);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL ConvertVectorSingleToDouble(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(vcvt_f64_f32(vget_low_f32(value)), vcvt_f64_f32(vget_high_f32(value)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_cvtps_pd(value));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_cvtps_pd(value), _mm_cvtps_pd(_mm_movehl_ps(value, value)));
#elif defined(NWB_HAS_NEON)
    return SIMDConvertDoubleDetail::MakeF64(vgetq_lane_f32(value, 0), vgetq_lane_f32(value, 1), vgetq_lane_f32(value, 2), vgetq_lane_f32(value, 3));
#else
    return SIMDConvertDoubleDetail::MakeF64(value.f[0], value.f[1], value.f[2], value.f[3]);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL ConvertVectorDoubleToSingle(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_NEON64)
    return vcombine_f32(vcvt_f32_f64(SIMDVectorDoubleDetail::GetLow(value)), vcvt_f32_f64(SIMDVectorDoubleDetail::GetHigh(value)));
#elif defined(NWB_HAS_AVX)
    return _mm256_cvtpd_ps(BitCast<__m256d>(value));
#elif defined(NWB_HAS_SSE2)
    return _mm_movelh_ps(_mm_cvtpd_ps(SIMDVectorDoubleDetail::GetLow(value)), _mm_cvtpd_ps(SIMDVectorDoubleDetail::GetHigh(value)));
#else
    return SIMDConvertDetail::MakeF32(static_cast<f32>(value[0]), static_cast<f32>(value[1]), static_cast<f32>(value[2]), static_cast<f32>(value[3]));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

