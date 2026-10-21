// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "collision_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CollisionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void FastIntersectSpherePlane(const SIMDVectorSingle center, const SIMDVectorSingle radius, const SIMDVectorSingle plane, SIMDVectorSingle& outOutside, SIMDVectorSingle& outInside)noexcept{
    const SIMDVectorSingle distance = PlaneDistance(plane, center);
    outOutside = VectorLess(distance, VectorNegate(radius));
    outInside = VectorGreaterOrEqual(distance, radius);
}

[[nodiscard]] NWB_INLINE ContainmentType::Enum NWB_SIMD_CALL ContainmentFromSpherePlaneTests(const SIMDVectorSingle sphereValue, const SIMDVectorSingle (&planes)[CollisionDetail::s_FrustumPlaneCount])noexcept{
    const SIMDVectorSingle centerVector = SphereCenter(sphereValue);
    const SIMDVectorSingle sphereRadius = SphereRadius(sphereValue);
    SIMDVectorSingle anyIntersecting = VectorFalseInt();
    for(const SIMDVectorSingle plane : planes){
        SIMDVectorSingle outside{};
        SIMDVectorSingle inside{};
        FastIntersectSpherePlane(centerVector, sphereRadius, plane, outside, inside);
        if(Vector4AllTrue(outside))
            return ContainmentType::Disjoint;
        anyIntersecting = VectorOrInt(anyIntersecting, VectorEqualInt(inside, VectorFalseInt()));
    }
    return Vector4AllTrue(anyIntersecting) ? ContainmentType::Intersects : ContainmentType::Contains;
}

NWB_INLINE void FastIntersectAxisAlignedBoxPlane(const SIMDVectorSingle center, const SIMDVectorSingle extents, const SIMDVectorSingle plane, SIMDVectorSingle& outOutside, SIMDVectorSingle& outInside)noexcept{
    const SIMDVectorSingle distance = PlaneDistance(plane, center);
    const SIMDVectorSingle radius = Vector3Dot(extents, VectorAbs(plane));
    outOutside = VectorLess(distance, VectorNegate(radius));
    outInside = VectorGreaterOrEqual(distance, radius);
}

NWB_INLINE void FastIntersectOrientedBoxPlane(
    const SIMDVectorSingle center,
    const SIMDVectorSingle extents,
    const SIMDVectorSingle axis0,
    const SIMDVectorSingle axis1,
    const SIMDVectorSingle axis2,
    const SIMDVectorSingle plane,
    SIMDVectorSingle& outOutside,
    SIMDVectorSingle& outInside
)noexcept{
    const SIMDVectorSingle distance = PlaneDistance(plane, center);
    SIMDVectorSingle radiusAxes = Vector3Dot(plane, axis0);
    radiusAxes = VectorSelect(radiusAxes, Vector3Dot(plane, axis1), VectorSelectControl(0u, 1u, 0u, 0u));
    radiusAxes = VectorSelect(radiusAxes, Vector3Dot(plane, axis2), VectorSelectControl(0u, 0u, 1u, 0u));
    const SIMDVectorSingle radius = Vector3Dot(extents, VectorAbs(radiusAxes));
    outOutside = VectorLess(distance, VectorNegate(radius));
    outInside = VectorGreaterOrEqual(distance, radius);
}

inline void FastIntersectPointsPlane(const NotNull<const SIMDVectorSingle*> points, const usize pointCount, const SIMDVectorSingle plane, SIMDVectorSingle& outOutside, SIMDVectorSingle& outInside)noexcept{
    NWB_ASSERT(pointCount > 0u);

    SIMDVectorSingle minDistance = PlaneDistance(plane, points.get()[0]);
    SIMDVectorSingle maxDistance = minDistance;
    for(usize pointIndex = 1u; pointIndex < pointCount; ++pointIndex){
        const SIMDVectorSingle distance = PlaneDistance(plane, points.get()[pointIndex]);
        minDistance = VectorMin(minDistance, distance);
        maxDistance = VectorMax(maxDistance, distance);
    }

    outOutside = VectorLess(maxDistance, VectorZero());
    outInside = VectorGreaterOrEqual(minDistance, VectorZero());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void ObbAxes(const SIMDVectorSingle orientation, SIMDVectorSingle& outAxis0, SIMDVectorSingle& outAxis1, SIMDVectorSingle& outAxis2)noexcept{
    outAxis0 = Vector3Rotate(s_SIMDIdentityR0, orientation);
    outAxis1 = Vector3Rotate(s_SIMDIdentityR1, orientation);
    outAxis2 = Vector3Rotate(s_SIMDIdentityR2, orientation);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle PointToObbLocal(const SIMDVectorSingle point, const SIMDVectorSingle center, const SIMDVectorSingle orientation)noexcept{
    return Vector3InverseRotate(VectorSubtract(point, center), orientation);
}

[[nodiscard]] NWB_INLINE bool PointInsideObb(const SIMDVectorSingle point, const SIMDVectorSingle center, const SIMDVectorSingle extents, const SIMDVectorSingle orientation)noexcept{
    return Vector3LessOrEqual(VectorAbs(PointToObbLocal(point, center, orientation)), extents);
}

[[nodiscard]] inline bool PointsInsideObb(const SIMDVectorSingle* points, const usize pointCount, const SIMDVectorSingle center, const SIMDVectorSingle extents, const SIMDVectorSingle orientation)noexcept{
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        if(Vector3AnyTrue(VectorGreater(VectorAbs(PointToObbLocal(points[pointIndex], center, orientation)), extents)))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum ContainmentFromPlaneTests(const SIMDVectorSingle* points, const usize pointCount, const SIMDVectorSingle* planes, const usize planeCount)noexcept{
    SIMDVectorSingle anyIntersecting = VectorFalseInt();
    for(usize planeIndex = 0u; planeIndex < planeCount; ++planeIndex){
        SIMDVectorSingle outside{};
        SIMDVectorSingle inside{};
        FastIntersectPointsPlane(NotNull<const SIMDVectorSingle*>(points), pointCount, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return ContainmentType::Disjoint;
        anyIntersecting = VectorOrInt(anyIntersecting, VectorEqualInt(inside, VectorFalseInt()));
    }
    return Vector4AllTrue(anyIntersecting) ? ContainmentType::Intersects : ContainmentType::Contains;
}

inline void FrustumPlanes(const SIMDVectorSingle origin, const SIMDVectorSingle orientation, const f32 rightSlope, const f32 leftSlope, const f32 topSlope, const f32 bottomSlope, const f32 nearPlane, const f32 farPlane, SIMDVectorSingle (&outPlanes)[CollisionDetail::s_FrustumPlaneCount])noexcept{
    const SIMDVectorSingle localPlanes[CollisionDetail::s_FrustumPlaneCount] = {
        VectorXorInt(VectorSet(0.0f, 0.0f, 1.0f, nearPlane), s_SIMDFlipW),
        VectorSet(0.0f, 0.0f, -1.0f, farPlane),
        VectorSet(-1.0f, 0.0f, rightSlope, 0.0f),
        VectorXorInt(VectorSet(1.0f, 0.0f, leftSlope, 0.0f), s_SIMDFlipZ),
        VectorSet(0.0f, -1.0f, topSlope, 0.0f),
        VectorXorInt(VectorSet(0.0f, 1.0f, bottomSlope, 0.0f), s_SIMDFlipZ),
    };
    for(u32 i = 0u; i < s_FrustumPlaneCount; ++i)
        outPlanes[i] = PlaneNormalizeSafe(TransformPlane(localPlanes[i], orientation, origin));
}

[[nodiscard]] inline bool FrustumPlanesIntersectAxisAlignedBox(const SIMDVectorSingle (&planes)[CollisionDetail::s_FrustumPlaneCount], const SIMDVectorSingle center, const SIMDVectorSingle extents)noexcept{
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVectorSingle outside{};
        SIMDVectorSingle inside{};
        FastIntersectAxisAlignedBoxPlane(center, extents, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectSphere(const SIMDVectorSingle (&planes)[CollisionDetail::s_FrustumPlaneCount], const SIMDVectorSingle centerRadius)noexcept{
    const SIMDVectorSingle center = SphereCenter(centerRadius);
    const SIMDVectorSingle radius = SphereRadius(centerRadius);
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVectorSingle outside{};
        SIMDVectorSingle inside{};
        FastIntersectSpherePlane(center, radius, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectOrientedBox(const SIMDVectorSingle (&planes)[CollisionDetail::s_FrustumPlaneCount], const SIMDVectorSingle center, const SIMDVectorSingle extents, const SIMDVectorSingle orientation)noexcept{
    SIMDVectorSingle axis0{};
    SIMDVectorSingle axis1{};
    SIMDVectorSingle axis2{};
    ObbAxes(orientation, axis0, axis1, axis2);
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVectorSingle outside{};
        SIMDVectorSingle inside{};
        FastIntersectOrientedBoxPlane(center, extents, axis0, axis1, axis2, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectPoints(const SIMDVectorSingle (&planes)[CollisionDetail::s_FrustumPlaneCount], const NotNull<const SIMDVectorSingle*> points, const usize pointCount)noexcept{
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVectorSingle outside{};
        SIMDVectorSingle inside{};
        FastIntersectPointsPlane(points, pointCount, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ObbIntersectsObb(
    const SIMDVectorSingle lhsCenter,
    const SIMDVectorSingle lhsExtents,
    const SIMDVectorSingle lhsOrientation,
    const SIMDVectorSingle rhsCenter,
    const SIMDVectorSingle rhsExtents,
    const SIMDVectorSingle rhsOrientation
)noexcept{
    const SIMDVectorSingle relativeOrientation = QuaternionMultiply(QuaternionConjugate(lhsOrientation), rhsOrientation);
    SIMDMatrix rotation = MatrixRotationQuaternion(relativeOrientation);
    const SIMDVectorSingle translation = Vector3InverseRotate(VectorSubtract(rhsCenter, lhsCenter), lhsOrientation);

    const SIMDVectorSingle r0 = rotation.v[0];
    const SIMDVectorSingle r1 = rotation.v[1];
    const SIMDVectorSingle r2 = rotation.v[2];
    rotation = MatrixTranspose(rotation);
    const SIMDVectorSingle c0 = rotation.v[0];
    const SIMDVectorSingle c1 = rotation.v[1];
    const SIMDVectorSingle c2 = rotation.v[2];

    const SIMDVectorSingle ar0 = VectorAbs(r0);
    const SIMDVectorSingle ar1 = VectorAbs(r1);
    const SIMDVectorSingle ar2 = VectorAbs(r2);
    const SIMDVectorSingle ac0 = VectorAbs(c0);
    const SIMDVectorSingle ac1 = VectorAbs(c1);
    const SIMDVectorSingle ac2 = VectorAbs(c2);

    SIMDVectorSingle distance = VectorSplatX(translation);
    SIMDVectorSingle lhsRadius = VectorSplatX(lhsExtents);
    SIMDVectorSingle rhsRadius = Vector3Dot(rhsExtents, ar0);
    SIMDVectorSingle noIntersection = VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius));

    distance = VectorSplatY(translation);
    lhsRadius = VectorSplatY(lhsExtents);
    rhsRadius = Vector3Dot(rhsExtents, ar1);
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = VectorSplatZ(translation);
    lhsRadius = VectorSplatZ(lhsExtents);
    rhsRadius = Vector3Dot(rhsExtents, ar2);
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, c0);
    lhsRadius = Vector3Dot(lhsExtents, ac0);
    rhsRadius = VectorSplatX(rhsExtents);
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, c1);
    lhsRadius = Vector3Dot(lhsExtents, ac1);
    rhsRadius = VectorSplatY(rhsExtents);
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, c2);
    lhsRadius = Vector3Dot(lhsExtents, ac2);
    rhsRadius = VectorSplatZ(rhsExtents);
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<3, 6, 1, 0>(c0, VectorNegate(c0)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<3, 2, 1, 0>(ac0));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<3, 2, 1, 0>(ar0));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<3, 6, 1, 0>(c1, VectorNegate(c1)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<3, 2, 1, 0>(ac1));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<2, 3, 0, 1>(ar0));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<3, 6, 1, 0>(c2, VectorNegate(c2)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<3, 2, 1, 0>(ac2));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<1, 0, 3, 2>(ar0));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<2, 3, 4, 1>(c0, VectorNegate(c0)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<2, 3, 0, 1>(ac0));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<3, 2, 1, 0>(ar1));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<2, 3, 4, 1>(c1, VectorNegate(c1)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<2, 3, 0, 1>(ac1));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<2, 3, 0, 1>(ar1));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<2, 3, 4, 1>(c2, VectorNegate(c2)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<2, 3, 0, 1>(ac2));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<1, 0, 3, 2>(ar1));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<5, 0, 3, 2>(c0, VectorNegate(c0)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<1, 0, 3, 2>(ac0));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<3, 2, 1, 0>(ar2));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<5, 0, 3, 2>(c1, VectorNegate(c1)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<1, 0, 3, 2>(ac1));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<2, 3, 0, 1>(ar2));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    distance = Vector3Dot(translation, VectorPermute<5, 0, 3, 2>(c2, VectorNegate(c2)));
    lhsRadius = Vector3Dot(lhsExtents, VectorSwizzle<1, 0, 3, 2>(ac2));
    rhsRadius = Vector3Dot(rhsExtents, VectorSwizzle<1, 0, 3, 2>(ar2));
    noIntersection = VectorOrInt(noIntersection, VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius)));

    return Vector4NotEqualInt(noIntersection, VectorTrueInt());
}

[[nodiscard]] inline bool TriangleAabbOverlap(const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2, const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    const SIMDVectorSingle triangleMin = VectorMin(v0, VectorMin(v1, v2));
    const SIMDVectorSingle triangleMax = VectorMax(v0, VectorMax(v1, v2));
    if(!MinMaxIntersects(triangleMin, triangleMax, minBounds, maxBounds))
        return false;

    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle relativeV1 = VectorSubtract(v1, v0);
    const SIMDVectorSingle relativeV2 = VectorSubtract(v2, v0);
    const SIMDVectorSingle relativeMin = VectorSubtract(minBounds, v0);
    const SIMDVectorSingle relativeMax = VectorSubtract(maxBounds, v0);
    const auto crossAxis = [](const SIMDVectorSingle first, const SIMDVectorSingle second)noexcept{
        // Separate products preserve zero axes for parallel and collapsed edges.
        const SIMDVectorSingle positive = VectorMultiply(VectorSwizzle<1, 2, 0, 3>(first), VectorSwizzle<2, 0, 1, 3>(second));
        const SIMDVectorSingle negative = VectorMultiply(VectorSwizzle<2, 0, 1, 3>(first), VectorSwizzle<1, 2, 0, 3>(second));
        return VectorSubtract(positive, negative);
    };
    const auto separated = [&](const SIMDVectorSingle axis)noexcept{
        if(Vector3Equal(axis, zero))
            return false;
        const SIMDVectorSingle p1 = Vector3Dot(relativeV1, axis);
        const SIMDVectorSingle p2 = Vector3Dot(relativeV2, axis);
        const SIMDVectorSingle minimumTriangle = VectorMin(zero, VectorMin(p1, p2));
        const SIMDVectorSingle maximumTriangle = VectorMax(zero, VectorMax(p1, p2));
        const SIMDVectorSingle firstBounds = VectorMultiply(relativeMin, axis);
        const SIMDVectorSingle secondBounds = VectorMultiply(relativeMax, axis);
        const SIMDVectorSingle minimumBox = Vector3Dot(VectorMin(firstBounds, secondBounds), s_SIMDOne3);
        const SIMDVectorSingle maximumBox = Vector3Dot(VectorMax(firstBounds, secondBounds), s_SIMDOne3);
        return Vector4Less(maximumTriangle, minimumBox) || Vector4Less(maximumBox, minimumTriangle);
    };
    if(separated(crossAxis(relativeV1, relativeV2)))
        return false;

    const auto edgeSeparated = [&](const SIMDVectorSingle edge)noexcept{
        return separated(crossAxis(edge, s_SIMDIdentityR0)) || separated(crossAxis(edge, s_SIMDIdentityR1)) || separated(crossAxis(edge, s_SIMDIdentityR2));
    };
    return !edgeSeparated(relativeV1) && !edgeSeparated(VectorSubtract(v2, v1)) && !edgeSeparated(VectorNegate(relativeV2));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CollisionDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void FastIntersectSpherePlane(const SIMDVectorDouble center, const SIMDVectorDouble radius, const SIMDVectorDouble plane, SIMDVectorDouble& outOutside, SIMDVectorDouble& outInside)noexcept{
    const SIMDVectorDouble distance = PlaneDoubleDistance(plane, center);
    outOutside = VectorDoubleLess(distance, VectorDoubleNegate(radius));
    outInside = VectorDoubleGreaterOrEqual(distance, radius);
}

[[nodiscard]] NWB_INLINE ContainmentType::Enum NWB_SIMD_CALL ContainmentFromSpherePlaneTests(const SIMDVectorDouble sphereValue, const SIMDVectorDouble (&planes)[s_DoubleFrustumPlaneCount])noexcept{
    const SIMDVectorDouble centerVector = SphereCenter(sphereValue);
    const SIMDVectorDouble sphereRadius = SphereRadius(sphereValue);
    SIMDVectorDouble anyIntersecting = VectorDoubleFalseInt();
    for(const SIMDVectorDouble plane : planes){
        SIMDVectorDouble outside{};
        SIMDVectorDouble inside{};
        FastIntersectSpherePlane(centerVector, sphereRadius, plane, outside, inside);
        if(VectorDouble4AllTrue(outside))
            return ContainmentType::Disjoint;
        anyIntersecting = VectorDoubleOrInt(anyIntersecting, VectorDoubleEqualInt(inside, VectorDoubleFalseInt()));
    }
    return VectorDouble4AllTrue(anyIntersecting) ? ContainmentType::Intersects : ContainmentType::Contains;
}

NWB_INLINE void FastIntersectAxisAlignedBoxPlane(const SIMDVectorDouble center, const SIMDVectorDouble extents, const SIMDVectorDouble plane, SIMDVectorDouble& outOutside, SIMDVectorDouble& outInside)noexcept{
    const SIMDVectorDouble distance = PlaneDoubleDistance(plane, center);
    const SIMDVectorDouble radius = VectorDouble3Dot(extents, VectorDoubleAbs(plane));
    outOutside = VectorDoubleLess(distance, VectorDoubleNegate(radius));
    outInside = VectorDoubleGreaterOrEqual(distance, radius);
}

NWB_INLINE void FastIntersectOrientedBoxPlane(
    const SIMDVectorDouble center,
    const SIMDVectorDouble extents,
    const SIMDVectorDouble axis0,
    const SIMDVectorDouble axis1,
    const SIMDVectorDouble axis2,
    const SIMDVectorDouble plane,
    SIMDVectorDouble& outOutside,
    SIMDVectorDouble& outInside
)noexcept{
    const SIMDVectorDouble distance = PlaneDoubleDistance(plane, center);
    SIMDVectorDouble radiusAxes = VectorDouble3Dot(plane, axis0);
    radiusAxes = VectorDoubleSelect(radiusAxes, VectorDouble3Dot(plane, axis1), VectorDoubleSelectControl(0u, 1u, 0u, 0u));
    radiusAxes = VectorDoubleSelect(radiusAxes, VectorDouble3Dot(plane, axis2), VectorDoubleSelectControl(0u, 0u, 1u, 0u));
    const SIMDVectorDouble radius = VectorDouble3Dot(extents, VectorDoubleAbs(radiusAxes));
    outOutside = VectorDoubleLess(distance, VectorDoubleNegate(radius));
    outInside = VectorDoubleGreaterOrEqual(distance, radius);
}

inline void FastIntersectPointsPlane(const NotNull<const SIMDVectorDouble*> points, const usize pointCount, const SIMDVectorDouble plane, SIMDVectorDouble& outOutside, SIMDVectorDouble& outInside)noexcept{
    NWB_ASSERT(pointCount > 0u);

    SIMDVectorDouble minDistance = PlaneDoubleDistance(plane, points.get()[0]);
    SIMDVectorDouble maxDistance = minDistance;
    for(usize pointIndex = 1u; pointIndex < pointCount; ++pointIndex){
        const SIMDVectorDouble distance = PlaneDoubleDistance(plane, points.get()[pointIndex]);
        minDistance = VectorDoubleMin(minDistance, distance);
        maxDistance = VectorDoubleMax(maxDistance, distance);
    }

    outOutside = VectorDoubleLess(maxDistance, VectorDoubleZero());
    outInside = VectorDoubleGreaterOrEqual(minDistance, VectorDoubleZero());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void ObbAxes(const SIMDVectorDouble orientation, SIMDVectorDouble& outAxis0, SIMDVectorDouble& outAxis1, SIMDVectorDouble& outAxis2)noexcept{
    outAxis0 = VectorDouble3Rotate(s_DoubleSIMDIdentityR0, orientation);
    outAxis1 = VectorDouble3Rotate(s_DoubleSIMDIdentityR1, orientation);
    outAxis2 = VectorDouble3Rotate(s_DoubleSIMDIdentityR2, orientation);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble PointToObbLocal(const SIMDVectorDouble point, const SIMDVectorDouble center, const SIMDVectorDouble orientation)noexcept{
    return VectorDouble3InverseRotate(VectorDoubleSubtract(point, center), orientation);
}

[[nodiscard]] NWB_INLINE bool PointInsideObb(const SIMDVectorDouble point, const SIMDVectorDouble center, const SIMDVectorDouble extents, const SIMDVectorDouble orientation)noexcept{
    return VectorDouble3LessOrEqual(VectorDoubleAbs(PointToObbLocal(point, center, orientation)), extents);
}

[[nodiscard]] inline bool PointsInsideObb(const SIMDVectorDouble* points, const usize pointCount, const SIMDVectorDouble center, const SIMDVectorDouble extents, const SIMDVectorDouble orientation)noexcept{
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        if(VectorDouble3AnyTrue(VectorDoubleGreater(VectorDoubleAbs(PointToObbLocal(points[pointIndex], center, orientation)), extents)))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum ContainmentFromPlaneTests(const SIMDVectorDouble* points, const usize pointCount, const SIMDVectorDouble* planes, const usize planeCount)noexcept{
    SIMDVectorDouble anyIntersecting = VectorDoubleFalseInt();
    for(usize planeIndex = 0u; planeIndex < planeCount; ++planeIndex){
        SIMDVectorDouble outside{};
        SIMDVectorDouble inside{};
        FastIntersectPointsPlane(NotNull<const SIMDVectorDouble*>(points), pointCount, planes[planeIndex], outside, inside);
        if(VectorDouble4AllTrue(outside))
            return ContainmentType::Disjoint;
        anyIntersecting = VectorDoubleOrInt(anyIntersecting, VectorDoubleEqualInt(inside, VectorDoubleFalseInt()));
    }
    return VectorDouble4AllTrue(anyIntersecting) ? ContainmentType::Intersects : ContainmentType::Contains;
}

inline void FrustumPlanes(const SIMDVectorDouble origin, const SIMDVectorDouble orientation, const f64 rightSlope, const f64 leftSlope, const f64 topSlope, const f64 bottomSlope, const f64 nearPlane, const f64 farPlane, SIMDVectorDouble (&outPlanes)[CollisionDetail::s_FrustumPlaneCount])noexcept{
    const SIMDVectorDouble localPlanes[s_DoubleFrustumPlaneCount] = {
        VectorDoubleXorInt(VectorDoubleSet(0.0, 0.0, 1.0, nearPlane), VectorDoubleAndInt(s_DoubleSIMDNegativeZero, s_DoubleSIMDMaskW)),
        VectorDoubleSet(0.0, 0.0, -1.0, farPlane),
        VectorDoubleSet(-1.0, 0.0, rightSlope, 0.0),
        VectorDoubleXorInt(VectorDoubleSet(1.0, 0.0, leftSlope, 0.0), VectorDoubleAndInt(s_DoubleSIMDNegativeZero, s_DoubleSIMDMaskZ)),
        VectorDoubleSet(0.0, -1.0, topSlope, 0.0),
        VectorDoubleXorInt(VectorDoubleSet(0.0, 1.0, bottomSlope, 0.0), VectorDoubleAndInt(s_DoubleSIMDNegativeZero, s_DoubleSIMDMaskZ)),
    };
    for(u64 i = 0u; i < s_DoubleFrustumPlaneCount; ++i)
        outPlanes[i] = PlaneDoubleNormalizeSafe(TransformPlane(localPlanes[i], orientation, origin));
}

[[nodiscard]] inline bool FrustumPlanesIntersectAxisAlignedBox(const SIMDVectorDouble (&planes)[CollisionDetail::s_FrustumPlaneCount], const SIMDVectorDouble center, const SIMDVectorDouble extents)noexcept{
    for(u64 planeIndex = 0u; planeIndex < s_DoubleFrustumPlaneCount; ++planeIndex){
        SIMDVectorDouble outside{};
        SIMDVectorDouble inside{};
        FastIntersectAxisAlignedBoxPlane(center, extents, planes[planeIndex], outside, inside);
        if(VectorDouble4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectSphere(const SIMDVectorDouble (&planes)[CollisionDetail::s_FrustumPlaneCount], const SIMDVectorDouble centerRadius)noexcept{
    const SIMDVectorDouble center = SphereCenter(centerRadius);
    const SIMDVectorDouble radius = SphereRadius(centerRadius);
    for(u64 planeIndex = 0u; planeIndex < s_DoubleFrustumPlaneCount; ++planeIndex){
        SIMDVectorDouble outside{};
        SIMDVectorDouble inside{};
        FastIntersectSpherePlane(center, radius, planes[planeIndex], outside, inside);
        if(VectorDouble4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectOrientedBox(const SIMDVectorDouble (&planes)[CollisionDetail::s_FrustumPlaneCount], const SIMDVectorDouble center, const SIMDVectorDouble extents, const SIMDVectorDouble orientation)noexcept{
    SIMDVectorDouble axis0{};
    SIMDVectorDouble axis1{};
    SIMDVectorDouble axis2{};
    ObbAxes(orientation, axis0, axis1, axis2);
    for(u64 planeIndex = 0u; planeIndex < s_DoubleFrustumPlaneCount; ++planeIndex){
        SIMDVectorDouble outside{};
        SIMDVectorDouble inside{};
        FastIntersectOrientedBoxPlane(center, extents, axis0, axis1, axis2, planes[planeIndex], outside, inside);
        if(VectorDouble4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectPoints(const SIMDVectorDouble (&planes)[CollisionDetail::s_FrustumPlaneCount], const NotNull<const SIMDVectorDouble*> points, const usize pointCount)noexcept{
    for(u64 planeIndex = 0u; planeIndex < s_DoubleFrustumPlaneCount; ++planeIndex){
        SIMDVectorDouble outside{};
        SIMDVectorDouble inside{};
        FastIntersectPointsPlane(points, pointCount, planes[planeIndex], outside, inside);
        if(VectorDouble4AllTrue(outside))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ObbIntersectsObb(
    const SIMDVectorDouble lhsCenter,
    const SIMDVectorDouble lhsExtents,
    const SIMDVectorDouble lhsOrientation,
    const SIMDVectorDouble rhsCenter,
    const SIMDVectorDouble rhsExtents,
    const SIMDVectorDouble rhsOrientation
)noexcept{
    const SIMDVectorDouble relativeOrientation = QuaternionDoubleMultiply(QuaternionDoubleConjugate(lhsOrientation), rhsOrientation);
    SIMDMatrixDouble rotation = MatrixDoubleRotationQuaternion(relativeOrientation);
    const SIMDVectorDouble translation = VectorDouble3InverseRotate(VectorDoubleSubtract(rhsCenter, lhsCenter), lhsOrientation);

    const SIMDVectorDouble r0 = rotation.v[0];
    const SIMDVectorDouble r1 = rotation.v[1];
    const SIMDVectorDouble r2 = rotation.v[2];
    rotation = MatrixDoubleTranspose(rotation);
    const SIMDVectorDouble c0 = rotation.v[0];
    const SIMDVectorDouble c1 = rotation.v[1];
    const SIMDVectorDouble c2 = rotation.v[2];

    const SIMDVectorDouble ar0 = VectorDoubleAbs(r0);
    const SIMDVectorDouble ar1 = VectorDoubleAbs(r1);
    const SIMDVectorDouble ar2 = VectorDoubleAbs(r2);
    const SIMDVectorDouble ac0 = VectorDoubleAbs(c0);
    const SIMDVectorDouble ac1 = VectorDoubleAbs(c1);
    const SIMDVectorDouble ac2 = VectorDoubleAbs(c2);

    SIMDVectorDouble distance = VectorDoubleSplatX(translation);
    SIMDVectorDouble lhsRadius = VectorDoubleSplatX(lhsExtents);
    SIMDVectorDouble rhsRadius = VectorDouble3Dot(rhsExtents, ar0);
    SIMDVectorDouble noIntersection = VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius));

    distance = VectorDoubleSplatY(translation);
    lhsRadius = VectorDoubleSplatY(lhsExtents);
    rhsRadius = VectorDouble3Dot(rhsExtents, ar1);
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDoubleSplatZ(translation);
    lhsRadius = VectorDoubleSplatZ(lhsExtents);
    rhsRadius = VectorDouble3Dot(rhsExtents, ar2);
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDouble3Dot(translation, c0);
    lhsRadius = VectorDouble3Dot(lhsExtents, ac0);
    rhsRadius = VectorDoubleSplatX(rhsExtents);
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDouble3Dot(translation, c1);
    lhsRadius = VectorDouble3Dot(lhsExtents, ac1);
    rhsRadius = VectorDoubleSplatY(rhsExtents);
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDouble3Dot(translation, c2);
    lhsRadius = VectorDouble3Dot(lhsExtents, ac2);
    rhsRadius = VectorDoubleSplatZ(rhsExtents);
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDouble3Dot(translation, VectorDoublePermute<3, 6, 1, 0>(c0, VectorDoubleNegate(c0)));
    lhsRadius = VectorDouble3Dot(lhsExtents, VectorDoubleSwizzle<3, 2, 1, 0>(ac0));
    rhsRadius = VectorDouble3Dot(rhsExtents, VectorDoubleSwizzle<3, 2, 1, 0>(ar0));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDouble3Dot(translation, VectorDoublePermute<3, 6, 1, 0>(c1, VectorDoubleNegate(c1)));
    lhsRadius = VectorDouble3Dot(lhsExtents, VectorDoubleSwizzle<3, 2, 1, 0>(ac1));
    rhsRadius = VectorDouble3Dot(rhsExtents, VectorDoubleSwizzle<2, 3, 0, 1>(ar0));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDouble3Dot(translation, VectorDoublePermute<3, 6, 1, 0>(c2, VectorDoubleNegate(c2)));
    lhsRadius = VectorDouble3Dot(lhsExtents, VectorDoubleSwizzle<3, 2, 1, 0>(ac2));
    rhsRadius = VectorDouble3Dot(rhsExtents, VectorDoubleSwizzle<1, 0, 3, 2>(ar0));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDouble3Dot(translation, VectorDoublePermute<2, 3, 4, 1>(c0, VectorDoubleNegate(c0)));
    lhsRadius = VectorDouble3Dot(lhsExtents, VectorDoubleSwizzle<2, 3, 0, 1>(ac0));
    rhsRadius = VectorDouble3Dot(rhsExtents, VectorDoubleSwizzle<3, 2, 1, 0>(ar1));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDouble3Dot(translation, VectorDoublePermute<2, 3, 4, 1>(c1, VectorDoubleNegate(c1)));
    lhsRadius = VectorDouble3Dot(lhsExtents, VectorDoubleSwizzle<2, 3, 0, 1>(ac1));
    rhsRadius = VectorDouble3Dot(rhsExtents, VectorDoubleSwizzle<2, 3, 0, 1>(ar1));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDouble3Dot(translation, VectorDoublePermute<2, 3, 4, 1>(c2, VectorDoubleNegate(c2)));
    lhsRadius = VectorDouble3Dot(lhsExtents, VectorDoubleSwizzle<2, 3, 0, 1>(ac2));
    rhsRadius = VectorDouble3Dot(rhsExtents, VectorDoubleSwizzle<1, 0, 3, 2>(ar1));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDouble3Dot(translation, VectorDoublePermute<5, 0, 3, 2>(c0, VectorDoubleNegate(c0)));
    lhsRadius = VectorDouble3Dot(lhsExtents, VectorDoubleSwizzle<1, 0, 3, 2>(ac0));
    rhsRadius = VectorDouble3Dot(rhsExtents, VectorDoubleSwizzle<3, 2, 1, 0>(ar2));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDouble3Dot(translation, VectorDoublePermute<5, 0, 3, 2>(c1, VectorDoubleNegate(c1)));
    lhsRadius = VectorDouble3Dot(lhsExtents, VectorDoubleSwizzle<1, 0, 3, 2>(ac1));
    rhsRadius = VectorDouble3Dot(rhsExtents, VectorDoubleSwizzle<2, 3, 0, 1>(ar2));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    distance = VectorDouble3Dot(translation, VectorDoublePermute<5, 0, 3, 2>(c2, VectorDoubleNegate(c2)));
    lhsRadius = VectorDouble3Dot(lhsExtents, VectorDoubleSwizzle<1, 0, 3, 2>(ac2));
    rhsRadius = VectorDouble3Dot(rhsExtents, VectorDoubleSwizzle<1, 0, 3, 2>(ar2));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleGreater(VectorDoubleAbs(distance), VectorDoubleAdd(lhsRadius, rhsRadius)));

    return VectorDouble4NotEqualInt(noIntersection, VectorDoubleTrueInt());
}

[[nodiscard]] inline bool TriangleAabbOverlap(const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2, const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    const SIMDVectorDouble triangleMin = VectorDoubleMin(v0, VectorDoubleMin(v1, v2));
    const SIMDVectorDouble triangleMax = VectorDoubleMax(v0, VectorDoubleMax(v1, v2));
    if(!MinMaxIntersects(triangleMin, triangleMax, minBounds, maxBounds))
        return false;

    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble relativeV1 = VectorDoubleSubtract(v1, v0);
    const SIMDVectorDouble relativeV2 = VectorDoubleSubtract(v2, v0);
    const SIMDVectorDouble relativeMin = VectorDoubleSubtract(minBounds, v0);
    const SIMDVectorDouble relativeMax = VectorDoubleSubtract(maxBounds, v0);
    const auto crossAxis = [](const SIMDVectorDouble first, const SIMDVectorDouble second)noexcept{
        // Separate products preserve zero axes for parallel and collapsed edges.
        const SIMDVectorDouble positive = VectorDoubleMultiply(VectorDoubleSwizzle<1, 2, 0, 3>(first), VectorDoubleSwizzle<2, 0, 1, 3>(second));
        const SIMDVectorDouble negative = VectorDoubleMultiply(VectorDoubleSwizzle<2, 0, 1, 3>(first), VectorDoubleSwizzle<1, 2, 0, 3>(second));
        return VectorDoubleSubtract(positive, negative);
    };
    const auto separated = [&](const SIMDVectorDouble axis)noexcept{
        if(VectorDouble3Equal(axis, zero))
            return false;
        const SIMDVectorDouble p1 = VectorDouble3Dot(relativeV1, axis);
        const SIMDVectorDouble p2 = VectorDouble3Dot(relativeV2, axis);
        const SIMDVectorDouble minimumTriangle = VectorDoubleMin(zero, VectorDoubleMin(p1, p2));
        const SIMDVectorDouble maximumTriangle = VectorDoubleMax(zero, VectorDoubleMax(p1, p2));
        const SIMDVectorDouble firstBounds = VectorDoubleMultiply(relativeMin, axis);
        const SIMDVectorDouble secondBounds = VectorDoubleMultiply(relativeMax, axis);
        const SIMDVectorDouble minimumBox = VectorDouble3Dot(VectorDoubleMin(firstBounds, secondBounds), s_DoubleSIMDOne3);
        const SIMDVectorDouble maximumBox = VectorDouble3Dot(VectorDoubleMax(firstBounds, secondBounds), s_DoubleSIMDOne3);
        return VectorDouble4Less(maximumTriangle, minimumBox) || VectorDouble4Less(maximumBox, minimumTriangle);
    };
    if(separated(crossAxis(relativeV1, relativeV2)))
        return false;

    const auto edgeSeparated = [&](const SIMDVectorDouble edge)noexcept{
        return separated(crossAxis(edge, s_DoubleSIMDIdentityR0)) || separated(crossAxis(edge, s_DoubleSIMDIdentityR1)) || separated(crossAxis(edge, s_DoubleSIMDIdentityR2));
    };
    return !edgeSeparated(relativeV1) && !edgeSeparated(VectorDoubleSubtract(v2, v1)) && !edgeSeparated(VectorDoubleNegate(relativeV2));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

