// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_lane.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

template<typename OutputT, typename InputT, typename TransformT>
NWB_INLINE OutputT* SIMDCALL VectorTransformStreamImpl(
    OutputT* outputStream,
    usize outputStride,
    const InputT* inputStream,
    usize inputStride,
    usize vectorCount,
    const SIMDMatrix& matrix,
    TransformT transform
)noexcept{
    NWB_ASSERT(outputStream != nullptr);
    NWB_ASSERT(inputStream != nullptr);
    NWB_ASSERT(inputStride >= sizeof(InputT));
    NWB_ASSERT(outputStride >= sizeof(OutputT));

    const SIMDMatrix transposedMatrix = MatrixTransposeForTransform(matrix);
    for(usize i = 0; i < vectorCount; ++i){
        const SIMDVector value = LoadFloat(*StridePointer(inputStream, inputStride, i));
        StoreFloat(transform(value, transposedMatrix), *StridePointer(outputStream, outputStride, i));
    }

    return outputStream;
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

