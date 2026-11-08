// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <global/floating_point_environment.h>
#include <global/simdmath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_quaternion_contract_tests{
void ExpectSingleDirectionNear(SIMDVectorSingle actual, SIMDVectorSingle expected){
    for(usize lane = 0u; lane < 3u; ++lane)
        EXPECT_NEAR(VectorGetByIndex(actual, lane), VectorGetByIndex(expected, lane), NWB::Tests::s_DefaultNearlyEqualEpsilon) << lane;
}
};


TEST(CurrentMathContractBoundaries, SingleRigidExtractionPreservesNonzeroRotationAndDualTranslationDirection){
    using namespace __hidden_quaternion_contract_tests;
    const SIMDMatrix matrix = MatrixSet(0.0f, -1.0f, 0.0f, 7.0f, 1.0f, 0.0f, 0.0f, -11.0f, 0.0f, 0.0f, 1.0f, 5.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    const SIMDVectorSingle direction = VectorSet(2.0f, -3.0f, 4.0f, 0.0f);
    const auto quaternion = MatrixTryBuildRigidRotationQuaternion(matrix, 0.0f, 0.0f);
    ASSERT_TRUE(quaternion);
    ExpectSingleDirectionNear(Vector3Rotate(direction, quaternion->value), Vector3TransformNormal(direction, matrix));

    const auto dualQuaternion = MatrixTryBuildRigidDualQuaternion(matrix, 0.0f, 0.0f);
    ASSERT_TRUE(dualQuaternion);
    const SIMDVectorSingle translation = VectorScale(QuaternionMultiply(dualQuaternion->dual, QuaternionConjugate(dualQuaternion->real)), 2.0f);
    ExpectSingleDirectionNear(translation, VectorSet(7.0f, -11.0f, 5.0f, 0.0f));
    ExpectSingleDirectionNear(VectorAdd(Vector3Rotate(direction, dualQuaternion->real), translation), Vector3Transform(direction, matrix));
}

TEST(CurrentMathContractBoundaries, DoubleQuaternionExtractionKeepsBranchTiesAndIgnoresNonrotationLanes){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    NWB::Tests::FloatingPointEnvironmentGuard restoreEnvironment(*originalEnvironment);
    alignas(SIMDVectorDouble) const Array<SIMDVectorDouble, 9u> inputs = {
        SIMDVectorDouble{ 1.0, 0.2, -0.3, 0.1 }, SIMDVectorDouble{ 0.2, 1.0, -0.3, 0.1 }, SIMDVectorDouble{ 0.2, -0.3, 1.0, 0.1 }, SIMDVectorDouble{ 0.2, -0.3, 0.1, 1.0 },
        SIMDVectorDouble{ 0.5, 0.5, 0.5, 0.5 }, SIMDVectorDouble{ 0.5 + 0x1p-40, 0.5, 0.5, 0.5 }, SIMDVectorDouble{ 0.5, 0.5 + 0x1p-40, 0.5, 0.5 }, SIMDVectorDouble{ 0.5, 0.5, 0.5 + 0x1p-40, 0.5 }, SIMDVectorDouble{ 0.5, 0.5, 0.5, 0.5 + 0x1p-40 }
    };
    volatile u64 unusedBits = 0x7ff0000000000001u;
    for(usize index = 0u; index < LengthOf(inputs); ++index){
        SCOPED_TRACE(index);
        const SIMDVectorDouble expected = QuaternionDoubleNormalize(inputs[index]);
        SIMDMatrixDouble matrix = MatrixDoubleRotationQuaternion(expected);
        for(usize row = 0u; row < 3u; ++row)
            matrix.v[row].w = BitCast<f64>(unusedBits);
        matrix.v[3] = VectorDoubleReplicate(BitCast<f64>(unusedBits));
        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        const SIMDVectorDouble actual = QuaternionDoubleRotationMatrix(matrix);
        const i32 exceptions = TestFloatingPointExceptions(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW);
        EXPECT_EQ(exceptions, 0);
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_NEAR(actual[lane], expected[lane], 8.0e-16) << lane;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
