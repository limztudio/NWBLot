// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "../span.h"
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

[[nodiscard]] inline SIMDVectorSingle CreateSphereFromVectorPoints(const Span<const SIMDVectorSingle> points)noexcept{
    NWB_ASSERT(!points.empty());

    SIMDVectorSingle centerVector = VectorZero();
    for(usize pointIndex = 0u; pointIndex < points.size(); ++pointIndex)
        centerVector = VectorAdd(centerVector, points[pointIndex]);
    centerVector = VectorDivide(centerVector, VectorReplicate(static_cast<f32>(points.size())));

    SIMDVectorSingle radiusSq = VectorZero();
    for(usize pointIndex = 0u; pointIndex < points.size(); ++pointIndex){
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


[[nodiscard]] inline bool PointsInsideMinMax(const Span<const SIMDVectorSingle> points, const SIMDVectorSingle minBounds, const SIMDVectorSingle maxBounds)noexcept{
    for(usize pointIndex = 0u; pointIndex < points.size(); ++pointIndex){
        const SIMDVectorSingle outside = VectorOrInt(VectorLess(points[pointIndex], minBounds), VectorGreater(points[pointIndex], maxBounds));
        if(Vector3AnyTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool PointsInsideSphere(const Span<const SIMDVectorSingle> points, const SIMDVectorSingle center, const SIMDVectorSingle radius)noexcept{
    const SIMDVectorSingle radiusSq = VectorMultiply(radius, radius);
    for(usize pointIndex = 0u; pointIndex < points.size(); ++pointIndex){
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

[[nodiscard]] inline SIMDVectorDouble CreateSphereFromVectorPoints(const Span<const SIMDVectorDouble> points)noexcept{
    NWB_ASSERT(!points.empty());

    SIMDVectorDouble centerVector = VectorDoubleZero();
    for(usize pointIndex = 0u; pointIndex < points.size(); ++pointIndex)
        centerVector = VectorDoubleAdd(centerVector, points[pointIndex]);
    centerVector = VectorDoubleDivide(centerVector, VectorDoubleReplicate(static_cast<f64>(points.size())));

    SIMDVectorDouble radiusSq = VectorDoubleZero();
    for(usize pointIndex = 0u; pointIndex < points.size(); ++pointIndex){
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


[[nodiscard]] inline bool PointsInsideMinMax(const Span<const SIMDVectorDouble> points, const SIMDVectorDouble minBounds, const SIMDVectorDouble maxBounds)noexcept{
    for(usize pointIndex = 0u; pointIndex < points.size(); ++pointIndex){
        const SIMDVectorDouble outside = VectorDoubleOrInt(VectorDoubleLess(points[pointIndex], minBounds), VectorDoubleGreater(points[pointIndex], maxBounds));
        if(VectorDouble3AnyTrue(outside))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool PointsInsideSphere(const Span<const SIMDVectorDouble> points, const SIMDVectorDouble center, const SIMDVectorDouble radius)noexcept{
    const SIMDVectorDouble radiusSq = VectorDoubleMultiply(radius, radius);
    for(usize pointIndex = 0u; pointIndex < points.size(); ++pointIndex){
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
    const SIMDVectorDouble extentSpan = VectorDoubleSubtract(maxBounds, minBounds);
    outExtents = VectorDoubleSetW(VectorDoubleMultiply(extentSpan, VectorDoubleReplicate(CollisionDoubleDetail::s_DoubleHalf)), 0.0);
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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

