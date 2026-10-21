// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix_arithmetic.h"
#include "quaternion.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorConstF s_SIMDMatrixNegativeTwo = { { { -2.0f, -2.0f, -2.0f, 0.0f } } };
static const SIMDVectorConstU s_SIMDMatrixSelect0001 = { { { s_SELECT_0, s_SELECT_0, s_SELECT_0, s_SELECT_1 } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorDoubleConstF s_DoubleSIMDMatrixNegativeTwo = { { { -2.0, -2.0, -2.0, 0.0 } } };
static const SIMDVectorDoubleConstU s_DoubleSIMDMatrixSelect0001 = { { { s_DoubleSELECT_0, s_DoubleSELECT_0, s_DoubleSELECT_0, s_DoubleSELECT_1 } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL PlaneDoubleNormalize(SIMDVectorDouble plane)noexcept{
    return VectorDoubleDivide(plane, VectorDouble3Length(plane));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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

