// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "convert_half.h"
#include "convert_lane.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDVectorSingle src, Float4& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.f[0];
    dst.y = src.f[1];
    dst.z = src.f[2];
    dst.w = src.f[3];
#elif defined (NWB_HAS_NEON)
    vst1q_f32(dst.raw, src);
#elif defined(NWB_HAS_SSE2)
    _mm_store_ps(dst.raw, src);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDVectorSingle src, f32& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst = src.f[0];
#elif defined (NWB_HAS_NEON)
    vst1q_lane_f32(&dst, src, 0);
#elif defined(NWB_HAS_SSE2)
    _mm_store_ss(&dst, src);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDVectorSingle src, Float2U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.f[0];
    dst.y = src.f[1];
#elif defined (NWB_HAS_NEON)
    float32x2_t lowValues = vget_low_f32(src);
    vst1_f32(dst.raw, lowValues);
#elif defined(NWB_HAS_SSE2)
    _mm_store_sd(reinterpret_cast<f64*>(&dst), _mm_castps_pd(src));
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDVectorSingle src, Float3U& dst)noexcept{
    SIMDConvertDetail::StoreFloat3Components(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDVectorSingle src, Float4U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.f[0];
    dst.y = src.f[1];
    dst.z = src.f[2];
    dst.w = src.f[3];
#elif defined (NWB_HAS_NEON)
    vst1q_f32(dst.raw, src);
#elif defined(NWB_HAS_SSE2)
    _mm_storeu_ps(dst.raw, src);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDMatrix src, Float34& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreFloat34Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_f32(&dst._11, src.v[0]);
    vst1q_f32(&dst._21, src.v[1]);
    vst1q_f32(&dst._31, src.v[2]);
#elif defined(NWB_HAS_SSE2)
    _mm_store_ps(&dst._11, src.v[0]);
    _mm_store_ps(&dst._21, src.v[1]);
    _mm_store_ps(&dst._31, src.v[2]);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDMatrix src, Float44& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreFloat44Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_f32(&dst._11, src.v[0]);
    vst1q_f32(&dst._21, src.v[1]);
    vst1q_f32(&dst._31, src.v[2]);
    vst1q_f32(&dst._41, src.v[3]);
#elif defined(NWB_HAS_SSE2)
    _mm_store_ps(&dst._11, src.v[0]);
    _mm_store_ps(&dst._21, src.v[1]);
    _mm_store_ps(&dst._31, src.v[2]);
    _mm_store_ps(&dst._41, src.v[3]);
#endif
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
#elif defined(NWB_HAS_SSE2)
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

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDMatrix src, Float34U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreFloat34Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_f32(&dst._11, src.v[0]);
    vst1q_f32(&dst._21, src.v[1]);
    vst1q_f32(&dst._31, src.v[2]);
#elif defined(NWB_HAS_SSE2)
    _mm_storeu_ps(&dst._11, src.v[0]);
    _mm_storeu_ps(&dst._21, src.v[1]);
    _mm_storeu_ps(&dst._31, src.v[2]);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreFloat(SIMDMatrix src, Float44U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreFloat44Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_f32(&dst._11, src.v[0]);
    vst1q_f32(&dst._21, src.v[1]);
    vst1q_f32(&dst._31, src.v[2]);
    vst1q_f32(&dst._41, src.v[3]);
#elif defined(NWB_HAS_SSE2)
    _mm_storeu_ps(&dst._11, src.v[0]);
    _mm_storeu_ps(&dst._21, src.v[1]);
    _mm_storeu_ps(&dst._31, src.v[2]);
    _mm_storeu_ps(&dst._41, src.v[3]);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double4& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<4u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, f64& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<1u>(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double2U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<2u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double3U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<3u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double4U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<4u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double2& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<2u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double3& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<3u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double34& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble34Rows(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double44& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble44Rows(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double33U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble33Rows(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double34U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble34Rows(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double44U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble44Rows(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double33& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble33Rows(src, dst);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL StoreFloatInt(SIMDVectorSingle src, i32 w, Float3Int& dst)noexcept{
    SIMDConvertDetail::StoreFloat3Components(src, dst);
    dst.w = w;
}

NWB_INLINE void NWB_SIMD_CALL StoreFloatInt(SIMDVectorSingle src, u32 w, Float3UInt& dst)noexcept{
    SIMDConvertDetail::StoreFloat3Components(src, dst);
    dst.w = w;
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, Int4& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreInt4Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_s32(dst.raw, vreinterpretq_s32_f32(src));
#elif defined(NWB_HAS_SSE2)
    SIMDConvertDetail::StoreInt4Sse<true>(src, dst.raw);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, i32& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst = static_cast<i32>(src.u[0]);
#elif defined (NWB_HAS_NEON)
    vst1q_lane_s32(&dst, vreinterpretq_s32_f32(src), 0);
#elif defined(NWB_HAS_SSE2)
    _mm_store_ss(reinterpret_cast<f32*>(&dst), src);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, Int2U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = static_cast<i32>(src.u[0]);
    dst.y = static_cast<i32>(src.u[1]);
#elif defined (NWB_HAS_NEON)
    vst1_s32(dst.raw, vget_low_s32(vreinterpretq_s32_f32(src)));
#elif defined(NWB_HAS_SSE2)
    _mm_store_sd(reinterpret_cast<f64*>(dst.raw), _mm_castps_pd(src));
#endif
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
#elif defined(NWB_HAS_SSE2)
    SIMDConvertDetail::StoreInt3Bits(src, dst.raw);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, Int4U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    SIMDConvertDetail::StoreInt4Scalar(src, dst);
#elif defined (NWB_HAS_NEON)
    vst1q_s32(dst.raw, vreinterpretq_s32_f32(src));
#elif defined(NWB_HAS_SSE2)
    SIMDConvertDetail::StoreInt4Sse<false>(src, dst.raw);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, UInt4& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.u[0];
    dst.y = src.u[1];
    dst.z = src.u[2];
    dst.w = src.u[3];
#elif defined (NWB_HAS_NEON)
    vst1q_u32(dst.raw, vreinterpretq_u32_f32(src));
#elif defined(NWB_HAS_SSE2)
    _mm_store_si128(reinterpret_cast<__m128i*>(dst.raw), _mm_castps_si128(src));
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, u32& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst = src.u[0];
#elif defined (NWB_HAS_NEON)
    vst1q_lane_u32(&dst, vreinterpretq_u32_f32(src), 0);
#elif defined(NWB_HAS_SSE2)
    _mm_store_ss(reinterpret_cast<f32*>(&dst), src);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, UInt2U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.u[0];
    dst.y = src.u[1];
#elif defined (NWB_HAS_NEON)
    vst1_u32(dst.raw, vget_low_u32(vreinterpretq_u32_f32(src)));
#elif defined(NWB_HAS_SSE2)
    _mm_store_sd(reinterpret_cast<f64*>(dst.raw), _mm_castps_pd(src));
#endif
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
#elif defined(NWB_HAS_SSE2)
    SIMDConvertDetail::StoreUInt3Bits(src, dst.raw);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreInt(SIMDVectorSingle src, UInt4U& dst)noexcept{
#if defined(NWB_HAS_SCALAR)
    dst.x = src.u[0];
    dst.y = src.u[1];
    dst.z = src.u[2];
    dst.w = src.u[3];
#elif defined (NWB_HAS_NEON)
    vst1q_u32(dst.raw, vreinterpretq_u32_f32(src));
#elif defined(NWB_HAS_SSE2)
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst.raw), _mm_castps_si128(src));
#endif
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, i64 w, Double3Int& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<3u>(src, dst.x);
    dst.w = w;
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, u64 w, Double3UInt& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDoubleComponents<3u>(src, dst.x);
    dst.w = w;
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, Int64x4& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<4u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, i64& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<1u>(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, Int64x2U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<2u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, Int64x3U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<3u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, Int64x4U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<4u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, UInt64x4& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<4u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, u64& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<1u>(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, UInt64x2U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<2u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, UInt64x3U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<3u>(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, UInt64x4U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreIntComponents<4u>(src, dst.raw[0]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void StreamFloatFence()noexcept{
#if defined(NWB_HAS_SSE2)
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


NWB_INLINE void NWB_SIMD_CALL StreamFloat(SIMDVectorSingle src, Float4& dst)noexcept{
#if defined(NWB_HAS_SSE2)
    _mm_stream_ps(dst.raw, src);
#else
    StoreFloat(src, dst);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StreamFloat(SIMDMatrix src, Float34& dst)noexcept{
#if defined(NWB_HAS_SSE2)
    _mm_stream_ps(&dst._11, src.v[0]);
    _mm_stream_ps(&dst._21, src.v[1]);
    _mm_stream_ps(&dst._31, src.v[2]);
#else
    StoreFloat(src, dst);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StreamFloat(SIMDMatrix src, Float44& dst)noexcept{
#if defined(NWB_HAS_SSE2)
    _mm_stream_ps(&dst._11, src.v[0]);
    _mm_stream_ps(&dst._21, src.v[1]);
    _mm_stream_ps(&dst._31, src.v[2]);
    _mm_stream_ps(&dst._41, src.v[3]);
#else
    StoreFloat(src, dst);
#endif
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDVectorDouble src, Double4& dst)noexcept{
    SIMDConvertDoubleDetail::StreamDouble4Components(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDVectorDouble src, Double2& dst)noexcept{
    SIMDConvertDoubleDetail::StreamDouble2Components(src, dst.raw[0]);
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDVectorDouble src, Double3& dst)noexcept{
    SIMDConvertDoubleDetail::StreamDouble2Components(src, dst.raw[0]);
    SIMDConvertDoubleDetail::StreamDoubleHighComponent(src, dst.z);
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDMatrixDouble src, Double34& dst)noexcept{
    for(usize row = 0u; row < 3u; ++row)
        SIMDConvertDoubleDetail::StreamDouble4Components(src.v[row], dst.raw[row * 4u]);
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDMatrixDouble src, Double44& dst)noexcept{
    for(usize row = 0u; row < 4u; ++row)
        SIMDConvertDoubleDetail::StreamDouble4Components(src.v[row], dst.raw[row * 4u]);
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
    SIMDConvertDoubleDetail::StreamDouble4Components(first, dst.raw[0]);
    SIMDConvertDoubleDetail::StreamDouble4Components(second, dst.raw[4u]);
    SIMDConvertDoubleDetail::StreamDoubleHighComponent(src.v[2], dst._33);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

