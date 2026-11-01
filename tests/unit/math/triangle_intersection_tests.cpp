// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/module.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <global/floating_point_environment.h>
#include <global/simd/collision_triangle.h>
#include <global/simd/convert.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_math_triangle_intersection_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using VertexCoords = Array<f64, 3>;


template<typename VectorT>
static void CheckWindingAndOperandOrder(const Span<const VectorT> first, const Span<const VectorT> second, const b8 expected){
    constexpr Array<Array<u32, 3>, 6> s_Permutations = {{ { 0u, 1u, 2u }, { 0u, 2u, 1u }, { 1u, 0u, 2u }, { 1u, 2u, 0u }, { 2u, 0u, 1u }, { 2u, 1u, 0u } }};
    for(const auto& firstOrder : s_Permutations){
        for(const auto& secondOrder : s_Permutations){
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

static void CheckBothPrecisions(const Span<const VertexCoords> first, const Span<const VertexCoords> second, const b8 expected){
    Array<SIMDVectorSingle, 3> firstSingle{}, secondSingle{};
    Array<SIMDVectorDouble, 3> firstDouble{}, secondDouble{};
    for(u32 index = 0u; index < 3u; ++index){
        firstDouble[index] = VectorDoubleSet(first[index][0], first[index][1], first[index][2], 0.0);
        secondDouble[index] = VectorDoubleSet(second[index][0], second[index][1], second[index][2], 0.0);
        firstSingle[index] = ConvertVectorDoubleToSingle(firstDouble[index]);
        secondSingle[index] = ConvertVectorDoubleToSingle(secondDouble[index]);
    }
    CheckWindingAndOperandOrder<SIMDVectorSingle>(firstSingle, secondSingle, expected);
    CheckWindingAndOperandOrder<SIMDVectorDouble>(firstDouble, secondDouble, expected);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MathTriangleIntersection, RejectsSeparatedCoplanarTrianglesWithOverlappingBounds){
    const Array<VertexCoords, 3> first = { VertexCoords{ 0.0, 0.0, 0.0 }, VertexCoords{ 2.0, 0.0, 0.0 }, VertexCoords{ 0.0, 2.0, 0.0 } };
    const Array<VertexCoords, 3> second = { VertexCoords{ 1.5, 1.5, 0.0 }, VertexCoords{ 2.0, 1.5, 0.0 }, VertexCoords{ 1.5, 2.0, 0.0 } };
    CheckBothPrecisions(first, second, false);
}

TEST(MathTriangleIntersection, RejectsSeparatedNoncoplanarTrianglesWithOverlappingBounds){
    const Array<VertexCoords, 3> first = { VertexCoords{ 0.0, 0.0, 0.0 }, VertexCoords{ 2.0, 0.0, 0.0 }, VertexCoords{ 0.0, 2.0, 0.0 } };
    const Array<VertexCoords, 3> second = { VertexCoords{ 1.5, 1.5, -1.0 }, VertexCoords{ 1.5, 2.0, 1.0 }, VertexCoords{ 2.0, 1.5, 1.0 } };
    CheckBothPrecisions(first, second, false);
}

TEST(MathTriangleIntersection, CoplanarOverlapAndContainmentIgnoreWinding){
    const Array<VertexCoords, 3> first = { VertexCoords{ 0.0, 0.0, 0.0 }, VertexCoords{ 2.0, 0.0, 0.0 }, VertexCoords{ 0.0, 2.0, 0.0 } };
    const Array<VertexCoords, 3> overlapping = { VertexCoords{ 0.5, 0.5, 0.0 }, VertexCoords{ 2.5, 0.5, 0.0 }, VertexCoords{ 0.5, 2.5, 0.0 } };
    const Array<VertexCoords, 3> contained = { VertexCoords{ 0.25, 0.25, 0.0 }, VertexCoords{ 0.75, 0.25, 0.0 }, VertexCoords{ 0.25, 0.75, 0.0 } };
    CheckBothPrecisions(first, overlapping, true);
    CheckBothPrecisions(first, contained, true);
}

TEST(MathTriangleIntersection, CoplanarSharedEdgeAndPointRemainIntersections){
    const Array<VertexCoords, 3> first = { VertexCoords{ 0.0, 0.0, 0.0 }, VertexCoords{ 2.0, 0.0, 0.0 }, VertexCoords{ 0.0, 2.0, 0.0 } };
    const Array<VertexCoords, 3> sharedEdge = { VertexCoords{ 2.0, 0.0, 0.0 }, VertexCoords{ 0.0, 2.0, 0.0 }, VertexCoords{ 2.0, 2.0, 0.0 } };
    const Array<VertexCoords, 3> sharedPoint = { VertexCoords{ 2.0, 0.0, 0.0 }, VertexCoords{ 3.0, 0.0, 0.0 }, VertexCoords{ 2.0, 1.0, 0.0 } };
    CheckBothPrecisions(first, sharedEdge, true);
    CheckBothPrecisions(first, sharedPoint, true);
}

TEST(MathTriangleIntersection, NoncoplanarCrossingRetainsTheVertexOnTheOppositePlane){
    const Array<VertexCoords, 3> first = { VertexCoords{ 0.0, 0.0, 0.0 }, VertexCoords{ 2.0, 0.0, 0.0 }, VertexCoords{ 0.0, 2.0, 0.0 } };
    const Array<VertexCoords, 3> crossing = { VertexCoords{ 0.5, 0.5, -1.0 }, VertexCoords{ 0.5, 0.5, 1.0 }, VertexCoords{ 1.5, 0.5, 0.0 } };
    CheckBothPrecisions(first, crossing, true);
}

TEST(MathTriangleIntersection, DegeneratePointOutsideTheTriangleIsRejectedInsideItsBounds){
    const Array<VertexCoords, 3> first = { VertexCoords{ 0.0, 0.0, 0.0 }, VertexCoords{ 2.0, 0.0, 0.0 }, VertexCoords{ 0.0, 2.0, 0.0 } };
    const Array<VertexCoords, 3> outside = { VertexCoords{ 1.5, 1.5, 0.0 }, VertexCoords{ 1.5, 1.5, 0.0 }, VertexCoords{ 1.5, 1.5, 0.0 } };
    const Array<VertexCoords, 3> inside = { VertexCoords{ 0.5, 0.5, 0.0 }, VertexCoords{ 0.5, 0.5, 0.0 }, VertexCoords{ 0.5, 0.5, 0.0 } };
    const Array<VertexCoords, 3> touching = { VertexCoords{ 2.0, 0.0, 0.0 }, VertexCoords{ 2.0, 0.0, 0.0 }, VertexCoords{ 2.0, 0.0, 0.0 } };
    CheckBothPrecisions(first, outside, false);
    CheckBothPrecisions(first, inside, true);
    CheckBothPrecisions(first, touching, true);
}

TEST(MathTriangleIntersection, DegenerateSegmentOutsideTheTriangleIsRejectedInsideItsBounds){
    const Array<VertexCoords, 3> first = { VertexCoords{ 0.0, 0.0, 0.0 }, VertexCoords{ 2.0, 0.0, 0.0 }, VertexCoords{ 0.0, 2.0, 0.0 } };
    const Array<VertexCoords, 3> outside = { VertexCoords{ 1.5, 1.5, 0.0 }, VertexCoords{ 1.75, 1.5, 0.0 }, VertexCoords{ 2.0, 1.5, 0.0 } };
    const Array<VertexCoords, 3> crossing = { VertexCoords{ -1.0, 1.0, 0.0 }, VertexCoords{ 0.5, 1.0, 0.0 }, VertexCoords{ 2.0, 1.0, 0.0 } };
    CheckBothPrecisions(first, outside, false);
    CheckBothPrecisions(first, crossing, true);
}

TEST(MathTriangleIntersection, DegenerateParallelSegmentsRequireTheSameLine){
    const Array<VertexCoords, 3> first = { VertexCoords{ 0.0, 0.0, 0.0 }, VertexCoords{ 1.0, 1.0, 0.0 }, VertexCoords{ 2.0, 2.0, 0.0 } };
    const Array<VertexCoords, 3> displaced = { VertexCoords{ 0.0, 1.0, 0.0 }, VertexCoords{ 0.5, 1.5, 0.0 }, VertexCoords{ 1.0, 2.0, 0.0 } };
    const Array<VertexCoords, 3> overlapping = { VertexCoords{ 1.0, 1.0, 0.0 }, VertexCoords{ 2.0, 2.0, 0.0 }, VertexCoords{ 3.0, 3.0, 0.0 } };
    const Array<VertexCoords, 3> touching = { VertexCoords{ 2.0, 2.0, 0.0 }, VertexCoords{ 3.0, 3.0, 0.0 }, VertexCoords{ 4.0, 4.0, 0.0 } };
    CheckBothPrecisions(first, displaced, false);
    CheckBothPrecisions(first, overlapping, true);
    CheckBothPrecisions(first, touching, true);
}

TEST(MathTriangleIntersection, DegenerateSkewSegmentsAreRejectedDespiteOverlappingBounds){
    const Array<VertexCoords, 3> first = { VertexCoords{ 0.0, 0.0, 0.0 }, VertexCoords{ 1.0, 1.0, 1.0 }, VertexCoords{ 2.0, 2.0, 2.0 } };
    const Array<VertexCoords, 3> skew = { VertexCoords{ 0.0, 2.0, 0.0 }, VertexCoords{ 1.0, 1.0, 0.5 }, VertexCoords{ 2.0, 0.0, 1.0 } };
    const Array<VertexCoords, 3> crossing = { VertexCoords{ 0.0, 2.0, 0.0 }, VertexCoords{ 1.0, 1.0, 1.0 }, VertexCoords{ 2.0, 0.0, 2.0 } };
    CheckBothPrecisions(first, skew, false);
    CheckBothPrecisions(first, crossing, true);
}

TEST(MathTriangleIntersection, TinyCoplanarTrianglesRetainSeparatingAxes){
    constexpr f64 s_Scale = 0x1p-60;
    const Array<VertexCoords, 3> first = { VertexCoords{ 0.0, 0.0, 0.0 }, VertexCoords{ 2.0 * s_Scale, 0.0, 0.0 }, VertexCoords{ 0.0, 2.0 * s_Scale, 0.0 } };
    const Array<VertexCoords, 3> second = { VertexCoords{ 1.5 * s_Scale, 1.5 * s_Scale, 0.0 }, VertexCoords{ 2.0 * s_Scale, 1.5 * s_Scale, 0.0 }, VertexCoords{ 1.5 * s_Scale, 2.0 * s_Scale, 0.0 } };
    CheckBothPrecisions(first, second, false);
}

TEST(MathTriangleIntersection, DoubleCoplanarSeparationAndTouchSurviveLargeTranslation){
    constexpr f64 s_Base = 0x1p+40;
    const Array<SIMDVectorDouble, 3> first = { VectorDoubleSet(s_Base, s_Base, 0.0, 0.0), VectorDoubleSet(s_Base + 2.0, s_Base, 0.0, 0.0), VectorDoubleSet(s_Base, s_Base + 2.0, 0.0, 0.0) };
    const Array<SIMDVectorDouble, 3> separated = { VectorDoubleSet(s_Base + 1.5, s_Base + 1.5, 0.0, 0.0), VectorDoubleSet(s_Base + 2.0, s_Base + 1.5, 0.0, 0.0), VectorDoubleSet(s_Base + 1.5, s_Base + 2.0, 0.0, 0.0) };
    const Array<SIMDVectorDouble, 3> touching = { VectorDoubleSet(s_Base + 2.0, s_Base, 0.0, 0.0), VectorDoubleSet(s_Base + 3.0, s_Base, 0.0, 0.0), VectorDoubleSet(s_Base + 2.0, s_Base + 1.0, 0.0, 0.0) };
    CheckWindingAndOperandOrder<SIMDVectorDouble>(first, separated, false);
    CheckWindingAndOperandOrder<SIMDVectorDouble>(first, touching, true);
}

TEST(MathTriangleIntersection, InactiveSignalingNaNsDoNotRaiseInvalidDuringIntersection){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    NWB::Tests::FloatingPointEnvironmentGuard restoreEnvironment(*originalEnvironment);
    volatile u32 singleInactiveBits = 0x7f800001u;
    volatile u64 doubleInactiveBits = 0x7ff0000000000001u;
    const Array<SIMDVectorSingle, 3> firstSingle = {
        VectorSetIntW(VectorSet(0.0f, 0.0f, 0.0f, 0.0f), singleInactiveBits),
        VectorSetIntW(VectorSet(2.0f, 0.0f, 0.0f, 0.0f), singleInactiveBits),
        VectorSetIntW(VectorSet(0.0f, 2.0f, 0.0f, 0.0f), singleInactiveBits)
    };
    const Array<SIMDVectorDouble, 3> firstDouble = {
        VectorDoubleSetIntW(VectorDoubleSet(0.0, 0.0, 0.0, 0.0), doubleInactiveBits),
        VectorDoubleSetIntW(VectorDoubleSet(2.0, 0.0, 0.0, 0.0), doubleInactiveBits),
        VectorDoubleSetIntW(VectorDoubleSet(0.0, 2.0, 0.0, 0.0), doubleInactiveBits)
    };
    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const b8 singleIntersects = TriangleTests::Intersects(firstSingle[0], firstSingle[1], firstSingle[2], firstSingle[0], firstSingle[1], firstSingle[2]);
    const i32 singleInvalid = TestFloatingPointExceptions(FE_INVALID);
    EXPECT_TRUE(singleIntersects);
    EXPECT_EQ(singleInvalid, 0);
    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const b8 doubleIntersects = TriangleDoubleTests::Intersects(firstDouble[0], firstDouble[1], firstDouble[2], firstDouble[0], firstDouble[1], firstDouble[2]);
    const i32 doubleInvalid = TestFloatingPointExceptions(FE_INVALID);
    EXPECT_TRUE(doubleIntersects);
    EXPECT_EQ(doubleInvalid, 0);
}

TEST(MathTriangleIntersection, ExactlyContainedDyadicPointSurvivesSingleProjectionCancellation){
    const Array<VertexCoords, 3> first = { VertexCoords{ 824370.0, 14961.0, -10577.0 }, VertexCoords{ -393548.0, -764217.0, 231491.0 }, VertexCoords{ -838274.0, -740881.0, -157088.0 } };
    // Exact coplanar interior point at the weighted average of A0, A1, and A2.
    const Array<VertexCoords, 3> contained = { VertexCoords{ 104229.5, -368794.0, 13312.25 }, VertexCoords{ 104229.5, -368794.0, 13312.25 }, VertexCoords{ 104229.5, -368794.0, 13312.25 } };
    CheckBothPrecisions(first, contained, true);
}

TEST(MathTriangleIntersection, ExactlyContainedDyadicPointSurvivesDoubleProjectionCancellation){
    const Array<SIMDVectorDouble, 3> first = {
        VectorDoubleSet(495456340.0, -271487682.75, 212747462.0, 0.0),
        VectorDoubleSet(48651637.25, -531182607.0, 473308109.25, 0.0),
        VectorDoubleSet(-155700933.5, -479176747.0, 416710936.25, 0.0)
    };
    // Interior point remains coplanar even when near-zero dot products round differently.
    const Array<SIMDVectorDouble, 3> contained = {
        VectorDoubleSet(220965845.9375, -388333679.875, 328878492.375, 0.0),
        VectorDoubleSet(220965845.9375, -388333679.875, 328878492.375, 0.0),
        VectorDoubleSet(220965845.9375, -388333679.875, 328878492.375, 0.0)
    };
    CheckWindingAndOperandOrder<SIMDVectorDouble>(first, contained, true);
}

TEST(MathTriangleIntersection, ProjectionRoundoffAllowancePreservesSmallRealGaps){
    constexpr f64 s_SingleGap = 0x1p-20;
    const Array<VertexCoords, 3> first = { VertexCoords{ 0.0, 0.0, 0.0 }, VertexCoords{ 2.0, 0.0, 0.0 }, VertexCoords{ 0.0, 2.0, 0.0 } };
    const Array<VertexCoords, 3> outside = { VertexCoords{ 1.0 + s_SingleGap, 1.0, 0.0 }, VertexCoords{ 1.0 + s_SingleGap, 1.0, 0.0 }, VertexCoords{ 1.0 + s_SingleGap, 1.0, 0.0 } };
    const Array<VertexCoords, 3> sloped = { VertexCoords{ 0.0, 0.0, 0.0 }, VertexCoords{ 2.0, 0.0, 2.0 }, VertexCoords{ 0.0, 2.0, -2.0 } };
    const Array<VertexCoords, 3> displaced = { VertexCoords{ 0.0, 0.0, s_SingleGap }, VertexCoords{ 2.0, 0.0, 2.0 + s_SingleGap }, VertexCoords{ 0.0, 2.0, -2.0 + s_SingleGap } };
    CheckBothPrecisions(first, outside, false);
    CheckBothPrecisions(sloped, displaced, false);

    constexpr f64 s_DoubleGap = 0x1p-40;
    const Array<SIMDVectorDouble, 3> firstDouble = { VectorDoubleSet(0.0, 0.0, 0.0, 0.0), VectorDoubleSet(2.0, 0.0, 0.0, 0.0), VectorDoubleSet(0.0, 2.0, 0.0, 0.0) };
    const Array<SIMDVectorDouble, 3> outsideDouble = { VectorDoubleSet(1.0 + s_DoubleGap, 1.0, 0.0, 0.0), VectorDoubleSet(1.0 + s_DoubleGap, 1.0, 0.0, 0.0), VectorDoubleSet(1.0 + s_DoubleGap, 1.0, 0.0, 0.0) };
    const Array<SIMDVectorDouble, 3> slopedDouble = { VectorDoubleSet(0.0, 0.0, 0.0, 0.0), VectorDoubleSet(2.0, 0.0, 2.0, 0.0), VectorDoubleSet(0.0, 2.0, -2.0, 0.0) };
    const Array<SIMDVectorDouble, 3> displacedDouble = { VectorDoubleSet(0.0, 0.0, s_DoubleGap, 0.0), VectorDoubleSet(2.0, 0.0, 2.0 + s_DoubleGap, 0.0), VectorDoubleSet(0.0, 2.0, -2.0 + s_DoubleGap, 0.0) };
    CheckWindingAndOperandOrder<SIMDVectorDouble>(firstDouble, outsideDouble, false);
    CheckWindingAndOperandOrder<SIMDVectorDouble>(slopedDouble, displacedDouble, false);
}

TEST(MathTriangleIntersection, ProjectionRoundoffAllowancePreservesTranslatedOneUlpGaps){
    constexpr f64 s_SingleBase = 0x1p+20, s_SingleGap = 0.125;
    const Array<VertexCoords, 3> first = { VertexCoords{ s_SingleBase, s_SingleBase, 0.0 }, VertexCoords{ s_SingleBase + 2.0, s_SingleBase, 0.0 }, VertexCoords{ s_SingleBase, s_SingleBase + 2.0, 0.0 } };
    const VertexCoords point = { s_SingleBase + 1.0 + s_SingleGap, s_SingleBase + 1.0, 0.0 };
    const Array<VertexCoords, 3> outside = { point, point, point };
    CheckBothPrecisions(first, outside, false);

    constexpr f64 s_DoubleBase = 0x1p+40, s_DoubleGap = 0x1p-12;
    const Array<SIMDVectorDouble, 3> firstDouble = { VectorDoubleSet(s_DoubleBase, s_DoubleBase, 0.0, 0.0), VectorDoubleSet(s_DoubleBase + 2.0, s_DoubleBase, 0.0, 0.0), VectorDoubleSet(s_DoubleBase, s_DoubleBase + 2.0, 0.0, 0.0) };
    const Array<SIMDVectorDouble, 3> outsideDouble = {
        VectorDoubleSet(s_DoubleBase + 1.0 + s_DoubleGap, s_DoubleBase + 1.0, 0.0, 0.0),
        VectorDoubleSet(s_DoubleBase + 1.0 + s_DoubleGap, s_DoubleBase + 1.0, 0.0, 0.0),
        VectorDoubleSet(s_DoubleBase + 1.0 + s_DoubleGap, s_DoubleBase + 1.0, 0.0, 0.0)
    };
    CheckWindingAndOperandOrder<SIMDVectorDouble>(firstDouble, outsideDouble, false);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

