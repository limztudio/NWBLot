// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix_arithmetic.h"
#include "quaternion.h"
#include "result.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_MatrixDecomposeEpsilon = 0.0001f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f64 s_DoubleMatrixDecomposeEpsilon = 1.0e-12;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(SIMDVectorSingle) MatrixDecomposition{
    SIMDVectorSingle scale;
    SIMDVectorSingle rotation;
    SIMDVectorSingle translation;
};
static_assert(alignof(Expected<MatrixDecomposition>) >= alignof(MatrixDecomposition));

struct alignas(SIMDVectorDouble) MatrixDoubleDecomposition{
    SIMDVectorDouble scale;
    SIMDVectorDouble rotation;
    SIMDVectorDouble translation;
};
static_assert(alignof(Expected<MatrixDoubleDecomposition>) >= alignof(MatrixDoubleDecomposition));

struct alignas(SIMDVectorSingle) RigidDualQuaternion{
    SIMDVectorSingle real;
    SIMDVectorSingle dual;
};
static_assert(alignof(Expected<RigidDualQuaternion>) >= alignof(RigidDualQuaternion));

struct alignas(SIMDVectorDouble) RigidDualQuaternionDouble{
    SIMDVectorDouble real;
    SIMDVectorDouble dual;
};
static_assert(alignof(Expected<RigidDualQuaternionDouble>) >= alignof(RigidDualQuaternionDouble));


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

[[nodiscard]] NWB_INLINE Expected<SIMDVectorSingleResult> NWB_SIMD_CALL MatrixTryBuildRigidRotationQuaternion(const SIMDMatrix& matrix, const f32 affineEpsilon, const f32 rigidEpsilon)noexcept{
    if(!MatrixIsRigidAffine(matrix, affineEpsilon, rigidEpsilon))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle quaternion = QuaternionRotationMatrix(matrix);
    if(QuaternionIsNaN(quaternion) || QuaternionIsInfinite(quaternion))
        return MakeUnexpected(Failure{});
    return SIMDVectorSingleResult{ quaternion };
}

[[nodiscard]] NWB_INLINE Expected<SIMDVectorDoubleResult> NWB_SIMD_CALL MatrixDoubleTryBuildRigidRotationQuaternion(const SIMDMatrixDouble& matrix, const f64 affineEpsilon, const f64 rigidEpsilon)noexcept{
    if(!MatrixDoubleIsRigidAffine(matrix, affineEpsilon, rigidEpsilon))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble quaternion = QuaternionDoubleRotationMatrix(matrix);
    if(QuaternionDoubleIsNaN(quaternion) || QuaternionDoubleIsInfinite(quaternion))
        return MakeUnexpected(Failure{});
    return SIMDVectorDoubleResult{ quaternion };
}

[[nodiscard]] NWB_INLINE Expected<RigidDualQuaternion> NWB_SIMD_CALL MatrixTryBuildRigidDualQuaternion(const SIMDMatrix& matrix, const f32 affineEpsilon, const f32 rigidEpsilon)noexcept{
    const auto real = MatrixTryBuildRigidRotationQuaternion(matrix, affineEpsilon, rigidEpsilon);
    if(!real)
        return MakeUnexpected(real.error());

    const SIMDVectorSingle translation = VectorMergeX(VectorSplatW(matrix.v[0]), VectorSplatW(matrix.v[1]), VectorSplatW(matrix.v[2]), VectorZero());
    const SIMDVectorSingle dual = VectorScale(QuaternionMultiply(translation, real->value), SIMDMatrixDetail::s_MatrixHalf);
    if(!VectorIsFinite(dual, VectorComponentMask::s_XYZW))
        return MakeUnexpected(Failure{});
    return RigidDualQuaternion{ real->value, dual };
}

[[nodiscard]] NWB_INLINE Expected<RigidDualQuaternionDouble> NWB_SIMD_CALL MatrixDoubleTryBuildRigidDualQuaternion(const SIMDMatrixDouble& matrix, const f64 affineEpsilon, const f64 rigidEpsilon)noexcept{
    const auto real = MatrixDoubleTryBuildRigidRotationQuaternion(matrix, affineEpsilon, rigidEpsilon);
    if(!real)
        return MakeUnexpected(real.error());

    const SIMDVectorDouble translation = VectorDoubleMergeX(VectorDoubleSplatW(matrix.v[0]), VectorDoubleSplatW(matrix.v[1]), VectorDoubleSplatW(matrix.v[2]), VectorDoubleZero());
    const SIMDVectorDouble dual = VectorDoubleScale(QuaternionDoubleMultiply(translation, real->value), SIMDMatrixDoubleDetail::s_DoubleMatrixHalf);
    if(!VectorDoubleIsFinite(dual, VectorDoubleComponentMask::s_DoubleXYZW))
        return MakeUnexpected(Failure{});
    return RigidDualQuaternionDouble{ real->value, dual };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

