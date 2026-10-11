// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "quaternion_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorDoubleConstF s_DoubleSIMDMatrixSign = { { { 1.0, -1.0, 1.0, -1.0 } } };
static const SIMDVectorDoubleConstF s_DoubleSIMDMatrixNegativeTwo = { { { -2.0, -2.0, -2.0, 0.0 } } };
static const SIMDVectorDoubleConstU s_DoubleSIMDMatrixSelect0001 = { { { s_DoubleSELECT_0, s_DoubleSELECT_0, s_DoubleSELECT_0, s_DoubleSELECT_1 } } };
inline constexpr f64 s_DoubleMatrixHalf = 0.5;
inline constexpr f64 s_DoubleMatrixTwo = 2.0;
inline constexpr f64 s_DoubleMatrixDecomposeEpsilon = 1.0e-12;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool ScalarNearEqual(f64 value0, f64 value1, f64 epsilon)noexcept{ return Abs(value0 - value1) <= epsilon; }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL MatrixDoubleRowMultiply(SIMDVectorDouble row, const SIMDMatrixDouble& matrix)noexcept{
    SIMDVectorDouble result = VectorDoubleMultiply(VectorDoubleSplatX(row), matrix.v[0]);
    result = VectorDoubleMultiplyAdd(VectorDoubleSplatY(row), matrix.v[1], result);
    result = VectorDoubleMultiplyAdd(VectorDoubleSplatZ(row), matrix.v[2], result);
    return VectorDoubleMultiplyAdd(VectorDoubleSplatW(row), matrix.v[3], result);
}


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL PlaneDoubleNormalize(SIMDVectorDouble plane)noexcept{
    return VectorDoubleDivide(plane, VectorDouble3Length(plane));
}

NWB_INLINE void RankDecompose(usize& a, usize& b, usize& c, f64 x, f64 y, f64 z)noexcept{
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


NWB_INLINE bool NWB_SIMD_CALL MatrixDoubleIsNaN(const SIMDMatrixDouble& matrix)noexcept{
    SIMDVectorDouble mask = VectorDoubleOrInt(VectorDoubleIsNaN(matrix.v[0]), VectorDoubleIsNaN(matrix.v[1]));
    mask = VectorDoubleOrInt(mask, VectorDoubleIsNaN(matrix.v[2]));
    mask = VectorDoubleOrInt(mask, VectorDoubleIsNaN(matrix.v[3]));
    return VectorDoubleMoveMask(mask) != 0;
}

NWB_INLINE bool NWB_SIMD_CALL MatrixDoubleIsInfinite(const SIMDMatrixDouble& matrix)noexcept{
    SIMDVectorDouble mask = VectorDoubleOrInt(VectorDoubleIsInfinite(matrix.v[0]), VectorDoubleIsInfinite(matrix.v[1]));
    mask = VectorDoubleOrInt(mask, VectorDoubleIsInfinite(matrix.v[2]));
    mask = VectorDoubleOrInt(mask, VectorDoubleIsInfinite(matrix.v[3]));
    return VectorDoubleMoveMask(mask) != 0;
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


NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleTranspose(const SIMDMatrixDouble& matrix)noexcept{
    return SIMDVectorDoubleDetail::MatrixDoubleTranspose4(matrix.v[0], matrix.v[1], matrix.v[2], matrix.v[3]);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleMultiply(const SIMDMatrixDouble& m0, const SIMDMatrixDouble& m1)noexcept{
    SIMDMatrixDouble result{};
    result.v[0] = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[0], m1);
    result.v[1] = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[1], m1);
    result.v[2] = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[2], m1);
    result.v[3] = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[3], m1);
    return result;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleMultiplyTranspose(const SIMDMatrixDouble& m0, const SIMDMatrixDouble& m1)noexcept{
    const SIMDVectorDouble r0 = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[0], m1);
    const SIMDVectorDouble r1 = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[1], m1);
    const SIMDVectorDouble r2 = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[2], m1);
    const SIMDVectorDouble r3 = SIMDMatrixDoubleDetail::MatrixDoubleRowMultiply(m0.v[3], m1);
    return SIMDVectorDoubleDetail::MatrixDoubleTranspose4(r0, r1, r2, r3);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleVectorTensorProduct(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    SIMDMatrixDouble result{};
    result.v[0] = VectorDoubleMultiply(VectorDoubleSplatX(v0), v1);
    result.v[1] = VectorDoubleMultiply(VectorDoubleSplatY(v0), v1);
    result.v[2] = VectorDoubleMultiply(VectorDoubleSplatZ(v0), v1);
    result.v[3] = VectorDoubleMultiply(VectorDoubleSplatW(v0), v1);
    return result;
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

struct MatrixDoubleDecomposition{
    SIMDVectorDouble scale;
    SIMDVectorDouble rotation;
    SIMDVectorDouble translation;
};

[[nodiscard]] NWB_INLINE Expected<MatrixDoubleDecomposition> NWB_SIMD_CALL MatrixDoubleDecompose(const SIMDMatrixDouble& matrix)noexcept{
    const SIMDVectorDouble canonicalBasis[3] = {
        s_DoubleSIMDIdentityR0,
        s_DoubleSIMDIdentityR1,
        s_DoubleSIMDIdentityR2
    };

    SIMDMatrixDouble transposed = MatrixDoubleTranspose(matrix);

    SIMDVectorDouble basis[3] = {
        VectorDoubleAndInt(transposed.v[0], s_DoubleSIMDMask3),
        VectorDoubleAndInt(transposed.v[1], s_DoubleSIMDMask3),
        VectorDoubleAndInt(transposed.v[2], s_DoubleSIMDMask3)
    };

    SIMDVectorDouble scale = VectorDoubleMergeX(
        VectorDouble3Length(basis[0]),
        VectorDouble3Length(basis[1]),
        VectorDouble3Length(basis[2]),
        VectorDoubleZero()
    );

    usize a{};
    usize b{};
    usize c{};
    SIMDMatrixDoubleDetail::RankDecompose(a, b, c, VectorDoubleGetX(scale), VectorDoubleGetY(scale), VectorDoubleGetZ(scale));

    if(VectorDoubleGetByIndex(scale, a) < SIMDMatrixDoubleDetail::s_DoubleMatrixDecomposeEpsilon)
        basis[a] = canonicalBasis[a];
    basis[a] = VectorDouble3Normalize(basis[a]);

    if(VectorDoubleGetByIndex(scale, b) < SIMDMatrixDoubleDetail::s_DoubleMatrixDecomposeEpsilon){
        usize aa{};
        usize bb{};
        usize cc{};
        const SIMDVectorDouble absoluteBasis = VectorDoubleAbs(basis[a]);
        SIMDMatrixDoubleDetail::RankDecompose(
            aa,
            bb,
            cc,
            VectorDoubleGetX(absoluteBasis),
            VectorDoubleGetY(absoluteBasis),
            VectorDoubleGetZ(absoluteBasis)
        );
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
        scale = VectorDoubleSetByIndex(scale, -VectorDoubleGetByIndex(scale, a), a);
        basis[a] = VectorDoubleNegate(basis[a]);

        basisAsRows.v[0] = basis[0];
        basisAsRows.v[1] = basis[1];
        basisAsRows.v[2] = basis[2];
        rotationMatrix = MatrixDoubleTranspose(basisAsRows);
        determinant = VectorDoubleNegate(determinant);
    }

    const SIMDVectorDouble determinantError = VectorDoubleSubtract(determinant, s_DoubleSIMDOne);
    if(VectorDouble4Greater(
        VectorDoubleMultiply(determinantError, determinantError),
        VectorDoubleReplicate(SIMDMatrixDoubleDetail::s_DoubleMatrixDecomposeEpsilon)
    ))
        return MakeUnexpected(Failure{});

    return MatrixDoubleDecomposition{ scale, QuaternionDoubleRotationMatrix(rotationMatrix), transposed.v[3] };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL MatrixDoubleIsAffine(const SIMDMatrixDouble& matrix, const f64 epsilon)noexcept{
    return
        VectorDoubleIsFinite(matrix.v[0], VectorDoubleComponentMask::s_DoubleXYZW)
        && VectorDoubleIsFinite(matrix.v[1], VectorDoubleComponentMask::s_DoubleXYZW)
        && VectorDoubleIsFinite(matrix.v[2], VectorDoubleComponentMask::s_DoubleXYZW)
        && VectorDoubleIsFinite(matrix.v[3], VectorDoubleComponentMask::s_DoubleXYZW)
        && VectorDouble4NearEqual(matrix.v[3], s_DoubleSIMDIdentityR3, VectorDoubleReplicate(epsilon))
    ;
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL MatrixDoubleLinearDeterminantV(const SIMDMatrixDouble& matrix)noexcept{
    const SIMDVectorDouble row0 = VectorDoubleSetW(matrix.v[0], 0.0);
    const SIMDVectorDouble row1 = VectorDoubleSetW(matrix.v[1], 0.0);
    const SIMDVectorDouble row2 = VectorDoubleSetW(matrix.v[2], 0.0);
    return VectorDouble3Dot(row0, VectorDouble3Cross(row1, row2));
}

[[nodiscard]] NWB_INLINE f64 NWB_SIMD_CALL MatrixDoubleLinearDeterminant(const SIMDMatrixDouble& matrix)noexcept{
    return VectorDoubleGetX(MatrixDoubleLinearDeterminantV(matrix));
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL MatrixDoubleIsInvertibleAffine(
    const SIMDMatrixDouble& matrix,
    const f64 affineEpsilon,
    const f64 determinantEpsilon
)noexcept{
    if(!MatrixDoubleIsAffine(matrix, affineEpsilon))
        return false;

    const SIMDVectorDouble determinant = MatrixDoubleLinearDeterminantV(matrix);
    return VectorDoubleIsFinite(determinant, VectorDoubleComponentMask::s_DoubleXYZW)
        && VectorDouble4Greater(VectorDoubleAbs(determinant), VectorDoubleReplicate(determinantEpsilon));
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL MatrixDoubleIsRigidAffine(
    const SIMDMatrixDouble& matrix,
    const f64 affineEpsilon,
    const f64 rigidEpsilon
)noexcept{
    if(!MatrixDoubleIsAffine(matrix, affineEpsilon))
        return false;

    const SIMDVectorDouble row0 = VectorDoubleSetW(matrix.v[0], 0.0);
    const SIMDVectorDouble row1 = VectorDoubleSetW(matrix.v[1], 0.0);
    const SIMDVectorDouble row2 = VectorDoubleSetW(matrix.v[2], 0.0);
    const SIMDVectorDouble lengthSq = VectorDoubleMergeX(
        VectorDouble3LengthSq(row0),
        VectorDouble3LengthSq(row1),
        VectorDouble3LengthSq(row2),
        s_DoubleSIMDOne
    );
    const SIMDVectorDouble dotAndDeterminant = VectorDoubleMergeX(
        VectorDouble3Dot(row0, row1),
        VectorDouble3Dot(row0, row2),
        VectorDouble3Dot(row1, row2),
        VectorDouble3Dot(row0, VectorDouble3Cross(row1, row2))
    );
    const SIMDVectorDouble epsilon = VectorDoubleReplicate(rigidEpsilon);
    return
        VectorDoubleIsFinite(lengthSq, VectorDoubleComponentMask::s_DoubleXYZ)
        && VectorDoubleIsFinite(dotAndDeterminant, VectorDoubleComponentMask::s_DoubleXYZW)
        && VectorDouble4LessOrEqual(VectorDoubleAbs(VectorDoubleSubtract(lengthSq, s_DoubleSIMDOne)), epsilon)
        && VectorDouble4LessOrEqual(VectorDoubleAbs(VectorDoubleSubtract(dotAndDeterminant, s_DoubleSIMDIdentityR3)), epsilon)
    ;
}

[[nodiscard]] NWB_INLINE Expected<SIMDVectorDouble> NWB_SIMD_CALL MatrixDoubleTryBuildRigidRotationQuaternion(
    const SIMDMatrixDouble& matrix,
    const f64 affineEpsilon,
    const f64 rigidEpsilon
)noexcept{
    if(!MatrixDoubleIsRigidAffine(matrix, affineEpsilon, rigidEpsilon))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble quaternion = QuaternionDoubleRotationMatrix(matrix);
    if(QuaternionDoubleIsNaN(quaternion) || QuaternionDoubleIsInfinite(quaternion))
        return MakeUnexpected(Failure{});
    return quaternion;
}

struct RigidDualQuaternionDouble{
    SIMDVectorDouble real;
    SIMDVectorDouble dual;
};

[[nodiscard]] NWB_INLINE Expected<RigidDualQuaternionDouble> NWB_SIMD_CALL MatrixDoubleTryBuildRigidDualQuaternion(
    const SIMDMatrixDouble& matrix,
    const f64 affineEpsilon,
    const f64 rigidEpsilon
)noexcept{
    const auto real = MatrixDoubleTryBuildRigidRotationQuaternion(matrix, affineEpsilon, rigidEpsilon);
    if(!real)
        return MakeUnexpected(real.error());

    const SIMDVectorDouble translation = VectorDoubleMergeX(
        VectorDoubleSplatW(matrix.v[0]),
        VectorDoubleSplatW(matrix.v[1]),
        VectorDoubleSplatW(matrix.v[2]),
        VectorDoubleZero()
    );
    const SIMDVectorDouble dual = VectorDoubleScale(QuaternionDoubleMultiply(translation, *real), SIMDMatrixDoubleDetail::s_DoubleMatrixHalf);
    if(!VectorDoubleIsFinite(dual, VectorDoubleComponentMask::s_DoubleXYZW))
        return MakeUnexpected(Failure{});
    return RigidDualQuaternionDouble{ *real, dual };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleIdentity()noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = s_DoubleSIMDIdentityR0;
    matrix.v[1] = s_DoubleSIMDIdentityR1;
    matrix.v[2] = s_DoubleSIMDIdentityR2;
    matrix.v[3] = s_DoubleSIMDIdentityR3;
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

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleTranslation(f64 offsetX, f64 offsetY, f64 offsetZ)noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleSet(1.0, 0.0, 0.0, offsetX);
    matrix.v[1] = VectorDoubleSet(0.0, 1.0, 0.0, offsetY);
    matrix.v[2] = VectorDoubleSet(0.0, 0.0, 1.0, offsetZ);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
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

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleScaling(f64 scaleX, f64 scaleY, f64 scaleZ)noexcept{
    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleSet(scaleX, 0.0, 0.0, 0.0);
    matrix.v[1] = VectorDoubleSet(0.0, scaleY, 0.0, 0.0);
    matrix.v[2] = VectorDoubleSet(0.0, 0.0, scaleZ, 0.0);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
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

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleRotationRollPitchYawFromVector(SIMDVectorDouble angles)noexcept{
    return MatrixDoubleRotationQuaternion(QuaternionDoubleRotationRollPitchYawFromVector(angles));
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleRotationRollPitchYaw(f64 pitch, f64 yaw, f64 roll)noexcept{
    return MatrixDoubleRotationRollPitchYawFromVector(VectorDoubleSet(pitch, yaw, roll, 0.0));
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

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleRotationAxis(SIMDVectorDouble axis, f64 angle)noexcept{
    NWB_ASSERT(!VectorDouble3Equal(axis, VectorDoubleZero()));
    NWB_ASSERT(!VectorDouble3IsInfinite(axis));

    return MatrixDoubleRotationNormal(VectorDouble3Normalize(axis), angle);
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

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleTransformation(SIMDVectorDouble scalingOrigin, SIMDVectorDouble scalingOrientationQuaternion, SIMDVectorDouble scaling, SIMDVectorDouble rotationOrigin, SIMDVectorDouble rotationQuaternion, SIMDVectorDouble translation)noexcept{
    const SIMDVectorDouble vScalingOrigin = VectorDoubleSelect(VectorDoubleZero(), scalingOrigin, s_DoubleSIMDSelect1110);
    const SIMDVectorDouble vRotationOrigin = VectorDoubleSelect(VectorDoubleZero(), rotationOrigin, s_DoubleSIMDSelect1110);

    // Oriented scaling Sso = Rso * diag(scaling) * Rso^T, then the linear part is Rr * Sso. The five
    // translation matrices of the original chain collapse into a single translation column.
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

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleLookToRH(SIMDVectorDouble eyePosition, SIMDVectorDouble eyeDirection, SIMDVectorDouble upDirection)noexcept{
    return MatrixDoubleLookToLH(eyePosition, VectorDoubleNegate(eyeDirection), upDirection);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleLookAtLH(SIMDVectorDouble eyePosition, SIMDVectorDouble focusPosition, SIMDVectorDouble upDirection)noexcept{
    return MatrixDoubleLookToLH(eyePosition, VectorDoubleSubtract(focusPosition, eyePosition), upDirection);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleLookAtRH(SIMDVectorDouble eyePosition, SIMDVectorDouble focusPosition, SIMDVectorDouble upDirection)noexcept{
    return MatrixDoubleLookToLH(eyePosition, VectorDoubleSubtract(eyePosition, focusPosition), upDirection);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f64 s_DoubleMatrixProjectionNearEqualEpsilon = 1.0e-12;
inline constexpr f64 s_DoubleMatrixPerspectiveFovNearEqualEpsilon = s_DoubleMatrixProjectionNearEqualEpsilon * s_DoubleMatrixTwo;

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveImpl(
    const f64 viewWidth,
    const f64 viewHeight,
    const f64 nearZ,
    const f64 farZ,
    const f64 rangeDenominator,
    const f64 rangeNearScale,
    const f64 forwardZ
)noexcept{
    NWB_ASSERT(nearZ > 0.0 && farZ > 0.0);
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(viewWidth, 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(viewHeight, 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(farZ, nearZ, s_DoubleMatrixProjectionNearEqualEpsilon));

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble nearZVector = VectorDoubleReplicate(nearZ);
    const SIMDVectorDouble twoNearZ = VectorDoubleAdd(nearZVector, nearZVector);
    const SIMDVectorDouble range = VectorDoubleDivide(VectorDoubleReplicate(farZ), VectorDoubleReplicate(rangeDenominator));
    const SIMDVectorDouble rangeNear = VectorDoubleMultiply(VectorDoubleScale(range, rangeNearScale), nearZVector);

    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleMergeX(VectorDoubleDivide(twoNearZ, VectorDoubleReplicate(viewWidth)), zero, zero, zero);
    matrix.v[1] = VectorDoubleMergeX(zero, VectorDoubleDivide(twoNearZ, VectorDoubleReplicate(viewHeight)), zero, zero);
    matrix.v[2] = VectorDoubleMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = VectorDoubleMergeX(zero, zero, VectorDoubleReplicate(forwardZ), zero);
    return matrix;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveLH(f64 viewWidth, f64 viewHeight, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveImpl(viewWidth, viewHeight, nearZ, farZ, farZ - nearZ, -1.0, 1.0);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveRH(f64 viewWidth, f64 viewHeight, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveImpl(viewWidth, viewHeight, nearZ, farZ, nearZ - farZ, 1.0, -1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDMatrixDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveFovImpl(
    const f64 fovAngleY,
    const f64 aspectRatio,
    const f64 nearZ,
    const f64 farZ,
    const f64 rangeDenominator,
    const f64 rangeNearScale,
    const f64 forwardZ
)noexcept{
    NWB_ASSERT(nearZ > 0.0 && farZ > 0.0);
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(fovAngleY, 0.0, s_DoubleMatrixPerspectiveFovNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(aspectRatio, 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(farZ, nearZ, s_DoubleMatrixProjectionNearEqualEpsilon));

    SIMDVectorDouble sinFov{};
    SIMDVectorDouble cosFov{};
    VectorDoubleSinCos(sinFov, cosFov, VectorDoubleScale(VectorDoubleReplicate(fovAngleY), s_DoubleMatrixHalf));
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble height = VectorDoubleDivide(cosFov, sinFov);
    const SIMDVectorDouble width = VectorDoubleDivide(height, VectorDoubleReplicate(aspectRatio));
    const SIMDVectorDouble range = VectorDoubleDivide(VectorDoubleReplicate(farZ), VectorDoubleReplicate(rangeDenominator));
    const SIMDVectorDouble rangeNear = VectorDoubleMultiply(
        VectorDoubleMultiply(VectorDoubleReplicate(rangeNearScale), range),
        VectorDoubleReplicate(nearZ)
    );

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
    const f64 rangeDenominator,
    const f64 centerScale,
    const f64 rangeNearScale,
    const f64 forwardZ
)noexcept{
    NWB_ASSERT(nearZ > 0.0 && farZ > 0.0);
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(viewRight, viewLeft, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(viewTop, viewBottom, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(farZ, nearZ, s_DoubleMatrixProjectionNearEqualEpsilon));

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble left = VectorDoubleReplicate(viewLeft);
    const SIMDVectorDouble right = VectorDoubleReplicate(viewRight);
    const SIMDVectorDouble bottom = VectorDoubleReplicate(viewBottom);
    const SIMDVectorDouble top = VectorDoubleReplicate(viewTop);
    const SIMDVectorDouble nearZVector = VectorDoubleReplicate(nearZ);
    const SIMDVectorDouble twoNearZ = VectorDoubleAdd(nearZVector, nearZVector);
    const SIMDVectorDouble reciprocalWidth = VectorDoubleReciprocal(VectorDoubleSubtract(right, left));
    const SIMDVectorDouble reciprocalHeight = VectorDoubleReciprocal(VectorDoubleSubtract(top, bottom));
    const SIMDVectorDouble range = VectorDoubleDivide(VectorDoubleReplicate(farZ), VectorDoubleReplicate(rangeDenominator));
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

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleOrthographicImpl(
    const f64 viewWidth,
    const f64 viewHeight,
    const f64 nearZ,
    const f64 rangeDenominator,
    const f64 rangeNearScale
)noexcept{
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(viewWidth, 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(viewHeight, 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(rangeDenominator, 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble nearZVector = VectorDoubleReplicate(nearZ);
    const SIMDVectorDouble range = VectorDoubleReciprocal(VectorDoubleReplicate(rangeDenominator));
    const SIMDVectorDouble rangeNear = VectorDoubleMultiply(VectorDoubleScale(range, rangeNearScale), nearZVector);

    SIMDMatrixDouble matrix{};
    matrix.v[0] = VectorDoubleMergeX(VectorDoubleDivide(s_DoubleSIMDTwo, VectorDoubleReplicate(viewWidth)), zero, zero, zero);
    matrix.v[1] = VectorDoubleMergeX(zero, VectorDoubleDivide(s_DoubleSIMDTwo, VectorDoubleReplicate(viewHeight)), zero, zero);
    matrix.v[2] = VectorDoubleMergeX(zero, zero, range, rangeNear);
    matrix.v[3] = s_DoubleSIMDIdentityR3;
    return matrix;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleOrthographicOffCenterImpl(
    const f64 viewLeft,
    const f64 viewRight,
    const f64 viewBottom,
    const f64 viewTop,
    const f64 nearZ,
    const f64 rangeDenominator,
    const f64 rangeNearScale
)noexcept{
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(viewRight, viewLeft, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(viewTop, viewBottom, s_DoubleMatrixProjectionNearEqualEpsilon));
    NWB_ASSERT(!SIMDMatrixDoubleDetail::ScalarNearEqual(rangeDenominator, 0.0, s_DoubleMatrixProjectionNearEqualEpsilon));

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble left = VectorDoubleReplicate(viewLeft);
    const SIMDVectorDouble right = VectorDoubleReplicate(viewRight);
    const SIMDVectorDouble bottom = VectorDoubleReplicate(viewBottom);
    const SIMDVectorDouble top = VectorDoubleReplicate(viewTop);
    const SIMDVectorDouble nearZVector = VectorDoubleReplicate(nearZ);
    const SIMDVectorDouble reciprocalWidth = VectorDoubleReciprocal(VectorDoubleSubtract(right, left));
    const SIMDVectorDouble reciprocalHeight = VectorDoubleReciprocal(VectorDoubleSubtract(top, bottom));
    const SIMDVectorDouble range = VectorDoubleReciprocal(VectorDoubleReplicate(rangeDenominator));
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


NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveFovLH(f64 fovAngleY, f64 aspectRatio, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveFovImpl(fovAngleY, aspectRatio, nearZ, farZ, farZ - nearZ, -1.0, 1.0);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveFovRH(f64 fovAngleY, f64 aspectRatio, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveFovImpl(fovAngleY, aspectRatio, nearZ, farZ, nearZ - farZ, 1.0, -1.0);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveOffCenterLH(f64 viewLeft, f64 viewRight, f64 viewBottom, f64 viewTop, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, farZ, farZ - nearZ, -1.0, -1.0, 1.0);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoublePerspectiveOffCenterRH(f64 viewLeft, f64 viewRight, f64 viewBottom, f64 viewTop, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoublePerspectiveOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, farZ, nearZ - farZ, 1.0, 1.0, -1.0);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleOrthographicLH(f64 viewWidth, f64 viewHeight, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoubleOrthographicImpl(viewWidth, viewHeight, nearZ, farZ - nearZ, -1.0);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleOrthographicRH(f64 viewWidth, f64 viewHeight, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoubleOrthographicImpl(viewWidth, viewHeight, nearZ, nearZ - farZ, 1.0);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleOrthographicOffCenterLH(f64 viewLeft, f64 viewRight, f64 viewBottom, f64 viewTop, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoubleOrthographicOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, farZ - nearZ, -1.0);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleOrthographicOffCenterRH(f64 viewLeft, f64 viewRight, f64 viewBottom, f64 viewTop, f64 nearZ, f64 farZ)noexcept{
    return SIMDMatrixDoubleDetail::MatrixDoubleOrthographicOffCenterImpl(viewLeft, viewRight, viewBottom, viewTop, nearZ, nearZ - farZ, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

