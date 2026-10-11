// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "matrix_double.h"
#include "collision_types.h"


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
    return VectorDoubleSet(
        (index & s_DoubleBoxCornerXSelectBit) ? 1.0 : -1.0,
        (index & s_DoubleBoxCornerYSelectBit) ? 1.0 : -1.0,
        (index & s_DoubleBoxCornerZSelectBit) ? 1.0 : -1.0,
        0.0
    );
}

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

[[nodiscard]] inline bool PointsInsideMinMax(
    const SIMDVectorDouble* points,
    const usize pointCount,
    const SIMDVectorDouble minBounds,
    const SIMDVectorDouble maxBounds
)noexcept{
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        const SIMDVectorDouble outside = VectorDoubleOrInt(VectorDoubleLess(points[pointIndex], minBounds), VectorDoubleGreater(points[pointIndex], maxBounds));
        if(VectorDouble3AnyTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool PointsInsideSphere(
    const SIMDVectorDouble* points,
    const usize pointCount,
    const SIMDVectorDouble center,
    const SIMDVectorDouble radius
)noexcept{
    const SIMDVectorDouble radiusSq = VectorDoubleMultiply(radius, radius);
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        const SIMDVectorDouble delta = VectorDoubleSubtract(points[pointIndex], center);
        if(VectorDouble4AnyTrue(VectorDoubleGreater(VectorDouble3LengthSq(delta), radiusSq)))
            return false;
    }
    return true;
}

NWB_INLINE void MinMaxFromCenterExtents(
    const SIMDVectorDouble center,
    const SIMDVectorDouble extents,
    SIMDVectorDouble& outMinBounds,
    SIMDVectorDouble& outMaxBounds
)noexcept{
    outMinBounds = VectorDoubleSubtract(center, extents);
    outMaxBounds = VectorDoubleAdd(center, extents);
}

NWB_INLINE void CenterExtentsFromMinMax(
    const SIMDVectorDouble minBounds,
    const SIMDVectorDouble maxBounds,
    SIMDVectorDouble& outCenter,
    SIMDVectorDouble& outExtents
)noexcept{
    outCenter = VectorDoubleSetW(VectorDoubleMultiply(VectorDoubleAdd(minBounds, maxBounds), VectorDoubleReplicate(CollisionDoubleDetail::s_DoubleHalf)), 0.0);
    outExtents = VectorDoubleSetW(VectorDoubleMultiply(VectorDoubleSubtract(maxBounds, minBounds), VectorDoubleReplicate(CollisionDoubleDetail::s_DoubleHalf)), 0.0);
}

NWB_INLINE void ExpandMinMax(const SIMDVectorDouble point, SIMDVectorDouble& inOutMinBounds, SIMDVectorDouble& inOutMaxBounds)noexcept{
    inOutMinBounds = VectorDoubleMin(inOutMinBounds, point);
    inOutMaxBounds = VectorDoubleMax(inOutMaxBounds, point);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble ClosestPointOnMinMax(
    const SIMDVectorDouble point,
    const SIMDVectorDouble minBounds,
    const SIMDVectorDouble maxBounds
)noexcept{
    return VectorDoubleMin(VectorDoubleMax(point, minBounds), maxBounds);
}

[[nodiscard]] NWB_INLINE bool MinMaxIntersects(
    const SIMDVectorDouble lhsMinBounds,
    const SIMDVectorDouble lhsMaxBounds,
    const SIMDVectorDouble rhsMinBounds,
    const SIMDVectorDouble rhsMaxBounds
)noexcept{
    const SIMDVectorDouble disjoint = VectorDoubleOrInt(VectorDoubleGreater(lhsMinBounds, rhsMaxBounds), VectorDoubleGreater(rhsMinBounds, lhsMaxBounds));
    return !VectorDouble3AnyTrue(disjoint);
}

NWB_INLINE void FastIntersectSpherePlane(
    const SIMDVectorDouble center,
    const SIMDVectorDouble radius,
    const SIMDVectorDouble plane,
    SIMDVectorDouble& outOutside,
    SIMDVectorDouble& outInside
)noexcept{
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

NWB_INLINE void FastIntersectAxisAlignedBoxPlane(
    const SIMDVectorDouble center,
    const SIMDVectorDouble extents,
    const SIMDVectorDouble plane,
    SIMDVectorDouble& outOutside,
    SIMDVectorDouble& outInside
)noexcept{
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

inline void FastIntersectPointsPlane(
    const SIMDVectorDouble* points,
    const usize pointCount,
    const SIMDVectorDouble plane,
    SIMDVectorDouble& outOutside,
    SIMDVectorDouble& outInside
)noexcept{
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

[[nodiscard]] inline Expected<f64> RayIntersectsMinMax(
    const SIMDVectorDouble origin,
    const SIMDVectorDouble direction,
    const SIMDVectorDouble minBounds,
    const SIMDVectorDouble maxBounds
)noexcept{
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

[[nodiscard]] inline SIMDVectorDouble ClosestPointOnTriangle(
    const SIMDVectorDouble point,
    const SIMDVectorDouble a,
    const SIMDVectorDouble b,
    const SIMDVectorDouble c
)noexcept{
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
    return VectorDoubleMultiplyAdd(
        ac,
        VectorDoubleMultiply(vc, denom),
        VectorDoubleMultiplyAdd(ab, VectorDoubleMultiply(vb, denom), a)
    );
}

[[nodiscard]] inline Expected<f64> RayIntersectsTriangle(
    const SIMDVectorDouble origin,
    const SIMDVectorDouble direction,
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)noexcept{
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

NWB_INLINE void ObbAxes(
    const SIMDVectorDouble orientation,
    SIMDVectorDouble& outAxis0,
    SIMDVectorDouble& outAxis1,
    SIMDVectorDouble& outAxis2
)noexcept{
    outAxis0 = VectorDouble3Rotate(s_DoubleSIMDIdentityR0, orientation);
    outAxis1 = VectorDouble3Rotate(s_DoubleSIMDIdentityR1, orientation);
    outAxis2 = VectorDouble3Rotate(s_DoubleSIMDIdentityR2, orientation);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble PointToObbLocal(
    const SIMDVectorDouble point,
    const SIMDVectorDouble center,
    const SIMDVectorDouble orientation
)noexcept{
    return VectorDouble3InverseRotate(VectorDoubleSubtract(point, center), orientation);
}

[[nodiscard]] NWB_INLINE bool PointInsideObb(
    const SIMDVectorDouble point,
    const SIMDVectorDouble center,
    const SIMDVectorDouble extents,
    const SIMDVectorDouble orientation
)noexcept{
    return VectorDouble3LessOrEqual(VectorDoubleAbs(PointToObbLocal(point, center, orientation)), extents);
}

[[nodiscard]] inline bool PointsInsideObb(
    const SIMDVectorDouble* points,
    const usize pointCount,
    const SIMDVectorDouble center,
    const SIMDVectorDouble extents,
    const SIMDVectorDouble orientation
)noexcept{
    for(usize pointIndex = 0u; pointIndex < pointCount; ++pointIndex){
        if(VectorDouble3AnyTrue(VectorDoubleGreater(VectorDoubleAbs(PointToObbLocal(points[pointIndex], center, orientation)), extents)))
            return false;
    }
    return true;
}


[[nodiscard]] inline ContainmentType::Enum ContainmentFromPlaneTests(
    const SIMDVectorDouble* points,
    const usize pointCount,
    const SIMDVectorDouble* planes,
    const usize planeCount
)noexcept{
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

inline void FrustumPlanes(
    const SIMDVectorDouble origin,
    const SIMDVectorDouble orientation,
    const f64 rightSlope,
    const f64 leftSlope,
    const f64 topSlope,
    const f64 bottomSlope,
    const f64 nearPlane,
    const f64 farPlane,
    SIMDVectorDouble* outPlanes
)noexcept{
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

[[nodiscard]] inline bool FrustumPlanesIntersectAxisAlignedBox(
    const SIMDVectorDouble* planes,
    const SIMDVectorDouble center,
    const SIMDVectorDouble extents
)noexcept{
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

[[nodiscard]] inline bool FrustumPlanesIntersectOrientedBox(
    const SIMDVectorDouble* planes,
    const SIMDVectorDouble center,
    const SIMDVectorDouble extents,
    const SIMDVectorDouble orientation
)noexcept{
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

[[nodiscard]] inline bool FrustumPlanesIntersectPoints(
    const SIMDVectorDouble* planes,
    const SIMDVectorDouble* points,
    const usize pointCount
)noexcept{
    for(u64 planeIndex = 0u; planeIndex < s_DoubleFrustumPlaneCount; ++planeIndex){
        SIMDVectorDouble outside{};
        SIMDVectorDouble inside{};
        FastIntersectPointsPlane(points, pointCount, planes[planeIndex], outside, inside);
        if(VectorDouble4AllTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool ObbIntersectsObb(
    const SIMDVectorDouble lhsCenter,
    const SIMDVectorDouble lhsExtents,
    const SIMDVectorDouble lhsOrientation,
    const SIMDVectorDouble rhsCenter,
    const SIMDVectorDouble rhsExtents,
    const SIMDVectorDouble rhsOrientation
)noexcept{
    const SIMDVectorDouble relativeOrientation = QuaternionDoubleMultiply(lhsOrientation, QuaternionDoubleConjugate(rhsOrientation));
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

[[nodiscard]] inline bool TriangleAabbOverlap(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2,
    const SIMDVectorDouble minBounds,
    const SIMDVectorDouble maxBounds
)noexcept{
    const SIMDVectorDouble triangleMin = VectorDoubleMin(v0, VectorDoubleMin(v1, v2));
    const SIMDVectorDouble triangleMax = VectorDoubleMax(v0, VectorDoubleMax(v1, v2));
    return MinMaxIntersects(triangleMin, triangleMax, minBounds, maxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

