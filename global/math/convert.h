// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"
#include "constant.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float4& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDVector{ src.x, src.y, src.z, src.w };
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

NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float2U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDVector{ src.x, src.y, 0.f, 0.f };
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
    return SIMDVector{ src.x, src.y, src.z, 0.f };
#elif defined (NWB_HAS_NEON)
    float32x2_t x = vld1_f32(src.raw);
    float32x2_t zero = vdup_n_f32(0);
    float32x2_t y = vld1_lane_f32(src.raw + 2, zero, 0);
    return vcombine_f32(x, y);
#elif defined(NWB_HAS_SSE4)
    __m128 xy = _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(src.raw)));
    __m128 z = _mm_load_ss(&src.z);
    return _mm_insert_ps(xy, z, 0x20);
#endif
}
NWB_INLINE SIMDVector SIMDCALL LoadFloat(const Float4U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDVector{ src.x, src.y, src.z, src.w };
#elif defined (NWB_HAS_NEON)
    return vld1q_f32(src.raw);
#elif defined(NWB_HAS_SSE4)
    return _mm_loadu_ps(src.raw);
#endif
}

NWB_INLINE SIMDMatrix SIMDCALL LoadFloat(const Float34& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDMatrix{
        src._11, src._12, src._13, src._14,
        src._21, src._22, src._23, src._24,
        src._31, src._32, src._33, src._34,
        0.f,     0.f,     0.f,     1.f
        };
#elif defined (NWB_HAS_NEON)
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    float32x2x4_t vTemp0 = vld4_f32_ex(&src._11, 128);
    float32x4_t vTemp1 = vld1q_f32_ex(&src._31, 128);
#else
    float32x2x4_t vTemp0 = vld4_f32(&src._11);
    float32x4_t vTemp1 = vld1q_f32(&src._31);
#endif

    float32x2_t l = vget_low_f32(vTemp1);
    float32x4_t T0 = vcombine_f32(vTemp0.val[0], l);
    float32x2_t rl = vrev64_f32(l);
    float32x4_t T1 = vcombine_f32(vTemp0.val[1], rl);

    float32x2_t h = vget_high_f32(vTemp1);
    float32x4_t T2 = vcombine_f32(vTemp0.val[2], h);
    float32x2_t rh = vrev64_f32(h);
    float32x4_t T3 = vcombine_f32(vTemp0.val[3], rh);

    SIMDMatrix M = {};
    M.r[0] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(T0), s_SIMDMask3));
    M.r[1] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(T1), s_SIMDMask3));
    M.r[2] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(T2), s_SIMDMask3));
    M.r[3] = vsetq_lane_f32(1.f, T3, 3);
    return M;
#elif defined(NWB_HAS_SSE4)
    SIMDMatrix M;
    M.r[0] = _mm_load_ps(&src._11);
    M.r[1] = _mm_load_ps(&src._21);
    M.r[2] = _mm_load_ps(&src._31);
    M.r[3] = s_SIMDIdentityR3;

    // x.x,x.y,y.x,y.y
    SIMDVector vTemp1 = _mm_shuffle_ps(M.r[0], M.r[1], _MM_SHUFFLE(1, 0, 1, 0));
    // x.z,x.w,y.z,y.w
    SIMDVector vTemp3 = _mm_shuffle_ps(M.r[0], M.r[1], _MM_SHUFFLE(3, 2, 3, 2));
    // z.x,z.y,w.x,w.y
    SIMDVector vTemp2 = _mm_shuffle_ps(M.r[2], M.r[3], _MM_SHUFFLE(1, 0, 1, 0));
    // z.z,z.w,w.z,w.w
    SIMDVector vTemp4 = _mm_shuffle_ps(M.r[2], M.r[3], _MM_SHUFFLE(3, 2, 3, 2));
    SIMDMatrix mResult;

    // x.x,y.x,z.x,w.x
    mResult.r[0] = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(2, 0, 2, 0));
    // x.y,y.y,z.y,w.y
    mResult.r[1] = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(3, 1, 3, 1));
    // x.z,y.z,z.z,w.z
    mResult.r[2] = _mm_shuffle_ps(vTemp3, vTemp4, _MM_SHUFFLE(2, 0, 2, 0));
    // x.w,y.w,z.w,w.w
    mResult.r[3] = _mm_shuffle_ps(vTemp3, vTemp4, _MM_SHUFFLE(3, 1, 3, 1));
    return mResult;
#endif
}
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat(const Float44& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDMatrix{
        src._11, src._12, src._13, src._14,
        src._21, src._22, src._23, src._24,
        src._31, src._32, src._33, src._34,
        src._41, src._42, src._43, src._44
        };
#elif defined (NWB_HAS_NEON)
    SIMDMatrix M;
#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    M.r[0] = vld1q_f32_ex(&src._11, 128);
    M.r[1] = vld1q_f32_ex(&src._21, 128);
    M.r[2] = vld1q_f32_ex(&src._31, 128);
    M.r[3] = vld1q_f32_ex(&src._41, 128);
#else
    M.r[0] = vld1q_f32(&src._11);
    M.r[1] = vld1q_f32(&src._21);
    M.r[2] = vld1q_f32(&src._31);
    M.r[3] = vld1q_f32(&src._41);
#endif
    return M;
#elif defined(NWB_HAS_SSE4)
    SIMDMatrix M;
    M.r[0] = _mm_load_ps(&src._11);
    M.r[1] = _mm_load_ps(&src._21);
    M.r[2] = _mm_load_ps(&src._31);
    M.r[3] = _mm_load_ps(&src._41);
    return M;
#endif
}

NWB_INLINE SIMDMatrix SIMDCALL LoadFloat(const Float33U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDMatrix{
        src._11, src._12, src._13, 0.f,
        src._21, src._22, src._23, 0.f,
        src._31, src._32, src._33, 0.f,
        0.f,     0.f,     0.f,     1.f
        };
#elif defined (NWB_HAS_NEON)
    float32x4_t v0 = vld1q_f32(&src.m[0][0]);
    float32x4_t v1 = vld1q_f32(&src.m[1][1]);
    float32x2_t v2 = vcreate_f32(static_cast<u64>(*reinterpret_cast<const u32*>(&src.m[2][2])));
    float32x4_t T = vextq_f32(v0, v1, 3);

    SIMDMatrix M;
    M.r[0] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(v0), s_SIMDMask3));
    M.r[1] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(T), s_SIMDMask3));
    M.r[2] = vcombine_f32(vget_high_f32(v1), v2);
    M.r[3] = s_SIMDIdentityR3;
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
    M.r[0] = _mm_movelh_ps(V1, T1);
    M.r[1] = _mm_add_ps(T4, T5);
    M.r[2] = _mm_shuffle_ps(V2, V3, _MM_SHUFFLE(1, 0, 3, 2));
    M.r[3] = s_SIMDIdentityR3;
    return M;
#endif
}
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat(const Float34U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDMatrix{
        src._11, src._12, src._13, src._14,
        src._21, src._22, src._23, src._24,
        src._31, src._32, src._33, src._34,
        0.f,     0.f,     0.f,     1.f
        };
#elif defined (NWB_HAS_NEON)
    float32x2x4_t vTemp0 = vld4_f32(&src._11);
    float32x4_t vTemp1 = vld1q_f32(&src._31);

    float32x2_t l = vget_low_f32(vTemp1);
    float32x4_t T0 = vcombine_f32(vTemp0.val[0], l);
    float32x2_t rl = vrev64_f32(l);
    float32x4_t T1 = vcombine_f32(vTemp0.val[1], rl);

    float32x2_t h = vget_high_f32(vTemp1);
    float32x4_t T2 = vcombine_f32(vTemp0.val[2], h);
    float32x2_t rh = vrev64_f32(h);
    float32x4_t T3 = vcombine_f32(vTemp0.val[3], rh);

    SIMDMatrix M = {};
    M.r[0] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(T0), s_SIMDMask3));
    M.r[1] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(T1), s_SIMDMask3));
    M.r[2] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(T2), s_SIMDMask3));
    M.r[3] = vsetq_lane_f32(1.f, T3, 3);
    return M;
#elif defined(NWB_HAS_SSE4)
    SIMDMatrix M;
    M.r[0] = _mm_loadu_ps(&src._11);
    M.r[1] = _mm_loadu_ps(&src._21);
    M.r[2] = _mm_loadu_ps(&src._31);
    M.r[3] = s_SIMDIdentityR3;

    // x.x,x.y,y.x,y.y
    SIMDVector vTemp1 = _mm_shuffle_ps(M.r[0], M.r[1], _MM_SHUFFLE(1, 0, 1, 0));
    // x.z,x.w,y.z,y.w
    SIMDVector vTemp3 = _mm_shuffle_ps(M.r[0], M.r[1], _MM_SHUFFLE(3, 2, 3, 2));
    // z.x,z.y,w.x,w.y
    SIMDVector vTemp2 = _mm_shuffle_ps(M.r[2], M.r[3], _MM_SHUFFLE(1, 0, 1, 0));
    // z.z,z.w,w.z,w.w
    SIMDVector vTemp4 = _mm_shuffle_ps(M.r[2], M.r[3], _MM_SHUFFLE(3, 2, 3, 2));
    SIMDMatrix mResult;

    // x.x,y.x,z.x,w.x
    mResult.r[0] = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(2, 0, 2, 0));
    // x.y,y.y,z.y,w.y
    mResult.r[1] = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(3, 1, 3, 1));
    // x.z,y.z,z.z,w.z
    mResult.r[2] = _mm_shuffle_ps(vTemp3, vTemp4, _MM_SHUFFLE(2, 0, 2, 0));
    // x.w,y.w,z.w,w.w
    mResult.r[3] = _mm_shuffle_ps(vTemp3, vTemp4, _MM_SHUFFLE(3, 1, 3, 1));
    return mResult;
#endif
}
NWB_INLINE SIMDMatrix SIMDCALL LoadFloat(const Float44U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDMatrix{
        src._11, src._12, src._13, src._14,
        src._21, src._22, src._23, src._24,
        src._31, src._32, src._33, src._34,
        src._41, src._42, src._43, src._44
        };
#elif defined (NWB_HAS_NEON)
    SIMDMatrix M;
    M.r[0] = vld1q_f32(&src._11);
    M.r[1] = vld1q_f32(&src._21);
    M.r[2] = vld1q_f32(&src._31);
    M.r[3] = vld1q_f32(&src._41);
    return M;
#elif defined(NWB_HAS_SSE4)
    SIMDMatrix M;
    M.r[0] = _mm_loadu_ps(&src._11);
    M.r[1] = _mm_loadu_ps(&src._21);
    M.r[2] = _mm_loadu_ps(&src._31);
    M.r[3] = _mm_loadu_ps(&src._41);
    return M;
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
    *reinterpret_cast<int*>(&dst->x) = _mm_extract_ps(src, 0);
    *reinterpret_cast<int*>(&dst->y) = _mm_extract_ps(src, 1);
    *reinterpret_cast<int*>(&dst->z) = _mm_extract_ps(src, 2);
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
    float32x4x2_t P0 = vzipq_f32(src.r[0], src.r[2]);
    float32x4x2_t P1 = vzipq_f32(src.r[1], src.r[3]);

    float32x4x2_t T0 = vzipq_f32(P0.val[0], P1.val[0]);
    float32x4x2_t T1 = vzipq_f32(P0.val[1], P1.val[1]);

#if defined(_MSC_VER) && !defined(__clang__) && !defined(_ARM64_DISTINCT_NEON_TYPES)
    vst1q_f32_ex(&dst->m[0][0], T0.val[0], 128);
    vst1q_f32_ex(&dst->m[1][0], T0.val[1], 128);
    vst1q_f32_ex(&dst->m[2][0], T1.val[0], 128);
#else
    vst1q_f32(&dst->m[0][0], T0.val[0]);
    vst1q_f32(&dst->m[1][0], T0.val[1]);
    vst1q_f32(&dst->m[2][0], T1.val[0]);
#endif
#elif defined(NWB_HAS_SSE4)
    // x.x,x.y,y.x,y.y
    SIMDVector vTemp1 = _mm_shuffle_ps(src.r[0], src.r[1], _MM_SHUFFLE(1, 0, 1, 0));
    // x.z,x.w,y.z,y.w
    SIMDVector vTemp3 = _mm_shuffle_ps(src.r[0], src.r[1], _MM_SHUFFLE(3, 2, 3, 2));
    // z.x,z.y,w.x,w.y
    SIMDVector vTemp2 = _mm_shuffle_ps(src.r[2], src.r[3], _MM_SHUFFLE(1, 0, 1, 0));
    // z.z,z.w,w.z,w.w
    SIMDVector vTemp4 = _mm_shuffle_ps(src.r[2], src.r[3], _MM_SHUFFLE(3, 2, 3, 2));

    // x.x,y.x,z.x,w.x
    SIMDVector r0 = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(2, 0, 2, 0));
    // x.y,y.y,z.y,w.y
    SIMDVector r1 = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(3, 1, 3, 1));
    // x.z,y.z,z.z,w.z
    SIMDVector r2 = _mm_shuffle_ps(vTemp3, vTemp4, _MM_SHUFFLE(2, 0, 2, 0));

    _mm_store_ps(&dst->m[0][0], r0);
    _mm_store_ps(&dst->m[1][0], r1);
    _mm_store_ps(&dst->m[2][0], r2);
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
    vst1q_f32_ex(&dst->_11, src.r[0], 128);
    vst1q_f32_ex(&dst->_21, src.r[1], 128);
    vst1q_f32_ex(&dst->_31, src.r[2], 128);
    vst1q_f32_ex(&dst->_41, src.r[3], 128);
#else
    vst1q_f32(&dst->_11, src.r[0]);
    vst1q_f32(&dst->_21, src.r[1]);
    vst1q_f32(&dst->_31, src.r[2]);
    vst1q_f32(&dst->_41, src.r[3]);
#endif
#elif defined(NWB_HAS_SSE4)
    _mm_store_ps(&dst->_11, src.r[0]);
    _mm_store_ps(&dst->_21, src.r[1]);
    _mm_store_ps(&dst->_31, src.r[2]);
    _mm_store_ps(&dst->_41, src.r[3]);
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
    float32x4_t T1 = vextq_f32(src.r[0], src.r[1], 1);
    float32x4_t T2 = vbslq_f32(g_XMMask3, src.r[0], T1);
    vst1q_f32(&dst->m[0][0], T2);

    T1 = vextq_f32(src.r[1], src.r[1], 1);
    T2 = vcombine_f32(vget_low_f32(T1), vget_low_f32(src.r[2]));
    vst1q_f32(&dst->m[1][1], T2);

    vst1q_lane_f32(&dst->m[2][2], src.r[2], 2);
#elif defined(NWB_HAS_SSE4)
    SIMDVector vTemp1 = src.r[0];
    SIMDVector vTemp2 = src.r[1];
    SIMDVector vTemp3 = src.r[2];
    SIMDVector vWork = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(0, 0, 2, 2));
    vTemp1 = _mm_shuffle_ps(vTemp1, vWork, _MM_SHUFFLE(2, 0, 1, 0));
    _mm_storeu_ps(&dst->m[0][0], vTemp1);
    vTemp2 = _mm_shuffle_ps(vTemp2, vTemp3, _MM_SHUFFLE(1, 0, 2, 1));
    _mm_storeu_ps(&dst->m[1][1], vTemp2);
    vTemp3 = XM_PERMUTE_PS(vTemp3, _MM_SHUFFLE(2, 2, 2, 2));
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
    float32x4x2_t P0 = vzipq_f32(src.r[0], src.r[2]);
    float32x4x2_t P1 = vzipq_f32(src.r[1], src.r[3]);

    float32x4x2_t T0 = vzipq_f32(P0.val[0], P1.val[0]);
    float32x4x2_t T1 = vzipq_f32(P0.val[1], P1.val[1]);

    vst1q_f32(&dst->m[0][0], T0.val[0]);
    vst1q_f32(&dst->m[1][0], T0.val[1]);
    vst1q_f32(&dst->m[2][0], T1.val[0]);
#elif defined(NWB_HAS_SSE4)
    // x.x,x.y,y.x,y.y
    SIMDVector vTemp1 = _mm_shuffle_ps(src.r[0], src.r[1], _MM_SHUFFLE(1, 0, 1, 0));
    // x.z,x.w,y.z,y.w
    SIMDVector vTemp3 = _mm_shuffle_ps(src.r[0], src.r[1], _MM_SHUFFLE(3, 2, 3, 2));
    // z.x,z.y,w.x,w.y
    SIMDVector vTemp2 = _mm_shuffle_ps(src.r[2], src.r[3], _MM_SHUFFLE(1, 0, 1, 0));
    // z.z,z.w,w.z,w.w
    SIMDVector vTemp4 = _mm_shuffle_ps(src.r[2], src.r[3], _MM_SHUFFLE(3, 2, 3, 2));

    // x.x,y.x,z.x,w.x
    SIMDVector r0 = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(2, 0, 2, 0));
    // x.y,y.y,z.y,w.y
    SIMDVector r1 = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(3, 1, 3, 1));
    // x.z,y.z,z.z,w.z
    SIMDVector r2 = _mm_shuffle_ps(vTemp3, vTemp4, _MM_SHUFFLE(2, 0, 2, 0));

    _mm_storeu_ps(&dst->m[0][0], r0);
    _mm_storeu_ps(&dst->m[1][0], r1);
    _mm_storeu_ps(&dst->m[2][0], r2);
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
    vst1q_f32(&dst->_11, src.r[0]);
    vst1q_f32(&dst->_21, src.r[1]);
    vst1q_f32(&dst->_31, src.r[2]);
    vst1q_f32(&dst->_41, src.r[3]);
#elif defined(NWB_HAS_SSE4)
    _mm_storeu_ps(&dst->_11, src.r[0]);
    _mm_storeu_ps(&dst->_21, src.r[1]);
    _mm_storeu_ps(&dst->_31, src.r[2]);
    _mm_storeu_ps(&dst->_41, src.r[3]);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

