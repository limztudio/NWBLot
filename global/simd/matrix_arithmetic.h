// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorConstF s_SIMDMatrixSign = { { { 1.0f, -1.0f, 1.0f, -1.0f } } };
inline constexpr f32 s_MatrixHalf = 0.5f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL MatrixRowMultiply(SIMDVectorSingle row, const SIMDMatrix& matrix)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(
        (matrix.v[0].f[0] * row.f[0]) + (matrix.v[1].f[0] * row.f[1]) + (matrix.v[2].f[0] * row.f[2]) + (matrix.v[3].f[0] * row.f[3]),
        (matrix.v[0].f[1] * row.f[0]) + (matrix.v[1].f[1] * row.f[1]) + (matrix.v[2].f[1] * row.f[2]) + (matrix.v[3].f[1] * row.f[3]),
        (matrix.v[0].f[2] * row.f[0]) + (matrix.v[1].f[2] * row.f[1]) + (matrix.v[2].f[2] * row.f[2]) + (matrix.v[3].f[2] * row.f[3]),
        (matrix.v[0].f[3] * row.f[0]) + (matrix.v[1].f[3] * row.f[1]) + (matrix.v[2].f[3] * row.f[2]) + (matrix.v[3].f[3] * row.f[3])
    );
#elif defined(NWB_HAS_NEON)
    const float32x2_t rowL = vget_low_f32(row);
    const float32x2_t rowH = vget_high_f32(row);
    const SIMDVectorSingle x = vmulq_lane_f32(matrix.v[0], rowL, 0);
    const SIMDVectorSingle y = vmulq_lane_f32(matrix.v[1], rowL, 1);
    const SIMDVectorSingle z = vmlaq_lane_f32(x, matrix.v[2], rowH, 0);
    const SIMDVectorSingle w = vmlaq_lane_f32(y, matrix.v[3], rowH, 1);
    return vaddq_f32(z, w);
#else
#if defined(NWB_HAS_AVX)
#if defined(NWB_HAS_AVX2)
    SIMDVectorSingle x = _mm_broadcastss_ps(row);
#else
    SIMDVectorSingle x = _mm_permute_ps(row, _MM_SHUFFLE(0, 0, 0, 0));
#endif
    SIMDVectorSingle y = _mm_permute_ps(row, _MM_SHUFFLE(1, 1, 1, 1));
    SIMDVectorSingle z = _mm_permute_ps(row, _MM_SHUFFLE(2, 2, 2, 2));
    SIMDVectorSingle w = _mm_permute_ps(row, _MM_SHUFFLE(3, 3, 3, 3));
#else
    SIMDVectorSingle x = _mm_shuffle_ps(row, row, _MM_SHUFFLE(0, 0, 0, 0));
    SIMDVectorSingle y = _mm_shuffle_ps(row, row, _MM_SHUFFLE(1, 1, 1, 1));
    SIMDVectorSingle z = _mm_shuffle_ps(row, row, _MM_SHUFFLE(2, 2, 2, 2));
    SIMDVectorSingle w = _mm_shuffle_ps(row, row, _MM_SHUFFLE(3, 3, 3, 3));
#endif
    x = _mm_mul_ps(x, matrix.v[0]);
    y = _mm_mul_ps(y, matrix.v[1]);
    z = _mm_mul_ps(z, matrix.v[2]);
    w = _mm_mul_ps(w, matrix.v[3]);
    x = _mm_add_ps(x, z);
    y = _mm_add_ps(y, w);
    return _mm_add_ps(x, y);
#endif
}

#if defined(NWB_HAS_AVX) && defined(NWB_HAS_FMA3)
NWB_INLINE void NWB_SIMD_CALL MatrixMultiplyPackedRowsFMA(const SIMDMatrix& m0, const SIMDMatrix& m1, __m256& outRows01, __m256& outRows23)noexcept{
    __m256 t0 = _mm256_castps128_ps256(m0.v[0]);
    t0 = _mm256_insertf128_ps(t0, m0.v[1], 1);
    __m256 t1 = _mm256_castps128_ps256(m0.v[2]);
    t1 = _mm256_insertf128_ps(t1, m0.v[3], 1);

    __m256 u0 = _mm256_castps128_ps256(m1.v[0]);
    u0 = _mm256_insertf128_ps(u0, m1.v[1], 1);
    __m256 u1 = _mm256_castps128_ps256(m1.v[2]);
    u1 = _mm256_insertf128_ps(u1, m1.v[3], 1);

    __m256 a0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(0, 0, 0, 0));
    __m256 a1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(0, 0, 0, 0));
    __m256 b0 = _mm256_permute2f128_ps(u0, u0, 0x00);
    __m256 c0 = _mm256_mul_ps(a0, b0);
    __m256 c1 = _mm256_mul_ps(a1, b0);

    a0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(1, 1, 1, 1));
    a1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(1, 1, 1, 1));
    b0 = _mm256_permute2f128_ps(u0, u0, 0x11);
    __m256 c2 = _mm256_fmadd_ps(a0, b0, c0);
    __m256 c3 = _mm256_fmadd_ps(a1, b0, c1);

    a0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(2, 2, 2, 2));
    a1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(2, 2, 2, 2));
    __m256 b1 = _mm256_permute2f128_ps(u1, u1, 0x00);
    __m256 c4 = _mm256_mul_ps(a0, b1);
    __m256 c5 = _mm256_mul_ps(a1, b1);

    a0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(3, 3, 3, 3));
    a1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(3, 3, 3, 3));
    b1 = _mm256_permute2f128_ps(u1, u1, 0x11);
    __m256 c6 = _mm256_fmadd_ps(a0, b1, c4);
    __m256 c7 = _mm256_fmadd_ps(a1, b1, c5);

    t0 = _mm256_add_ps(c2, c6);
    t1 = _mm256_add_ps(c3, c7);
    outRows01 = t0;
    outRows23 = t1;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixMultiplyFMA(const SIMDMatrix& m0, const SIMDMatrix& m1)noexcept{
    __m256 rows01{};
    __m256 rows23{};
    MatrixMultiplyPackedRowsFMA(m0, m1, rows01, rows23);
    SIMDMatrix result{};
    result.v[0] = _mm256_castps256_ps128(rows01);
    result.v[1] = _mm256_extractf128_ps(rows01, 1);
    result.v[2] = _mm256_castps256_ps128(rows23);
    result.v[3] = _mm256_extractf128_ps(rows23, 1);
    return result;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixMultiplyTransposeFMA(const SIMDMatrix& m0, const SIMDMatrix& m1)noexcept{
    __m256 rows01{};
    __m256 rows23{};
    MatrixMultiplyPackedRowsFMA(m0, m1, rows01, rows23);
    return SIMDVectorDetail::MatrixTransposePackedRows(rows01, rows23);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorDoubleConstF s_DoubleSIMDMatrixSign = { { { 1.0, -1.0, 1.0, -1.0 } } };
inline constexpr f64 s_DoubleMatrixHalf = 0.5;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL MatrixDoubleRowMultiply(SIMDVectorDouble row, const SIMDMatrixDouble& matrix)noexcept{
    SIMDVectorDouble result = VectorDoubleMultiply(VectorDoubleSplatX(row), matrix.v[0]);
    result = VectorDoubleMultiplyAdd(VectorDoubleSplatY(row), matrix.v[1], result);
    result = VectorDoubleMultiplyAdd(VectorDoubleSplatZ(row), matrix.v[2], result);
    return VectorDoubleMultiplyAdd(VectorDoubleSplatW(row), matrix.v[3], result);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool NWB_SIMD_CALL MatrixIsNaN(const SIMDMatrix& matrix)noexcept{
    SIMDVectorSingle mask = VectorOrInt(VectorIsNaN(matrix.v[0]), VectorIsNaN(matrix.v[1]));
    mask = VectorOrInt(mask, VectorIsNaN(matrix.v[2]));
    mask = VectorOrInt(mask, VectorIsNaN(matrix.v[3]));
    return VectorMoveMask(mask) != 0;
}

NWB_INLINE bool NWB_SIMD_CALL MatrixDoubleIsNaN(const SIMDMatrixDouble& matrix)noexcept{
    SIMDVectorDouble mask = VectorDoubleOrInt(VectorDoubleIsNaN(matrix.v[0]), VectorDoubleIsNaN(matrix.v[1]));
    mask = VectorDoubleOrInt(mask, VectorDoubleIsNaN(matrix.v[2]));
    mask = VectorDoubleOrInt(mask, VectorDoubleIsNaN(matrix.v[3]));
    return VectorDoubleMoveMask(mask) != 0;
}

NWB_INLINE bool NWB_SIMD_CALL MatrixIsInfinite(const SIMDMatrix& matrix)noexcept{
    SIMDVectorSingle mask = VectorOrInt(VectorIsInfinite(matrix.v[0]), VectorIsInfinite(matrix.v[1]));
    mask = VectorOrInt(mask, VectorIsInfinite(matrix.v[2]));
    mask = VectorOrInt(mask, VectorIsInfinite(matrix.v[3]));
    return VectorMoveMask(mask) != 0;
}

NWB_INLINE bool NWB_SIMD_CALL MatrixDoubleIsInfinite(const SIMDMatrixDouble& matrix)noexcept{
    SIMDVectorDouble mask = VectorDoubleOrInt(VectorDoubleIsInfinite(matrix.v[0]), VectorDoubleIsInfinite(matrix.v[1]));
    mask = VectorDoubleOrInt(mask, VectorDoubleIsInfinite(matrix.v[2]));
    mask = VectorDoubleOrInt(mask, VectorDoubleIsInfinite(matrix.v[3]));
    return VectorDoubleMoveMask(mask) != 0;
}

NWB_INLINE bool NWB_SIMD_CALL MatrixIsIdentity(const SIMDMatrix& matrix)noexcept{
    SIMDVectorSingle mask0 = VectorEqual(matrix.v[0], s_SIMDIdentityR0);
    SIMDVectorSingle mask1 = VectorEqual(matrix.v[1], s_SIMDIdentityR1);
    SIMDVectorSingle mask2 = VectorEqual(matrix.v[2], s_SIMDIdentityR2);
    SIMDVectorSingle mask3 = VectorEqual(matrix.v[3], s_SIMDIdentityR3);
    mask0 = VectorAndInt(mask0, mask1);
    mask2 = VectorAndInt(mask2, mask3);
    return VectorMoveMask(VectorAndInt(mask0, mask2)) == VectorComponentMask::s_XYZW;
}

NWB_INLINE bool NWB_SIMD_CALL MatrixDoubleIsIdentity(const SIMDMatrixDouble& matrix)noexcept{
    SIMDVectorDouble mask0 = VectorDoubleEqual(matrix.v[0], s_DoubleSIMDIdentityR0);
    SIMDVectorDouble mask1 = VectorDoubleEqual(matrix.v[1], s_DoubleSIMDIdentityR1);
    SIMDVectorDouble mask2 = VectorDoubleEqual(matrix.v[2], s_DoubleSIMDIdentityR2);
    SIMDVectorDouble mask3 = VectorDoubleEqual(matrix.v[3], s_DoubleSIMDIdentityR3);
    mask0 = VectorDoubleAndInt(mask0, mask1);
    mask2 = VectorDoubleAndInt(mask2, mask3);
    return VectorDoubleMoveMask(VectorDoubleAndInt(mask0, mask2)) == VectorDoubleComponentMask::s_DoubleXYZW;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixTranspose(const SIMDMatrix& matrix)noexcept{
    return SIMDVectorDetail::MatrixTranspose4(matrix.v[0], matrix.v[1], matrix.v[2], matrix.v[3]);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleTranspose(const SIMDMatrixDouble& matrix)noexcept{
    return SIMDVectorDoubleDetail::MatrixDoubleTranspose4(matrix.v[0], matrix.v[1], matrix.v[2], matrix.v[3]);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixMultiply(const SIMDMatrix& m0, const SIMDMatrix& m1)noexcept{
#if defined(NWB_HAS_AVX) && defined(NWB_HAS_FMA3)
    return SIMDMatrixDetail::MatrixMultiplyFMA(m0, m1);
#else
    SIMDMatrix result{};
    result.v[0] = SIMDMatrixDetail::MatrixRowMultiply(m0.v[0], m1);
    result.v[1] = SIMDMatrixDetail::MatrixRowMultiply(m0.v[1], m1);
    result.v[2] = SIMDMatrixDetail::MatrixRowMultiply(m0.v[2], m1);
    result.v[3] = SIMDMatrixDetail::MatrixRowMultiply(m0.v[3], m1);
    return result;
#endif
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleMultiply(const SIMDMatrixDouble& m0, const SIMDMatrixDouble& m1)noexcept{
    SIMDMatrixDouble result{};
    result.v[0] = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[0], m1);
    result.v[1] = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[1], m1);
    result.v[2] = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[2], m1);
    result.v[3] = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[3], m1);
    return result;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixMultiplyTranspose(const SIMDMatrix& m0, const SIMDMatrix& m1)noexcept{
#if defined(NWB_HAS_AVX) && defined(NWB_HAS_FMA3)
    return SIMDMatrixDetail::MatrixMultiplyTransposeFMA(m0, m1);
#else
    const SIMDVectorSingle r0 = SIMDMatrixDetail::MatrixRowMultiply(m0.v[0], m1);
    const SIMDVectorSingle r1 = SIMDMatrixDetail::MatrixRowMultiply(m0.v[1], m1);
    const SIMDVectorSingle r2 = SIMDMatrixDetail::MatrixRowMultiply(m0.v[2], m1);
    const SIMDVectorSingle r3 = SIMDMatrixDetail::MatrixRowMultiply(m0.v[3], m1);
    return SIMDVectorDetail::MatrixTranspose4(r0, r1, r2, r3);
#endif
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleMultiplyTranspose(const SIMDMatrixDouble& m0, const SIMDMatrixDouble& m1)noexcept{
    const SIMDVectorDouble r0 = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[0], m1);
    const SIMDVectorDouble r1 = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[1], m1);
    const SIMDVectorDouble r2 = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[2], m1);
    const SIMDVectorDouble r3 = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[3], m1);
    return SIMDVectorDoubleDetail::MatrixDoubleTranspose4(r0, r1, r2, r3);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixVectorTensorProduct(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    SIMDMatrix result{};
    result.v[0] = VectorMultiply(VectorSplatX(v0), v1);
    result.v[1] = VectorMultiply(VectorSplatY(v0), v1);
    result.v[2] = VectorMultiply(VectorSplatZ(v0), v1);
    result.v[3] = VectorMultiply(VectorSplatW(v0), v1);
    return result;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleVectorTensorProduct(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    SIMDMatrixDouble result{};
    result.v[0] = VectorDoubleMultiply(VectorDoubleSplatX(v0), v1);
    result.v[1] = VectorDoubleMultiply(VectorDoubleSplatY(v0), v1);
    result.v[2] = VectorDoubleMultiply(VectorDoubleSplatZ(v0), v1);
    result.v[3] = VectorDoubleMultiply(VectorDoubleSplatW(v0), v1);
    return result;
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL MatrixDeterminant(const SIMDMatrix& matrix)noexcept{
    SIMDVectorSingle v0 = VectorSwizzle<1, 0, 0, 0>(matrix.v[2]);
    SIMDVectorSingle v1 = VectorSwizzle<2, 2, 1, 1>(matrix.v[3]);
    SIMDVectorSingle v2 = VectorSwizzle<1, 0, 0, 0>(matrix.v[2]);
    SIMDVectorSingle v3 = VectorSwizzle<3, 3, 3, 2>(matrix.v[3]);
    SIMDVectorSingle v4 = VectorSwizzle<2, 2, 1, 1>(matrix.v[2]);
    SIMDVectorSingle v5 = VectorSwizzle<3, 3, 3, 2>(matrix.v[3]);

    SIMDVectorSingle p0 = VectorMultiply(v0, v1);
    SIMDVectorSingle p1 = VectorMultiply(v2, v3);
    SIMDVectorSingle p2 = VectorMultiply(v4, v5);

    v0 = VectorSwizzle<2, 2, 1, 1>(matrix.v[2]);
    v1 = VectorSwizzle<1, 0, 0, 0>(matrix.v[3]);
    v2 = VectorSwizzle<3, 3, 3, 2>(matrix.v[2]);
    v3 = VectorSwizzle<1, 0, 0, 0>(matrix.v[3]);
    v4 = VectorSwizzle<3, 3, 3, 2>(matrix.v[2]);
    v5 = VectorSwizzle<2, 2, 1, 1>(matrix.v[3]);

    p0 = VectorNegativeMultiplySubtract(v0, v1, p0);
    p1 = VectorNegativeMultiplySubtract(v2, v3, p1);
    p2 = VectorNegativeMultiplySubtract(v4, v5, p2);

    v0 = VectorSwizzle<3, 3, 3, 2>(matrix.v[1]);
    v1 = VectorSwizzle<2, 2, 1, 1>(matrix.v[1]);
    v2 = VectorSwizzle<1, 0, 0, 0>(matrix.v[1]);

    const SIMDVectorSingle s = VectorMultiply(matrix.v[0], SIMDMatrixDetail::s_SIMDMatrixSign);
    SIMDVectorSingle r = VectorMultiply(v0, p0);
    r = VectorNegativeMultiplySubtract(v1, p1, r);
    r = VectorMultiplyAdd(v2, p2, r);
    return Vector4Dot(s, r);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL MatrixDoubleDeterminant(const SIMDMatrixDouble& matrix)noexcept{
    SIMDVectorDouble v0 = VectorDoubleSwizzle<1, 0, 0, 0>(matrix.v[2]);
    SIMDVectorDouble v1 = VectorDoubleSwizzle<2, 2, 1, 1>(matrix.v[3]);
    SIMDVectorDouble v2 = VectorDoubleSwizzle<1, 0, 0, 0>(matrix.v[2]);
    SIMDVectorDouble v3 = VectorDoubleSwizzle<3, 3, 3, 2>(matrix.v[3]);
    SIMDVectorDouble v4 = VectorDoubleSwizzle<2, 2, 1, 1>(matrix.v[2]);
    SIMDVectorDouble v5 = VectorDoubleSwizzle<3, 3, 3, 2>(matrix.v[3]);

    SIMDVectorDouble p0 = VectorDoubleMultiply(v0, v1);
    SIMDVectorDouble p1 = VectorDoubleMultiply(v2, v3);
    SIMDVectorDouble p2 = VectorDoubleMultiply(v4, v5);

    v0 = VectorDoubleSwizzle<2, 2, 1, 1>(matrix.v[2]);
    v1 = VectorDoubleSwizzle<1, 0, 0, 0>(matrix.v[3]);
    v2 = VectorDoubleSwizzle<3, 3, 3, 2>(matrix.v[2]);
    v3 = VectorDoubleSwizzle<1, 0, 0, 0>(matrix.v[3]);
    v4 = VectorDoubleSwizzle<3, 3, 3, 2>(matrix.v[2]);
    v5 = VectorDoubleSwizzle<2, 2, 1, 1>(matrix.v[3]);

    p0 = VectorDoubleNegativeMultiplySubtract(v0, v1, p0);
    p1 = VectorDoubleNegativeMultiplySubtract(v2, v3, p1);
    p2 = VectorDoubleNegativeMultiplySubtract(v4, v5, p2);

    v0 = VectorDoubleSwizzle<3, 3, 3, 2>(matrix.v[1]);
    v1 = VectorDoubleSwizzle<2, 2, 1, 1>(matrix.v[1]);
    v2 = VectorDoubleSwizzle<1, 0, 0, 0>(matrix.v[1]);

    const SIMDVectorDouble s = VectorDoubleMultiply(matrix.v[0], SIMDMatrixDoubleDetail::s_DoubleSIMDMatrixSign);
    SIMDVectorDouble r = VectorDoubleMultiply(v0, p0);
    r = VectorDoubleNegativeMultiplySubtract(v1, p1, r);
    r = VectorDoubleMultiplyAdd(v2, p2, r);
    return VectorDouble4Dot(s, r);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixInverse(SIMDVectorSingle* outDeterminant, const SIMDMatrix& matrix)noexcept{
    const SIMDMatrix mt = MatrixTranspose(matrix);

    SIMDVectorSingle v0[4]{};
    SIMDVectorSingle v1[4]{};
    v0[0] = VectorSwizzle<0, 0, 1, 1>(mt.v[2]);
    v1[0] = VectorSwizzle<2, 3, 2, 3>(mt.v[3]);
    v0[1] = VectorSwizzle<0, 0, 1, 1>(mt.v[0]);
    v1[1] = VectorSwizzle<2, 3, 2, 3>(mt.v[1]);
    v0[2] = VectorPermute<0, 2, 4, 6>(mt.v[2], mt.v[0]);
    v1[2] = VectorPermute<1, 3, 5, 7>(mt.v[3], mt.v[1]);

    SIMDVectorSingle d0 = VectorMultiply(v0[0], v1[0]);
    SIMDVectorSingle d1 = VectorMultiply(v0[1], v1[1]);
    SIMDVectorSingle d2 = VectorMultiply(v0[2], v1[2]);

    v0[0] = VectorSwizzle<2, 3, 2, 3>(mt.v[2]);
    v1[0] = VectorSwizzle<0, 0, 1, 1>(mt.v[3]);
    v0[1] = VectorSwizzle<2, 3, 2, 3>(mt.v[0]);
    v1[1] = VectorSwizzle<0, 0, 1, 1>(mt.v[1]);
    v0[2] = VectorPermute<1, 3, 5, 7>(mt.v[2], mt.v[0]);
    v1[2] = VectorPermute<0, 2, 4, 6>(mt.v[3], mt.v[1]);

    d0 = VectorNegativeMultiplySubtract(v0[0], v1[0], d0);
    d1 = VectorNegativeMultiplySubtract(v0[1], v1[1], d1);
    d2 = VectorNegativeMultiplySubtract(v0[2], v1[2], d2);

    v0[0] = VectorSwizzle<1, 2, 0, 1>(mt.v[1]);
    v1[0] = VectorPermute<5, 1, 3, 0>(d0, d2);
    v0[1] = VectorSwizzle<2, 0, 1, 0>(mt.v[0]);
    v1[1] = VectorPermute<3, 5, 1, 2>(d0, d2);
    v0[2] = VectorSwizzle<1, 2, 0, 1>(mt.v[3]);
    v1[2] = VectorPermute<7, 1, 3, 0>(d1, d2);
    v0[3] = VectorSwizzle<2, 0, 1, 0>(mt.v[2]);
    v1[3] = VectorPermute<3, 7, 1, 2>(d1, d2);

    SIMDVectorSingle c0 = VectorMultiply(v0[0], v1[0]);
    SIMDVectorSingle c2 = VectorMultiply(v0[1], v1[1]);
    SIMDVectorSingle c4 = VectorMultiply(v0[2], v1[2]);
    SIMDVectorSingle c6 = VectorMultiply(v0[3], v1[3]);

    v0[0] = VectorSwizzle<2, 3, 1, 2>(mt.v[1]);
    v1[0] = VectorPermute<3, 0, 1, 4>(d0, d2);
    v0[1] = VectorSwizzle<3, 2, 3, 1>(mt.v[0]);
    v1[1] = VectorPermute<2, 1, 4, 0>(d0, d2);
    v0[2] = VectorSwizzle<2, 3, 1, 2>(mt.v[3]);
    v1[2] = VectorPermute<3, 0, 1, 6>(d1, d2);
    v0[3] = VectorSwizzle<3, 2, 3, 1>(mt.v[2]);
    v1[3] = VectorPermute<2, 1, 6, 0>(d1, d2);

    c0 = VectorNegativeMultiplySubtract(v0[0], v1[0], c0);
    c2 = VectorNegativeMultiplySubtract(v0[1], v1[1], c2);
    c4 = VectorNegativeMultiplySubtract(v0[2], v1[2], c4);
    c6 = VectorNegativeMultiplySubtract(v0[3], v1[3], c6);

    v0[0] = VectorSwizzle<3, 0, 3, 0>(mt.v[1]);
    v1[0] = VectorPermute<2, 5, 4, 2>(d0, d2);
    v0[1] = VectorSwizzle<1, 3, 0, 2>(mt.v[0]);
    v1[1] = VectorPermute<5, 0, 3, 4>(d0, d2);
    v0[2] = VectorSwizzle<3, 0, 3, 0>(mt.v[3]);
    v1[2] = VectorPermute<2, 7, 6, 2>(d1, d2);
    v0[3] = VectorSwizzle<1, 3, 0, 2>(mt.v[2]);
    v1[3] = VectorPermute<7, 0, 3, 6>(d1, d2);

    SIMDVectorSingle c1 = VectorNegativeMultiplySubtract(v0[0], v1[0], c0);
    c0 = VectorMultiplyAdd(v0[0], v1[0], c0);
    SIMDVectorSingle c3 = VectorMultiplyAdd(v0[1], v1[1], c2);
    c2 = VectorNegativeMultiplySubtract(v0[1], v1[1], c2);
    SIMDVectorSingle c5 = VectorNegativeMultiplySubtract(v0[2], v1[2], c4);
    c4 = VectorMultiplyAdd(v0[2], v1[2], c4);
    SIMDVectorSingle c7 = VectorMultiplyAdd(v0[3], v1[3], c6);
    c6 = VectorNegativeMultiplySubtract(v0[3], v1[3], c6);

    SIMDMatrix r{};
    r.v[0] = VectorSelect(c0, c1, s_SIMDSelect0101);
    r.v[1] = VectorSelect(c2, c3, s_SIMDSelect0101);
    r.v[2] = VectorSelect(c4, c5, s_SIMDSelect0101);
    r.v[3] = VectorSelect(c6, c7, s_SIMDSelect0101);

    const SIMDVectorSingle determinant = Vector4Dot(r.v[0], mt.v[0]);
    if(outDeterminant != nullptr)
        *outDeterminant = determinant;

    const SIMDVectorSingle reciprocal = VectorReciprocal(determinant);
    SIMDMatrix result{};
    result.v[0] = VectorMultiply(r.v[0], reciprocal);
    result.v[1] = VectorMultiply(r.v[1], reciprocal);
    result.v[2] = VectorMultiply(r.v[2], reciprocal);
    result.v[3] = VectorMultiply(r.v[3], reciprocal);
    return result;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleInverse(SIMDVectorDouble* outDeterminant, const SIMDMatrixDouble& matrix)noexcept{
    const SIMDMatrixDouble mt = MatrixDoubleTranspose(matrix);

    SIMDVectorDouble v0[4]{};
    SIMDVectorDouble v1[4]{};
    v0[0] = VectorDoubleSwizzle<0, 0, 1, 1>(mt.v[2]);
    v1[0] = VectorDoubleSwizzle<2, 3, 2, 3>(mt.v[3]);
    v0[1] = VectorDoubleSwizzle<0, 0, 1, 1>(mt.v[0]);
    v1[1] = VectorDoubleSwizzle<2, 3, 2, 3>(mt.v[1]);
    v0[2] = VectorDoublePermute<0, 2, 4, 6>(mt.v[2], mt.v[0]);
    v1[2] = VectorDoublePermute<1, 3, 5, 7>(mt.v[3], mt.v[1]);

    SIMDVectorDouble d0 = VectorDoubleMultiply(v0[0], v1[0]);
    SIMDVectorDouble d1 = VectorDoubleMultiply(v0[1], v1[1]);
    SIMDVectorDouble d2 = VectorDoubleMultiply(v0[2], v1[2]);

    v0[0] = VectorDoubleSwizzle<2, 3, 2, 3>(mt.v[2]);
    v1[0] = VectorDoubleSwizzle<0, 0, 1, 1>(mt.v[3]);
    v0[1] = VectorDoubleSwizzle<2, 3, 2, 3>(mt.v[0]);
    v1[1] = VectorDoubleSwizzle<0, 0, 1, 1>(mt.v[1]);
    v0[2] = VectorDoublePermute<1, 3, 5, 7>(mt.v[2], mt.v[0]);
    v1[2] = VectorDoublePermute<0, 2, 4, 6>(mt.v[3], mt.v[1]);

    d0 = VectorDoubleNegativeMultiplySubtract(v0[0], v1[0], d0);
    d1 = VectorDoubleNegativeMultiplySubtract(v0[1], v1[1], d1);
    d2 = VectorDoubleNegativeMultiplySubtract(v0[2], v1[2], d2);

    v0[0] = VectorDoubleSwizzle<1, 2, 0, 1>(mt.v[1]);
    v1[0] = VectorDoublePermute<5, 1, 3, 0>(d0, d2);
    v0[1] = VectorDoubleSwizzle<2, 0, 1, 0>(mt.v[0]);
    v1[1] = VectorDoublePermute<3, 5, 1, 2>(d0, d2);
    v0[2] = VectorDoubleSwizzle<1, 2, 0, 1>(mt.v[3]);
    v1[2] = VectorDoublePermute<7, 1, 3, 0>(d1, d2);
    v0[3] = VectorDoubleSwizzle<2, 0, 1, 0>(mt.v[2]);
    v1[3] = VectorDoublePermute<3, 7, 1, 2>(d1, d2);

    SIMDVectorDouble c0 = VectorDoubleMultiply(v0[0], v1[0]);
    SIMDVectorDouble c2 = VectorDoubleMultiply(v0[1], v1[1]);
    SIMDVectorDouble c4 = VectorDoubleMultiply(v0[2], v1[2]);
    SIMDVectorDouble c6 = VectorDoubleMultiply(v0[3], v1[3]);

    v0[0] = VectorDoubleSwizzle<2, 3, 1, 2>(mt.v[1]);
    v1[0] = VectorDoublePermute<3, 0, 1, 4>(d0, d2);
    v0[1] = VectorDoubleSwizzle<3, 2, 3, 1>(mt.v[0]);
    v1[1] = VectorDoublePermute<2, 1, 4, 0>(d0, d2);
    v0[2] = VectorDoubleSwizzle<2, 3, 1, 2>(mt.v[3]);
    v1[2] = VectorDoublePermute<3, 0, 1, 6>(d1, d2);
    v0[3] = VectorDoubleSwizzle<3, 2, 3, 1>(mt.v[2]);
    v1[3] = VectorDoublePermute<2, 1, 6, 0>(d1, d2);

    c0 = VectorDoubleNegativeMultiplySubtract(v0[0], v1[0], c0);
    c2 = VectorDoubleNegativeMultiplySubtract(v0[1], v1[1], c2);
    c4 = VectorDoubleNegativeMultiplySubtract(v0[2], v1[2], c4);
    c6 = VectorDoubleNegativeMultiplySubtract(v0[3], v1[3], c6);

    v0[0] = VectorDoubleSwizzle<3, 0, 3, 0>(mt.v[1]);
    v1[0] = VectorDoublePermute<2, 5, 4, 2>(d0, d2);
    v0[1] = VectorDoubleSwizzle<1, 3, 0, 2>(mt.v[0]);
    v1[1] = VectorDoublePermute<5, 0, 3, 4>(d0, d2);
    v0[2] = VectorDoubleSwizzle<3, 0, 3, 0>(mt.v[3]);
    v1[2] = VectorDoublePermute<2, 7, 6, 2>(d1, d2);
    v0[3] = VectorDoubleSwizzle<1, 3, 0, 2>(mt.v[2]);
    v1[3] = VectorDoublePermute<7, 0, 3, 6>(d1, d2);

    SIMDVectorDouble c1 = VectorDoubleNegativeMultiplySubtract(v0[0], v1[0], c0);
    c0 = VectorDoubleMultiplyAdd(v0[0], v1[0], c0);
    SIMDVectorDouble c3 = VectorDoubleMultiplyAdd(v0[1], v1[1], c2);
    c2 = VectorDoubleNegativeMultiplySubtract(v0[1], v1[1], c2);
    SIMDVectorDouble c5 = VectorDoubleNegativeMultiplySubtract(v0[2], v1[2], c4);
    c4 = VectorDoubleMultiplyAdd(v0[2], v1[2], c4);
    SIMDVectorDouble c7 = VectorDoubleMultiplyAdd(v0[3], v1[3], c6);
    c6 = VectorDoubleNegativeMultiplySubtract(v0[3], v1[3], c6);

    SIMDMatrixDouble r{};
    r.v[0] = VectorDoubleSelect(c0, c1, s_DoubleSIMDSelect0101);
    r.v[1] = VectorDoubleSelect(c2, c3, s_DoubleSIMDSelect0101);
    r.v[2] = VectorDoubleSelect(c4, c5, s_DoubleSIMDSelect0101);
    r.v[3] = VectorDoubleSelect(c6, c7, s_DoubleSIMDSelect0101);

    const SIMDVectorDouble determinant = VectorDouble4Dot(r.v[0], mt.v[0]);
    if(outDeterminant != nullptr)
        *outDeterminant = determinant;

    const SIMDVectorDouble reciprocal = VectorDoubleReciprocal(determinant);
    SIMDMatrixDouble result{};
    result.v[0] = VectorDoubleMultiply(r.v[0], reciprocal);
    result.v[1] = VectorDoubleMultiply(r.v[1], reciprocal);
    result.v[2] = VectorDoubleMultiply(r.v[2], reciprocal);
    result.v[3] = VectorDoubleMultiply(r.v[3], reciprocal);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

