// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/bit.h>
#include <global/limit.h>
#include <global/simdmath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MathGeometryBoundaries, Vector3TryNormalizeRejectsInvalidValues){
    EXPECT_FALSE(Vector3TryNormalize(VectorZero()));
    EXPECT_FALSE(Vector3TryNormalize(s_SIMDQNaN));
    EXPECT_FALSE(Vector3TryNormalize(s_SIMDInfinity));
}

TEST(MathGeometryBoundaries, TriangleAreaRejectsExactThreshold){
    const Float3U a(1.0f, 1.0f, 1.0f);
    const Float3U b(4.0f, 1.0f, 1.0f);
    const Float3U c(1.0f, 5.0f, 1.0f);
    EXPECT_TRUE(::TriangleHasArea(LoadFloat(a), LoadFloat(b), LoadFloat(c), 143.0));
    EXPECT_FALSE(::TriangleHasArea(LoadFloat(a), LoadFloat(b), LoadFloat(c), 144.0));
}

// Binary32 subtraction collapses this nondegenerate triangle to a line; promotion must happen before subtracting its origin.
TEST(MathGeometryBoundaries, TriangleAreaPromotesBeforeCancellingLargeEdges){
    const SIMDVectorSingle a = VectorSet(33554432.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVectorSingle b = VectorSet(1.0f, 1.0f, 0.0f, 0.0f);
    const SIMDVectorSingle c = VectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    EXPECT_TRUE(::TriangleHasArea(a, b, c, 0.5));
    EXPECT_FALSE(::TriangleHasArea(a, b, c, 1.0));
}

// Inactive W coordinates must not participate in promoted cross products or area reductions.
TEST(MathGeometryBoundaries, TriangleAreaIgnoresNonfiniteInactiveCoordinates){
    const SIMDVectorSingle a = VectorSetW(VectorZero(), Limit<f32>::s_Infinity);
    const SIMDVectorSingle b = VectorSetW(VectorSet(3.0f, 0.0f, 0.0f, 0.0f), VectorGetW(s_SIMDQNaN));
    const SIMDVectorSingle c = VectorSetW(VectorSet(0.0f, 4.0f, 0.0f, 0.0f), -Limit<f32>::s_Infinity);
    const SIMDVectorDouble normal = ::BuildTriangleAreaNormal64(a, b, c);
    EXPECT_EQ(BitCast<u64>(VectorDoubleGetW(normal)), 0u);
    EXPECT_TRUE(::TriangleHasArea(a, b, c, 143.0));
    EXPECT_FALSE(::TriangleHasArea(a, b, c, 144.0));
}

// The small squared components round away separately but survive their sum; retain the existing backend's strict area threshold.
TEST(MathGeometryBoundaries, TriangleAreaPreservesSquaredSumOrderAtThreshold){
    const SIMDVectorSingle a = VectorZero();
    const SIMDVectorSingle b = VectorSet(1.0f, 0.0f, -1.0f, 0.0f);
    const SIMDVectorSingle c = VectorSet(0.0f, 1.25f, -134217728.0f, 0.0f);
    constexpr f64 s_LargeSquaredComponent = 18014398509481984.0;
#if defined(NWB_HAS_AVX2)
    EXPECT_TRUE(::TriangleHasArea(a, b, c, s_LargeSquaredComponent));
    EXPECT_FALSE(::TriangleHasArea(a, b, c, s_LargeSquaredComponent + 4.0));
#else
    EXPECT_FALSE(::TriangleHasArea(a, b, c, s_LargeSquaredComponent));
    EXPECT_TRUE(::TriangleHasArea(a, b, c, s_LargeSquaredComponent - 2.0));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

