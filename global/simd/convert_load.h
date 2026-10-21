// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "convert_half.h"
#include "convert_lane.h"


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
    return SIMDConvertDetail::MakeF32(ConvertHalfToFloat(value.x), ConvertHalfToFloat(value.y), ConvertHalfToFloat(value.z), ConvertHalfToFloat(value.w));
#endif
}

// SIMD types are calculation values; persistent data uses typed layouts via these boundaries.
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloat(const Float4& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(src.x, src.y, src.z, src.w);
#elif defined (NWB_HAS_NEON)
    return vld1q_f32(src.raw);
#elif defined(NWB_HAS_SSE2)
    return _mm_load_ps(src.raw);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloat(f32 src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(src, 0.0f, 0.0f, 0.0f);
#elif defined (NWB_HAS_NEON)
    uint32x4_t zero = vdupq_n_u32(0);
    return vreinterpretq_f32_u32(vld1q_lane_u32(&src, zero, 0));
#elif defined(NWB_HAS_SSE2)
    return _mm_load_ss(&src);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloat(const Float2U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(src.x, src.y, 0.0f, 0.0f);
#elif defined (NWB_HAS_NEON)
    float32x2_t x = vld1_f32(src.raw);
    float32x2_t zero = vdup_n_f32(0);
    return vcombine_f32(x, zero);
#elif defined(NWB_HAS_SSE2)
    return _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(src.raw)));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloat(const Float3U& src)noexcept{
    return SIMDConvertDetail::LoadFloat3Components(src);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloat(const Float4U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(src.x, src.y, src.z, src.w);
#elif defined (NWB_HAS_NEON)
    return vld1q_f32(src.raw);
#elif defined(NWB_HAS_SSE2)
    return _mm_loadu_ps(src.raw);
#endif
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat(const Float34& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::LoadFloat34Scalar(src);
#elif defined (NWB_HAS_NEON)
    return SIMDConvertDetail::LoadFloat34Neon(src);
#elif defined(NWB_HAS_SSE2)
    return SIMDConvertDetail::LoadFloat34Sse<true>(src);
#endif
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat(const Float44& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::LoadFloat44Scalar(src);
#elif defined (NWB_HAS_NEON)
    return SIMDConvertDetail::LoadFloat44Neon(src);
#elif defined(NWB_HAS_SSE2)
    return SIMDConvertDetail::LoadFloat44Sse<true>(src);
#endif
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
#elif defined(NWB_HAS_SSE2)
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
    matrix.v[1] = _mm_or_ps(secondRowTail, secondRowHead);
    matrix.v[2] = _mm_shuffle_ps(middleValues, lastValue, _MM_SHUFFLE(1, 0, 3, 2));
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
#endif
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat(const Float34U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::LoadFloat34Scalar(src);
#elif defined (NWB_HAS_NEON)
    return SIMDConvertDetail::LoadFloat34Neon(src);
#elif defined(NWB_HAS_SSE2)
    return SIMDConvertDetail::LoadFloat34Sse<false>(src);
#endif
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL LoadFloat(const Float44U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::LoadFloat44Scalar(src);
#elif defined (NWB_HAS_NEON)
    return SIMDConvertDetail::LoadFloat44Neon(src);
#elif defined(NWB_HAS_SSE2)
    return SIMDConvertDetail::LoadFloat44Sse<false>(src);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Half2U& src)noexcept{
    return SIMDConvertDoubleDetail::MakeF64(ConvertHalfToFloat(src.x), ConvertHalfToFloat(src.y), 0.0, 0.0);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Half4U& src)noexcept{
    return SIMDConvertDoubleDetail::MakeF64(ConvertHalfToFloat(src.x), ConvertHalfToFloat(src.y), ConvertHalfToFloat(src.z), ConvertHalfToFloat(src.w));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double4& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<4u>(src.raw[0]);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(f64 src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<1u>(src);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double2U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<2u>(src.raw[0]);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double3U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<3u>(src.raw[0]);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double4U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<4u>(src.raw[0]);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double2& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<2u>(src.raw[0]);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double3& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<3u>(src.raw[0]);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double34& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble34Rows(src);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double44& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble44Rows(src);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double33U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble33Rows(src);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double34U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble34Rows(src);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double44U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble44Rows(src);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double33& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble33Rows(src);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloatInt(const Float3Int& src)noexcept{
    return SIMDConvertDetail::LoadFloat3Components(src);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadFloatInt(const Float3UInt& src)noexcept{
    return SIMDConvertDetail::LoadFloat3Components(src);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const Int4& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(static_cast<u32>(src.x), static_cast<u32>(src.y), static_cast<u32>(src.z), static_cast<u32>(src.w));
#elif defined (NWB_HAS_NEON)
    return vreinterpretq_f32_s32(vld1q_s32(src.raw));
#elif defined(NWB_HAS_SSE2)
    __m128i vector = _mm_load_si128(reinterpret_cast<const __m128i*>(src.raw));
    return _mm_castsi128_ps(vector);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(i32 src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(static_cast<u32>(src), 0, 0, 0);
#elif defined (NWB_HAS_NEON)
    int32x4_t v = vdupq_n_s32(0);
    v = vsetq_lane_s32(src, v, 0);
    return vreinterpretq_f32_s32(v);
#elif defined(NWB_HAS_SSE2)
    return _mm_castsi128_ps(_mm_cvtsi32_si128(src));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const Int2U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(static_cast<u32>(src.x), static_cast<u32>(src.y), 0, 0);
#elif defined (NWB_HAS_NEON)
    int32x2_t x = vld1_s32(src.raw);
    int32x2_t zero = vdup_n_s32(0);
    return vreinterpretq_f32_s32(vcombine_s32(x, zero));
#elif defined(NWB_HAS_SSE2)
    return _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(src.raw)));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const Int3U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(static_cast<u32>(src.x), static_cast<u32>(src.y), static_cast<u32>(src.z), 0);
#elif defined (NWB_HAS_NEON)
    int32x2_t x = vld1_s32(src.raw);
    int32x2_t zero = vdup_n_s32(0);
    int32x2_t y = vld1_lane_s32(src.raw + 2, zero, 0);
    return vreinterpretq_f32_s32(vcombine_s32(x, y));
#elif defined(NWB_HAS_SSE2)
    __m128 xy = _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(src.raw)));
    __m128 z = _mm_load_ss(reinterpret_cast<const f32*>(src.raw + 2));
    return _mm_movelh_ps(xy, z);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const Int4U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(static_cast<u32>(src.x), static_cast<u32>(src.y), static_cast<u32>(src.z), static_cast<u32>(src.w));
#elif defined (NWB_HAS_NEON)
    return vreinterpretq_f32_s32(vld1q_s32(src.raw));
#elif defined(NWB_HAS_SSE2)
    __m128i vector = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src.raw));
    return _mm_castsi128_ps(vector);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const UInt4& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(src.x, src.y, src.z, src.w);
#elif defined (NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vld1q_u32(src.raw));
#elif defined(NWB_HAS_SSE2)
    __m128i vector = _mm_load_si128(reinterpret_cast<const __m128i*>(src.raw));
    return _mm_castsi128_ps(vector);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(u32 src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(src, 0, 0, 0);
#elif defined (NWB_HAS_NEON)
    uint32x4_t v = vdupq_n_u32(0);
    v = vsetq_lane_u32(src, v, 0);
    return vreinterpretq_f32_u32(v);
#elif defined(NWB_HAS_SSE2)
    return _mm_castsi128_ps(_mm_cvtsi32_si128(static_cast<i32>(src)));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const UInt2U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(src.x, src.y, 0, 0);
#elif defined (NWB_HAS_NEON)
    uint32x2_t x = vld1_u32(src.raw);
    uint32x2_t zero = vdup_n_u32(0);
    return vreinterpretq_f32_u32(vcombine_u32(x, zero));
#elif defined(NWB_HAS_SSE2)
    return _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(src.raw)));
#endif
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
#elif defined(NWB_HAS_SSE2)
    __m128 xy = _mm_castpd_ps(_mm_load_sd(reinterpret_cast<const f64*>(src.raw)));
    __m128 z = _mm_load_ss(reinterpret_cast<const f32*>(src.raw + 2));
    return _mm_movelh_ps(xy, z);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL LoadInt(const UInt4U& src)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(src.x, src.y, src.z, src.w);
#elif defined (NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vld1q_u32(src.raw));
#elif defined(NWB_HAS_SSE2)
    __m128i vector = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src.raw));
    return _mm_castsi128_ps(vector);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Double3Int& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<3u>(src.x);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Double3UInt& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDoubleComponents<3u>(src.x);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Int64x4& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<4u>(src.raw[0]);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(i64 src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<1u>(src);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Int64x2U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<2u>(src.raw[0]);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Int64x3U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<3u>(src.raw[0]);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Int64x4U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<4u>(src.raw[0]);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const UInt64x4& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<4u>(src.raw[0]);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(u64 src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<1u>(src);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const UInt64x2U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<2u>(src.raw[0]);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const UInt64x3U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<3u>(src.raw[0]);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const UInt64x4U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadIntComponents<4u>(src.raw[0]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

