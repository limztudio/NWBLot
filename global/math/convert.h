// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"
#include "constant.h"


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
    alignas(16) const f32 values[4] = { x, y, z, w };
    return vld1q_f32(values);
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
    alignas(16) const u32 values[4] = { x, y, z, w };
    return vreinterpretq_f32_u32(vld1q_u32(values));
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(src.x, src.y, src.z, 0.0f);
#elif defined (NWB_HAS_NEON)
    float32x2_t x = vld1_f32(src.raw);
    float32x2_t zero = vdup_n_f32(0);
    float32x2_t y = vld1_lane_f32(src.raw + 2, zero, 0);
    return vcombine_f32(x, y);
#elif defined(NWB_HAS_SSE4)
    __m128 xy = _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(src.raw)));
    __m128 z = _mm_load_ss(&src.z);
    return _mm_movelh_ps(xy, z);
#endif
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
    SIMDMatrix result{};
    result.v[0] = SIMDConvertDetail::MakeF32(src._11, src._12, src._13, src._14);
    result.v[1] = SIMDConvertDetail::MakeF32(src._21, src._22, src._23, src._24);
    result.v[2] = SIMDConvertDetail::MakeF32(src._31, src._32, src._33, src._34);
    result.v[3] = s_SIMDIdentityR3;
    return result;
#elif defined (NWB_HAS_NEON)
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    SIMDMatrix M;
    M.v[0] = vld1q_f32_ex(&src._11, 128);
    M.v[1] = vld1q_f32_ex(&src._21, 128);
    M.v[2] = vld1q_f32_ex(&src._31, 128);
#else
    SIMDMatrix M;
    M.v[0] = vld1q_f32(&src._11);
    M.v[1] = vld1q_f32(&src._21);
    M.v[2] = vld1q_f32(&src._31);
#endif
    M.v[3] = s_SIMDIdentityR3;
    return M;
#elif defined(NWB_HAS_SSE4)
    SIMDMatrix M;
    M.v[0] = _mm_load_ps(&src._11);
    M.v[1] = _mm_load_ps(&src._21);
    M.v[2] = _mm_load_ps(&src._31);
    M.v[3] = s_SIMDIdentityR3;
    return M;
#endif
}
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat(const Float44& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDMatrix result{};
    result.v[0] = SIMDConvertDetail::MakeF32(src._11, src._12, src._13, src._14);
    result.v[1] = SIMDConvertDetail::MakeF32(src._21, src._22, src._23, src._24);
    result.v[2] = SIMDConvertDetail::MakeF32(src._31, src._32, src._33, src._34);
    result.v[3] = SIMDConvertDetail::MakeF32(src._41, src._42, src._43, src._44);
    return result;
#elif defined (NWB_HAS_NEON)
    SIMDMatrix M;
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    M.v[0] = vld1q_f32_ex(&src._11, 128);
    M.v[1] = vld1q_f32_ex(&src._21, 128);
    M.v[2] = vld1q_f32_ex(&src._31, 128);
    M.v[3] = vld1q_f32_ex(&src._41, 128);
#else
    M.v[0] = vld1q_f32(&src._11);
    M.v[1] = vld1q_f32(&src._21);
    M.v[2] = vld1q_f32(&src._31);
    M.v[3] = vld1q_f32(&src._41);
#endif
    return M;
#elif defined(NWB_HAS_SSE4)
    SIMDMatrix M;
    M.v[0] = _mm_load_ps(&src._11);
    M.v[1] = _mm_load_ps(&src._21);
    M.v[2] = _mm_load_ps(&src._31);
    M.v[3] = _mm_load_ps(&src._41);
    return M;
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
    SIMDMatrix result{};
    result.v[0] = SIMDConvertDetail::MakeF32(src._11, src._12, src._13, src._14);
    result.v[1] = SIMDConvertDetail::MakeF32(src._21, src._22, src._23, src._24);
    result.v[2] = SIMDConvertDetail::MakeF32(src._31, src._32, src._33, src._34);
    result.v[3] = s_SIMDIdentityR3;
    return result;
#elif defined (NWB_HAS_NEON)
    SIMDMatrix M;
    M.v[0] = vld1q_f32(&src._11);
    M.v[1] = vld1q_f32(&src._21);
    M.v[2] = vld1q_f32(&src._31);
    M.v[3] = s_SIMDIdentityR3;
    return M;
#elif defined(NWB_HAS_SSE4)
    SIMDMatrix M;
    M.v[0] = _mm_loadu_ps(&src._11);
    M.v[1] = _mm_loadu_ps(&src._21);
    M.v[2] = _mm_loadu_ps(&src._31);
    M.v[3] = s_SIMDIdentityR3;
    return M;
#endif
}
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat(const Float44U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDMatrix result{};
    result.v[0] = SIMDConvertDetail::MakeF32(src._11, src._12, src._13, src._14);
    result.v[1] = SIMDConvertDetail::MakeF32(src._21, src._22, src._23, src._24);
    result.v[2] = SIMDConvertDetail::MakeF32(src._31, src._32, src._33, src._34);
    result.v[3] = SIMDConvertDetail::MakeF32(src._41, src._42, src._43, src._44);
    return result;
#elif defined (NWB_HAS_NEON)
    SIMDMatrix M;
    M.v[0] = vld1q_f32(&src._11);
    M.v[1] = vld1q_f32(&src._21);
    M.v[2] = vld1q_f32(&src._31);
    M.v[3] = vld1q_f32(&src._41);
    return M;
#elif defined(NWB_HAS_SSE4)
    SIMDMatrix M;
    M.v[0] = _mm_loadu_ps(&src._11);
    M.v[1] = _mm_loadu_ps(&src._21);
    M.v[2] = _mm_loadu_ps(&src._31);
    M.v[3] = _mm_loadu_ps(&src._41);
    return M;
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
#if defined(NWB_HAS_SCALAR)
    dst->x = src.f[0];
    dst->y = src.f[1];
    dst->z = src.f[2];
#elif defined (NWB_HAS_NEON)
    float32x2_t VL = vget_low_f32(src);
    vst1_f32(dst->raw, VL);
    vst1q_lane_f32(dst->raw + 2, src, 2);
#elif defined(NWB_HAS_SSE4)
    _mm_store_sd(reinterpret_cast<f64*>(dst->raw), _mm_castps_pd(src));
    *reinterpret_cast<i32*>(&dst->z) = _mm_extract_ps(src, 2);
#endif
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
NWB_INLINE void SIMDCALL StoreFloat(SIMDMatrix src, Float44* dst)noexcept{
#if defined(NWB_HAS_SCALAR)
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

    dst->_41 = src._41;
    dst->_42 = src._42;
    dst->_43 = src._43;
    dst->_44 = src._44;
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

    dst->_41 = src._41;
    dst->_42 = src._42;
    dst->_43 = src._43;
    dst->_44 = src._44;
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
    dst->x = static_cast<i32>(src.u[0]);
    dst->y = static_cast<i32>(src.u[1]);
    dst->z = static_cast<i32>(src.u[2]);
    dst->w = static_cast<i32>(src.u[3]);
#elif defined (NWB_HAS_NEON)
    vst1q_s32(dst->raw, vreinterpretq_s32_f32(src));
#elif defined(NWB_HAS_SSE4)
    _mm_store_si128(reinterpret_cast<__m128i*>(dst->raw), _mm_castps_si128(src));
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
    dst->x = static_cast<i32>(src.u[0]);
    dst->y = static_cast<i32>(src.u[1]);
    dst->z = static_cast<i32>(src.u[2]);
    dst->w = static_cast<i32>(src.u[3]);
#elif defined (NWB_HAS_NEON)
    vst1q_s32(dst->raw, vreinterpretq_s32_f32(src));
#elif defined(NWB_HAS_SSE4)
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst->raw), _mm_castps_si128(src));
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

