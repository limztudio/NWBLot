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


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL TriangleTests::EdgeCross2D(
    const SIMDVectorSingle a,
    const SIMDVectorSingle b,
    const SIMDVectorSingle c
)noexcept{
    const SIMDVectorSingle ab = VectorSubtract(b, a);
    const SIMDVectorSingle ac = VectorSubtract(c, a);
    return VectorSubtract(VectorMultiply(VectorSplatX(ab), VectorSplatY(ac)), VectorMultiply(VectorSplatY(ab), VectorSplatX(ac)));
}

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


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL TriangleTests::SignedArea2D(
    const SIMDVectorSingle a,
    const SIMDVectorSingle b,
    const SIMDVectorSingle c
)noexcept{
    return VectorScale(TriangleTests::EdgeCross2D(a, b, c), CollisionDetail::s_Half);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL TriangleDoubleTests::SignedArea2D(
    const SIMDVectorDouble a,
    const SIMDVectorDouble b,
    const SIMDVectorDouble c
)noexcept{
    return VectorDoubleScale(TriangleDoubleTests::EdgeCross2D(a, b, c), CollisionDoubleDetail::s_DoubleHalf);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL TriangleTests::AreaNormal(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)noexcept{
    return Vector3Cross(VectorSubtract(v1, v0), VectorSubtract(v2, v0));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL TriangleDoubleTests::AreaNormal(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)noexcept{
    return VectorDouble3Cross(VectorDoubleSubtract(v1, v0), VectorDoubleSubtract(v2, v0));
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


[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL TriangleTests::Intersects(
    const SIMDVectorSingle origin,
    const SIMDVectorSingle direction,
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)noexcept{
    return CollisionDetail::RayIntersectsTriangle(origin, direction, v0, v1, v2);
}

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

