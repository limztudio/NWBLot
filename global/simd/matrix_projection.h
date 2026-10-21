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
NWB_INLINE bool ScalarNearEqual(T value0, T value1, T epsilon)noexcept{
    if constexpr(IsSame_V<T, f32>)
        return (VectorMoveMask(VectorNearEqual(VectorReplicate(value0), VectorReplicate(value1), VectorReplicate(epsilon))) & 1u) != 0u;
    else
        return (VectorDoubleMoveMask(VectorDoubleNearEqual(VectorDoubleReplicate(value0), VectorDoubleReplicate(value1), VectorDoubleReplicate(epsilon))) & 1u) != 0u;
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


inline constexpr f64 s_DoubleMatrixTwo = 2.0;

inline constexpr f64 s_DoubleMatrixProjectionNearEqualEpsilon = 1.0e-12;
inline constexpr f64 s_DoubleMatrixPerspectiveFovNearEqualEpsilon = s_DoubleMatrixProjectionNearEqualEpsilon * s_DoubleMatrixTwo;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

