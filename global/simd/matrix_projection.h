// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix_arithmetic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_MatrixTwo = 2.0f;

inline constexpr f32 s_MatrixProjectionNearEqualEpsilon = 0.00001f;
inline constexpr f32 s_MatrixPerspectiveFovNearEqualEpsilon = s_MatrixProjectionNearEqualEpsilon * s_MatrixTwo;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T> requires(IsSame_V<T, f32> || IsSame_V<T, f64>)
NWB_INLINE b8 ScalarNearEqual(T value0, T value1, T epsilon)noexcept{
    return Abs(value0 - value1) <= epsilon;
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

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixOrthographicOffCenterImpl(const f32 viewLeft, const f32 viewRight, const f32 viewBottom, const f32 viewTop, const f32 nearZ, const f32 rangeDenominator, const f32 rangeNearScale)noexcept{
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewRight, viewLeft, s_MatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(viewTop, viewBottom, s_MatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDetail::ScalarNearEqual(rangeDenominator, 0.0f, s_MatrixProjectionNearEqualEpsilon));

    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle left = VectorReplicate(viewLeft);
    const SIMDVectorSingle right = VectorReplicate(viewRight);
    const SIMDVectorSingle bottom = VectorReplicate(viewBottom);
    const SIMDVectorSingle top = VectorReplicate(viewTop);
    const SIMDVectorSingle nearZVector = VectorReplicate(nearZ);
    const SIMDVectorSingle reciprocalWidth = VectorReciprocal(VectorSubtract(right, left));
    const SIMDVectorSingle reciprocalHeight = VectorReciprocal(VectorSubtract(top, bottom));
    const SIMDVectorSingle range = VectorReciprocal(VectorReplicate(rangeDenominator));
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


inline constexpr f64 s_DoubleMatrixTwo = 2.0;

inline constexpr f64 s_DoubleMatrixProjectionNearEqualEpsilon = 1.0e-12;
inline constexpr f64 s_DoubleMatrixPerspectiveFovNearEqualEpsilon = s_DoubleMatrixProjectionNearEqualEpsilon * s_DoubleMatrixTwo;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixPerspectiveFovLH(f32 fovAngleY, f32 aspectRatio, f32 nearZ, f32 farZ)noexcept{
    return SIMDMatrixDetail::MatrixPerspectiveFovImpl(fovAngleY, aspectRatio, nearZ, farZ, VectorSubtract(VectorReplicate(farZ), VectorReplicate(nearZ)), -1.0f, 1.0f);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveFovLH(f64 fovAngleY, f64 aspectRatio, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveFovImpl(fovAngleY, aspectRatio, nearZ, farZ, VectorDoubleSubtract(VectorDoubleReplicate(farZ), VectorDoubleReplicate(nearZ)), -1.0, 1.0);
}

NWB_INLINE SIMDMatrix NWB_SIMD_CALL MatrixOrthographicOffCenterLH(f32 viewLeft, f32 viewRight, f32 viewBottom, f32 viewTop, f32 nearZ, f32 farZ)noexcept{
    return SIMDMatrixDetail::MatrixOrthographicOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, farZ - nearZ, -1.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

