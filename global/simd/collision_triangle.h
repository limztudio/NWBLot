// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "matrix.h"
#include "collision_detail.h"
#include "collision_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TriangleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL EdgeCross2D(SIMDVectorSingle a, SIMDVectorSingle b, SIMDVectorSingle c)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL SignedArea2D(SIMDVectorSingle a, SIMDVectorSingle b, SIMDVectorSingle c)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL AreaNormal(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;
[[nodiscard]] bool NWB_SIMD_CALL ContainsPoint2D(SIMDVectorSingle point, SIMDVectorSingle a, SIMDVectorSingle b, SIMDVectorSingle c, SIMDVectorSingle tolerance)noexcept;

[[nodiscard]] Expected<f32> NWB_SIMD_CALL Intersects(SIMDVectorSingle origin, SIMDVectorSingle direction, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;

[[nodiscard]] bool NWB_SIMD_CALL Intersects(SIMDVectorSingle a0, SIMDVectorSingle a1, SIMDVectorSingle a2, SIMDVectorSingle b0, SIMDVectorSingle b1, SIMDVectorSingle b2)noexcept;

[[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL Intersects(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2, SIMDVectorSingle plane)noexcept;

[[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL ContainedBy(
    SIMDVectorSingle v0,
    SIMDVectorSingle v1,
    SIMDVectorSingle v2,
    SIMDVectorSingle plane0,
    SIMDVectorSingle plane1,
    SIMDVectorSingle plane2,
    SIMDVectorSingle plane3,
    SIMDVectorSingle plane4,
    SIMDVectorSingle plane5
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TriangleDoubleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL EdgeCross2D(SIMDVectorDouble a, SIMDVectorDouble b, SIMDVectorDouble c)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL SignedArea2D(SIMDVectorDouble a, SIMDVectorDouble b, SIMDVectorDouble c)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL AreaNormal(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
[[nodiscard]] bool NWB_SIMD_CALL ContainsPoint2D(SIMDVectorDouble point, SIMDVectorDouble a, SIMDVectorDouble b, SIMDVectorDouble c, SIMDVectorDouble tolerance)noexcept;

[[nodiscard]] Expected<f64> NWB_SIMD_CALL Intersects(SIMDVectorDouble origin, SIMDVectorDouble direction, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;

[[nodiscard]] bool NWB_SIMD_CALL Intersects(SIMDVectorDouble a0, SIMDVectorDouble a1, SIMDVectorDouble a2, SIMDVectorDouble b0, SIMDVectorDouble b1, SIMDVectorDouble b2)noexcept;

[[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL Intersects(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2, SIMDVectorDouble plane)noexcept;

[[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL ContainedBy(
    SIMDVectorDouble v0,
    SIMDVectorDouble v1,
    SIMDVectorDouble v2,
    SIMDVectorDouble plane0,
    SIMDVectorDouble plane1,
    SIMDVectorDouble plane2,
    SIMDVectorDouble plane3,
    SIMDVectorDouble plane4,
    SIMDVectorDouble plane5
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL TriangleTests::EdgeCross2D(const SIMDVectorSingle a, const SIMDVectorSingle b, const SIMDVectorSingle c)noexcept{
    const SIMDVectorSingle ab = VectorSubtract(b, a);
    const SIMDVectorSingle ac = VectorSubtract(c, a);
    return VectorSubtract(VectorMultiply(VectorSplatX(ab), VectorSplatY(ac)), VectorMultiply(VectorSplatY(ab), VectorSplatX(ac)));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL TriangleDoubleTests::EdgeCross2D(const SIMDVectorDouble a, const SIMDVectorDouble b, const SIMDVectorDouble c)noexcept{
    const SIMDVectorDouble ab = VectorDoubleSubtract(b, a);
    const SIMDVectorDouble ac = VectorDoubleSubtract(c, a);
    return VectorDoubleSubtract(VectorDoubleMultiply(VectorDoubleSplatX(ab), VectorDoubleSplatY(ac)), VectorDoubleMultiply(VectorDoubleSplatY(ab), VectorDoubleSplatX(ac)));
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL TriangleTests::SignedArea2D(const SIMDVectorSingle a, const SIMDVectorSingle b, const SIMDVectorSingle c)noexcept{
    return VectorScale(TriangleTests::EdgeCross2D(a, b, c), CollisionDetail::s_Half);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL TriangleDoubleTests::SignedArea2D(const SIMDVectorDouble a, const SIMDVectorDouble b, const SIMDVectorDouble c)noexcept{
    return VectorDoubleScale(TriangleDoubleTests::EdgeCross2D(a, b, c), CollisionDoubleDetail::s_DoubleHalf);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL TriangleTests::AreaNormal(const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2)noexcept{
    return Vector3Cross(VectorSubtract(v1, v0), VectorSubtract(v2, v0));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL TriangleDoubleTests::AreaNormal(const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2)noexcept{
    return VectorDouble3Cross(VectorDoubleSubtract(v1, v0), VectorDoubleSubtract(v2, v0));
}


// Promote positions before subtracting so the area normal retains binary64 edge precision.
[[nodiscard]] NWB_INLINE SIMDVectorDouble BuildTriangleAreaNormal64(const SIMDVectorSingle a, const SIMDVectorSingle b, const SIMDVectorSingle c)noexcept{
    const SIMDVectorDouble origin = ConvertVectorSingleToDouble(VectorAndInt(a, s_SIMDMask3));
    const SIMDVectorDouble ab = VectorDoubleSubtract(ConvertVectorSingleToDouble(VectorAndInt(b, s_SIMDMask3)), origin);
    const SIMDVectorDouble ac = VectorDoubleSubtract(ConvertVectorSingleToDouble(VectorAndInt(c, s_SIMDMask3)), origin);
    const SIMDVectorDouble positive = VectorDoubleMultiply(VectorDoubleSwizzle<1, 2, 0, 3>(ab), VectorDoubleSwizzle<2, 0, 1, 3>(ac));
    const SIMDVectorDouble negative = VectorDoubleMultiply(VectorDoubleSwizzle<2, 0, 1, 3>(ab), VectorDoubleSwizzle<1, 2, 0, 3>(ac));
    return VectorDoubleAndInt(VectorDoubleSubtract(positive, negative), s_DoubleSIMDMask3);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble TriangleAreaNormalLengthSquared(SIMDVectorDouble areaNormal)noexcept{
#if defined(NWB_HAS_AVX2)
    // Retain the AVX2 reduction order: (x squared + z squared) + y squared.
    areaNormal = VectorDoubleSwizzle<0, 2, 1, 3>(areaNormal);
#endif
#if defined(NWB_HAS_NEON64)
    // The ARM64 final multiply-add follows the compiler's contraction policy.
    const SIMDVectorDouble z = VectorDoubleSplatZ(areaNormal);
    return VectorDoubleMultiplyAddExpression(z, z, VectorDouble2LengthSq(areaNormal));
#else
    return VectorDouble3LengthSq(areaNormal);
#endif
}

[[nodiscard]] NWB_INLINE bool TriangleHasArea(const SIMDVectorSingle a, const SIMDVectorSingle b, const SIMDVectorSingle c, const f64 triangleAreaLengthSquaredEpsilon)noexcept{
    return VectorDoubleGetX(TriangleAreaNormalLengthSquared(BuildTriangleAreaNormal64(a, b, c))) > triangleAreaLengthSquaredEpsilon;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL TriangleTests::ContainsPoint2D(const SIMDVectorSingle point, const SIMDVectorSingle a, const SIMDVectorSingle b, const SIMDVectorSingle c, const SIMDVectorSingle tolerance)noexcept{
    const SIMDVectorSingle negativeTolerance = VectorNegate(VectorSplatX(tolerance));
    const SIMDVectorSingle ab = TriangleTests::EdgeCross2D(a, b, point);
    const SIMDVectorSingle bc = TriangleTests::EdgeCross2D(b, c, point);
    const SIMDVectorSingle ca = TriangleTests::EdgeCross2D(c, a, point);
    SIMDVectorSingle inside = VectorGreaterOrEqual(ab, negativeTolerance);
    inside = VectorAndInt(inside, VectorGreaterOrEqual(bc, negativeTolerance));
    inside = VectorAndInt(inside, VectorGreaterOrEqual(ca, negativeTolerance));
    return CollisionDetail::Vector4AllTrue(inside);
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL TriangleDoubleTests::ContainsPoint2D(const SIMDVectorDouble point, const SIMDVectorDouble a, const SIMDVectorDouble b, const SIMDVectorDouble c, const SIMDVectorDouble tolerance)noexcept{
    const SIMDVectorDouble negativeTolerance = VectorDoubleNegate(VectorDoubleSplatX(tolerance));
    const SIMDVectorDouble ab = TriangleDoubleTests::EdgeCross2D(a, b, point);
    const SIMDVectorDouble bc = TriangleDoubleTests::EdgeCross2D(b, c, point);
    const SIMDVectorDouble ca = TriangleDoubleTests::EdgeCross2D(c, a, point);
    SIMDVectorDouble inside = VectorDoubleGreaterOrEqual(ab, negativeTolerance);
    inside = VectorDoubleAndInt(inside, VectorDoubleGreaterOrEqual(bc, negativeTolerance));
    inside = VectorDoubleAndInt(inside, VectorDoubleGreaterOrEqual(ca, negativeTolerance));
    return CollisionDoubleDetail::VectorDouble4AllTrue(inside);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL TriangleTests::Intersects(const SIMDVectorSingle origin, const SIMDVectorSingle direction, const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2)noexcept{
    return CollisionDetail::RayIntersectsTriangle(origin, direction, v0, v1, v2);
}

[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL TriangleDoubleTests::Intersects(const SIMDVectorDouble origin, const SIMDVectorDouble direction, const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2)noexcept{
    return CollisionDoubleDetail::RayIntersectsTriangle(origin, direction, v0, v1, v2);
}

[[nodiscard]] inline bool NWB_SIMD_CALL TriangleTests::Intersects(const SIMDVectorSingle a0, const SIMDVectorSingle a1, const SIMDVectorSingle a2, const SIMDVectorSingle b0, const SIMDVectorSingle b1, const SIMDVectorSingle b2)noexcept{
    // Promotion keeps the separating-axis products finite across the full binary32 coordinate range.
    return TriangleDoubleTests::Intersects(
        ConvertVectorSingleToDouble(VectorAndInt(a0, s_SIMDMask3)), ConvertVectorSingleToDouble(VectorAndInt(a1, s_SIMDMask3)), ConvertVectorSingleToDouble(VectorAndInt(a2, s_SIMDMask3)),
        ConvertVectorSingleToDouble(VectorAndInt(b0, s_SIMDMask3)), ConvertVectorSingleToDouble(VectorAndInt(b1, s_SIMDMask3)), ConvertVectorSingleToDouble(VectorAndInt(b2, s_SIMDMask3))
    );
}

[[nodiscard]] inline bool NWB_SIMD_CALL TriangleDoubleTests::Intersects(const SIMDVectorDouble a0, const SIMDVectorDouble a1, const SIMDVectorDouble a2, const SIMDVectorDouble b0, const SIMDVectorDouble b1, const SIMDVectorDouble b2)noexcept{
    const SIMDVectorDouble first0 = VectorDoubleAndInt(a0, s_DoubleSIMDMask3), first1 = VectorDoubleAndInt(a1, s_DoubleSIMDMask3), first2 = VectorDoubleAndInt(a2, s_DoubleSIMDMask3);
    const SIMDVectorDouble second0 = VectorDoubleAndInt(b0, s_DoubleSIMDMask3), second1 = VectorDoubleAndInt(b1, s_DoubleSIMDMask3), second2 = VectorDoubleAndInt(b2, s_DoubleSIMDMask3);
    const SIMDVectorDouble minA = VectorDoubleMin(first0, VectorDoubleMin(first1, first2)), maxA = VectorDoubleMax(first0, VectorDoubleMax(first1, first2));
    const SIMDVectorDouble minB = VectorDoubleMin(second0, VectorDoubleMin(second1, second2)), maxB = VectorDoubleMax(second0, VectorDoubleMax(second1, second2));
    if(!CollisionDoubleDetail::MinMaxIntersects(minA, maxA, minB, maxB))
        return false;

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble relativeA1 = VectorDoubleSubtract(first1, first0), relativeA2 = VectorDoubleSubtract(first2, first0);
    const SIMDVectorDouble relativeB0 = VectorDoubleSubtract(second0, first0), relativeB1 = VectorDoubleSubtract(second1, first0), relativeB2 = VectorDoubleSubtract(second2, first0);
    const auto differenceRoundoff = [](const SIMDVectorDouble point, const SIMDVectorDouble origin, const SIMDVectorDouble difference)noexcept{
        // TwoDiff recovers the subtraction residual without expanding the allowance with a large common translation.
        const SIMDVectorDouble virtualOrigin = VectorDoubleSubtract(point, difference), virtualPoint = VectorDoubleAdd(difference, virtualOrigin);
        return VectorDoubleAdd(VectorDoubleSubtract(point, virtualPoint), VectorDoubleSubtract(virtualOrigin, origin));
    };
    const SIMDVectorDouble roundoffA1 = differenceRoundoff(first1, first0, relativeA1), roundoffA2 = differenceRoundoff(first2, first0, relativeA2);
    const SIMDVectorDouble roundoffB0 = differenceRoundoff(second0, first0, relativeB0), roundoffB1 = differenceRoundoff(second1, first0, relativeB1), roundoffB2 = differenceRoundoff(second2, first0, relativeB2);
#if defined(NWB_HAS_AVX2)
    const SIMDVectorDouble absoluteA1 = VectorDoubleAbs(relativeA1), absoluteA2 = VectorDoubleAbs(relativeA2);
    const SIMDVectorDouble absoluteB0 = VectorDoubleAbs(relativeB0), absoluteB1 = VectorDoubleAbs(relativeB1), absoluteB2 = VectorDoubleAbs(relativeB2);
    const SIMDVectorDouble absoluteRoundoffA1 = VectorDoubleAbs(roundoffA1), absoluteRoundoffA2 = VectorDoubleAbs(roundoffA2);
    const SIMDVectorDouble absoluteRoundoffB0 = VectorDoubleAbs(roundoffB0), absoluteRoundoffB1 = VectorDoubleAbs(roundoffB1), absoluteRoundoffB2 = VectorDoubleAbs(roundoffB2);
#endif
    const SIMDVectorDouble edgeA0 = relativeA1, edgeA1 = VectorDoubleSubtract(first2, first1), edgeA2 = VectorDoubleNegate(relativeA2);
    const SIMDVectorDouble edgeB0 = VectorDoubleSubtract(second1, second0), edgeB1 = VectorDoubleSubtract(second2, second1), edgeB2 = VectorDoubleSubtract(second0, second2);
    const auto crossAxis = [](const SIMDVectorDouble first, const SIMDVectorDouble second)noexcept{
        // Separate products make parallel directions produce a zero axis instead of an explicit-FMA residual.
        const SIMDVectorDouble positive = VectorDoubleMultiply(VectorDoubleSwizzle<1, 2, 0, 3>(first), VectorDoubleSwizzle<2, 0, 1, 3>(second));
        const SIMDVectorDouble negative = VectorDoubleMultiply(VectorDoubleSwizzle<2, 0, 1, 3>(first), VectorDoubleSwizzle<1, 2, 0, 3>(second));
        return VectorDoubleSubtract(positive, negative);
    };
    const auto separated = [&](const SIMDVectorDouble axis)noexcept{
        if(VectorDouble3Equal(axis, zero))
            return false;
        const SIMDVectorDouble absoluteAxis = VectorDoubleAbs(axis);
#if defined(NWB_HAS_AVX2)
        const auto projectionInterval = [&](const SIMDVectorDouble relative, const SIMDVectorDouble absoluteRelative, const SIMDVectorDouble absoluteRoundoff)noexcept{
            const SIMDVectorDouble projection = VectorDouble3Dot(relative, axis);
            const SIMDVectorDouble magnitude = VectorDouble3Dot(absoluteRelative, absoluteAxis), coordinateError = VectorDouble3Dot(absoluteRoundoff, absoluteAxis);
#else
        const auto projectionInterval = [&](const SIMDVectorDouble relative, const SIMDVectorDouble roundoff)noexcept{
            const SIMDVectorDouble projection = VectorDouble3Dot(relative, axis);
            const SIMDVectorDouble magnitude = VectorDouble3Dot(VectorDoubleAbs(relative), absoluteAxis), coordinateError = VectorDouble3Dot(VectorDoubleAbs(roundoff), absoluteAxis);
#endif
            // Three products and two additions need an absolute cancellation bound; eight epsilons also cover rounding the bound and endpoints.
            const SIMDVectorDouble allowance = VectorDoubleMultiplyAddExpression(VectorDoubleAdd(magnitude, coordinateError), VectorDoubleReplicate(0x1p-49), coordinateError);
            return VectorDoubleSelect(VectorDoubleSubtract(projection, allowance), VectorDoubleAdd(projection, allowance), s_DoubleSIMDMaskY);
        };
#if defined(NWB_HAS_AVX2)
        const SIMDVectorDouble intervalA1 = projectionInterval(relativeA1, absoluteA1, absoluteRoundoffA1), intervalA2 = projectionInterval(relativeA2, absoluteA2, absoluteRoundoffA2);
#else
        const SIMDVectorDouble intervalA1 = projectionInterval(relativeA1, roundoffA1), intervalA2 = projectionInterval(relativeA2, roundoffA2);
#endif
        const SIMDVectorDouble minimumA = VectorDoubleMin(zero, VectorDoubleMin(VectorDoubleSplatX(intervalA1), VectorDoubleSplatX(intervalA2)));
        const SIMDVectorDouble maximumA = VectorDoubleMax(zero, VectorDoubleMax(VectorDoubleSplatY(intervalA1), VectorDoubleSplatY(intervalA2)));
#if defined(NWB_HAS_AVX2)
        const SIMDVectorDouble intervalB0 = projectionInterval(relativeB0, absoluteB0, absoluteRoundoffB0), intervalB1 = projectionInterval(relativeB1, absoluteB1, absoluteRoundoffB1), intervalB2 = projectionInterval(relativeB2, absoluteB2, absoluteRoundoffB2);
#else
        const SIMDVectorDouble intervalB0 = projectionInterval(relativeB0, roundoffB0), intervalB1 = projectionInterval(relativeB1, roundoffB1), intervalB2 = projectionInterval(relativeB2, roundoffB2);
#endif
        const SIMDVectorDouble minimumB = VectorDoubleMin(VectorDoubleSplatX(intervalB0), VectorDoubleMin(VectorDoubleSplatX(intervalB1), VectorDoubleSplatX(intervalB2)));
        const SIMDVectorDouble maximumB = VectorDoubleMax(VectorDoubleSplatY(intervalB0), VectorDoubleMax(VectorDoubleSplatY(intervalB1), VectorDoubleSplatY(intervalB2)));
        return VectorDouble4Less(maximumA, minimumB) || VectorDouble4Less(maximumB, minimumA);
    };
    const SIMDVectorDouble normalA = crossAxis(edgeA0, VectorDoubleNegate(edgeA2)), normalB = crossAxis(edgeB0, VectorDoubleNegate(edgeB2));
    if(separated(normalA) || separated(normalB))
        return false;

    if(VectorDouble3Equal(normalA, zero) && VectorDouble3Equal(normalB, zero)){
        // Closed degenerate triangles retain their point/segment hulls, including collinear endpoint contact.
        const SIMDVectorDouble directionA = VectorDouble3Equal(edgeA0, zero) ? edgeA1 : edgeA0, directionB = VectorDouble3Equal(edgeB0, zero) ? edgeB1 : edgeB0;
        const SIMDVectorDouble normal = crossAxis(directionA, directionB);
        if(separated(directionA) || separated(directionB) || separated(normal) || separated(crossAxis(directionA, normal)) || separated(crossAxis(directionB, normal)))
            return false;
        const auto parallelSeparated = [&](const SIMDVectorDouble direction)noexcept{
            return separated(crossAxis(direction, s_DoubleSIMDIdentityR0)) || separated(crossAxis(direction, s_DoubleSIMDIdentityR1)) || separated(crossAxis(direction, s_DoubleSIMDIdentityR2));
        };
        return !parallelSeparated(directionA) && !parallelSeparated(directionB);
    }

    const auto edgeSeparated = [&](const SIMDVectorDouble edge)noexcept{
        // In-plane edge normals are necessary when the triangles are coplanar or one hull is degenerate.
        return separated(crossAxis(edge, normalA)) || separated(crossAxis(edge, normalB)) || separated(crossAxis(edge, edgeB0)) || separated(crossAxis(edge, edgeB1)) || separated(crossAxis(edge, edgeB2));
    };
    if(edgeSeparated(edgeA0) || edgeSeparated(edgeA1) || edgeSeparated(edgeA2))
        return false;
    const auto secondEdgeSeparated = [&](const SIMDVectorDouble edge)noexcept{
        return separated(crossAxis(edge, normalA)) || separated(crossAxis(edge, normalB));
    };
    return !secondEdgeSeparated(edgeB0) && !secondEdgeSeparated(edgeB1) && !secondEdgeSeparated(edgeB2);
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL TriangleTests::Intersects(const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2, const SIMDVectorSingle plane)noexcept{
    const SIMDVectorSingle d0 = CollisionDetail::PlaneDistance(plane, v0);
    const SIMDVectorSingle d1 = CollisionDetail::PlaneDistance(plane, v1);
    const SIMDVectorSingle d2 = CollisionDetail::PlaneDistance(plane, v2);
    const SIMDVectorSingle minDistance = VectorMin(d0, VectorMin(d1, d2));
    const SIMDVectorSingle maxDistance = VectorMax(d0, VectorMax(d1, d2));
    if(CollisionDetail::Vector4AllTrue(VectorGreater(minDistance, VectorZero())))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(VectorLess(maxDistance, VectorZero())))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL TriangleDoubleTests::Intersects(const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2, const SIMDVectorDouble plane)noexcept{
    const SIMDVectorDouble d0 = CollisionDoubleDetail::PlaneDoubleDistance(plane, v0);
    const SIMDVectorDouble d1 = CollisionDoubleDetail::PlaneDoubleDistance(plane, v1);
    const SIMDVectorDouble d2 = CollisionDoubleDetail::PlaneDoubleDistance(plane, v2);
    const SIMDVectorDouble minDistance = VectorDoubleMin(d0, VectorDoubleMin(d1, d2));
    const SIMDVectorDouble maxDistance = VectorDoubleMax(d0, VectorDoubleMax(d1, d2));
    if(CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleGreater(minDistance, VectorDoubleZero())))
        return PlaneIntersectionType::Front;
    if(CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleLess(maxDistance, VectorDoubleZero())))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL TriangleTests::ContainedBy(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2,
    const SIMDVectorSingle plane0,
    const SIMDVectorSingle plane1,
    const SIMDVectorSingle plane2,
    const SIMDVectorSingle plane3,
    const SIMDVectorSingle plane4,
    const SIMDVectorSingle plane5
)noexcept{
    const SIMDVectorSingle points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    const SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(points, CollisionDetail::s_TriangleVertexCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL TriangleDoubleTests::ContainedBy(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2,
    const SIMDVectorDouble plane0,
    const SIMDVectorDouble plane1,
    const SIMDVectorDouble plane2,
    const SIMDVectorDouble plane3,
    const SIMDVectorDouble plane4,
    const SIMDVectorDouble plane5
)noexcept{
    const SIMDVectorDouble points[CollisionDoubleDetail::s_DoubleTriangleVertexCount] = { v0, v1, v2 };
    const SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDoubleDetail::ContainmentFromPlaneTests(points, CollisionDoubleDetail::s_DoubleTriangleVertexCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

