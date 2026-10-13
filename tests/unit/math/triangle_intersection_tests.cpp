// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>

#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/floating_point_environment.h>
#include <global/simd/collision_triangle.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_math_triangle_intersection_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VectorT>
static void CheckWindingAndOperandOrder(const VectorT (&first)[3], const VectorT (&second)[3], const bool expected){
    constexpr u32 permutations[][3] = { { 0u, 1u, 2u }, { 0u, 2u, 1u }, { 1u, 0u, 2u }, { 1u, 2u, 0u }, { 2u, 0u, 1u }, { 2u, 1u, 0u } };
    for(const auto& firstOrder : permutations){
        for(const auto& secondOrder : permutations){
            if constexpr(IsSame_V<VectorT, SIMDVectorSingle>){
                EXPECT_EQ(TriangleTests::Intersects(first[firstOrder[0]], first[firstOrder[1]], first[firstOrder[2]], second[secondOrder[0]], second[secondOrder[1]], second[secondOrder[2]]), expected);
                EXPECT_EQ(TriangleTests::Intersects(second[secondOrder[0]], second[secondOrder[1]], second[secondOrder[2]], first[firstOrder[0]], first[firstOrder[1]], first[firstOrder[2]]), expected);
            }
            else{
                EXPECT_EQ(TriangleDoubleTests::Intersects(first[firstOrder[0]], first[firstOrder[1]], first[firstOrder[2]], second[secondOrder[0]], second[secondOrder[1]], second[secondOrder[2]]), expected);
                EXPECT_EQ(TriangleDoubleTests::Intersects(second[secondOrder[0]], second[secondOrder[1]], second[secondOrder[2]], first[firstOrder[0]], first[firstOrder[1]], first[firstOrder[2]]), expected);
            }
        }
    }
}

static void CheckBothPrecisions(const f64 (&first)[3][3], const f64 (&second)[3][3], const bool expected){
    SIMDVectorSingle firstSingle[3], secondSingle[3];
    SIMDVectorDouble firstDouble[3], secondDouble[3];
    for(u32 index = 0u; index < 3u; ++index){
        firstSingle[index] = VectorSet(static_cast<f32>(first[index][0]), static_cast<f32>(first[index][1]), static_cast<f32>(first[index][2]), 0.0f);
        secondSingle[index] = VectorSet(static_cast<f32>(second[index][0]), static_cast<f32>(second[index][1]), static_cast<f32>(second[index][2]), 0.0f);
        firstDouble[index] = VectorDoubleSet(first[index][0], first[index][1], first[index][2], 0.0);
        secondDouble[index] = VectorDoubleSet(second[index][0], second[index][1], second[index][2], 0.0);
    }
    CheckWindingAndOperandOrder(firstSingle, secondSingle, expected);
    CheckWindingAndOperandOrder(firstDouble, secondDouble, expected);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MathTriangleIntersection, RejectsSeparatedCoplanarTrianglesWithOverlappingBounds){
    const f64 first[][3] = { { 0.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 }, { 0.0, 2.0, 0.0 } };
    const f64 second[][3] = { { 1.5, 1.5, 0.0 }, { 2.0, 1.5, 0.0 }, { 1.5, 2.0, 0.0 } };
    CheckBothPrecisions(first, second, false);
}

TEST(MathTriangleIntersection, RejectsSeparatedNoncoplanarTrianglesWithOverlappingBounds){
    const f64 first[][3] = { { 0.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 }, { 0.0, 2.0, 0.0 } };
    const f64 second[][3] = { { 1.5, 1.5, -1.0 }, { 1.5, 2.0, 1.0 }, { 2.0, 1.5, 1.0 } };
    CheckBothPrecisions(first, second, false);
}

TEST(MathTriangleIntersection, CoplanarOverlapAndContainmentIgnoreWinding){
    const f64 first[][3] = { { 0.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 }, { 0.0, 2.0, 0.0 } };
    const f64 overlapping[][3] = { { 0.5, 0.5, 0.0 }, { 2.5, 0.5, 0.0 }, { 0.5, 2.5, 0.0 } };
    const f64 contained[][3] = { { 0.25, 0.25, 0.0 }, { 0.75, 0.25, 0.0 }, { 0.25, 0.75, 0.0 } };
    CheckBothPrecisions(first, overlapping, true);
    CheckBothPrecisions(first, contained, true);
}

TEST(MathTriangleIntersection, CoplanarSharedEdgeAndPointRemainIntersections){
    const f64 first[][3] = { { 0.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 }, { 0.0, 2.0, 0.0 } };
    const f64 sharedEdge[][3] = { { 2.0, 0.0, 0.0 }, { 0.0, 2.0, 0.0 }, { 2.0, 2.0, 0.0 } };
    const f64 sharedPoint[][3] = { { 2.0, 0.0, 0.0 }, { 3.0, 0.0, 0.0 }, { 2.0, 1.0, 0.0 } };
    CheckBothPrecisions(first, sharedEdge, true);
    CheckBothPrecisions(first, sharedPoint, true);
}

TEST(MathTriangleIntersection, NoncoplanarCrossingRetainsTheVertexOnTheOppositePlane){
    const f64 first[][3] = { { 0.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 }, { 0.0, 2.0, 0.0 } };
    const f64 crossing[][3] = { { 0.5, 0.5, -1.0 }, { 0.5, 0.5, 1.0 }, { 1.5, 0.5, 0.0 } };
    CheckBothPrecisions(first, crossing, true);
}

TEST(MathTriangleIntersection, DegeneratePointOutsideTheTriangleIsRejectedInsideItsBounds){
    const f64 first[][3] = { { 0.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 }, { 0.0, 2.0, 0.0 } };
    const f64 outside[][3] = { { 1.5, 1.5, 0.0 }, { 1.5, 1.5, 0.0 }, { 1.5, 1.5, 0.0 } };
    const f64 inside[][3] = { { 0.5, 0.5, 0.0 }, { 0.5, 0.5, 0.0 }, { 0.5, 0.5, 0.0 } };
    const f64 touching[][3] = { { 2.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 } };
    CheckBothPrecisions(first, outside, false);
    CheckBothPrecisions(first, inside, true);
    CheckBothPrecisions(first, touching, true);
}

TEST(MathTriangleIntersection, DegenerateSegmentOutsideTheTriangleIsRejectedInsideItsBounds){
    const f64 first[][3] = { { 0.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 }, { 0.0, 2.0, 0.0 } };
    const f64 outside[][3] = { { 1.5, 1.5, 0.0 }, { 1.75, 1.5, 0.0 }, { 2.0, 1.5, 0.0 } };
    const f64 crossing[][3] = { { -1.0, 1.0, 0.0 }, { 0.5, 1.0, 0.0 }, { 2.0, 1.0, 0.0 } };
    CheckBothPrecisions(first, outside, false);
    CheckBothPrecisions(first, crossing, true);
}

TEST(MathTriangleIntersection, DegenerateParallelSegmentsRequireTheSameLine){
    const f64 first[][3] = { { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 0.0 }, { 2.0, 2.0, 0.0 } };
    const f64 displaced[][3] = { { 0.0, 1.0, 0.0 }, { 0.5, 1.5, 0.0 }, { 1.0, 2.0, 0.0 } };
    const f64 overlapping[][3] = { { 1.0, 1.0, 0.0 }, { 2.0, 2.0, 0.0 }, { 3.0, 3.0, 0.0 } };
    const f64 touching[][3] = { { 2.0, 2.0, 0.0 }, { 3.0, 3.0, 0.0 }, { 4.0, 4.0, 0.0 } };
    CheckBothPrecisions(first, displaced, false);
    CheckBothPrecisions(first, overlapping, true);
    CheckBothPrecisions(first, touching, true);
}

TEST(MathTriangleIntersection, DegenerateSkewSegmentsAreRejectedDespiteOverlappingBounds){
    const f64 first[][3] = { { 0.0, 0.0, 0.0 }, { 1.0, 1.0, 1.0 }, { 2.0, 2.0, 2.0 } };
    const f64 skew[][3] = { { 0.0, 2.0, 0.0 }, { 1.0, 1.0, 0.5 }, { 2.0, 0.0, 1.0 } };
    const f64 crossing[][3] = { { 0.0, 2.0, 0.0 }, { 1.0, 1.0, 1.0 }, { 2.0, 0.0, 2.0 } };
    CheckBothPrecisions(first, skew, false);
    CheckBothPrecisions(first, crossing, true);
}

TEST(MathTriangleIntersection, TinyCoplanarTrianglesRetainSeparatingAxes){
    constexpr f64 s_Scale = 0x1p-60;
    const f64 first[][3] = { { 0.0, 0.0, 0.0 }, { 2.0 * s_Scale, 0.0, 0.0 }, { 0.0, 2.0 * s_Scale, 0.0 } };
    const f64 second[][3] = { { 1.5 * s_Scale, 1.5 * s_Scale, 0.0 }, { 2.0 * s_Scale, 1.5 * s_Scale, 0.0 }, { 1.5 * s_Scale, 2.0 * s_Scale, 0.0 } };
    CheckBothPrecisions(first, second, false);
}

TEST(MathTriangleIntersection, DoubleCoplanarSeparationAndTouchSurviveLargeTranslation){
    constexpr f64 s_Base = 0x1p+40;
    const SIMDVectorDouble first[] = { VectorDoubleSet(s_Base, s_Base, 0.0, 0.0), VectorDoubleSet(s_Base + 2.0, s_Base, 0.0, 0.0), VectorDoubleSet(s_Base, s_Base + 2.0, 0.0, 0.0) };
    const SIMDVectorDouble separated[] = { VectorDoubleSet(s_Base + 1.5, s_Base + 1.5, 0.0, 0.0), VectorDoubleSet(s_Base + 2.0, s_Base + 1.5, 0.0, 0.0), VectorDoubleSet(s_Base + 1.5, s_Base + 2.0, 0.0, 0.0) };
    const SIMDVectorDouble touching[] = { VectorDoubleSet(s_Base + 2.0, s_Base, 0.0, 0.0), VectorDoubleSet(s_Base + 3.0, s_Base, 0.0, 0.0), VectorDoubleSet(s_Base + 2.0, s_Base + 1.0, 0.0, 0.0) };
    CheckWindingAndOperandOrder(first, separated, false);
    CheckWindingAndOperandOrder(first, touching, true);
}

TEST(MathTriangleIntersection, InactiveSignalingNaNsDoNotRaiseInvalidDuringIntersection){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    });
    volatile u32 singleInactiveBits = 0x7f800001u;
    volatile u64 doubleInactiveBits = 0x7ff0000000000001u;
    const SIMDVectorSingle firstSingle[] = {
        VectorSetIntW(VectorSet(0.0f, 0.0f, 0.0f, 0.0f), singleInactiveBits),
        VectorSetIntW(VectorSet(2.0f, 0.0f, 0.0f, 0.0f), singleInactiveBits),
        VectorSetIntW(VectorSet(0.0f, 2.0f, 0.0f, 0.0f), singleInactiveBits)
    };
    const SIMDVectorDouble firstDouble[] = {
        VectorDoubleSetIntW(VectorDoubleSet(0.0, 0.0, 0.0, 0.0), doubleInactiveBits),
        VectorDoubleSetIntW(VectorDoubleSet(2.0, 0.0, 0.0, 0.0), doubleInactiveBits),
        VectorDoubleSetIntW(VectorDoubleSet(0.0, 2.0, 0.0, 0.0), doubleInactiveBits)
    };
    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const bool singleIntersects = TriangleTests::Intersects(firstSingle[0], firstSingle[1], firstSingle[2], firstSingle[0], firstSingle[1], firstSingle[2]);
    const i32 singleInvalid = TestFloatingPointExceptions(FE_INVALID);
    EXPECT_TRUE(singleIntersects);
    EXPECT_EQ(singleInvalid, 0);
    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const bool doubleIntersects = TriangleDoubleTests::Intersects(firstDouble[0], firstDouble[1], firstDouble[2], firstDouble[0], firstDouble[1], firstDouble[2]);
    const i32 doubleInvalid = TestFloatingPointExceptions(FE_INVALID);
    EXPECT_TRUE(doubleIntersects);
    EXPECT_EQ(doubleInvalid, 0);
}

TEST(MathTriangleIntersection, ExactlyContainedDyadicPointSurvivesSingleProjectionCancellation){
    const f64 first[][3] = { { 824370.0, 14961.0, -10577.0 }, { -393548.0, -764217.0, 231491.0 }, { -838274.0, -740881.0, -157088.0 } };
    // Each coordinate is exactly representable in both precisions; the point equals (2*A0 + A1 + A2)/4.
    const f64 contained[][3] = { { 104229.5, -368794.0, 13312.25 }, { 104229.5, -368794.0, 13312.25 }, { 104229.5, -368794.0, 13312.25 } };
    CheckBothPrecisions(first, contained, true);
}

TEST(MathTriangleIntersection, ExactlyContainedDyadicPointSurvivesDoubleProjectionCancellation){
    const SIMDVectorDouble first[] = {
        VectorDoubleSet(495456340.0, -271487682.75, 212747462.0, 0.0),
        VectorDoubleSet(48651637.25, -531182607.0, 473308109.25, 0.0),
        VectorDoubleSet(-155700933.5, -479176747.0, 416710936.25, 0.0)
    };
    // The exact convex combination (2*A0 + A1 + A2)/4 is coplanar, even when the computed dot products disagree near zero.
    const SIMDVectorDouble contained[] = {
        VectorDoubleSet(220965845.9375, -388333679.875, 328878492.375, 0.0),
        VectorDoubleSet(220965845.9375, -388333679.875, 328878492.375, 0.0),
        VectorDoubleSet(220965845.9375, -388333679.875, 328878492.375, 0.0)
    };
    CheckWindingAndOperandOrder(first, contained, true);
}

TEST(MathTriangleIntersection, ProjectionRoundoffAllowancePreservesSmallRealGaps){
    constexpr f64 s_SingleGap = 0x1p-20;
    const f64 first[][3] = { { 0.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 }, { 0.0, 2.0, 0.0 } };
    const f64 outside[][3] = { { 1.0 + s_SingleGap, 1.0, 0.0 }, { 1.0 + s_SingleGap, 1.0, 0.0 }, { 1.0 + s_SingleGap, 1.0, 0.0 } };
    const f64 sloped[][3] = { { 0.0, 0.0, 0.0 }, { 2.0, 0.0, 2.0 }, { 0.0, 2.0, -2.0 } };
    const f64 displaced[][3] = { { 0.0, 0.0, s_SingleGap }, { 2.0, 0.0, 2.0 + s_SingleGap }, { 0.0, 2.0, -2.0 + s_SingleGap } };
    CheckBothPrecisions(first, outside, false);
    CheckBothPrecisions(sloped, displaced, false);

    constexpr f64 s_DoubleGap = 0x1p-40;
    const SIMDVectorDouble firstDouble[] = { VectorDoubleSet(0.0, 0.0, 0.0, 0.0), VectorDoubleSet(2.0, 0.0, 0.0, 0.0), VectorDoubleSet(0.0, 2.0, 0.0, 0.0) };
    const SIMDVectorDouble outsideDouble[] = { VectorDoubleSet(1.0 + s_DoubleGap, 1.0, 0.0, 0.0), VectorDoubleSet(1.0 + s_DoubleGap, 1.0, 0.0, 0.0), VectorDoubleSet(1.0 + s_DoubleGap, 1.0, 0.0, 0.0) };
    const SIMDVectorDouble slopedDouble[] = { VectorDoubleSet(0.0, 0.0, 0.0, 0.0), VectorDoubleSet(2.0, 0.0, 2.0, 0.0), VectorDoubleSet(0.0, 2.0, -2.0, 0.0) };
    const SIMDVectorDouble displacedDouble[] = { VectorDoubleSet(0.0, 0.0, s_DoubleGap, 0.0), VectorDoubleSet(2.0, 0.0, 2.0 + s_DoubleGap, 0.0), VectorDoubleSet(0.0, 2.0, -2.0 + s_DoubleGap, 0.0) };
    CheckWindingAndOperandOrder(firstDouble, outsideDouble, false);
    CheckWindingAndOperandOrder(slopedDouble, displacedDouble, false);
}

TEST(MathTriangleIntersection, ProjectionRoundoffAllowancePreservesTranslatedOneUlpGaps){
    constexpr f64 s_SingleBase = 0x1p+20, s_SingleGap = 0.125;
    const f64 first[][3] = { { s_SingleBase, s_SingleBase, 0.0 }, { s_SingleBase + 2.0, s_SingleBase, 0.0 }, { s_SingleBase, s_SingleBase + 2.0, 0.0 } };
    const f64 point[] = { s_SingleBase + 1.0 + s_SingleGap, s_SingleBase + 1.0, 0.0 };
    const f64 outside[][3] = { { point[0], point[1], point[2] }, { point[0], point[1], point[2] }, { point[0], point[1], point[2] } };
    CheckBothPrecisions(first, outside, false);

    constexpr f64 s_DoubleBase = 0x1p+40, s_DoubleGap = 0x1p-12;
    const SIMDVectorDouble firstDouble[] = { VectorDoubleSet(s_DoubleBase, s_DoubleBase, 0.0, 0.0), VectorDoubleSet(s_DoubleBase + 2.0, s_DoubleBase, 0.0, 0.0), VectorDoubleSet(s_DoubleBase, s_DoubleBase + 2.0, 0.0, 0.0) };
    const SIMDVectorDouble outsideDouble[] = {
        VectorDoubleSet(s_DoubleBase + 1.0 + s_DoubleGap, s_DoubleBase + 1.0, 0.0, 0.0),
        VectorDoubleSet(s_DoubleBase + 1.0 + s_DoubleGap, s_DoubleBase + 1.0, 0.0, 0.0),
        VectorDoubleSet(s_DoubleBase + 1.0 + s_DoubleGap, s_DoubleBase + 1.0, 0.0, 0.0)
    };
    CheckWindingAndOperandOrder(firstDouble, outsideDouble, false);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

