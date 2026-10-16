// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <global/floating_point_environment.h>
#include <global/scope_exit.h>
#include <global/simdmath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_current_math_contract_boundaries{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr f32 s_SingleEpsilon = NWB::Tests::s_DefaultNearlyEqualEpsilon;
constexpr f64 s_DoubleEpsilon = 2.0e-14;

void ExpectSingleDirectionNear(SIMDVectorSingle actual, SIMDVectorSingle expected){
    for(usize lane = 0u; lane < 3u; ++lane)
        EXPECT_NEAR(VectorGetByIndex(actual, lane), VectorGetByIndex(expected, lane), s_SingleEpsilon) << lane;
}

void ExpectDoubleDirectionNear(SIMDVectorDouble actual, SIMDVectorDouble expected){
    for(usize lane = 0u; lane < 3u; ++lane)
        EXPECT_NEAR(actual[lane], expected[lane], s_DoubleEpsilon) << lane;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CurrentMathContractBoundaries, SingleRigidExtractionPreservesNonzeroRotationAndDualTranslationDirection){
    using namespace __hidden_current_math_contract_boundaries;
    const SIMDMatrix matrix = MatrixSet(0.0f, -1.0f, 0.0f, 7.0f, 1.0f, 0.0f, 0.0f, -11.0f, 0.0f, 0.0f, 1.0f, 5.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    const SIMDVectorSingle direction = VectorSet(2.0f, -3.0f, 4.0f, 0.0f);
    const auto quaternion = MatrixTryBuildRigidRotationQuaternion(matrix, 0.0f, 0.0f);
    ASSERT_TRUE(quaternion);
    ExpectSingleDirectionNear(Vector3Rotate(direction, *quaternion), Vector3TransformNormal(direction, matrix));

    const auto dualQuaternion = MatrixTryBuildRigidDualQuaternion(matrix, 0.0f, 0.0f);
    ASSERT_TRUE(dualQuaternion);
    const SIMDVectorSingle translation = VectorScale(QuaternionMultiply(dualQuaternion->dual, QuaternionConjugate(dualQuaternion->real)), 2.0f);
    ExpectSingleDirectionNear(translation, VectorSet(7.0f, -11.0f, 5.0f, 0.0f));
    ExpectSingleDirectionNear(VectorAdd(Vector3Rotate(direction, dualQuaternion->real), translation), Vector3Transform(direction, matrix));
}

TEST(CurrentMathContractBoundaries, OppositelyOrientedThinBoxesDoNotInventAnIntersection){
    const BoundingOrientedBox lhs(Float3U(0.0f, 0.0f, 0.0f), Float3U(1.0f, 1.0f, 0.1f), Float4(0.0f, 0.0f, 0.0f, 1.0f));
    const BoundingDoubleOrientedBox doubleLhs(Double3U(0.0, 0.0, 0.0), Double3U(1.0, 1.0, 0.1), Double4(0.0, 0.0, 0.0, 1.0));
    for(i32 sign = -1; sign <= 1; sign += 2){
        SCOPED_TRACE(sign);
        Float4 orientation;
        StoreFloat(QuaternionRotationNormal(VectorSet(0.0f, 0.0f, 1.0f, 0.0f), static_cast<f32>(sign) * s_PIDIV4), orientation);
        const BoundingOrientedBox rhs(Float3U(1.1f, -static_cast<f32>(sign) * 1.1f, 0.0f), Float3U(2.0f, 0.1f, 0.1f), orientation);
        EXPECT_FALSE(lhs.intersects(rhs));
        EXPECT_FALSE(rhs.intersects(lhs));

        Double4 doubleOrientation;
        StoreDouble(QuaternionDoubleRotationNormal(SIMDVectorDouble{ 0.0, 0.0, 1.0, 0.0 }, static_cast<f64>(sign) * s_DoublePIDIV4), doubleOrientation);
        const BoundingDoubleOrientedBox doubleRhs(Double3U(1.1, -static_cast<f64>(sign) * 1.1, 0.0), Double3U(2.0, 0.1, 0.1), doubleOrientation);
        EXPECT_FALSE(doubleLhs.intersects(doubleRhs));
        EXPECT_FALSE(doubleRhs.intersects(doubleLhs));
    }
}

TEST(CurrentMathContractBoundaries, OrientedBoxTransformationAppliesTheNewRotationAfterItsExistingRotation){
    using namespace __hidden_current_math_contract_boundaries;
    Float4 orientation;
    StoreFloat(QuaternionRotationNormal(VectorSet(0.0f, 0.0f, 1.0f, 0.0f), s_PIDIV2), orientation);
    const BoundingOrientedBox source(Float3U(0.0f, 0.0f, 0.0f), Float3U(2.0f, 0.1f, 0.1f), orientation);
    const SIMDVectorSingle rotation = QuaternionRotationNormal(VectorSet(1.0f, 0.0f, 0.0f, 0.0f), s_PIDIV2);
    BoundingOrientedBox fromSrt;
    BoundingOrientedBox fromMatrix;
    source.transform(fromSrt, 1.0f, rotation, VectorZero());
    source.transform(fromMatrix, MatrixRotationQuaternion(rotation));
    const SIMDVectorSingle axis = VectorSet(1.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVectorSingle expected = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    ExpectSingleDirectionNear(Vector3Rotate(axis, LoadFloat(fromSrt.orientation)), expected);
    ExpectSingleDirectionNear(Vector3Rotate(axis, LoadFloat(fromMatrix.orientation)), expected);

    Double4 doubleOrientation;
    StoreDouble(QuaternionDoubleRotationNormal(SIMDVectorDouble{ 0.0, 0.0, 1.0, 0.0 }, s_DoublePIDIV2), doubleOrientation);
    const BoundingDoubleOrientedBox doubleSource(Double3U(0.0, 0.0, 0.0), Double3U(2.0, 0.1, 0.1), doubleOrientation);
    const SIMDVectorDouble doubleRotation = QuaternionDoubleRotationNormal(SIMDVectorDouble{ 1.0, 0.0, 0.0, 0.0 }, s_DoublePIDIV2);
    BoundingDoubleOrientedBox doubleFromSrt;
    BoundingDoubleOrientedBox doubleFromMatrix;
    doubleSource.transform(doubleFromSrt, 1.0, doubleRotation, VectorDoubleZero());
    doubleSource.transform(doubleFromMatrix, MatrixDoubleRotationQuaternion(doubleRotation));
    const SIMDVectorDouble doubleAxis{ 1.0, 0.0, 0.0, 0.0 };
    const SIMDVectorDouble doubleExpected{ 0.0, 0.0, 1.0, 0.0 };
    ExpectDoubleDirectionNear(VectorDouble3Rotate(doubleAxis, LoadDouble(doubleFromSrt.orientation)), doubleExpected);
    ExpectDoubleDirectionNear(VectorDouble3Rotate(doubleAxis, LoadDouble(doubleFromMatrix.orientation)), doubleExpected);
}

TEST(CurrentMathContractBoundaries, TriangleBoxIntersectionRejectsASeparatingPlaneAndRetainsClosedDegenerateContacts){
    const BoundingBox box(Float3U(0.0f, 0.0f, 0.0f), Float3U(1.0f, 1.0f, 1.0f));
    const BoundingOrientedBox oriented(Float3U(0.0f, 0.0f, 0.0f), Float3U(1.0f, 1.0f, 1.0f), Float4(0.0f, 0.0f, 0.0f, 1.0f));
    const SIMDVectorSingle triangles[][3]{
        { VectorSet(4.0f, 0.0f, 0.0f, 0.0f), VectorSet(0.0f, 4.0f, 0.0f, 0.0f), VectorSet(0.0f, 0.0f, 4.0f, 0.0f) },
        { VectorSet(1.0f, -1.0f, -1.0f, 0.0f), VectorSet(1.0f, 1.0f, -1.0f, 0.0f), VectorSet(1.0f, 0.0f, 1.0f, 0.0f) },
        { VectorSet(-2.0f, 0.0f, 0.0f, 0.0f), VectorSet(2.0f, 0.0f, 0.0f, 0.0f), VectorSet(2.0f, 0.0f, 0.0f, 0.0f) },
        { VectorSet(1.0f, 1.0f, 1.0f, 0.0f), VectorSet(1.0f, 1.0f, 1.0f, 0.0f), VectorSet(1.0f, 1.0f, 1.0f, 0.0f) },
        { VectorSet(1.2f, 1.0f, 0.0f, 0.0f), VectorSet(1.0f, 1.2f, 0.0f, 0.0f), VectorSet(1.0f, 1.2f, 0.0f, 0.0f) },
    };
    constexpr bool s_Expected[]{ false, true, true, true, false };
    for(usize index = 0u; index < LengthOf(triangles); ++index){
        SCOPED_TRACE(index);
        EXPECT_EQ(box.intersects(triangles[index][0], triangles[index][1], triangles[index][2]), s_Expected[index]);
        EXPECT_EQ(oriented.intersects(triangles[index][0], triangles[index][1], triangles[index][2]), s_Expected[index]);
    }

    const BoundingDoubleBox doubleBox(Double3U(0.0, 0.0, 0.0), Double3U(1.0, 1.0, 1.0));
    const BoundingDoubleOrientedBox doubleOriented(Double3U(0.0, 0.0, 0.0), Double3U(1.0, 1.0, 1.0), Double4(0.0, 0.0, 0.0, 1.0));
    const SIMDVectorDouble doubleTriangles[][3]{
        { SIMDVectorDouble{ 4.0, 0.0, 0.0, 0.0 }, SIMDVectorDouble{ 0.0, 4.0, 0.0, 0.0 }, SIMDVectorDouble{ 0.0, 0.0, 4.0, 0.0 } },
        { SIMDVectorDouble{ 1.0, -1.0, -1.0, 0.0 }, SIMDVectorDouble{ 1.0, 1.0, -1.0, 0.0 }, SIMDVectorDouble{ 1.0, 0.0, 1.0, 0.0 } },
        { SIMDVectorDouble{ -2.0, 0.0, 0.0, 0.0 }, SIMDVectorDouble{ 2.0, 0.0, 0.0, 0.0 }, SIMDVectorDouble{ 2.0, 0.0, 0.0, 0.0 } },
        { SIMDVectorDouble{ 1.0, 1.0, 1.0, 0.0 }, SIMDVectorDouble{ 1.0, 1.0, 1.0, 0.0 }, SIMDVectorDouble{ 1.0, 1.0, 1.0, 0.0 } },
        { SIMDVectorDouble{ 1.2, 1.0, 0.0, 0.0 }, SIMDVectorDouble{ 1.0, 1.2, 0.0, 0.0 }, SIMDVectorDouble{ 1.0, 1.2, 0.0, 0.0 } },
    };
    for(usize index = 0u; index < LengthOf(doubleTriangles); ++index){
        SCOPED_TRACE(index);
        EXPECT_EQ(doubleBox.intersects(doubleTriangles[index][0], doubleTriangles[index][1], doubleTriangles[index][2]), s_Expected[index]);
        EXPECT_EQ(doubleOriented.intersects(doubleTriangles[index][0], doubleTriangles[index][1], doubleTriangles[index][2]), s_Expected[index]);
    }
}

TEST(CurrentMathContractBoundaries, ShearedSphereContainsATransformedSurfacePointWithoutChangingAdmittedSrt){
    using namespace __hidden_current_math_contract_boundaries;
    const BoundingSphere source(Float3U(0.0f, 0.0f, 0.0f), 1.0f);
    SIMDMatrix shear = MatrixIdentity();
    shear.v[0] = VectorSet(1.0f, 1.0f, 0.0f, 0.0f);
    BoundingSphere transformed;
    source.transform(transformed, shear);
    const f32 diagonal = 1.0f / Sqrt(2.0f);
    const SIMDVectorSingle point = Vector3Transform(VectorSet(diagonal, diagonal, 0.0f, 1.0f), shear);
    EXPECT_NE(transformed.contains(point), ContainmentType::Disjoint);
    const SIMDMatrix srt = MatrixMultiply(MatrixTranslation(7.0f, -11.0f, 5.0f), MatrixScaling(2.0f, 3.0f, 4.0f));
    ASSERT_TRUE(MatrixDecompose(srt));
    source.transform(transformed, srt);
    EXPECT_FLOAT_EQ(transformed.centerRadius.w, 4.0f);
    ExpectSingleDirectionNear(LoadFloat(transformed.centerRadius), VectorSet(7.0f, -11.0f, 5.0f, 0.0f));

    const BoundingDoubleSphere doubleSource(Double3U(0.0, 0.0, 0.0), 1.0);
    SIMDMatrixDouble doubleShear = MatrixDoubleIdentity();
    doubleShear.v[0] = SIMDVectorDouble{ 1.0, 1.0, 0.0, 0.0 };
    BoundingDoubleSphere doubleTransformed;
    doubleSource.transform(doubleTransformed, doubleShear);
    const f64 doubleDiagonal = 1.0 / Sqrt(2.0);
    const SIMDVectorDouble doublePoint = VectorDouble3Transform(SIMDVectorDouble{ doubleDiagonal, doubleDiagonal, 0.0, 1.0 }, doubleShear);
    EXPECT_NE(doubleTransformed.contains(doublePoint), ContainmentType::Disjoint);
    const SIMDMatrixDouble doubleSrt = MatrixDoubleMultiply(MatrixDoubleTranslation(7.0, -11.0, 5.0), MatrixDoubleScaling(2.0, 3.0, 4.0));
    ASSERT_TRUE(MatrixDoubleDecompose(doubleSrt));
    doubleSource.transform(doubleTransformed, doubleSrt);
    EXPECT_DOUBLE_EQ(doubleTransformed.centerRadius.w, 4.0);
    ExpectDoubleDirectionNear(LoadDouble(doubleTransformed.centerRadius), SIMDVectorDouble{ 7.0, -11.0, 5.0, 0.0 });
}

TEST(CurrentMathContractBoundaries, SphereContainmentAndIntersectionAgreeAtExternalTangencyAndZeroRadius){
    const BoundingSphere lhs(Float3U(0.0f, 0.0f, 0.0f), 1.0f);
    const BoundingSphere touching(Float3U(2.0f, 0.0f, 0.0f), 1.0f);
    const BoundingSphere point(Float3U(0.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_TRUE(lhs.intersects(touching));
    EXPECT_EQ(lhs.contains(touching), ContainmentType::Intersects);
    EXPECT_EQ(touching.contains(lhs), ContainmentType::Intersects);
    EXPECT_TRUE(point.intersects(point));
    EXPECT_EQ(point.contains(point), ContainmentType::Contains);
    EXPECT_EQ(BoundingSphere(Float3U(1.0f, 0.0f, 0.0f), 2.0f).contains(lhs), ContainmentType::Contains);

    const BoundingDoubleSphere doubleLhs(Double3U(0.0, 0.0, 0.0), 1.0);
    const BoundingDoubleSphere doubleTouching(Double3U(2.0, 0.0, 0.0), 1.0);
    const BoundingDoubleSphere doublePoint(Double3U(0.0, 0.0, 0.0), 0.0);
    EXPECT_TRUE(doubleLhs.intersects(doubleTouching));
    EXPECT_EQ(doubleLhs.contains(doubleTouching), ContainmentType::Intersects);
    EXPECT_EQ(doubleTouching.contains(doubleLhs), ContainmentType::Intersects);
    EXPECT_TRUE(doublePoint.intersects(doublePoint));
    EXPECT_EQ(doublePoint.contains(doublePoint), ContainmentType::Contains);
    EXPECT_EQ(BoundingDoubleSphere(Double3U(1.0, 0.0, 0.0), 2.0).contains(doubleLhs), ContainmentType::Contains);
}

TEST(CurrentMathContractBoundaries, FrustumTransformationAppliesTheNewRotationAfterItsExistingRotation){
    using namespace __hidden_current_math_contract_boundaries;
    Float4 orientation;
    StoreFloat(QuaternionRotationNormal(VectorSet(0.0f, 0.0f, 1.0f, 0.0f), s_PIDIV2), orientation);
    const BoundingFrustum source(Float3U(0.0f, 0.0f, 0.0f), orientation, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 2.0f);
    const SIMDVectorSingle rotation = QuaternionRotationNormal(VectorSet(1.0f, 0.0f, 0.0f, 0.0f), s_PIDIV2);
    BoundingFrustum fromSrt;
    BoundingFrustum fromMatrix;
    source.transform(fromSrt, 1.0f, rotation, VectorZero());
    source.transform(fromMatrix, MatrixRotationQuaternion(rotation));
    Float3U sourceCorners[BoundingFrustum::s_CornerCount];
    Float3U srtCorners[BoundingFrustum::s_CornerCount];
    Float3U matrixCorners[BoundingFrustum::s_CornerCount];
    source.getCorners(sourceCorners);
    fromSrt.getCorners(srtCorners);
    fromMatrix.getCorners(matrixCorners);
    for(usize index = 0u; index < BoundingFrustum::s_CornerCount; ++index){
        SCOPED_TRACE(index);
        const SIMDVectorSingle expected = Vector3Rotate(LoadFloat(sourceCorners[index]), rotation);
        ExpectSingleDirectionNear(LoadFloat(srtCorners[index]), expected);
        ExpectSingleDirectionNear(LoadFloat(matrixCorners[index]), expected);
    }

    Double4 doubleOrientation;
    StoreDouble(QuaternionDoubleRotationNormal(SIMDVectorDouble{ 0.0, 0.0, 1.0, 0.0 }, s_DoublePIDIV2), doubleOrientation);
    const BoundingDoubleFrustum doubleSource(Double3U(0.0, 0.0, 0.0), doubleOrientation, 1.0, -1.0, 1.0, -1.0, 1.0, 2.0);
    const SIMDVectorDouble doubleRotation = QuaternionDoubleRotationNormal(SIMDVectorDouble{ 1.0, 0.0, 0.0, 0.0 }, s_DoublePIDIV2);
    BoundingDoubleFrustum doubleFromSrt;
    BoundingDoubleFrustum doubleFromMatrix;
    doubleSource.transform(doubleFromSrt, 1.0, doubleRotation, VectorDoubleZero());
    doubleSource.transform(doubleFromMatrix, MatrixDoubleRotationQuaternion(doubleRotation));
    Double3U doubleSourceCorners[BoundingDoubleFrustum::s_DoubleCornerCount];
    Double3U doubleSrtCorners[BoundingDoubleFrustum::s_DoubleCornerCount];
    Double3U doubleMatrixCorners[BoundingDoubleFrustum::s_DoubleCornerCount];
    doubleSource.getCorners(doubleSourceCorners);
    doubleFromSrt.getCorners(doubleSrtCorners);
    doubleFromMatrix.getCorners(doubleMatrixCorners);
    for(usize index = 0u; index < BoundingDoubleFrustum::s_DoubleCornerCount; ++index){
        SCOPED_TRACE(index);
        const SIMDVectorDouble expected = VectorDouble3Rotate(LoadDouble(doubleSourceCorners[index]), doubleRotation);
        ExpectDoubleDirectionNear(LoadDouble(doubleSrtCorners[index]), expected);
        ExpectDoubleDirectionNear(LoadDouble(doubleMatrixCorners[index]), expected);
    }
}


TEST(CurrentMathContractBoundaries, NonuniformOrientedBoxTransformBoundsItsRotatedAxesAndReflections){
    using namespace __hidden_current_math_contract_boundaries;
    const SIMDVectorSingle orientation = QuaternionRotationNormal(VectorSet(0.0f, 0.0f, 1.0f, 0.0f), s_PIDIV4);
    Float4 orientationStorage;
    StoreFloat(orientation, orientationStorage);
    const BoundingOrientedBox source(Float3U(0.0f, 0.0f, 0.0f), Float3U(1.0f, 2.0f, 1.0f), orientationStorage);
    const SIMDVectorSingle sourcePoint = Vector3Rotate(VectorSet(0.9f, -1.8f, 0.9f, 0.0f), orientation);
    ASSERT_EQ(source.contains(sourcePoint), ContainmentType::Contains);

    const SIMDVectorDouble doubleOrientation = QuaternionDoubleRotationNormal(SIMDVectorDouble{ 0.0, 0.0, 1.0, 0.0 }, s_DoublePIDIV4);
    Double4 doubleOrientationStorage;
    StoreDouble(doubleOrientation, doubleOrientationStorage);
    const BoundingDoubleOrientedBox doubleSource(Double3U(0.0, 0.0, 0.0), Double3U(1.0, 2.0, 1.0), doubleOrientationStorage);
    const SIMDVectorDouble doubleSourcePoint = VectorDouble3Rotate(SIMDVectorDouble{ 0.9, -1.8, 0.9, 0.0 }, doubleOrientation);
    ASSERT_EQ(doubleSource.contains(doubleSourcePoint), ContainmentType::Contains);
    for(i32 sign = -1; sign <= 1; sign += 2){
        SCOPED_TRACE(sign);
        const SIMDMatrix matrix = MatrixScaling(static_cast<f32>(sign) * 2.0f, 1.0f, 1.0f);
        ASSERT_TRUE(MatrixDecompose(matrix));
        BoundingOrientedBox transformed;
        source.transform(transformed, matrix);
        EXPECT_EQ(transformed.contains(Vector3Transform(sourcePoint, matrix)), ContainmentType::Contains);
        const SIMDVectorSingle expectedExtents = sign > 0 ? VectorSet(2.5f, 3.5f, 1.0f, 0.0f) : VectorSet(3.5f, 2.5f, 1.0f, 0.0f);
        ExpectSingleDirectionNear(LoadFloat(transformed.extents), expectedExtents);

        const SIMDMatrixDouble doubleMatrix = MatrixDoubleScaling(static_cast<f64>(sign) * 2.0, 1.0, 1.0);
        ASSERT_TRUE(MatrixDoubleDecompose(doubleMatrix));
        BoundingDoubleOrientedBox doubleTransformed;
        doubleSource.transform(doubleTransformed, doubleMatrix);
        EXPECT_EQ(doubleTransformed.contains(VectorDouble3Transform(doubleSourcePoint, doubleMatrix)), ContainmentType::Contains);
        const SIMDVectorDouble doubleExpectedExtents = sign > 0 ? SIMDVectorDouble{ 2.5, 3.5, 1.0, 0.0 } : SIMDVectorDouble{ 3.5, 2.5, 1.0, 0.0 };
        ExpectDoubleDirectionNear(LoadDouble(doubleTransformed.extents), doubleExpectedExtents);
    }
}

TEST(CurrentMathContractBoundaries, SphereTransformBoundsSmallShearAdmittedByDecomposition){
    const BoundingSphere source(Float3U(0.0f, 0.0f, 0.0f), 1.0f);
    const BoundingDoubleSphere doubleSource(Double3U(0.0, 0.0, 0.0), 1.0);
    const f32 diagonal = 1.0f / Sqrt(2.0f);
    const f64 doubleDiagonal = 1.0 / Sqrt(2.0);
    for(i32 sign = -1; sign <= 1; sign += 2){
        SCOPED_TRACE(sign);
        SIMDMatrix shear = MatrixIdentity();
        shear.v[0] = VectorSet(1.0f, static_cast<f32>(sign) * 0.1f, 0.0f, 0.0f);
        ASSERT_TRUE(MatrixDecompose(shear));
        BoundingSphere transformed;
        source.transform(transformed, shear);
        const SIMDVectorSingle point = Vector3Transform(VectorSet(diagonal, static_cast<f32>(sign) * diagonal, 0.0f, 0.0f), shear);
        EXPECT_NE(transformed.contains(point), ContainmentType::Disjoint);

        SIMDMatrixDouble doubleShear = MatrixDoubleIdentity();
        doubleShear.v[0] = SIMDVectorDouble{ 1.0, static_cast<f64>(sign) * 0.001, 0.0, 0.0 };
        ASSERT_TRUE(MatrixDoubleDecompose(doubleShear));
        BoundingDoubleSphere doubleTransformed;
        doubleSource.transform(doubleTransformed, doubleShear);
        const SIMDVectorDouble doublePoint = VectorDouble3Transform(SIMDVectorDouble{ doubleDiagonal, static_cast<f64>(sign) * doubleDiagonal, 0.0, 0.0 }, doubleShear);
        EXPECT_NE(doubleTransformed.contains(doublePoint), ContainmentType::Disjoint);
    }
}


TEST(CurrentMathContractBoundaries, DoubleQuaternionExtractionKeepsBranchTiesAndIgnoresNonrotationLanes){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    });
    const SIMDVectorDouble inputs[]{
        { 1.0, 0.2, -0.3, 0.1 }, { 0.2, 1.0, -0.3, 0.1 }, { 0.2, -0.3, 1.0, 0.1 }, { 0.2, -0.3, 0.1, 1.0 },
        { 0.5, 0.5, 0.5, 0.5 }, { 0.5 + 0x1p-40, 0.5, 0.5, 0.5 }, { 0.5, 0.5 + 0x1p-40, 0.5, 0.5 }, { 0.5, 0.5, 0.5 + 0x1p-40, 0.5 }, { 0.5, 0.5, 0.5, 0.5 + 0x1p-40 }
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

