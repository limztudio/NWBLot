// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>

#include <tests/common/test_context.h>
#include <tests/unit/math/case_types.adhd.h>

#include <gtest/gtest.h>

#include <global/floating_point_environment.h>
#include <global/simdmath.h>
#include <global/compile.h>
#include <global/limit.h>
#include <global/simplemath.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_math_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::NearlyEqual;
using NWB::Tests::NearlyEqual3;
using NWB::Tests::NearlyEqual4;


TEST(Math, DoubleMatrixTransformsAndInversePreserveSubunitOffsetsAtLargeTranslations){
    constexpr f64 s_Base = 0x1p+40;
    const SIMDMatrixDouble matrix = MatrixDoubleMultiply(MatrixDoubleTranslation(s_Base + 0.25, -s_Base + 0.5, 3.0 + 0x1p-40), MatrixDoubleScaling(2.0, 4.0, 0.5));
    const SIMDVectorDouble point{ 0.25, -0.125, 0x1p-39, 1.0 };
    const SIMDVectorDouble transformed = VectorDouble3TransformCoord(point, matrix);
    EXPECT_EQ(transformed.x, s_Base + 0.75);
    EXPECT_EQ(transformed.y, -s_Base);
    EXPECT_EQ(transformed.z, 3.0 + 0x1p-39);
    EXPECT_EQ(transformed.w, 1.0);

    Double34 packed{};
    StoreDouble(matrix, packed);
    EXPECT_EQ(packed._14, s_Base + 0.25);
    EXPECT_EQ(packed._24, -s_Base + 0.5);
    EXPECT_EQ(packed._34, 3.0 + 0x1p-40);
    const SIMDVectorDouble fromPacked = VectorDouble3TransformCoord(point, LoadDouble(packed));
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(fromPacked[lane], transformed[lane]) << lane;

    SIMDVectorDouble determinant{};
    const SIMDMatrixDouble inverse = MatrixDoubleInverse(&determinant, matrix);
    EXPECT_EQ(determinant.x, 4.0);
    const SIMDVectorDouble restored = VectorDouble3TransformCoord(transformed, inverse);
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(restored[lane], point[lane]) << lane;
}

TEST(Math, DoubleDecompositionReconstructsNoncommutingRotationsWithNegativeScaleAndRejectsShear){
    const SIMDMatrixDouble rotation = MatrixDoubleMultiply(MatrixDoubleRotationX(0.5), MatrixDoubleMultiply(MatrixDoubleRotationY(-0.75), MatrixDoubleRotationZ(1.25)));
    const SIMDMatrixDouble matrix = MatrixDoubleMultiply(MatrixDoubleTranslation(0x1p+40 + 0.25, -7.0 + 0x1p-40, 11.0 + 0x1p-40), MatrixDoubleMultiply(rotation, MatrixDoubleScaling(-2.0, 3.0, 0.5)));
    const auto decomposition = MatrixDoubleDecompose(matrix);
    ASSERT_TRUE(decomposition);
    EXPECT_FALSE(QuaternionDoubleIsNaN(decomposition->rotation));
    const SIMDMatrixDouble reconstructed = MatrixDoubleAffineTransformation(decomposition->scale, VectorDoubleZero(), decomposition->rotation, decomposition->translation);
    for(usize row = 0u; row < 4u; ++row){
        for(usize lane = 0u; lane < 3u; ++lane)
            EXPECT_NEAR(reconstructed.v[row][lane], matrix.v[row][lane], 4.0e-15) << row << ':' << lane;
        EXPECT_EQ(reconstructed.v[row].w, matrix.v[row].w) << row;
    }
    SIMDMatrixDouble shear = MatrixDoubleIdentity();
    shear.v[0].y = 0.5;
    EXPECT_FALSE(MatrixDoubleDecompose(shear));
}

TEST(Math, DoubleAffineAdmissionRejectsSingularNonfiniteAndProjectiveMatrices){
    EXPECT_FALSE(MatrixDoubleIsInvertibleAffine(MatrixDoubleScaling(1.0, 1.0, 0x1p-81), 0.0, 0x1p-80));
    EXPECT_TRUE(MatrixDoubleIsInvertibleAffine(MatrixDoubleScaling(1.0, 1.0, 0x1p-79), 0.0, 0x1p-80));
    EXPECT_FALSE(MatrixDoubleIsInvertibleAffine(MatrixDoubleScaling(1.0, 1.0, 0.0), 0.0, 0.0));
    EXPECT_FALSE(MatrixDoubleIsAffine(MatrixDoubleTranslation(Limit<f64>::s_Infinity, 0.0, 0.0), 0.0));
    SIMDMatrixDouble projective = MatrixDoubleIdentity();
    projective.v[3] = SIMDVectorDouble{ 0.0, 0.0, 0x1p-40, 1.0 };
    EXPECT_FALSE(MatrixDoubleIsAffine(projective, 0x1p-41));
    EXPECT_TRUE(MatrixDoubleIsAffine(projective, 0x1p-39));
}

TEST(Math, DoubleQuaternionAntipodalInterpolationPreservesSmallDeviationFromRightAngle){
    const f64 angle = 0x1.921fb54442d18p+0 + 0x1p-40;
    const SIMDVectorDouble rotation = QuaternionDoubleRotationNormal(SIMDVectorDouble{ 0.0, 0.0, 1.0, 0.0 }, angle);
    const SIMDVectorDouble interpolated = QuaternionDoubleSlerp(rotation, VectorDoubleNegate(rotation), 0.5);
    EXPECT_FALSE(QuaternionDoubleIsNaN(interpolated));
    EXPECT_FALSE(QuaternionDoubleIsInfinite(interpolated));
    const SIMDVectorDouble rotated = VectorDouble3Rotate(SIMDVectorDouble{ 1.0, 0.0, 0.0, 0.0 }, interpolated);
    EXPECT_NEAR(rotated.x, Cos(angle), 4.0e-16);
    EXPECT_NEAR(rotated.y, Sin(angle), 4.0e-16);
    EXPECT_LT(rotated.x, -0x1p-41);
    const SIMDVectorDouble degenerateInverse = QuaternionDoubleInverse(VectorDoubleZero());
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(degenerateInverse[lane], 0.0) << lane;
}

TEST(Math, DoubleRigidQuaternionExtractionKeepsRowMatrixRotationAndDualTranslationDirection){
    // Transposing an M*v matrix during extraction reverses its nonzero rotation and corrupts the dual translation.
    const SIMDVectorDouble translation{ 7.0 + 0x1p-40, -11.0 + 0x1p-40, 5.0 + 0x1p-40, 0.0 };
    const SIMDMatrixDouble matrix = MatrixDoubleMultiply(MatrixDoubleTranslationFromVector(translation), MatrixDoubleRotationZ(0.75));
    const auto quaternion = MatrixDoubleTryBuildRigidRotationQuaternion(matrix, 0.0, 1.0e-12);
    ASSERT_TRUE(quaternion);
    const SIMDVectorDouble direction{ 2.0, -3.0, 4.0, 0.0 };
    const SIMDVectorDouble fromMatrix = VectorDouble3TransformNormal(direction, matrix);
    const SIMDVectorDouble fromQuaternion = VectorDouble3Rotate(direction, quaternion->value);
    for(usize lane = 0u; lane < 3u; ++lane)
        EXPECT_NEAR(fromQuaternion[lane], fromMatrix[lane], 2.0e-15) << lane;

    const auto dualQuaternion = MatrixDoubleTryBuildRigidDualQuaternion(matrix, 0.0, 1.0e-12);
    ASSERT_TRUE(dualQuaternion);
    const SIMDVectorDouble extractedTranslation = VectorDoubleScale(QuaternionDoubleMultiply(dualQuaternion->dual, QuaternionDoubleConjugate(dualQuaternion->real)), 2.0);
    for(usize lane = 0u; lane < 3u; ++lane)
        EXPECT_NEAR(extractedTranslation[lane], translation[lane], 4.0e-15) << lane;

    const SIMDVectorDouble transformed = VectorDoubleAdd(VectorDouble3Rotate(direction, dualQuaternion->real), extractedTranslation);
    const SIMDVectorDouble expected = VectorDouble3Transform(direction, matrix);
    for(usize lane = 0u; lane < 3u; ++lane)
        EXPECT_NEAR(transformed[lane], expected[lane], 4.0e-15) << lane;
}

TEST(Math, CenteredOrthographicProjectionPreservesNegativeZeroTranslation){
    const SIMDMatrix projection = MatrixOrthographicOffCenterLH(-1.0f, 1.0f, -1.0f, 1.0f, 1.0f, 11.0f);
    EXPECT_TRUE(SignBit(VectorGetW(projection.v[0])));
    EXPECT_TRUE(SignBit(VectorGetW(projection.v[1])));
}

TEST(Math, MatrixDecompositionReconstructsCollapsedAxesWithNegativeBasisTies){
    alignas(SIMDMatrix) const Array<SIMDMatrix, 4u> matrices = {
        MatrixSet(-1.0f, 0.0f, 0.0f, 3.0f, -1.0f, 0.0f, 0.0f, -2.0f, -1.0f, 0.0f, 0.0f, 7.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        MatrixSet(0.0f, -1.0f, 0.0f, 3.0f, 0.0f, -1.0f, 0.0f, -2.0f, 0.0f, -1.0f, 0.0f, 7.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        MatrixSet(0.0f, 0.0f, -1.0f, 3.0f, 0.0f, 0.0f, -1.0f, -2.0f, 0.0f, 0.0f, -1.0f, 7.0f, 0.0f, 0.0f, 0.0f, 1.0f),
        MatrixSet(0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, -2.0f, 0.0f, 0.0f, 0.0f, 7.0f, 0.0f, 0.0f, 0.0f, 1.0f),
    };
    for(usize index = 0u; index < LengthOf(matrices); ++index){
        SCOPED_TRACE(index);
        const auto decomposition = MatrixDecompose(matrices[index]);
        ASSERT_TRUE(decomposition);
        EXPECT_FALSE(Vector4IsNaN(decomposition->rotation));
        EXPECT_FALSE(Vector4IsInfinite(decomposition->rotation));
        const SIMDMatrix reconstructed = MatrixAffineTransformation(decomposition->scale, VectorZero(), decomposition->rotation, decomposition->translation);
        for(usize row = 0u; row < 4u; ++row){
            EXPECT_TRUE(Vector4LessOrEqual(VectorAbs(VectorSubtract(reconstructed.v[row], matrices[index].v[row])), VectorReplicate(NWB::Tests::s_DefaultNearlyEqualEpsilon))) << row;
        }
    }

    const SIMDMatrix shear = MatrixSet(1.0f, 1.0f, 0.0f, 3.0f, 0.0f, 1.0f, 0.0f, -2.0f, 0.0f, 0.0f, 1.0f, 7.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_FALSE(MatrixDecompose(shear));
}

TEST(Math, AffineQueriesRejectNonrigidSingularOverflowAndProjectiveMatrices){
    constexpr f32 s_AffineEpsilon = NWB::Tests::MathCases::s_MathMatrixAffineEpsilon;
    constexpr f32 s_DeterminantEpsilon = NWB::Tests::MathCases::s_MathMatrixDeterminantEpsilon;
    constexpr f32 s_RigidEpsilon = NWB::Tests::MathCases::s_MathMatrixRigidEpsilon;

    const SIMDMatrix scaled = MatrixScaling(2.0f, 3.0f, 4.0f);
    EXPECT_FALSE(MatrixIsRigidAffine(scaled, s_AffineEpsilon, s_RigidEpsilon));

    const SIMDMatrix nearSingular = MatrixScaling(1.0f, 1.0f, s_DeterminantEpsilon * 0.5f);
    EXPECT_FALSE(MatrixIsInvertibleAffine(nearSingular, s_AffineEpsilon, s_DeterminantEpsilon));

    const SIMDMatrix overflowed = MatrixScaling(s_MaxF32, s_MaxF32, s_MaxF32);
    EXPECT_TRUE(MatrixIsAffine(overflowed, s_AffineEpsilon));
    EXPECT_FALSE(MatrixIsInvertibleAffine(overflowed, s_AffineEpsilon, s_DeterminantEpsilon));

    SIMDMatrix nonAffine = MatrixIdentity();
    nonAffine.v[3] = VectorZero();
    EXPECT_FALSE(MatrixIsAffine(nonAffine, s_AffineEpsilon));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

