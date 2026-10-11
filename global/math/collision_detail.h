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

[[nodiscard]] NWB_INLINE SIMDVectorSingle CapsuleYSegmentPoint(const SIMDVectorSingle position, const SIMDVectorSingle radiusHalfHeight)noexcept{
    const SIMDVectorSingle halfHeight = VectorSplatY(radiusHalfHeight);
    const SIMDVectorSingle clampedY = VectorClamp(VectorSplatY(position), VectorNegate(halfHeight), halfHeight);
    return VectorAndInt(clampedY, s_SIMDMaskY);
}

[[nodiscard]] NWB_INLINE const Float3U* StrideFloat3Pointer(const Float3U* points, const usize stride, const usize index)noexcept{
    return reinterpret_cast<const Float3U*>(reinterpret_cast<const u8*>(points) + stride * index);
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
    return VectorSet(
        (index & s_BoxCornerXSelectBit) ? 1.0f : -1.0f,
        (index & s_BoxCornerYSelectBit) ? 1.0f : -1.0f,
        (index & s_BoxCornerZSelectBit) ? 1.0f : -1.0f,
        0.0f
    );
}

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

[[nodiscard]] inline bool PointsInsideMinMax(
    const SIMDVectorSingle* points,
    const usize pointCount,
    const SIMDVectorSingle minBounds,
    const SIMDVectorSingle maxBounds
)noexcept{
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        const SIMDVectorSingle outside = VectorOrInt(VectorLess(points[pointIndex], minBounds), VectorGreater(points[pointIndex], maxBounds));
        if(Vector3AnyTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool PointsInsideSphere(
    const SIMDVectorSingle* points,
    const usize pointCount,
    const SIMDVectorSingle center,
    const SIMDVectorSingle radius
)noexcept{
    const SIMDVectorSingle radiusSq = VectorMultiply(radius, radius);
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        const SIMDVectorSingle delta = VectorSubtract(points[pointIndex], center);
        if(Vector4AnyTrue(VectorGreater(Vector3LengthSq(delta), radiusSq)))
            return false;
    }
    return true;
}

NWB_INLINE void MinMaxFromCenterExtents(
    const SIMDVectorSingle center,
    const SIMDVectorSingle extents,
    SIMDVectorSingle& outMinBounds,
    SIMDVectorSingle& outMaxBounds
)noexcept{
    outMinBounds = VectorSubtract(center, extents);
    outMaxBounds = VectorAdd(center, extents);
}

NWB_INLINE void CenterExtentsFromMinMax(
    const SIMDVectorSingle minBounds,
    const SIMDVectorSingle maxBounds,
    SIMDVectorSingle& outCenter,
    SIMDVectorSingle& outExtents
)noexcept{
    outCenter = VectorSetW(VectorMultiply(VectorAdd(minBounds, maxBounds), VectorReplicate(CollisionDetail::s_Half)), 0.0f);
    outExtents = VectorSetW(VectorMultiply(VectorSubtract(maxBounds, minBounds), VectorReplicate(CollisionDetail::s_Half)), 0.0f);
}

NWB_INLINE void ExpandMinMax(const SIMDVectorSingle point, SIMDVectorSingle& inOutMinBounds, SIMDVectorSingle& inOutMaxBounds)noexcept{
    inOutMinBounds = VectorMin(inOutMinBounds, point);
    inOutMaxBounds = VectorMax(inOutMaxBounds, point);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle ClosestPointOnMinMax(
    const SIMDVectorSingle point,
    const SIMDVectorSingle minBounds,
    const SIMDVectorSingle maxBounds
)noexcept{
    return VectorMin(VectorMax(point, minBounds), maxBounds);
}

[[nodiscard]] NWB_INLINE bool MinMaxIntersects(
    const SIMDVectorSingle lhsMinBounds,
    const SIMDVectorSingle lhsMaxBounds,
    const SIMDVectorSingle rhsMinBounds,
    const SIMDVectorSingle rhsMaxBounds
)noexcept{
    const SIMDVectorSingle disjoint = VectorOrInt(VectorGreater(lhsMinBounds, rhsMaxBounds), VectorGreater(rhsMinBounds, lhsMaxBounds));
    return !Vector3AnyTrue(disjoint);
}

NWB_INLINE void FastIntersectSpherePlane(
    const SIMDVectorSingle center,
    const SIMDVectorSingle radius,
    const SIMDVectorSingle plane,
    SIMDVectorSingle& outOutside,
    SIMDVectorSingle& outInside
)noexcept{
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

NWB_INLINE void FastIntersectAxisAlignedBoxPlane(
    const SIMDVectorSingle center,
    const SIMDVectorSingle extents,
    const SIMDVectorSingle plane,
    SIMDVectorSingle& outOutside,
    SIMDVectorSingle& outInside
)noexcept{
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

inline void FastIntersectPointsPlane(
    const SIMDVectorSingle* points,
    const usize pointCount,
    const SIMDVectorSingle plane,
    SIMDVectorSingle& outOutside,
    SIMDVectorSingle& outInside
)noexcept{
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

[[nodiscard]] inline Expected<f32> RayIntersectsMinMax(
    const SIMDVectorSingle origin,
    const SIMDVectorSingle direction,
    const SIMDVectorSingle minBounds,
    const SIMDVectorSingle maxBounds
)noexcept{
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

[[nodiscard]] inline SIMDVectorSingle ClosestPointOnTriangle(
    const SIMDVectorSingle point,
    const SIMDVectorSingle a,
    const SIMDVectorSingle b,
    const SIMDVectorSingle c
)noexcept{
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
    return VectorMultiplyAdd(
        ac,
        VectorMultiply(vc, denom),
        VectorMultiplyAdd(ab, VectorMultiply(vb, denom), a)
    );
}

[[nodiscard]] inline Expected<f32> RayIntersectsTriangle(
    const SIMDVectorSingle origin,
    const SIMDVectorSingle direction,
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)noexcept{
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

NWB_INLINE void ObbAxes(
    const SIMDVectorSingle orientation,
    SIMDVectorSingle& outAxis0,
    SIMDVectorSingle& outAxis1,
    SIMDVectorSingle& outAxis2
)noexcept{
    outAxis0 = Vector3Rotate(s_SIMDIdentityR0, orientation);
    outAxis1 = Vector3Rotate(s_SIMDIdentityR1, orientation);
    outAxis2 = Vector3Rotate(s_SIMDIdentityR2, orientation);
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle PointToObbLocal(
    const SIMDVectorSingle point,
    const SIMDVectorSingle center,
    const SIMDVectorSingle orientation
)noexcept{
    return Vector3InverseRotate(VectorSubtract(point, center), orientation);
}

[[nodiscard]] NWB_INLINE bool PointInsideObb(
    const SIMDVectorSingle point,
    const SIMDVectorSingle center,
    const SIMDVectorSingle extents,
    const SIMDVectorSingle orientation
)noexcept{
    return Vector3LessOrEqual(VectorAbs(PointToObbLocal(point, center, orientation)), extents);
}

[[nodiscard]] inline bool PointsInsideObb(
    const SIMDVectorSingle* points,
    const usize pointCount,
    const SIMDVectorSingle center,
    const SIMDVectorSingle extents,
    const SIMDVectorSingle orientation
)noexcept{
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        if(Vector3AnyTrue(VectorGreater(VectorAbs(PointToObbLocal(points[pointIndex], center, orientation)), extents)))
            return false;
    }
    return true;
}


[[nodiscard]] inline ContainmentType::Enum ContainmentFromPlaneTests(
    const SIMDVectorSingle* points,
    const usize pointCount,
    const SIMDVectorSingle* planes,
    const usize planeCount
)noexcept{
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

inline void FrustumPlanes(
    const SIMDVectorSingle origin,
    const SIMDVectorSingle orientation,
    const f32 rightSlope,
    const f32 leftSlope,
    const f32 topSlope,
    const f32 bottomSlope,
    const f32 nearPlane,
    const f32 farPlane,
    SIMDVectorSingle* outPlanes
)noexcept{
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

[[nodiscard]] inline bool FrustumPlanesIntersectAxisAlignedBox(
    const SIMDVectorSingle* planes,
    const SIMDVectorSingle center,
    const SIMDVectorSingle extents
)noexcept{
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

[[nodiscard]] inline bool FrustumPlanesIntersectOrientedBox(
    const SIMDVectorSingle* planes,
    const SIMDVectorSingle center,
    const SIMDVectorSingle extents,
    const SIMDVectorSingle orientation
)noexcept{
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

[[nodiscard]] inline bool FrustumPlanesIntersectPoints(
    const SIMDVectorSingle* planes,
    const SIMDVectorSingle* points,
    const usize pointCount
)noexcept{
    for(u32 planeIndex = 0u; planeIndex < s_FrustumPlaneCount; ++planeIndex){
        SIMDVectorSingle outside{};
        SIMDVectorSingle inside{};
        FastIntersectPointsPlane(points, pointCount, planes[planeIndex], outside, inside);
        if(Vector4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool ObbIntersectsObb(
    const SIMDVectorSingle lhsCenter,
    const SIMDVectorSingle lhsExtents,
    const SIMDVectorSingle lhsOrientation,
    const SIMDVectorSingle rhsCenter,
    const SIMDVectorSingle rhsExtents,
    const SIMDVectorSingle rhsOrientation
)noexcept{
    const SIMDVectorSingle relativeOrientation = QuaternionMultiply(lhsOrientation, QuaternionConjugate(rhsOrientation));
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

[[nodiscard]] inline bool TriangleAabbOverlap(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2,
    const SIMDVectorSingle minBounds,
    const SIMDVectorSingle maxBounds
)noexcept{
    const SIMDVectorSingle triangleMin = VectorMin(v0, VectorMin(v1, v2));
    const SIMDVectorSingle triangleMax = VectorMax(v0, VectorMax(v1, v2));
    return MinMaxIntersects(triangleMin, triangleMax, minBounds, maxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

