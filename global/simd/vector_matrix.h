// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_lane.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_AVX)
NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixTransposePackedRows(__m256 t0, __m256 t1)noexcept;
#endif

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixTranspose4(SIMDVectorSingle r0, SIMDVectorSingle r1, SIMDVectorSingle r2, SIMDVectorSingle r3)noexcept{
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
#elif defined(NWB_HAS_AVX)
    __m256 t0 = _mm256_castps128_ps256(r0);
    t0 = _mm256_insertf128_ps(t0, r1, 1);
    __m256 t1 = _mm256_castps128_ps256(r2);
    t1 = _mm256_insertf128_ps(t1, r3, 1);

    return MatrixTransposePackedRows(t0, t1);
#elif defined(NWB_HAS_SSE2)
    SIMDVectorSingle temp0 = _mm_shuffle_ps(r0, r1, _MM_SHUFFLE(1, 0, 1, 0));
    SIMDVectorSingle temp1 = _mm_shuffle_ps(r0, r1, _MM_SHUFFLE(3, 2, 3, 2));
    SIMDVectorSingle temp2 = _mm_shuffle_ps(r2, r3, _MM_SHUFFLE(1, 0, 1, 0));
    SIMDVectorSingle temp3 = _mm_shuffle_ps(r2, r3, _MM_SHUFFLE(3, 2, 3, 2));

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

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixTransposeForTransform(const SIMDMatrix& matrix)noexcept{
    return MatrixTranspose4(matrix.v[0], matrix.v[1], matrix.v[2], matrix.v[3]);
}

#if defined(NWB_HAS_AVX)
NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixTransposePackedRows(__m256 t0, __m256 t1)noexcept{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector4TransformTransposed(SIMDVectorSingle value, const SIMDMatrix& transposedMatrix)noexcept{
#if defined(NWB_HAS_SCALAR)
    const f32 x = value.f[0];
    const f32 y = value.f[1];
    const f32 z = value.f[2];
    const f32 w = value.f[3];
    const SIMDVectorSingle& m0 = transposedMatrix.v[0];
    const SIMDVectorSingle& m1 = transposedMatrix.v[1];
    const SIMDVectorSingle& m2 = transposedMatrix.v[2];
    const SIMDVectorSingle& m3 = transposedMatrix.v[3];
    return SIMDConvertDetail::MakeF32(
        (m0.f[0] * x) + (m1.f[0] * y) + (m2.f[0] * z) + (m3.f[0] * w),
        (m0.f[1] * x) + (m1.f[1] * y) + (m2.f[1] * z) + (m3.f[1] * w),
        (m0.f[2] * x) + (m1.f[2] * y) + (m2.f[2] * z) + (m3.f[2] * w),
        (m0.f[3] * x) + (m1.f[3] * y) + (m2.f[3] * z) + (m3.f[3] * w)
    );
#elif defined(NWB_HAS_NEON)
    const float32x2_t low = vget_low_f32(value);
    const float32x2_t high = vget_high_f32(value);
    SIMDVectorSingle result = vmulq_lane_f32(transposedMatrix.v[0], low, 0);
    result = vmlaq_lane_f32(result, transposedMatrix.v[1], low, 1);
    result = vmlaq_lane_f32(result, transposedMatrix.v[2], high, 0);
    return vmlaq_lane_f32(result, transposedMatrix.v[3], high, 1);
#else
    SIMDVectorSingle x{};
    SIMDVectorSingle y{};
    SIMDVectorSingle z{};
    SIMDVectorSingle w{};
#if defined(NWB_HAS_AVX)
#if defined(NWB_HAS_AVX2)
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
#if defined(NWB_HAS_FMA3)
    SIMDVectorSingle result = _mm_fmadd_ps(w, transposedMatrix.v[3], _mm_mul_ps(z, transposedMatrix.v[2]));
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

template<typename OutputT, typename InputT, typename TransformT>
NWB_INLINE OutputT* NWB_SIMD_CALL VectorTransformStreamImpl(OutputT* outputStream, usize outputStride, const InputT* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix, TransformT transform)noexcept{
    NWB_ASSERT(outputStream != nullptr);
    NWB_ASSERT(inputStream != nullptr);
    NWB_ASSERT(inputStride >= sizeof(InputT));
    NWB_ASSERT(outputStride >= sizeof(OutputT));

    const SIMDMatrix transposedMatrix = MatrixTransposeForTransform(matrix);
    for(usize i = 0u; i < vectorCount; ++i){
        const SIMDVectorSingle value = LoadFloat(*VectorScalarDetail::StridePointer(inputStream, inputStride, i));
        StoreFloat(transform(value, transposedMatrix), *VectorScalarDetail::StridePointer(outputStream, outputStride, i));
    }

    return outputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_SSE4)
template<int Mask>
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL MatrixDotPack(const SIMDMatrix& matrix, SIMDVectorSingle value)noexcept{
    const SIMDVectorSingle x = _mm_dp_ps(matrix.v[0], value, Mask);
    const SIMDVectorSingle y = _mm_dp_ps(matrix.v[1], value, Mask);
    const SIMDVectorSingle z = _mm_dp_ps(matrix.v[2], value, Mask);
    const SIMDVectorSingle w = _mm_dp_ps(matrix.v[3], value, Mask);
    const SIMDVectorSingle xy = _mm_unpacklo_ps(x, y);
    const SIMDVectorSingle zw = _mm_unpacklo_ps(z, w);
    return _mm_movelh_ps(xy, zw);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleTranspose4(SIMDVectorDouble r0, SIMDVectorDouble r1, SIMDVectorDouble r2, SIMDVectorDouble r3)noexcept{
    SIMDMatrixDouble result{};
#if defined(NWB_HAS_NEON64)
    const float64x2_t low0 = GetLow(r0);
    const float64x2_t low1 = GetLow(r1);
    const float64x2_t low2 = GetLow(r2);
    const float64x2_t low3 = GetLow(r3);
    const float64x2_t high0 = GetHigh(r0);
    const float64x2_t high1 = GetHigh(r1);
    const float64x2_t high2 = GetHigh(r2);
    const float64x2_t high3 = GetHigh(r3);
    result.v[0] = FromPairs(vzip1q_f64(low0, low1), vzip1q_f64(low2, low3));
    result.v[1] = FromPairs(vzip2q_f64(low0, low1), vzip2q_f64(low2, low3));
    result.v[2] = FromPairs(vzip1q_f64(high0, high1), vzip1q_f64(high2, high3));
    result.v[3] = FromPairs(vzip2q_f64(high0, high1), vzip2q_f64(high2, high3));
#elif defined(NWB_HAS_AVX)
    const __m256d row0 = BitCast<__m256d>(r0);
    const __m256d row1 = BitCast<__m256d>(r1);
    const __m256d row2 = BitCast<__m256d>(r2);
    const __m256d row3 = BitCast<__m256d>(r3);
    const __m256d even01 = _mm256_unpacklo_pd(row0, row1);
    const __m256d odd01 = _mm256_unpackhi_pd(row0, row1);
    const __m256d even23 = _mm256_unpacklo_pd(row2, row3);
    const __m256d odd23 = _mm256_unpackhi_pd(row2, row3);
    result.v[0] = BitCast<SIMDVectorDouble>(_mm256_permute2f128_pd(even01, even23, 0x20));
    result.v[1] = BitCast<SIMDVectorDouble>(_mm256_permute2f128_pd(odd01, odd23, 0x20));
    result.v[2] = BitCast<SIMDVectorDouble>(_mm256_permute2f128_pd(even01, even23, 0x31));
    result.v[3] = BitCast<SIMDVectorDouble>(_mm256_permute2f128_pd(odd01, odd23, 0x31));
#elif defined(NWB_HAS_SSE2)
    const __m128d low0 = GetLow(r0);
    const __m128d low1 = GetLow(r1);
    const __m128d low2 = GetLow(r2);
    const __m128d low3 = GetLow(r3);
    const __m128d high0 = GetHigh(r0);
    const __m128d high1 = GetHigh(r1);
    const __m128d high2 = GetHigh(r2);
    const __m128d high3 = GetHigh(r3);
    result.v[0] = FromPairs(_mm_unpacklo_pd(low0, low1), _mm_unpacklo_pd(low2, low3));
    result.v[1] = FromPairs(_mm_unpackhi_pd(low0, low1), _mm_unpackhi_pd(low2, low3));
    result.v[2] = FromPairs(_mm_unpacklo_pd(high0, high1), _mm_unpacklo_pd(high2, high3));
    result.v[3] = FromPairs(_mm_unpackhi_pd(high0, high1), _mm_unpackhi_pd(high2, high3));
#else
    result.v[0] = SIMDVectorDouble{ r0[0], r1[0], r2[0], r3[0] };
    result.v[1] = SIMDVectorDouble{ r0[1], r1[1], r2[1], r3[1] };
    result.v[2] = SIMDVectorDouble{ r0[2], r1[2], r2[2], r3[2] };
    result.v[3] = SIMDVectorDouble{ r0[3], r1[3], r2[3], r3[3] };
#endif
    return result;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleTransposeForTransform(const SIMDMatrixDouble& matrix)noexcept{
    return MatrixDoubleTranspose4(matrix.v[0], matrix.v[1], matrix.v[2], matrix.v[3]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4TransformTransposed(SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
#if defined(NWB_HAS_NEON64)
    const float64x2_t low = GetLow(value);
    const float64x2_t high = GetHigh(value);
    const float64x2_t x = vdupq_laneq_f64(low, 0);
    const float64x2_t y = vdupq_laneq_f64(low, 1);
    const float64x2_t z = vdupq_laneq_f64(high, 0);
    const float64x2_t w = vdupq_laneq_f64(high, 1);
    float64x2_t resultLow = vmulq_f64(GetLow(transposedMatrix.v[0]), x);
    resultLow = vaddq_f64(resultLow, vmulq_f64(GetLow(transposedMatrix.v[1]), y));
    resultLow = vaddq_f64(resultLow, vmulq_f64(GetLow(transposedMatrix.v[2]), z));
    resultLow = vaddq_f64(resultLow, vmulq_f64(GetLow(transposedMatrix.v[3]), w));
    float64x2_t resultHigh = vmulq_f64(GetHigh(transposedMatrix.v[0]), x);
    resultHigh = vaddq_f64(resultHigh, vmulq_f64(GetHigh(transposedMatrix.v[1]), y));
    resultHigh = vaddq_f64(resultHigh, vmulq_f64(GetHigh(transposedMatrix.v[2]), z));
    resultHigh = vaddq_f64(resultHigh, vmulq_f64(GetHigh(transposedMatrix.v[3]), w));
    return FromPairs(resultLow, resultHigh);
#elif defined(NWB_HAS_AVX)
    const __m256d lanes = BitCast<__m256d>(value);
    const __m256d low = _mm256_permute2f128_pd(lanes, lanes, 0x00);
    const __m256d high = _mm256_permute2f128_pd(lanes, lanes, 0x11);
    const __m256d x = _mm256_permute_pd(low, 0x0);
    const __m256d y = _mm256_permute_pd(low, 0xf);
    const __m256d z = _mm256_permute_pd(high, 0x0);
    const __m256d w = _mm256_permute_pd(high, 0xf);
    __m256d result = _mm256_mul_pd(BitCast<__m256d>(transposedMatrix.v[0]), x);
    result = _mm256_add_pd(result, _mm256_mul_pd(BitCast<__m256d>(transposedMatrix.v[1]), y));
    result = _mm256_add_pd(result, _mm256_mul_pd(BitCast<__m256d>(transposedMatrix.v[2]), z));
    result = _mm256_add_pd(result, _mm256_mul_pd(BitCast<__m256d>(transposedMatrix.v[3]), w));
    return BitCast<SIMDVectorDouble>(result);
#elif defined(NWB_HAS_SSE2)
    const __m128d low = GetLow(value);
    const __m128d high = GetHigh(value);
    const __m128d x = _mm_unpacklo_pd(low, low);
    const __m128d y = _mm_unpackhi_pd(low, low);
    const __m128d z = _mm_unpacklo_pd(high, high);
    const __m128d w = _mm_unpackhi_pd(high, high);
    __m128d resultLow = _mm_mul_pd(GetLow(transposedMatrix.v[0]), x);
    resultLow = _mm_add_pd(resultLow, _mm_mul_pd(GetLow(transposedMatrix.v[1]), y));
    resultLow = _mm_add_pd(resultLow, _mm_mul_pd(GetLow(transposedMatrix.v[2]), z));
    resultLow = _mm_add_pd(resultLow, _mm_mul_pd(GetLow(transposedMatrix.v[3]), w));
    __m128d resultHigh = _mm_mul_pd(GetHigh(transposedMatrix.v[0]), x);
    resultHigh = _mm_add_pd(resultHigh, _mm_mul_pd(GetHigh(transposedMatrix.v[1]), y));
    resultHigh = _mm_add_pd(resultHigh, _mm_mul_pd(GetHigh(transposedMatrix.v[2]), z));
    resultHigh = _mm_add_pd(resultHigh, _mm_mul_pd(GetHigh(transposedMatrix.v[3]), w));
    return FromPairs(resultLow, resultHigh);
#else
    const SIMDVectorDouble& m0 = transposedMatrix.v[0];
    const SIMDVectorDouble& m1 = transposedMatrix.v[1];
    const SIMDVectorDouble& m2 = transposedMatrix.v[2];
    const SIMDVectorDouble& m3 = transposedMatrix.v[3];
    return SIMDVectorDouble{
        ((m0[0] * value[0] + m1[0] * value[1]) + m2[0] * value[2]) + m3[0] * value[3],
        ((m0[1] * value[0] + m1[1] * value[1]) + m2[1] * value[2]) + m3[1] * value[3],
        ((m0[2] * value[0] + m1[2] * value[1]) + m2[2] * value[2]) + m3[2] * value[3],
        ((m0[3] * value[0] + m1[3] * value[1]) + m2[3] * value[2]) + m3[3] * value[3]
    };
#endif
}

template<typename OutputT, typename InputT, typename TransformT>
NWB_INLINE OutputT* NWB_SIMD_CALL VectorDoubleTransformStreamImpl(OutputT* outputStream, usize outputStride, const InputT* inputStream, usize inputStride, usize vectorCount, const SIMDMatrixDouble& matrix, TransformT transform)noexcept{
    NWB_ASSERT(outputStream != nullptr);
    NWB_ASSERT(inputStream != nullptr);
    NWB_ASSERT(inputStride >= sizeof(InputT));
    NWB_ASSERT(outputStride >= sizeof(OutputT));
    NWB_ASSERT(inputStride % alignof(InputT) == 0u);
    NWB_ASSERT(outputStride % alignof(OutputT) == 0u);

    const SIMDMatrixDouble transposedMatrix = MatrixDoubleTransposeForTransform(matrix);
    for(usize i = 0u; i < vectorCount; ++i){
        const SIMDVectorDouble value = LoadDouble(*VectorScalarDetail::StridePointer(inputStream, inputStride, i));
        StoreDouble(transform(value, transposedMatrix), *VectorScalarDetail::StridePointer(outputStream, outputStride, i));
    }

    return outputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

