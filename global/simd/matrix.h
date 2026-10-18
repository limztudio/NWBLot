// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "quaternion.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorConstF s_SIMDMatrixSign = { { { 1.0f, -1.0f, 1.0f, -1.0f } } };
static const SIMDVectorConstF s_SIMDMatrixNegativeTwo = { { { -2.0f, -2.0f, -2.0f, 0.0f } } };
static const SIMDVectorConstU s_SIMDMatrixSelect0001 = { { { s_SELECT_0, s_SELECT_0, s_SELECT_0, s_SELECT_1 } } };
inline constexpr f32 s_MatrixHalf = 0.5f;
inline constexpr f32 s_MatrixTwo = 2.0f;
inline constexpr f32 s_MatrixDecomposeEpsilon = 0.0001f;

inline constexpr f32 s_MatrixProjectionNearEqualEpsilon = 0.00001f;
inline constexpr f32 s_MatrixPerspectiveFovNearEqualEpsilon = s_MatrixProjectionNearEqualEpsilon * s_MatrixTwo;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T> requires(IsSame_V<T, f32> || IsSame_V<T, f64>)
NWB_INLINE bool ScalarNearEqual(T value0, T value1, T epsilon)noexcept{
    if constexpr(IsSame_V<T, f32>)
        return (VectorMoveMask(VectorNearEqual(VectorReplicate(value0), VectorReplicate(value1), VectorReplicate(epsilon))) & 1u) != 0u;
    else
        return (VectorDoubleMoveMask(VectorDoubleNearEqual(VectorDoubleReplicate(value0), VectorDoubleReplicate(value1), VectorDoubleReplicate(epsilon))) & 1u) != 0u;
}

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

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL PlaneNormalize(SIMDVectorSingle plane)noexcept{
#if defined(NWB_HAS_SSE4)
    SIMDVectorSingle lengthSq = _mm_dp_ps(plane, plane, 0x7F);
    const SIMDVectorSingle length = _mm_sqrt_ps(lengthSq);
    lengthSq = _mm_cmpneq_ps(lengthSq, s_SIMDInfinity);
    const SIMDVectorSingle result = _mm_div_ps(plane, length);
    return _mm_and_ps(result, lengthSq);
#else
    return VectorDivide(plane, Vector3Length(plane));
#endif
}

template<typename T> requires(IsSame_V<T, f32> || IsSame_V<T, f64>)
NWB_INLINE void RankDecompose(usize& a, usize& b, usize& c, T x, T y, T z)noexcept{
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


NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixPerspectiveImpl(const f32 viewWidth, const f32 viewHeight, const f32 nearZ, const f32 farZ, const SIMDVectorSingle rangeDenominator, const f32 rangeNearScale, const f32 forwardZ)noexcept{
    NWB_ASSERT(nearZ > 0.0f && farZ > 0.0f);
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewWidth, 0.0f, s_MatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewHeight, 0.0f, s_MatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, s_MatrixProjectionNearEqualEpsilon));

    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle nearZVector = VectorReplicate(nearZ);
    const SIMDVectorSingle twoNearZ = VectorAdd(nearZVector, nearZVector);
    const SIMDVectorSingle range = VectorDivide(VectorReplicate(farZ), rangeDenominator);
    const SIMDVectorSingle rangeNear = VectorMultiply(VectorScale(range, rangeNearScale), nearZVector);

    SIMDMatrix matrix{};
    matrix.v[0] = VectorMergeX(VectorDivide(twoNearZ, VectorReplicate(viewWidth)), zero, zero, zero);
    matrix.v[1] = VectorMergeX(zero, VectorDivide(twoNearZ, VectorReplicate(viewHeight)), zero, zero);
    matrix.v[2] = VectorMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = VectorMergeX(zero, zero, VectorReplicate(forwardZ), zero);
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixPerspectiveFovImpl(const f32 fovAngleY, const f32 aspectRatio, const f32 nearZ, const f32 farZ, const SIMDVectorSingle rangeDenominator, const f32 rangeNearScale, const f32 forwardZ)noexcept{
    NWB_ASSERT(nearZ > 0.0f && farZ > 0.0f);
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(fovAngleY, 0.0f, s_MatrixPerspectiveFovNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(aspectRatio, 0.0f, s_MatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, s_MatrixProjectionNearEqualEpsilon));

    SIMDVectorSingle sinFov{};
    SIMDVectorSingle cosFov{};
    VectorSinCos(sinFov, cosFov, VectorScale(VectorReplicate(fovAngleY), s_MatrixHalf));
    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle height = VectorDivide(cosFov, sinFov);
    const SIMDVectorSingle width = VectorDivide(height, VectorReplicate(aspectRatio));
    const SIMDVectorSingle range = VectorDivide(VectorReplicate(farZ), rangeDenominator);
    const SIMDVectorSingle rangeNear = VectorMultiply(VectorMultiply(VectorReplicate(rangeNearScale), range), VectorReplicate(nearZ));

    SIMDMatrix matrix{};
    matrix.v[0] = VectorMergeX(width, zero, zero, zero);
    matrix.v[1] = VectorMergeX(zero, height, zero, zero);
    matrix.v[2] = VectorMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = VectorMergeX(zero, zero, VectorReplicate(forwardZ), zero);
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixPerspectiveOffCenterImpl(
    const f32 viewLeft,
    const f32 viewRight,
    const f32 viewBottom,
    const f32 viewTop,
    const f32 nearZ,
    const f32 farZ,
    const SIMDVectorSingle rangeDenominator,
    const f32 centerScale,
    const f32 rangeNearScale,
    const f32 forwardZ
)noexcept{
    NWB_ASSERT(nearZ > 0.0f && farZ > 0.0f);
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewRight, viewLeft, s_MatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewTop, viewBottom, s_MatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, s_MatrixProjectionNearEqualEpsilon));

    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle left = VectorReplicate(viewLeft);
    const SIMDVectorSingle right = VectorReplicate(viewRight);
    const SIMDVectorSingle bottom = VectorReplicate(viewBottom);
    const SIMDVectorSingle top = VectorReplicate(viewTop);
    const SIMDVectorSingle nearZVector = VectorReplicate(nearZ);
    const SIMDVectorSingle twoNearZ = VectorAdd(nearZVector, nearZVector);
    const SIMDVectorSingle reciprocalWidth = VectorReciprocal(VectorSubtract(right, left));
    const SIMDVectorSingle reciprocalHeight = VectorReciprocal(VectorSubtract(top, bottom));
    const SIMDVectorSingle range = VectorDivide(VectorReplicate(farZ), rangeDenominator);
    const SIMDVectorSingle rangeNear = VectorMultiply(VectorScale(range, rangeNearScale), nearZVector);
    const SIMDVectorSingle offsetX = VectorMultiply(VectorScale(VectorAdd(left, right), centerScale), reciprocalWidth);
    const SIMDVectorSingle offsetY = VectorMultiply(VectorScale(VectorAdd(bottom, top), centerScale), reciprocalHeight);

    SIMDMatrix matrix{};
    matrix.v[0] = VectorMergeX(VectorMultiply(twoNearZ, reciprocalWidth), zero, offsetX, zero);
    matrix.v[1] = VectorMergeX(zero, VectorMultiply(twoNearZ, reciprocalHeight), offsetY, zero);
    matrix.v[2] = VectorMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = VectorMergeX(zero, zero, VectorReplicate(forwardZ), zero);
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixOrthographicImpl(const f32 viewWidth, const f32 viewHeight, const f32 nearZ, const SIMDVectorSingle rangeDenominator, const f32 rangeNearScale)noexcept{
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewWidth, 0.0f, s_MatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewHeight, 0.0f, s_MatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(VectorGetX(rangeDenominator), 0.0f, s_MatrixProjectionNearEqualEpsilon));

    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle nearZVector = VectorReplicate(nearZ);
    const SIMDVectorSingle range = VectorReciprocal(rangeDenominator);
    const SIMDVectorSingle rangeNear = VectorMultiply(VectorScale(range, rangeNearScale), nearZVector);

    SIMDMatrix matrix{};
    matrix.v[0] = VectorMergeX(VectorDivide(s_SIMDTwo, VectorReplicate(viewWidth)), zero, zero, zero);
    matrix.v[1] = VectorMergeX(zero, VectorDivide(s_SIMDTwo, VectorReplicate(viewHeight)), zero, zero);
    matrix.v[2] = VectorMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixOrthographicOffCenterImpl(const f32 viewLeft, const f32 viewRight, const f32 viewBottom, const f32 viewTop, const f32 nearZ, const SIMDVectorSingle rangeDenominator, const f32 rangeNearScale)noexcept{
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewRight, viewLeft, s_MatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewTop, viewBottom, s_MatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(VectorGetX(rangeDenominator), 0.0f, s_MatrixProjectionNearEqualEpsilon));

    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle left = VectorReplicate(viewLeft);
    const SIMDVectorSingle right = VectorReplicate(viewRight);
    const SIMDVectorSingle bottom = VectorReplicate(viewBottom);
    const SIMDVectorSingle top = VectorReplicate(viewTop);
    const SIMDVectorSingle nearZVector = VectorReplicate(nearZ);
    const SIMDVectorSingle reciprocalWidth = VectorReciprocal(VectorSubtract(right, left));
    const SIMDVectorSingle reciprocalHeight = VectorReciprocal(VectorSubtract(top, bottom));
    const SIMDVectorSingle range = VectorReciprocal(rangeDenominator);
    const SIMDVectorSingle rangeNear = VectorMultiply(VectorScale(range, rangeNearScale), nearZVector);
    const SIMDVectorSingle offsetX = VectorNegate(VectorMultiply(VectorAdd(left, right), reciprocalWidth));
    const SIMDVectorSingle offsetY = VectorNegate(VectorMultiply(VectorAdd(bottom, top), reciprocalHeight));

    SIMDMatrix matrix{};
    matrix.v[0] = VectorMergeX(VectorAdd(reciprocalWidth, reciprocalWidth), zero, zero, offsetX);
    matrix.v[1] = VectorMergeX(zero, VectorAdd(reciprocalHeight, reciprocalHeight), zero, offsetY);
    matrix.v[2] = VectorMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorDoubleConstF s_DoubleSIMDMatrixSign = { { { 1.0, -1.0, 1.0, -1.0 } } };
static const SIMDVectorDoubleConstF s_DoubleSIMDMatrixNegativeTwo = { { { -2.0, -2.0, -2.0, 0.0 } } };
static const SIMDVectorDoubleConstU s_DoubleSIMDMatrixSelect0001 = { { { s_DoubleSELECT_0, s_DoubleSELECT_0, s_DoubleSELECT_0, s_DoubleSELECT_1 } } };
inline constexpr f64 s_DoubleMatrixHalf = 0.5;
inline constexpr f64 s_DoubleMatrixTwo = 2.0;
inline constexpr f64 s_DoubleMatrixDecomposeEpsilon = 1.0e-12;

inline constexpr f64 s_DoubleMatrixProjectionNearEqualEpsilon = 1.0e-12;
inline constexpr f64 s_DoubleMatrixPerspectiveFovNearEqualEpsilon = s_DoubleMatrixProjectionNearEqualEpsilon * s_DoubleMatrixTwo;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL MatrixDoubleRowMultiply(SIMDVectorDouble row, const SIMDMatrixDouble& matrix)noexcept{
    SIMDVectorDouble result = VectorDoubleMultiply(VectorDoubleSplatX(row), matrix.v[0]);
    result = VectorDoubleMultiplyAdd(VectorDoubleSplatY(row), matrix.v[1], result);
    result = VectorDoubleMultiplyAdd(VectorDoubleSplatZ(row), matrix.v[2], result);
    return VectorDoubleMultiplyAdd(VectorDoubleSplatW(row), matrix.v[3], result);
}


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL PlaneDoubleNormalize(SIMDVectorDouble plane)noexcept{
    return VectorDoubleDivide(plane, VectorDouble3Length(plane));
}


NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveImpl(const f64 viewWidth, const f64 viewHeight, const f64 nearZ, const f64 farZ, const SIMDVectorDouble rangeDenominator, const f64 rangeNearScale, const f64 forwardZ)noexcept{
    NWB_ASSERT(nearZ > 0.0 && farZ > 0.0);
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewWidth, 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewHeight, 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, s_DoubleMatrixProjectionNearEqualEpsilon));

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble nearZVector = VectorDoubleReplicate(nearZ);
    const SIMDVectorDouble twoNearZ = VectorDoubleAdd(nearZVector, nearZVector);
    const SIMDVectorDouble range = VectorDoubleDivide(VectorDoubleReplicate(farZ), rangeDenominator);
    const SIMDVectorDouble rangeNear = VectorDoubleMultiply(VectorDoubleScale(range, rangeNearScale), nearZVector);

    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleMergeX(VectorDoubleDivide(twoNearZ, VectorDoubleReplicate(viewWidth)), zero, zero, zero);
    matrix.v[1] = VectorDoubleMergeX(zero, VectorDoubleDivide(twoNearZ, VectorDoubleReplicate(viewHeight)), zero, zero);
    matrix.v[2] = VectorDoubleMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = VectorDoubleMergeX(zero, zero, VectorDoubleReplicate(forwardZ), zero);
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveFovImpl(const f64 fovAngleY, const f64 aspectRatio, const f64 nearZ, const f64 farZ, const SIMDVectorDouble rangeDenominator, const f64 rangeNearScale, const f64 forwardZ)noexcept{
    NWB_ASSERT(nearZ > 0.0 && farZ > 0.0);
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(fovAngleY, 0.0, s_DoubleMatrixPerspectiveFovNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(aspectRatio, 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, s_DoubleMatrixProjectionNearEqualEpsilon));

    SIMDVectorDouble sinFov{};
    SIMDVectorDouble cosFov{};
    SIMDVectorDoubleDetail::SinCosReplicated(sinFov, cosFov, VectorDoubleScale(VectorDoubleReplicate(fovAngleY), s_DoubleMatrixHalf));
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble height = VectorDoubleDivide(cosFov, sinFov);
    const SIMDVectorDouble width = VectorDoubleDivide(height, VectorDoubleReplicate(aspectRatio));
    const SIMDVectorDouble range = VectorDoubleDivide(VectorDoubleReplicate(farZ), rangeDenominator);
    const SIMDVectorDouble rangeNear = VectorDoubleMultiply(VectorDoubleMultiply(VectorDoubleReplicate(rangeNearScale), range), VectorDoubleReplicate(nearZ));

    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleMergeX(width, zero, zero, zero);
    matrix.v[1] = VectorDoubleMergeX(zero, height, zero, zero);
    matrix.v[2] = VectorDoubleMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = VectorDoubleMergeX(zero, zero, VectorDoubleReplicate(forwardZ), zero);
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveOffCenterImpl(
    const f64 viewLeft,
    const f64 viewRight,
    const f64 viewBottom,
    const f64 viewTop,
    const f64 nearZ,
    const f64 farZ,
    const SIMDVectorDouble rangeDenominator,
    const f64 centerScale,
    const f64 rangeNearScale,
    const f64 forwardZ
)noexcept{
    NWB_ASSERT(nearZ > 0.0 && farZ > 0.0);
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewRight, viewLeft, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewTop, viewBottom, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(farZ, nearZ, s_DoubleMatrixProjectionNearEqualEpsilon));

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble left = VectorDoubleReplicate(viewLeft);
    const SIMDVectorDouble right = VectorDoubleReplicate(viewRight);
    const SIMDVectorDouble bottom = VectorDoubleReplicate(viewBottom);
    const SIMDVectorDouble top = VectorDoubleReplicate(viewTop);
    const SIMDVectorDouble nearZVector = VectorDoubleReplicate(nearZ);
    const SIMDVectorDouble twoNearZ = VectorDoubleAdd(nearZVector, nearZVector);
    const SIMDVectorDouble reciprocalWidth = VectorDoubleReciprocal(VectorDoubleSubtract(right, left));
    const SIMDVectorDouble reciprocalHeight = VectorDoubleReciprocal(VectorDoubleSubtract(top, bottom));
    const SIMDVectorDouble range = VectorDoubleDivide(VectorDoubleReplicate(farZ), rangeDenominator);
    const SIMDVectorDouble rangeNear = VectorDoubleMultiply(VectorDoubleScale(range, rangeNearScale), nearZVector);
    const SIMDVectorDouble offsetX = VectorDoubleMultiply(VectorDoubleScale(VectorDoubleAdd(left, right), centerScale), reciprocalWidth);
    const SIMDVectorDouble offsetY = VectorDoubleMultiply(VectorDoubleScale(VectorDoubleAdd(bottom, top), centerScale), reciprocalHeight);

    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleMergeX(VectorDoubleMultiply(twoNearZ, reciprocalWidth), zero, offsetX, zero);
    matrix.v[1] = VectorDoubleMergeX(zero, VectorDoubleMultiply(twoNearZ, reciprocalHeight), offsetY, zero);
    matrix.v[2] = VectorDoubleMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = VectorDoubleMergeX(zero, zero, VectorDoubleReplicate(forwardZ), zero);
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleOrthographicImpl(const f64 viewWidth, const f64 viewHeight, const f64 nearZ, const SIMDVectorDouble rangeDenominator, const f64 rangeNearScale)noexcept{
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewWidth, 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewHeight, 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(VectorDoubleGetX(rangeDenominator), 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble nearZVector = VectorDoubleReplicate(nearZ);
    const SIMDVectorDouble range = VectorDoubleReciprocal(rangeDenominator);
    const SIMDVectorDouble rangeNear = VectorDoubleMultiply(VectorDoubleScale(range, rangeNearScale), nearZVector);

    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleMergeX(VectorDoubleDivide(s_DoubleSIMDTwo, VectorDoubleReplicate(viewWidth)), zero, zero, zero);
    matrix.v[1] = VectorDoubleMergeX(zero, VectorDoubleDivide(s_DoubleSIMDTwo, VectorDoubleReplicate(viewHeight)), zero, zero);
    matrix.v[2] = VectorDoubleMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleOrthographicOffCenterImpl(const f64 viewLeft, const f64 viewRight, const f64 viewBottom, const f64 viewTop, const f64 nearZ, const SIMDVectorDouble rangeDenominator, const f64 rangeNearScale)noexcept{
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewRight, viewLeft, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewTop, viewBottom, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(VectorDoubleGetX(rangeDenominator), 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble left = VectorDoubleReplicate(viewLeft);
    const SIMDVectorDouble right = VectorDoubleReplicate(viewRight);
    const SIMDVectorDouble bottom = VectorDoubleReplicate(viewBottom);
    const SIMDVectorDouble top = VectorDoubleReplicate(viewTop);
    const SIMDVectorDouble nearZVector = VectorDoubleReplicate(nearZ);
    const SIMDVectorDouble reciprocalWidth = VectorDoubleReciprocal(VectorDoubleSubtract(right, left));
    const SIMDVectorDouble reciprocalHeight = VectorDoubleReciprocal(VectorDoubleSubtract(top, bottom));
    const SIMDVectorDouble range = VectorDoubleReciprocal(rangeDenominator);
    const SIMDVectorDouble rangeNear = VectorDoubleMultiply(VectorDoubleScale(range, rangeNearScale), nearZVector);
    const SIMDVectorDouble offsetX = VectorDoubleNegate(VectorDoubleMultiply(VectorDoubleAdd(left, right), reciprocalWidth));
    const SIMDVectorDouble offsetY = VectorDoubleNegate(VectorDoubleMultiply(VectorDoubleAdd(bottom, top), reciprocalHeight));

    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleMergeX(VectorDoubleAdd(reciprocalWidth, reciprocalWidth), zero, zero, offsetX);
    matrix.v[1] = VectorDoubleMergeX(zero, VectorDoubleAdd(reciprocalHeight, reciprocalHeight), zero, offsetY);
    matrix.v[2] = VectorDoubleMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MatrixDecomposition{
    SIMDVectorSingle scale;
    SIMDVectorSingle rotation;
    SIMDVectorSingle translation;
};

struct MatrixDoubleDecomposition{
    SIMDVectorDouble scale;
    SIMDVectorDouble rotation;
    SIMDVectorDouble translation;
};

struct RigidDualQuaternion{
    SIMDVectorSingle real;
    SIMDVectorSingle dual;
};

struct RigidDualQuaternionDouble{
    SIMDVectorDouble real;
    SIMDVectorDouble dual;
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


[[nodiscard]] NWB_INLINE Expected<MatrixDecomposition> NWB_SIMD_CALL MatrixDecompose(const SIMDMatrix& matrix)noexcept{
    const SIMDVectorSingle canonicalBasis[3] = { s_SIMDIdentityR0, s_SIMDIdentityR1, s_SIMDIdentityR2 };

    SIMDMatrix transposed = MatrixTranspose(matrix);

    SIMDVectorSingle basis[3] = { VectorAndInt(transposed.v[0], s_SIMDMask3), VectorAndInt(transposed.v[1], s_SIMDMask3), VectorAndInt(transposed.v[2], s_SIMDMask3) };

    SIMDVectorSingle scale = VectorMergeX(Vector3Length(basis[0]), Vector3Length(basis[1]), Vector3Length(basis[2]), VectorZero());

    usize a{};
    usize b{};
    usize c{};
    SIMDMatrixDetail::RankDecompose(a, b, c, VectorGetX(scale), VectorGetY(scale), VectorGetZ(scale));

    if(VectorGetByIndex(scale, a) < SIMDMatrixDetail::s_MatrixDecomposeEpsilon)
        basis[a] = canonicalBasis[a];
    basis[a] = Vector3Normalize(basis[a]);

    if(VectorGetByIndex(scale, b) < SIMDMatrixDetail::s_MatrixDecomposeEpsilon){
        usize aa{};
        usize bb{};
        usize cc{};
        const SIMDVectorSingle absoluteBasis = VectorAbs(basis[a]);
        SIMDMatrixDetail::RankDecompose(aa, bb, cc, VectorGetX(absoluteBasis), VectorGetY(absoluteBasis), VectorGetZ(absoluteBasis));
        basis[b] = Vector3Cross(basis[a], canonicalBasis[cc]);
    }
    basis[b] = Vector3Normalize(basis[b]);

    if(VectorGetByIndex(scale, c) < SIMDMatrixDetail::s_MatrixDecomposeEpsilon)
        basis[c] = Vector3Cross(basis[a], basis[b]);
    basis[c] = Vector3Normalize(basis[c]);

    SIMDMatrix basisAsRows{};
    basisAsRows.v[0] = basis[0];
    basisAsRows.v[1] = basis[1];
    basisAsRows.v[2] = basis[2];
    basisAsRows.v[3] = s_SIMDIdentityR3;

    SIMDMatrix rotationMatrix = MatrixTranspose(basisAsRows);
    SIMDVectorSingle determinant = MatrixDeterminant(rotationMatrix);
    if(Vector4Less(determinant, VectorZero())){
        scale = VectorSelect(scale, VectorNegate(scale), VectorSelectControl(a == 0u, a == 1u, a == 2u, a == 3u));
        basis[a] = VectorNegate(basis[a]);

        basisAsRows.v[0] = basis[0];
        basisAsRows.v[1] = basis[1];
        basisAsRows.v[2] = basis[2];
        rotationMatrix = MatrixTranspose(basisAsRows);
        determinant = VectorNegate(determinant);
    }

    const SIMDVectorSingle determinantError = VectorSubtract(determinant, s_SIMDOne);
    if(Vector4Greater(VectorMultiply(determinantError, determinantError), VectorReplicate(SIMDMatrixDetail::s_MatrixDecomposeEpsilon)))
        return MakeUnexpected(Failure{});

    return MatrixDecomposition{ scale, QuaternionRotationMatrix(rotationMatrix), transposed.v[3] };
}

[[nodiscard]] NWB_INLINE Expected<MatrixDoubleDecomposition> NWB_SIMD_CALL MatrixDoubleDecompose(const SIMDMatrixDouble& matrix)noexcept{
    const SIMDVectorDouble canonicalBasis[3] = { s_DoubleSIMDIdentityR0, s_DoubleSIMDIdentityR1, s_DoubleSIMDIdentityR2 };

    SIMDMatrixDouble transposed = MatrixDoubleTranspose(matrix);

    SIMDVectorDouble basis[3] = { VectorDoubleAndInt(transposed.v[0], s_DoubleSIMDMask3), VectorDoubleAndInt(transposed.v[1], s_DoubleSIMDMask3), VectorDoubleAndInt(transposed.v[2], s_DoubleSIMDMask3) };

    SIMDVectorDouble scale = VectorDoubleMergeX(VectorDouble3Length(basis[0]), VectorDouble3Length(basis[1]), VectorDouble3Length(basis[2]), VectorDoubleZero());

    usize a{};
    usize b{};
    usize c{};
    SIMDMatrixDetail::RankDecompose(a, b, c, VectorDoubleGetX(scale), VectorDoubleGetY(scale), VectorDoubleGetZ(scale));

    if(VectorDoubleGetByIndex(scale, a) < SIMDMatrixDoubleDetail::s_DoubleMatrixDecomposeEpsilon)
        basis[a] = canonicalBasis[a];
    basis[a] = VectorDouble3Normalize(basis[a]);

    if(VectorDoubleGetByIndex(scale, b) < SIMDMatrixDoubleDetail::s_DoubleMatrixDecomposeEpsilon){
        usize aa{};
        usize bb{};
        usize cc{};
        const SIMDVectorDouble absoluteBasis = VectorDoubleAbs(basis[a]);
        SIMDMatrixDetail::RankDecompose(aa, bb, cc, VectorDoubleGetX(absoluteBasis), VectorDoubleGetY(absoluteBasis), VectorDoubleGetZ(absoluteBasis));
        basis[b] = VectorDouble3Cross(basis[a], canonicalBasis[cc]);
    }
    basis[b] = VectorDouble3Normalize(basis[b]);

    if(VectorDoubleGetByIndex(scale, c) < SIMDMatrixDoubleDetail::s_DoubleMatrixDecomposeEpsilon)
        basis[c] = VectorDouble3Cross(basis[a], basis[b]);
    basis[c] = VectorDouble3Normalize(basis[c]);

    SIMDMatrixDouble basisAsRows{};
    basisAsRows.v[0] = basis[0];
    basisAsRows.v[1] = basis[1];
    basisAsRows.v[2] = basis[2];
    basisAsRows.v[3] = s_DoubleSIMDIdentityR3;

    SIMDMatrixDouble rotationMatrix = MatrixDoubleTranspose(basisAsRows);
    SIMDVectorDouble determinant = MatrixDoubleDeterminant(rotationMatrix);
    if(VectorDouble4Less(determinant, VectorDoubleZero())){
        scale = VectorDoubleSelect(scale, VectorDoubleNegate(scale), VectorDoubleSelectControl(a == 0u, a == 1u, a == 2u, a == 3u));
        basis[a] = VectorDoubleNegate(basis[a]);

        basisAsRows.v[0] = basis[0];
        basisAsRows.v[1] = basis[1];
        basisAsRows.v[2] = basis[2];
        rotationMatrix = MatrixDoubleTranspose(basisAsRows);
        determinant = VectorDoubleNegate(determinant);
    }

    const SIMDVectorDouble determinantError = VectorDoubleSubtract(determinant, s_DoubleSIMDOne);
    if(VectorDouble4Greater(VectorDoubleMultiply(determinantError, determinantError), VectorDoubleReplicate(SIMDMatrixDoubleDetail::s_DoubleMatrixDecomposeEpsilon)))
        return MakeUnexpected(Failure{});

    return MatrixDoubleDecomposition{ scale, QuaternionDoubleRotationMatrix(rotationMatrix), transposed.v[3] };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL MatrixIsAffine(const SIMDMatrix& matrix, const f32 epsilon)noexcept{
    return
        VectorIsFinite(matrix.v[0], VectorComponentMask::s_XYZW)
        && VectorIsFinite(matrix.v[1], VectorComponentMask::s_XYZW)
        && VectorIsFinite(matrix.v[2], VectorComponentMask::s_XYZW)
        && VectorIsFinite(matrix.v[3], VectorComponentMask::s_XYZW)
        && Vector4NearEqual(matrix.v[3], s_SIMDIdentityR3, VectorReplicate(epsilon))
    ;
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL MatrixDoubleIsAffine(const SIMDMatrixDouble& matrix, const f64 epsilon)noexcept{
    return
        VectorDoubleIsFinite(matrix.v[0], VectorDoubleComponentMask::s_DoubleXYZW)
        && VectorDoubleIsFinite(matrix.v[1], VectorDoubleComponentMask::s_DoubleXYZW)
        && VectorDoubleIsFinite(matrix.v[2], VectorDoubleComponentMask::s_DoubleXYZW)
        && VectorDoubleIsFinite(matrix.v[3], VectorDoubleComponentMask::s_DoubleXYZW)
        && VectorDouble4NearEqual(matrix.v[3], s_DoubleSIMDIdentityR3, VectorDoubleReplicate(epsilon))
    ;
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL MatrixLinearDeterminantV(const SIMDMatrix& matrix)noexcept{
    const SIMDVectorSingle row0 = VectorSetW(matrix.v[0], 0.0f);
    const SIMDVectorSingle row1 = VectorSetW(matrix.v[1], 0.0f);
    const SIMDVectorSingle row2 = VectorSetW(matrix.v[2], 0.0f);
    return Vector3Dot(row0, Vector3Cross(row1, row2));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL MatrixDoubleLinearDeterminantV(const SIMDMatrixDouble& matrix)noexcept{
    const SIMDVectorDouble row0 = VectorDoubleSetW(matrix.v[0], 0.0);
    const SIMDVectorDouble row1 = VectorDoubleSetW(matrix.v[1], 0.0);
    const SIMDVectorDouble row2 = VectorDoubleSetW(matrix.v[2], 0.0);
    return VectorDouble3Dot(row0, VectorDouble3Cross(row1, row2));
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL MatrixIsInvertibleAffine(const SIMDMatrix& matrix, const f32 affineEpsilon, const f32 determinantEpsilon)noexcept{
    if(!MatrixIsAffine(matrix, affineEpsilon))
        return false;

    const SIMDVectorSingle determinant = MatrixLinearDeterminantV(matrix);
    return VectorIsFinite(determinant, VectorComponentMask::s_XYZW) && Vector4Greater(VectorAbs(determinant), VectorReplicate(determinantEpsilon));
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL MatrixDoubleIsInvertibleAffine(const SIMDMatrixDouble& matrix, const f64 affineEpsilon, const f64 determinantEpsilon)noexcept{
    if(!MatrixDoubleIsAffine(matrix, affineEpsilon))
        return false;

    const SIMDVectorDouble determinant = MatrixDoubleLinearDeterminantV(matrix);
    return VectorDoubleIsFinite(determinant, VectorDoubleComponentMask::s_DoubleXYZW) && VectorDouble4Greater(VectorDoubleAbs(determinant), VectorDoubleReplicate(determinantEpsilon));
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL MatrixIsRigidAffine(const SIMDMatrix& matrix, const f32 affineEpsilon, const f32 rigidEpsilon)noexcept{
    if(!MatrixIsAffine(matrix, affineEpsilon))
        return false;

    const SIMDVectorSingle row0 = VectorSetW(matrix.v[0], 0.0f);
    const SIMDVectorSingle row1 = VectorSetW(matrix.v[1], 0.0f);
    const SIMDVectorSingle row2 = VectorSetW(matrix.v[2], 0.0f);
    const SIMDVectorSingle lengthSq = VectorMergeX(Vector3LengthSq(row0), Vector3LengthSq(row1), Vector3LengthSq(row2), s_SIMDOne);
    const SIMDVectorSingle dotAndDeterminant = VectorMergeX(Vector3Dot(row0, row1), Vector3Dot(row0, row2), Vector3Dot(row1, row2), Vector3Dot(row0, Vector3Cross(row1, row2)));
    const SIMDVectorSingle epsilon = VectorReplicate(rigidEpsilon);
    return
        VectorIsFinite(lengthSq, VectorComponentMask::s_XYZ)
        && VectorIsFinite(dotAndDeterminant, VectorComponentMask::s_XYZW)
        && Vector4LessOrEqual(VectorAbs(VectorSubtract(lengthSq, s_SIMDOne)), epsilon)
        && Vector4LessOrEqual(VectorAbs(VectorSubtract(dotAndDeterminant, s_SIMDIdentityR3)), epsilon)
    ;
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL MatrixDoubleIsRigidAffine(const SIMDMatrixDouble& matrix, const f64 affineEpsilon, const f64 rigidEpsilon)noexcept{
    if(!MatrixDoubleIsAffine(matrix, affineEpsilon))
        return false;

    const SIMDVectorDouble row0 = VectorDoubleSetW(matrix.v[0], 0.0);
    const SIMDVectorDouble row1 = VectorDoubleSetW(matrix.v[1], 0.0);
    const SIMDVectorDouble row2 = VectorDoubleSetW(matrix.v[2], 0.0);
    const SIMDVectorDouble lengthSq = VectorDoubleMergeX(VectorDouble3LengthSq(row0), VectorDouble3LengthSq(row1), VectorDouble3LengthSq(row2), s_DoubleSIMDOne);
    const SIMDVectorDouble dotAndDeterminant = VectorDoubleMergeX(VectorDouble3Dot(row0, row1), VectorDouble3Dot(row0, row2), VectorDouble3Dot(row1, row2), VectorDouble3Dot(row0, VectorDouble3Cross(row1, row2)));
    const SIMDVectorDouble epsilon = VectorDoubleReplicate(rigidEpsilon);
    return
        VectorDoubleIsFinite(lengthSq, VectorDoubleComponentMask::s_DoubleXYZ)
        && VectorDoubleIsFinite(dotAndDeterminant, VectorDoubleComponentMask::s_DoubleXYZW)
        && VectorDouble4LessOrEqual(VectorDoubleAbs(VectorDoubleSubtract(lengthSq, s_DoubleSIMDOne)), epsilon)
        && VectorDouble4LessOrEqual(VectorDoubleAbs(VectorDoubleSubtract(dotAndDeterminant, s_DoubleSIMDIdentityR3)), epsilon)
    ;
}

[[nodiscard]] NWB_INLINE Expected<SIMDVectorSingle> NWB_SIMD_CALL MatrixTryBuildRigidRotationQuaternion(const SIMDMatrix& matrix, const f32 affineEpsilon, const f32 rigidEpsilon)noexcept{
    if(!MatrixIsRigidAffine(matrix, affineEpsilon, rigidEpsilon))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle quaternion = QuaternionRotationMatrix(matrix);
    if(QuaternionIsNaN(quaternion) || QuaternionIsInfinite(quaternion))
        return MakeUnexpected(Failure{});
    return quaternion;
}

[[nodiscard]] NWB_INLINE Expected<SIMDVectorDouble> NWB_SIMD_CALL MatrixDoubleTryBuildRigidRotationQuaternion(const SIMDMatrixDouble& matrix, const f64 affineEpsilon, const f64 rigidEpsilon)noexcept{
    if(!MatrixDoubleIsRigidAffine(matrix, affineEpsilon, rigidEpsilon))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble quaternion = QuaternionDoubleRotationMatrix(matrix);
    if(QuaternionDoubleIsNaN(quaternion) || QuaternionDoubleIsInfinite(quaternion))
        return MakeUnexpected(Failure{});
    return quaternion;
}

[[nodiscard]] NWB_INLINE Expected<RigidDualQuaternion> NWB_SIMD_CALL MatrixTryBuildRigidDualQuaternion(const SIMDMatrix& matrix, const f32 affineEpsilon, const f32 rigidEpsilon)noexcept{
    const auto real = MatrixTryBuildRigidRotationQuaternion(matrix, affineEpsilon, rigidEpsilon);
    if(!real)
        return MakeUnexpected(real.error());

    const SIMDVectorSingle translation = VectorMergeX(VectorSplatW(matrix.v[0]), VectorSplatW(matrix.v[1]), VectorSplatW(matrix.v[2]), VectorZero());
    const SIMDVectorSingle dual = VectorScale(QuaternionMultiply(translation, *real), SIMDMatrixDetail::s_MatrixHalf);
    if(!VectorIsFinite(dual, VectorComponentMask::s_XYZW))
        return MakeUnexpected(Failure{});
    return RigidDualQuaternion{ *real, dual };
}

[[nodiscard]] NWB_INLINE Expected<RigidDualQuaternionDouble> NWB_SIMD_CALL MatrixDoubleTryBuildRigidDualQuaternion(const SIMDMatrixDouble& matrix, const f64 affineEpsilon, const f64 rigidEpsilon)noexcept{
    const auto real = MatrixDoubleTryBuildRigidRotationQuaternion(matrix, affineEpsilon, rigidEpsilon);
    if(!real)
        return MakeUnexpected(real.error());

    const SIMDVectorDouble translation = VectorDoubleMergeX(VectorDoubleSplatW(matrix.v[0]), VectorDoubleSplatW(matrix.v[1]), VectorDoubleSplatW(matrix.v[2]), VectorDoubleZero());
    const SIMDVectorDouble dual = VectorDoubleScale(QuaternionDoubleMultiply(translation, *real), SIMDMatrixDoubleDetail::s_DoubleMatrixHalf);
    if(!VectorDoubleIsFinite(dual, VectorDoubleComponentMask::s_DoubleXYZW))
        return MakeUnexpected(Failure{});
    return RigidDualQuaternionDouble{ *real, dual };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixIdentity()noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = s_SIMDIdentityR0;
    matrix.v[1] = s_SIMDIdentityR1;
    matrix.v[2] = s_SIMDIdentityR2;
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleIdentity()noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = s_DoubleSIMDIdentityR0;
    matrix.v[1] = s_DoubleSIMDIdentityR1;
    matrix.v[2] = s_DoubleSIMDIdentityR2;
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixSet(f32 m00, f32 m01, f32 m02, f32 m03, f32 m10, f32 m11, f32 m12, f32 m13, f32 m20, f32 m21, f32 m22, f32 m23, f32 m30, f32 m31, f32 m32, f32 m33)noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = VectorSet(m00, m01, m02, m03);
    matrix.v[1] = VectorSet(m10, m11, m12, m13);
    matrix.v[2] = VectorSet(m20, m21, m22, m23);
    matrix.v[3] = VectorSet(m30, m31, m32, m33);
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleSet(f64 m00, f64 m01, f64 m02, f64 m03, f64 m10, f64 m11, f64 m12, f64 m13, f64 m20, f64 m21, f64 m22, f64 m23, f64 m30, f64 m31, f64 m32, f64 m33)noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleSet(m00, m01, m02, m03);
    matrix.v[1] = VectorDoubleSet(m10, m11, m12, m13);
    matrix.v[2] = VectorDoubleSet(m20, m21, m22, m23);
    matrix.v[3] = VectorDoubleSet(m30, m31, m32, m33);
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixTranslation(f32 offsetX, f32 offsetY, f32 offsetZ)noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = VectorSet(1.0f, 0.0f, 0.0f, offsetX);
    matrix.v[1] = VectorSet(0.0f, 1.0f, 0.0f, offsetY);
    matrix.v[2] = VectorSet(0.0f, 0.0f, 1.0f, offsetZ);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleTranslation(f64 offsetX, f64 offsetY, f64 offsetZ)noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleSet(1.0, 0.0, 0.0, offsetX);
    matrix.v[1] = VectorDoubleSet(0.0, 1.0, 0.0, offsetY);
    matrix.v[2] = VectorDoubleSet(0.0, 0.0, 1.0, offsetZ);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixTranslationFromVector(SIMDVectorSingle offset)noexcept{
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

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleTranslationFromVector(SIMDVectorDouble offset)noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleSelect(s_DoubleSIMDIdentityR0, VectorDoubleSplatX(offset), s_DoubleSIMDMaskW);
    matrix.v[1] = VectorDoubleSelect(s_DoubleSIMDIdentityR1, VectorDoubleSplatY(offset), s_DoubleSIMDMaskW);
    matrix.v[2] = VectorDoubleSelect(s_DoubleSIMDIdentityR2, VectorDoubleSplatZ(offset), s_DoubleSIMDMaskW);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixScaling(f32 scaleX, f32 scaleY, f32 scaleZ)noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = VectorSet(scaleX, 0.0f, 0.0f, 0.0f);
    matrix.v[1] = VectorSet(0.0f, scaleY, 0.0f, 0.0f);
    matrix.v[2] = VectorSet(0.0f, 0.0f, scaleZ, 0.0f);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleScaling(f64 scaleX, f64 scaleY, f64 scaleZ)noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleSet(scaleX, 0.0, 0.0, 0.0);
    matrix.v[1] = VectorDoubleSet(0.0, scaleY, 0.0, 0.0);
    matrix.v[2] = VectorDoubleSet(0.0, 0.0, scaleZ, 0.0);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixScalingFromVector(SIMDVectorSingle scale)noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = VectorAndInt(scale, s_SIMDMaskX);
    matrix.v[1] = VectorAndInt(scale, s_SIMDMaskY);
    matrix.v[2] = VectorAndInt(scale, s_SIMDMaskZ);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleScalingFromVector(SIMDVectorDouble scale)noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleAndInt(scale, s_DoubleSIMDMaskX);
    matrix.v[1] = VectorDoubleAndInt(scale, s_DoubleSIMDMaskY);
    matrix.v[2] = VectorDoubleAndInt(scale, s_DoubleSIMDMaskZ);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixRotationX(f32 angle)noexcept{
    SIMDVectorSingle sinAngle{};
    SIMDVectorSingle cosAngle{};
    VectorSinCos(sinAngle, cosAngle, VectorReplicate(angle));

    SIMDMatrix matrix{};
    matrix.v[0] = s_SIMDIdentityR0;
    matrix.v[1] = VectorMergeX(VectorZero(), cosAngle, VectorNegate(sinAngle), VectorZero());
    matrix.v[2] = VectorMergeX(VectorZero(), sinAngle, cosAngle, VectorZero());
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleRotationX(f64 angle)noexcept{
    SIMDVectorDouble sinAngle{};
    SIMDVectorDouble cosAngle{};
    VectorDoubleSinCos(sinAngle, cosAngle, VectorDoubleReplicate(angle));

    SIMDMatrixDouble matrix{};
    matrix.v[0] = s_DoubleSIMDIdentityR0;
    matrix.v[1] = VectorDoubleMergeX(VectorDoubleZero(), cosAngle, VectorDoubleNegate(sinAngle), VectorDoubleZero());
    matrix.v[2] = VectorDoubleMergeX(VectorDoubleZero(), sinAngle, cosAngle, VectorDoubleZero());
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixRotationY(f32 angle)noexcept{
    SIMDVectorSingle sinAngle{};
    SIMDVectorSingle cosAngle{};
    VectorSinCos(sinAngle, cosAngle, VectorReplicate(angle));

    SIMDMatrix matrix{};
    matrix.v[0] = VectorMergeX(cosAngle, VectorZero(), sinAngle, VectorZero());
    matrix.v[1] = s_SIMDIdentityR1;
    matrix.v[2] = VectorMergeX(VectorNegate(sinAngle), VectorZero(), cosAngle, VectorZero());
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleRotationY(f64 angle)noexcept{
    SIMDVectorDouble sinAngle{};
    SIMDVectorDouble cosAngle{};
    VectorDoubleSinCos(sinAngle, cosAngle, VectorDoubleReplicate(angle));

    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleMergeX(cosAngle, VectorDoubleZero(), sinAngle, VectorDoubleZero());
    matrix.v[1] = s_DoubleSIMDIdentityR1;
    matrix.v[2] = VectorDoubleMergeX(VectorDoubleNegate(sinAngle), VectorDoubleZero(), cosAngle, VectorDoubleZero());
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixRotationZ(f32 angle)noexcept{
    SIMDVectorSingle sinAngle{};
    SIMDVectorSingle cosAngle{};
    VectorSinCos(sinAngle, cosAngle, VectorReplicate(angle));

    SIMDMatrix matrix{};
    matrix.v[0] = VectorMergeX(cosAngle, VectorNegate(sinAngle), VectorZero(), VectorZero());
    matrix.v[1] = VectorMergeX(sinAngle, cosAngle, VectorZero(), VectorZero());
    matrix.v[2] = s_SIMDIdentityR2;
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleRotationZ(f64 angle)noexcept{
    SIMDVectorDouble sinAngle{};
    SIMDVectorDouble cosAngle{};
    VectorDoubleSinCos(sinAngle, cosAngle, VectorDoubleReplicate(angle));

    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleMergeX(cosAngle, VectorDoubleNegate(sinAngle), VectorDoubleZero(), VectorDoubleZero());
    matrix.v[1] = VectorDoubleMergeX(sinAngle, cosAngle, VectorDoubleZero(), VectorDoubleZero());
    matrix.v[2] = s_DoubleSIMDIdentityR2;
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixRotationQuaternion(SIMDVectorSingle quaternion)noexcept{
    quaternion = QuaternionConjugate(quaternion);

    const SIMDVectorSingle q0 = VectorAdd(quaternion, quaternion);
    const SIMDVectorSingle q1 = VectorMultiply(quaternion, q0);

    SIMDVectorSingle v0 = VectorSwizzle<1, 0, 0, 3>(q1);
    v0 = VectorAndInt(v0, s_SIMDMask3);
    SIMDVectorSingle v1 = VectorSwizzle<2, 2, 1, 3>(q1);
    v1 = VectorAndInt(v1, s_SIMDMask3);
    SIMDVectorSingle r0 = VectorSubtract(s_SIMDOne3, v0);
    r0 = VectorSubtract(r0, v1);

    v0 = VectorSwizzle<0, 0, 1, 3>(quaternion);
    v1 = VectorSwizzle<2, 1, 2, 3>(q0);
    v0 = VectorMultiply(v0, v1);

    v1 = VectorSplatW(quaternion);
    const SIMDVectorSingle v2 = VectorSwizzle<1, 2, 0, 3>(q0);
    v1 = VectorMultiply(v1, v2);

    const SIMDVectorSingle r1 = VectorAdd(v0, v1);
    const SIMDVectorSingle r2 = VectorSubtract(v0, v1);

    v0 = VectorPermute<1, 4, 5, 2>(r1, r2);
    v1 = VectorPermute<0, 6, 0, 6>(r1, r2);

    SIMDMatrix matrix{};
    matrix.v[0] = VectorPermute<0, 4, 5, 3>(r0, v0);
    matrix.v[1] = VectorPermute<6, 1, 7, 3>(r0, v0);
    matrix.v[2] = VectorPermute<4, 5, 2, 3>(r0, v1);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleRotationQuaternion(SIMDVectorDouble quaternion)noexcept{
    quaternion = QuaternionDoubleConjugate(quaternion);

    const SIMDVectorDouble q0 = VectorDoubleAdd(quaternion, quaternion);
    const SIMDVectorDouble q1 = VectorDoubleMultiply(quaternion, q0);

    SIMDVectorDouble v0 = VectorDoubleSwizzle<1, 0, 0, 3>(q1);
    v0 = VectorDoubleAndInt(v0, s_DoubleSIMDMask3);
    SIMDVectorDouble v1 = VectorDoubleSwizzle<2, 2, 1, 3>(q1);
    v1 = VectorDoubleAndInt(v1, s_DoubleSIMDMask3);
    SIMDVectorDouble r0 = VectorDoubleSubtract(s_DoubleSIMDOne3, v0);
    r0 = VectorDoubleSubtract(r0, v1);

    v0 = VectorDoubleSwizzle<0, 0, 1, 3>(quaternion);
    v1 = VectorDoubleSwizzle<2, 1, 2, 3>(q0);
    v0 = VectorDoubleMultiply(v0, v1);

    v1 = VectorDoubleSplatW(quaternion);
    const SIMDVectorDouble v2 = VectorDoubleSwizzle<1, 2, 0, 3>(q0);
    v1 = VectorDoubleMultiply(v1, v2);

    const SIMDVectorDouble r1 = VectorDoubleAdd(v0, v1);
    const SIMDVectorDouble r2 = VectorDoubleSubtract(v0, v1);

    v0 = VectorDoublePermute<1, 4, 5, 2>(r1, r2);
    v1 = VectorDoublePermute<0, 6, 0, 6>(r1, r2);

    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoublePermute<0, 4, 5, 3>(r0, v0);
    matrix.v[1] = VectorDoublePermute<6, 1, 7, 3>(r0, v0);
    matrix.v[2] = VectorDoublePermute<4, 5, 2, 3>(r0, v1);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixRotationRollPitchYawFromVector(SIMDVectorSingle angles)noexcept{
    return MatrixRotationQuaternion(QuaternionRotationRollPitchYawFromVector(angles));
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleRotationRollPitchYawFromVector(SIMDVectorDouble angles)noexcept{
    return MatrixDoubleRotationQuaternion(QuaternionDoubleRotationRollPitchYawFromVector(angles));
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixRotationRollPitchYaw(f32 pitch, f32 yaw, f32 roll)noexcept{
    return MatrixRotationRollPitchYawFromVector(VectorSet(pitch, yaw, roll, 0.0f));
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleRotationRollPitchYaw(f64 pitch, f64 yaw, f64 roll)noexcept{
    return MatrixDoubleRotationRollPitchYawFromVector(VectorDoubleSet(pitch, yaw, roll, 0.0));
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixRotationNormal(SIMDVectorSingle normalAxis, f32 angle)noexcept{
    SIMDVectorSingle sinAngle{};
    SIMDVectorSingle cosAngle{};
    VectorSinCos(sinAngle, cosAngle, VectorReplicate(angle));
    const SIMDVectorSingle oneMinusCosAngle = VectorSubtract(s_SIMDOne, cosAngle);

#if defined(NWB_HAS_SCALAR) || defined(NWB_HAS_NEON)
    const SIMDVectorSingle a = VectorMergeX(VectorNegate(sinAngle), cosAngle, oneMinusCosAngle, VectorZero());
    const SIMDVectorSingle c2 = VectorSplatZ(a);
    const SIMDVectorSingle c1 = VectorSplatY(a);
    const SIMDVectorSingle c0 = VectorSplatX(a);

    const SIMDVectorSingle n0 = VectorSwizzle<1, 2, 0, 3>(normalAxis);
    const SIMDVectorSingle n1 = VectorSwizzle<2, 0, 1, 3>(normalAxis);

    SIMDVectorSingle v0 = VectorMultiply(c2, n0);
    v0 = VectorMultiply(v0, n1);

    SIMDVectorSingle r0 = VectorMultiply(c2, normalAxis);
    r0 = VectorMultiplyAdd(r0, normalAxis, c1);

    const SIMDVectorSingle s = VectorMultiply(c0, normalAxis);
    const SIMDVectorSingle r1 = VectorAdd(s, v0);
    const SIMDVectorSingle r2 = VectorSubtract(v0, s);

    v0 = VectorSelect(a, r0, s_SIMDMask3);
    const SIMDVectorSingle v1 = VectorPermute<2, 5, 6, 0>(r1, r2);
    const SIMDVectorSingle v2 = VectorPermute<1, 4, 1, 4>(r1, r2);

    SIMDMatrix matrix{};
    matrix.v[0] = VectorPermute<0, 4, 5, 3>(v0, v1);
    matrix.v[1] = VectorPermute<6, 1, 7, 3>(v0, v1);
    matrix.v[2] = VectorPermute<4, 5, 2, 3>(v0, v2);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
#else
    const SIMDVectorSingle c2 = oneMinusCosAngle;
    const SIMDVectorSingle c1 = cosAngle;
    const SIMDVectorSingle c0 = VectorNegate(sinAngle);

#if defined(NWB_HAS_AVX)
    const SIMDVectorSingle n0 = _mm_permute_ps(normalAxis, _MM_SHUFFLE(3, 0, 2, 1));
    const SIMDVectorSingle n1 = _mm_permute_ps(normalAxis, _MM_SHUFFLE(3, 1, 0, 2));
#else
    const SIMDVectorSingle n0 = _mm_shuffle_ps(normalAxis, normalAxis, _MM_SHUFFLE(3, 0, 2, 1));
    const SIMDVectorSingle n1 = _mm_shuffle_ps(normalAxis, normalAxis, _MM_SHUFFLE(3, 1, 0, 2));
#endif

    SIMDVectorSingle v0 = _mm_mul_ps(c2, n0);
    v0 = _mm_mul_ps(v0, n1);

    SIMDVectorSingle r0 = _mm_mul_ps(c2, normalAxis);
    r0 = VectorMultiplyAdd(r0, normalAxis, c1);

    SIMDVectorSingle r1 = VectorMultiplyAdd(c0, normalAxis, v0);
    SIMDVectorSingle r2 = VectorNegativeMultiplySubtract(c0, normalAxis, v0);

    v0 = _mm_and_ps(r0, s_SIMDMask3);
    SIMDVectorSingle v1 = _mm_shuffle_ps(r1, r2, _MM_SHUFFLE(2, 1, 2, 0));
#if defined(NWB_HAS_AVX)
    v1 = _mm_permute_ps(v1, _MM_SHUFFLE(0, 3, 2, 1));
#else
    v1 = _mm_shuffle_ps(v1, v1, _MM_SHUFFLE(0, 3, 2, 1));
#endif
    SIMDVectorSingle v2 = _mm_shuffle_ps(r1, r2, _MM_SHUFFLE(0, 0, 1, 1));
#if defined(NWB_HAS_AVX)
    v2 = _mm_permute_ps(v2, _MM_SHUFFLE(2, 0, 2, 0));
#else
    v2 = _mm_shuffle_ps(v2, v2, _MM_SHUFFLE(2, 0, 2, 0));
#endif

    r2 = _mm_shuffle_ps(v0, v1, _MM_SHUFFLE(1, 0, 3, 0));
#if defined(NWB_HAS_AVX)
    r2 = _mm_permute_ps(r2, _MM_SHUFFLE(1, 3, 2, 0));
#else
    r2 = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(1, 3, 2, 0));
#endif

    SIMDMatrix matrix{};
    matrix.v[0] = r2;

    r2 = _mm_shuffle_ps(v0, v1, _MM_SHUFFLE(3, 2, 3, 1));
#if defined(NWB_HAS_AVX)
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

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleRotationNormal(SIMDVectorDouble normalAxis, f64 angle)noexcept{
    SIMDVectorDouble sinAngle{};
    SIMDVectorDouble cosAngle{};
    VectorDoubleSinCos(sinAngle, cosAngle, VectorDoubleReplicate(angle));
    const SIMDVectorDouble oneMinusCosAngle = VectorDoubleSubtract(s_DoubleSIMDOne, cosAngle);

    const SIMDVectorDouble a = VectorDoubleMergeX(VectorDoubleNegate(sinAngle), cosAngle, oneMinusCosAngle, VectorDoubleZero());
    const SIMDVectorDouble c2 = VectorDoubleSplatZ(a);
    const SIMDVectorDouble c1 = VectorDoubleSplatY(a);
    const SIMDVectorDouble c0 = VectorDoubleSplatX(a);

    const SIMDVectorDouble n0 = VectorDoubleSwizzle<1, 2, 0, 3>(normalAxis);
    const SIMDVectorDouble n1 = VectorDoubleSwizzle<2, 0, 1, 3>(normalAxis);

    SIMDVectorDouble v0 = VectorDoubleMultiply(c2, n0);
    v0 = VectorDoubleMultiply(v0, n1);

    SIMDVectorDouble r0 = VectorDoubleMultiply(c2, normalAxis);
    r0 = VectorDoubleMultiplyAdd(r0, normalAxis, c1);

    const SIMDVectorDouble s = VectorDoubleMultiply(c0, normalAxis);
    const SIMDVectorDouble r1 = VectorDoubleAdd(s, v0);
    const SIMDVectorDouble r2 = VectorDoubleSubtract(v0, s);

    v0 = VectorDoubleSelect(a, r0, s_DoubleSIMDMask3);
    const SIMDVectorDouble v1 = VectorDoublePermute<2, 5, 6, 0>(r1, r2);
    const SIMDVectorDouble v2 = VectorDoublePermute<1, 4, 1, 4>(r1, r2);

    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoublePermute<0, 4, 5, 3>(v0, v1);
    matrix.v[1] = VectorDoublePermute<6, 1, 7, 3>(v0, v1);
    matrix.v[2] = VectorDoublePermute<4, 5, 2, 3>(v0, v2);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixRotationAxis(SIMDVectorSingle axis, f32 angle)noexcept{
    NWB_ASSERT(!Vector3Equal(axis, VectorZero()));
    NWB_ASSERT(!Vector3IsInfinite(axis));

    return MatrixRotationNormal(Vector3Normalize(axis), angle);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleRotationAxis(SIMDVectorDouble axis, f64 angle)noexcept{
    NWB_ASSERT(!VectorDouble3Equal(axis, VectorDoubleZero()));
    NWB_ASSERT(!VectorDouble3IsInfinite(axis));

    return MatrixDoubleRotationNormal(VectorDouble3Normalize(axis), angle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixAffineTransformation(SIMDVectorSingle scaling, SIMDVectorSingle rotationOrigin, SIMDVectorSingle rotationQuaternion, SIMDVectorSingle translation)noexcept{
    // T(translation) * T(origin) * R * T(-origin) * S folds to a rotation-scaled basis and one translation column.
    const SIMDVectorSingle origin = VectorSelect(VectorZero(), rotationOrigin, s_SIMDSelect1110);
    const SIMDMatrix rotation = MatrixRotationQuaternion(rotationQuaternion);
    const SIMDVectorSingle translationColumn = VectorAdd(translation, VectorSubtract(origin, Vector3TransformNormal(origin, rotation)));

    SIMDMatrix matrix;
    matrix.v[0] = VectorSelect(VectorMultiply(rotation.v[0], scaling), VectorSplatX(translationColumn), s_SIMDMaskW);
    matrix.v[1] = VectorSelect(VectorMultiply(rotation.v[1], scaling), VectorSplatY(translationColumn), s_SIMDMaskW);
    matrix.v[2] = VectorSelect(VectorMultiply(rotation.v[2], scaling), VectorSplatZ(translationColumn), s_SIMDMaskW);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleAffineTransformation(SIMDVectorDouble scaling, SIMDVectorDouble rotationOrigin, SIMDVectorDouble rotationQuaternion, SIMDVectorDouble translation)noexcept{
    // T(translation) * T(origin) * R * T(-origin) * S folds to a rotation-scaled basis and one translation column.
    const SIMDVectorDouble origin = VectorDoubleSelect(VectorDoubleZero(), rotationOrigin, s_DoubleSIMDSelect1110);
    const SIMDMatrixDouble rotation = MatrixDoubleRotationQuaternion(rotationQuaternion);
    const SIMDVectorDouble translationColumn = VectorDoubleAdd(translation, VectorDoubleSubtract(origin, VectorDouble3TransformNormal(origin, rotation)));

    SIMDMatrixDouble matrix;
    matrix.v[0] = VectorDoubleSelect(VectorDoubleMultiply(rotation.v[0], scaling), VectorDoubleSplatX(translationColumn), s_DoubleSIMDMaskW);
    matrix.v[1] = VectorDoubleSelect(VectorDoubleMultiply(rotation.v[1], scaling), VectorDoubleSplatY(translationColumn), s_DoubleSIMDMaskW);
    matrix.v[2] = VectorDoubleSelect(VectorDoubleMultiply(rotation.v[2], scaling), VectorDoubleSplatZ(translationColumn), s_DoubleSIMDMaskW);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixAffineTransformation2D(SIMDVectorSingle scaling, SIMDVectorSingle rotationOrigin, f32 rotation, SIMDVectorSingle translation)noexcept{
    const SIMDVectorSingle vScaling = VectorSelect(s_SIMDOne, scaling, s_SIMDSelect1100);
    const SIMDVectorSingle origin = VectorSelect(VectorZero(), rotationOrigin, s_SIMDSelect1100);
    const SIMDVectorSingle vTranslation = VectorSelect(VectorZero(), translation, s_SIMDSelect1100);
    const SIMDMatrix rotationMatrix = MatrixRotationZ(rotation);
    const SIMDVectorSingle translationColumn = VectorAdd(vTranslation, VectorSubtract(origin, Vector3TransformNormal(origin, rotationMatrix)));

    SIMDMatrix matrix;
    matrix.v[0] = VectorSelect(VectorMultiply(rotationMatrix.v[0], vScaling), VectorSplatX(translationColumn), s_SIMDMaskW);
    matrix.v[1] = VectorSelect(VectorMultiply(rotationMatrix.v[1], vScaling), VectorSplatY(translationColumn), s_SIMDMaskW);
    matrix.v[2] = VectorSelect(VectorMultiply(rotationMatrix.v[2], vScaling), VectorSplatZ(translationColumn), s_SIMDMaskW);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleAffineTransformation2D(SIMDVectorDouble scaling, SIMDVectorDouble rotationOrigin, f64 rotation, SIMDVectorDouble translation)noexcept{
    const SIMDVectorDouble vScaling = VectorDoubleSelect(s_DoubleSIMDOne, scaling, s_DoubleSIMDSelect1100);
    const SIMDVectorDouble origin = VectorDoubleSelect(VectorDoubleZero(), rotationOrigin, s_DoubleSIMDSelect1100);
    const SIMDVectorDouble vTranslation = VectorDoubleSelect(VectorDoubleZero(), translation, s_DoubleSIMDSelect1100);
    const SIMDMatrixDouble rotationMatrix = MatrixDoubleRotationZ(rotation);
    const SIMDVectorDouble translationColumn = VectorDoubleAdd(vTranslation, VectorDoubleSubtract(origin, VectorDouble3TransformNormal(origin, rotationMatrix)));

    SIMDMatrixDouble matrix;
    matrix.v[0] = VectorDoubleSelect(VectorDoubleMultiply(rotationMatrix.v[0], vScaling), VectorDoubleSplatX(translationColumn), s_DoubleSIMDMaskW);
    matrix.v[1] = VectorDoubleSelect(VectorDoubleMultiply(rotationMatrix.v[1], vScaling), VectorDoubleSplatY(translationColumn), s_DoubleSIMDMaskW);
    matrix.v[2] = VectorDoubleSelect(VectorDoubleMultiply(rotationMatrix.v[2], vScaling), VectorDoubleSplatZ(translationColumn), s_DoubleSIMDMaskW);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixTransformation(
    SIMDVectorSingle scalingOrigin, SIMDVectorSingle scalingOrientationQuaternion, SIMDVectorSingle scaling, SIMDVectorSingle rotationOrigin, SIMDVectorSingle rotationQuaternion, SIMDVectorSingle translation
)noexcept{
    const SIMDVectorSingle vScalingOrigin = VectorSelect(VectorZero(), scalingOrigin, s_SIMDSelect1110);
    const SIMDVectorSingle vRotationOrigin = VectorSelect(VectorZero(), rotationOrigin, s_SIMDSelect1110);

    // Oriented scaling Sso = Rso * diag(scaling) * Rso^T, then the linear part is Rr * Sso.
    // The five translation matrices of the original chain collapse into a single translation column.
    const SIMDMatrix scalingOrientation = MatrixRotationQuaternion(scalingOrientationQuaternion);
    SIMDMatrix scaledOrientation;
    scaledOrientation.v[0] = VectorMultiply(scalingOrientation.v[0], scaling);
    scaledOrientation.v[1] = VectorMultiply(scalingOrientation.v[1], scaling);
    scaledOrientation.v[2] = VectorMultiply(scalingOrientation.v[2], scaling);
    scaledOrientation.v[3] = s_SIMDIdentityR3;
    const SIMDMatrix orientedScaling = MatrixMultiply(scaledOrientation, MatrixTranspose(scalingOrientation));

    const SIMDMatrix rotation = MatrixRotationQuaternion(rotationQuaternion);
    const SIMDMatrix linear = MatrixMultiply(rotation, orientedScaling);

    const SIMDVectorSingle inner = VectorSubtract(VectorSubtract(vScalingOrigin, vRotationOrigin), Vector3TransformNormal(vScalingOrigin, orientedScaling));
    const SIMDVectorSingle translationColumn = VectorAdd(VectorAdd(Vector3TransformNormal(inner, rotation), vRotationOrigin), translation);

    SIMDMatrix matrix;
    matrix.v[0] = VectorSelect(linear.v[0], VectorSplatX(translationColumn), s_SIMDMaskW);
    matrix.v[1] = VectorSelect(linear.v[1], VectorSplatY(translationColumn), s_SIMDMaskW);
    matrix.v[2] = VectorSelect(linear.v[2], VectorSplatZ(translationColumn), s_SIMDMaskW);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleTransformation(
    SIMDVectorDouble scalingOrigin, SIMDVectorDouble scalingOrientationQuaternion, SIMDVectorDouble scaling, SIMDVectorDouble rotationOrigin, SIMDVectorDouble rotationQuaternion, SIMDVectorDouble translation
)noexcept{
    const SIMDVectorDouble vScalingOrigin = VectorDoubleSelect(VectorDoubleZero(), scalingOrigin, s_DoubleSIMDSelect1110);
    const SIMDVectorDouble vRotationOrigin = VectorDoubleSelect(VectorDoubleZero(), rotationOrigin, s_DoubleSIMDSelect1110);

    // Oriented scaling Sso = Rso * diag(scaling) * Rso^T, then the linear part is Rr * Sso.
    // The five translation matrices of the original chain collapse into a single translation column.
    const SIMDMatrixDouble scalingOrientation = MatrixDoubleRotationQuaternion(scalingOrientationQuaternion);
    SIMDMatrixDouble scaledOrientation;
    scaledOrientation.v[0] = VectorDoubleMultiply(scalingOrientation.v[0], scaling);
    scaledOrientation.v[1] = VectorDoubleMultiply(scalingOrientation.v[1], scaling);
    scaledOrientation.v[2] = VectorDoubleMultiply(scalingOrientation.v[2], scaling);
    scaledOrientation.v[3] = s_DoubleSIMDIdentityR3;
    const SIMDMatrixDouble orientedScaling = MatrixDoubleMultiply(scaledOrientation, MatrixDoubleTranspose(scalingOrientation));

    const SIMDMatrixDouble rotation = MatrixDoubleRotationQuaternion(rotationQuaternion);
    const SIMDMatrixDouble linear = MatrixDoubleMultiply(rotation, orientedScaling);

    const SIMDVectorDouble inner = VectorDoubleSubtract(VectorDoubleSubtract(vScalingOrigin, vRotationOrigin), VectorDouble3TransformNormal(vScalingOrigin, orientedScaling));
    const SIMDVectorDouble translationColumn = VectorDoubleAdd(VectorDoubleAdd(VectorDouble3TransformNormal(inner, rotation), vRotationOrigin), translation);

    SIMDMatrixDouble matrix;
    matrix.v[0] = VectorDoubleSelect(linear.v[0], VectorDoubleSplatX(translationColumn), s_DoubleSIMDMaskW);
    matrix.v[1] = VectorDoubleSelect(linear.v[1], VectorDoubleSplatY(translationColumn), s_DoubleSIMDMaskW);
    matrix.v[2] = VectorDoubleSelect(linear.v[2], VectorDoubleSplatZ(translationColumn), s_DoubleSIMDMaskW);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixTransformation2D(SIMDVectorSingle scalingOrigin, f32 scalingOrientation, SIMDVectorSingle scaling, SIMDVectorSingle rotationOrigin, f32 rotation, SIMDVectorSingle translation)noexcept{
    const SIMDVectorSingle vScalingOrigin = VectorSelect(VectorZero(), scalingOrigin, s_SIMDSelect1100);
    const SIMDVectorSingle vRotationOrigin = VectorSelect(VectorZero(), rotationOrigin, s_SIMDSelect1100);
    const SIMDVectorSingle vScaling = VectorSelect(s_SIMDOne, scaling, s_SIMDSelect1100);
    const SIMDVectorSingle vTranslation = VectorSelect(VectorZero(), translation, s_SIMDSelect1100);

    // Oriented Z scaling; translation matrices fold into one column.
    const SIMDMatrix scalingOrientationMatrix = MatrixRotationZ(scalingOrientation);
    SIMDMatrix scaledOrientation;
    scaledOrientation.v[0] = VectorMultiply(scalingOrientationMatrix.v[0], vScaling);
    scaledOrientation.v[1] = VectorMultiply(scalingOrientationMatrix.v[1], vScaling);
    scaledOrientation.v[2] = VectorMultiply(scalingOrientationMatrix.v[2], vScaling);
    scaledOrientation.v[3] = s_SIMDIdentityR3;
    const SIMDMatrix orientedScaling = MatrixMultiply(scaledOrientation, MatrixTranspose(scalingOrientationMatrix));

    const SIMDMatrix rotationMatrix = MatrixRotationZ(rotation);
    const SIMDMatrix linear = MatrixMultiply(rotationMatrix, orientedScaling);

    const SIMDVectorSingle inner = VectorSubtract(VectorSubtract(vScalingOrigin, vRotationOrigin), Vector3TransformNormal(vScalingOrigin, orientedScaling));
    const SIMDVectorSingle translationColumn = VectorAdd(VectorAdd(Vector3TransformNormal(inner, rotationMatrix), vRotationOrigin), vTranslation);

    SIMDMatrix matrix;
    matrix.v[0] = VectorSelect(linear.v[0], VectorSplatX(translationColumn), s_SIMDMaskW);
    matrix.v[1] = VectorSelect(linear.v[1], VectorSplatY(translationColumn), s_SIMDMaskW);
    matrix.v[2] = VectorSelect(linear.v[2], VectorSplatZ(translationColumn), s_SIMDMaskW);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleTransformation2D(SIMDVectorDouble scalingOrigin, f64 scalingOrientation, SIMDVectorDouble scaling, SIMDVectorDouble rotationOrigin, f64 rotation, SIMDVectorDouble translation)noexcept{
    const SIMDVectorDouble vScalingOrigin = VectorDoubleSelect(VectorDoubleZero(), scalingOrigin, s_DoubleSIMDSelect1100);
    const SIMDVectorDouble vRotationOrigin = VectorDoubleSelect(VectorDoubleZero(), rotationOrigin, s_DoubleSIMDSelect1100);
    const SIMDVectorDouble vScaling = VectorDoubleSelect(s_DoubleSIMDOne, scaling, s_DoubleSIMDSelect1100);
    const SIMDVectorDouble vTranslation = VectorDoubleSelect(VectorDoubleZero(), translation, s_DoubleSIMDSelect1100);

    // Oriented Z scaling; translation matrices fold into one column.
    const SIMDMatrixDouble scalingOrientationMatrix = MatrixDoubleRotationZ(scalingOrientation);
    SIMDMatrixDouble scaledOrientation;
    scaledOrientation.v[0] = VectorDoubleMultiply(scalingOrientationMatrix.v[0], vScaling);
    scaledOrientation.v[1] = VectorDoubleMultiply(scalingOrientationMatrix.v[1], vScaling);
    scaledOrientation.v[2] = VectorDoubleMultiply(scalingOrientationMatrix.v[2], vScaling);
    scaledOrientation.v[3] = s_DoubleSIMDIdentityR3;
    const SIMDMatrixDouble orientedScaling = MatrixDoubleMultiply(scaledOrientation, MatrixDoubleTranspose(scalingOrientationMatrix));

    const SIMDMatrixDouble rotationMatrix = MatrixDoubleRotationZ(rotation);
    const SIMDMatrixDouble linear = MatrixDoubleMultiply(rotationMatrix, orientedScaling);

    const SIMDVectorDouble inner = VectorDoubleSubtract(VectorDoubleSubtract(vScalingOrigin, vRotationOrigin), VectorDouble3TransformNormal(vScalingOrigin, orientedScaling));
    const SIMDVectorDouble translationColumn = VectorDoubleAdd(VectorDoubleAdd(VectorDouble3TransformNormal(inner, rotationMatrix), vRotationOrigin), vTranslation);

    SIMDMatrixDouble matrix;
    matrix.v[0] = VectorDoubleSelect(linear.v[0], VectorDoubleSplatX(translationColumn), s_DoubleSIMDMaskW);
    matrix.v[1] = VectorDoubleSelect(linear.v[1], VectorDoubleSplatY(translationColumn), s_DoubleSIMDMaskW);
    matrix.v[2] = VectorDoubleSelect(linear.v[2], VectorDoubleSplatZ(translationColumn), s_DoubleSIMDMaskW);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixReflect(SIMDVectorSingle reflectionPlane)noexcept{
    NWB_ASSERT(!Vector3Equal(reflectionPlane, VectorZero()));
    NWB_ASSERT(!Vector4IsInfinite(reflectionPlane));

    const SIMDVectorSingle plane = SIMDMatrixDetail::PlaneNormalize(reflectionPlane);
    const SIMDVectorSingle s = VectorMultiply(plane, SIMDMatrixDetail::s_SIMDMatrixNegativeTwo);
    const SIMDVectorSingle a = VectorSplatX(plane);
    const SIMDVectorSingle b = VectorSplatY(plane);
    const SIMDVectorSingle c = VectorSplatZ(plane);
    const SIMDVectorSingle d = VectorSplatW(plane);

    SIMDMatrix rowVectorMatrix{};
    rowVectorMatrix.v[0] = VectorMultiplyAdd(a, s, s_SIMDIdentityR0);
    rowVectorMatrix.v[1] = VectorMultiplyAdd(b, s, s_SIMDIdentityR1);
    rowVectorMatrix.v[2] = VectorMultiplyAdd(c, s, s_SIMDIdentityR2);
    rowVectorMatrix.v[3] = VectorMultiplyAdd(d, s, s_SIMDIdentityR3);
    return MatrixTranspose(rowVectorMatrix);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleReflect(SIMDVectorDouble reflectionPlane)noexcept{
    NWB_ASSERT(!VectorDouble3Equal(reflectionPlane, VectorDoubleZero()));
    NWB_ASSERT(!VectorDouble4IsInfinite(reflectionPlane));

    const SIMDVectorDouble plane = SIMDMatrixDoubleDetail::PlaneDoubleNormalize(reflectionPlane);
    const SIMDVectorDouble s = VectorDoubleMultiply(plane, SIMDMatrixDoubleDetail::s_DoubleSIMDMatrixNegativeTwo);
    const SIMDVectorDouble a = VectorDoubleSplatX(plane);
    const SIMDVectorDouble b = VectorDoubleSplatY(plane);
    const SIMDVectorDouble c = VectorDoubleSplatZ(plane);
    const SIMDVectorDouble d = VectorDoubleSplatW(plane);

    SIMDMatrixDouble rowVectorMatrix{};
    rowVectorMatrix.v[0] = VectorDoubleMultiplyAdd(a, s, s_DoubleSIMDIdentityR0);
    rowVectorMatrix.v[1] = VectorDoubleMultiplyAdd(b, s, s_DoubleSIMDIdentityR1);
    rowVectorMatrix.v[2] = VectorDoubleMultiplyAdd(c, s, s_DoubleSIMDIdentityR2);
    rowVectorMatrix.v[3] = VectorDoubleMultiplyAdd(d, s, s_DoubleSIMDIdentityR3);
    return MatrixDoubleTranspose(rowVectorMatrix);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixShadow(SIMDVectorSingle shadowPlane, SIMDVectorSingle lightPosition)noexcept{
    NWB_ASSERT(!Vector3Equal(shadowPlane, VectorZero()));
    NWB_ASSERT(!Vector4IsInfinite(shadowPlane));

    SIMDVectorSingle plane = SIMDMatrixDetail::PlaneNormalize(shadowPlane);
    SIMDVectorSingle dot = Vector4Dot(plane, lightPosition);
    plane = VectorNegate(plane);

    const SIMDVectorSingle d = VectorSplatW(plane);
    const SIMDVectorSingle c = VectorSplatZ(plane);
    const SIMDVectorSingle b = VectorSplatY(plane);
    const SIMDVectorSingle a = VectorSplatX(plane);
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

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleShadow(SIMDVectorDouble shadowPlane, SIMDVectorDouble lightPosition)noexcept{
    NWB_ASSERT(!VectorDouble3Equal(shadowPlane, VectorDoubleZero()));
    NWB_ASSERT(!VectorDouble4IsInfinite(shadowPlane));

    SIMDVectorDouble plane = SIMDMatrixDoubleDetail::PlaneDoubleNormalize(shadowPlane);
    SIMDVectorDouble dot = VectorDouble4Dot(plane, lightPosition);
    plane = VectorDoubleNegate(plane);

    const SIMDVectorDouble d = VectorDoubleSplatW(plane);
    const SIMDVectorDouble c = VectorDoubleSplatZ(plane);
    const SIMDVectorDouble b = VectorDoubleSplatY(plane);
    const SIMDVectorDouble a = VectorDoubleSplatX(plane);
    dot = VectorDoubleSelect(SIMDMatrixDoubleDetail::s_DoubleSIMDMatrixSelect0001, dot, SIMDMatrixDoubleDetail::s_DoubleSIMDMatrixSelect0001);

    SIMDMatrixDouble rowVectorMatrix{};
    rowVectorMatrix.v[3] = VectorDoubleMultiplyAdd(d, lightPosition, dot);
    dot = VectorDoubleRotateLeft(dot, 1);
    rowVectorMatrix.v[2] = VectorDoubleMultiplyAdd(c, lightPosition, dot);
    dot = VectorDoubleRotateLeft(dot, 1);
    rowVectorMatrix.v[1] = VectorDoubleMultiplyAdd(b, lightPosition, dot);
    dot = VectorDoubleRotateLeft(dot, 1);
    rowVectorMatrix.v[0] = VectorDoubleMultiplyAdd(a, lightPosition, dot);
    return MatrixDoubleTranspose(rowVectorMatrix);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixLookToLH(SIMDVectorSingle eyePosition, SIMDVectorSingle eyeDirection, SIMDVectorSingle upDirection)noexcept{
    NWB_ASSERT(!Vector3Equal(eyeDirection, VectorZero()));
    NWB_ASSERT(!Vector3IsInfinite(eyeDirection));
    NWB_ASSERT(!Vector3Equal(upDirection, VectorZero()));
    NWB_ASSERT(!Vector3IsInfinite(upDirection));

    const SIMDVectorSingle r2 = Vector3Normalize(eyeDirection);
    SIMDVectorSingle r0 = Vector3Cross(upDirection, r2);
    r0 = Vector3Normalize(r0);
    const SIMDVectorSingle r1 = Vector3Cross(r2, r0);

    const SIMDVectorSingle negEyePosition = VectorNegate(eyePosition);
    const SIMDVectorSingle d0 = Vector3Dot(r0, negEyePosition);
    const SIMDVectorSingle d1 = Vector3Dot(r1, negEyePosition);
    const SIMDVectorSingle d2 = Vector3Dot(r2, negEyePosition);

    SIMDMatrix matrix{};
    matrix.v[0] = VectorSelect(d0, r0, s_SIMDSelect1110);
    matrix.v[1] = VectorSelect(d1, r1, s_SIMDSelect1110);
    matrix.v[2] = VectorSelect(d2, r2, s_SIMDSelect1110);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleLookToLH(SIMDVectorDouble eyePosition, SIMDVectorDouble eyeDirection, SIMDVectorDouble upDirection)noexcept{
    NWB_ASSERT(!VectorDouble3Equal(eyeDirection, VectorDoubleZero()));
    NWB_ASSERT(!VectorDouble3IsInfinite(eyeDirection));
    NWB_ASSERT(!VectorDouble3Equal(upDirection, VectorDoubleZero()));
    NWB_ASSERT(!VectorDouble3IsInfinite(upDirection));

    const SIMDVectorDouble r2 = VectorDouble3Normalize(eyeDirection);
    SIMDVectorDouble r0 = VectorDouble3Cross(upDirection, r2);
    r0 = VectorDouble3Normalize(r0);
    const SIMDVectorDouble r1 = VectorDouble3Cross(r2, r0);

    const SIMDVectorDouble negEyePosition = VectorDoubleNegate(eyePosition);
    const SIMDVectorDouble d0 = VectorDouble3Dot(r0, negEyePosition);
    const SIMDVectorDouble d1 = VectorDouble3Dot(r1, negEyePosition);
    const SIMDVectorDouble d2 = VectorDouble3Dot(r2, negEyePosition);

    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleSelect(d0, r0, s_DoubleSIMDSelect1110);
    matrix.v[1] = VectorDoubleSelect(d1, r1, s_DoubleSIMDSelect1110);
    matrix.v[2] = VectorDoubleSelect(d2, r2, s_DoubleSIMDSelect1110);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixLookToRH(SIMDVectorSingle eyePosition, SIMDVectorSingle eyeDirection, SIMDVectorSingle upDirection)noexcept{
    return MatrixLookToLH(eyePosition, VectorNegate(eyeDirection), upDirection);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleLookToRH(SIMDVectorDouble eyePosition, SIMDVectorDouble eyeDirection, SIMDVectorDouble upDirection)noexcept{
    return MatrixDoubleLookToLH(eyePosition, VectorDoubleNegate(eyeDirection), upDirection);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixLookAtLH(SIMDVectorSingle eyePosition, SIMDVectorSingle focusPosition, SIMDVectorSingle upDirection)noexcept{
    return MatrixLookToLH(eyePosition, VectorSubtract(focusPosition, eyePosition), upDirection);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleLookAtLH(SIMDVectorDouble eyePosition, SIMDVectorDouble focusPosition, SIMDVectorDouble upDirection)noexcept{
    return MatrixDoubleLookToLH(eyePosition, VectorDoubleSubtract(focusPosition, eyePosition), upDirection);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixLookAtRH(SIMDVectorSingle eyePosition, SIMDVectorSingle focusPosition, SIMDVectorSingle upDirection)noexcept{
    return MatrixLookToLH(eyePosition, VectorSubtract(eyePosition, focusPosition), upDirection);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleLookAtRH(SIMDVectorDouble eyePosition, SIMDVectorDouble focusPosition, SIMDVectorDouble upDirection)noexcept{
    return MatrixDoubleLookToLH(eyePosition, VectorDoubleSubtract(eyePosition, focusPosition), upDirection);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixPerspectiveLH(f32 viewWidth, f32 viewHeight, f32 nearZ, f32 farZ)noexcept{
    return SIMDMatrixDetail::MatrixPerspectiveImpl(viewWidth, viewHeight, nearZ, farZ, VectorSubtract(VectorReplicate(farZ), VectorReplicate(nearZ)), -1.0f, 1.0f);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveLH(f64 viewWidth, f64 viewHeight, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveImpl(viewWidth, viewHeight, nearZ, farZ, VectorDoubleSubtract(VectorDoubleReplicate(farZ), VectorDoubleReplicate(nearZ)), -1.0, 1.0);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixPerspectiveRH(f32 viewWidth, f32 viewHeight, f32 nearZ, f32 farZ)noexcept{
    return SIMDMatrixDetail::MatrixPerspectiveImpl(viewWidth, viewHeight, nearZ, farZ, VectorSubtract(VectorReplicate(nearZ), VectorReplicate(farZ)), 1.0f, -1.0f);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveRH(f64 viewWidth, f64 viewHeight, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveImpl(viewWidth, viewHeight, nearZ, farZ, VectorDoubleSubtract(VectorDoubleReplicate(nearZ), VectorDoubleReplicate(farZ)), 1.0, -1.0);
}


NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixPerspectiveFovLH(f32 fovAngleY, f32 aspectRatio, f32 nearZ, f32 farZ)noexcept{
    return SIMDMatrixDetail::MatrixPerspectiveFovImpl(fovAngleY, aspectRatio, nearZ, farZ, VectorSubtract(VectorReplicate(farZ), VectorReplicate(nearZ)), -1.0f, 1.0f);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveFovLH(f64 fovAngleY, f64 aspectRatio, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveFovImpl(fovAngleY, aspectRatio, nearZ, farZ, VectorDoubleSubtract(VectorDoubleReplicate(farZ), VectorDoubleReplicate(nearZ)), -1.0, 1.0);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixPerspectiveFovRH(f32 fovAngleY, f32 aspectRatio, f32 nearZ, f32 farZ)noexcept{
    return SIMDMatrixDetail::MatrixPerspectiveFovImpl(fovAngleY, aspectRatio, nearZ, farZ, VectorSubtract(VectorReplicate(nearZ), VectorReplicate(farZ)), 1.0f, -1.0f);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveFovRH(f64 fovAngleY, f64 aspectRatio, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveFovImpl(fovAngleY, aspectRatio, nearZ, farZ, VectorDoubleSubtract(VectorDoubleReplicate(nearZ), VectorDoubleReplicate(farZ)), 1.0, -1.0);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixPerspectiveOffCenterLH(f32 viewLeft, f32 viewRight, f32 viewBottom, f32 viewTop, f32 nearZ, f32 farZ)noexcept{
    return SIMDMatrixDetail::MatrixPerspectiveOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, farZ, VectorSubtract(VectorReplicate(farZ), VectorReplicate(nearZ)), -1.0f, -1.0f, 1.0f);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveOffCenterLH(f64 viewLeft, f64 viewRight, f64 viewBottom, f64 viewTop, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, farZ, VectorDoubleSubtract(VectorDoubleReplicate(farZ), VectorDoubleReplicate(nearZ)), -1.0, -1.0, 1.0);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixPerspectiveOffCenterRH(f32 viewLeft, f32 viewRight, f32 viewBottom, f32 viewTop, f32 nearZ, f32 farZ)noexcept{
    return SIMDMatrixDetail::MatrixPerspectiveOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, farZ, VectorSubtract(VectorReplicate(nearZ), VectorReplicate(farZ)), 1.0f, 1.0f, -1.0f);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveOffCenterRH(f64 viewLeft, f64 viewRight, f64 viewBottom, f64 viewTop, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, farZ, VectorDoubleSubtract(VectorDoubleReplicate(nearZ), VectorDoubleReplicate(farZ)), 1.0, 1.0, -1.0);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixOrthographicLH(f32 viewWidth, f32 viewHeight, f32 nearZ, f32 farZ)noexcept{
    return SIMDMatrixDetail::MatrixOrthographicImpl(viewWidth, viewHeight, nearZ, VectorSubtract(VectorReplicate(farZ), VectorReplicate(nearZ)), -1.0f);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleOrthographicLH(f64 viewWidth, f64 viewHeight, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoubleOrthographicImpl(viewWidth, viewHeight, nearZ, VectorDoubleSubtract(VectorDoubleReplicate(farZ), VectorDoubleReplicate(nearZ)), -1.0);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixOrthographicRH(f32 viewWidth, f32 viewHeight, f32 nearZ, f32 farZ)noexcept{
    return SIMDMatrixDetail::MatrixOrthographicImpl(viewWidth, viewHeight, nearZ, VectorSubtract(VectorReplicate(nearZ), VectorReplicate(farZ)), 1.0f);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleOrthographicRH(f64 viewWidth, f64 viewHeight, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoubleOrthographicImpl(viewWidth, viewHeight, nearZ, VectorDoubleSubtract(VectorDoubleReplicate(nearZ), VectorDoubleReplicate(farZ)), 1.0);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixOrthographicOffCenterLH(f32 viewLeft, f32 viewRight, f32 viewBottom, f32 viewTop, f32 nearZ, f32 farZ)noexcept{
    return SIMDMatrixDetail::MatrixOrthographicOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, VectorSubtract(VectorReplicate(farZ), VectorReplicate(nearZ)), -1.0f);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleOrthographicOffCenterLH(f64 viewLeft, f64 viewRight, f64 viewBottom, f64 viewTop, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoubleOrthographicOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, VectorDoubleSubtract(VectorDoubleReplicate(farZ), VectorDoubleReplicate(nearZ)), -1.0);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixOrthographicOffCenterRH(f32 viewLeft, f32 viewRight, f32 viewBottom, f32 viewTop, f32 nearZ, f32 farZ)noexcept{
    return SIMDMatrixDetail::MatrixOrthographicOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, VectorSubtract(VectorReplicate(nearZ), VectorReplicate(farZ)), 1.0f);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleOrthographicOffCenterRH(f64 viewLeft, f64 viewRight, f64 viewBottom, f64 viewTop, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoubleOrthographicOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, VectorDoubleSubtract(VectorDoubleReplicate(nearZ), VectorDoubleReplicate(farZ)), 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

