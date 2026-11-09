// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix_arithmetic.h"
#include "quaternion.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDMatrix FOUNDATION_SIMD_CALL MatrixIdentity()noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = s_SIMDIdentityR0;
    matrix.v[1] = s_SIMDIdentityR1;
    matrix.v[2] = s_SIMDIdentityR2;
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

FOUNDATION_INLINE SIMDMatrixDouble FOUNDATION_SIMD_CALL MatrixDoubleIdentity()noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = s_DoubleSIMDIdentityR0;
    matrix.v[1] = s_DoubleSIMDIdentityR1;
    matrix.v[2] = s_DoubleSIMDIdentityR2;
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

FOUNDATION_INLINE SIMDMatrix FOUNDATION_SIMD_CALL MatrixSet(f32 m00, f32 m01, f32 m02, f32 m03, f32 m10, f32 m11, f32 m12, f32 m13, f32 m20, f32 m21, f32 m22, f32 m23, f32 m30, f32 m31, f32 m32, f32 m33)noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = VectorSet(m00, m01, m02, m03);
    matrix.v[1] = VectorSet(m10, m11, m12, m13);
    matrix.v[2] = VectorSet(m20, m21, m22, m23);
    matrix.v[3] = VectorSet(m30, m31, m32, m33);
    return matrix;
}

FOUNDATION_INLINE SIMDMatrixDouble FOUNDATION_SIMD_CALL MatrixDoubleSet(f64 m00, f64 m01, f64 m02, f64 m03, f64 m10, f64 m11, f64 m12, f64 m13, f64 m20, f64 m21, f64 m22, f64 m23, f64 m30, f64 m31, f64 m32, f64 m33)noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleSet(m00, m01, m02, m03);
    matrix.v[1] = VectorDoubleSet(m10, m11, m12, m13);
    matrix.v[2] = VectorDoubleSet(m20, m21, m22, m23);
    matrix.v[3] = VectorDoubleSet(m30, m31, m32, m33);
    return matrix;
}

FOUNDATION_INLINE SIMDMatrix FOUNDATION_SIMD_CALL MatrixTranslation(f32 offsetX, f32 offsetY, f32 offsetZ)noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = VectorSet(1.0f, 0.0f, 0.0f, offsetX);
    matrix.v[1] = VectorSet(0.0f, 1.0f, 0.0f, offsetY);
    matrix.v[2] = VectorSet(0.0f, 0.0f, 1.0f, offsetZ);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

FOUNDATION_INLINE SIMDMatrixDouble FOUNDATION_SIMD_CALL MatrixDoubleTranslation(f64 offsetX, f64 offsetY, f64 offsetZ)noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleSet(1.0, 0.0, 0.0, offsetX);
    matrix.v[1] = VectorDoubleSet(0.0, 1.0, 0.0, offsetY);
    matrix.v[2] = VectorDoubleSet(0.0, 0.0, 1.0, offsetZ);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

FOUNDATION_INLINE SIMDMatrixDouble FOUNDATION_SIMD_CALL MatrixDoubleTranslationFromVector(SIMDVectorDouble offset)noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleSelect(s_DoubleSIMDIdentityR0, VectorDoubleSplatX(offset), s_DoubleSIMDMaskW);
    matrix.v[1] = VectorDoubleSelect(s_DoubleSIMDIdentityR1, VectorDoubleSplatY(offset), s_DoubleSIMDMaskW);
    matrix.v[2] = VectorDoubleSelect(s_DoubleSIMDIdentityR2, VectorDoubleSplatZ(offset), s_DoubleSIMDMaskW);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

FOUNDATION_INLINE SIMDMatrix FOUNDATION_SIMD_CALL MatrixScaling(f32 scaleX, f32 scaleY, f32 scaleZ)noexcept{
    SIMDMatrix matrix{};
    matrix.v[0] = VectorSet(scaleX, 0.0f, 0.0f, 0.0f);
    matrix.v[1] = VectorSet(0.0f, scaleY, 0.0f, 0.0f);
    matrix.v[2] = VectorSet(0.0f, 0.0f, scaleZ, 0.0f);
    matrix.v[3] = s_SIMDIdentityR3;
    return matrix;
}

FOUNDATION_INLINE SIMDMatrixDouble FOUNDATION_SIMD_CALL MatrixDoubleScaling(f64 scaleX, f64 scaleY, f64 scaleZ)noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleSet(scaleX, 0.0, 0.0, 0.0);
    matrix.v[1] = VectorDoubleSet(0.0, scaleY, 0.0, 0.0);
    matrix.v[2] = VectorDoubleSet(0.0, 0.0, scaleZ, 0.0);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDMatrixDouble FOUNDATION_SIMD_CALL MatrixDoubleRotationX(f64 angle)noexcept{
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

FOUNDATION_INLINE SIMDMatrixDouble FOUNDATION_SIMD_CALL MatrixDoubleRotationY(f64 angle)noexcept{
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

FOUNDATION_INLINE SIMDMatrixDouble FOUNDATION_SIMD_CALL MatrixDoubleRotationZ(f64 angle)noexcept{
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

FOUNDATION_INLINE SIMDMatrix FOUNDATION_SIMD_CALL MatrixRotationQuaternion(SIMDVectorSingle quaternion)noexcept{
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

FOUNDATION_INLINE SIMDMatrixDouble FOUNDATION_SIMD_CALL MatrixDoubleRotationQuaternion(SIMDVectorDouble quaternion)noexcept{
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

FOUNDATION_INLINE SIMDMatrix FOUNDATION_SIMD_CALL MatrixRotationRollPitchYawFromVector(SIMDVectorSingle angles)noexcept{
    return MatrixRotationQuaternion(QuaternionRotationRollPitchYawFromVector(angles));
}

FOUNDATION_INLINE SIMDMatrix FOUNDATION_SIMD_CALL MatrixRotationRollPitchYaw(f32 pitch, f32 yaw, f32 roll)noexcept{
    return MatrixRotationRollPitchYawFromVector(VectorSet(pitch, yaw, roll, 0.0f));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDMatrix FOUNDATION_SIMD_CALL MatrixAffineTransformation(SIMDVectorSingle scaling, SIMDVectorSingle rotationOrigin, SIMDVectorSingle rotationQuaternion, SIMDVectorSingle translation)noexcept{
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

FOUNDATION_INLINE SIMDMatrixDouble FOUNDATION_SIMD_CALL MatrixDoubleAffineTransformation(SIMDVectorDouble scaling, SIMDVectorDouble rotationOrigin, SIMDVectorDouble rotationQuaternion, SIMDVectorDouble translation)noexcept{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

