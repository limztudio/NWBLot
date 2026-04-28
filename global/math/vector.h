// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../assert.h"
#include "../simplemath.h"
#include "macro.h"
#include "constant.h"
#include "convert.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


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
    if(value < 0.5f)
        return integer;
    if(value > 0.5f)
        return integer + 1.0f;

    f32 intPart{};
    static_cast<void>(ModF(integer * 0.5f, &intPart));
    if((2.0f * intPart) == integer)
        return integer;
    return integer + 1.0f;
}

NWB_INLINE void ScalarSinCos(f32* outSin, f32* outCos, f32 value)noexcept{
    NWB_ASSERT(outSin != nullptr);
    NWB_ASSERT(outCos != nullptr);

    f32 quotient = s_1DIV2PI * value;
    if(value >= 0.0f)
        quotient = static_cast<f32>(static_cast<i32>(quotient + 0.5f));
    else
        quotient = static_cast<f32>(static_cast<i32>(quotient - 0.5f));

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
    *outSin = (((((-2.3889859e-08f * y2 + 2.7525562e-06f) * y2 - 0.00019840874f) * y2 + 0.0083333310f) * y2 - 0.16666667f) * y2 + 1.0f) * y;
    *outCos = sign * (((((-2.6051615e-07f * y2 + 2.4760495e-05f) * y2 - 0.0013888378f) * y2 + 0.041666638f) * y2 - 0.5f) * y2 + 1.0f);
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
NWB_INLINE __m128i MultiSllEpi32(__m128i value, __m128i count)noexcept{
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
}

NWB_INLINE __m128i MultiSrlEpi32(__m128i value, __m128i count)noexcept{
    __m128i v = _mm_shuffle_epi32(value, _MM_SHUFFLE(0, 0, 0, 0));
    __m128i c = _mm_and_si128(_mm_shuffle_epi32(count, _MM_SHUFFLE(0, 0, 0, 0)), s_SIMDMaskX);
    const __m128i r0 = _mm_srl_epi32(v, c);

    v = _mm_shuffle_epi32(value, _MM_SHUFFLE(1, 1, 1, 1));
    c = _mm_and_si128(_mm_shuffle_epi32(count, _MM_SHUFFLE(1, 1, 1, 1)), s_SIMDMaskX);
    const __m128i r1 = _mm_srl_epi32(v, c);

    v = _mm_shuffle_epi32(value, _MM_SHUFFLE(2, 2, 2, 2));
    c = _mm_and_si128(_mm_shuffle_epi32(count, _MM_SHUFFLE(2, 2, 2, 2)), s_SIMDMaskX);
    const __m128i r2 = _mm_srl_epi32(v, c);

    v = _mm_shuffle_epi32(value, _MM_SHUFFLE(3, 3, 3, 3));
    c = _mm_and_si128(_mm_shuffle_epi32(count, _MM_SHUFFLE(3, 3, 3, 3)), s_SIMDMaskX);
    const __m128i r3 = _mm_srl_epi32(v, c);

    const __m128 r01 = _mm_shuffle_ps(_mm_castsi128_ps(r0), _mm_castsi128_ps(r1), _MM_SHUFFLE(0, 0, 0, 0));
    const __m128 r23 = _mm_shuffle_ps(_mm_castsi128_ps(r2), _mm_castsi128_ps(r3), _MM_SHUFFLE(0, 0, 0, 0));
    return _mm_castps_si128(_mm_shuffle_ps(r01, r23, _MM_SHUFFLE(2, 0, 2, 0)));
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// column-vector transforms over row-stored matrices


#if defined(NWB_HAS_AVX2)
NWB_INLINE SIMDMatrix SIMDCALL MatrixTransposePackedRows(__m256 t0, __m256 t1)noexcept;
#endif

NWB_INLINE SIMDMatrix SIMDCALL MatrixTranspose4(SIMDVector r0, SIMDVector r1, SIMDVector r2, SIMDVector r3)noexcept{
#if defined(NWB_HAS_NEON)
    const float32x4x2_t p0 = vzipq_f32(r0, r2);
    const float32x4x2_t p1 = vzipq_f32(r1, r3);
    const float32x4x2_t t0 = vzipq_f32(p0.val[0], p1.val[0]);
    const float32x4x2_t t1 = vzipq_f32(p0.val[1], p1.val[1]);

    SIMDMatrix result{};
    result.v[0] = t0.val[0];
    result.v[1] = t0.val[1];
    result.v[2] = t1.val[0];
    result.v[3] = t1.val[1];
    return result;
#elif defined(NWB_HAS_AVX2)
    __m256 t0 = _mm256_castps128_ps256(r0);
    t0 = _mm256_insertf128_ps(t0, r1, 1);
    __m256 t1 = _mm256_castps128_ps256(r2);
    t1 = _mm256_insertf128_ps(t1, r3, 1);

    return MatrixTransposePackedRows(t0, t1);
#elif defined(NWB_HAS_SSE4)
    SIMDVector temp0 = _mm_shuffle_ps(r0, r1, _MM_SHUFFLE(1, 0, 1, 0));
    SIMDVector temp1 = _mm_shuffle_ps(r0, r1, _MM_SHUFFLE(3, 2, 3, 2));
    SIMDVector temp2 = _mm_shuffle_ps(r2, r3, _MM_SHUFFLE(1, 0, 1, 0));
    SIMDVector temp3 = _mm_shuffle_ps(r2, r3, _MM_SHUFFLE(3, 2, 3, 2));

    SIMDMatrix result{};
    result.v[0] = _mm_shuffle_ps(temp0, temp2, _MM_SHUFFLE(2, 0, 2, 0));
    result.v[1] = _mm_shuffle_ps(temp0, temp2, _MM_SHUFFLE(3, 1, 3, 1));
    result.v[2] = _mm_shuffle_ps(temp1, temp3, _MM_SHUFFLE(2, 0, 2, 0));
    result.v[3] = _mm_shuffle_ps(temp1, temp3, _MM_SHUFFLE(3, 1, 3, 1));
    return result;
#else
    SIMDMatrix result{};
    result.m[0][0] = r0.f[0];
    result.m[0][1] = r1.f[0];
    result.m[0][2] = r2.f[0];
    result.m[0][3] = r3.f[0];
    result.m[1][0] = r0.f[1];
    result.m[1][1] = r1.f[1];
    result.m[1][2] = r2.f[1];
    result.m[1][3] = r3.f[1];
    result.m[2][0] = r0.f[2];
    result.m[2][1] = r1.f[2];
    result.m[2][2] = r2.f[2];
    result.m[2][3] = r3.f[2];
    result.m[3][0] = r0.f[3];
    result.m[3][1] = r1.f[3];
    result.m[3][2] = r2.f[3];
    result.m[3][3] = r3.f[3];
    return result;
#endif
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixTransposeForTransform(const SIMDMatrix& matrix)noexcept{
    return MatrixTranspose4(matrix.v[0], matrix.v[1], matrix.v[2], matrix.v[3]);
}

#if defined(NWB_HAS_AVX2)
NWB_INLINE SIMDMatrix SIMDCALL MatrixTransposePackedRows(__m256 t0, __m256 t1)noexcept{
    __m256 temp0 = _mm256_unpacklo_ps(t0, t1);
    __m256 temp1 = _mm256_unpackhi_ps(t0, t1);
    __m256 temp2 = _mm256_permute2f128_ps(temp0, temp1, 0x20);
    __m256 temp3 = _mm256_permute2f128_ps(temp0, temp1, 0x31);
    temp0 = _mm256_unpacklo_ps(temp2, temp3);
    temp1 = _mm256_unpackhi_ps(temp2, temp3);
    t0 = _mm256_permute2f128_ps(temp0, temp1, 0x20);
    t1 = _mm256_permute2f128_ps(temp0, temp1, 0x31);

    SIMDMatrix result{};
    result.v[0] = _mm256_castps256_ps128(t0);
    result.v[1] = _mm256_extractf128_ps(t0, 1);
    result.v[2] = _mm256_castps256_ps128(t1);
    result.v[3] = _mm256_extractf128_ps(t1, 1);
    return result;
}
#endif

NWB_INLINE SIMDVector SIMDCALL Vector4TransformTransposed(SIMDVector value, const SIMDMatrix& transposedMatrix)noexcept{
#if defined(NWB_HAS_SCALAR)
    const f32 x = value.f[0];
    const f32 y = value.f[1];
    const f32 z = value.f[2];
    const f32 w = value.f[3];
    const SIMDVector& m0 = transposedMatrix.v[0];
    const SIMDVector& m1 = transposedMatrix.v[1];
    const SIMDVector& m2 = transposedMatrix.v[2];
    const SIMDVector& m3 = transposedMatrix.v[3];
    return SIMDConvertDetail::MakeF32(
        (m0.f[0] * x) + (m1.f[0] * y) + (m2.f[0] * z) + (m3.f[0] * w),
        (m0.f[1] * x) + (m1.f[1] * y) + (m2.f[1] * z) + (m3.f[1] * w),
        (m0.f[2] * x) + (m1.f[2] * y) + (m2.f[2] * z) + (m3.f[2] * w),
        (m0.f[3] * x) + (m1.f[3] * y) + (m2.f[3] * z) + (m3.f[3] * w)
    );
#elif defined(NWB_HAS_NEON)
    const float32x2_t low = vget_low_f32(value);
    const float32x2_t high = vget_high_f32(value);
    SIMDVector result = vmulq_lane_f32(transposedMatrix.v[0], low, 0);
    result = vmlaq_lane_f32(result, transposedMatrix.v[1], low, 1);
    result = vmlaq_lane_f32(result, transposedMatrix.v[2], high, 0);
    return vmlaq_lane_f32(result, transposedMatrix.v[3], high, 1);
#else
    SIMDVector x{};
    SIMDVector y{};
    SIMDVector z{};
    SIMDVector w{};
#if defined(NWB_HAS_AVX2)
#if defined(__AVX2__) || defined(_M_AVX2)
    x = _mm_broadcastss_ps(value);
#else
    x = _mm_permute_ps(value, _MM_SHUFFLE(0, 0, 0, 0));
#endif
    y = _mm_permute_ps(value, _MM_SHUFFLE(1, 1, 1, 1));
    z = _mm_permute_ps(value, _MM_SHUFFLE(2, 2, 2, 2));
    w = _mm_permute_ps(value, _MM_SHUFFLE(3, 3, 3, 3));
#else
    x = _mm_shuffle_ps(value, value, _MM_SHUFFLE(0, 0, 0, 0));
    y = _mm_shuffle_ps(value, value, _MM_SHUFFLE(1, 1, 1, 1));
    z = _mm_shuffle_ps(value, value, _MM_SHUFFLE(2, 2, 2, 2));
    w = _mm_shuffle_ps(value, value, _MM_SHUFFLE(3, 3, 3, 3));
#endif
#if defined(__FMA__) || defined(_M_FMA)
    SIMDVector result = _mm_fmadd_ps(w, transposedMatrix.v[3], _mm_mul_ps(z, transposedMatrix.v[2]));
    result = _mm_fmadd_ps(y, transposedMatrix.v[1], result);
    return _mm_fmadd_ps(x, transposedMatrix.v[0], result);
#else
    x = _mm_mul_ps(x, transposedMatrix.v[0]);
    y = _mm_mul_ps(y, transposedMatrix.v[1]);
    z = _mm_mul_ps(z, transposedMatrix.v[2]);
    w = _mm_mul_ps(w, transposedMatrix.v[3]);
    x = _mm_add_ps(x, z);
    y = _mm_add_ps(y, w);
    return _mm_add_ps(x, y);
#endif
#endif
}

#if defined(NWB_HAS_SSE4)
template<int Mask>
NWB_INLINE SIMDVector SIMDCALL MatrixDotPack(const SIMDMatrix& matrix, SIMDVector value)noexcept{
    const SIMDVector x = _mm_dp_ps(matrix.v[0], value, Mask);
    const SIMDVector y = _mm_dp_ps(matrix.v[1], value, Mask);
    const SIMDVector z = _mm_dp_ps(matrix.v[2], value, Mask);
    const SIMDVector w = _mm_dp_ps(matrix.v[3], value, Mask);
    const SIMDVector xy = _mm_unpacklo_ps(x, y);
    const SIMDVector zw = _mm_unpacklo_ps(z, w);
    return _mm_movelh_ps(xy, zw);
}
#endif


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

NWB_INLINE SIMDVector SIMDCALL VectorReplicatePtr(const f32* value)noexcept{
    NWB_ASSERT(value != nullptr);
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(*value);
#elif defined(NWB_HAS_NEON)
    return vld1q_dup_f32(value);
#elif defined(NWB_HAS_AVX2)
    return _mm_broadcast_ss(value);
#else
    return _mm_load_ps1(value);
#endif
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

NWB_INLINE SIMDVector SIMDCALL VectorReplicateIntPtr(const u32* value)noexcept{
    NWB_ASSERT(value != nullptr);
#if defined(NWB_HAS_SCALAR)
    return VectorReplicateInt(*value);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vld1q_dup_u32(value));
#else
    return _mm_castsi128_ps(_mm_set1_epi32(static_cast<i32>(*value)));
#endif
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

NWB_INLINE f32 SIMDCALL VectorGetX(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return value.f[0];
#elif defined(NWB_HAS_NEON)
    return vgetq_lane_f32(value, 0);
#else
    return _mm_cvtss_f32(value);
#endif
}

NWB_INLINE f32 SIMDCALL VectorGetY(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return value.f[1];
#elif defined(NWB_HAS_NEON)
    return vgetq_lane_f32(value, 1);
#elif defined(NWB_HAS_AVX2)
    return _mm_cvtss_f32(_mm_permute_ps(value, _MM_SHUFFLE(1, 1, 1, 1)));
#else
    return _mm_cvtss_f32(_mm_shuffle_ps(value, value, _MM_SHUFFLE(1, 1, 1, 1)));
#endif
}

NWB_INLINE f32 SIMDCALL VectorGetZ(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return value.f[2];
#elif defined(NWB_HAS_NEON)
    return vgetq_lane_f32(value, 2);
#elif defined(NWB_HAS_AVX2)
    return _mm_cvtss_f32(_mm_permute_ps(value, _MM_SHUFFLE(2, 2, 2, 2)));
#else
    return _mm_cvtss_f32(_mm_shuffle_ps(value, value, _MM_SHUFFLE(2, 2, 2, 2)));
#endif
}

NWB_INLINE f32 SIMDCALL VectorGetW(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return value.f[3];
#elif defined(NWB_HAS_NEON)
    return vgetq_lane_f32(value, 3);
#elif defined(NWB_HAS_AVX2)
    return _mm_cvtss_f32(_mm_permute_ps(value, _MM_SHUFFLE(3, 3, 3, 3)));
#else
    return _mm_cvtss_f32(_mm_shuffle_ps(value, value, _MM_SHUFFLE(3, 3, 3, 3)));
#endif
}

NWB_INLINE f32 SIMDCALL VectorGetByIndex(SIMDVector value, usize index)noexcept{
    NWB_ASSERT(index < 4);
#if defined(NWB_HAS_SCALAR)
    return value.f[index];
#else
    alignas(16) f32 v[4]{};
#if defined(NWB_HAS_NEON)
    vst1q_f32(v, value);
#else
    _mm_store_ps(v, value);
#endif
    return v[index];
#endif
}

NWB_INLINE void SIMDCALL VectorGetByIndexPtr(f32* out, SIMDVector value, usize index)noexcept{
    NWB_ASSERT(out != nullptr);
    NWB_ASSERT(index < 4);
#if defined(NWB_HAS_SCALAR)
    *out = value.f[index];
#else
    alignas(16) f32 v[4]{};
#if defined(NWB_HAS_NEON)
    vst1q_f32(v, value);
#else
    _mm_store_ps(v, value);
#endif
    *out = v[index];
#endif
}

NWB_INLINE void SIMDCALL VectorGetXPtr(f32* out, SIMDVector value)noexcept{
    NWB_ASSERT(out != nullptr);
#if defined(NWB_HAS_SCALAR)
    *out = VectorGetX(value);
#elif defined(NWB_HAS_NEON)
    vst1q_lane_f32(out, value, 0);
#else
    _mm_store_ss(out, value);
#endif
}

NWB_INLINE void SIMDCALL VectorGetYPtr(f32* out, SIMDVector value)noexcept{
    NWB_ASSERT(out != nullptr);
#if defined(NWB_HAS_SCALAR)
    *out = VectorGetY(value);
#elif defined(NWB_HAS_NEON)
    vst1q_lane_f32(out, value, 1);
#else
    *reinterpret_cast<i32*>(out) = _mm_extract_ps(value, 1);
#endif
}

NWB_INLINE void SIMDCALL VectorGetZPtr(f32* out, SIMDVector value)noexcept{
    NWB_ASSERT(out != nullptr);
#if defined(NWB_HAS_SCALAR)
    *out = VectorGetZ(value);
#elif defined(NWB_HAS_NEON)
    vst1q_lane_f32(out, value, 2);
#else
    *reinterpret_cast<i32*>(out) = _mm_extract_ps(value, 2);
#endif
}

NWB_INLINE void SIMDCALL VectorGetWPtr(f32* out, SIMDVector value)noexcept{
    NWB_ASSERT(out != nullptr);
#if defined(NWB_HAS_SCALAR)
    *out = VectorGetW(value);
#elif defined(NWB_HAS_NEON)
    vst1q_lane_f32(out, value, 3);
#else
    *reinterpret_cast<i32*>(out) = _mm_extract_ps(value, 3);
#endif
}

NWB_INLINE u32 SIMDCALL VectorGetIntX(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return value.u[0];
#elif defined(NWB_HAS_NEON)
    return vgetq_lane_u32(vreinterpretq_u32_f32(value), 0);
#else
    return static_cast<u32>(_mm_cvtsi128_si32(_mm_castps_si128(value)));
#endif
}

NWB_INLINE u32 SIMDCALL VectorGetIntY(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return value.u[1];
#elif defined(NWB_HAS_NEON)
    return vgetq_lane_u32(vreinterpretq_u32_f32(value), 1);
#else
    return static_cast<u32>(_mm_extract_epi32(_mm_castps_si128(value), 1));
#endif
}

NWB_INLINE u32 SIMDCALL VectorGetIntZ(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return value.u[2];
#elif defined(NWB_HAS_NEON)
    return vgetq_lane_u32(vreinterpretq_u32_f32(value), 2);
#else
    return static_cast<u32>(_mm_extract_epi32(_mm_castps_si128(value), 2));
#endif
}

NWB_INLINE u32 SIMDCALL VectorGetIntW(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return value.u[3];
#elif defined(NWB_HAS_NEON)
    return vgetq_lane_u32(vreinterpretq_u32_f32(value), 3);
#else
    return static_cast<u32>(_mm_extract_epi32(_mm_castps_si128(value), 3));
#endif
}

NWB_INLINE u32 SIMDCALL VectorGetIntByIndex(SIMDVector value, usize index)noexcept{
    NWB_ASSERT(index < 4);
#if defined(NWB_HAS_SCALAR)
    return value.u[index];
#else
    alignas(16) u32 v[4]{};
#if defined(NWB_HAS_NEON)
    vst1q_u32(v, vreinterpretq_u32_f32(value));
#else
    _mm_store_si128(reinterpret_cast<__m128i*>(v), _mm_castps_si128(value));
#endif
    return v[index];
#endif
}

NWB_INLINE void SIMDCALL VectorGetIntByIndexPtr(u32* out, SIMDVector value, usize index)noexcept{
    NWB_ASSERT(out != nullptr);
    NWB_ASSERT(index < 4);
#if defined(NWB_HAS_SCALAR)
    *out = value.u[index];
#else
    alignas(16) u32 v[4]{};
#if defined(NWB_HAS_NEON)
    vst1q_u32(v, vreinterpretq_u32_f32(value));
#else
    _mm_store_si128(reinterpret_cast<__m128i*>(v), _mm_castps_si128(value));
#endif
    *out = v[index];
#endif
}

NWB_INLINE void SIMDCALL VectorGetIntXPtr(u32* out, SIMDVector value)noexcept{
    NWB_ASSERT(out != nullptr);
#if defined(NWB_HAS_SCALAR)
    *out = VectorGetIntX(value);
#elif defined(NWB_HAS_NEON)
    vst1q_lane_u32(out, vreinterpretq_u32_f32(value), 0);
#else
    _mm_store_ss(reinterpret_cast<f32*>(out), value);
#endif
}

NWB_INLINE void SIMDCALL VectorGetIntYPtr(u32* out, SIMDVector value)noexcept{
    NWB_ASSERT(out != nullptr);
#if defined(NWB_HAS_SCALAR)
    *out = VectorGetIntY(value);
#elif defined(NWB_HAS_NEON)
    vst1q_lane_u32(out, vreinterpretq_u32_f32(value), 1);
#else
    *out = static_cast<u32>(_mm_extract_epi32(_mm_castps_si128(value), 1));
#endif
}

NWB_INLINE void SIMDCALL VectorGetIntZPtr(u32* out, SIMDVector value)noexcept{
    NWB_ASSERT(out != nullptr);
#if defined(NWB_HAS_SCALAR)
    *out = VectorGetIntZ(value);
#elif defined(NWB_HAS_NEON)
    vst1q_lane_u32(out, vreinterpretq_u32_f32(value), 2);
#else
    *out = static_cast<u32>(_mm_extract_epi32(_mm_castps_si128(value), 2));
#endif
}

NWB_INLINE void SIMDCALL VectorGetIntWPtr(u32* out, SIMDVector value)noexcept{
    NWB_ASSERT(out != nullptr);
#if defined(NWB_HAS_SCALAR)
    *out = VectorGetIntW(value);
#elif defined(NWB_HAS_NEON)
    vst1q_lane_u32(out, vreinterpretq_u32_f32(value), 3);
#else
    *out = static_cast<u32>(_mm_extract_epi32(_mm_castps_si128(value), 3));
#endif
}

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
#if defined(NWB_HAS_SCALAR)
    value.f[index] = component;
    return value;
#else
    alignas(16) f32 v[4]{};
#if defined(NWB_HAS_NEON)
    vst1q_f32(v, value);
    v[index] = component;
    return vld1q_f32(v);
#else
    _mm_store_ps(v, value);
    v[index] = component;
    return _mm_load_ps(v);
#endif
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetByIndexPtr(SIMDVector value, const f32* component, usize index)noexcept{
    NWB_ASSERT(component != nullptr);
    NWB_ASSERT(index < 4);
#if defined(NWB_HAS_SCALAR)
    value.f[index] = *component;
    return value;
#else
    alignas(16) f32 v[4]{};
#if defined(NWB_HAS_NEON)
    vst1q_f32(v, value);
    v[index] = *component;
    return vld1q_f32(v);
#else
    _mm_store_ps(v, value);
    v[index] = *component;
    return _mm_load_ps(v);
#endif
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetXPtr(SIMDVector value, const f32* x)noexcept{
    NWB_ASSERT(x != nullptr);
#if defined(NWB_HAS_SCALAR)
    value.f[0] = *x;
    return value;
#elif defined(NWB_HAS_NEON)
    return vld1q_lane_f32(x, value, 0);
#else
    return _mm_move_ss(value, _mm_load_ss(x));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetYPtr(SIMDVector value, const f32* y)noexcept{
    NWB_ASSERT(y != nullptr);
#if defined(NWB_HAS_SCALAR)
    value.f[1] = *y;
    return value;
#elif defined(NWB_HAS_NEON)
    return vld1q_lane_f32(y, value, 1);
#else
    return _mm_insert_ps(value, _mm_load_ss(y), 0x10);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetZPtr(SIMDVector value, const f32* z)noexcept{
    NWB_ASSERT(z != nullptr);
#if defined(NWB_HAS_SCALAR)
    value.f[2] = *z;
    return value;
#elif defined(NWB_HAS_NEON)
    return vld1q_lane_f32(z, value, 2);
#else
    return _mm_insert_ps(value, _mm_load_ss(z), 0x20);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetWPtr(SIMDVector value, const f32* w)noexcept{
    NWB_ASSERT(w != nullptr);
#if defined(NWB_HAS_SCALAR)
    value.f[3] = *w;
    return value;
#elif defined(NWB_HAS_NEON)
    return vld1q_lane_f32(w, value, 3);
#else
    return _mm_insert_ps(value, _mm_load_ss(w), 0x30);
#endif
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
#if defined(NWB_HAS_SCALAR)
    value.u[index] = component;
    return value;
#else
    alignas(16) u32 v[4]{};
#if defined(NWB_HAS_NEON)
    vst1q_u32(v, vreinterpretq_u32_f32(value));
    v[index] = component;
    return vreinterpretq_f32_u32(vld1q_u32(v));
#else
    _mm_store_si128(reinterpret_cast<__m128i*>(v), _mm_castps_si128(value));
    v[index] = component;
    return _mm_castsi128_ps(_mm_load_si128(reinterpret_cast<const __m128i*>(v)));
#endif
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntByIndexPtr(SIMDVector value, const u32* component, usize index)noexcept{
    NWB_ASSERT(component != nullptr);
    NWB_ASSERT(index < 4);
#if defined(NWB_HAS_SCALAR)
    value.u[index] = *component;
    return value;
#else
    alignas(16) u32 v[4]{};
#if defined(NWB_HAS_NEON)
    vst1q_u32(v, vreinterpretq_u32_f32(value));
    v[index] = *component;
    return vreinterpretq_f32_u32(vld1q_u32(v));
#else
    _mm_store_si128(reinterpret_cast<__m128i*>(v), _mm_castps_si128(value));
    v[index] = *component;
    return _mm_castsi128_ps(_mm_load_si128(reinterpret_cast<const __m128i*>(v)));
#endif
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntXPtr(SIMDVector value, const u32* x)noexcept{
    NWB_ASSERT(x != nullptr);
#if defined(NWB_HAS_SCALAR)
    value.u[0] = *x;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vld1q_lane_u32(x, vreinterpretq_u32_f32(value), 0));
#else
    return _mm_move_ss(value, _mm_load_ss(reinterpret_cast<const f32*>(x)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntYPtr(SIMDVector value, const u32* y)noexcept{
    NWB_ASSERT(y != nullptr);
#if defined(NWB_HAS_SCALAR)
    value.u[1] = *y;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vld1q_lane_u32(y, vreinterpretq_u32_f32(value), 1));
#else
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(*y), 1));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntZPtr(SIMDVector value, const u32* z)noexcept{
    NWB_ASSERT(z != nullptr);
#if defined(NWB_HAS_SCALAR)
    value.u[2] = *z;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vld1q_lane_u32(z, vreinterpretq_u32_f32(value), 2));
#else
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(*z), 2));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSetIntWPtr(SIMDVector value, const u32* w)noexcept{
    NWB_ASSERT(w != nullptr);
#if defined(NWB_HAS_SCALAR)
    value.u[3] = *w;
    return value;
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vld1q_lane_u32(w, vreinterpretq_u32_f32(value), 3));
#else
    return _mm_castsi128_ps(_mm_insert_epi32(_mm_castps_si128(value), static_cast<i32>(*w), 3));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSplatX(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(value.f[0]);
#elif defined(NWB_HAS_NEON)
    return vdupq_lane_f32(vget_low_f32(value), 0);
#elif defined(__AVX2__) || defined(_M_AVX2)
    return _mm_broadcastss_ps(value);
#elif defined(NWB_HAS_AVX2)
    return _mm_permute_ps(value, _MM_SHUFFLE(0, 0, 0, 0));
#else
    return _mm_shuffle_ps(value, value, _MM_SHUFFLE(0, 0, 0, 0));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSplatY(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(value.f[1]);
#elif defined(NWB_HAS_NEON)
    return vdupq_lane_f32(vget_low_f32(value), 1);
#elif defined(NWB_HAS_AVX2)
    return _mm_permute_ps(value, _MM_SHUFFLE(1, 1, 1, 1));
#else
    return _mm_shuffle_ps(value, value, _MM_SHUFFLE(1, 1, 1, 1));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSplatZ(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(value.f[2]);
#elif defined(NWB_HAS_NEON)
    return vdupq_lane_f32(vget_high_f32(value), 0);
#elif defined(NWB_HAS_AVX2)
    return _mm_permute_ps(value, _MM_SHUFFLE(2, 2, 2, 2));
#else
    return _mm_shuffle_ps(value, value, _MM_SHUFFLE(2, 2, 2, 2));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSplatW(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(value.f[3]);
#elif defined(NWB_HAS_NEON)
    return vdupq_lane_f32(vget_high_f32(value), 1);
#elif defined(NWB_HAS_AVX2)
    return _mm_permute_ps(value, _MM_SHUFFLE(3, 3, 3, 3));
#else
    return _mm_shuffle_ps(value, value, _MM_SHUFFLE(3, 3, 3, 3));
#endif
}
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
    alignas(16) const i32 control[4] = { static_cast<i32>(e0), static_cast<i32>(e1), static_cast<i32>(e2), static_cast<i32>(e3) };
    return _mm_permutevar_ps(value, _mm_load_si128(reinterpret_cast<const __m128i*>(control)));
#elif defined(NWB_HAS_SSE4)
    alignas(16) u32 stored[4]{};
    _mm_store_si128(reinterpret_cast<__m128i*>(stored), _mm_castps_si128(value));
    return _mm_castsi128_ps(_mm_set_epi32(static_cast<i32>(stored[e3]), static_cast<i32>(stored[e2]), static_cast<i32>(stored[e1]), static_cast<i32>(stored[e0])));
#else
    return SIMDConvertDetail::MakeU32(value.u[e0], value.u[e1], value.u[e2], value.u[e3]);
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
    alignas(16) const i32 control[4] = { static_cast<i32>(x), static_cast<i32>(y), static_cast<i32>(z), static_cast<i32>(w) };
    const __m128i controlVector = _mm_load_si128(reinterpret_cast<const __m128i*>(control));
    const __m128i select = _mm_cmpgt_epi32(controlVector, _mm_set1_epi32(3));
    const __m128i swizzle = _mm_and_si128(controlVector, _mm_set1_epi32(3));
    const SIMDVector a = _mm_permutevar_ps(v0, swizzle);
    const SIMDVector b = _mm_permutevar_ps(v1, swizzle);
    return _mm_or_ps(_mm_andnot_ps(_mm_castsi128_ps(select), a), _mm_and_ps(_mm_castsi128_ps(select), b));
#elif defined(NWB_HAS_SSE4)
    alignas(16) u32 a[4]{};
    alignas(16) u32 b[4]{};
    _mm_store_si128(reinterpret_cast<__m128i*>(a), _mm_castps_si128(v0));
    _mm_store_si128(reinterpret_cast<__m128i*>(b), _mm_castps_si128(v1));
    const u32* source[2] = { a, b };
    return _mm_castsi128_ps(_mm_set_epi32(
        static_cast<i32>(source[w >> 2][w & 3]),
        static_cast<i32>(source[z >> 2][z & 3]),
        static_cast<i32>(source[y >> 2][y & 3]),
        static_cast<i32>(source[x >> 2][x & 3])
    ));
#else
    const u32* source[2] = { v0.u, v1.u };
    return SIMDConvertDetail::MakeU32(source[x >> 2][x & 3], source[y >> 2][y & 3], source[z >> 2][z & 3], source[w >> 2][w & 3]);
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
// comparisons


NWB_INLINE SIMDVector SIMDCALL VectorEqual(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetX(v0) == VectorGetX(v1) ? 0xFFFFFFFFu : 0u, VectorGetY(v0) == VectorGetY(v1) ? 0xFFFFFFFFu : 0u, VectorGetZ(v0) == VectorGetZ(v1) ? 0xFFFFFFFFu : 0u, VectorGetW(v0) == VectorGetW(v1) ? 0xFFFFFFFFu : 0u);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vceqq_f32(v0, v1));
#else
    return _mm_cmpeq_ps(v0, v1);
#endif
}

NWB_INLINE u32 SIMDCALL VectorEqualR(SIMDVector v0, SIMDVector v1)noexcept{
    return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqual(v0, v1)), 0xFu);
}

NWB_INLINE SIMDVector SIMDCALL VectorEqualInt(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) == VectorGetIntX(v1) ? 0xFFFFFFFFu : 0u, VectorGetIntY(v0) == VectorGetIntY(v1) ? 0xFFFFFFFFu : 0u, VectorGetIntZ(v0) == VectorGetIntZ(v1) ? 0xFFFFFFFFu : 0u, VectorGetIntW(v0) == VectorGetIntW(v1) ? 0xFFFFFFFFu : 0u);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vceqq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_castps_si128(v0), _mm_castps_si128(v1)));
#endif
}

NWB_INLINE u32 SIMDCALL VectorEqualIntR(SIMDVector v0, SIMDVector v1)noexcept{
    return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqualInt(v0, v1)), 0xFu);
}

NWB_INLINE SIMDVector SIMDCALL VectorNearEqual(SIMDVector v0, SIMDVector v1, SIMDVector epsilon)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(
        Abs(VectorGetX(v0) - VectorGetX(v1)) <= VectorGetX(epsilon) ? 0xFFFFFFFFu : 0u,
        Abs(VectorGetY(v0) - VectorGetY(v1)) <= VectorGetY(epsilon) ? 0xFFFFFFFFu : 0u,
        Abs(VectorGetZ(v0) - VectorGetZ(v1)) <= VectorGetZ(epsilon) ? 0xFFFFFFFFu : 0u,
        Abs(VectorGetW(v0) - VectorGetW(v1)) <= VectorGetW(epsilon) ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcleq_f32(vabsq_f32(vsubq_f32(v0, v1)), epsilon));
#else
    const SIMDVector delta = _mm_andnot_ps(s_SIMDNegativeZero, _mm_sub_ps(v0, v1));
    return _mm_cmple_ps(delta, epsilon);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorNotEqual(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(
        v0.f[0] != v1.f[0] ? 0xFFFFFFFFu : 0u,
        v0.f[1] != v1.f[1] ? 0xFFFFFFFFu : 0u,
        v0.f[2] != v1.f[2] ? 0xFFFFFFFFu : 0u,
        v0.f[3] != v1.f[3] ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vmvnq_u32(vceqq_f32(v0, v1)));
#else
    return _mm_cmpneq_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorNotEqualInt(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(
        v0.u[0] != v1.u[0] ? 0xFFFFFFFFu : 0u,
        v0.u[1] != v1.u[1] ? 0xFFFFFFFFu : 0u,
        v0.u[2] != v1.u[2] ? 0xFFFFFFFFu : 0u,
        v0.u[3] != v1.u[3] ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vmvnq_u32(vceqq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1))));
#else
    const __m128i equal = _mm_cmpeq_epi32(_mm_castps_si128(v0), _mm_castps_si128(v1));
    return _mm_xor_ps(_mm_castsi128_ps(equal), s_SIMDNegOneMask);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorGreater(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcgtq_f32(v0, v1));
#elif defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetX(v0) > VectorGetX(v1) ? 0xFFFFFFFFu : 0u, VectorGetY(v0) > VectorGetY(v1) ? 0xFFFFFFFFu : 0u, VectorGetZ(v0) > VectorGetZ(v1) ? 0xFFFFFFFFu : 0u, VectorGetW(v0) > VectorGetW(v1) ? 0xFFFFFFFFu : 0u);
#else
    return _mm_cmpgt_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorGreaterOrEqual(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetX(v0) >= VectorGetX(v1) ? 0xFFFFFFFFu : 0u, VectorGetY(v0) >= VectorGetY(v1) ? 0xFFFFFFFFu : 0u, VectorGetZ(v0) >= VectorGetZ(v1) ? 0xFFFFFFFFu : 0u, VectorGetW(v0) >= VectorGetW(v1) ? 0xFFFFFFFFu : 0u);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcgeq_f32(v0, v1));
#else
    return _mm_cmpge_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorLess(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(
        v0.f[0] < v1.f[0] ? 0xFFFFFFFFu : 0u,
        v0.f[1] < v1.f[1] ? 0xFFFFFFFFu : 0u,
        v0.f[2] < v1.f[2] ? 0xFFFFFFFFu : 0u,
        v0.f[3] < v1.f[3] ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcltq_f32(v0, v1));
#else
    return _mm_cmplt_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorLessOrEqual(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(
        v0.f[0] <= v1.f[0] ? 0xFFFFFFFFu : 0u,
        v0.f[1] <= v1.f[1] ? 0xFFFFFFFFu : 0u,
        v0.f[2] <= v1.f[2] ? 0xFFFFFFFFu : 0u,
        v0.f[3] <= v1.f[3] ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcleq_f32(v0, v1));
#else
    return _mm_cmple_ps(v0, v1);
#endif
}

NWB_INLINE u32 SIMDCALL VectorGreaterR(SIMDVector v0, SIMDVector v1)noexcept{
    return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreater(v0, v1)), 0xFu);
}

NWB_INLINE u32 SIMDCALL VectorGreaterOrEqualR(SIMDVector v0, SIMDVector v1)noexcept{
    return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreaterOrEqual(v0, v1)), 0xFu);
}

NWB_INLINE SIMDVector SIMDCALL VectorInBounds(SIMDVector value, SIMDVector bounds)noexcept{
    return VectorLessOrEqual(VectorAndInt(value, s_SIMDAbsMask), bounds);
}

NWB_INLINE u32 SIMDCALL VectorInBoundsR(SIMDVector value, SIMDVector bounds)noexcept{
    return SIMDVectorDetail::BoundsMaskR(VectorMoveMask(VectorInBounds(value, bounds)), 0xFu);
}

NWB_INLINE SIMDVector SIMDCALL VectorEqualR(u32* outCR, SIMDVector v0, SIMDVector v1)noexcept{
    NWB_ASSERT(outCR != nullptr);
    *outCR = VectorEqualR(v0, v1);
    return VectorEqual(v0, v1);
}

NWB_INLINE SIMDVector SIMDCALL VectorEqualIntR(u32* outCR, SIMDVector v0, SIMDVector v1)noexcept{
    NWB_ASSERT(outCR != nullptr);
    *outCR = VectorEqualIntR(v0, v1);
    return VectorEqualInt(v0, v1);
}

NWB_INLINE SIMDVector SIMDCALL VectorGreaterR(u32* outCR, SIMDVector v0, SIMDVector v1)noexcept{
    NWB_ASSERT(outCR != nullptr);
    *outCR = VectorGreaterR(v0, v1);
    return VectorGreater(v0, v1);
}

NWB_INLINE SIMDVector SIMDCALL VectorGreaterOrEqualR(u32* outCR, SIMDVector v0, SIMDVector v1)noexcept{
    NWB_ASSERT(outCR != nullptr);
    *outCR = VectorGreaterOrEqualR(v0, v1);
    return VectorGreaterOrEqual(v0, v1);
}

NWB_INLINE SIMDVector SIMDCALL VectorInBoundsR(u32* outCR, SIMDVector value, SIMDVector bounds)noexcept{
    NWB_ASSERT(outCR != nullptr);
    *outCR = VectorInBoundsR(value, bounds);
    return VectorInBounds(value, bounds);
}

NWB_INLINE SIMDVector SIMDCALL VectorIsNaN(SIMDVector value)noexcept{
    return VectorNotEqual(value, value);
}

NWB_INLINE SIMDVector SIMDCALL VectorIsInfinite(SIMDVector value)noexcept{
    return VectorEqualInt(VectorAndInt(value, s_SIMDAbsMask), s_SIMDInfinity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// arithmetic


NWB_INLINE SIMDVector SIMDCALL VectorNegate(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(-VectorGetX(value), -VectorGetY(value), -VectorGetZ(value), -VectorGetW(value));
#elif defined(NWB_HAS_NEON)
    return vnegq_f32(value);
#else
    return _mm_xor_ps(value, s_SIMDNegativeZero);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorAdd(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) + VectorGetX(v1), VectorGetY(v0) + VectorGetY(v1), VectorGetZ(v0) + VectorGetZ(v1), VectorGetW(v0) + VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vaddq_f32(v0, v1);
#else
    return _mm_add_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSubtract(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) - VectorGetX(v1), VectorGetY(v0) - VectorGetY(v1), VectorGetZ(v0) - VectorGetZ(v1), VectorGetW(v0) - VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vsubq_f32(v0, v1);
#else
    return _mm_sub_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMultiply(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) * VectorGetX(v1), VectorGetY(v0) * VectorGetY(v1), VectorGetZ(v0) * VectorGetZ(v1), VectorGetW(v0) * VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vmulq_f32(v0, v1);
#else
    return _mm_mul_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorDivide(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) / VectorGetX(v1), VectorGetY(v0) / VectorGetY(v1), VectorGetZ(v0) / VectorGetZ(v1), VectorGetW(v0) / VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    return vdivq_f32(v0, v1);
#else
    float32x4_t reciprocal = vrecpeq_f32(v1);
    float32x4_t scale = vrecpsq_f32(reciprocal, v1);
    reciprocal = vmulq_f32(scale, reciprocal);
    scale = vrecpsq_f32(reciprocal, v1);
    reciprocal = vmulq_f32(scale, reciprocal);
    return vmulq_f32(v0, reciprocal);
#endif
#else
    return _mm_div_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMultiplyAdd(SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept{
#if defined(__FMA__) || defined(_M_FMA)
    return _mm_fmadd_ps(v0, v1, v2);
#elif defined(NWB_HAS_NEON)
    return vmlaq_f32(v2, v0, v1);
#else
    return VectorAdd(VectorMultiply(v0, v1), v2);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorNegativeMultiplySubtract(SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept{
#if defined(__FMA__) || defined(_M_FMA)
    return _mm_fnmadd_ps(v0, v1, v2);
#elif defined(NWB_HAS_NEON)
    return vmlsq_f32(v2, v0, v1);
#else
    return VectorSubtract(v2, VectorMultiply(v0, v1));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorScale(SIMDVector value, f32 scale)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(value.f[0] * scale, value.f[1] * scale, value.f[2] * scale, value.f[3] * scale);
#elif defined(NWB_HAS_NEON)
    return vmulq_n_f32(value, scale);
#else
    return _mm_mul_ps(_mm_set1_ps(scale), value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMin(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) < VectorGetX(v1) ? VectorGetX(v0) : VectorGetX(v1), VectorGetY(v0) < VectorGetY(v1) ? VectorGetY(v0) : VectorGetY(v1), VectorGetZ(v0) < VectorGetZ(v1) ? VectorGetZ(v0) : VectorGetZ(v1), VectorGetW(v0) < VectorGetW(v1) ? VectorGetW(v0) : VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vminq_f32(v0, v1);
#else
    return _mm_min_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMax(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) > VectorGetX(v1) ? VectorGetX(v0) : VectorGetX(v1), VectorGetY(v0) > VectorGetY(v1) ? VectorGetY(v0) : VectorGetY(v1), VectorGetZ(v0) > VectorGetZ(v1) ? VectorGetZ(v0) : VectorGetZ(v1), VectorGetW(v0) > VectorGetW(v1) ? VectorGetW(v0) : VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vmaxq_f32(v0, v1);
#else
    return _mm_max_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorClamp(SIMDVector value, SIMDVector minValue, SIMDVector maxValue)noexcept{
    NWB_ASSERT((VectorMoveMask(VectorLessOrEqual(minValue, maxValue)) & 0xFu) == 0xFu);
    return VectorMin(maxValue, VectorMax(minValue, value));
}

NWB_INLINE SIMDVector SIMDCALL VectorSaturate(SIMDVector value)noexcept{
    return VectorMin(VectorSplatOne(), VectorMax(VectorZero(), value));
}

NWB_INLINE SIMDVector SIMDCALL VectorAbs(SIMDVector value)noexcept{
    return VectorAndInt(value, s_SIMDAbsMask);
}

NWB_INLINE SIMDVector SIMDCALL VectorSum(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(VectorGetX(value) + VectorGetY(value) + VectorGetZ(value) + VectorGetW(value));
#elif defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    SIMDVector temp = vpaddq_f32(value, value);
    return vpaddq_f32(temp, temp);
#else
    float32x2_t lo = vget_low_f32(value);
    float32x2_t hi = vget_high_f32(value);
    lo = vadd_f32(lo, hi);
    lo = vpadd_f32(lo, lo);
    return vcombine_f32(lo, lo);
#endif
#else
    SIMDVector temp = _mm_hadd_ps(value, value);
    return _mm_hadd_ps(temp, temp);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorReciprocal(SIMDVector value)noexcept{
#if defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    return vdivq_f32(vdupq_n_f32(1.0f), value);
#else
    float32x4_t reciprocal = vrecpeq_f32(value);
    float32x4_t scale = vrecpsq_f32(reciprocal, value);
    reciprocal = vmulq_f32(scale, reciprocal);
    scale = vrecpsq_f32(reciprocal, value);
    return vmulq_f32(scale, reciprocal);
#endif
#elif defined(NWB_HAS_SSE4)
    return _mm_div_ps(s_SIMDOne, value);
#else
    return VectorDivide(VectorSplatOne(), value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorReciprocalEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReciprocal(value);
#elif defined(NWB_HAS_NEON)
    return vrecpeq_f32(value);
#else
    return _mm_rcp_ps(value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSqrt(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(Sqrt(VectorGetX(value)), Sqrt(VectorGetY(value)), Sqrt(VectorGetZ(value)), Sqrt(VectorGetW(value)));
#elif defined(NWB_HAS_NEON)
    float32x4_t s0 = vrsqrteq_f32(value);
    float32x4_t p0 = vmulq_f32(value, s0);
    float32x4_t r0 = vrsqrtsq_f32(p0, s0);
    float32x4_t s1 = vmulq_f32(s0, r0);
    float32x4_t p1 = vmulq_f32(value, s1);
    float32x4_t r1 = vrsqrtsq_f32(p1, s1);
    float32x4_t s2 = vmulq_f32(s1, r1);
    float32x4_t p2 = vmulq_f32(value, s2);
    float32x4_t r2 = vrsqrtsq_f32(p2, s2);
    float32x4_t result = vmulq_f32(value, vmulq_f32(s2, r2));
    const SIMDVector select = VectorEqualInt(VectorEqualInt(value, s_SIMDInfinity), VectorEqual(value, VectorZero()));
    return VectorSelect(value, result, select);
#else
    return _mm_sqrt_ps(value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSqrtEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_NEON)
    float32x4_t s0 = vrsqrteq_f32(value);
    float32x4_t p0 = vmulq_f32(value, s0);
    float32x4_t r0 = vrsqrtsq_f32(p0, s0);
    SIMDVector result = vmulq_f32(value, vmulq_f32(s0, r0));
    const SIMDVector select = VectorEqualInt(VectorEqualInt(value, s_SIMDInfinity), VectorEqual(value, VectorZero()));
    return VectorSelect(value, result, select);
#else
    return VectorSqrt(value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorReciprocalSqrt(SIMDVector value)noexcept{
#if defined(NWB_HAS_NEON)
    float32x4_t s0 = vrsqrteq_f32(value);
    float32x4_t p0 = vmulq_f32(value, s0);
    float32x4_t r0 = vrsqrtsq_f32(p0, s0);
    float32x4_t s1 = vmulq_f32(s0, r0);
    float32x4_t p1 = vmulq_f32(value, s1);
    float32x4_t r1 = vrsqrtsq_f32(p1, s1);
    return vmulq_f32(s1, r1);
#elif defined(NWB_HAS_SSE4)
    return _mm_div_ps(s_SIMDOne, _mm_sqrt_ps(value));
#else
    return VectorReciprocal(VectorSqrt(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorReciprocalSqrtEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReciprocalSqrt(value);
#elif defined(NWB_HAS_NEON)
    return vrsqrteq_f32(value);
#else
    return _mm_rsqrt_ps(value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorRound(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_round_ps(value, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
#elif defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    return vrndnq_f32(value);
#else
    const uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(value), vreinterpretq_u32_f32(s_SIMDNegativeZero));
    const float32x4_t magic = vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(s_SIMDNoFraction), sign));
    float32x4_t result = vaddq_f32(value, magic);
    result = vsubq_f32(result, magic);
    const uint32x4_t mask = vcleq_f32(vabsq_f32(value), s_SIMDNoFraction);
    return vbslq_f32(mask, result, value);
#endif
#else
    return SIMDConvertDetail::MakeF32(
        SIMDVectorDetail::RoundToNearest(value.f[0]),
        SIMDVectorDetail::RoundToNearest(value.f[1]),
        SIMDVectorDetail::RoundToNearest(value.f[2]),
        SIMDVectorDetail::RoundToNearest(value.f[3])
    );
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorTruncate(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_round_ps(value, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
#elif defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    return vrndq_f32(value);
#else
    const uint32x4_t mask = vcltq_f32(vabsq_f32(value), s_SIMDNoFraction);
    const int32x4_t integer = vcvtq_s32_f32(value);
    const float32x4_t result = vcvtq_f32_s32(integer);
    return vbslq_f32(mask, result, value);
#endif
#else
    return SIMDConvertDetail::MakeU32(
        SIMDVectorDetail::TruncateBits(value.f[0]),
        SIMDVectorDetail::TruncateBits(value.f[1]),
        SIMDVectorDetail::TruncateBits(value.f[2]),
        SIMDVectorDetail::TruncateBits(value.f[3])
    );
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorFloor(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_floor_ps(value);
#elif defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    return vrndmq_f32(value);
#else
    const uint32x4_t mask = vcltq_f32(vabsq_f32(value), s_SIMDNoFraction);
    const int32x4_t integer = vcvtq_s32_f32(value);
    float32x4_t result = vcvtq_f32_s32(integer);
    const uint32x4_t largerMask = vcgtq_f32(result, value);
    const float32x4_t larger = vcvtq_f32_s32(vreinterpretq_s32_u32(largerMask));
    result = vaddq_f32(result, larger);
    return vbslq_f32(mask, result, value);
#endif
#else
    return SIMDConvertDetail::MakeF32(Floor(value.f[0]), Floor(value.f[1]), Floor(value.f[2]), Floor(value.f[3]));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorCeiling(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_ceil_ps(value);
#elif defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    return vrndpq_f32(value);
#else
    const uint32x4_t mask = vcltq_f32(vabsq_f32(value), s_SIMDNoFraction);
    const int32x4_t integer = vcvtq_s32_f32(value);
    float32x4_t result = vcvtq_f32_s32(integer);
    const uint32x4_t smallerMask = vcltq_f32(result, value);
    const float32x4_t smaller = vcvtq_f32_s32(vreinterpretq_s32_u32(smallerMask));
    result = vsubq_f32(result, smaller);
    return vbslq_f32(mask, result, value);
#endif
#else
    return SIMDConvertDetail::MakeF32(Ceil(value.f[0]), Ceil(value.f[1]), Ceil(value.f[2]), Ceil(value.f[3]));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMod(SIMDVector v0, SIMDVector v1)noexcept{
    const SIMDVector quotient = VectorFloor(VectorDivide(v0, v1));
    return VectorNegativeMultiplySubtract(v1, quotient, v0);
}

NWB_INLINE SIMDVector SIMDCALL VectorModAngles(SIMDVector angles)noexcept{
    SIMDVector quotient = VectorMultiply(angles, s_SIMDReciprocalTwoPi);
    quotient = VectorRound(quotient);
    return VectorNegativeMultiplySubtract(s_SIMDTwoPi, quotient, angles);
}

NWB_INLINE SIMDVector SIMDCALL VectorAddAngles(SIMDVector v0, SIMDVector v1)noexcept{
    SIMDVector result = VectorAdd(v0, v1);
    SIMDVector mask = VectorLess(result, s_SIMDNegativePi);
    SIMDVector offset = VectorSelect(VectorZero(), s_SIMDTwoPi, mask);
    mask = VectorGreaterOrEqual(result, s_SIMDPi);
    offset = VectorSelect(offset, s_SIMDNegativeTwoPi, mask);
    return VectorAdd(result, offset);
}

NWB_INLINE SIMDVector SIMDCALL VectorSubtractAngles(SIMDVector v0, SIMDVector v1)noexcept{
    SIMDVector result = VectorSubtract(v0, v1);
    SIMDVector mask = VectorLess(result, s_SIMDNegativePi);
    SIMDVector offset = VectorSelect(VectorZero(), s_SIMDTwoPi, mask);
    mask = VectorGreaterOrEqual(result, s_SIMDPi);
    offset = VectorSelect(offset, s_SIMDNegativeTwoPi, mask);
    return VectorAdd(result, offset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// transcendental helpers


NWB_INLINE SIMDVector SIMDCALL VectorExp2(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    __m128i iTrunc = _mm_cvttps_epi32(value);
    SIMDVector fTrunc = _mm_cvtepi32_ps(iTrunc);
    SIMDVector y = VectorSubtract(value, fTrunc);

    SIMDVector poly = VectorMultiplyAdd(s_SIMDExpEst7, y, s_SIMDExpEst6);
    poly = VectorMultiplyAdd(poly, y, s_SIMDExpEst5);
    poly = VectorMultiplyAdd(poly, y, s_SIMDExpEst4);
    poly = VectorMultiplyAdd(poly, y, s_SIMDExpEst3);
    poly = VectorMultiplyAdd(poly, y, s_SIMDExpEst2);
    poly = VectorMultiplyAdd(poly, y, s_SIMDExpEst1);
    poly = VectorMultiplyAdd(poly, y, s_SIMDOne);

    __m128i biased = _mm_add_epi32(iTrunc, s_SIMDExponentBias);
    biased = _mm_slli_epi32(biased, 23);
    SIMDVector result0 = VectorDivide(_mm_castsi128_ps(biased), poly);

    biased = _mm_add_epi32(iTrunc, s_SIMD253);
    biased = _mm_slli_epi32(biased, 23);
    SIMDVector result1 = VectorDivide(_mm_castsi128_ps(biased), poly);
    result1 = VectorMultiply(s_SIMDMinNormal, result1);

    __m128i comp = _mm_cmplt_epi32(_mm_castps_si128(value), s_SIMDBin128);
    __m128i select0 = _mm_and_si128(comp, _mm_castps_si128(result0));
    __m128i select1 = _mm_andnot_si128(comp, s_SIMDInfinity);
    __m128i result2 = _mm_or_si128(select0, select1);

    comp = _mm_cmplt_epi32(iTrunc, s_SIMDSubnormalExponent);
    select1 = _mm_and_si128(comp, _mm_castps_si128(result1));
    select0 = _mm_andnot_si128(comp, _mm_castps_si128(result0));
    __m128i result3 = _mm_or_si128(select0, select1);

    comp = _mm_cmplt_epi32(_mm_castps_si128(value), s_SIMDBinNeg150);
    select0 = _mm_and_si128(comp, result3);
    select1 = _mm_andnot_si128(comp, s_SIMDZero);
    __m128i result4 = _mm_or_si128(select0, select1);

    __m128i sign = _mm_and_si128(_mm_castps_si128(value), s_SIMDNegativeZero);
    comp = _mm_cmpeq_epi32(sign, s_SIMDNegativeZero);
    select0 = _mm_and_si128(comp, result4);
    select1 = _mm_andnot_si128(comp, result2);
    __m128i result5 = _mm_or_si128(select0, select1);

    __m128i t0 = _mm_and_si128(_mm_castps_si128(value), s_SIMDQNaNTest);
    __m128i t1 = _mm_and_si128(_mm_castps_si128(value), s_SIMDInfinity);
    t0 = _mm_cmpeq_epi32(t0, s_SIMDZero);
    t1 = _mm_cmpeq_epi32(t1, s_SIMDInfinity);
    __m128i isNaN = _mm_andnot_si128(t0, t1);

    select0 = _mm_and_si128(isNaN, s_SIMDQNaN);
    select1 = _mm_andnot_si128(isNaN, result5);
    return _mm_castsi128_ps(_mm_or_si128(select0, select1));
#elif defined(NWB_HAS_NEON)
    int32x4_t iTrunc = vcvtq_s32_f32(value);
    const float32x4_t fTrunc = vcvtq_f32_s32(iTrunc);
    const float32x4_t y = vsubq_f32(value, fTrunc);

    float32x4_t poly = vmlaq_f32(s_SIMDExpEst6, s_SIMDExpEst7, y);
    poly = vmlaq_f32(s_SIMDExpEst5, poly, y);
    poly = vmlaq_f32(s_SIMDExpEst4, poly, y);
    poly = vmlaq_f32(s_SIMDExpEst3, poly, y);
    poly = vmlaq_f32(s_SIMDExpEst2, poly, y);
    poly = vmlaq_f32(s_SIMDExpEst1, poly, y);
    poly = vmlaq_f32(s_SIMDOne, poly, y);

    int32x4_t biased = vaddq_s32(iTrunc, s_SIMDExponentBias);
    biased = vshlq_n_s32(biased, 23);
    float32x4_t result0 = VectorDivide(vreinterpretq_f32_s32(biased), poly);

    biased = vaddq_s32(iTrunc, s_SIMD253);
    biased = vshlq_n_s32(biased, 23);
    float32x4_t result1 = VectorDivide(vreinterpretq_f32_s32(biased), poly);
    result1 = vmulq_f32(s_SIMDMinNormal, result1);

    uint32x4_t comp = vcltq_s32(vreinterpretq_s32_f32(value), s_SIMDBin128);
    const float32x4_t result2 = vbslq_f32(comp, result0, s_SIMDInfinity);

    comp = vcltq_s32(iTrunc, s_SIMDSubnormalExponent);
    const float32x4_t result3 = vbslq_f32(comp, result1, result0);

    comp = vcltq_s32(vreinterpretq_s32_f32(value), s_SIMDBinNeg150);
    const float32x4_t result4 = vbslq_f32(comp, result3, s_SIMDZero);

    const int32x4_t sign = vandq_s32(vreinterpretq_s32_f32(value), s_SIMDNegativeZero);
    comp = vceqq_s32(sign, s_SIMDNegativeZero);
    const float32x4_t result5 = vbslq_f32(comp, result4, result2);

    uint32x4_t t0 = vandq_u32(vreinterpretq_u32_f32(value), s_SIMDQNaNTest);
    uint32x4_t t1 = vandq_u32(vreinterpretq_u32_f32(value), s_SIMDInfinity);
    t0 = vceqq_u32(t0, s_SIMDZero);
    t1 = vceqq_u32(t1, s_SIMDInfinity);
    const uint32x4_t isNaN = vbicq_u32(t1, t0);

    return vbslq_f32(isNaN, s_SIMDQNaN, result5);
#else
    return VectorSet(Exp2(VectorGetX(value)), Exp2(VectorGetY(value)), Exp2(VectorGetZ(value)), Exp2(VectorGetW(value)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorExp10(SIMDVector value)noexcept{
    return VectorExp2(VectorMultiply(s_SIMDLg10, value));
}

NWB_INLINE SIMDVector SIMDCALL VectorExpE(SIMDVector value)noexcept{
    return VectorExp2(VectorMultiply(s_SIMDLgE, value));
}

NWB_INLINE SIMDVector SIMDCALL VectorExp(SIMDVector value)noexcept{ return VectorExpE(value); }

NWB_INLINE SIMDVector SIMDCALL VectorLog2(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    __m128i rawBiased = _mm_and_si128(_mm_castps_si128(value), s_SIMDInfinity);
    __m128i trailing = _mm_and_si128(_mm_castps_si128(value), s_SIMDQNaNTest);
    const __m128i isExponentZero = _mm_cmpeq_epi32(s_SIMDZero, rawBiased);

    const __m128i biased = _mm_srli_epi32(rawBiased, 23);
    const __m128i exponentNormal = _mm_sub_epi32(biased, s_SIMDExponentBias);
    const __m128i trailingNormal = trailing;

    const __m128i leading = SIMDVectorDetail::GetLeadingBit(trailing);
    const __m128i shift = _mm_sub_epi32(s_SIMDNumTrailing, leading);
    const __m128i exponentSubnormal = _mm_sub_epi32(s_SIMDSubnormalExponent, shift);
    __m128i trailingSubnormal = SIMDVectorDetail::MultiSllEpi32(trailing, shift);
    trailingSubnormal = _mm_and_si128(trailingSubnormal, s_SIMDQNaNTest);

    __m128i select0 = _mm_and_si128(isExponentZero, exponentSubnormal);
    __m128i select1 = _mm_andnot_si128(isExponentZero, exponentNormal);
    const __m128i exponent = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isExponentZero, trailingSubnormal);
    select1 = _mm_andnot_si128(isExponentZero, trailingNormal);
    const __m128i mantissa = _mm_or_si128(select0, select1);

    const __m128i normalized = _mm_or_si128(s_SIMDOne, mantissa);
    const SIMDVector y = _mm_sub_ps(_mm_castsi128_ps(normalized), s_SIMDOne);

    SIMDVector log2 = VectorMultiplyAdd(s_SIMDLogEst7, y, s_SIMDLogEst6);
    log2 = VectorMultiplyAdd(log2, y, s_SIMDLogEst5);
    log2 = VectorMultiplyAdd(log2, y, s_SIMDLogEst4);
    log2 = VectorMultiplyAdd(log2, y, s_SIMDLogEst3);
    log2 = VectorMultiplyAdd(log2, y, s_SIMDLogEst2);
    log2 = VectorMultiplyAdd(log2, y, s_SIMDLogEst1);
    log2 = VectorMultiplyAdd(log2, y, s_SIMDLogEst0);
    log2 = VectorMultiplyAdd(log2, y, _mm_cvtepi32_ps(exponent));

    __m128i isInfinite = _mm_and_si128(_mm_castps_si128(value), s_SIMDAbsMask);
    isInfinite = _mm_cmpeq_epi32(isInfinite, s_SIMDInfinity);

    const __m128i isGreaterZero = _mm_cmpgt_epi32(_mm_castps_si128(value), s_SIMDZero);
    const __m128i isNotFinite = _mm_cmpgt_epi32(_mm_castps_si128(value), s_SIMDInfinity);
    const __m128i isPositive = _mm_andnot_si128(isNotFinite, isGreaterZero);

    __m128i isZero = _mm_and_si128(_mm_castps_si128(value), s_SIMDAbsMask);
    isZero = _mm_cmpeq_epi32(isZero, s_SIMDZero);

    __m128i t0 = _mm_and_si128(_mm_castps_si128(value), s_SIMDQNaNTest);
    __m128i t1 = _mm_and_si128(_mm_castps_si128(value), s_SIMDInfinity);
    t0 = _mm_cmpeq_epi32(t0, s_SIMDZero);
    t1 = _mm_cmpeq_epi32(t1, s_SIMDInfinity);
    const __m128i isNaN = _mm_andnot_si128(t0, t1);

    select0 = _mm_and_si128(isInfinite, s_SIMDInfinity);
    select1 = _mm_andnot_si128(isInfinite, _mm_castps_si128(log2));
    __m128i result = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isZero, s_SIMDNegInfinity);
    select1 = _mm_andnot_si128(isZero, s_SIMDNegQNaN);
    const __m128i nonPositive = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isPositive, result);
    select1 = _mm_andnot_si128(isPositive, nonPositive);
    result = _mm_or_si128(select0, select1);

    select0 = _mm_and_si128(isNaN, s_SIMDQNaN);
    select1 = _mm_andnot_si128(isNaN, result);
    return _mm_castsi128_ps(_mm_or_si128(select0, select1));
#elif defined(NWB_HAS_NEON)
    const int32x4_t rawBiased = vandq_s32(vreinterpretq_s32_f32(value), s_SIMDInfinity);
    const int32x4_t trailing = vandq_s32(vreinterpretq_s32_f32(value), s_SIMDQNaNTest);
    const uint32x4_t isExponentZero = vceqq_s32(vreinterpretq_s32_f32(s_SIMDZero), rawBiased);

    const int32x4_t biased = vshrq_n_s32(rawBiased, 23);
    const int32x4_t exponentNormal = vsubq_s32(biased, s_SIMDExponentBias);
    const int32x4_t trailingNormal = trailing;

    const int32x4_t leading = SIMDVectorDetail::GetLeadingBit(trailing);
    const int32x4_t shift = vsubq_s32(s_SIMDNumTrailing, leading);
    const int32x4_t exponentSubnormal = vsubq_s32(s_SIMDSubnormalExponent, shift);
    int32x4_t trailingSubnormal = vshlq_s32(trailing, shift);
    trailingSubnormal = vandq_s32(trailingSubnormal, s_SIMDQNaNTest);

    const int32x4_t exponent = vbslq_s32(isExponentZero, exponentSubnormal, exponentNormal);
    const int32x4_t mantissa = vbslq_s32(isExponentZero, trailingSubnormal, trailingNormal);

    const int32x4_t normalized = vorrq_s32(vreinterpretq_s32_f32(s_SIMDOne), mantissa);
    const float32x4_t y = vsubq_f32(vreinterpretq_f32_s32(normalized), s_SIMDOne);

    float32x4_t log2 = vmlaq_f32(s_SIMDLogEst6, s_SIMDLogEst7, y);
    log2 = vmlaq_f32(s_SIMDLogEst5, log2, y);
    log2 = vmlaq_f32(s_SIMDLogEst4, log2, y);
    log2 = vmlaq_f32(s_SIMDLogEst3, log2, y);
    log2 = vmlaq_f32(s_SIMDLogEst2, log2, y);
    log2 = vmlaq_f32(s_SIMDLogEst1, log2, y);
    log2 = vmlaq_f32(s_SIMDLogEst0, log2, y);
    log2 = vmlaq_f32(vcvtq_f32_s32(exponent), log2, y);

    uint32x4_t isInfinite = vandq_u32(vreinterpretq_u32_f32(value), s_SIMDAbsMask);
    isInfinite = vceqq_u32(isInfinite, s_SIMDInfinity);

    const uint32x4_t isGreaterZero = vcgtq_f32(value, s_SIMDZero);
    const uint32x4_t isNotFinite = vcgtq_f32(value, s_SIMDInfinity);
    const uint32x4_t isPositive = vbicq_u32(isGreaterZero, isNotFinite);

    uint32x4_t isZero = vandq_u32(vreinterpretq_u32_f32(value), s_SIMDAbsMask);
    isZero = vceqq_u32(isZero, s_SIMDZero);

    uint32x4_t t0 = vandq_u32(vreinterpretq_u32_f32(value), s_SIMDQNaNTest);
    uint32x4_t t1 = vandq_u32(vreinterpretq_u32_f32(value), s_SIMDInfinity);
    t0 = vceqq_u32(t0, s_SIMDZero);
    t1 = vceqq_u32(t1, s_SIMDInfinity);
    const uint32x4_t isNaN = vbicq_u32(t1, t0);

    float32x4_t result = vbslq_f32(isInfinite, s_SIMDInfinity, log2);
    const float32x4_t nonPositive = vbslq_f32(isZero, s_SIMDNegInfinity, s_SIMDNegQNaN);
    result = vbslq_f32(isPositive, result, nonPositive);
    return vbslq_f32(isNaN, s_SIMDQNaN, result);
#else
    return VectorSet(Log2(VectorGetX(value)), Log2(VectorGetY(value)), Log2(VectorGetZ(value)), Log2(VectorGetW(value)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorLog10(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4) || defined(NWB_HAS_NEON)
    return VectorMultiply(s_SIMDInvLg10, VectorLog2(value));
#else
    return VectorSet(Log10(VectorGetX(value)), Log10(VectorGetY(value)), Log10(VectorGetZ(value)), Log10(VectorGetW(value)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorLogE(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4) || defined(NWB_HAS_NEON)
    return VectorMultiply(s_SIMDInvLgE, VectorLog2(value));
#else
    return VectorSet(Log(VectorGetX(value)), Log(VectorGetY(value)), Log(VectorGetZ(value)), Log(VectorGetW(value)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorLog(SIMDVector value)noexcept{ return VectorLogE(value); }

NWB_INLINE SIMDVector SIMDCALL VectorPow(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(Pow(VectorGetX(v0), VectorGetX(v1)), Pow(VectorGetY(v0), VectorGetY(v1)), Pow(VectorGetZ(v0), VectorGetZ(v1)), Pow(VectorGetW(v0), VectorGetW(v1)));
#elif defined(NWB_HAS_NEON)
    return SIMDConvertDetail::MakeF32(
        Pow(vgetq_lane_f32(v0, 0), vgetq_lane_f32(v1, 0)),
        Pow(vgetq_lane_f32(v0, 1), vgetq_lane_f32(v1, 1)),
        Pow(vgetq_lane_f32(v0, 2), vgetq_lane_f32(v1, 2)),
        Pow(vgetq_lane_f32(v0, 3), vgetq_lane_f32(v1, 3))
    );
#else
    alignas(16) f32 a[4]{};
    alignas(16) f32 b[4]{};
    _mm_store_ps(a, v0);
    _mm_store_ps(b, v1);
    return _mm_set_ps(Pow(a[3], b[3]), Pow(a[2], b[2]), Pow(a[1], b[1]), Pow(a[0], b[0]));
#endif
}

namespace SIMDVectorDetail{

NWB_INLINE SIMDVector SIMDCALL VectorTrigCanonicalAngle(SIMDVector value, SIMDVector& outCosSignSelect)noexcept{
    SIMDVector x = VectorModAngles(value);

    const SIMDVector sign = VectorAndInt(x, s_SIMDNegativeZero);
    const SIMDVector c = VectorOrInt(s_SIMDPi, sign);
    const SIMDVector absX = VectorAbs(x);
    const SIMDVector rflX = VectorSubtract(c, x);
    outCosSignSelect = VectorLessOrEqual(absX, s_SIMDHalfPi);
    return VectorSelect(rflX, x, outCosSignSelect);
}

NWB_INLINE SIMDVector SIMDCALL VectorTrigCosSign(SIMDVector cosSignSelect)noexcept{
    return VectorSelect(s_SIMDNegativeOne, s_SIMDOne, cosSignSelect);
}

};

NWB_INLINE SIMDVector SIMDCALL VectorSin(SIMDVector value)noexcept{
    SIMDVector cosSignSelect;
    SIMDVector x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);

    SIMDVector x2 = VectorMultiply(x, x);
    SIMDVector result = VectorMultiplyAdd(VectorSplatX(s_SIMDSinCoefficients1), x2, VectorSplatW(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatZ(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, s_SIMDOne);
    return VectorMultiply(result, x);
}

NWB_INLINE SIMDVector SIMDCALL VectorCos(SIMDVector value)noexcept{
    SIMDVector cosSignSelect;
    SIMDVector x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVector sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVector x2 = VectorMultiply(x, x);
    SIMDVector result = VectorMultiplyAdd(VectorSplatX(s_SIMDCosCoefficients1), x2, VectorSplatW(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatZ(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, s_SIMDOne);
    return VectorMultiply(result, sign);
}

NWB_INLINE void SIMDCALL VectorSinCos(SIMDVector* outSin, SIMDVector* outCos, SIMDVector value)noexcept{
    NWB_ASSERT(outSin != nullptr);
    NWB_ASSERT(outCos != nullptr);

    SIMDVector cosSignSelect;
    SIMDVector x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVector sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVector x2 = VectorMultiply(x, x);

    SIMDVector result = VectorMultiplyAdd(VectorSplatX(s_SIMDSinCoefficients1), x2, VectorSplatW(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatZ(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, s_SIMDOne);
    *outSin = VectorMultiply(result, x);

    result = VectorMultiplyAdd(VectorSplatX(s_SIMDCosCoefficients1), x2, VectorSplatW(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatZ(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, s_SIMDOne);
    *outCos = VectorMultiply(result, sign);
}

NWB_INLINE SIMDVector SIMDCALL VectorTan(SIMDVector value)noexcept{
    static const SIMDVectorConstF tanCoefficients0 = { { { 1.0f, -4.667168334e-1f, 2.566383229e-2f, -3.118153191e-4f } } };
    static const SIMDVectorConstF tanCoefficients1 = { { { 4.981943399e-7f, -1.333835001e-1f, 3.424887824e-3f, -1.786170734e-5f } } };
    static const SIMDVectorConstF tanConstants = { { { 1.570796371f, 6.077100628e-11f, 0.000244140625f, 0.63661977228f } } };
    static const SIMDVectorConstU mask = { { { 0x1u, 0x1u, 0x1u, 0x1u } } };

    SIMDVector twoDivPi = VectorSplatW(tanConstants);
    SIMDVector zero = VectorZero();

    SIMDVector c0 = VectorSplatX(tanConstants);
    SIMDVector c1 = VectorSplatY(tanConstants);
    SIMDVector epsilon = VectorSplatZ(tanConstants);

    SIMDVector va = VectorMultiply(value, twoDivPi);
    va = VectorRound(va);

    SIMDVector vc = VectorNegativeMultiplySubtract(va, c0, value);
    SIMDVector vb = VectorAbs(va);
    vc = VectorNegativeMultiplySubtract(va, c1, vc);

#if defined(NWB_HAS_SSE4)
    vb = _mm_castsi128_ps(_mm_cvttps_epi32(vb));
#elif defined(NWB_HAS_NEON)
    vb = vreinterpretq_f32_u32(vcvtq_u32_f32(vb));
#else
    f32 vbValues[4]{};
    SIMDConvertDetail::StoreF32(vbValues, vb);
    vb = VectorSetInt(static_cast<u32>(vbValues[0]), static_cast<u32>(vbValues[1]), static_cast<u32>(vbValues[2]), static_cast<u32>(vbValues[3]));
#endif

    SIMDVector vc2 = VectorMultiply(vc, vc);

    SIMDVector t7 = VectorSplatW(tanCoefficients1);
    SIMDVector t6 = VectorSplatZ(tanCoefficients1);
    SIMDVector t4 = VectorSplatX(tanCoefficients1);
    SIMDVector t3 = VectorSplatW(tanCoefficients0);
    SIMDVector t5 = VectorSplatY(tanCoefficients1);
    SIMDVector t2 = VectorSplatZ(tanCoefficients0);
    SIMDVector t1 = VectorSplatY(tanCoefficients0);
    SIMDVector t0 = VectorSplatX(tanCoefficients0);

    SIMDVector vbIsEven = VectorAndInt(vb, mask);
    vbIsEven = VectorEqualInt(vbIsEven, zero);

    SIMDVector n = VectorMultiplyAdd(vc2, t7, t6);
    SIMDVector d = VectorMultiplyAdd(vc2, t4, t3);
    n = VectorMultiplyAdd(vc2, n, t5);
    d = VectorMultiplyAdd(vc2, d, t2);
    n = VectorMultiply(vc2, n);
    d = VectorMultiplyAdd(vc2, d, t1);
    n = VectorMultiplyAdd(vc, n, vc);
    SIMDVector vcNearZero = VectorInBounds(vc, epsilon);
    d = VectorMultiplyAdd(vc2, d, t0);

    n = VectorSelect(n, vc, vcNearZero);
    d = VectorSelect(d, s_SIMDOne, vcNearZero);

    SIMDVector r0 = VectorNegate(n);
    SIMDVector r1 = VectorDivide(n, d);
    r0 = VectorDivide(d, r0);

    SIMDVector valueIsZero = VectorEqual(value, zero);
    SIMDVector result = VectorSelect(r0, r1, vbIsEven);
    return VectorSelect(result, zero, valueIsZero);
}

NWB_INLINE SIMDVector SIMDCALL VectorSinH(SIMDVector value)noexcept{
    const SIMDVector e1 = VectorExp(value);
    const SIMDVector e2 = VectorExp(VectorNegate(value));
    return VectorMultiply(VectorSubtract(e1, e2), s_SIMDOneHalf);
}

NWB_INLINE SIMDVector SIMDCALL VectorCosH(SIMDVector value)noexcept{
    const SIMDVector e1 = VectorExp(value);
    const SIMDVector e2 = VectorExp(VectorNegate(value));
    return VectorMultiply(VectorAdd(e1, e2), s_SIMDOneHalf);
}

NWB_INLINE SIMDVector SIMDCALL VectorTanH(SIMDVector value)noexcept{
    const SIMDVector sign = VectorAndInt(value, s_SIMDNegativeZero);
    const SIMDVector absValue = VectorAbs(value);
    const SIMDVector e = VectorExp(VectorNegate(VectorAdd(absValue, absValue)));
    const SIMDVector magnitude = VectorDivide(VectorSubtract(s_SIMDOne, e), VectorAdd(s_SIMDOne, e));
    return VectorOrInt(magnitude, sign);
}

namespace SIMDVectorDetail{

NWB_INLINE SIMDVector SIMDCALL VectorArcCoefficientApproximation(SIMDVector value)noexcept{
    SIMDVector x = VectorAbs(value);
    SIMDVector root = VectorSqrt(VectorMax(s_SIMDZero, VectorSubtract(s_SIMDOne, x)));

    SIMDVector result = VectorMultiplyAdd(VectorSplatW(s_SIMDArcCoefficients1), x, VectorSplatZ(s_SIMDArcCoefficients1));
    result = VectorMultiplyAdd(result, x, VectorSplatY(s_SIMDArcCoefficients1));
    result = VectorMultiplyAdd(result, x, VectorSplatX(s_SIMDArcCoefficients1));
    result = VectorMultiplyAdd(result, x, VectorSplatW(s_SIMDArcCoefficients0));
    result = VectorMultiplyAdd(result, x, VectorSplatZ(s_SIMDArcCoefficients0));
    result = VectorMultiplyAdd(result, x, VectorSplatY(s_SIMDArcCoefficients0));
    result = VectorMultiplyAdd(result, x, VectorSplatX(s_SIMDArcCoefficients0));
    return VectorMultiply(result, root);
}

NWB_INLINE SIMDVector SIMDCALL VectorATan2SelectResult(SIMDVector y, SIMDVector x, SIMDVector atanResult, SIMDVector constants)noexcept{
    const SIMDVector zero = VectorZero();
    SIMDVector atanResultValid = VectorTrueInt();

    SIMDVector pi = VectorSplatX(constants);
    SIMDVector piOverTwo = VectorSplatY(constants);
    SIMDVector piOverFour = VectorSplatZ(constants);
    SIMDVector threePiOverFour = VectorSplatW(constants);

    SIMDVector yEqualsZero = VectorEqual(y, zero);
    SIMDVector xEqualsZero = VectorEqual(x, zero);
    SIMDVector xIsPositive = VectorEqualInt(VectorAndInt(x, s_SIMDNegativeZero), zero);
    SIMDVector yEqualsInfinity = VectorIsInfinite(y);
    SIMDVector xEqualsInfinity = VectorIsInfinite(x);

    SIMDVector ySign = VectorAndInt(y, s_SIMDNegativeZero);
    pi = VectorOrInt(pi, ySign);
    piOverTwo = VectorOrInt(piOverTwo, ySign);
    piOverFour = VectorOrInt(piOverFour, ySign);
    threePiOverFour = VectorOrInt(threePiOverFour, ySign);

    SIMDVector r1 = VectorSelect(pi, ySign, xIsPositive);
    SIMDVector r2 = VectorSelect(atanResultValid, piOverTwo, xEqualsZero);
    SIMDVector r3 = VectorSelect(r2, r1, yEqualsZero);
    SIMDVector r4 = VectorSelect(threePiOverFour, piOverFour, xIsPositive);
    SIMDVector r5 = VectorSelect(piOverTwo, r4, xEqualsInfinity);
    SIMDVector result = VectorSelect(r3, r5, yEqualsInfinity);
    atanResultValid = VectorEqualInt(result, atanResultValid);

    r1 = VectorSelect(pi, s_SIMDNegativeZero, xIsPositive);
    r2 = VectorAdd(atanResult, r1);
    return VectorSelect(result, r2, atanResultValid);
}

};

NWB_INLINE SIMDVector SIMDCALL VectorASin(SIMDVector value)noexcept{
    const SIMDVector nonnegative = VectorGreaterOrEqual(value, s_SIMDZero);
    SIMDVector t0 = SIMDVectorDetail::VectorArcCoefficientApproximation(value);
    SIMDVector t1 = VectorSubtract(s_SIMDPi, t0);
    t0 = VectorSelect(t1, t0, nonnegative);
    return VectorSubtract(s_SIMDHalfPi, t0);
}

NWB_INLINE SIMDVector SIMDCALL VectorACos(SIMDVector value)noexcept{
    const SIMDVector nonnegative = VectorGreaterOrEqual(value, s_SIMDZero);
    const SIMDVector t0 = SIMDVectorDetail::VectorArcCoefficientApproximation(value);
    SIMDVector t1 = VectorSubtract(s_SIMDPi, t0);
    return VectorSelect(t1, t0, nonnegative);
}

NWB_INLINE SIMDVector SIMDCALL VectorATan(SIMDVector value)noexcept{
    SIMDVector absV = VectorAbs(value);
    SIMDVector invV = VectorReciprocal(value);
    SIMDVector comp = VectorGreater(value, s_SIMDOne);
    SIMDVector sign = VectorSelect(s_SIMDNegativeOne, s_SIMDOne, comp);
    comp = VectorLessOrEqual(absV, s_SIMDOne);
    sign = VectorSelect(sign, s_SIMDZero, comp);
    SIMDVector x = VectorSelect(invV, value, comp);

    SIMDVector x2 = VectorMultiply(x, x);
    SIMDVector result = VectorMultiplyAdd(VectorSplatW(s_SIMDATanCoefficients1), x2, VectorSplatZ(s_SIMDATanCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDATanCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDATanCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatW(s_SIMDATanCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatZ(s_SIMDATanCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDATanCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDATanCoefficients0));
    result = VectorMultiplyAdd(result, x2, s_SIMDOne);
    result = VectorMultiply(result, x);

    SIMDVector result1 = VectorSubtract(VectorMultiply(sign, s_SIMDHalfPi), result);
    comp = VectorEqual(sign, s_SIMDZero);
    return VectorSelect(result1, result, comp);
}

NWB_INLINE SIMDVector SIMDCALL VectorATan2(SIMDVector y, SIMDVector x)noexcept{
    const SIMDVector constants = VectorSet(s_PI, s_PIDIV2, s_PIDIV4, s_PI * 0.75f);
    SIMDVector v = VectorDivide(y, x);
    return SIMDVectorDetail::VectorATan2SelectResult(y, x, VectorATan(v), constants);
}

NWB_INLINE SIMDVector SIMDCALL VectorSinEst(SIMDVector value)noexcept{
    SIMDVector cosSignSelect;
    SIMDVector x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);

    SIMDVector x2 = VectorMultiply(x, x);
    SIMDVector result = VectorMultiplyAdd(VectorSplatW(s_SIMDSinCoefficients1), x2, VectorSplatZ(s_SIMDSinCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDSinCoefficients1));
    result = VectorMultiplyAdd(result, x2, s_SIMDOne);
    return VectorMultiply(result, x);
}

NWB_INLINE SIMDVector SIMDCALL VectorCosEst(SIMDVector value)noexcept{
    SIMDVector cosSignSelect;
    SIMDVector x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVector sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVector x2 = VectorMultiply(x, x);
    SIMDVector result = VectorMultiplyAdd(VectorSplatW(s_SIMDCosCoefficients1), x2, VectorSplatZ(s_SIMDCosCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDCosCoefficients1));
    result = VectorMultiplyAdd(result, x2, s_SIMDOne);
    return VectorMultiply(result, sign);
}

NWB_INLINE void SIMDCALL VectorSinCosEst(SIMDVector* outSin, SIMDVector* outCos, SIMDVector value)noexcept{
    NWB_ASSERT(outSin != nullptr);
    NWB_ASSERT(outCos != nullptr);

    SIMDVector cosSignSelect;
    SIMDVector x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVector sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVector x2 = VectorMultiply(x, x);

    SIMDVector result = VectorMultiplyAdd(VectorSplatW(s_SIMDSinCoefficients1), x2, VectorSplatZ(s_SIMDSinCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDSinCoefficients1));
    result = VectorMultiplyAdd(result, x2, s_SIMDOne);
    *outSin = VectorMultiply(result, x);

    result = VectorMultiplyAdd(VectorSplatW(s_SIMDCosCoefficients1), x2, VectorSplatZ(s_SIMDCosCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDCosCoefficients1));
    result = VectorMultiplyAdd(result, x2, s_SIMDOne);
    *outCos = VectorMultiply(result, sign);
}

NWB_INLINE SIMDVector SIMDCALL VectorTanEst(SIMDVector value)noexcept{
    SIMDVector v1 = VectorMultiply(value, VectorSplatW(s_SIMDTanEstCoefficients));
    v1 = VectorRound(v1);
    v1 = VectorNegativeMultiplySubtract(s_SIMDPi, v1, value);

    SIMDVector t0 = VectorSplatX(s_SIMDTanEstCoefficients);
    SIMDVector t1 = VectorSplatY(s_SIMDTanEstCoefficients);
    SIMDVector t2 = VectorSplatZ(s_SIMDTanEstCoefficients);

    SIMDVector v2t2 = VectorNegativeMultiplySubtract(v1, v1, t2);
    SIMDVector v2 = VectorMultiply(v1, v1);
    SIMDVector v1t0 = VectorMultiply(v1, t0);
    SIMDVector v1t1 = VectorMultiply(v1, t1);

    SIMDVector d = VectorReciprocalEst(v2t2);
    SIMDVector n = VectorMultiplyAdd(v2, v1t1, v1t0);
    return VectorMultiply(n, d);
}

NWB_INLINE SIMDVector SIMDCALL VectorASinEst(SIMDVector value)noexcept{
    SIMDVector nonnegative = VectorGreaterOrEqual(value, s_SIMDZero);
    SIMDVector x = VectorAbs(value);
    SIMDVector root = VectorSqrt(VectorMax(s_SIMDZero, VectorSubtract(s_SIMDOne, x)));

    SIMDVector t0 = VectorMultiplyAdd(VectorSplatW(s_SIMDArcEstCoefficients), x, VectorSplatZ(s_SIMDArcEstCoefficients));
    t0 = VectorMultiplyAdd(t0, x, VectorSplatY(s_SIMDArcEstCoefficients));
    t0 = VectorMultiplyAdd(t0, x, VectorSplatX(s_SIMDArcEstCoefficients));
    t0 = VectorMultiply(t0, root);

    SIMDVector t1 = VectorSubtract(s_SIMDPi, t0);
    t0 = VectorSelect(t1, t0, nonnegative);
    return VectorSubtract(s_SIMDHalfPi, t0);
}

NWB_INLINE SIMDVector SIMDCALL VectorACosEst(SIMDVector value)noexcept{
    SIMDVector nonnegative = VectorGreaterOrEqual(value, s_SIMDZero);
    SIMDVector x = VectorAbs(value);
    SIMDVector root = VectorSqrt(VectorMax(s_SIMDZero, VectorSubtract(s_SIMDOne, x)));

    SIMDVector t0 = VectorMultiplyAdd(VectorSplatW(s_SIMDArcEstCoefficients), x, VectorSplatZ(s_SIMDArcEstCoefficients));
    t0 = VectorMultiplyAdd(t0, x, VectorSplatY(s_SIMDArcEstCoefficients));
    t0 = VectorMultiplyAdd(t0, x, VectorSplatX(s_SIMDArcEstCoefficients));
    t0 = VectorMultiply(t0, root);

    SIMDVector t1 = VectorSubtract(s_SIMDPi, t0);
    return VectorSelect(t1, t0, nonnegative);
}

NWB_INLINE SIMDVector SIMDCALL VectorATanEst(SIMDVector value)noexcept{
    SIMDVector absV = VectorAbs(value);
    SIMDVector invV = VectorReciprocalEst(value);
    SIMDVector comp = VectorGreater(value, s_SIMDOne);
    SIMDVector sign = VectorSelect(s_SIMDNegativeOne, s_SIMDOne, comp);
    comp = VectorLessOrEqual(absV, s_SIMDOne);
    sign = VectorSelect(sign, s_SIMDZero, comp);
    SIMDVector x = VectorSelect(invV, value, comp);

    SIMDVector x2 = VectorMultiply(x, x);
    SIMDVector result = VectorMultiplyAdd(VectorSplatW(s_SIMDATanEstCoefficients1), x2, VectorSplatZ(s_SIMDATanEstCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDATanEstCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDATanEstCoefficients1));
    result = VectorMultiplyAdd(result, x2, s_SIMDATanEstCoefficients0);
    result = VectorMultiply(result, x);

    SIMDVector result1 = VectorSubtract(VectorMultiply(sign, s_SIMDHalfPi), result);
    comp = VectorEqual(sign, s_SIMDZero);
    return VectorSelect(result1, result, comp);
}

NWB_INLINE SIMDVector SIMDCALL VectorATan2Est(SIMDVector y, SIMDVector x)noexcept{
    const SIMDVector constants = VectorSet(s_PI, s_PIDIV2, s_PIDIV4, 2.3561944905f);
    SIMDVector v = VectorMultiply(y, VectorReciprocalEst(x));
    return SIMDVectorDetail::VectorATan2SelectResult(y, x, VectorATanEst(v), constants);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// interpolation


NWB_INLINE SIMDVector SIMDCALL VectorLerp(SIMDVector v0, SIMDVector v1, f32 t)noexcept{
    return VectorMultiplyAdd(VectorSubtract(v1, v0), VectorReplicate(t), v0);
}

NWB_INLINE SIMDVector SIMDCALL VectorLerpV(SIMDVector v0, SIMDVector v1, SIMDVector t)noexcept{
    return VectorMultiplyAdd(VectorSubtract(v1, v0), t, v0);
}

NWB_INLINE SIMDVector SIMDCALL VectorHermite(SIMDVector position0, SIMDVector tangent0, SIMDVector position1, SIMDVector tangent1, f32 t)noexcept{
    const f32 t2 = t * t;
    const f32 t3 = t2 * t;
    const f32 p0 = 2.0f * t3 - 3.0f * t2 + 1.0f;
    const f32 t0 = t3 - 2.0f * t2 + t;
    const f32 p1 = -2.0f * t3 + 3.0f * t2;
    const f32 t1 = t3 - t2;
    SIMDVector result = VectorScale(position0, p0);
    result = VectorMultiplyAdd(tangent0, VectorReplicate(t0), result);
    result = VectorMultiplyAdd(position1, VectorReplicate(p1), result);
    return VectorMultiplyAdd(tangent1, VectorReplicate(t1), result);
}

NWB_INLINE SIMDVector SIMDCALL VectorHermiteV(SIMDVector position0, SIMDVector tangent0, SIMDVector position1, SIMDVector tangent1, SIMDVector t)noexcept{
    const SIMDVector catMulT2 = VectorSet(-3.0f, -2.0f, 3.0f, -1.0f);
    const SIMDVector catMulT3 = VectorSet(2.0f, 1.0f, -2.0f, 1.0f);
    SIMDVector t2 = VectorMultiply(t, t);
    SIMDVector t3 = VectorMultiply(t, t2);
    t2 = VectorMultiply(t2, catMulT2);
    t3 = VectorMultiplyAdd(t3, catMulT3, t2);
    t3 = VectorAdd(t3, VectorAndInt(t, s_SIMDMaskY));
    t3 = VectorAdd(t3, s_SIMDIdentityR0);

    SIMDVector result = VectorMultiply(VectorSplatX(t3), position0);
    result = VectorMultiplyAdd(VectorSplatY(t3), tangent0, result);
    result = VectorMultiplyAdd(VectorSplatZ(t3), position1, result);
    return VectorMultiplyAdd(VectorSplatW(t3), tangent1, result);
}

NWB_INLINE SIMDVector SIMDCALL VectorCatmullRom(SIMDVector p0, SIMDVector p1, SIMDVector p2, SIMDVector p3, f32 t)noexcept{
    const f32 t2 = t * t;
    const f32 t3 = t2 * t;
    const f32 c0 = (-t3 + 2.0f * t2 - t) * 0.5f;
    const f32 c1 = (3.0f * t3 - 5.0f * t2 + 2.0f) * 0.5f;
    const f32 c2 = (-3.0f * t3 + 4.0f * t2 + t) * 0.5f;
    const f32 c3 = (t3 - t2) * 0.5f;
    SIMDVector result = VectorScale(p1, c1);
    result = VectorMultiplyAdd(p0, VectorReplicate(c0), result);
    SIMDVector temp = VectorScale(p3, c3);
    temp = VectorMultiplyAdd(p2, VectorReplicate(c2), temp);
    return VectorAdd(result, temp);
}

NWB_INLINE SIMDVector SIMDCALL VectorCatmullRomV(SIMDVector p0, SIMDVector p1, SIMDVector p2, SIMDVector p3, SIMDVector t)noexcept{
    const SIMDVector three = VectorReplicate(3.0f);
    const SIMDVector five = VectorReplicate(5.0f);
    SIMDVector t2 = VectorMultiply(t, t);
    SIMDVector t3 = VectorMultiply(t, t2);
    SIMDVector result = VectorSubtract(VectorAdd(t2, t2), t);
    result = VectorMultiply(VectorSubtract(result, t3), p0);
    SIMDVector temp = VectorNegativeMultiplySubtract(t2, five, VectorMultiply(t3, three));
    temp = VectorAdd(temp, s_SIMDTwo);
    result = VectorMultiplyAdd(temp, p1, result);
    temp = VectorNegativeMultiplySubtract(t3, three, VectorMultiply(t2, s_SIMDFour));
    temp = VectorAdd(temp, t);
    result = VectorMultiplyAdd(temp, p2, result);
    t3 = VectorSubtract(t3, t2);
    result = VectorMultiplyAdd(t3, p3, result);
    return VectorMultiply(result, s_SIMDOneHalf);
}

NWB_INLINE SIMDVector SIMDCALL VectorBaryCentric(SIMDVector p0, SIMDVector p1, SIMDVector p2, f32 f, f32 g)noexcept{
    const SIMDVector p10 = VectorSubtract(p1, p0);
    const SIMDVector p20 = VectorSubtract(p2, p0);
    SIMDVector result = VectorMultiplyAdd(p10, VectorReplicate(f), p0);
    return VectorMultiplyAdd(p20, VectorReplicate(g), result);
}

NWB_INLINE SIMDVector SIMDCALL VectorBaryCentricV(SIMDVector p0, SIMDVector p1, SIMDVector p2, SIMDVector f, SIMDVector g)noexcept{
    const SIMDVector p10 = VectorSubtract(p1, p0);
    const SIMDVector p20 = VectorSubtract(p2, p0);
    SIMDVector result = VectorMultiplyAdd(p10, f, p0);
    return VectorMultiplyAdd(p20, g, result);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 2D helpers


NWB_INLINE bool SIMDCALL Vector2Equal(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorEqual(v0, v1)) & 0x3u) == 0x3u; }
NWB_INLINE bool SIMDCALL Vector2EqualInt(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorEqualInt(v0, v1)) & 0x3u) == 0x3u; }
NWB_INLINE bool SIMDCALL Vector2NearEqual(SIMDVector v0, SIMDVector v1, SIMDVector epsilon)noexcept{ return (VectorMoveMask(VectorNearEqual(v0, v1, epsilon)) & 0x3u) == 0x3u; }
NWB_INLINE bool SIMDCALL Vector2NotEqual(SIMDVector v0, SIMDVector v1)noexcept{ return !Vector2Equal(v0, v1); }
NWB_INLINE bool SIMDCALL Vector2NotEqualInt(SIMDVector v0, SIMDVector v1)noexcept{ return !Vector2EqualInt(v0, v1); }
NWB_INLINE bool SIMDCALL Vector2Greater(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorGreater(v0, v1)) & 0x3u) == 0x3u; }
NWB_INLINE bool SIMDCALL Vector2GreaterOrEqual(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorGreaterOrEqual(v0, v1)) & 0x3u) == 0x3u; }
NWB_INLINE bool SIMDCALL Vector2Less(SIMDVector v0, SIMDVector v1)noexcept{ return Vector2Greater(v1, v0); }
NWB_INLINE bool SIMDCALL Vector2LessOrEqual(SIMDVector v0, SIMDVector v1)noexcept{ return Vector2GreaterOrEqual(v1, v0); }
NWB_INLINE bool SIMDCALL Vector2InBounds(SIMDVector value, SIMDVector bounds)noexcept{ return (VectorMoveMask(VectorInBounds(value, bounds)) & 0x3u) == 0x3u; }
NWB_INLINE bool SIMDCALL Vector2IsNaN(SIMDVector value)noexcept{ return (VectorMoveMask(VectorIsNaN(value)) & 0x3u) != 0; }
NWB_INLINE bool SIMDCALL Vector2IsInfinite(SIMDVector value)noexcept{ return (VectorMoveMask(VectorIsInfinite(value)) & 0x3u) != 0; }

NWB_INLINE u32 SIMDCALL Vector2EqualR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqual(v0, v1)), 0x3u); }
NWB_INLINE u32 SIMDCALL Vector2EqualIntR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqualInt(v0, v1)), 0x3u); }
NWB_INLINE u32 SIMDCALL Vector2GreaterR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreater(v0, v1)), 0x3u); }
NWB_INLINE u32 SIMDCALL Vector2GreaterOrEqualR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreaterOrEqual(v0, v1)), 0x3u); }
NWB_INLINE u32 SIMDCALL Vector2InBoundsR(SIMDVector value, SIMDVector bounds)noexcept{ return SIMDVectorDetail::BoundsMaskR(VectorMoveMask(VectorInBounds(value, bounds)), 0x3u); }

NWB_INLINE SIMDVector SIMDCALL Vector2Dot(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(VectorGetX(v0) * VectorGetX(v1) + VectorGetY(v0) * VectorGetY(v1));
#elif defined(NWB_HAS_NEON)
    const float32x2_t product = vmul_f32(vget_low_f32(v0), vget_low_f32(v1));
    const float32x2_t result = vpadd_f32(product, product);
    return vcombine_f32(result, result);
#else
    return _mm_dp_ps(v0, v1, 0x3F);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2Cross(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(v0.f[0] * v1.f[1] - v0.f[1] * v1.f[0]);
#elif defined(NWB_HAS_NEON)
    const float32x2_t negate = vcreate_f32(0xBF8000003F800000ull);
    float32x2_t result = vmul_f32(vget_low_f32(v0), vrev64_f32(vget_low_f32(v1)));
    result = vmul_f32(result, negate);
    result = vpadd_f32(result, result);
    return vcombine_f32(result, result);
#else
    SIMDVector result = VectorSwizzle<1, 0, 1, 0>(v1);
    result = VectorMultiply(result, v0);
    const SIMDVector temp = VectorSplatY(result);
    result = _mm_sub_ss(result, temp);
    return VectorSplatX(result);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2LengthSq(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_dp_ps(value, value, 0x3F);
#else
    return Vector2Dot(value, value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2ReciprocalLength(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector lengthSq = _mm_dp_ps(value, value, 0x3F);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    return _mm_div_ps(s_SIMDOne, length);
#else
    return VectorReciprocalSqrt(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2ReciprocalLengthEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_rsqrt_ps(_mm_dp_ps(value, value, 0x3F));
#else
    return VectorReciprocalSqrtEst(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2Length(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x3F));
#else
    return VectorSqrt(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2LengthEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x3F));
#else
    return VectorSqrtEst(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2Normalize(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    SIMDVector lengthSq = _mm_dp_ps(value, value, 0x3F);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    const SIMDVector zeroMask = _mm_cmpneq_ps(_mm_setzero_ps(), length);
    lengthSq = _mm_cmpneq_ps(lengthSq, s_SIMDInfinity);
    SIMDVector result = _mm_div_ps(value, length);
    result = _mm_and_ps(result, zeroMask);
    return _mm_and_ps(_mm_or_ps(_mm_andnot_ps(lengthSq, s_SIMDQNaN), _mm_and_ps(result, lengthSq)), s_SIMDMaskXY);
#else
    const SIMDVector lengthSq = Vector2LengthSq(value);
    const SIMDVector length = VectorSqrt(lengthSq);
    SIMDVector result = VectorDivide(value, length);
    result = VectorAndInt(result, VectorNotEqual(length, VectorZero()));
    return VectorAndInt(VectorSelect(s_SIMDQNaN, result, VectorNotEqualInt(lengthSq, s_SIMDInfinity)), s_SIMDMaskXY);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2NormalizeEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector lengthSq = _mm_dp_ps(value, value, 0x3F);
    return _mm_and_ps(_mm_mul_ps(value, _mm_rsqrt_ps(lengthSq)), s_SIMDMaskXY);
#else
    return VectorAndInt(VectorMultiply(value, Vector2ReciprocalLengthEst(value)), s_SIMDMaskXY);
#endif
}

namespace SIMDVectorDetail{

NWB_INLINE SIMDVector SIMDCALL ClampLengthV(SIMDVector value, SIMDVector lengthSq, SIMDVector lengthMin, SIMDVector lengthMax)noexcept{
    const SIMDVector reciprocalLength = VectorReciprocalSqrt(lengthSq);
    const SIMDVector infiniteLength = VectorEqualInt(lengthSq, s_SIMDInfinity);
    const SIMDVector zeroLength = VectorEqual(lengthSq, s_SIMDZero);
    const SIMDVector select = VectorEqualInt(infiniteLength, zeroLength);
    SIMDVector normal = VectorMultiply(value, reciprocalLength);
    SIMDVector length = VectorMultiply(lengthSq, reciprocalLength);
    length = VectorSelect(lengthSq, length, select);
    normal = VectorSelect(lengthSq, normal, select);
    const SIMDVector controlMax = VectorGreater(length, lengthMax);
    const SIMDVector controlMin = VectorLess(length, lengthMin);
    SIMDVector clampedLength = VectorSelect(length, lengthMax, controlMax);
    clampedLength = VectorSelect(clampedLength, lengthMin, controlMin);
    SIMDVector result = VectorMultiply(normal, clampedLength);
    return VectorSelect(result, value, VectorEqualInt(controlMax, controlMin));
}

NWB_INLINE SIMDVector SIMDCALL RefractV(
    SIMDVector incident,
    SIMDVector normal,
    SIMDVector refractionIndex,
    SIMDVector dot)noexcept
{
    SIMDVector r = VectorNegativeMultiplySubtract(dot, dot, s_SIMDOne);
    const SIMDVector refractionIndexSq = VectorMultiply(refractionIndex, refractionIndex);
    r = VectorNegativeMultiplySubtract(r, refractionIndexSq, s_SIMDOne);
    const SIMDVector totalInternalReflection = VectorLess(r, s_SIMDZero);
    r = VectorMultiplyAdd(refractionIndex, dot, VectorSqrt(VectorMax(r, s_SIMDZero)));
    SIMDVector result = VectorMultiply(refractionIndex, incident);
    result = VectorNegativeMultiplySubtract(r, normal, result);
    return VectorSelect(result, VectorZero(), totalInternalReflection);
}

}

NWB_INLINE SIMDVector SIMDCALL Vector2ClampLengthV(SIMDVector value, SIMDVector lengthMin, SIMDVector lengthMax)noexcept{
    NWB_ASSERT(VectorGetY(lengthMin) == VectorGetX(lengthMin));
    NWB_ASSERT(VectorGetY(lengthMax) == VectorGetX(lengthMax));
    NWB_ASSERT(Vector2GreaterOrEqual(lengthMin, s_SIMDZero));
    NWB_ASSERT(Vector2GreaterOrEqual(lengthMax, s_SIMDZero));
    NWB_ASSERT(Vector2GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVector lengthSq = Vector2LengthSq(value);
    return VectorAndInt(SIMDVectorDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax), s_SIMDMaskXY);
}

NWB_INLINE SIMDVector SIMDCALL Vector2ClampLength(SIMDVector value, f32 lengthMin, f32 lengthMax)noexcept{
    return Vector2ClampLengthV(value, VectorReplicate(lengthMin), VectorReplicate(lengthMax));
}

NWB_INLINE SIMDVector SIMDCALL Vector2Reflect(SIMDVector incident, SIMDVector normal)noexcept{
    SIMDVector result = Vector2Dot(incident, normal);
    result = VectorAdd(result, result);
    return VectorNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVector SIMDCALL Vector2RefractV(SIMDVector incident, SIMDVector normal, SIMDVector refractionIndex)noexcept{
    return SIMDVectorDetail::RefractV(incident, normal, refractionIndex, Vector2Dot(incident, normal));
}

NWB_INLINE SIMDVector SIMDCALL Vector2Refract(SIMDVector incident, SIMDVector normal, f32 refractionIndex)noexcept{
    return Vector2RefractV(incident, normal, VectorReplicate(refractionIndex));
}

NWB_INLINE SIMDVector SIMDCALL Vector2Orthogonal(SIMDVector value)noexcept{
    return VectorMultiply(VectorSwizzle<1, 0, 2, 3>(value), VectorSet(-1.0f, 1.0f, 0.0f, 0.0f));
}

NWB_INLINE SIMDVector SIMDCALL Vector2AngleBetweenNormals(SIMDVector n0, SIMDVector n1)noexcept{
    return VectorACos(VectorClamp(Vector2Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne));
}

NWB_INLINE SIMDVector SIMDCALL Vector2AngleBetweenNormalsEst(SIMDVector n0, SIMDVector n1)noexcept{ return VectorACosEst(VectorClamp(Vector2Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVector SIMDCALL Vector2AngleBetweenVectors(SIMDVector v0, SIMDVector v1)noexcept{
    const SIMDVector reciprocalLength0 = Vector2ReciprocalLength(v0);
    const SIMDVector reciprocalLength1 = Vector2ReciprocalLength(v1);
    SIMDVector cosAngle = VectorMultiply(Vector2Dot(v0, v1), VectorMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorClamp(cosAngle, s_SIMDNegativeOne, s_SIMDOne);
    return VectorACos(cosAngle);
}

NWB_INLINE SIMDVector SIMDCALL Vector2LinePointDistance(SIMDVector linePoint0, SIMDVector linePoint1, SIMDVector point)noexcept{
    const SIMDVector line = VectorSubtract(linePoint1, linePoint0);
    const SIMDVector pointVector = VectorSubtract(point, linePoint0);
    const SIMDVector projectionScale = VectorDivide(Vector2Dot(pointVector, line), Vector2LengthSq(line));
    return Vector2Length(VectorSubtract(pointVector, VectorMultiply(line, projectionScale)));
}

NWB_INLINE SIMDVector SIMDCALL Vector2IntersectLine(SIMDVector line1Point1, SIMDVector line1Point2, SIMDVector line2Point1, SIMDVector line2Point2)noexcept{
    const SIMDVector v1 = VectorSubtract(line1Point2, line1Point1);
    const SIMDVector v2 = VectorSubtract(line2Point2, line2Point1);
    const SIMDVector v3 = VectorSubtract(line1Point1, line2Point1);
    const SIMDVector c1 = Vector2Cross(v1, v2);
    const SIMDVector c2 = Vector2Cross(v2, v3);

    const SIMDVector resultMask = VectorGreater(VectorAbs(c1), s_SIMDEpsilon);
    const SIMDVector failMask = VectorLessOrEqual(VectorAbs(c2), s_SIMDEpsilon);
    const SIMDVector fail = VectorOrInt(VectorAndInt(failMask, s_SIMDInfinity), VectorAndCInt(s_SIMDQNaN, failMask));
    const SIMDVector intersection = VectorMultiplyAdd(v1, VectorDivide(c2, c1), line1Point1);
    return VectorSelect(fail, intersection, resultMask);
}

NWB_INLINE SIMDVector SIMDCALL Vector2Transform(SIMDVector value, const SIMDMatrix& matrix)noexcept;
NWB_INLINE SIMDVector SIMDCALL Vector2TransformCoord(SIMDVector value, const SIMDMatrix& matrix)noexcept;
NWB_INLINE SIMDVector SIMDCALL Vector2TransformNormal(SIMDVector value, const SIMDMatrix& matrix)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 3D helpers


NWB_INLINE bool SIMDCALL Vector3Equal(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorEqual(v0, v1)) & 0x7u) == 0x7u; }
NWB_INLINE bool SIMDCALL Vector3EqualInt(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorEqualInt(v0, v1)) & 0x7u) == 0x7u; }
NWB_INLINE bool SIMDCALL Vector3NearEqual(SIMDVector v0, SIMDVector v1, SIMDVector epsilon)noexcept{ return (VectorMoveMask(VectorNearEqual(v0, v1, epsilon)) & 0x7u) == 0x7u; }
NWB_INLINE bool SIMDCALL Vector3NotEqual(SIMDVector v0, SIMDVector v1)noexcept{ return !Vector3Equal(v0, v1); }
NWB_INLINE bool SIMDCALL Vector3NotEqualInt(SIMDVector v0, SIMDVector v1)noexcept{ return !Vector3EqualInt(v0, v1); }
NWB_INLINE bool SIMDCALL Vector3Greater(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorGreater(v0, v1)) & 0x7u) == 0x7u; }
NWB_INLINE bool SIMDCALL Vector3GreaterOrEqual(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorGreaterOrEqual(v0, v1)) & 0x7u) == 0x7u; }
NWB_INLINE bool SIMDCALL Vector3Less(SIMDVector v0, SIMDVector v1)noexcept{ return Vector3Greater(v1, v0); }
NWB_INLINE bool SIMDCALL Vector3LessOrEqual(SIMDVector v0, SIMDVector v1)noexcept{ return Vector3GreaterOrEqual(v1, v0); }
NWB_INLINE bool SIMDCALL Vector3InBounds(SIMDVector value, SIMDVector bounds)noexcept{ return (VectorMoveMask(VectorInBounds(value, bounds)) & 0x7u) == 0x7u; }
NWB_INLINE bool SIMDCALL Vector3IsNaN(SIMDVector value)noexcept{ return (VectorMoveMask(VectorIsNaN(value)) & 0x7u) != 0; }
NWB_INLINE bool SIMDCALL Vector3IsInfinite(SIMDVector value)noexcept{ return (VectorMoveMask(VectorIsInfinite(value)) & 0x7u) != 0; }

NWB_INLINE u32 SIMDCALL Vector3EqualR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqual(v0, v1)), 0x7u); }
NWB_INLINE u32 SIMDCALL Vector3EqualIntR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqualInt(v0, v1)), 0x7u); }
NWB_INLINE u32 SIMDCALL Vector3GreaterR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreater(v0, v1)), 0x7u); }
NWB_INLINE u32 SIMDCALL Vector3GreaterOrEqualR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreaterOrEqual(v0, v1)), 0x7u); }
NWB_INLINE u32 SIMDCALL Vector3InBoundsR(SIMDVector value, SIMDVector bounds)noexcept{ return SIMDVectorDetail::BoundsMaskR(VectorMoveMask(VectorInBounds(value, bounds)), 0x7u); }

NWB_INLINE SIMDVector SIMDCALL Vector3Dot(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(VectorGetX(v0) * VectorGetX(v1) + VectorGetY(v0) * VectorGetY(v1) + VectorGetZ(v0) * VectorGetZ(v1));
#elif defined(NWB_HAS_NEON)
    float32x4_t product = vmulq_f32(v0, v1);
    float32x2_t lo = vget_low_f32(product);
    float32x2_t hi = vget_high_f32(product);
    lo = vpadd_f32(lo, lo);
    hi = vdup_lane_f32(hi, 0);
    lo = vadd_f32(lo, hi);
    return vcombine_f32(lo, lo);
#else
    return _mm_dp_ps(v0, v1, 0x7F);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3Cross(SIMDVector v0, SIMDVector v1)noexcept{
    SIMDVector temp0 = VectorSwizzle<1, 2, 0, 3>(v0);
    SIMDVector temp1 = VectorSwizzle<2, 0, 1, 3>(v1);
    SIMDVector result = VectorMultiply(temp0, temp1);
    temp0 = VectorSwizzle<2, 0, 1, 3>(v0);
    temp1 = VectorSwizzle<1, 2, 0, 3>(v1);
    return VectorAndInt(VectorNegativeMultiplySubtract(temp0, temp1, result), s_SIMDMask3);
}

NWB_INLINE SIMDVector SIMDCALL Vector3LengthSq(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_dp_ps(value, value, 0x7F);
#else
    return Vector3Dot(value, value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3ReciprocalLength(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector lengthSq = _mm_dp_ps(value, value, 0x7F);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    return _mm_div_ps(s_SIMDOne, length);
#else
    return VectorReciprocalSqrt(Vector3LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3ReciprocalLengthEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_rsqrt_ps(_mm_dp_ps(value, value, 0x7F));
#else
    return VectorReciprocalSqrtEst(Vector3LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3Length(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x7F));
#else
    return VectorSqrt(Vector3LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3LengthEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x7F));
#else
    return VectorSqrtEst(Vector3LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3Normalize(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    SIMDVector lengthSq = _mm_dp_ps(value, value, 0x7F);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    const SIMDVector zeroMask = _mm_cmpneq_ps(_mm_setzero_ps(), length);
    lengthSq = _mm_cmpneq_ps(lengthSq, s_SIMDInfinity);
    SIMDVector result = _mm_div_ps(value, length);
    result = _mm_and_ps(result, zeroMask);
    return _mm_and_ps(_mm_or_ps(_mm_andnot_ps(lengthSq, s_SIMDQNaN), _mm_and_ps(result, lengthSq)), s_SIMDMask3);
#else
    const SIMDVector lengthSq = Vector3LengthSq(value);
    const SIMDVector length = VectorSqrt(lengthSq);
    SIMDVector result = VectorDivide(value, length);
    result = VectorAndInt(result, VectorNotEqual(length, VectorZero()));
    return VectorAndInt(VectorSelect(s_SIMDQNaN, result, VectorNotEqualInt(lengthSq, s_SIMDInfinity)), s_SIMDMask3);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3NormalizeEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector lengthSq = _mm_dp_ps(value, value, 0x7F);
    return _mm_and_ps(_mm_mul_ps(value, _mm_rsqrt_ps(lengthSq)), s_SIMDMask3);
#else
    return VectorAndInt(VectorMultiply(value, Vector3ReciprocalLengthEst(value)), s_SIMDMask3);
#endif
}
NWB_INLINE SIMDVector SIMDCALL Vector3ClampLengthV(SIMDVector value, SIMDVector lengthMin, SIMDVector lengthMax)noexcept{
    NWB_ASSERT((VectorGetY(lengthMin) == VectorGetX(lengthMin)) && (VectorGetZ(lengthMin) == VectorGetX(lengthMin)));
    NWB_ASSERT((VectorGetY(lengthMax) == VectorGetX(lengthMax)) && (VectorGetZ(lengthMax) == VectorGetX(lengthMax)));
    NWB_ASSERT(Vector3GreaterOrEqual(lengthMin, s_SIMDZero));
    NWB_ASSERT(Vector3GreaterOrEqual(lengthMax, s_SIMDZero));
    NWB_ASSERT(Vector3GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVector lengthSq = Vector3LengthSq(value);
    return VectorAndInt(SIMDVectorDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax), s_SIMDMask3);
}

NWB_INLINE SIMDVector SIMDCALL Vector3ClampLength(SIMDVector value, f32 lengthMin, f32 lengthMax)noexcept{
    return Vector3ClampLengthV(value, VectorReplicate(lengthMin), VectorReplicate(lengthMax));
}
NWB_INLINE SIMDVector SIMDCALL Vector3Reflect(SIMDVector incident, SIMDVector normal)noexcept{
    SIMDVector result = Vector3Dot(incident, normal);
    result = VectorAdd(result, result);
    return VectorNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVector SIMDCALL Vector3RefractV(SIMDVector incident, SIMDVector normal, SIMDVector refractionIndex)noexcept{
    return SIMDVectorDetail::RefractV(incident, normal, refractionIndex, Vector3Dot(incident, normal));
}

NWB_INLINE SIMDVector SIMDCALL Vector3Refract(SIMDVector incident, SIMDVector normal, f32 refractionIndex)noexcept{ return Vector3RefractV(incident, normal, VectorReplicate(refractionIndex)); }

NWB_INLINE SIMDVector SIMDCALL Vector3Rotate(SIMDVector value, SIMDVector rotationQuaternion)noexcept{
    const SIMDVector q = VectorAndInt(rotationQuaternion, s_SIMDMask3);
    const SIMDVector t = VectorScale(Vector3Cross(q, value), 2.0f);
    return VectorAdd(VectorAdd(value, VectorMultiply(t, VectorSplatW(rotationQuaternion))), Vector3Cross(q, t));
}

NWB_INLINE SIMDVector SIMDCALL Vector3InverseRotate(SIMDVector value, SIMDVector rotationQuaternion)noexcept{
    return Vector3Rotate(value, VectorXorInt(rotationQuaternion, s_SIMDNegate3));
}

NWB_INLINE SIMDVector SIMDCALL Vector3Orthogonal(SIMDVector value)noexcept{
    const SIMDVector zero = VectorZero();
    const SIMDVector z = VectorSplatZ(value);
    const SIMDVector yzyy = VectorSwizzle<1, 2, 1, 1>(value);
    const SIMDVector negativeValue = VectorSubtract(zero, value);
    const SIMDVector zIsNegative = VectorLess(z, zero);
    const SIMDVector yzyyIsNegative = VectorLess(yzyy, zero);
    const SIMDVector s = VectorAdd(yzyy, z);
    const SIMDVector d = VectorSubtract(yzyy, z);
    const SIMDVector select = VectorEqualInt(zIsNegative, yzyyIsNegative);
    const SIMDVector r0 = VectorPermute<4, 0, 0, 0>(negativeValue, s);
    const SIMDVector r1 = VectorPermute<4, 0, 0, 0>(value, d);
    return VectorSelect(r1, r0, select);
}

NWB_INLINE SIMDVector SIMDCALL Vector3AngleBetweenNormals(SIMDVector n0, SIMDVector n1)noexcept{ return VectorACos(VectorClamp(Vector3Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVector SIMDCALL Vector3AngleBetweenNormalsEst(SIMDVector n0, SIMDVector n1)noexcept{ return VectorACosEst(VectorClamp(Vector3Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVector SIMDCALL Vector3AngleBetweenVectors(SIMDVector v0, SIMDVector v1)noexcept{
    const SIMDVector reciprocalLength0 = Vector3ReciprocalLength(v0);
    const SIMDVector reciprocalLength1 = Vector3ReciprocalLength(v1);
    SIMDVector cosAngle = VectorMultiply(Vector3Dot(v0, v1), VectorMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorClamp(cosAngle, s_SIMDNegativeOne, s_SIMDOne);
    return VectorACos(cosAngle);
}

NWB_INLINE SIMDVector SIMDCALL Vector3LinePointDistance(SIMDVector linePoint0, SIMDVector linePoint1, SIMDVector point)noexcept{
    const SIMDVector line = VectorSubtract(linePoint1, linePoint0);
    const SIMDVector pointVector = VectorSubtract(point, linePoint0);
    const SIMDVector projectionScale = VectorDivide(Vector3Dot(pointVector, line), Vector3LengthSq(line));
    return Vector3Length(VectorSubtract(pointVector, VectorMultiply(line, projectionScale)));
}

NWB_INLINE void SIMDCALL Vector3ComponentsFromNormal(SIMDVector* outParallel, SIMDVector* outPerpendicular, SIMDVector value, SIMDVector normal)noexcept{
    NWB_ASSERT(outParallel != nullptr);
    NWB_ASSERT(outPerpendicular != nullptr);
    const SIMDVector scale = Vector3Dot(value, normal);
    *outParallel = VectorMultiply(normal, scale);
    *outPerpendicular = VectorSubtract(value, *outParallel);
}

NWB_INLINE SIMDVector SIMDCALL Vector3Transform(SIMDVector value, const SIMDMatrix& matrix)noexcept;
NWB_INLINE SIMDVector SIMDCALL Vector3TransformCoord(SIMDVector value, const SIMDMatrix& matrix)noexcept;
NWB_INLINE SIMDVector SIMDCALL Vector3TransformNormal(SIMDVector value, const SIMDMatrix& matrix)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 4D helpers


NWB_INLINE bool SIMDCALL Vector4Equal(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorEqual(v0, v1)) & 0xFu) == 0xFu; }
NWB_INLINE bool SIMDCALL Vector4EqualInt(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorEqualInt(v0, v1)) & 0xFu) == 0xFu; }
NWB_INLINE bool SIMDCALL Vector4NearEqual(SIMDVector v0, SIMDVector v1, SIMDVector epsilon)noexcept{ return (VectorMoveMask(VectorNearEqual(v0, v1, epsilon)) & 0xFu) == 0xFu; }
NWB_INLINE bool SIMDCALL Vector4NotEqual(SIMDVector v0, SIMDVector v1)noexcept{ return !Vector4Equal(v0, v1); }
NWB_INLINE bool SIMDCALL Vector4NotEqualInt(SIMDVector v0, SIMDVector v1)noexcept{ return !Vector4EqualInt(v0, v1); }
NWB_INLINE bool SIMDCALL Vector4Greater(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorGreater(v0, v1)) & 0xFu) == 0xFu; }
NWB_INLINE bool SIMDCALL Vector4GreaterOrEqual(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorGreaterOrEqual(v0, v1)) & 0xFu) == 0xFu; }
NWB_INLINE bool SIMDCALL Vector4Less(SIMDVector v0, SIMDVector v1)noexcept{ return Vector4Greater(v1, v0); }
NWB_INLINE bool SIMDCALL Vector4LessOrEqual(SIMDVector v0, SIMDVector v1)noexcept{ return Vector4GreaterOrEqual(v1, v0); }
NWB_INLINE bool SIMDCALL Vector4InBounds(SIMDVector value, SIMDVector bounds)noexcept{ return (VectorMoveMask(VectorInBounds(value, bounds)) & 0xFu) == 0xFu; }
NWB_INLINE bool SIMDCALL Vector4IsNaN(SIMDVector value)noexcept{ return (VectorMoveMask(VectorIsNaN(value)) & 0xFu) != 0; }
NWB_INLINE bool SIMDCALL Vector4IsInfinite(SIMDVector value)noexcept{ return (VectorMoveMask(VectorIsInfinite(value)) & 0xFu) != 0; }

NWB_INLINE u32 SIMDCALL Vector4EqualR(SIMDVector v0, SIMDVector v1)noexcept{ return VectorEqualR(v0, v1); }
NWB_INLINE u32 SIMDCALL Vector4EqualIntR(SIMDVector v0, SIMDVector v1)noexcept{ return VectorEqualIntR(v0, v1); }
NWB_INLINE u32 SIMDCALL Vector4GreaterR(SIMDVector v0, SIMDVector v1)noexcept{ return VectorGreaterR(v0, v1); }
NWB_INLINE u32 SIMDCALL Vector4GreaterOrEqualR(SIMDVector v0, SIMDVector v1)noexcept{ return VectorGreaterOrEqualR(v0, v1); }
NWB_INLINE u32 SIMDCALL Vector4InBoundsR(SIMDVector value, SIMDVector bounds)noexcept{ return VectorInBoundsR(value, bounds); }

NWB_INLINE SIMDVector SIMDCALL Vector4Dot(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(VectorGetX(v0) * VectorGetX(v1) + VectorGetY(v0) * VectorGetY(v1) + VectorGetZ(v0) * VectorGetZ(v1) + VectorGetW(v0) * VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    float32x4_t product = vmulq_f32(v0, v1);
    float32x2_t lo = vget_low_f32(product);
    float32x2_t hi = vget_high_f32(product);
    lo = vadd_f32(lo, hi);
    lo = vpadd_f32(lo, lo);
    return vcombine_f32(lo, lo);
#else
    return _mm_dp_ps(v0, v1, 0xFF);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4LengthSq(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_dp_ps(value, value, 0xFF);
#else
    return Vector4Dot(value, value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4ReciprocalLength(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector lengthSq = _mm_dp_ps(value, value, 0xFF);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    return _mm_div_ps(s_SIMDOne, length);
#else
    return VectorReciprocalSqrt(Vector4LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4ReciprocalLengthEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_rsqrt_ps(_mm_dp_ps(value, value, 0xFF));
#else
    return VectorReciprocalSqrtEst(Vector4LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4Length(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0xFF));
#else
    return VectorSqrt(Vector4LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4LengthEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0xFF));
#else
    return VectorSqrtEst(Vector4LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4Normalize(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    SIMDVector lengthSq = _mm_dp_ps(value, value, 0xFF);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    const SIMDVector zeroMask = _mm_cmpneq_ps(_mm_setzero_ps(), length);
    lengthSq = _mm_cmpneq_ps(lengthSq, s_SIMDInfinity);
    SIMDVector result = _mm_div_ps(value, length);
    result = _mm_and_ps(result, zeroMask);
    return _mm_or_ps(_mm_andnot_ps(lengthSq, s_SIMDQNaN), _mm_and_ps(result, lengthSq));
#else
    const SIMDVector lengthSq = Vector4LengthSq(value);
    const SIMDVector length = VectorSqrt(lengthSq);
    SIMDVector result = VectorDivide(value, length);
    result = VectorAndInt(result, VectorNotEqual(length, VectorZero()));
    return VectorSelect(s_SIMDQNaN, result, VectorNotEqualInt(lengthSq, s_SIMDInfinity));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector4NormalizeEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector lengthSq = _mm_dp_ps(value, value, 0xFF);
    return _mm_mul_ps(value, _mm_rsqrt_ps(lengthSq));
#else
    return VectorMultiply(value, Vector4ReciprocalLengthEst(value));
#endif
}
NWB_INLINE SIMDVector SIMDCALL Vector4ClampLengthV(SIMDVector value, SIMDVector lengthMin, SIMDVector lengthMax)noexcept{
    NWB_ASSERT((VectorGetY(lengthMin) == VectorGetX(lengthMin)) && (VectorGetZ(lengthMin) == VectorGetX(lengthMin)) && (VectorGetW(lengthMin) == VectorGetX(lengthMin)));
    NWB_ASSERT((VectorGetY(lengthMax) == VectorGetX(lengthMax)) && (VectorGetZ(lengthMax) == VectorGetX(lengthMax)) && (VectorGetW(lengthMax) == VectorGetX(lengthMax)));
    NWB_ASSERT(Vector4GreaterOrEqual(lengthMin, s_SIMDZero));
    NWB_ASSERT(Vector4GreaterOrEqual(lengthMax, s_SIMDZero));
    NWB_ASSERT(Vector4GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVector lengthSq = Vector4LengthSq(value);
    return SIMDVectorDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax);
}

NWB_INLINE SIMDVector SIMDCALL Vector4ClampLength(SIMDVector value, f32 lengthMin, f32 lengthMax)noexcept{
    return Vector4ClampLengthV(value, VectorReplicate(lengthMin), VectorReplicate(lengthMax));
}
NWB_INLINE SIMDVector SIMDCALL Vector4Reflect(SIMDVector incident, SIMDVector normal)noexcept{
    SIMDVector result = Vector4Dot(incident, normal);
    result = VectorAdd(result, result);
    return VectorNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVector SIMDCALL Vector4RefractV(SIMDVector incident, SIMDVector normal, SIMDVector refractionIndex)noexcept{
    return SIMDVectorDetail::RefractV(incident, normal, refractionIndex, Vector4Dot(incident, normal));
}

NWB_INLINE SIMDVector SIMDCALL Vector4Refract(SIMDVector incident, SIMDVector normal, f32 refractionIndex)noexcept{ return Vector4RefractV(incident, normal, VectorReplicate(refractionIndex)); }
NWB_INLINE SIMDVector SIMDCALL Vector4Orthogonal(SIMDVector value)noexcept{ return VectorMultiply(VectorSwizzle<2, 3, 0, 1>(value), VectorSet(1.0f, 1.0f, -1.0f, -1.0f)); }
NWB_INLINE SIMDVector SIMDCALL Vector4AngleBetweenNormals(SIMDVector n0, SIMDVector n1)noexcept{ return VectorACos(VectorClamp(Vector4Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVector SIMDCALL Vector4AngleBetweenNormalsEst(SIMDVector n0, SIMDVector n1)noexcept{ return VectorACosEst(VectorClamp(Vector4Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVector SIMDCALL Vector4AngleBetweenVectors(SIMDVector v0, SIMDVector v1)noexcept{
    const SIMDVector reciprocalLength0 = Vector4ReciprocalLength(v0);
    const SIMDVector reciprocalLength1 = Vector4ReciprocalLength(v1);
    SIMDVector cosAngle = VectorMultiply(Vector4Dot(v0, v1), VectorMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorClamp(cosAngle, s_SIMDNegativeOne, s_SIMDOne);
    return VectorACos(cosAngle);
}

NWB_INLINE SIMDVector SIMDCALL Vector4Cross(SIMDVector v1, SIMDVector v2, SIMDVector v3)noexcept{
    SIMDVector result = VectorMultiply(VectorSwizzle<2, 3, 1, 2>(v2), VectorSwizzle<3, 2, 3, 1>(v3));
    SIMDVector temp = VectorSwizzle<3, 2, 3, 1>(v2);
    SIMDVector temp3 = VectorSwizzle<2, 3, 1, 2>(v3);
    result = VectorNegativeMultiplySubtract(temp, temp3, result);
    result = VectorMultiply(result, VectorSwizzle<1, 0, 0, 0>(v1));

    temp = VectorSwizzle<1, 3, 0, 2>(v2);
    temp3 = VectorSwizzle<3, 0, 3, 0>(v3);
    temp3 = VectorMultiply(temp3, temp);
    temp = VectorSwizzle<3, 0, 3, 0>(temp);
    SIMDVector temp1 = VectorSwizzle<1, 3, 0, 2>(v3);
    temp3 = VectorNegativeMultiplySubtract(temp, temp1, temp3);
    temp1 = VectorSwizzle<2, 2, 1, 1>(v1);
    result = VectorNegativeMultiplySubtract(temp1, temp3, result);

    temp = VectorSwizzle<1, 2, 0, 1>(v2);
    temp3 = VectorSwizzle<2, 0, 1, 0>(v3);
    temp3 = VectorMultiply(temp3, temp);
    temp = VectorSwizzle<1, 2, 0, 2>(temp);
    temp1 = VectorSwizzle<1, 2, 0, 1>(v3);
    temp3 = VectorNegativeMultiplySubtract(temp1, temp, temp3);
    temp1 = VectorSwizzle<3, 3, 3, 2>(v1);
    return VectorNegate(VectorMultiplyAdd(temp3, temp1, result));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// column-vector transforms over row-stored matrices


NWB_INLINE SIMDVector SIMDCALL Vector4Transform(SIMDVector value, const SIMDMatrix& matrix)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(
        VectorGetX(Vector4Dot(matrix.v[0], value)),
        VectorGetX(Vector4Dot(matrix.v[1], value)),
        VectorGetX(Vector4Dot(matrix.v[2], value)),
        VectorGetX(Vector4Dot(matrix.v[3], value))
    );
#elif defined(NWB_HAS_NEON)
    const float32x4x2_t p0 = vzipq_f32(matrix.v[0], matrix.v[2]);
    const float32x4x2_t p1 = vzipq_f32(matrix.v[1], matrix.v[3]);
    const float32x4x2_t t0 = vzipq_f32(p0.val[0], p1.val[0]);
    const float32x4x2_t t1 = vzipq_f32(p0.val[1], p1.val[1]);
    const float32x2_t low = vget_low_f32(value);
    const float32x2_t high = vget_high_f32(value);
    SIMDVector result = vmulq_lane_f32(t0.val[0], low, 0);
    result = vmlaq_lane_f32(result, t0.val[1], low, 1);
    result = vmlaq_lane_f32(result, t1.val[0], high, 0);
    return vmlaq_lane_f32(result, t1.val[1], high, 1);
#else
    const SIMDVector x = Vector4Dot(matrix.v[0], value);
    const SIMDVector y = Vector4Dot(matrix.v[1], value);
    const SIMDVector z = Vector4Dot(matrix.v[2], value);
    const SIMDVector w = Vector4Dot(matrix.v[3], value);
    const SIMDVector xy = _mm_unpacklo_ps(x, y);
    const SIMDVector zw = _mm_unpacklo_ps(z, w);
    return _mm_movelh_ps(xy, zw);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2Transform(SIMDVector value, const SIMDMatrix& matrix)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector v = _mm_or_ps(_mm_and_ps(value, s_SIMDMaskXY), s_SIMDIdentityR3);
    return SIMDVectorDetail::MatrixDotPack<0xBF>(matrix, v);
#else
    return Vector4Transform(VectorSetW(VectorSetZ(value, 0.0f), 1.0f), matrix);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2TransformCoord(SIMDVector value, const SIMDMatrix& matrix)noexcept{
    SIMDVector result = Vector2Transform(value, matrix);
    return VectorSetW(VectorDivide(result, VectorSplatW(result)), 1.0f);
}

NWB_INLINE SIMDVector SIMDCALL Vector2TransformNormal(SIMDVector value, const SIMDMatrix& matrix)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector v = _mm_and_ps(value, s_SIMDMaskXY);
    return SIMDVectorDetail::MatrixDotPack<0x3F>(matrix, v);
#else
    return Vector4Transform(VectorAndInt(value, s_SIMDMaskXY), matrix);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3Transform(SIMDVector value, const SIMDMatrix& matrix)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector v = _mm_or_ps(_mm_and_ps(value, s_SIMDMask3), s_SIMDIdentityR3);
    return SIMDVectorDetail::MatrixDotPack<0xFF>(matrix, v);
#else
    return Vector4Transform(VectorSetW(value, 1.0f), matrix);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3TransformCoord(SIMDVector value, const SIMDMatrix& matrix)noexcept{
    SIMDVector result = Vector3Transform(value, matrix);
    return VectorSetW(VectorDivide(result, VectorSplatW(result)), 1.0f);
}

NWB_INLINE SIMDVector SIMDCALL Vector3TransformNormal(SIMDVector value, const SIMDMatrix& matrix)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector v = _mm_and_ps(value, s_SIMDMask3);
    return SIMDVectorDetail::MatrixDotPack<0x7F>(matrix, v);
#else
    return Vector4Transform(VectorAndInt(value, s_SIMDMask3), matrix);
#endif
}

NWB_INLINE Float4U* SIMDCALL Vector2TransformStream(Float4U* outputStream, usize outputStride, const Float2U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    NWB_ASSERT(outputStream != nullptr);
    NWB_ASSERT(inputStream != nullptr);
    NWB_ASSERT(inputStride >= sizeof(Float2U));
    NWB_ASSERT(outputStride >= sizeof(Float4U));

    const SIMDMatrix transposedMatrix = SIMDVectorDetail::MatrixTransposeForTransform(matrix);
    for(usize i = 0; i < vectorCount; ++i){
        const SIMDVector value = LoadFloat(*SIMDVectorDetail::StridePointer(inputStream, inputStride, i));
        StoreFloat(
            SIMDVectorDetail::Vector4TransformTransposed(VectorSetW(VectorSetZ(value, 0.0f), 1.0f), transposedMatrix),
            SIMDVectorDetail::StridePointer(outputStream, outputStride, i)
        );
    }

    return outputStream;
}

NWB_INLINE Float2U* SIMDCALL Vector2TransformCoordStream(Float2U* outputStream, usize outputStride, const Float2U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    NWB_ASSERT(outputStream != nullptr);
    NWB_ASSERT(inputStream != nullptr);
    NWB_ASSERT(inputStride >= sizeof(Float2U));
    NWB_ASSERT(outputStride >= sizeof(Float2U));

    const SIMDMatrix transposedMatrix = SIMDVectorDetail::MatrixTransposeForTransform(matrix);
    for(usize i = 0; i < vectorCount; ++i){
        const SIMDVector value = LoadFloat(*SIMDVectorDetail::StridePointer(inputStream, inputStride, i));
        SIMDVector result = SIMDVectorDetail::Vector4TransformTransposed(
            VectorSetW(VectorSetZ(value, 0.0f), 1.0f),
            transposedMatrix
        );
        result = VectorSetW(VectorDivide(result, VectorSplatW(result)), 1.0f);
        StoreFloat(result, SIMDVectorDetail::StridePointer(outputStream, outputStride, i));
    }

    return outputStream;
}

NWB_INLINE Float2U* SIMDCALL Vector2TransformNormalStream(Float2U* outputStream, usize outputStride, const Float2U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    NWB_ASSERT(outputStream != nullptr);
    NWB_ASSERT(inputStream != nullptr);
    NWB_ASSERT(inputStride >= sizeof(Float2U));
    NWB_ASSERT(outputStride >= sizeof(Float2U));

    const SIMDMatrix transposedMatrix = SIMDVectorDetail::MatrixTransposeForTransform(matrix);
    for(usize i = 0; i < vectorCount; ++i){
        const SIMDVector value = LoadFloat(*SIMDVectorDetail::StridePointer(inputStream, inputStride, i));
        StoreFloat(
            SIMDVectorDetail::Vector4TransformTransposed(VectorAndInt(value, s_SIMDMaskXY), transposedMatrix),
            SIMDVectorDetail::StridePointer(outputStream, outputStride, i)
        );
    }

    return outputStream;
}

NWB_INLINE Float4U* SIMDCALL Vector3TransformStream(Float4U* outputStream, usize outputStride, const Float3U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    NWB_ASSERT(outputStream != nullptr);
    NWB_ASSERT(inputStream != nullptr);
    NWB_ASSERT(inputStride >= sizeof(Float3U));
    NWB_ASSERT(outputStride >= sizeof(Float4U));

    const SIMDMatrix transposedMatrix = SIMDVectorDetail::MatrixTransposeForTransform(matrix);
    for(usize i = 0; i < vectorCount; ++i){
        const SIMDVector value = LoadFloat(*SIMDVectorDetail::StridePointer(inputStream, inputStride, i));
        StoreFloat(
            SIMDVectorDetail::Vector4TransformTransposed(VectorSetW(value, 1.0f), transposedMatrix),
            SIMDVectorDetail::StridePointer(outputStream, outputStride, i)
        );
    }

    return outputStream;
}

NWB_INLINE Float3U* SIMDCALL Vector3TransformCoordStream(Float3U* outputStream, usize outputStride, const Float3U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    NWB_ASSERT(outputStream != nullptr);
    NWB_ASSERT(inputStream != nullptr);
    NWB_ASSERT(inputStride >= sizeof(Float3U));
    NWB_ASSERT(outputStride >= sizeof(Float3U));

    const SIMDMatrix transposedMatrix = SIMDVectorDetail::MatrixTransposeForTransform(matrix);
    for(usize i = 0; i < vectorCount; ++i){
        const SIMDVector value = LoadFloat(*SIMDVectorDetail::StridePointer(inputStream, inputStride, i));
        SIMDVector result = SIMDVectorDetail::Vector4TransformTransposed(
            VectorSetW(value, 1.0f),
            transposedMatrix
        );
        result = VectorSetW(VectorDivide(result, VectorSplatW(result)), 1.0f);
        StoreFloat(result, SIMDVectorDetail::StridePointer(outputStream, outputStride, i));
    }

    return outputStream;
}

NWB_INLINE Float3U* SIMDCALL Vector3TransformNormalStream(Float3U* outputStream, usize outputStride, const Float3U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    NWB_ASSERT(outputStream != nullptr);
    NWB_ASSERT(inputStream != nullptr);
    NWB_ASSERT(inputStride >= sizeof(Float3U));
    NWB_ASSERT(outputStride >= sizeof(Float3U));

    const SIMDMatrix transposedMatrix = SIMDVectorDetail::MatrixTransposeForTransform(matrix);
    for(usize i = 0; i < vectorCount; ++i){
        const SIMDVector value = LoadFloat(*SIMDVectorDetail::StridePointer(inputStream, inputStride, i));
        StoreFloat(
            SIMDVectorDetail::Vector4TransformTransposed(VectorAndInt(value, s_SIMDMask3), transposedMatrix),
            SIMDVectorDetail::StridePointer(outputStream, outputStride, i)
        );
    }

    return outputStream;
}

NWB_INLINE Float4U* SIMDCALL Vector4TransformStream(Float4U* outputStream, usize outputStride, const Float4U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    NWB_ASSERT(outputStream != nullptr);
    NWB_ASSERT(inputStream != nullptr);
    NWB_ASSERT(inputStride >= sizeof(Float4U));
    NWB_ASSERT(outputStride >= sizeof(Float4U));

    const SIMDMatrix transposedMatrix = SIMDVectorDetail::MatrixTransposeForTransform(matrix);
    for(usize i = 0; i < vectorCount; ++i){
        const SIMDVector value = LoadFloat(*SIMDVectorDetail::StridePointer(inputStream, inputStride, i));
        StoreFloat(
            SIMDVectorDetail::Vector4TransformTransposed(value, transposedMatrix),
            SIMDVectorDetail::StridePointer(outputStream, outputStride, i)
        );
    }

    return outputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

