// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "matrix.h"
#include "collision_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CollisionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_RayEpsilon = 1.0e-20f;
inline constexpr f32 s_PlaneEpsilon = 1.192092896e-7f;
inline constexpr f32 s_Half = 0.5f;
inline constexpr usize s_TriangleVertexCount = 3u;
inline constexpr u32 s_FrustumPlaneCount = 6u;
inline constexpr u32 s_AabbCornerCount = 8u;
inline constexpr u32 s_BoxCornerXSelectBit = 1u;
inline constexpr u32 s_BoxCornerYSelectBit = 2u;
inline constexpr u32 s_BoxCornerZSelectBit = 4u;
inline constexpr u32 s_BoxCornerYSelectShift = 1u;
inline constexpr u32 s_BoxCornerZSelectShift = 2u;

namespace FrustumPlaneIndex{
    enum Enum : usize{
        Near,
        Far,
        Right,
        Left,
        Top,
        Bottom,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool Vector3AnyTrue(const SIMDVectorSingle value)noexcept{
    return (VectorMoveMask(value) & VectorComponentMask::s_XYZ) != 0u;
}

[[nodiscard]] NWB_INLINE bool Vector4AllTrue(const SIMDVectorSingle value)noexcept{
    return (VectorMoveMask(value) & VectorComponentMask::s_XYZW) == VectorComponentMask::s_XYZW;
}

[[nodiscard]] NWB_INLINE bool Vector4AnyTrue(const SIMDVectorSingle value)noexcept{
    return (VectorMoveMask(value) & VectorComponentMask::s_XYZW) != 0u;
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle Vector3MaxComponent(const SIMDVectorSingle value)noexcept{
    return VectorMax(VectorSplatX(value), VectorMax(VectorSplatY(value), VectorSplatZ(value)));
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle Vector3SignedUnitMask(const SIMDVectorSingle position, const SIMDVectorSingle componentMask)noexcept{
    const SIMDVectorSingle sign = VectorSelect(s_SIMDNegativeOne, s_SIMDOne, VectorGreaterOrEqual(position, VectorZero()));
    return VectorAndInt(sign, componentMask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle CapsuleYSegmentPoint(const SIMDVectorSingle position, const SIMDVectorSingle radiusHalfHeight)noexcept{
    const SIMDVectorSingle halfHeight = VectorSplatY(radiusHalfHeight);
    const SIMDVectorSingle clampedY = VectorClamp(VectorSplatY(position), VectorNegate(halfHeight), halfHeight);
    return VectorAndInt(clampedY, s_SIMDMaskY);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle SphereCenter(const SIMDVectorSingle centerRadius)noexcept{
    return VectorSetW(centerRadius, 0.0f);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle SphereRadius(const SIMDVectorSingle centerRadius)noexcept{
    return VectorSplatW(centerRadius);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle SphereCenterRadius(const SIMDVectorSingle center, const SIMDVectorSingle radius)noexcept{
    return VectorSelect(center, radius, s_SIMDMaskW);
}

[[nodiscard]] inline SIMDVectorSingle CreateSphereFromVectorPoints(const SIMDVectorSingle* points, const usize count)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(count > 0u);

    SIMDVectorSingle centerVector = VectorZero();
    for(usize pointIndex = 0u; pointIndex < count; ++pointIndex)
        centerVector = VectorAdd(centerVector, points[pointIndex]);
    centerVector = VectorDivide(centerVector, VectorReplicate(static_cast<f32>(count)));

    SIMDVectorSingle radiusSq = VectorZero();
    for(usize pointIndex = 0u; pointIndex < count; ++pointIndex){
        const SIMDVectorSingle delta = VectorSubtract(points[pointIndex], centerVector);
        radiusSq = VectorMax(radiusSq, Vector3LengthSq(delta));
    }

    return SphereCenterRadius(centerVector, VectorSqrt(radiusSq));
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle BoxCornerOffset(const u32 index)noexcept{
    return VectorSet((index & s_BoxCornerXSelectBit) ? 1.0f : -1.0f, (index & s_BoxCornerYSelectBit) ? 1.0f : -1.0f, (index & s_BoxCornerZSelectBit) ? 1.0f : -1.0f, 0.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle PlaneNormalizeSafe(const SIMDVectorSingle plane)noexcept{
    const SIMDVectorSingle length = Vector3Length(plane);
    if(Vector4LessOrEqual(length, VectorReplicate(s_PlaneEpsilon)))
        return plane;
    return VectorDivide(plane, length);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle TransformPlane(const SIMDVectorSingle plane, const SIMDVectorSingle rotation, const SIMDVectorSingle translation)noexcept{
    const SIMDVectorSingle normal = Vector3Rotate(plane, rotation);
    const SIMDVectorSingle distance = VectorSubtract(VectorSplatW(plane), Vector3Dot(normal, translation));
    return VectorSelect(normal, distance, s_SIMDMaskW);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle PlaneDistance(const SIMDVectorSingle plane, const SIMDVectorSingle point)noexcept{
    return VectorAdd(Vector3Dot(plane, point), VectorSplatW(plane));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool PointsInsideMinMax(const SIMDVectorSingle* points, const usize pointCount, const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        const SIMDVectorSingle outside = VectorOrInt(VectorLess(points[pointIndex], minBounds), VectorGreater(points[pointIndex], maxBounds));
        if(Vector3AnyTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool PointsInsideSphere(const SIMDVectorSingle* points, const usize pointCount, const SIMDVectorSingle center, const SIMDVectorSingle radius)noexcept{
    const SIMDVectorSingle radiusSq = VectorMultiply(radius, radius);
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        const SIMDVectorSingle delta = VectorSubtract(points[pointIndex], center);
        if(Vector4AnyTrue(VectorGreater(Vector3LengthSq(delta), radiusSq)))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void MinMaxFromCenterExtents(const SIMDVectorSingle center, const SIMDVectorSingle extents, SIMDVectorSingle& outMinBounds, SIMDVectorSingle& outMaxBounds)noexcept{
    outMinBounds = VectorSubtract(center, extents);
    outMaxBounds = VectorAdd(center, extents);
}

NWB_INLINE void CenterExtentsFromMinMax(const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents)noexcept{
    outCenter = VectorSetW(VectorMultiply(VectorAdd(minBounds, maxBounds), VectorReplicate(CollisionDetail::s_Half)), 0.0f);
    outExtents = VectorSetW(VectorMultiply(VectorSubtract(maxBounds, minBounds), VectorReplicate(CollisionDetail::s_Half)), 0.0f);
}

NWB_INLINE void ExpandMinMax(const SIMDVectorSingle point, SIMDVectorSingle& inOutMinBounds, SIMDVectorSingle& inOutMaxBounds)noexcept{
    inOutMinBounds = VectorMin(inOutMinBounds, point);
    inOutMaxBounds = VectorMax(inOutMaxBounds, point);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle ClosestPointOnMinMax(const SIMDVectorSingle point, const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    return VectorMin(VectorMax(point, minBounds), maxBounds);
}

[[nodiscard]] NWB_INLINE bool MinMaxIntersects(const SIMDVectorSingle lhsMinBounds, const SIMDVectorSingle lhsMaxBounds, const SIMDVectorSingle rhsMinBounds, const SIMDVectorSingle rhsMaxBounds)noexcept{
    const SIMDVectorSingle disjoint = VectorOrInt(VectorGreater(lhsMinBounds, rhsMaxBounds), VectorGreater(rhsMinBounds, lhsMaxBounds));
    return !Vector3AnyTrue(disjoint);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void FastIntersectSpherePlane(const SIMDVectorSingle center, const SIMDVectorSingle radius, const SIMDVectorSingle plane, SIMDVectorSingle& outOutside, SIMDVectorSingle& outInside)noexcept{
    const SIMDVectorSingle distance = PlaneDistance(plane, center);
    outOutside = VectorLess(distance, VectorNegate(radius));
    outInside = VectorGreaterOrEqual(distance, radius);
}

[[nodiscard]] NWB_INLINE ContainmentType::Enum NWB_SIMD_CALL ContainmentFromSpherePlaneTests(const SIMDVectorSingle sphereValue, const SIMDVectorSingle (&planes)[s_FrustumPlaneCount])noexcept{
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

inline void FastIntersectPointsPlane(const SIMDVectorSingle* points, const usize pointCount, const SIMDVectorSingle plane, SIMDVectorSingle& outOutside, SIMDVectorSingle& outInside)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(pointCount > 0u);

    SIMDVectorSingle minDistance = PlaneDistance(plane, points[0]);
    SIMDVectorSingle maxDistance = minDistance;
    for(usize pointIndex = 1u; pointIndex < pointCount; ++pointIndex){
        const SIMDVectorSingle distance = PlaneDistance(plane, points[pointIndex]);
        minDistance = VectorMin(minDistance, distance);
        maxDistance = VectorMax(maxDistance, distance);
    }

    outOutside = VectorLess(maxDistance, VectorZero());
    outInside = VectorGreaterOrEqual(minDistance, VectorZero());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f32> RayIntersectsMinMax(const SIMDVectorSingle origin, const SIMDVectorSingle direction, const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    const SIMDVectorSingle center = VectorScale(VectorAdd(minBounds, maxBounds), s_Half);
    const SIMDVectorSingle extents = VectorScale(VectorSubtract(maxBounds, minBounds), s_Half);
    const SIMDVectorSingle axisOrigin = VectorSubtract(center, origin);
    const SIMDVectorSingle isParallel = VectorLessOrEqual(VectorAbs(direction), VectorReplicate(s_RayEpsilon));
    const SIMDVectorSingle inverseDirection = VectorReciprocal(direction);
    const SIMDVectorSingle t1 = VectorMultiply(VectorSubtract(axisOrigin, extents), inverseDirection);
    const SIMDVectorSingle t2 = VectorMultiply(VectorAdd(axisOrigin, extents), inverseDirection);
    const SIMDVectorSingle negativeMax = VectorReplicate(-s_MaxF32);
    const SIMDVectorSingle positiveMax = VectorReplicate(s_MaxF32);

    SIMDVectorSingle tMin = VectorSelect(VectorMin(t1, t2), negativeMax, isParallel);
    SIMDVectorSingle tMax = VectorSelect(VectorMax(t1, t2), positiveMax, isParallel);
    tMin = VectorMax(tMin, VectorSplatY(tMin));
    tMin = VectorMax(tMin, VectorSplatZ(tMin));
    tMax = VectorMin(tMax, VectorSplatY(tMax));
    tMax = VectorMin(tMax, VectorSplatZ(tMax));

    SIMDVectorSingle noIntersection = VectorGreater(VectorSplatX(tMin), VectorSplatX(tMax));
    noIntersection = VectorOrInt(noIntersection, VectorLess(VectorSplatX(tMax), VectorZero()));
    noIntersection = VectorOrInt(noIntersection, VectorAndCInt(isParallel, VectorInBounds(axisOrigin, extents)));

    if(Vector3AnyTrue(noIntersection)){
        return MakeUnexpected(Failure{});
    }

    return VectorGetX(tMin);
}

[[nodiscard]] inline SIMDVectorSingle ClosestPointOnTriangle(const SIMDVectorSingle point, const SIMDVectorSingle a, const SIMDVectorSingle b, const SIMDVectorSingle c)noexcept{
    const SIMDVectorSingle ab = VectorSubtract(b, a);
    const SIMDVectorSingle ac = VectorSubtract(c, a);
    const SIMDVectorSingle ap = VectorSubtract(point, a);
    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle d1 = Vector3Dot(ab, ap);
    const SIMDVectorSingle d2 = Vector3Dot(ac, ap);
    if(Vector4LessOrEqual(d1, zero) && Vector4LessOrEqual(d2, zero))
        return a;

    const SIMDVectorSingle bp = VectorSubtract(point, b);
    const SIMDVectorSingle d3 = Vector3Dot(ab, bp);
    const SIMDVectorSingle d4 = Vector3Dot(ac, bp);
    if(Vector4GreaterOrEqual(d3, zero) && Vector4LessOrEqual(d4, d3))
        return b;

    const SIMDVectorSingle vc = VectorSubtract(VectorMultiply(d1, d4), VectorMultiply(d3, d2));
    if(Vector4LessOrEqual(vc, zero) && Vector4GreaterOrEqual(d1, zero) && Vector4LessOrEqual(d3, zero))
        return VectorMultiplyAdd(ab, VectorDivide(d1, VectorSubtract(d1, d3)), a);

    const SIMDVectorSingle cp = VectorSubtract(point, c);
    const SIMDVectorSingle d5 = Vector3Dot(ab, cp);
    const SIMDVectorSingle d6 = Vector3Dot(ac, cp);
    if(Vector4GreaterOrEqual(d6, zero) && Vector4LessOrEqual(d5, d6))
        return c;

    const SIMDVectorSingle vb = VectorSubtract(VectorMultiply(d5, d2), VectorMultiply(d1, d6));
    if(Vector4LessOrEqual(vb, zero) && Vector4GreaterOrEqual(d2, zero) && Vector4LessOrEqual(d6, zero))
        return VectorMultiplyAdd(ac, VectorDivide(d2, VectorSubtract(d2, d6)), a);

    const SIMDVectorSingle va = VectorSubtract(VectorMultiply(d3, d6), VectorMultiply(d5, d4));
    const SIMDVectorSingle d43 = VectorSubtract(d4, d3);
    const SIMDVectorSingle d56 = VectorSubtract(d5, d6);
    if(Vector4LessOrEqual(va, zero) && Vector4GreaterOrEqual(d43, zero) && Vector4GreaterOrEqual(d56, zero)){
        const SIMDVectorSingle bc = VectorSubtract(c, b);
        return VectorMultiplyAdd(bc, VectorDivide(d43, VectorAdd(d43, d56)), b);
    }

    const SIMDVectorSingle denom = VectorReciprocal(VectorAdd(va, VectorAdd(vb, vc)));
    return VectorMultiplyAdd(ac, VectorMultiply(vc, denom), VectorMultiplyAdd(ab, VectorMultiply(vb, denom), a));
}

[[nodiscard]] inline Expected<f32> RayIntersectsTriangle(const SIMDVectorSingle origin, const SIMDVectorSingle direction, const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2)noexcept{
    const SIMDVectorSingle edge1 = VectorSubtract(v1, v0);
    const SIMDVectorSingle edge2 = VectorSubtract(v2, v0);
    const SIMDVectorSingle p = Vector3Cross(direction, edge2);
    const SIMDVectorSingle determinant = Vector3Dot(edge1, p);
    const SIMDVectorSingle zero = VectorZero();
    if(Vector4LessOrEqual(VectorAbs(determinant), VectorReplicate(s_RayEpsilon)))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle inverseDeterminant = VectorReciprocal(determinant);
    const SIMDVectorSingle s = VectorSubtract(origin, v0);
    const SIMDVectorSingle u = VectorMultiply(Vector3Dot(s, p), inverseDeterminant);
    if(Vector4Less(u, zero) || Vector4Greater(u, s_SIMDOne))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle q = Vector3Cross(s, edge1);
    const SIMDVectorSingle v = VectorMultiply(Vector3Dot(direction, q), inverseDeterminant);
    if(Vector4Less(v, zero) || Vector4Greater(VectorAdd(u, v), s_SIMDOne))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle t = VectorMultiply(Vector3Dot(edge2, q), inverseDeterminant);
    if(Vector4Less(t, zero))
        return MakeUnexpected(Failure{});

    return VectorGetX(t);
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
        FastIntersectPointsPlane(points, pointCount, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return ContainmentType::Disjoint;
        anyIntersecting = VectorOrInt(anyIntersecting, VectorEqualInt(inside, VectorFalseInt()));
    }
    return Vector4AllTrue(anyIntersecting) ? ContainmentType::Intersects : ContainmentType::Contains;
}

inline void FrustumPlanes(const SIMDVectorSingle origin, const SIMDVectorSingle orientation, const f32 rightSlope, const f32 leftSlope, const f32 topSlope, const f32 bottomSlope, const f32 nearPlane, const f32 farPlane, SIMDVectorSingle* outPlanes)noexcept{
    const SIMDVectorSingle localPlanes[s_FrustumPlaneCount] = {
        VectorSet(0.0f, 0.0f, 1.0f, -nearPlane),
        VectorSet(0.0f, 0.0f, -1.0f, farPlane),
        VectorSet(-1.0f, 0.0f, rightSlope, 0.0f),
        VectorSet(1.0f, 0.0f, -leftSlope, 0.0f),
        VectorSet(0.0f, -1.0f, topSlope, 0.0f),
        VectorSet(0.0f, 1.0f, -bottomSlope, 0.0f),
    };
    for(u32 i = 0u; i < s_FrustumPlaneCount; ++i)
        outPlanes[i] = PlaneNormalizeSafe(TransformPlane(localPlanes[i], orientation, origin));
}

[[nodiscard]] inline bool FrustumPlanesIntersectAxisAlignedBox(const SIMDVectorSingle* planes, const SIMDVectorSingle center, const SIMDVectorSingle extents)noexcept{
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVectorSingle outside{};
        SIMDVectorSingle inside{};
        FastIntersectAxisAlignedBoxPlane(center, extents, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectSphere(const SIMDVectorSingle* planes, const SIMDVectorSingle centerRadius)noexcept{
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

[[nodiscard]] inline bool FrustumPlanesIntersectOrientedBox(const SIMDVectorSingle* planes, const SIMDVectorSingle center, const SIMDVectorSingle extents, const SIMDVectorSingle orientation)noexcept{
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

[[nodiscard]] inline bool FrustumPlanesIntersectPoints(const SIMDVectorSingle* planes, const SIMDVectorSingle* points, const usize pointCount)noexcept{
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


inline constexpr f64 s_DoubleRayEpsilon = 1.0e-20;
inline constexpr f64 s_DoublePlaneEpsilon = 0x1p-52;
inline constexpr f64 s_DoubleHalf = 0.5;
inline constexpr usize s_DoubleTriangleVertexCount = 3u;
inline constexpr u64 s_DoubleFrustumPlaneCount = 6u;
inline constexpr u64 s_DoubleAabbCornerCount = 8u;
inline constexpr u64 s_DoubleBoxCornerXSelectBit = 1u;
inline constexpr u64 s_DoubleBoxCornerYSelectBit = 2u;
inline constexpr u64 s_DoubleBoxCornerZSelectBit = 4u;
inline constexpr u64 s_DoubleBoxCornerYSelectShift = 1u;
inline constexpr u64 s_DoubleBoxCornerZSelectShift = 2u;

namespace FrustumPlaneIndex{
    enum Enum : usize{
        Near,
        Far,
        Right,
        Left,
        Top,
        Bottom,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool VectorDouble3AnyTrue(const SIMDVectorDouble value)noexcept{
    return (VectorDoubleMoveMask(value) & VectorDoubleComponentMask::s_DoubleXYZ) != 0u;
}

[[nodiscard]] NWB_INLINE bool VectorDouble4AllTrue(const SIMDVectorDouble value)noexcept{
    return (VectorDoubleMoveMask(value) & VectorDoubleComponentMask::s_DoubleXYZW) == VectorDoubleComponentMask::s_DoubleXYZW;
}

[[nodiscard]] NWB_INLINE bool VectorDouble4AnyTrue(const SIMDVectorDouble value)noexcept{
    return (VectorDoubleMoveMask(value) & VectorDoubleComponentMask::s_DoubleXYZW) != 0u;
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble VectorDouble3MaxComponent(const SIMDVectorDouble value)noexcept{
    return VectorDoubleMax(VectorDoubleSplatX(value), VectorDoubleMax(VectorDoubleSplatY(value), VectorDoubleSplatZ(value)));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble VectorDouble3SignedUnitMask(const SIMDVectorDouble position, const SIMDVectorDouble componentMask)noexcept{
    const SIMDVectorDouble sign = VectorDoubleSelect(s_DoubleSIMDNegativeOne, s_DoubleSIMDOne, VectorDoubleGreaterOrEqual(position, VectorDoubleZero()));
    return VectorDoubleAndInt(sign, componentMask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble CapsuleYSegmentPoint(const SIMDVectorDouble position, const SIMDVectorDouble radiusHalfHeight)noexcept{
    const SIMDVectorDouble halfHeight = VectorDoubleSplatY(radiusHalfHeight);
    const SIMDVectorDouble clampedY = VectorDoubleClamp(VectorDoubleSplatY(position), VectorDoubleNegate(halfHeight), halfHeight);
    return VectorDoubleAndInt(clampedY, s_DoubleSIMDMaskY);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble SphereCenter(const SIMDVectorDouble centerRadius)noexcept{
    return VectorDoubleSetW(centerRadius, 0.0);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble SphereRadius(const SIMDVectorDouble centerRadius)noexcept{
    return VectorDoubleSplatW(centerRadius);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble SphereCenterRadius(const SIMDVectorDouble center, const SIMDVectorDouble radius)noexcept{
    return VectorDoubleSelect(center, radius, s_DoubleSIMDMaskW);
}

[[nodiscard]] inline SIMDVectorDouble CreateSphereFromVectorPoints(const SIMDVectorDouble* points, const usize count)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(count > 0u);

    SIMDVectorDouble centerVector = VectorDoubleZero();
    for(usize pointIndex = 0u; pointIndex < count; ++pointIndex)
        centerVector = VectorDoubleAdd(centerVector, points[pointIndex]);
    centerVector = VectorDoubleDivide(centerVector, VectorDoubleReplicate(static_cast<f64>(count)));

    SIMDVectorDouble radiusSq = VectorDoubleZero();
    for(usize pointIndex = 0u; pointIndex < count; ++pointIndex){
        const SIMDVectorDouble delta = VectorDoubleSubtract(points[pointIndex], centerVector);
        radiusSq = VectorDoubleMax(radiusSq, VectorDouble3LengthSq(delta));
    }

    return SphereCenterRadius(centerVector, VectorDoubleSqrt(radiusSq));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble BoxCornerOffset(const u64 index)noexcept{
    return VectorDoubleSet((index & s_DoubleBoxCornerXSelectBit) ? 1.0 : -1.0, (index & s_DoubleBoxCornerYSelectBit) ? 1.0 : -1.0, (index & s_DoubleBoxCornerZSelectBit) ? 1.0 : -1.0, 0.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble PlaneDoubleNormalizeSafe(const SIMDVectorDouble plane)noexcept{
    const SIMDVectorDouble length = VectorDouble3Length(plane);
    if(VectorDouble4LessOrEqual(length, VectorDoubleReplicate(s_DoublePlaneEpsilon)))
        return plane;
    return VectorDoubleDivide(plane, length);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble TransformPlane(const SIMDVectorDouble plane, const SIMDVectorDouble rotation, const SIMDVectorDouble translation)noexcept{
    const SIMDVectorDouble normal = VectorDouble3Rotate(plane, rotation);
    const SIMDVectorDouble distance = VectorDoubleSubtract(VectorDoubleSplatW(plane), VectorDouble3Dot(normal, translation));
    return VectorDoubleSelect(normal, distance, s_DoubleSIMDMaskW);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble PlaneDoubleDistance(const SIMDVectorDouble plane, const SIMDVectorDouble point)noexcept{
    return VectorDoubleAdd(VectorDouble3Dot(plane, point), VectorDoubleSplatW(plane));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool PointsInsideMinMax(const SIMDVectorDouble* points, const usize pointCount, const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        const SIMDVectorDouble outside = VectorDoubleOrInt(VectorDoubleLess(points[pointIndex], minBounds), VectorDoubleGreater(points[pointIndex], maxBounds));
        if(VectorDouble3AnyTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool PointsInsideSphere(const SIMDVectorDouble* points, const usize pointCount, const SIMDVectorDouble center, const SIMDVectorDouble radius)noexcept{
    const SIMDVectorDouble radiusSq = VectorDoubleMultiply(radius, radius);
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        const SIMDVectorDouble delta = VectorDoubleSubtract(points[pointIndex], center);
        if(VectorDouble4AnyTrue(VectorDoubleGreater(VectorDouble3LengthSq(delta), radiusSq)))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void MinMaxFromCenterExtents(const SIMDVectorDouble center, const SIMDVectorDouble extents, SIMDVectorDouble& outMinBounds, SIMDVectorDouble& outMaxBounds)noexcept{
    outMinBounds = VectorDoubleSubtract(center, extents);
    outMaxBounds = VectorDoubleAdd(center, extents);
}

NWB_INLINE void CenterExtentsFromMinMax(const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents)noexcept{
    outCenter = VectorDoubleSetW(VectorDoubleMultiply(VectorDoubleAdd(minBounds, maxBounds), VectorDoubleReplicate(CollisionDoubleDetail::s_DoubleHalf)), 0.0);
    outExtents = VectorDoubleSetW(VectorDoubleMultiply(VectorDoubleSubtract(maxBounds, minBounds), VectorDoubleReplicate(CollisionDoubleDetail::s_DoubleHalf)), 0.0);
}

NWB_INLINE void ExpandMinMax(const SIMDVectorDouble point, SIMDVectorDouble& inOutMinBounds, SIMDVectorDouble& inOutMaxBounds)noexcept{
    inOutMinBounds = VectorDoubleMin(inOutMinBounds, point);
    inOutMaxBounds = VectorDoubleMax(inOutMaxBounds, point);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble ClosestPointOnMinMax(const SIMDVectorDouble point, const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    return VectorDoubleMin(VectorDoubleMax(point, minBounds), maxBounds);
}

[[nodiscard]] NWB_INLINE bool MinMaxIntersects(const SIMDVectorDouble lhsMinBounds, const SIMDVectorDouble lhsMaxBounds, const SIMDVectorDouble rhsMinBounds, const SIMDVectorDouble rhsMaxBounds)noexcept{
    const SIMDVectorDouble disjoint = VectorDoubleOrInt(VectorDoubleGreater(lhsMinBounds, rhsMaxBounds), VectorDoubleGreater(rhsMinBounds, lhsMaxBounds));
    return !VectorDouble3AnyTrue(disjoint);
}


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

inline void FastIntersectPointsPlane(const SIMDVectorDouble* points, const usize pointCount, const SIMDVectorDouble plane, SIMDVectorDouble& outOutside, SIMDVectorDouble& outInside)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(pointCount > 0u);

    SIMDVectorDouble minDistance = PlaneDoubleDistance(plane, points[0]);
    SIMDVectorDouble maxDistance = minDistance;
    for(usize pointIndex = 1u; pointIndex < pointCount; ++pointIndex){
        const SIMDVectorDouble distance = PlaneDoubleDistance(plane, points[pointIndex]);
        minDistance = VectorDoubleMin(minDistance, distance);
        maxDistance = VectorDoubleMax(maxDistance, distance);
    }

    outOutside = VectorDoubleLess(maxDistance, VectorDoubleZero());
    outInside = VectorDoubleGreaterOrEqual(minDistance, VectorDoubleZero());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f64> RayIntersectsMinMax(const SIMDVectorDouble origin, const SIMDVectorDouble direction, const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    const SIMDVectorDouble center = VectorDoubleScale(VectorDoubleAdd(minBounds, maxBounds), s_DoubleHalf);
    const SIMDVectorDouble extents = VectorDoubleScale(VectorDoubleSubtract(maxBounds, minBounds), s_DoubleHalf);
    const SIMDVectorDouble axisOrigin = VectorDoubleSubtract(center, origin);
    const SIMDVectorDouble isParallel = VectorDoubleLessOrEqual(VectorDoubleAbs(direction), VectorDoubleReplicate(s_DoubleRayEpsilon));
    const SIMDVectorDouble inverseDirection = VectorDoubleReciprocal(direction);
    const SIMDVectorDouble t1 = VectorDoubleMultiply(VectorDoubleSubtract(axisOrigin, extents), inverseDirection);
    const SIMDVectorDouble t2 = VectorDoubleMultiply(VectorDoubleAdd(axisOrigin, extents), inverseDirection);
    const SIMDVectorDouble negativeMax = VectorDoubleReplicate(-Limit<f64>::s_Max);
    const SIMDVectorDouble positiveMax = VectorDoubleReplicate(Limit<f64>::s_Max);

    SIMDVectorDouble tMin = VectorDoubleSelect(VectorDoubleMin(t1, t2), negativeMax, isParallel);
    SIMDVectorDouble tMax = VectorDoubleSelect(VectorDoubleMax(t1, t2), positiveMax, isParallel);
    tMin = VectorDoubleMax(tMin, VectorDoubleSplatY(tMin));
    tMin = VectorDoubleMax(tMin, VectorDoubleSplatZ(tMin));
    tMax = VectorDoubleMin(tMax, VectorDoubleSplatY(tMax));
    tMax = VectorDoubleMin(tMax, VectorDoubleSplatZ(tMax));

    SIMDVectorDouble noIntersection = VectorDoubleGreater(VectorDoubleSplatX(tMin), VectorDoubleSplatX(tMax));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleLess(VectorDoubleSplatX(tMax), VectorDoubleZero()));
    noIntersection = VectorDoubleOrInt(noIntersection, VectorDoubleAndCInt(isParallel, VectorDoubleInBounds(axisOrigin, extents)));

    if(VectorDouble3AnyTrue(noIntersection)){
        return MakeUnexpected(Failure{});
    }

    return VectorDoubleGetX(tMin);
}

[[nodiscard]] inline SIMDVectorDouble ClosestPointOnTriangle(const SIMDVectorDouble point, const SIMDVectorDouble a, const SIMDVectorDouble b, const SIMDVectorDouble c)noexcept{
    const SIMDVectorDouble ab = VectorDoubleSubtract(b, a);
    const SIMDVectorDouble ac = VectorDoubleSubtract(c, a);
    const SIMDVectorDouble ap = VectorDoubleSubtract(point, a);
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble d1 = VectorDouble3Dot(ab, ap);
    const SIMDVectorDouble d2 = VectorDouble3Dot(ac, ap);
    if(VectorDouble4LessOrEqual(d1, zero) && VectorDouble4LessOrEqual(d2, zero))
        return a;

    const SIMDVectorDouble bp = VectorDoubleSubtract(point, b);
    const SIMDVectorDouble d3 = VectorDouble3Dot(ab, bp);
    const SIMDVectorDouble d4 = VectorDouble3Dot(ac, bp);
    if(VectorDouble4GreaterOrEqual(d3, zero) && VectorDouble4LessOrEqual(d4, d3))
        return b;

    const SIMDVectorDouble vc = VectorDoubleSubtract(VectorDoubleMultiply(d1, d4), VectorDoubleMultiply(d3, d2));
    if(VectorDouble4LessOrEqual(vc, zero) && VectorDouble4GreaterOrEqual(d1, zero) && VectorDouble4LessOrEqual(d3, zero))
        return VectorDoubleMultiplyAdd(ab, VectorDoubleDivide(d1, VectorDoubleSubtract(d1, d3)), a);

    const SIMDVectorDouble cp = VectorDoubleSubtract(point, c);
    const SIMDVectorDouble d5 = VectorDouble3Dot(ab, cp);
    const SIMDVectorDouble d6 = VectorDouble3Dot(ac, cp);
    if(VectorDouble4GreaterOrEqual(d6, zero) && VectorDouble4LessOrEqual(d5, d6))
        return c;

    const SIMDVectorDouble vb = VectorDoubleSubtract(VectorDoubleMultiply(d5, d2), VectorDoubleMultiply(d1, d6));
    if(VectorDouble4LessOrEqual(vb, zero) && VectorDouble4GreaterOrEqual(d2, zero) && VectorDouble4LessOrEqual(d6, zero))
        return VectorDoubleMultiplyAdd(ac, VectorDoubleDivide(d2, VectorDoubleSubtract(d2, d6)), a);

    const SIMDVectorDouble va = VectorDoubleSubtract(VectorDoubleMultiply(d3, d6), VectorDoubleMultiply(d5, d4));
    const SIMDVectorDouble d43 = VectorDoubleSubtract(d4, d3);
    const SIMDVectorDouble d56 = VectorDoubleSubtract(d5, d6);
    if(VectorDouble4LessOrEqual(va, zero) && VectorDouble4GreaterOrEqual(d43, zero) && VectorDouble4GreaterOrEqual(d56, zero)){
        const SIMDVectorDouble bc = VectorDoubleSubtract(c, b);
        return VectorDoubleMultiplyAdd(bc, VectorDoubleDivide(d43, VectorDoubleAdd(d43, d56)), b);
    }

    const SIMDVectorDouble denom = VectorDoubleReciprocal(VectorDoubleAdd(va, VectorDoubleAdd(vb, vc)));
    return VectorDoubleMultiplyAdd(ac, VectorDoubleMultiply(vc, denom), VectorDoubleMultiplyAdd(ab, VectorDoubleMultiply(vb, denom), a));
}

[[nodiscard]] inline Expected<f64> RayIntersectsTriangle(const SIMDVectorDouble origin, const SIMDVectorDouble direction, const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2)noexcept{
    const SIMDVectorDouble edge1 = VectorDoubleSubtract(v1, v0);
    const SIMDVectorDouble edge2 = VectorDoubleSubtract(v2, v0);
    const SIMDVectorDouble p = VectorDouble3Cross(direction, edge2);
    const SIMDVectorDouble determinant = VectorDouble3Dot(edge1, p);
    const SIMDVectorDouble zero = VectorDoubleZero();
    if(VectorDouble4LessOrEqual(VectorDoubleAbs(determinant), VectorDoubleReplicate(s_DoubleRayEpsilon)))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble inverseDeterminant = VectorDoubleReciprocal(determinant);
    const SIMDVectorDouble s = VectorDoubleSubtract(origin, v0);
    const SIMDVectorDouble u = VectorDoubleMultiply(VectorDouble3Dot(s, p), inverseDeterminant);
    if(VectorDouble4Less(u, zero) || VectorDouble4Greater(u, s_DoubleSIMDOne))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble q = VectorDouble3Cross(s, edge1);
    const SIMDVectorDouble v = VectorDoubleMultiply(VectorDouble3Dot(direction, q), inverseDeterminant);
    if(VectorDouble4Less(v, zero) || VectorDouble4Greater(VectorDoubleAdd(u, v), s_DoubleSIMDOne))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble t = VectorDoubleMultiply(VectorDouble3Dot(edge2, q), inverseDeterminant);
    if(VectorDouble4Less(t, zero))
        return MakeUnexpected(Failure{});

    return VectorDoubleGetX(t);
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
        FastIntersectPointsPlane(points, pointCount, planes[planeIndex], outside, inside);
        if(VectorDouble4AllTrue(outside))
            return ContainmentType::Disjoint;
        anyIntersecting = VectorDoubleOrInt(anyIntersecting, VectorDoubleEqualInt(inside, VectorDoubleFalseInt()));
    }
    return VectorDouble4AllTrue(anyIntersecting) ? ContainmentType::Intersects : ContainmentType::Contains;
}

inline void FrustumPlanes(const SIMDVectorDouble origin, const SIMDVectorDouble orientation, const f64 rightSlope, const f64 leftSlope, const f64 topSlope, const f64 bottomSlope, const f64 nearPlane, const f64 farPlane, SIMDVectorDouble* outPlanes)noexcept{
    const SIMDVectorDouble localPlanes[s_DoubleFrustumPlaneCount] = {
        VectorDoubleSet(0.0, 0.0, 1.0, -nearPlane),
        VectorDoubleSet(0.0, 0.0, -1.0, farPlane),
        VectorDoubleSet(-1.0, 0.0, rightSlope, 0.0),
        VectorDoubleSet(1.0, 0.0, -leftSlope, 0.0),
        VectorDoubleSet(0.0, -1.0, topSlope, 0.0),
        VectorDoubleSet(0.0, 1.0, -bottomSlope, 0.0),
    };
    for(u64 i = 0u; i < s_DoubleFrustumPlaneCount; ++i)
        outPlanes[i] = PlaneDoubleNormalizeSafe(TransformPlane(localPlanes[i], orientation, origin));
}

[[nodiscard]] inline bool FrustumPlanesIntersectAxisAlignedBox(const SIMDVectorDouble* planes, const SIMDVectorDouble center, const SIMDVectorDouble extents)noexcept{
    for(u64 planeIndex = 0u; planeIndex < s_DoubleFrustumPlaneCount; ++planeIndex){
        SIMDVectorDouble outside{};
        SIMDVectorDouble inside{};
        FastIntersectAxisAlignedBoxPlane(center, extents, planes[planeIndex], outside, inside);
        if(VectorDouble4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectSphere(const SIMDVectorDouble* planes, const SIMDVectorDouble centerRadius)noexcept{
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

[[nodiscard]] inline bool FrustumPlanesIntersectOrientedBox(const SIMDVectorDouble* planes, const SIMDVectorDouble center, const SIMDVectorDouble extents, const SIMDVectorDouble orientation)noexcept{
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

[[nodiscard]] inline bool FrustumPlanesIntersectPoints(const SIMDVectorDouble* planes, const SIMDVectorDouble* points, const usize pointCount)noexcept{
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

