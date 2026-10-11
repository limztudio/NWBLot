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
[[nodiscard]] bool NWB_SIMD_CALL ContainsPoint2D(
    SIMDVectorSingle point,
    SIMDVectorSingle a,
    SIMDVectorSingle b,
    SIMDVectorSingle c,
    SIMDVectorSingle tolerance
)noexcept;

[[nodiscard]] Expected<f32> NWB_SIMD_CALL Intersects(
    SIMDVectorSingle origin,
    SIMDVectorSingle direction,
    SIMDVectorSingle v0,
    SIMDVectorSingle v1,
    SIMDVectorSingle v2
)noexcept;

[[nodiscard]] bool NWB_SIMD_CALL Intersects(
    SIMDVectorSingle a0,
    SIMDVectorSingle a1,
    SIMDVectorSingle a2,
    SIMDVectorSingle b0,
    SIMDVectorSingle b1,
    SIMDVectorSingle b2
)noexcept;

[[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL Intersects(
    SIMDVectorSingle v0,
    SIMDVectorSingle v1,
    SIMDVectorSingle v2,
    SIMDVectorSingle plane
)noexcept;

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


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL TriangleTests::EdgeCross2D(
    const SIMDVectorSingle a,
    const SIMDVectorSingle b,
    const SIMDVectorSingle c
)noexcept{
    const SIMDVectorSingle ab = VectorSubtract(b, a);
    const SIMDVectorSingle ac = VectorSubtract(c, a);
    return VectorSubtract(VectorMultiply(VectorSplatX(ab), VectorSplatY(ac)), VectorMultiply(VectorSplatY(ab), VectorSplatX(ac)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL TriangleTests::SignedArea2D(
    const SIMDVectorSingle a,
    const SIMDVectorSingle b,
    const SIMDVectorSingle c
)noexcept{
    return VectorScale(TriangleTests::EdgeCross2D(a, b, c), CollisionDetail::s_Half);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL TriangleTests::AreaNormal(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)noexcept{
    return Vector3Cross(VectorSubtract(v1, v0), VectorSubtract(v2, v0));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL TriangleTests::ContainsPoint2D(
    const SIMDVectorSingle point,
    const SIMDVectorSingle a,
    const SIMDVectorSingle b,
    const SIMDVectorSingle c,
    const SIMDVectorSingle tolerance
)noexcept{
    const SIMDVectorSingle negativeTolerance = VectorNegate(VectorSplatX(tolerance));
    const SIMDVectorSingle ab = TriangleTests::EdgeCross2D(a, b, point);
    const SIMDVectorSingle bc = TriangleTests::EdgeCross2D(b, c, point);
    const SIMDVectorSingle ca = TriangleTests::EdgeCross2D(c, a, point);
    SIMDVectorSingle inside = VectorGreaterOrEqual(ab, negativeTolerance);
    inside = VectorAndInt(inside, VectorGreaterOrEqual(bc, negativeTolerance));
    inside = VectorAndInt(inside, VectorGreaterOrEqual(ca, negativeTolerance));
    return CollisionDetail::Vector4AllTrue(inside);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL TriangleTests::Intersects(
    const SIMDVectorSingle origin,
    const SIMDVectorSingle direction,
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)noexcept{
    return CollisionDetail::RayIntersectsTriangle(origin, direction, v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL TriangleTests::Intersects(
    const SIMDVectorSingle a0,
    const SIMDVectorSingle a1,
    const SIMDVectorSingle a2,
    const SIMDVectorSingle b0,
    const SIMDVectorSingle b1,
    const SIMDVectorSingle b2
)noexcept{
    const auto segmentIntersectsTriangle = [](const SIMDVectorSingle p0, const SIMDVectorSingle p1, const SIMDVectorSingle t0, const SIMDVectorSingle t1, const SIMDVectorSingle t2)noexcept{
        const SIMDVectorSingle direction = VectorSubtract(p1, p0);
        const auto distance = CollisionDetail::RayIntersectsTriangle(p0, direction, t0, t1, t2);
        return distance && *distance >= 0.0f && *distance <= 1.0f;
    };

    if(segmentIntersectsTriangle(a0, a1, b0, b1, b2) || segmentIntersectsTriangle(a1, a2, b0, b1, b2) || segmentIntersectsTriangle(a2, a0, b0, b1, b2))
        return true;
    if(segmentIntersectsTriangle(b0, b1, a0, a1, a2) || segmentIntersectsTriangle(b1, b2, a0, a1, a2) || segmentIntersectsTriangle(b2, b0, a0, a1, a2))
        return true;

    const SIMDVectorSingle minA = VectorMin(a0, VectorMin(a1, a2));
    const SIMDVectorSingle maxA = VectorMax(a0, VectorMax(a1, a2));
    const SIMDVectorSingle minB = VectorMin(b0, VectorMin(b1, b2));
    const SIMDVectorSingle maxB = VectorMax(b0, VectorMax(b1, b2));
    return CollisionDetail::MinMaxIntersects(minA, maxA, minB, maxB);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL TriangleTests::Intersects(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2,
    const SIMDVectorSingle plane
)noexcept{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

