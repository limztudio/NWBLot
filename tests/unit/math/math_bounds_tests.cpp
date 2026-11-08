// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>

#include <tests/common/test_context.h>

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


TEST(Math, DoubleSphereContainmentDistinguishesTangencyFromSubunitSeparationAtLargeCoordinates){
    constexpr f64 s_Base = 0x1p+40;
    const BoundingDoubleSphere sphere(Double3U(s_Base, 0.0, 0.0), 1.0);
    EXPECT_EQ(sphere.contains(SIMDVectorDouble{ s_Base + 1.0, 0.0, 0.0, 0.0 }), ContainmentType::Contains);
    EXPECT_EQ(sphere.contains(SIMDVectorDouble{ s_Base + 1.0 + 0x1p-10, 0.0, 0.0, 0.0 }), ContainmentType::Disjoint);
    const BoundingDoubleSphere point(Double3U(s_Base, 0.0, 0.0), 0.0);
    EXPECT_EQ(point.contains(SIMDVectorDouble{ s_Base + 0x1p-10, 0.0, 0.0, 0.0 }), ContainmentType::Disjoint);
}

TEST(Math, FrustumSphereTangencyAndZeroRadiusKeepContainmentBoundary){
    BoundingFrustum frustum;
    frustum.nearPlane = 1.0f;
    frustum.farPlane = 10.0f;
    frustum.rightSlope = 1.0f;
    frustum.leftSlope = -1.0f;
    frustum.topSlope = 1.0f;
    frustum.bottomSlope = -1.0f;
    alignas(SIMDVectorSingle) Array<SIMDVectorSingle, CollisionDetail::s_FrustumPlaneCount> planes{};
    frustum.getPlanes(&planes[0], &planes[1], &planes[2], &planes[3], &planes[4], &planes[5]);
    const Array<f32, 6u> centers = { 0.5f, 0.25f, 1.5f, 9.5f, 10.5f, 1.0f };
    const Array<ContainmentType::Enum, 6u> expected = { ContainmentType::Intersects, ContainmentType::Disjoint, ContainmentType::Contains, ContainmentType::Contains, ContainmentType::Intersects, ContainmentType::Contains, };
    for(usize index = 0u; index < centers.size(); ++index){
        const BoundingSphere sphere(Float3U(0.0f, 0.0f, centers[index]), index == centers.size() - 1u ? 0.0f : 0.5f);
        EXPECT_EQ(frustum.contains(sphere), expected[index]) << index;
        EXPECT_EQ(sphere.containedBy(planes[0], planes[1], planes[2], planes[3], planes[4], planes[5]), expected[index]) << index;
    }
}

TEST(Math, TransformedBoundsRejectInvalidAndOverflowingInputs){
    const SIMDVectorSingle minimum = VectorSet(-1.0f, -2.0f, -3.0f, 0.0f);
    const SIMDVectorSingle maximum = VectorSet(1.0f, 2.0f, 3.0f, 0.0f);
    EXPECT_FALSE(AabbTests::Transform(MatrixIdentity(), maximum, minimum));
    SIMDMatrix invalid = MatrixIdentity();
    invalid.v[0] = VectorSet(Limit<f32>::s_Infinity, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(AabbTests::Transform(invalid, minimum, maximum));
    const SIMDMatrix overflow = MatrixScaling(Limit<f32>::s_Max, Limit<f32>::s_Max, Limit<f32>::s_Max);
    EXPECT_FALSE(AabbTests::Transform(overflow, minimum, maximum));
    const auto point = AabbTests::Transform(MatrixIdentity(), VectorZero(), VectorZero());
    ASSERT_TRUE(point);
    EXPECT_TRUE(AabbTests::Valid(point->minBounds, point->maxBounds));
    EXPECT_TRUE(NearlyEqual3(point->minBounds, 0.0f, 0.0f, 0.0f));
    EXPECT_TRUE(NearlyEqual3(point->maxBounds, 0.0f, 0.0f, 0.0f));
}

TEST(Math, BoxNormalHandlesCornerAndEqualDistanceTie){
    const SIMDVectorSingle boxHalfExtents = VectorSet(1.0f, 1.0f, 1.0f, 0.0f);


    EXPECT_TRUE(NearlyEqual3(SdfTests::BoxNormal(VectorSet(2.0f, 2.0f, 1.0f, 0.0f), boxHalfExtents, VectorSet(0.0f, 1.0f, 0.0f, 0.0f), 0.000001f), 0.70710677f, 0.70710677f, 0.0f, 0.00001f));
    EXPECT_TRUE(NearlyEqual3(SdfTests::BoxNormal(VectorSet(0.9f, 0.9f, 0.2f, 0.0f), boxHalfExtents, VectorSet(0.0f, 1.0f, 0.0f, 0.0f), 0.000001f), 1.0f, 0.0f, 0.0f));
}

TEST(Math, DegeneratePlaneNormalUsesFallback){
    EXPECT_TRUE(NearlyEqual4(PlaneTests::FromPointNormal(VectorZero(), VectorSet(1.0f, 2.0f, 3.0f, 0.0f), VectorSet(0.0f, 0.0f, 1.0f, 0.0f)), 0.0f, 0.0f, 1.0f, -3.0f));
}

TEST(Math, RectangleExtentPoliciesPreserveNanSignedZeroAndInsetRounding){
    const f32 negativeZero = BitCast<f32>(u32(0x80000000u));
    const SIMDVectorSingle rectangle = VectorSet(0.0f, 0.0f, Limit<f32>::s_QuietNaN, negativeZero);
    const SIMDVectorSingle preserved = InsetRectValuePreservingExtent(rectangle, VectorZero());
    const SIMDVectorSingle ordinary = InsetRectValue(rectangle, VectorZero());
    EXPECT_TRUE(IsNaN(VectorGetZ(preserved)));
    EXPECT_TRUE(SignBit(VectorGetW(preserved)));
    EXPECT_EQ(BitCast<u32>(VectorGetZ(ordinary)), 0u);
    EXPECT_EQ(BitCast<u32>(VectorGetW(ordinary)), 0u);

    const SIMDVectorSingle finite = VectorSet(0.0f, 0.0f, 1.0f, 1.0f);
    const SIMDVectorSingle nanWidth = VectorSet(0.0f, 0.0f, Limit<f32>::s_QuietNaN, 1.0f);
    EXPECT_TRUE(IsNaN(VectorGetZ(IntersectRectValuePreservingExtent(finite, nanWidth))));
    EXPECT_EQ(BitCast<u32>(VectorGetZ(IntersectRectValue(finite, nanWidth))), 0u);

    const SIMDVectorSingle rounded = InsetRectValuePreservingExtent(VectorSet(0.0f, 0.0f, 16777216.0f, 1.0f), VectorSet(-1.0f, 0.0f, 16777216.0f, 0.0f));
    EXPECT_EQ(VectorGetZ(rounded), 0.0f);
    const SIMDVectorSingle bounds = IntersectRectBoundsValue(VectorSet(0.0f, 0.0f, 1.0f, 1.0f), VectorSet(negativeZero, 0.0f, 1.0f, 1.0f));
    EXPECT_TRUE(SignBit(VectorGetX(bounds)));
    EXPECT_FALSE(HasVisibleRectIntersection(VectorSet(0.0f, 0.0f, 1.0f, 1.0f), VectorSet(Limit<f32>::s_QuietNaN, 0.0f, 1.0f, 1.0f)));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

