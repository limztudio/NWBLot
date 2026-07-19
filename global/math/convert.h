// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"
#include "constant.h"

#include <bit>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace HalfConvertDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE Half FloatToHalfScalar(const f32 value)noexcept{
    u32 bits = std::bit_cast<u32>(value);
    const u32 sign = (bits & 0x80000000u) >> 16u;
    bits &= 0x7fffffffu;

    u32 result = 0u;
    if(bits >= 0x47800000u){
        result = 0x7c00u | ((bits > 0x7f800000u) ? (0x0200u | ((bits >> 13u) & 0x03ffu)) : 0u);
    }
    else if(bits <= 0x33000000u){
        result = 0u;
    }
    else if(bits < 0x38800000u){
        const u32 shift = 125u - (bits >> 23u);
        bits = 0x800000u | (bits & 0x7fffffu);
        result = bits >> (shift + 1u);
        const u32 sticky = (bits & ((1u << shift) - 1u)) != 0u ? 1u : 0u;
        result += (result | sticky) & ((bits >> shift) & 1u);
    }
    else{
        bits += 0xc8000000u;
        result = ((bits + 0x0fffu + ((bits >> 13u) & 1u)) >> 13u) & 0x7fffu;
    }

    return static_cast<Half>(result | sign);
}

[[nodiscard]] NWB_INLINE f32 HalfToFloatScalar(const Half value)noexcept{
    u32 mantissa = static_cast<u32>(value & 0x03ffu);
    i32 exponent = static_cast<i32>(value & 0x7c00u);
    if(exponent == 0x7c00){
        exponent = 0x8f;
    }
    else if(exponent != 0){
        exponent = static_cast<i32>((value >> 10u) & 0x1fu);
    }
    else if(mantissa != 0u){
        exponent = 1;
        do{
            --exponent;
            mantissa <<= 1u;
        }while((mantissa & 0x0400u) == 0u);
        mantissa &= 0x03ffu;
    }
    else{
        exponent = -112;
    }

    const u32 result = ((static_cast<u32>(value) & 0x8000u) << 16u) | (static_cast<u32>(exponent + 112) << 23u) | (mantissa << 13u);
    return std::bit_cast<f32>(result);
}

#if defined(NWB_HAS_F16C)
[[nodiscard]] NWB_INLINE Half FloatToHalfF16C(const f32 value)noexcept{
    const __m128 floatValue = _mm_set_ss(value);
    const __m128i halfValue = _mm_cvtps_ph(floatValue, 0);
    return static_cast<Half>(_mm_cvtsi128_si32(halfValue));
}

[[nodiscard]] NWB_INLINE f32 HalfToFloatF16C(const Half value)noexcept{
    const __m128i halfValue = _mm_cvtsi32_si128(static_cast<int>(value));
    const __m128 floatValue = _mm_cvtph_ps(halfValue);
    return _mm_cvtss_f32(floatValue);
}

NWB_INLINE Half* FloatBufferToHalfF16C(Half* outHalfBuffer, const f32* floatBuffer, const usize count)noexcept{
    usize i = 0u;
    const usize vectorCount = count >> 2u;
    for(usize vectorIndex = 0u; vectorIndex < vectorCount; ++vectorIndex){
        const __m128 floatValue = _mm_loadu_ps(floatBuffer + i);
        const __m128i halfValue = _mm_cvtps_ph(floatValue, 0);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(outHalfBuffer + i), halfValue);
        i += 4u;
    }

    for(; i < count; ++i)
        outHalfBuffer[i] = FloatToHalfF16C(floatBuffer[i]);
    return outHalfBuffer;
}

NWB_INLINE f32* HalfBufferToFloatF16C(f32* outFloatBuffer, const Half* halfBuffer, const usize count)noexcept{
    usize i = 0u;
    const usize vectorCount = count >> 2u;
    for(usize vectorIndex = 0u; vectorIndex < vectorCount; ++vectorIndex){
        const __m128i halfValue = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(halfBuffer + i));
        const __m128 floatValue = _mm_cvtph_ps(halfValue);
        _mm_storeu_ps(outFloatBuffer + i, floatValue);
        i += 4u;
    }

    for(; i < count; ++i)
        outFloatBuffer[i] = HalfToFloatF16C(halfBuffer[i]);
    return outFloatBuffer;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    for(usize i = 0; i < count; ++i)
        outHalfBuffer[i] = ConvertFloatToHalf(floatBuffer[i]);
    return outHalfBuffer;
#endif
}

NWB_INLINE f32* ConvertHalfBufferToFloat(f32* outFloatBuffer, const Half* halfBuffer, const usize count)noexcept{
#if defined(NWB_HAS_F16C)
    return HalfConvertDetail::HalfBufferToFloatF16C(outFloatBuffer, halfBuffer, count);
#else
    for(usize i = 0; i < count; ++i)
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
    ConvertFloatBufferToHalf(result.raw, values.raw, 4u);
    return result;
}

[[nodiscard]] NWB_INLINE Float2U LoadHalf2U(const Half2U& value)noexcept{
    return Float2U(ConvertHalfToFloat(value.x), ConvertHalfToFloat(value.y));
}

[[nodiscard]] NWB_INLINE Float4U LoadHalf4U(const Half4U& value)noexcept{
    Float4U result{};
    ConvertHalfBufferToFloat(result.raw, value.raw, 4u);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDConvertDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL MakeF32(f32 x, f32 y, f32 z, f32 w)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDVector result{};
    result.f[0] = x;
    result.f[1] = y;
    result.f[2] = z;
    result.f[3] = w;
    return result;
#elif defined(NWB_HAS_NEON)
    const Float4 values(x, y, z, w);
    return vld1q_f32(values.raw);
#elif defined(NWB_HAS_SSE4)
    return _mm_set_ps(w, z, y, x);
#endif
}

NWB_INLINE SIMDVector SIMDCALL MakeU32(u32 x, u32 y, u32 z, u32 w)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDVector result{};
    result.u[0] = x;
    result.u[1] = y;
    result.u[2] = z;
    result.u[3] = w;
    return result;
#elif defined(NWB_HAS_NEON)
    UInt4 values{};
    values.x = x;
    values.y = y;
    values.z = z;
    values.w = w;
    return vreinterpretq_f32_u32(vld1q_u32(values.raw));
#elif defined(NWB_HAS_SSE4)
    return _mm_castsi128_ps(_mm_set_epi32(static_cast<i32>(w), static_cast<i32>(z), static_cast<i32>(y), static_cast<i32>(x)));
#endif
}

NWB_INLINE void SIMDCALL StoreF32(f32* out, SIMDVector value)noexcept{
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

NWB_INLINE void SIMDCALL StoreU32(u32* out, SIMDVector value)noexcept{
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
NWB_INLINE void SIMDCALL StoreInt3Bits(SIMDVector value, i32* out)noexcept{
    _mm_store_sd(reinterpret_cast<f64*>(out), _mm_castps_pd(value));
    out[2] = _mm_extract_ps(value, 2);
}

NWB_INLINE void SIMDCALL StoreUInt3Bits(SIMDVector value, u32* out)noexcept{
    _mm_store_sd(reinterpret_cast<f64*>(out), _mm_castps_pd(value));
    out[2] = static_cast<u32>(_mm_extract_ps(value, 2));
}
#endif

template<typename T>
NWB_INLINE SIMDVector SIMDCALL LoadFloat3Components(const T& src)noexcept{
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

template<typename T>
NWB_INLINE void SIMDCALL StoreFloat3Components(SIMDVector src, T* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst->x = src.f[0];
    dst->y = src.f[1];
    dst->z = src.f[2];
#elif defined(NWB_HAS_NEON)
    float32x2_t xy = vget_low_f32(src);
    vst1_f32(&dst->x, xy);
    vst1q_lane_f32(&dst->z, src, 2);
#elif defined(NWB_HAS_SSE4)
    _mm_store_sd(reinterpret_cast<f64*>(&dst->x), _mm_castps_pd(src));
    _mm_store_ss(&dst->z, _mm_shuffle_ps(src, src, _MM_SHUFFLE(2, 2, 2, 2)));
#endif
}

#if defined(NWB_HAS_SCALAR)
template<typename T>
NWB_INLINE void StoreFloat34Scalar(SIMDMatrix src, T* dst)noexcept{
    dst->_11 = src._11;
    dst->_12 = src._12;
    dst->_13 = src._13;
    dst->_14 = src._14;

    dst->_21 = src._21;
    dst->_22 = src._22;
    dst->_23 = src._23;
    dst->_24 = src._24;

    dst->_31 = src._31;
    dst->_32 = src._32;
    dst->_33 = src._33;
    dst->_34 = src._34;
}

template<typename T>
NWB_INLINE void StoreFloat44Scalar(SIMDMatrix src, T* dst)noexcept{
    StoreFloat34Scalar(src, dst);

    dst->_41 = src._41;
    dst->_42 = src._42;
    dst->_43 = src._43;
    dst->_44 = src._44;
}
#endif

template<typename T>
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat34Scalar(const T& src)noexcept{
    SIMDMatrix result{};
    result.v[0] = MakeF32(src._11, src._12, src._13, src._14);
    result.v[1] = MakeF32(src._21, src._22, src._23, src._24);
    result.v[2] = MakeF32(src._31, src._32, src._33, src._34);
    result.v[3] = s_SIMDIdentityR3;
    return result;
}

template<typename T>
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat44Scalar(const T& src)noexcept{
    SIMDMatrix result = LoadFloat34Scalar(src);
    result.v[3] = MakeF32(src._41, src._42, src._43, src._44);
    return result;
}

#if defined(NWB_HAS_NEON)
template<typename T>
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat34Neon(const T& src)noexcept{
    SIMDMatrix result;
    result.v[0] = vld1q_f32(&src._11);
    result.v[1] = vld1q_f32(&src._21);
    result.v[2] = vld1q_f32(&src._31);
    result.v[3] = s_SIMDIdentityR3;
    return result;
}

template<typename T>
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat44Neon(const T& src)noexcept{
    SIMDMatrix result = LoadFloat34Neon(src);
    result.v[3] = vld1q_f32(&src._41);
    return result;
}

#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
template<typename T>
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat34NeonAligned(const T& src)noexcept{
    SIMDMatrix result;
    result.v[0] = vld1q_f32_ex(&src._11, 128);
    result.v[1] = vld1q_f32_ex(&src._21, 128);
    result.v[2] = vld1q_f32_ex(&src._31, 128);
    result.v[3] = s_SIMDIdentityR3;
    return result;
}

template<typename T>
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat44NeonAligned(const T& src)noexcept{
    SIMDMatrix result = LoadFloat34NeonAligned(src);
    result.v[3] = vld1q_f32_ex(&src._41, 128);
    return result;
}
#endif
#endif

#if defined(NWB_HAS_SSE4)
template<bool Aligned>
NWB_INLINE SIMDVector SIMDCALL LoadMatrixRow4(const f32* row)noexcept{
    if constexpr(Aligned)
        return _mm_load_ps(row);
    else
        return _mm_loadu_ps(row);
}

template<bool Aligned, typename T>
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat34Sse(const T& src)noexcept{
    SIMDMatrix result;
    result.v[0] = LoadMatrixRow4<Aligned>(&src._11);
    result.v[1] = LoadMatrixRow4<Aligned>(&src._21);
    result.v[2] = LoadMatrixRow4<Aligned>(&src._31);
    result.v[3] = s_SIMDIdentityR3;
    return result;
}

template<bool Aligned, typename T>
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat44Sse(const T& src)noexcept{
    SIMDMatrix result = LoadFloat34Sse<Aligned>(src);
    result.v[3] = LoadMatrixRow4<Aligned>(&src._41);
    return result;
}
#endif

#if defined(NWB_HAS_SCALAR)
template<typename T>
NWB_INLINE void SIMDCALL StoreInt4Scalar(SIMDVector src, T* dst)noexcept{
    dst->x = static_cast<i32>(src.u[0]);
    dst->y = static_cast<i32>(src.u[1]);
    dst->z = static_cast<i32>(src.u[2]);
    dst->w = static_cast<i32>(src.u[3]);
}
#endif

#if defined(NWB_HAS_SSE4)
template<bool Aligned>
NWB_INLINE void SIMDCALL StoreInt4Sse(SIMDVector src, i32* dst)noexcept{
    if constexpr(Aligned)
        _mm_store_si128(reinterpret_cast<__m128i*>(dst), _mm_castps_si128(src));
    else
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), _mm_castps_si128(src));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// SIMDVector and SIMDMatrix are calculation values. Persistent CPU/GPU data must use the typed Float#/Int#/UInt#
// storage layouts and cross into SIMD only through these conversion boundaries.
NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float4& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(src.x, src.y, src.z, src.w);
#elif defined (NWB_HAS_NEON)
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    return vld1q_f32_ex(src.raw, 128);
#else
    return vld1q_f32(src.raw);
#endif
#elif defined(NWB_HAS_SSE4)
    return _mm_load_ps(src.raw);
#endif
}

NWB_INLINE SIMDVector SIMDCALL LoadFloat(f32 src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(src, 0.0f, 0.0f, 0.0f);
#elif defined (NWB_HAS_NEON)
    uint32x4_t zero = vdupq_n_u32(0);
    return vreinterpretq_f32_u32(vld1q_lane_u32(&src, zero, 0));
#elif defined(NWB_HAS_SSE4)
    return _mm_load_ss(&src);
#endif
}
NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float2U& src)noexcept{
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
NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float3U& src)noexcept{
    return SIMDConvertDetail::LoadFloat3Components(src);
}
NWB_INLINE SIMDVector SIMDCALL LoadFloatInt(const Float3Int& src)noexcept{
    return SIMDConvertDetail::LoadFloat3Components(src);
}
NWB_INLINE SIMDVector SIMDCALL LoadFloatInt(const Float3UInt& src)noexcept{
    return SIMDConvertDetail::LoadFloat3Components(src);
}
NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float4U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(src.x, src.y, src.z, src.w);
#elif defined (NWB_HAS_NEON)
    return vld1q_f32(src.raw);
#elif defined(NWB_HAS_SSE4)
    return _mm_loadu_ps(src.raw);
#endif
}

NWB_INLINE SIMDMatrix SIMDCALL LoadFloat(const Float34& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::LoadFloat34Scalar(src);
#elif defined (NWB_HAS_NEON)
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    return SIMDConvertDetail::LoadFloat34NeonAligned(src);
#else
    return SIMDConvertDetail::LoadFloat34Neon(src);
#endif
#elif defined(NWB_HAS_SSE4)
    return SIMDConvertDetail::LoadFloat34Sse<true>(src);
#endif
}
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat(const Float44& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::LoadFloat44Scalar(src);
#elif defined (NWB_HAS_NEON)
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    return SIMDConvertDetail::LoadFloat44NeonAligned(src);
#else
    return SIMDConvertDetail::LoadFloat44Neon(src);
#endif
#elif defined(NWB_HAS_SSE4)
    return SIMDConvertDetail::LoadFloat44Sse<true>(src);
#endif
}

NWB_INLINE SIMDMatrix SIMDCALL LoadFloat(const Float33U& src)noexcept{
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
    float32x2_t v2 = vcreate_f32(static_cast<u64>(*reinterpret_cast<const u32*>(&src.m[2][2])));
    float32x4_t T = vextq_f32(v0, v1, 3);

    SIMDMatrix M;
    M.v[0] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(v0), s_SIMDMask3));
    M.v[1] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(T), s_SIMDMask3));
    M.v[2] = vcombine_f32(vget_high_f32(v1), v2);
    M.v[3] = s_SIMDIdentityR3;
    return M;
#elif defined(NWB_HAS_SSE4)
    __m128 Z = _mm_setzero_ps();

    __m128 V1 = _mm_loadu_ps(&src.m[0][0]);
    __m128 V2 = _mm_loadu_ps(&src.m[1][1]);
    __m128 V3 = _mm_load_ss(&src.m[2][2]);

    __m128 T1 = _mm_unpackhi_ps(V1, Z);
    __m128 T2 = _mm_unpacklo_ps(V2, Z);
    __m128 T3 = _mm_shuffle_ps(V3, T2, _MM_SHUFFLE(0, 1, 0, 0));
    __m128 T4 = _mm_movehl_ps(T2, T3);
    __m128 T5 = _mm_movehl_ps(Z, T1);

    SIMDMatrix M;
    M.v[0] = _mm_movelh_ps(V1, T1);
    M.v[1] = _mm_add_ps(T4, T5);
    M.v[2] = _mm_shuffle_ps(V2, V3, _MM_SHUFFLE(1, 0, 3, 2));
    M.v[3] = s_SIMDIdentityR3;
    return M;
#endif
}
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat(const Float34U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::LoadFloat34Scalar(src);
#elif defined (NWB_HAS_NEON)
    return SIMDConvertDetail::LoadFloat34Neon(src);
#elif defined(NWB_HAS_SSE4)
    return SIMDConvertDetail::LoadFloat34Sse<false>(src);
#endif
}
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat(const Float44U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::LoadFloat44Scalar(src);
#elif defined (NWB_HAS_NEON)
    return SIMDConvertDetail::LoadFloat44Neon(src);
#elif defined(NWB_HAS_SSE4)
    return SIMDConvertDetail::LoadFloat44Sse<false>(src);
#endif
}

NWB_INLINE SIMDVector SIMDCALL LoadInt(const Int4& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(static_cast<u32>(src.x), static_cast<u32>(src.y), static_cast<u32>(src.z), static_cast<u32>(src.w));
#elif defined (NWB_HAS_NEON)
    return vreinterpretq_f32_s32(vld1q_s32(src.raw));
#elif defined(NWB_HAS_SSE4)
    __m128i V = _mm_load_si128(reinterpret_cast<const __m128i*>(src.raw));
    return _mm_castsi128_ps(V);
#endif
}

NWB_INLINE SIMDVector SIMDCALL LoadInt(i32 src)noexcept{
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
NWB_INLINE SIMDVector SIMDCALL LoadInt(const Int2U& src)noexcept{
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
NWB_INLINE SIMDVector SIMDCALL LoadInt(const Int3U& src)noexcept{
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
NWB_INLINE SIMDVector SIMDCALL LoadInt(const Int4U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(static_cast<u32>(src.x), static_cast<u32>(src.y), static_cast<u32>(src.z), static_cast<u32>(src.w));
#elif defined (NWB_HAS_NEON)
    return vreinterpretq_f32_s32(vld1q_s32(src.raw));
#elif defined(NWB_HAS_SSE4)
    __m128i V = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src.raw));
    return _mm_castsi128_ps(V);
#endif
}

NWB_INLINE SIMDVector SIMDCALL LoadInt(const UInt4& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(src.x, src.y, src.z, src.w);
#elif defined (NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vld1q_u32(src.raw));
#elif defined(NWB_HAS_SSE4)
    __m128i V = _mm_load_si128(reinterpret_cast<const __m128i*>(src.raw));
    return _mm_castsi128_ps(V);
#endif
}

NWB_INLINE SIMDVector SIMDCALL LoadInt(u32 src)noexcept{
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
NWB_INLINE SIMDVector SIMDCALL LoadInt(const UInt2U& src)noexcept{
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
NWB_INLINE SIMDVector SIMDCALL LoadInt(const UInt3U& src)noexcept{
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
NWB_INLINE SIMDVector SIMDCALL LoadInt(const UInt4U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(src.x, src.y, src.z, src.w);
#elif defined (NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vld1q_u32(src.raw));
#elif defined(NWB_HAS_SSE4)
    __m128i V = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src.raw));
    return _mm_castsi128_ps(V);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void StreamFloatFence()noexcept{
#if defined(NWB_HAS_SSE4)
    _mm_sfence();
#endif
}


NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float4* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst->x = src.f[0];
    dst->y = src.f[1];
    dst->z = src.f[2];
    dst->w = src.f[3];
#elif defined (NWB_HAS_NEON)
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    vst1q_f32_ex(dst->raw, src, 128);
#else
    vst1q_f32(dst->raw, src);
#endif
#elif defined(NWB_HAS_SSE4)
    _mm_store_ps(dst->raw, src);
#endif
}

NWB_INLINE void SIMDCALL StreamFloat(SIMDVector src, Float4* dst)noexcept{
#if defined(NWB_HAS_SSE4)
    _mm_stream_ps(dst->raw, src);
#else
    StoreFloat(src, dst);
#endif
}

NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, f32* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    (*dst) = src.f[0];
#elif defined (NWB_HAS_NEON)
    vst1q_lane_f32(dst, src, 0);
#elif defined(NWB_HAS_SSE4)
    _mm_store_ss(dst, src);
#endif
}
NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float2U* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst->x = src.f[0];
    dst->y = src.f[1];
#elif defined (NWB_HAS_NEON)
    float32x2_t VL = vget_low_f32(src);
    vst1_f32(dst->raw, VL);
#elif defined(NWB_HAS_SSE4)
    _mm_store_sd(reinterpret_cast<f64*>(dst), _mm_castps_pd(src));
#endif
}
NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float3U* dst)noexcept{
    SIMDConvertDetail::StoreFloat3Components(src, dst);
}
NWB_INLINE void SIMDCALL StoreFloatInt(SIMDVector src, i32 w, Float3Int* dst)noexcept{
    SIMDConvertDetail::StoreFloat3Components(src, dst);
    dst->w = w;
}
NWB_INLINE void SIMDCALL StoreFloatInt(SIMDVector src, u32 w, Float3UInt* dst)noexcept{
    SIMDConvertDetail::StoreFloat3Components(src, dst);
    dst->w = w;
}
NWB_INLINE void SIMDCALL StoreFloat(SIMDVector src, Float4U* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst->x = src.f[0];
    dst->y = src.f[1];
    dst->z = src.f[2];
    dst->w = src.f[3];
#elif defined (NWB_HAS_NEON)
    vst1q_f32(dst->raw, src);
#elif defined(NWB_HAS_SSE4)
    _mm_storeu_ps(dst->raw, src);
#endif
}

NWB_INLINE void SIMDCALL StoreFloat(SIMDMatrix src, Float34* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreFloat34Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    vst1q_f32_ex(&dst->_11, src.v[0], 128);
    vst1q_f32_ex(&dst->_21, src.v[1], 128);
    vst1q_f32_ex(&dst->_31, src.v[2], 128);
#else
    vst1q_f32(&dst->_11, src.v[0]);
    vst1q_f32(&dst->_21, src.v[1]);
    vst1q_f32(&dst->_31, src.v[2]);
#endif
#elif defined(NWB_HAS_SSE4)
    _mm_store_ps(&dst->_11, src.v[0]);
    _mm_store_ps(&dst->_21, src.v[1]);
    _mm_store_ps(&dst->_31, src.v[2]);
#endif
}

NWB_INLINE void SIMDCALL StreamFloat(SIMDMatrix src, Float34* dst)noexcept{
#if defined(NWB_HAS_SSE4)
    _mm_stream_ps(&dst->_11, src.v[0]);
    _mm_stream_ps(&dst->_21, src.v[1]);
    _mm_stream_ps(&dst->_31, src.v[2]);
#else
    StoreFloat(src, dst);
#endif
}

NWB_INLINE void SIMDCALL StoreFloat(SIMDMatrix src, Float44* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreFloat44Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    vst1q_f32_ex(&dst->_11, src.v[0], 128);
    vst1q_f32_ex(&dst->_21, src.v[1], 128);
    vst1q_f32_ex(&dst->_31, src.v[2], 128);
    vst1q_f32_ex(&dst->_41, src.v[3], 128);
#else
    vst1q_f32(&dst->_11, src.v[0]);
    vst1q_f32(&dst->_21, src.v[1]);
    vst1q_f32(&dst->_31, src.v[2]);
    vst1q_f32(&dst->_41, src.v[3]);
#endif
#elif defined(NWB_HAS_SSE4)
    _mm_store_ps(&dst->_11, src.v[0]);
    _mm_store_ps(&dst->_21, src.v[1]);
    _mm_store_ps(&dst->_31, src.v[2]);
    _mm_store_ps(&dst->_41, src.v[3]);
#endif
}

NWB_INLINE void SIMDCALL StreamFloat(SIMDMatrix src, Float44* dst)noexcept{
#if defined(NWB_HAS_SSE4)
    _mm_stream_ps(&dst->_11, src.v[0]);
    _mm_stream_ps(&dst->_21, src.v[1]);
    _mm_stream_ps(&dst->_31, src.v[2]);
    _mm_stream_ps(&dst->_41, src.v[3]);
#else
    StoreFloat(src, dst);
#endif
}

NWB_INLINE void SIMDCALL StoreFloat(SIMDMatrix src, Float33U* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst->_11 = src._11;
    dst->_12 = src._12;
    dst->_13 = src._13;

    dst->_21 = src._21;
    dst->_22 = src._22;
    dst->_23 = src._23;

    dst->_31 = src._31;
    dst->_32 = src._32;
    dst->_33 = src._33;
#elif defined (NWB_HAS_NEON)
    float32x4_t T1 = vextq_f32(src.v[0], src.v[1], 1);
    float32x4_t T2 = vbslq_f32(s_SIMDMask3, src.v[0], T1);
    vst1q_f32(&dst->m[0][0], T2);

    T1 = vextq_f32(src.v[1], src.v[1], 1);
    T2 = vcombine_f32(vget_low_f32(T1), vget_low_f32(src.v[2]));
    vst1q_f32(&dst->m[1][1], T2);

    vst1q_lane_f32(&dst->m[2][2], src.v[2], 2);
#elif defined(NWB_HAS_SSE4)
    SIMDVector vTemp1 = src.v[0];
    SIMDVector vTemp2 = src.v[1];
    SIMDVector vTemp3 = src.v[2];
    SIMDVector vWork = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(0, 0, 2, 2));
    vTemp1 = _mm_shuffle_ps(vTemp1, vWork, _MM_SHUFFLE(2, 0, 1, 0));
    _mm_storeu_ps(&dst->m[0][0], vTemp1);
    vTemp2 = _mm_shuffle_ps(vTemp2, vTemp3, _MM_SHUFFLE(1, 0, 2, 1));
    _mm_storeu_ps(&dst->m[1][1], vTemp2);
    vTemp3 = _mm_shuffle_ps(vTemp3, vTemp3, _MM_SHUFFLE(2, 2, 2, 2));
    _mm_store_ss(&dst->m[2][2], vTemp3);
#endif
}
NWB_INLINE void SIMDCALL StoreFloat(SIMDMatrix src, Float34U* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreFloat34Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_f32(&dst->_11, src.v[0]);
    vst1q_f32(&dst->_21, src.v[1]);
    vst1q_f32(&dst->_31, src.v[2]);
#elif defined(NWB_HAS_SSE4)
    _mm_storeu_ps(&dst->_11, src.v[0]);
    _mm_storeu_ps(&dst->_21, src.v[1]);
    _mm_storeu_ps(&dst->_31, src.v[2]);
#endif
}
NWB_INLINE void SIMDCALL StoreFloat(SIMDMatrix src, Float44U* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreFloat44Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_f32(&dst->_11, src.v[0]);
    vst1q_f32(&dst->_21, src.v[1]);
    vst1q_f32(&dst->_31, src.v[2]);
    vst1q_f32(&dst->_41, src.v[3]);
#elif defined(NWB_HAS_SSE4)
    _mm_storeu_ps(&dst->_11, src.v[0]);
    _mm_storeu_ps(&dst->_21, src.v[1]);
    _mm_storeu_ps(&dst->_31, src.v[2]);
    _mm_storeu_ps(&dst->_41, src.v[3]);
#endif
}

NWB_INLINE void SIMDCALL StoreInt(SIMDVector src, Int4* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreInt4Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_s32(dst->raw, vreinterpretq_s32_f32(src));
#elif defined(NWB_HAS_SSE4)
    SIMDConvertDetail::StoreInt4Sse<true>(src, dst->raw);
#endif
}

NWB_INLINE void SIMDCALL StoreInt(SIMDVector src, i32* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    *dst = static_cast<i32>(src.u[0]);
#elif defined (NWB_HAS_NEON)
    vst1q_lane_s32(dst, vreinterpretq_s32_f32(src), 0);
#elif defined(NWB_HAS_SSE4)
    _mm_store_ss(reinterpret_cast<f32*>(dst), src);
#endif
}
NWB_INLINE void SIMDCALL StoreInt(SIMDVector src, Int2U* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst->x = static_cast<i32>(src.u[0]);
    dst->y = static_cast<i32>(src.u[1]);
#elif defined (NWB_HAS_NEON)
    vst1_s32(dst->raw, vget_low_s32(vreinterpretq_s32_f32(src)));
#elif defined(NWB_HAS_SSE4)
    _mm_store_sd(reinterpret_cast<f64*>(dst->raw), _mm_castps_pd(src));
#endif
}
NWB_INLINE void SIMDCALL StoreInt(SIMDVector src, Int3U* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst->x = static_cast<i32>(src.u[0]);
    dst->y = static_cast<i32>(src.u[1]);
    dst->z = static_cast<i32>(src.u[2]);
#elif defined (NWB_HAS_NEON)
    int32x4_t value = vreinterpretq_s32_f32(src);
    vst1_s32(dst->raw, vget_low_s32(value));
    vst1q_lane_s32(dst->raw + 2, value, 2);
#elif defined(NWB_HAS_SSE4)
    SIMDConvertDetail::StoreInt3Bits(src, dst->raw);
#endif
}
NWB_INLINE void SIMDCALL StoreInt(SIMDVector src, Int4U* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreInt4Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_s32(dst->raw, vreinterpretq_s32_f32(src));
#elif defined(NWB_HAS_SSE4)
    SIMDConvertDetail::StoreInt4Sse<false>(src, dst->raw);
#endif
}

NWB_INLINE void SIMDCALL StoreInt(SIMDVector src, UInt4* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst->x = src.u[0];
    dst->y = src.u[1];
    dst->z = src.u[2];
    dst->w = src.u[3];
#elif defined (NWB_HAS_NEON)
    vst1q_u32(dst->raw, vreinterpretq_u32_f32(src));
#elif defined(NWB_HAS_SSE4)
    _mm_store_si128(reinterpret_cast<__m128i*>(dst->raw), _mm_castps_si128(src));
#endif
}

NWB_INLINE void SIMDCALL StoreInt(SIMDVector src, u32* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    *dst = src.u[0];
#elif defined (NWB_HAS_NEON)
    vst1q_lane_u32(dst, vreinterpretq_u32_f32(src), 0);
#elif defined(NWB_HAS_SSE4)
    _mm_store_ss(reinterpret_cast<f32*>(dst), src);
#endif
}
NWB_INLINE void SIMDCALL StoreInt(SIMDVector src, UInt2U* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst->x = src.u[0];
    dst->y = src.u[1];
#elif defined (NWB_HAS_NEON)
    vst1_u32(dst->raw, vget_low_u32(vreinterpretq_u32_f32(src)));
#elif defined(NWB_HAS_SSE4)
    _mm_store_sd(reinterpret_cast<f64*>(dst->raw), _mm_castps_pd(src));
#endif
}
NWB_INLINE void SIMDCALL StoreInt(SIMDVector src, UInt3U* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst->x = src.u[0];
    dst->y = src.u[1];
    dst->z = src.u[2];
#elif defined (NWB_HAS_NEON)
    uint32x4_t value = vreinterpretq_u32_f32(src);
    vst1_u32(dst->raw, vget_low_u32(value));
    vst1q_lane_u32(dst->raw + 2, value, 2);
#elif defined(NWB_HAS_SSE4)
    SIMDConvertDetail::StoreUInt3Bits(src, dst->raw);
#endif
}
NWB_INLINE void SIMDCALL StoreInt(SIMDVector src, UInt4U* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst->x = src.u[0];
    dst->y = src.u[1];
    dst->z = src.u[2];
    dst->w = src.u[3];
#elif defined (NWB_HAS_NEON)
    vst1q_u32(dst->raw, vreinterpretq_u32_f32(src));
#elif defined(NWB_HAS_SSE4)
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst->raw), _mm_castps_si128(src));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

