// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "matrix_double.h"
#include "collision_detail_double.h"
#include "collision_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TriangleDoubleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL EdgeCross2D(SIMDVectorDouble a, SIMDVectorDouble b, SIMDVectorDouble c)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL SignedArea2D(SIMDVectorDouble a, SIMDVectorDouble b, SIMDVectorDouble c)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL AreaNormal(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
[[nodiscard]] bool NWB_SIMD_CALL ContainsPoint2D(
    SIMDVectorDouble point,
    SIMDVectorDouble a,
    SIMDVectorDouble b,
    SIMDVectorDouble c,
    SIMDVectorDouble tolerance
)noexcept;

[[nodiscard]] Expected<f64> NWB_SIMD_CALL Intersects(
    SIMDVectorDouble origin,
    SIMDVectorDouble direction,
    SIMDVectorDouble v0,
    SIMDVectorDouble v1,
    SIMDVectorDouble v2
)noexcept;

[[nodiscard]] bool NWB_SIMD_CALL Intersects(
    SIMDVectorDouble a0,
    SIMDVectorDouble a1,
    SIMDVectorDouble a2,
    SIMDVectorDouble b0,
    SIMDVectorDouble b1,
    SIMDVectorDouble b2
)noexcept;

[[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL Intersects(
    SIMDVectorDouble v0,
    SIMDVectorDouble v1,
    SIMDVectorDouble v2,
    SIMDVectorDouble plane
)noexcept;

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


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL TriangleDoubleTests::EdgeCross2D(
    const SIMDVectorDouble a,
    const SIMDVectorDouble b,
    const SIMDVectorDouble c
)noexcept{
    const SIMDVectorDouble ab = VectorDoubleSubtract(b, a);
    const SIMDVectorDouble ac = VectorDoubleSubtract(c, a);
    return VectorDoubleSubtract(VectorDoubleMultiply(VectorDoubleSplatX(ab), VectorDoubleSplatY(ac)), VectorDoubleMultiply(VectorDoubleSplatY(ab), VectorDoubleSplatX(ac)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL TriangleDoubleTests::SignedArea2D(
    const SIMDVectorDouble a,
    const SIMDVectorDouble b,
    const SIMDVectorDouble c
)noexcept{
    return VectorDoubleScale(TriangleDoubleTests::EdgeCross2D(a, b, c), CollisionDoubleDetail::s_DoubleHalf);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL TriangleDoubleTests::AreaNormal(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)noexcept{
    return VectorDouble3Cross(VectorDoubleSubtract(v1, v0), VectorDoubleSubtract(v2, v0));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL TriangleDoubleTests::ContainsPoint2D(
    const SIMDVectorDouble point,
    const SIMDVectorDouble a,
    const SIMDVectorDouble b,
    const SIMDVectorDouble c,
    const SIMDVectorDouble tolerance
)noexcept{
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


[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL TriangleDoubleTests::Intersects(
    const SIMDVectorDouble origin,
    const SIMDVectorDouble direction,
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)noexcept{
    return CollisionDoubleDetail::RayIntersectsTriangle(origin, direction, v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL TriangleDoubleTests::Intersects(
    const SIMDVectorDouble a0,
    const SIMDVectorDouble a1,
    const SIMDVectorDouble a2,
    const SIMDVectorDouble b0,
    const SIMDVectorDouble b1,
    const SIMDVectorDouble b2
)noexcept{
    const auto segmentIntersectsTriangle = [](const SIMDVectorDouble p0, const SIMDVectorDouble p1, const SIMDVectorDouble t0, const SIMDVectorDouble t1, const SIMDVectorDouble t2)noexcept{
        const SIMDVectorDouble direction = VectorDoubleSubtract(p1, p0);
        const auto distance = CollisionDoubleDetail::RayIntersectsTriangle(p0, direction, t0, t1, t2);
        return distance && *distance >= 0.0 && *distance <= 1.0;
    };

    if(segmentIntersectsTriangle(a0, a1, b0, b1, b2) || segmentIntersectsTriangle(a1, a2, b0, b1, b2) || segmentIntersectsTriangle(a2, a0, b0, b1, b2))
        return true;
    if(segmentIntersectsTriangle(b0, b1, a0, a1, a2) || segmentIntersectsTriangle(b1, b2, a0, a1, a2) || segmentIntersectsTriangle(b2, b0, a0, a1, a2))
        return true;

    const SIMDVectorDouble minA = VectorDoubleMin(a0, VectorDoubleMin(a1, a2));
    const SIMDVectorDouble maxA = VectorDoubleMax(a0, VectorDoubleMax(a1, a2));
    const SIMDVectorDouble minB = VectorDoubleMin(b0, VectorDoubleMin(b1, b2));
    const SIMDVectorDouble maxB = VectorDoubleMax(b0, VectorDoubleMax(b1, b2));
    return CollisionDoubleDetail::MinMaxIntersects(minA, maxA, minB, maxB);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL TriangleDoubleTests::Intersects(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2,
    const SIMDVectorDouble plane
)noexcept{
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

