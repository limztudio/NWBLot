// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


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

[[nodiscard]] NWB_INLINE bool Vector3AnyTrue(const SIMDVector value)noexcept{
    return (VectorMoveMask(value) & VectorComponentMask::s_XYZ) != 0u;
}

[[nodiscard]] NWB_INLINE bool Vector4AllTrue(const SIMDVector value)noexcept{
    return (VectorMoveMask(value) & VectorComponentMask::s_XYZW) == VectorComponentMask::s_XYZW;
}

[[nodiscard]] NWB_INLINE bool Vector4AnyTrue(const SIMDVector value)noexcept{
    return (VectorMoveMask(value) & VectorComponentMask::s_XYZW) != 0u;
}

[[nodiscard]] NWB_INLINE SIMDVector Vector3MaxComponent(const SIMDVector value)noexcept{
    return VectorMax(VectorSplatX(value), VectorMax(VectorSplatY(value), VectorSplatZ(value)));
}

[[nodiscard]] NWB_INLINE SIMDVector Vector3SignedUnitMask(const SIMDVector position, const SIMDVector componentMask)noexcept{
    const SIMDVector sign = VectorSelect(s_SIMDNegativeOne, s_SIMDOne, VectorGreaterOrEqual(position, VectorZero()));
    return VectorAndInt(sign, componentMask);
}

[[nodiscard]] NWB_INLINE SIMDVector CapsuleYSegmentPoint(const SIMDVector position, const SIMDVector radiusHalfHeight)noexcept{
    const SIMDVector halfHeight = VectorSplatY(radiusHalfHeight);
    const SIMDVector clampedY = VectorClamp(VectorSplatY(position), VectorNegate(halfHeight), halfHeight);
    return VectorAndInt(clampedY, s_SIMDMaskY);
}

[[nodiscard]] NWB_INLINE const Float3U* StrideFloat3Pointer(const Float3U* points, const usize stride, const usize index)noexcept{
    return reinterpret_cast<const Float3U*>(reinterpret_cast<const u8*>(points) + stride * index);
}

[[nodiscard]] NWB_INLINE SIMDVector SphereCenter(const SIMDVector centerRadius)noexcept{
    return VectorSetW(centerRadius, 0.0f);
}

[[nodiscard]] NWB_INLINE SIMDVector SphereRadius(const SIMDVector centerRadius)noexcept{
    return VectorSplatW(centerRadius);
}

[[nodiscard]] NWB_INLINE SIMDVector SphereCenterRadius(const SIMDVector center, const SIMDVector radius)noexcept{
    return VectorSelect(center, radius, s_SIMDMaskW);
}

[[nodiscard]] inline SIMDVector CreateSphereFromVectorPoints(const SIMDVector* points, const usize count)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(count > 0u);

    SIMDVector centerVector = VectorZero();
    for(usize pointIndex = 0u; pointIndex < count; ++pointIndex)
        centerVector = VectorAdd(centerVector, points[pointIndex]);
    centerVector = VectorDivide(centerVector, VectorReplicate(static_cast<f32>(count)));

    SIMDVector radiusSq = VectorZero();
    for(usize pointIndex = 0u; pointIndex < count; ++pointIndex){
        const SIMDVector delta = VectorSubtract(points[pointIndex], centerVector);
        radiusSq = VectorMax(radiusSq, Vector3LengthSq(delta));
    }

    return SphereCenterRadius(centerVector, VectorSqrt(radiusSq));
}

[[nodiscard]] NWB_INLINE SIMDVector BoxCornerOffset(const u32 index)noexcept{
    return VectorSet(
        (index & s_BoxCornerXSelectBit) ? 1.0f : -1.0f,
        (index & s_BoxCornerYSelectBit) ? 1.0f : -1.0f,
        (index & s_BoxCornerZSelectBit) ? 1.0f : -1.0f,
        0.0f
    );
}

[[nodiscard]] NWB_INLINE SIMDVector PlaneNormalizeSafe(const SIMDVector plane)noexcept{
    const SIMDVector length = Vector3Length(plane);
    if(Vector4LessOrEqual(length, VectorReplicate(s_PlaneEpsilon)))
        return plane;
    return VectorDivide(plane, length);
}

[[nodiscard]] NWB_INLINE SIMDVector TransformPlane(const SIMDVector plane, const SIMDVector rotation, const SIMDVector translation)noexcept{
    const SIMDVector normal = Vector3Rotate(plane, rotation);
    const SIMDVector distance = VectorSubtract(VectorSplatW(plane), Vector3Dot(normal, translation));
    return VectorSelect(normal, distance, s_SIMDMaskW);
}

[[nodiscard]] NWB_INLINE SIMDVector PlaneDistance(const SIMDVector plane, const SIMDVector point)noexcept{
    return VectorAdd(Vector3Dot(plane, point), VectorSplatW(plane));
}

[[nodiscard]] inline bool PointsInsideMinMax(
    const SIMDVector* points,
    const usize pointCount,
    const SIMDVector minBounds,
    const SIMDVector maxBounds
)noexcept{
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        const SIMDVector outside = VectorOrInt(VectorLess(points[pointIndex], minBounds), VectorGreater(points[pointIndex], maxBounds));
        if(Vector3AnyTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool PointsInsideSphere(
    const SIMDVector* points,
    const usize pointCount,
    const SIMDVector center,
    const SIMDVector radius
)noexcept{
    const SIMDVector radiusSq = VectorMultiply(radius, radius);
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        const SIMDVector delta = VectorSubtract(points[pointIndex], center);
        if(Vector4AnyTrue(VectorGreater(Vector3LengthSq(delta), radiusSq)))
            return false;
    }
    return true;
}

NWB_INLINE void MinMaxFromCenterExtents(
    const SIMDVector center,
    const SIMDVector extents,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds
)noexcept{
    outMinBounds = VectorSubtract(center, extents);
    outMaxBounds = VectorAdd(center, extents);
}

NWB_INLINE void CenterExtentsFromMinMax(
    const SIMDVector minBounds,
    const SIMDVector maxBounds,
    SIMDVector& outCenter,
    SIMDVector& outExtents
)noexcept{
    outCenter = VectorSetW(VectorMultiply(VectorAdd(minBounds, maxBounds), VectorReplicate(CollisionDetail::s_Half)), 0.0f);
    outExtents = VectorSetW(VectorMultiply(VectorSubtract(maxBounds, minBounds), VectorReplicate(CollisionDetail::s_Half)), 0.0f);
}

NWB_INLINE void ExpandMinMax(const SIMDVector point, SIMDVector& inOutMinBounds, SIMDVector& inOutMaxBounds)noexcept{
    inOutMinBounds = VectorMin(inOutMinBounds, point);
    inOutMaxBounds = VectorMax(inOutMaxBounds, point);
}

[[nodiscard]] NWB_INLINE SIMDVector ClosestPointOnMinMax(
    const SIMDVector point,
    const SIMDVector minBounds,
    const SIMDVector maxBounds
)noexcept{
    return VectorMin(VectorMax(point, minBounds), maxBounds);
}

[[nodiscard]] NWB_INLINE bool MinMaxIntersects(
    const SIMDVector lhsMinBounds,
    const SIMDVector lhsMaxBounds,
    const SIMDVector rhsMinBounds,
    const SIMDVector rhsMaxBounds
)noexcept{
    const SIMDVector disjoint = VectorOrInt(VectorGreater(lhsMinBounds, rhsMaxBounds), VectorGreater(rhsMinBounds, lhsMaxBounds));
    return !Vector3AnyTrue(disjoint);
}

NWB_INLINE void FastIntersectSpherePlane(
    const SIMDVector center,
    const SIMDVector radius,
    const SIMDVector plane,
    SIMDVector& outOutside,
    SIMDVector& outInside
)noexcept{
    const SIMDVector distance = PlaneDistance(plane, center);
    outOutside = VectorLess(distance, VectorNegate(radius));
    outInside = VectorGreaterOrEqual(distance, radius);
}

NWB_INLINE void FastIntersectAxisAlignedBoxPlane(
    const SIMDVector center,
    const SIMDVector extents,
    const SIMDVector plane,
    SIMDVector& outOutside,
    SIMDVector& outInside
)noexcept{
    const SIMDVector distance = PlaneDistance(plane, center);
    const SIMDVector radius = Vector3Dot(extents, VectorAbs(plane));
    outOutside = VectorLess(distance, VectorNegate(radius));
    outInside = VectorGreaterOrEqual(distance, radius);
}

NWB_INLINE void FastIntersectOrientedBoxPlane(
    const SIMDVector center,
    const SIMDVector extents,
    const SIMDVector axis0,
    const SIMDVector axis1,
    const SIMDVector axis2,
    const SIMDVector plane,
    SIMDVector& outOutside,
    SIMDVector& outInside
)noexcept{
    const SIMDVector distance = PlaneDistance(plane, center);
    SIMDVector radiusAxes = Vector3Dot(plane, axis0);
    radiusAxes = VectorSelect(radiusAxes, Vector3Dot(plane, axis1), VectorSelectControl(0u, 1u, 0u, 0u));
    radiusAxes = VectorSelect(radiusAxes, Vector3Dot(plane, axis2), VectorSelectControl(0u, 0u, 1u, 0u));
    const SIMDVector radius = Vector3Dot(extents, VectorAbs(radiusAxes));
    outOutside = VectorLess(distance, VectorNegate(radius));
    outInside = VectorGreaterOrEqual(distance, radius);
}

inline void FastIntersectPointsPlane(
    const SIMDVector* points,
    const usize pointCount,
    const SIMDVector plane,
    SIMDVector& outOutside,
    SIMDVector& outInside
)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(pointCount > 0u);

    SIMDVector minDistance = PlaneDistance(plane, points[0]);
    SIMDVector maxDistance = minDistance;
    for(usize pointIndex = 1u; pointIndex < pointCount; ++pointIndex){
        const SIMDVector distance = PlaneDistance(plane, points[pointIndex]);
        minDistance = VectorMin(minDistance, distance);
        maxDistance = VectorMax(maxDistance, distance);
    }

    outOutside = VectorLess(maxDistance, VectorZero());
    outInside = VectorGreaterOrEqual(minDistance, VectorZero());
}

[[nodiscard]] inline bool RayIntersectsMinMax(
    const SIMDVector origin,
    const SIMDVector direction,
    const SIMDVector minBounds,
    const SIMDVector maxBounds,
    f32& outDistance
)noexcept{
    const SIMDVector center = VectorScale(VectorAdd(minBounds, maxBounds), s_Half);
    const SIMDVector extents = VectorScale(VectorSubtract(maxBounds, minBounds), s_Half);
    const SIMDVector axisOrigin = VectorSubtract(center, origin);
    const SIMDVector isParallel = VectorLessOrEqual(VectorAbs(direction), VectorReplicate(s_RayEpsilon));
    const SIMDVector inverseDirection = VectorReciprocal(direction);
    const SIMDVector t1 = VectorMultiply(VectorSubtract(axisOrigin, extents), inverseDirection);
    const SIMDVector t2 = VectorMultiply(VectorAdd(axisOrigin, extents), inverseDirection);
    const SIMDVector negativeMax = VectorReplicate(-s_MaxF32);
    const SIMDVector positiveMax = VectorReplicate(s_MaxF32);

    SIMDVector tMin = VectorSelect(VectorMin(t1, t2), negativeMax, isParallel);
    SIMDVector tMax = VectorSelect(VectorMax(t1, t2), positiveMax, isParallel);
    tMin = VectorMax(tMin, VectorSplatY(tMin));
    tMin = VectorMax(tMin, VectorSplatZ(tMin));
    tMax = VectorMin(tMax, VectorSplatY(tMax));
    tMax = VectorMin(tMax, VectorSplatZ(tMax));

    SIMDVector noIntersection = VectorGreater(VectorSplatX(tMin), VectorSplatX(tMax));
    noIntersection = VectorOrInt(noIntersection, VectorLess(VectorSplatX(tMax), VectorZero()));
    noIntersection = VectorOrInt(noIntersection, VectorAndCInt(isParallel, VectorInBounds(axisOrigin, extents)));

    if(Vector3AnyTrue(noIntersection)){
        outDistance = 0.0f;
        return false;
    }

    outDistance = VectorGetX(tMin);
    return true;
}

[[nodiscard]] inline SIMDVector ClosestPointOnTriangle(
    const SIMDVector point,
    const SIMDVector a,
    const SIMDVector b,
    const SIMDVector c
)noexcept{
    const SIMDVector ab = VectorSubtract(b, a);
    const SIMDVector ac = VectorSubtract(c, a);
    const SIMDVector ap = VectorSubtract(point, a);
    const SIMDVector zero = VectorZero();
    const SIMDVector d1 = Vector3Dot(ab, ap);
    const SIMDVector d2 = Vector3Dot(ac, ap);
    if(Vector4LessOrEqual(d1, zero) && Vector4LessOrEqual(d2, zero))
        return a;

    const SIMDVector bp = VectorSubtract(point, b);
    const SIMDVector d3 = Vector3Dot(ab, bp);
    const SIMDVector d4 = Vector3Dot(ac, bp);
    if(Vector4GreaterOrEqual(d3, zero) && Vector4LessOrEqual(d4, d3))
        return b;

    const SIMDVector vc = VectorSubtract(VectorMultiply(d1, d4), VectorMultiply(d3, d2));
    if(Vector4LessOrEqual(vc, zero) && Vector4GreaterOrEqual(d1, zero) && Vector4LessOrEqual(d3, zero))
        return VectorMultiplyAdd(ab, VectorDivide(d1, VectorSubtract(d1, d3)), a);

    const SIMDVector cp = VectorSubtract(point, c);
    const SIMDVector d5 = Vector3Dot(ab, cp);
    const SIMDVector d6 = Vector3Dot(ac, cp);
    if(Vector4GreaterOrEqual(d6, zero) && Vector4LessOrEqual(d5, d6))
        return c;

    const SIMDVector vb = VectorSubtract(VectorMultiply(d5, d2), VectorMultiply(d1, d6));
    if(Vector4LessOrEqual(vb, zero) && Vector4GreaterOrEqual(d2, zero) && Vector4LessOrEqual(d6, zero))
        return VectorMultiplyAdd(ac, VectorDivide(d2, VectorSubtract(d2, d6)), a);

    const SIMDVector va = VectorSubtract(VectorMultiply(d3, d6), VectorMultiply(d5, d4));
    const SIMDVector d43 = VectorSubtract(d4, d3);
    const SIMDVector d56 = VectorSubtract(d5, d6);
    if(Vector4LessOrEqual(va, zero) && Vector4GreaterOrEqual(d43, zero) && Vector4GreaterOrEqual(d56, zero)){
        const SIMDVector bc = VectorSubtract(c, b);
        return VectorMultiplyAdd(bc, VectorDivide(d43, VectorAdd(d43, d56)), b);
    }

    const SIMDVector denom = VectorReciprocal(VectorAdd(va, VectorAdd(vb, vc)));
    return VectorMultiplyAdd(
        ac,
        VectorMultiply(vc, denom),
        VectorMultiplyAdd(ab, VectorMultiply(vb, denom), a)
    );
}

[[nodiscard]] inline bool RayIntersectsTriangle(
    const SIMDVector origin,
    const SIMDVector direction,
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2,
    f32& outDistance
)noexcept{
    const SIMDVector edge1 = VectorSubtract(v1, v0);
    const SIMDVector edge2 = VectorSubtract(v2, v0);
    const SIMDVector p = Vector3Cross(direction, edge2);
    const SIMDVector determinant = Vector3Dot(edge1, p);
    const SIMDVector zero = VectorZero();
    if(Vector4LessOrEqual(VectorAbs(determinant), VectorReplicate(s_RayEpsilon)))
        return false;

    const SIMDVector inverseDeterminant = VectorReciprocal(determinant);
    const SIMDVector s = VectorSubtract(origin, v0);
    const SIMDVector u = VectorMultiply(Vector3Dot(s, p), inverseDeterminant);
    if(Vector4Less(u, zero) || Vector4Greater(u, s_SIMDOne))
        return false;

    const SIMDVector q = Vector3Cross(s, edge1);
    const SIMDVector v = VectorMultiply(Vector3Dot(direction, q), inverseDeterminant);
    if(Vector4Less(v, zero) || Vector4Greater(VectorAdd(u, v), s_SIMDOne))
        return false;

    const SIMDVector t = VectorMultiply(Vector3Dot(edge2, q), inverseDeterminant);
    if(Vector4Less(t, zero))
        return false;

    outDistance = VectorGetX(t);
    return true;
}

NWB_INLINE void ObbAxes(
    const SIMDVector orientation,
    SIMDVector& outAxis0,
    SIMDVector& outAxis1,
    SIMDVector& outAxis2
)noexcept{
    outAxis0 = Vector3Rotate(s_SIMDIdentityR0, orientation);
    outAxis1 = Vector3Rotate(s_SIMDIdentityR1, orientation);
    outAxis2 = Vector3Rotate(s_SIMDIdentityR2, orientation);
}

[[nodiscard]] NWB_INLINE SIMDVector PointToObbLocal(
    const SIMDVector point,
    const SIMDVector center,
    const SIMDVector orientation
)noexcept{
    return Vector3InverseRotate(VectorSubtract(point, center), orientation);
}

[[nodiscard]] NWB_INLINE bool PointInsideObb(
    const SIMDVector point,
    const SIMDVector center,
    const SIMDVector extents,
    const SIMDVector orientation
)noexcept{
    return Vector3LessOrEqual(VectorAbs(PointToObbLocal(point, center, orientation)), extents);
}

[[nodiscard]] inline bool PointsInsideObb(
    const SIMDVector* points,
    const usize pointCount,
    const SIMDVector center,
    const SIMDVector extents,
    const SIMDVector orientation
)noexcept{
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        if(Vector3AnyTrue(VectorGreater(VectorAbs(PointToObbLocal(points[pointIndex], center, orientation)), extents)))
            return false;
    }
    return true;
}


[[nodiscard]] inline ContainmentType::Enum ContainmentFromPlaneTests(
    const SIMDVector* points,
    const usize pointCount,
    const SIMDVector* planes,
    const usize planeCount
)noexcept{
    SIMDVector anyIntersecting = VectorFalseInt();
    for(usize planeIndex = 0u; planeIndex < planeCount; ++planeIndex){
        SIMDVector outside{};
        SIMDVector inside{};
        FastIntersectPointsPlane(points, pointCount, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return ContainmentType::Disjoint;
        anyIntersecting = VectorOrInt(anyIntersecting, VectorEqualInt(inside, VectorFalseInt()));
    }
    return Vector4AllTrue(anyIntersecting) ? ContainmentType::Intersects : ContainmentType::Contains;
}

inline void FrustumPlanes(
    const SIMDVector origin,
    const SIMDVector orientation,
    const f32 rightSlope,
    const f32 leftSlope,
    const f32 topSlope,
    const f32 bottomSlope,
    const f32 nearPlane,
    const f32 farPlane,
    SIMDVector* outPlanes
)noexcept{
    const SIMDVector localPlanes[s_FrustumPlaneCount] = {
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

[[nodiscard]] inline bool FrustumPlanesIntersectAxisAlignedBox(
    const SIMDVector* planes,
    const SIMDVector center,
    const SIMDVector extents
)noexcept{
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVector outside{};
        SIMDVector inside{};
        FastIntersectAxisAlignedBoxPlane(center, extents, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectSphere(const SIMDVector* planes, const SIMDVector centerRadius)noexcept{
    const SIMDVector center = SphereCenter(centerRadius);
    const SIMDVector radius = SphereRadius(centerRadius);
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVector outside{};
        SIMDVector inside{};
        FastIntersectSpherePlane(center, radius, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectOrientedBox(
    const SIMDVector* planes,
    const SIMDVector center,
    const SIMDVector extents,
    const SIMDVector orientation
)noexcept{
    SIMDVector axis0{};
    SIMDVector axis1{};
    SIMDVector axis2{};
    ObbAxes(orientation, axis0, axis1, axis2);
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVector outside{};
        SIMDVector inside{};
        FastIntersectOrientedBoxPlane(center, extents, axis0, axis1, axis2, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool FrustumPlanesIntersectPoints(
    const SIMDVector* planes,
    const SIMDVector* points,
    const usize pointCount
)noexcept{
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVector outside{};
        SIMDVector inside{};
        FastIntersectPointsPlane(points, pointCount, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool ObbIntersectsObb(
    const SIMDVector lhsCenter,
    const SIMDVector lhsExtents,
    const SIMDVector lhsOrientation,
    const SIMDVector rhsCenter,
    const SIMDVector rhsExtents,
    const SIMDVector rhsOrientation
)noexcept{
    const SIMDVector relativeOrientation = QuaternionMultiply(lhsOrientation, QuaternionConjugate(rhsOrientation));
    SIMDMatrix rotation = MatrixRotationQuaternion(relativeOrientation);
    const SIMDVector translation = Vector3InverseRotate(VectorSubtract(rhsCenter, lhsCenter), lhsOrientation);

    const SIMDVector r0 = rotation.v[0];
    const SIMDVector r1 = rotation.v[1];
    const SIMDVector r2 = rotation.v[2];
    rotation = MatrixTranspose(rotation);
    const SIMDVector c0 = rotation.v[0];
    const SIMDVector c1 = rotation.v[1];
    const SIMDVector c2 = rotation.v[2];

    const SIMDVector ar0 = VectorAbs(r0);
    const SIMDVector ar1 = VectorAbs(r1);
    const SIMDVector ar2 = VectorAbs(r2);
    const SIMDVector ac0 = VectorAbs(c0);
    const SIMDVector ac1 = VectorAbs(c1);
    const SIMDVector ac2 = VectorAbs(c2);

    SIMDVector distance = VectorSplatX(translation);
    SIMDVector lhsRadius = VectorSplatX(lhsExtents);
    SIMDVector rhsRadius = Vector3Dot(rhsExtents, ar0);
    SIMDVector noIntersection = VectorGreater(VectorAbs(distance), VectorAdd(lhsRadius, rhsRadius));

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

[[nodiscard]] inline bool TriangleAabbOverlap(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2,
    const SIMDVector minBounds,
    const SIMDVector maxBounds
)noexcept{
    const SIMDVector triangleMin = VectorMin(v0, VectorMin(v1, v2));
    const SIMDVector triangleMax = VectorMax(v0, VectorMax(v1, v2));
    return MinMaxIntersects(triangleMin, triangleMax, minBounds, maxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

