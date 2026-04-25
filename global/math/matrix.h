// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "quaternion.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorConstF s_SIMDMatrixSign = { { { 1.0f, -1.0f, 1.0f, -1.0f } } };
static const SIMDVectorConstF s_SIMDMatrixNegativeTwo = { { { -2.0f, -2.0f, -2.0f, 0.0f } } };
static const SIMDVectorConstU s_SIMDMatrixSelect0001 = { { { s_SELECT_0, s_SELECT_0, s_SELECT_0, s_SELECT_1 } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool ScalarNearEqual(f32 value0, f32 value1, f32 epsilon)noexcept{ return Abs(value0 - value1) <= epsilon; }

NWB_INLINE SIMDVector SIMDCALL MatrixRowMultiply(SIMDVector row, const SIMDMatrix& matrix)noexcept{
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
    const SIMDVector x = vmulq_lane_f32(matrix.v[0], rowL, 0);
    const SIMDVector y = vmulq_lane_f32(matrix.v[1], rowL, 1);
    const SIMDVector z = vmlaq_lane_f32(x, matrix.v[2], rowH, 0);
    const SIMDVector w = vmlaq_lane_f32(y, matrix.v[3], rowH, 1);
    return vaddq_f32(z, w);
#else
#if defined(NWB_HAS_AVX2)
#if defined(__AVX2__) || defined(_M_AVX2)
    SIMDVector x = _mm_broadcastss_ps(row);
#else
    SIMDVector x = _mm_permute_ps(row, _MM_SHUFFLE(0, 0, 0, 0));
#endif
    SIMDVector y = _mm_permute_ps(row, _MM_SHUFFLE(1, 1, 1, 1));
    SIMDVector z = _mm_permute_ps(row, _MM_SHUFFLE(2, 2, 2, 2));
    SIMDVector w = _mm_permute_ps(row, _MM_SHUFFLE(3, 3, 3, 3));
#else
    SIMDVector x = _mm_shuffle_ps(row, row, _MM_SHUFFLE(0, 0, 0, 0));
    SIMDVector y = _mm_shuffle_ps(row, row, _MM_SHUFFLE(1, 1, 1, 1));
    SIMDVector z = _mm_shuffle_ps(row, row, _MM_SHUFFLE(2, 2, 2, 2));
    SIMDVector w = _mm_shuffle_ps(row, row, _MM_SHUFFLE(3, 3, 3, 3));
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

#if defined(NWB_HAS_AVX2) && (defined(__FMA__) || defined(_M_FMA))
NWB_INLINE SIMDMatrix SIMDCALL MatrixMultiplyFMA(const SIMDMatrix& m0, const SIMDMatrix& m1)noexcept{
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

    SIMDMatrix result{};
    result.v[0] = _mm256_castps256_ps128(t0);
    result.v[1] = _mm256_extractf128_ps(t0, 1);
    result.v[2] = _mm256_castps256_ps128(t1);
    result.v[3] = _mm256_extractf128_ps(t1, 1);
    return result;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixMultiplyTransposeFMA(const SIMDMatrix& m0, const SIMDMatrix& m1)noexcept{
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

    return SIMDVectorDetail::MatrixTransposePackedRows(t0, t1);
}
#endif

NWB_INLINE SIMDVector SIMDCALL PlaneNormalize(SIMDVector plane)noexcept{
#if defined(NWB_HAS_SSE4)
    SIMDVector lengthSq = _mm_dp_ps(plane, plane, 0x7F);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    lengthSq = _mm_cmpneq_ps(lengthSq, s_SIMDInfinity);
    const SIMDVector result = _mm_div_ps(plane, length);
    return _mm_and_ps(result, lengthSq);
#else
    return VectorDivide(plane, Vector3Length(plane));
#endif
}

NWB_INLINE void RankDecompose(usize& a, usize& b, usize& c, f32 x, f32 y, f32 z)noexcept{
    if(x < y){
        if(y < z){
            a = 2;
            b = 1;
            c = 0;
        }
        else{
            a = 1;
            if(x < z){
                b = 2;
                c = 0;
            }
            else{
                b = 0;
                c = 2;
            }
        }
    }
    else{
        if(x < z){
            a = 2;
            b = 0;
            c = 1;
        }
        else{
            a = 0;
            if(y < z){
                b = 2;
                c = 1;
            }
            else{
                b = 1;
                c = 2;
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// comparison operations


NWB_INLINE bool SIMDCALL MatrixIsNaN(const SIMDMatrix& matrix)noexcept{
    SIMDVector mask = VectorOrInt(VectorIsNaN(matrix.v[0]), VectorIsNaN(matrix.v[1]));
    mask = VectorOrInt(mask, VectorIsNaN(matrix.v[2]));
    mask = VectorOrInt(mask, VectorIsNaN(matrix.v[3]));
    return VectorMoveMask(mask) != 0;
}

NWB_INLINE bool SIMDCALL MatrixIsInfinite(const SIMDMatrix& matrix)noexcept{
    SIMDVector mask = VectorOrInt(VectorIsInfinite(matrix.v[0]), VectorIsInfinite(matrix.v[1]));
    mask = VectorOrInt(mask, VectorIsInfinite(matrix.v[2]));
    mask = VectorOrInt(mask, VectorIsInfinite(matrix.v[3]));
    return VectorMoveMask(mask) != 0;
}

NWB_INLINE bool SIMDCALL MatrixIsIdentity(const SIMDMatrix& matrix)noexcept{
    SIMDVector mask0 = VectorEqual(matrix.v[0], s_SIMDIdentityR0);
    SIMDVector mask1 = VectorEqual(matrix.v[1], s_SIMDIdentityR1);
    SIMDVector mask2 = VectorEqual(matrix.v[2], s_SIMDIdentityR2);
    SIMDVector mask3 = VectorEqual(matrix.v[3], s_SIMDIdentityR3);
    mask0 = VectorAndInt(mask0, mask1);
    mask2 = VectorAndInt(mask2, mask3);
    return VectorMoveMask(VectorAndInt(mask0, mask2)) == 0xFu;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// computation operations


NWB_INLINE SIMDMatrix SIMDCALL MatrixTranspose(const SIMDMatrix& matrix)noexcept{
    return SIMDVectorDetail::MatrixTranspose4(matrix.v[0], matrix.v[1], matrix.v[2], matrix.v[3]);
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixMultiply(const SIMDMatrix& m0, const SIMDMatrix& m1)noexcept{
#if defined(NWB_HAS_AVX2) && (defined(__FMA__) || defined(_M_FMA))
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

NWB_INLINE SIMDMatrix SIMDCALL MatrixMultiplyTranspose(const SIMDMatrix& m0, const SIMDMatrix& m1)noexcept{
#if defined(NWB_HAS_AVX2) && (defined(__FMA__) || defined(_M_FMA))
    return SIMDMatrixDetail::MatrixMultiplyTransposeFMA(m0, m1);
#else
    const SIMDVector r0 = SIMDMatrixDetail::MatrixRowMultiply(m0.v[0], m1);
    const SIMDVector r1 = SIMDMatrixDetail::MatrixRowMultiply(m0.v[1], m1);
    const SIMDVector r2 = SIMDMatrixDetail::MatrixRowMultiply(m0.v[2], m1);
    const SIMDVector r3 = SIMDMatrixDetail::MatrixRowMultiply(m0.v[3], m1);
    return SIMDVectorDetail::MatrixTranspose4(r0, r1, r2, r3);
#endif
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixVectorTensorProduct(SIMDVector v0, SIMDVector v1)noexcept{
    SIMDMatrix result{};
    result.v[0] = VectorMultiply(VectorSplatX(v0), v1);
    result.v[1] = VectorMultiply(VectorSplatY(v0), v1);
    result.v[2] = VectorMultiply(VectorSplatZ(v0), v1);
    result.v[3] = VectorMultiply(VectorSplatW(v0), v1);
    return result;
}

NWB_INLINE SIMDVector SIMDCALL MatrixDeterminant(const SIMDMatrix& matrix)noexcept{
    SIMDVector v0 = VectorSwizzle<1, 0, 0, 0>(matrix.v[2]);
    SIMDVector v1 = VectorSwizzle<2, 2, 1, 1>(matrix.v[3]);
    SIMDVector v2 = VectorSwizzle<1, 0, 0, 0>(matrix.v[2]);
    SIMDVector v3 = VectorSwizzle<3, 3, 3, 2>(matrix.v[3]);
    SIMDVector v4 = VectorSwizzle<2, 2, 1, 1>(matrix.v[2]);
    SIMDVector v5 = VectorSwizzle<3, 3, 3, 2>(matrix.v[3]);

    SIMDVector p0 = VectorMultiply(v0, v1);
    SIMDVector p1 = VectorMultiply(v2, v3);
    SIMDVector p2 = VectorMultiply(v4, v5);

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

    const SIMDVector s = VectorMultiply(matrix.v[0], SIMDMatrixDetail::s_SIMDMatrixSign);
    SIMDVector r = VectorMultiply(v0, p0);
    r = VectorNegativeMultiplySubtract(v1, p1, r);
    r = VectorMultiplyAdd(v2, p2, r);
    return Vector4Dot(s, r);
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixInverse(SIMDVector* outDeterminant, const SIMDMatrix& matrix)noexcept{
    const SIMDMatrix mt = MatrixTranspose(matrix);

    SIMDVector v0[4]{};
    SIMDVector v1[4]{};
    v0[0] = VectorSwizzle<0, 0, 1, 1>(mt.v[2]);
    v1[0] = VectorSwizzle<2, 3, 2, 3>(mt.v[3]);
    v0[1] = VectorSwizzle<0, 0, 1, 1>(mt.v[0]);
    v1[1] = VectorSwizzle<2, 3, 2, 3>(mt.v[1]);
    v0[2] = VectorPermute<0, 2, 4, 6>(mt.v[2], mt.v[0]);
    v1[2] = VectorPermute<1, 3, 5, 7>(mt.v[3], mt.v[1]);

    SIMDVector d0 = VectorMultiply(v0[0], v1[0]);
    SIMDVector d1 = VectorMultiply(v0[1], v1[1]);
    SIMDVector d2 = VectorMultiply(v0[2], v1[2]);

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

    SIMDVector c0 = VectorMultiply(v0[0], v1[0]);
    SIMDVector c2 = VectorMultiply(v0[1], v1[1]);
    SIMDVector c4 = VectorMultiply(v0[2], v1[2]);
    SIMDVector c6 = VectorMultiply(v0[3], v1[3]);

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

    SIMDVector c1 = VectorNegativeMultiplySubtract(v0[0], v1[0], c0);
    c0 = VectorMultiplyAdd(v0[0], v1[0], c0);
    SIMDVector c3 = VectorMultiplyAdd(v0[1], v1[1], c2);
    c2 = VectorNegativeMultiplySubtract(v0[1], v1[1], c2);
    SIMDVector c5 = VectorNegativeMultiplySubtract(v0[2], v1[2], c4);
    c4 = VectorMultiplyAdd(v0[2], v1[2], c4);
    SIMDVector c7 = VectorMultiplyAdd(v0[3], v1[3], c6);
    c6 = VectorNegativeMultiplySubtract(v0[3], v1[3], c6);

    SIMDMatrix r{};
    r.v[0] = VectorSelect(c0, c1, s_SIMDSelect0101);
    r.v[1] = VectorSelect(c2, c3, s_SIMDSelect0101);
    r.v[2] = VectorSelect(c4, c5, s_SIMDSelect0101);
    r.v[3] = VectorSelect(c6, c7, s_SIMDSelect0101);

    const SIMDVector determinant = Vector4Dot(r.v[0], mt.v[0]);
    if(outDeterminant != nullptr)
        *outDeterminant = determinant;

    const SIMDVector reciprocal = VectorReciprocal(determinant);
    SIMDMatrix result{};
    result.v[0] = VectorMultiply(r.v[0], reciprocal);
    result.v[1] = VectorMultiply(r.v[1], reciprocal);
    result.v[2] = VectorMultiply(r.v[2], reciprocal);
    result.v[3] = VectorMultiply(r.v[3], reciprocal);
    return result;
}

NWB_INLINE bool SIMDCALL MatrixDecompose(SIMDVector* outScale, SIMDVector* outRotQuat, SIMDVector* outTrans, const SIMDMatrix& matrix)noexcept{
    NWB_ASSERT(outScale != nullptr);
    NWB_ASSERT(outRotQuat != nullptr);
    NWB_ASSERT(outTrans != nullptr);

    static constexpr f32 epsilon = 0.0001f;

    const SIMDVector canonicalBasis[3] = {
        s_SIMDIdentityR0,
        s_SIMDIdentityR1,
        s_SIMDIdentityR2
    };

    SIMDMatrix transposed = MatrixTranspose(matrix);
    *outTrans = transposed.v[3];

    SIMDVector basis[3] = {
        VectorAndInt(transposed.v[0], s_SIMDMask3),
        VectorAndInt(transposed.v[1], s_SIMDMask3),
        VectorAndInt(transposed.v[2], s_SIMDMask3)
    };

    f32 scale[3] = {
        VectorGetX(Vector3Length(basis[0])),
        VectorGetX(Vector3Length(basis[1])),
        VectorGetX(Vector3Length(basis[2]))
    };

    usize a{};
    usize b{};
    usize c{};
    SIMDMatrixDetail::RankDecompose(a, b, c, scale[0], scale[1], scale[2]);

    if(scale[a] < epsilon)
        basis[a] = canonicalBasis[a];
    basis[a] = Vector3Normalize(basis[a]);

    if(scale[b] < epsilon){
        usize aa{};
        usize bb{};
        usize cc{};
        SIMDMatrixDetail::RankDecompose(
            aa,
            bb,
            cc,
            Abs(VectorGetX(basis[a])),
            Abs(VectorGetY(basis[a])),
            Abs(VectorGetZ(basis[a]))
        );
        basis[b] = Vector3Cross(basis[a], canonicalBasis[cc]);
    }
    basis[b] = Vector3Normalize(basis[b]);

    if(scale[c] < epsilon)
        basis[c] = Vector3Cross(basis[a], basis[b]);
    basis[c] = Vector3Normalize(basis[c]);

    SIMDMatrix basisAsRows{};
    basisAsRows.v[0] = basis[0];
    basisAsRows.v[1] = basis[1];
    basisAsRows.v[2] = basis[2];
    basisAsRows.v[3] = s_SIMDIdentityR3;

    SIMDMatrix rotationMatrix = MatrixTranspose(basisAsRows);
    f32 determinant = VectorGetX(MatrixDeterminant(rotationMatrix));
    if(determinant < 0.0f){
        scale[a] = -scale[a];
        basis[a] = VectorNegate(basis[a]);

        basisAsRows.v[0] = basis[0];
        basisAsRows.v[1] = basis[1];
        basisAsRows.v[2] = basis[2];
        rotationMatrix = MatrixTranspose(basisAsRows);
        determinant = -determinant;
    }

    determinant -= 1.0f;
    determinant *= determinant;
    if(epsilon < determinant)
        return false;

    *outScale = VectorSet(scale[0], scale[1], scale[2], 0.0f);
    *outRotQuat = QuaternionRotationMatrix(rotationMatrix);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// transformation operations


NWB_INLINE SIMDMatrix SIMDCALL MatrixIdentity()noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = s_SIMDIdentityR0;
    matrix.v[1] = s_SIMDIdentityR1;
    matrix.v[2] = s_SIMDIdentityR2;
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixSet(f32 m00, f32 m01, f32 m02, f32 m03, f32 m10, f32 m11, f32 m12, f32 m13, f32 m20, f32 m21, f32 m22, f32 m23, f32 m30, f32 m31, f32 m32, f32 m33)noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = VectorSet(m00, m01, m02, m03);
    matrix.v[1] = VectorSet(m10, m11, m12, m13);
    matrix.v[2] = VectorSet(m20, m21, m22, m23);
    matrix.v[3] = VectorSet(m30, m31, m32, m33);
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixTranslation(f32 offsetX, f32 offsetY, f32 offsetZ)noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = VectorSet(1.0f, 0.0f, 0.0f, offsetX);
    matrix.v[1] = VectorSet(0.0f, 1.0f, 0.0f, offsetY);
    matrix.v[2] = VectorSet(0.0f, 0.0f, 1.0f, offsetZ);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixTranslationFromVector(SIMDVector offset)noexcept{
    SIMDMatrix matrix{};
#if defined(NWB_HAS_SSE4)
    matrix.v[0] = _mm_blend_ps(s_SIMDIdentityR0, VectorSplatX(offset), 0x8);
    matrix.v[1] = _mm_blend_ps(s_SIMDIdentityR1, VectorSplatY(offset), 0x8);
    matrix.v[2] = _mm_blend_ps(s_SIMDIdentityR2, VectorSplatZ(offset), 0x8);
#else
    matrix.v[0] = VectorSelect(s_SIMDIdentityR0, VectorSplatX(offset), s_SIMDMaskW);
    matrix.v[1] = VectorSelect(s_SIMDIdentityR1, VectorSplatY(offset), s_SIMDMaskW);
    matrix.v[2] = VectorSelect(s_SIMDIdentityR2, VectorSplatZ(offset), s_SIMDMaskW);
#endif
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixScaling(f32 scaleX, f32 scaleY, f32 scaleZ)noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = VectorSet(scaleX, 0.0f, 0.0f, 0.0f);
    matrix.v[1] = VectorSet(0.0f, scaleY, 0.0f, 0.0f);
    matrix.v[2] = VectorSet(0.0f, 0.0f, scaleZ, 0.0f);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixScalingFromVector(SIMDVector scale)noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = VectorAndInt(scale, s_SIMDMaskX);
    matrix.v[1] = VectorAndInt(scale, s_SIMDMaskY);
    matrix.v[2] = VectorAndInt(scale, s_SIMDMaskZ);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixRotationX(f32 angle)noexcept{
    f32 sinAngle{};
    f32 cosAngle{};
    SIMDVectorDetail::ScalarSinCos(&sinAngle, &cosAngle, angle);

    SIMDMatrix matrix{};
    matrix.v[0] = s_SIMDIdentityR0;
    matrix.v[1] = VectorSet(0.0f, cosAngle, -sinAngle, 0.0f);
    matrix.v[2] = VectorSet(0.0f, sinAngle, cosAngle, 0.0f);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixRotationY(f32 angle)noexcept{
    f32 sinAngle{};
    f32 cosAngle{};
    SIMDVectorDetail::ScalarSinCos(&sinAngle, &cosAngle, angle);

    SIMDMatrix matrix{};
    matrix.v[0] = VectorSet(cosAngle, 0.0f, sinAngle, 0.0f);
    matrix.v[1] = s_SIMDIdentityR1;
    matrix.v[2] = VectorSet(-sinAngle, 0.0f, cosAngle, 0.0f);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixRotationZ(f32 angle)noexcept{
    f32 sinAngle{};
    f32 cosAngle{};
    SIMDVectorDetail::ScalarSinCos(&sinAngle, &cosAngle, angle);

    SIMDMatrix matrix{};
    matrix.v[0] = VectorSet(cosAngle, -sinAngle, 0.0f, 0.0f);
    matrix.v[1] = VectorSet(sinAngle, cosAngle, 0.0f, 0.0f);
    matrix.v[2] = s_SIMDIdentityR2;
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixRotationQuaternion(SIMDVector quaternion)noexcept{
    quaternion = QuaternionConjugate(quaternion);

    const SIMDVector q0 = VectorAdd(quaternion, quaternion);
    const SIMDVector q1 = VectorMultiply(quaternion, q0);

    SIMDVector v0 = VectorSwizzle<1, 0, 0, 3>(q1);
    v0 = VectorAndInt(v0, s_SIMDMask3);
    SIMDVector v1 = VectorSwizzle<2, 2, 1, 3>(q1);
    v1 = VectorAndInt(v1, s_SIMDMask3);
    SIMDVector r0 = VectorSubtract(s_SIMDOne3, v0);
    r0 = VectorSubtract(r0, v1);

    v0 = VectorSwizzle<0, 0, 1, 3>(quaternion);
    v1 = VectorSwizzle<2, 1, 2, 3>(q0);
    v0 = VectorMultiply(v0, v1);

    v1 = VectorSplatW(quaternion);
    const SIMDVector v2 = VectorSwizzle<1, 2, 0, 3>(q0);
    v1 = VectorMultiply(v1, v2);

    const SIMDVector r1 = VectorAdd(v0, v1);
    const SIMDVector r2 = VectorSubtract(v0, v1);

    v0 = VectorPermute<1, 4, 5, 2>(r1, r2);
    v1 = VectorPermute<0, 6, 0, 6>(r1, r2);

    SIMDMatrix matrix{};
    matrix.v[0] = VectorPermute<0, 4, 5, 3>(r0, v0);
    matrix.v[1] = VectorPermute<6, 1, 7, 3>(r0, v0);
    matrix.v[2] = VectorPermute<4, 5, 2, 3>(r0, v1);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixRotationRollPitchYawFromVector(SIMDVector angles)noexcept{
    return MatrixRotationQuaternion(QuaternionRotationRollPitchYawFromVector(angles));
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixRotationRollPitchYaw(f32 pitch, f32 yaw, f32 roll)noexcept{
    return MatrixRotationRollPitchYawFromVector(VectorSet(pitch, yaw, roll, 0.0f));
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixRotationNormal(SIMDVector normalAxis, f32 angle)noexcept{
    f32 sinAngle{};
    f32 cosAngle{};
    SIMDVectorDetail::ScalarSinCos(&sinAngle, &cosAngle, angle);

#if defined(NWB_HAS_SCALAR) || defined(NWB_HAS_NEON)
    const SIMDVector a = VectorSet(-sinAngle, cosAngle, 1.0f - cosAngle, 0.0f);
    const SIMDVector c2 = VectorSplatZ(a);
    const SIMDVector c1 = VectorSplatY(a);
    const SIMDVector c0 = VectorSplatX(a);

    const SIMDVector n0 = VectorSwizzle<1, 2, 0, 3>(normalAxis);
    const SIMDVector n1 = VectorSwizzle<2, 0, 1, 3>(normalAxis);

    SIMDVector v0 = VectorMultiply(c2, n0);
    v0 = VectorMultiply(v0, n1);

    SIMDVector r0 = VectorMultiply(c2, normalAxis);
    r0 = VectorMultiplyAdd(r0, normalAxis, c1);

    const SIMDVector s = VectorMultiply(c0, normalAxis);
    const SIMDVector r1 = VectorAdd(s, v0);
    const SIMDVector r2 = VectorSubtract(v0, s);

    v0 = VectorSelect(a, r0, s_SIMDMask3);
    const SIMDVector v1 = VectorPermute<2, 5, 6, 0>(r1, r2);
    const SIMDVector v2 = VectorPermute<1, 4, 1, 4>(r1, r2);

    SIMDMatrix matrix{};
    matrix.v[0] = VectorPermute<0, 4, 5, 3>(v0, v1);
    matrix.v[1] = VectorPermute<6, 1, 7, 3>(v0, v1);
    matrix.v[2] = VectorPermute<4, 5, 2, 3>(v0, v2);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
#else
    const SIMDVector c2 = _mm_set1_ps(1.0f - cosAngle);
    const SIMDVector c1 = _mm_set1_ps(cosAngle);
    const SIMDVector c0 = _mm_set1_ps(-sinAngle);

#if defined(NWB_HAS_AVX2)
    const SIMDVector n0 = _mm_permute_ps(normalAxis, _MM_SHUFFLE(3, 0, 2, 1));
    const SIMDVector n1 = _mm_permute_ps(normalAxis, _MM_SHUFFLE(3, 1, 0, 2));
#else
    const SIMDVector n0 = _mm_shuffle_ps(normalAxis, normalAxis, _MM_SHUFFLE(3, 0, 2, 1));
    const SIMDVector n1 = _mm_shuffle_ps(normalAxis, normalAxis, _MM_SHUFFLE(3, 1, 0, 2));
#endif

    SIMDVector v0 = _mm_mul_ps(c2, n0);
    v0 = _mm_mul_ps(v0, n1);

    SIMDVector r0 = _mm_mul_ps(c2, normalAxis);
    r0 = VectorMultiplyAdd(r0, normalAxis, c1);

    SIMDVector r1 = VectorMultiplyAdd(c0, normalAxis, v0);
    SIMDVector r2 = VectorNegativeMultiplySubtract(c0, normalAxis, v0);

    v0 = _mm_and_ps(r0, s_SIMDMask3);
    SIMDVector v1 = _mm_shuffle_ps(r1, r2, _MM_SHUFFLE(2, 1, 2, 0));
#if defined(NWB_HAS_AVX2)
    v1 = _mm_permute_ps(v1, _MM_SHUFFLE(0, 3, 2, 1));
#else
    v1 = _mm_shuffle_ps(v1, v1, _MM_SHUFFLE(0, 3, 2, 1));
#endif
    SIMDVector v2 = _mm_shuffle_ps(r1, r2, _MM_SHUFFLE(0, 0, 1, 1));
#if defined(NWB_HAS_AVX2)
    v2 = _mm_permute_ps(v2, _MM_SHUFFLE(2, 0, 2, 0));
#else
    v2 = _mm_shuffle_ps(v2, v2, _MM_SHUFFLE(2, 0, 2, 0));
#endif

    r2 = _mm_shuffle_ps(v0, v1, _MM_SHUFFLE(1, 0, 3, 0));
#if defined(NWB_HAS_AVX2)
    r2 = _mm_permute_ps(r2, _MM_SHUFFLE(1, 3, 2, 0));
#else
    r2 = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(1, 3, 2, 0));
#endif

    SIMDMatrix matrix{};
    matrix.v[0] = r2;

    r2 = _mm_shuffle_ps(v0, v1, _MM_SHUFFLE(3, 2, 3, 1));
#if defined(NWB_HAS_AVX2)
    r2 = _mm_permute_ps(r2, _MM_SHUFFLE(1, 3, 0, 2));
#else
    r2 = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(1, 3, 0, 2));
#endif
    matrix.v[1] = r2;

    matrix.v[2] = _mm_shuffle_ps(v2, v0, _MM_SHUFFLE(3, 2, 1, 0));
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
#endif
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixRotationAxis(SIMDVector axis, f32 angle)noexcept{
    NWB_ASSERT(!Vector3Equal(axis, VectorZero()));
    NWB_ASSERT(!Vector3IsInfinite(axis));

    return MatrixRotationNormal(Vector3Normalize(axis), angle);
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixAffineTransformation(SIMDVector scaling, SIMDVector rotationOrigin, SIMDVector rotationQuaternion, SIMDVector translation)noexcept{
    const SIMDVector origin = VectorSelect(VectorZero(), rotationOrigin, s_SIMDSelect1110);
    const SIMDVector negOrigin = VectorNegate(origin);

    SIMDMatrix matrix = MatrixMultiply(MatrixTranslationFromVector(translation), MatrixTranslationFromVector(origin));
    matrix = MatrixMultiply(matrix, MatrixRotationQuaternion(rotationQuaternion));
    matrix = MatrixMultiply(matrix, MatrixTranslationFromVector(negOrigin));
    matrix = MatrixMultiply(matrix, MatrixScalingFromVector(scaling));
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixAffineTransformation2D(SIMDVector scaling, SIMDVector rotationOrigin, f32 rotation, SIMDVector translation)noexcept{
    const SIMDVector vScaling = VectorSelect(s_SIMDOne, scaling, s_SIMDSelect1100);
    const SIMDVector origin = VectorSelect(VectorZero(), rotationOrigin, s_SIMDSelect1100);
    const SIMDVector negOrigin = VectorNegate(origin);
    const SIMDVector vTranslation = VectorSelect(VectorZero(), translation, s_SIMDSelect1100);

    SIMDMatrix matrix = MatrixMultiply(MatrixTranslationFromVector(vTranslation), MatrixTranslationFromVector(origin));
    matrix = MatrixMultiply(matrix, MatrixRotationZ(rotation));
    matrix = MatrixMultiply(matrix, MatrixTranslationFromVector(negOrigin));
    matrix = MatrixMultiply(matrix, MatrixScalingFromVector(vScaling));
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixTransformation(SIMDVector scalingOrigin, SIMDVector scalingOrientationQuaternion, SIMDVector scaling, SIMDVector rotationOrigin, SIMDVector rotationQuaternion, SIMDVector translation)noexcept{
    const SIMDVector vScalingOrigin = VectorSelect(VectorZero(), scalingOrigin, s_SIMDSelect1110);
    const SIMDVector negScalingOrigin = VectorNegate(vScalingOrigin);
    const SIMDVector vRotationOrigin = VectorSelect(VectorZero(), rotationOrigin, s_SIMDSelect1110);
    const SIMDVector negRotationOrigin = VectorNegate(vRotationOrigin);

    SIMDMatrix matrix = MatrixTranslationFromVector(translation);
    matrix = MatrixMultiply(matrix, MatrixTranslationFromVector(vRotationOrigin));
    matrix = MatrixMultiply(matrix, MatrixRotationQuaternion(rotationQuaternion));
    matrix = MatrixMultiply(matrix, MatrixTranslationFromVector(negRotationOrigin));
    matrix = MatrixMultiply(matrix, MatrixTranslationFromVector(vScalingOrigin));
    matrix = MatrixMultiply(matrix, MatrixRotationQuaternion(scalingOrientationQuaternion));
    matrix = MatrixMultiply(matrix, MatrixScalingFromVector(scaling));
    matrix = MatrixMultiply(matrix, MatrixTranspose(MatrixRotationQuaternion(scalingOrientationQuaternion)));
    matrix = MatrixMultiply(matrix, MatrixTranslationFromVector(negScalingOrigin));
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixTransformation2D(SIMDVector scalingOrigin, f32 scalingOrientation, SIMDVector scaling, SIMDVector rotationOrigin, f32 rotation, SIMDVector translation)noexcept{
    const SIMDVector vScalingOrigin = VectorSelect(VectorZero(), scalingOrigin, s_SIMDSelect1100);
    const SIMDVector negScalingOrigin = VectorNegate(vScalingOrigin);
    const SIMDVector vRotationOrigin = VectorSelect(VectorZero(), rotationOrigin, s_SIMDSelect1100);
    const SIMDVector negRotationOrigin = VectorNegate(vRotationOrigin);
    const SIMDVector vScaling = VectorSelect(s_SIMDOne, scaling, s_SIMDSelect1100);
    const SIMDVector vTranslation = VectorSelect(VectorZero(), translation, s_SIMDSelect1100);

    SIMDMatrix matrix = MatrixTranslationFromVector(vTranslation);
    matrix = MatrixMultiply(matrix, MatrixTranslationFromVector(vRotationOrigin));
    matrix = MatrixMultiply(matrix, MatrixRotationZ(rotation));
    matrix = MatrixMultiply(matrix, MatrixTranslationFromVector(negRotationOrigin));
    matrix = MatrixMultiply(matrix, MatrixTranslationFromVector(vScalingOrigin));
    matrix = MatrixMultiply(matrix, MatrixRotationZ(scalingOrientation));
    matrix = MatrixMultiply(matrix, MatrixScalingFromVector(vScaling));
    matrix = MatrixMultiply(matrix, MatrixRotationZ(-scalingOrientation));
    matrix = MatrixMultiply(matrix, MatrixTranslationFromVector(negScalingOrigin));
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixReflect(SIMDVector reflectionPlane)noexcept{
    NWB_ASSERT(!Vector3Equal(reflectionPlane, VectorZero()));
    NWB_ASSERT(!Vector4IsInfinite(reflectionPlane));

    const SIMDVector plane = SIMDMatrixDetail::PlaneNormalize(reflectionPlane);
    const SIMDVector s = VectorMultiply(plane, SIMDMatrixDetail::s_SIMDMatrixNegativeTwo);
    const SIMDVector a = VectorSplatX(plane);
    const SIMDVector b = VectorSplatY(plane);
    const SIMDVector c = VectorSplatZ(plane);
    const SIMDVector d = VectorSplatW(plane);

    SIMDMatrix rowVectorMatrix{};
    rowVectorMatrix.v[0] = VectorMultiplyAdd(a, s, s_SIMDIdentityR0);
    rowVectorMatrix.v[1] = VectorMultiplyAdd(b, s, s_SIMDIdentityR1);
    rowVectorMatrix.v[2] = VectorMultiplyAdd(c, s, s_SIMDIdentityR2);
    rowVectorMatrix.v[3] = VectorMultiplyAdd(d, s, s_SIMDIdentityR3);
    return MatrixTranspose(rowVectorMatrix);
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixShadow(SIMDVector shadowPlane, SIMDVector lightPosition)noexcept{
    NWB_ASSERT(!Vector3Equal(shadowPlane, VectorZero()));
    NWB_ASSERT(!Vector4IsInfinite(shadowPlane));

    SIMDVector plane = SIMDMatrixDetail::PlaneNormalize(shadowPlane);
    SIMDVector dot = Vector4Dot(plane, lightPosition);
    plane = VectorNegate(plane);

    const SIMDVector d = VectorSplatW(plane);
    const SIMDVector c = VectorSplatZ(plane);
    const SIMDVector b = VectorSplatY(plane);
    const SIMDVector a = VectorSplatX(plane);
    dot = VectorSelect(SIMDMatrixDetail::s_SIMDMatrixSelect0001, dot, SIMDMatrixDetail::s_SIMDMatrixSelect0001);

    SIMDMatrix rowVectorMatrix{};
    rowVectorMatrix.v[3] = VectorMultiplyAdd(d, lightPosition, dot);
    dot = VectorRotateLeft(dot, 1);
    rowVectorMatrix.v[2] = VectorMultiplyAdd(c, lightPosition, dot);
    dot = VectorRotateLeft(dot, 1);
    rowVectorMatrix.v[1] = VectorMultiplyAdd(b, lightPosition, dot);
    dot = VectorRotateLeft(dot, 1);
    rowVectorMatrix.v[0] = VectorMultiplyAdd(a, lightPosition, dot);
    return MatrixTranspose(rowVectorMatrix);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// view and projection operations


NWB_INLINE SIMDMatrix SIMDCALL MatrixLookToLH(SIMDVector eyePosition, SIMDVector eyeDirection, SIMDVector upDirection)noexcept{
    NWB_ASSERT(!Vector3Equal(eyeDirection, VectorZero()));
    NWB_ASSERT(!Vector3IsInfinite(eyeDirection));
    NWB_ASSERT(!Vector3Equal(upDirection, VectorZero()));
    NWB_ASSERT(!Vector3IsInfinite(upDirection));

    const SIMDVector r2 = Vector3Normalize(eyeDirection);
    SIMDVector r0 = Vector3Cross(upDirection, r2);
    r0 = Vector3Normalize(r0);
    const SIMDVector r1 = Vector3Cross(r2, r0);

    const SIMDVector negEyePosition = VectorNegate(eyePosition);
    const SIMDVector d0 = Vector3Dot(r0, negEyePosition);
    const SIMDVector d1 = Vector3Dot(r1, negEyePosition);
    const SIMDVector d2 = Vector3Dot(r2, negEyePosition);

    SIMDMatrix matrix{};
    matrix.v[0] = VectorSelect(d0, r0, s_SIMDSelect1110);
    matrix.v[1] = VectorSelect(d1, r1, s_SIMDSelect1110);
    matrix.v[2] = VectorSelect(d2, r2, s_SIMDSelect1110);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixLookToRH(SIMDVector eyePosition, SIMDVector eyeDirection, SIMDVector upDirection)noexcept{
    return MatrixLookToLH(eyePosition, VectorNegate(eyeDirection), upDirection);
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixLookAtLH(SIMDVector eyePosition, SIMDVector focusPosition, SIMDVector upDirection)noexcept{
    return MatrixLookToLH(eyePosition, VectorSubtract(focusPosition, eyePosition), upDirection);
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixLookAtRH(SIMDVector eyePosition, SIMDVector focusPosition, SIMDVector upDirection)noexcept{
    return MatrixLookToLH(eyePosition, VectorSubtract(eyePosition, focusPosition), upDirection);
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixPerspectiveLH(f32 viewWidth, f32 viewHeight, f32 nearZ, f32 farZ)noexcept{
    NWB_ASSERT(nearZ > 0.0f && farZ > 0.0f);
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewWidth, 0.0f, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewHeight, 0.0f, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, 0.00001f));

    const f32 twoNearZ = nearZ + nearZ;
    const f32 range = farZ / (farZ - nearZ);
    return MatrixSet(
        twoNearZ / viewWidth, 0.0f, 0.0f, 0.0f,
        0.0f, twoNearZ / viewHeight, 0.0f, 0.0f,
        0.0f, 0.0f, range, -range * nearZ,
        0.0f, 0.0f, 1.0f, 0.0f
    );
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixPerspectiveRH(f32 viewWidth, f32 viewHeight, f32 nearZ, f32 farZ)noexcept{
    NWB_ASSERT(nearZ > 0.0f && farZ > 0.0f);
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewWidth, 0.0f, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewHeight, 0.0f, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, 0.00001f));

    const f32 twoNearZ = nearZ + nearZ;
    const f32 range = farZ / (nearZ - farZ);
    return MatrixSet(
        twoNearZ / viewWidth, 0.0f, 0.0f, 0.0f,
        0.0f, twoNearZ / viewHeight, 0.0f, 0.0f,
        0.0f, 0.0f, range, range * nearZ,
        0.0f, 0.0f, -1.0f, 0.0f
    );
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixPerspectiveFovLH(f32 fovAngleY, f32 aspectRatio, f32 nearZ, f32 farZ)noexcept{
    NWB_ASSERT(nearZ > 0.0f && farZ > 0.0f);
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(fovAngleY, 0.0f, 0.00002f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(aspectRatio, 0.0f, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, 0.00001f));

    f32 sinFov{};
    f32 cosFov{};
    SIMDVectorDetail::ScalarSinCos(&sinFov, &cosFov, 0.5f * fovAngleY);
    const f32 height = cosFov / sinFov;
    const f32 width = height / aspectRatio;
    const f32 range = farZ / (farZ - nearZ);
    return MatrixSet(
        width, 0.0f, 0.0f, 0.0f,
        0.0f, height, 0.0f, 0.0f,
        0.0f, 0.0f, range, -range * nearZ,
        0.0f, 0.0f, 1.0f, 0.0f
    );
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixPerspectiveFovRH(f32 fovAngleY, f32 aspectRatio, f32 nearZ, f32 farZ)noexcept{
    NWB_ASSERT(nearZ > 0.0f && farZ > 0.0f);
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(fovAngleY, 0.0f, 0.00002f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(aspectRatio, 0.0f, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, 0.00001f));

    f32 sinFov{};
    f32 cosFov{};
    SIMDVectorDetail::ScalarSinCos(&sinFov, &cosFov, 0.5f * fovAngleY);
    const f32 height = cosFov / sinFov;
    const f32 width = height / aspectRatio;
    const f32 range = farZ / (nearZ - farZ);
    return MatrixSet(
        width, 0.0f, 0.0f, 0.0f,
        0.0f, height, 0.0f, 0.0f,
        0.0f, 0.0f, range, range * nearZ,
        0.0f, 0.0f, -1.0f, 0.0f
    );
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixPerspectiveOffCenterLH(f32 viewLeft, f32 viewRight, f32 viewBottom, f32 viewTop, f32 nearZ, f32 farZ)noexcept{
    NWB_ASSERT(nearZ > 0.0f && farZ > 0.0f);
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewRight, viewLeft, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewTop, viewBottom, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, 0.00001f));

    const f32 twoNearZ = nearZ + nearZ;
    const f32 reciprocalWidth = 1.0f / (viewRight - viewLeft);
    const f32 reciprocalHeight = 1.0f / (viewTop - viewBottom);
    const f32 range = farZ / (farZ - nearZ);
    return MatrixSet(
        twoNearZ * reciprocalWidth, 0.0f, -(viewLeft + viewRight) * reciprocalWidth, 0.0f,
        0.0f, twoNearZ * reciprocalHeight, -(viewTop + viewBottom) * reciprocalHeight, 0.0f,
        0.0f, 0.0f, range, -range * nearZ,
        0.0f, 0.0f, 1.0f, 0.0f
    );
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixPerspectiveOffCenterRH(f32 viewLeft, f32 viewRight, f32 viewBottom, f32 viewTop, f32 nearZ, f32 farZ)noexcept{
    NWB_ASSERT(nearZ > 0.0f && farZ > 0.0f);
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewRight, viewLeft, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewTop, viewBottom, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, 0.00001f));

    const f32 twoNearZ = nearZ + nearZ;
    const f32 reciprocalWidth = 1.0f / (viewRight - viewLeft);
    const f32 reciprocalHeight = 1.0f / (viewTop - viewBottom);
    const f32 range = farZ / (nearZ - farZ);
    return MatrixSet(
        twoNearZ * reciprocalWidth, 0.0f, (viewLeft + viewRight) * reciprocalWidth, 0.0f,
        0.0f, twoNearZ * reciprocalHeight, (viewTop + viewBottom) * reciprocalHeight, 0.0f,
        0.0f, 0.0f, range, range * nearZ,
        0.0f, 0.0f, -1.0f, 0.0f
    );
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixOrthographicLH(f32 viewWidth, f32 viewHeight, f32 nearZ, f32 farZ)noexcept{
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewWidth, 0.0f, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewHeight, 0.0f, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, 0.00001f));

    const f32 range = 1.0f / (farZ - nearZ);
    return MatrixSet(
        2.0f / viewWidth, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / viewHeight, 0.0f, 0.0f,
        0.0f, 0.0f, range, -range * nearZ,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixOrthographicRH(f32 viewWidth, f32 viewHeight, f32 nearZ, f32 farZ)noexcept{
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewWidth, 0.0f, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewHeight, 0.0f, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, 0.00001f));

    const f32 range = 1.0f / (nearZ - farZ);
    return MatrixSet(
        2.0f / viewWidth, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / viewHeight, 0.0f, 0.0f,
        0.0f, 0.0f, range, range * nearZ,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixOrthographicOffCenterLH(f32 viewLeft, f32 viewRight, f32 viewBottom, f32 viewTop, f32 nearZ, f32 farZ)noexcept{
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewRight, viewLeft, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewTop, viewBottom, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, 0.00001f));

    const f32 reciprocalWidth = 1.0f / (viewRight - viewLeft);
    const f32 reciprocalHeight = 1.0f / (viewTop - viewBottom);
    const f32 range = 1.0f / (farZ - nearZ);
    return MatrixSet(
        reciprocalWidth + reciprocalWidth, 0.0f, 0.0f, -(viewLeft + viewRight) * reciprocalWidth,
        0.0f, reciprocalHeight + reciprocalHeight, 0.0f, -(viewTop + viewBottom) * reciprocalHeight,
        0.0f, 0.0f, range, -range * nearZ,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

NWB_INLINE SIMDMatrix SIMDCALL MatrixOrthographicOffCenterRH(f32 viewLeft, f32 viewRight, f32 viewBottom, f32 viewTop, f32 nearZ, f32 farZ)noexcept{
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewRight, viewLeft, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewTop, viewBottom, 0.00001f));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, 0.00001f));

    const f32 reciprocalWidth = 1.0f / (viewRight - viewLeft);
    const f32 reciprocalHeight = 1.0f / (viewTop - viewBottom);
    const f32 range = 1.0f / (nearZ - farZ);
    return MatrixSet(
        reciprocalWidth + reciprocalWidth, 0.0f, 0.0f, -(viewLeft + viewRight) * reciprocalWidth,
        0.0f, reciprocalHeight + reciprocalHeight, 0.0f, -(viewTop + viewBottom) * reciprocalHeight,
        0.0f, 0.0f, range, range * nearZ,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

