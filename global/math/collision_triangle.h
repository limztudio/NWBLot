// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix.h"
#include "collision_detail.h"
#include "collision_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TriangleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVector SIMDCALL EdgeCross2D(SIMDVector a, SIMDVector b, SIMDVector c)noexcept;
[[nodiscard]] SIMDVector SIMDCALL SignedArea2D(SIMDVector a, SIMDVector b, SIMDVector c)noexcept;
[[nodiscard]] SIMDVector SIMDCALL AreaNormal(SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
[[nodiscard]] bool SIMDCALL ContainsPoint2D(
    SIMDVector point,
    SIMDVector a,
    SIMDVector b,
    SIMDVector c,
    SIMDVector tolerance
)noexcept;

[[nodiscard]] bool SIMDCALL Intersects(
    SIMDVector origin,
    SIMDVector direction,
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2,
    f32& outDistance
)noexcept;

[[nodiscard]] bool SIMDCALL Intersects(
    SIMDVector a0,
    SIMDVector a1,
    SIMDVector a2,
    SIMDVector b0,
    SIMDVector b1,
    SIMDVector b2
)noexcept;

[[nodiscard]] PlaneIntersectionType::Enum SIMDCALL Intersects(
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2,
    SIMDVector plane
)noexcept;

[[nodiscard]] ContainmentType::Enum SIMDCALL ContainedBy(
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2,
    SIMDVector plane0,
    SIMDVector plane1,
    SIMDVector plane2,
    SIMDVector plane3,
    SIMDVector plane4,
    SIMDVector plane5
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL TriangleTests::EdgeCross2D(
    const SIMDVector a,
    const SIMDVector b,
    const SIMDVector c
)noexcept{
    const SIMDVector ab = VectorSubtract(b, a);
    const SIMDVector ac = VectorSubtract(c, a);
    return VectorSubtract(VectorMultiply(VectorSplatX(ab), VectorSplatY(ac)), VectorMultiply(VectorSplatY(ab), VectorSplatX(ac)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL TriangleTests::SignedArea2D(
    const SIMDVector a,
    const SIMDVector b,
    const SIMDVector c
)noexcept{
    return VectorScale(TriangleTests::EdgeCross2D(a, b, c), CollisionDetail::s_Half);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL TriangleTests::AreaNormal(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)noexcept{
    return Vector3Cross(VectorSubtract(v1, v0), VectorSubtract(v2, v0));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool SIMDCALL TriangleTests::ContainsPoint2D(
    const SIMDVector point,
    const SIMDVector a,
    const SIMDVector b,
    const SIMDVector c,
    const SIMDVector tolerance
)noexcept{
    const SIMDVector negativeTolerance = VectorNegate(VectorSplatX(tolerance));
    const SIMDVector ab = TriangleTests::EdgeCross2D(a, b, point);
    const SIMDVector bc = TriangleTests::EdgeCross2D(b, c, point);
    const SIMDVector ca = TriangleTests::EdgeCross2D(c, a, point);
    SIMDVector inside = VectorGreaterOrEqual(ab, negativeTolerance);
    inside = VectorAndInt(inside, VectorGreaterOrEqual(bc, negativeTolerance));
    inside = VectorAndInt(inside, VectorGreaterOrEqual(ca, negativeTolerance));
    return CollisionDetail::Vector4AllTrue(inside);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL TriangleTests::Intersects(
    const SIMDVector origin,
    const SIMDVector direction,
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2,
    f32& outDistance
)noexcept{
    return CollisionDetail::RayIntersectsTriangle(origin, direction, v0, v1, v2, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL TriangleTests::Intersects(
    const SIMDVector a0,
    const SIMDVector a1,
    const SIMDVector a2,
    const SIMDVector b0,
    const SIMDVector b1,
    const SIMDVector b2
)noexcept{
    const auto segmentIntersectsTriangle = [](const SIMDVector p0, const SIMDVector p1, const SIMDVector t0, const SIMDVector t1, const SIMDVector t2)noexcept{
        f32 distance = 0.0f;
        const SIMDVector direction = VectorSubtract(p1, p0);
        return CollisionDetail::RayIntersectsTriangle(p0, direction, t0, t1, t2, distance) && distance >= 0.0f && distance <= 1.0f;
    };

    if(segmentIntersectsTriangle(a0, a1, b0, b1, b2) || segmentIntersectsTriangle(a1, a2, b0, b1, b2) || segmentIntersectsTriangle(a2, a0, b0, b1, b2))
        return true;
    if(segmentIntersectsTriangle(b0, b1, a0, a1, a2) || segmentIntersectsTriangle(b1, b2, a0, a1, a2) || segmentIntersectsTriangle(b2, b0, a0, a1, a2))
        return true;

    const SIMDVector minA = VectorMin(a0, VectorMin(a1, a2));
    const SIMDVector maxA = VectorMax(a0, VectorMax(a1, a2));
    const SIMDVector minB = VectorMin(b0, VectorMin(b1, b2));
    const SIMDVector maxB = VectorMax(b0, VectorMax(b1, b2));
    return CollisionDetail::MinMaxIntersects(minA, maxA, minB, maxB);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL TriangleTests::Intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2,
    const SIMDVector plane
)noexcept{
    const SIMDVector d0 = CollisionDetail::PlaneDistance(plane, v0);
    const SIMDVector d1 = CollisionDetail::PlaneDistance(plane, v1);
    const SIMDVector d2 = CollisionDetail::PlaneDistance(plane, v2);
    const SIMDVector minDistance = VectorMin(d0, VectorMin(d1, d2));
    const SIMDVector maxDistance = VectorMax(d0, VectorMax(d1, d2));
    if(CollisionDetail::Vector4AllTrue(VectorGreater(minDistance, VectorZero())))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(VectorLess(maxDistance, VectorZero())))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL TriangleTests::ContainedBy(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2,
    const SIMDVector plane0,
    const SIMDVector plane1,
    const SIMDVector plane2,
    const SIMDVector plane3,
    const SIMDVector plane4,
    const SIMDVector plane5
)noexcept{
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(points, CollisionDetail::s_TriangleVertexCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

