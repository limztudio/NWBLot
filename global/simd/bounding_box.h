// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "matrix.h"
#include "collision_detail_ray.h"
#include "collision_detail_volume.h"
#include "collision_plane.h"
#include "collision_sdf.h"
#include "collision_aabb.h"
#include "collision_triangle.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bounding_shape_decls.h"
#include "bounding_corners.h"
#include "bounding_frustum_tests.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingBox::TransformBoxValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, const SIMDMatrix& matrix, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents)noexcept{
    SIMDVectorSingle corners[s_CornerCount];
    CollisionDetail::AabbCorners(boxCenter, boxExtents, corners);
    SIMDVectorSingle minBounds = Vector3Transform(corners[0], matrix);
    SIMDVectorSingle maxBounds = minBounds;
    for(u32 i = 1u; i < s_CornerCount; ++i)
        CollisionDetail::ExpandMinMax(Vector3Transform(corners[i], matrix), minBounds, maxBounds);
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
}

inline void NWB_SIMD_CALL BoundingBox::TransformBoxValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents)noexcept{
    SIMDVectorSingle corners[s_CornerCount];
    CollisionDetail::AabbCorners(boxCenter, boxExtents, corners);
    const SIMDVectorSingle scaleVector = VectorReplicate(scale);
    SIMDVectorSingle minBounds = VectorAdd(Vector3Rotate(VectorMultiply(corners[0], scaleVector), rotation), translation);
    SIMDVectorSingle maxBounds = minBounds;
    for(u32 i = 1u; i < s_CornerCount; ++i){
        const SIMDVectorSingle transformed = VectorAdd(Vector3Rotate(VectorMultiply(corners[i], scaleVector), rotation), translation);
        CollisionDetail::ExpandMinMax(transformed, minBounds, maxBounds);
    }
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
}

inline void NWB_SIMD_CALL BoundingDoubleBox::TransformBoxValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, const SIMDMatrixDouble& matrix, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents)noexcept{
    SIMDVectorDouble corners[s_DoubleCornerCount];
    CollisionDoubleDetail::AabbCorners(boxCenter, boxExtents, corners);
    SIMDVectorDouble minBounds = VectorDouble3Transform(corners[0], matrix);
    SIMDVectorDouble maxBounds = minBounds;
    for(u64 i = 1u; i < s_DoubleCornerCount; ++i)
        CollisionDoubleDetail::ExpandMinMax(VectorDouble3Transform(corners[i], matrix), minBounds, maxBounds);
    CollisionDoubleDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
}

inline void NWB_SIMD_CALL BoundingDoubleBox::TransformBoxValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents)noexcept{
    SIMDVectorDouble corners[s_DoubleCornerCount];
    CollisionDoubleDetail::AabbCorners(boxCenter, boxExtents, corners);
    const SIMDVectorDouble scaleVector = VectorDoubleReplicate(scale);
    SIMDVectorDouble minBounds = VectorDoubleAdd(VectorDouble3Rotate(VectorDoubleMultiply(corners[0], scaleVector), rotation), translation);
    SIMDVectorDouble maxBounds = minBounds;
    for(u64 i = 1u; i < s_DoubleCornerCount; ++i){
        const SIMDVectorDouble transformed = VectorDoubleAdd(VectorDouble3Rotate(VectorDoubleMultiply(corners[i], scaleVector), rotation), translation);
        CollisionDoubleDetail::ExpandMinMax(transformed, minBounds, maxBounds);
    }
    CollisionDoubleDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingBox::CornersValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle (&outCorners)[BoundingBox::s_CornerCount])noexcept{
    CollisionDetail::AabbCorners(boxCenter, boxExtents, outCorners);
}

inline void NWB_SIMD_CALL BoundingDoubleBox::CornersValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble (&outCorners)[BoundingDoubleBox::s_DoubleCornerCount])noexcept{
    CollisionDoubleDetail::AabbCorners(boxCenter, boxExtents, outCorners);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingBox::ContainsPointValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle point)noexcept{
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return Vector3GreaterOrEqual(point, minBounds) && Vector3LessOrEqual(point, maxBounds) ? ContainmentType::Contains : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::ContainsPointValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble point)noexcept{
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return VectorDouble3GreaterOrEqual(point, minBounds) && VectorDouble3LessOrEqual(point, maxBounds) ? ContainmentType::Contains : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingBox::ContainsTriangleValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept{
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    const SIMDVectorSingle points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(points, CollisionDetail::s_TriangleVertexCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return IntersectsTriangleValue(boxCenter, boxExtents, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::ContainsTriangleValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    const SIMDVectorDouble points[CollisionDoubleDetail::s_DoubleTriangleVertexCount] = { v0, v1, v2 };
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDoubleDetail::PointsInsideMinMax(points, CollisionDoubleDetail::s_DoubleTriangleVertexCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return IntersectsTriangleValue(boxCenter, boxExtents, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingBox::IntersectsTriangleValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept{
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return CollisionDetail::TriangleAabbOverlap(v0, v1, v2, minBounds, maxBounds);
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleBox::IntersectsTriangleValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return CollisionDoubleDetail::TriangleAabbOverlap(v0, v1, v2, minBounds, maxBounds);
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingBox::IntersectsPlaneValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle plane)noexcept{
    SIMDVectorSingle outside{};
    SIMDVectorSingle inside{};
    CollisionDetail::FastIntersectAxisAlignedBoxPlane(boxCenter, boxExtents, plane, outside, inside);
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleBox::IntersectsPlaneValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble plane)noexcept{
    SIMDVectorDouble outside{};
    SIMDVectorDouble inside{};
    CollisionDoubleDetail::FastIntersectAxisAlignedBoxPlane(boxCenter, boxExtents, plane, outside, inside);
    if(CollisionDoubleDetail::VectorDouble4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDoubleDetail::VectorDouble4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingBox::IntersectsRayValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle origin, SIMDVectorSingle direction)noexcept{
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return CollisionDetail::RayIntersectsMinMax(origin, direction, minBounds, maxBounds);
}

[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleBox::IntersectsRayValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble origin, SIMDVectorDouble direction)noexcept{
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return CollisionDoubleDetail::RayIntersectsMinMax(origin, direction, minBounds, maxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingBox::ContainedByValue(
    SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5
)noexcept{
    SIMDVectorSingle points[s_CornerCount];
    CollisionDetail::AabbCorners(boxCenter, boxExtents, points);
    const SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(points, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::ContainedByValue(
    SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5
)noexcept{
    SIMDVectorDouble points[s_DoubleCornerCount];
    CollisionDoubleDetail::AabbCorners(boxCenter, boxExtents, points);
    const SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDoubleDetail::ContainmentFromPlaneTests(points, s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingBox::MergeBoxValues(SIMDVectorSingle boxCenter0, SIMDVectorSingle boxExtents0, SIMDVectorSingle boxCenter1, SIMDVectorSingle boxExtents1, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents)noexcept{
    SIMDVectorSingle min0{};
    SIMDVectorSingle max0{};
    SIMDVectorSingle min1{};
    SIMDVectorSingle max1{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter0, boxExtents0, min0, max0);
    CollisionDetail::MinMaxFromCenterExtents(boxCenter1, boxExtents1, min1, max1);
    CollisionDetail::CenterExtentsFromMinMax(VectorMin(min0, min1), VectorMax(max0, max1), outCenter, outExtents);
}

inline void NWB_SIMD_CALL BoundingDoubleBox::MergeBoxValues(SIMDVectorDouble boxCenter0, SIMDVectorDouble boxExtents0, SIMDVectorDouble boxCenter1, SIMDVectorDouble boxExtents1, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents)noexcept{
    SIMDVectorDouble min0{};
    SIMDVectorDouble max0{};
    SIMDVectorDouble min1{};
    SIMDVectorDouble max1{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter0, boxExtents0, min0, max0);
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter1, boxExtents1, min1, max1);
    CollisionDoubleDetail::CenterExtentsFromMinMax(VectorDoubleMin(min0, min1), VectorDoubleMax(max0, max1), outCenter, outExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingBox::containsSphereValues(const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle sphereValue)const noexcept{
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    const SIMDVectorSingle sphereCenter = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVectorSingle sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVectorSingle closestPoint = CollisionDetail::ClosestPointOnMinMax(sphereCenter, minBounds, maxBounds);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(Vector3LengthSq(VectorSubtract(closestPoint, sphereCenter)), VectorMultiply(sphereRadius, sphereRadius))))
        return ContainmentType::Disjoint;

    if(Vector3GreaterOrEqual(VectorSubtract(sphereCenter, sphereRadius), minBounds) && Vector3LessOrEqual(VectorAdd(sphereCenter, sphereRadius), maxBounds))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::containsSphereValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble sphereValue)const noexcept{
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    const SIMDVectorDouble sphereCenter = CollisionDoubleDetail::SphereCenter(sphereValue);
    const SIMDVectorDouble sphereRadius = CollisionDoubleDetail::SphereRadius(sphereValue);
    const SIMDVectorDouble closestPoint = CollisionDoubleDetail::ClosestPointOnMinMax(sphereCenter, minBounds, maxBounds);
    if(CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleGreater(VectorDouble3LengthSq(VectorDoubleSubtract(closestPoint, sphereCenter)), VectorDoubleMultiply(sphereRadius, sphereRadius))))
        return ContainmentType::Disjoint;

    if(VectorDouble3GreaterOrEqual(VectorDoubleSubtract(sphereCenter, sphereRadius), minBounds) && VectorDouble3LessOrEqual(VectorDoubleAdd(sphereCenter, sphereRadius), maxBounds))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingBox::containsBoxValues(const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents)const noexcept{
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    SIMDVectorSingle otherMin{};
    SIMDVectorSingle otherMax{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    CollisionDetail::MinMaxFromCenterExtents(otherBoxCenter, otherBoxExtents, otherMin, otherMax);
    if(!CollisionDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax))
        return ContainmentType::Disjoint;
    if(Vector3GreaterOrEqual(otherMin, minBounds) && Vector3LessOrEqual(otherMax, maxBounds))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::containsBoxValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents)const noexcept{
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    SIMDVectorDouble otherMin{};
    SIMDVectorDouble otherMax{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    CollisionDoubleDetail::MinMaxFromCenterExtents(otherBoxCenter, otherBoxExtents, otherMin, otherMax);
    if(!CollisionDoubleDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax))
        return ContainmentType::Disjoint;
    if(VectorDouble3GreaterOrEqual(otherMin, minBounds) && VectorDouble3LessOrEqual(otherMax, maxBounds))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingBox::containsOrientedBoxValues(
    const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation
)const noexcept{
    SIMDVectorSingle corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(corners, BoundingOrientedBox::s_CornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return intersectsOrientedBoxValues(boxCenter, boxExtents, otherBoxCenter, otherBoxExtents, otherBoxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::containsOrientedBoxValues(
    const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation
)const noexcept{
    SIMDVectorDouble corners[BoundingDoubleOrientedBox::s_DoubleCornerCount];
    CollisionDoubleDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDoubleDetail::PointsInsideMinMax(corners, BoundingDoubleOrientedBox::s_DoubleCornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return intersectsOrientedBoxValues(boxCenter, boxExtents, otherBoxCenter, otherBoxExtents, otherBoxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingBox::containsFrustumValues(
    const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue,
    const f32 nearPlaneValue, const f32 farPlaneValue
)const noexcept{
    SIMDVectorSingle corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(corners, BoundingFrustum::s_CornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectAxisAlignedBox(planes, boxCenter, boxExtents) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::containsFrustumValues(
    const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue,
    const f64 nearPlaneValue, const f64 farPlaneValue
)const noexcept{
    SIMDVectorDouble corners[BoundingDoubleFrustum::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDoubleDetail::PointsInsideMinMax(corners, BoundingDoubleFrustum::s_DoubleCornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::FrustumPlanesIntersectAxisAlignedBox(planes, boxCenter, boxExtents) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingBox::intersectsBoxValues(const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents)const noexcept{
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    SIMDVectorSingle otherMin{};
    SIMDVectorSingle otherMax{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    CollisionDetail::MinMaxFromCenterExtents(otherBoxCenter, otherBoxExtents, otherMin, otherMax);
    return CollisionDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax);
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleBox::intersectsBoxValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents)const noexcept{
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    SIMDVectorDouble otherMin{};
    SIMDVectorDouble otherMax{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    CollisionDoubleDetail::MinMaxFromCenterExtents(otherBoxCenter, otherBoxExtents, otherMin, otherMax);
    return CollisionDoubleDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax);
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingBox::intersectsOrientedBoxValues(
    const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation
)const noexcept{
    return CollisionDetail::ObbIntersectsObb(boxCenter, boxExtents, s_SIMDIdentityR3, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleBox::intersectsOrientedBoxValues(
    const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation
)const noexcept{
    return CollisionDoubleDetail::ObbIntersectsObb(boxCenter, boxExtents, s_DoubleSIMDIdentityR3, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingBox::transform(BoundingBox& outBox, const SIMDMatrix& matrix)const noexcept{
    SIMDVectorSingle centerVector{};
    SIMDVectorSingle extentsVector{};
    TransformBoxValue(LoadFloat(center), LoadFloat(extents), matrix, centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}

inline void NWB_SIMD_CALL BoundingBox::transform(BoundingBox& outBox, const f32 scale, const SIMDVectorSingle rotation, const SIMDVectorSingle translation)const noexcept{
    SIMDVectorSingle centerVector{};
    SIMDVectorSingle extentsVector{};
    TransformBoxValue(LoadFloat(center), LoadFloat(extents), scale, rotation, translation, centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}

inline void NWB_SIMD_CALL BoundingDoubleBox::transform(BoundingDoubleBox& outBox, const SIMDMatrixDouble& matrix)const noexcept{
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    TransformBoxValue(LoadDouble(center), LoadDouble(extents), matrix, centerVector, extentsVector);
    StoreDouble(centerVector, outBox.center);
    StoreDouble(extentsVector, outBox.extents);
}

inline void NWB_SIMD_CALL BoundingDoubleBox::transform(BoundingDoubleBox& outBox, const f64 scale, const SIMDVectorDouble rotation, const SIMDVectorDouble translation)const noexcept{
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    TransformBoxValue(LoadDouble(center), LoadDouble(extents), scale, rotation, translation, centerVector, extentsVector);
    StoreDouble(centerVector, outBox.center);
    StoreDouble(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::getCorners(Float3U (&corners)[s_CornerCount])const noexcept{
    SIMDVectorSingle cornerVectors[s_CornerCount];
    CornersValue(LoadFloat(center), LoadFloat(extents), cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}

template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
inline void BoundingDoubleBox::getCorners(PointT (&corners)[s_DoubleCornerCount])const noexcept{
    SIMDVectorDouble cornerVectors[s_DoubleCornerCount];
    CornersValue(LoadDouble(center), LoadDouble(extents), cornerVectors);
    for(u64 i = 0u; i < s_DoubleCornerCount; ++i)
        StoreDouble(VectorDoubleSetW(cornerVectors[i], 0.0), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingBox::contains(const SIMDVectorSingle point)const noexcept{
    return ContainsPointValue(LoadFloat(center), LoadFloat(extents), point);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingBox::contains(const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2)const noexcept{
    return ContainsTriangleValue(LoadFloat(center), LoadFloat(extents), v0, v1, v2);
}

[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingSphere& sphere)const noexcept{
    return containsSphereValues(LoadFloat(center), LoadFloat(extents), LoadFloat(sphere.centerRadius));
}

[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingBox& box)const noexcept{
    return containsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(box.center), LoadFloat(box.extents));
}

[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}

[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingFrustum& frustum)const noexcept{
    return containsFrustumValues(LoadFloat(center), LoadFloat(extents), LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::contains(const SIMDVectorDouble point)const noexcept{
    return ContainsPointValue(LoadDouble(center), LoadDouble(extents), point);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::contains(const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2)const noexcept{
    return ContainsTriangleValue(LoadDouble(center), LoadDouble(extents), v0, v1, v2);
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleBox::contains(const BoundingDoubleSphere& sphere)const noexcept{
    return containsSphereValues(LoadDouble(center), LoadDouble(extents), LoadDouble(sphere.centerRadius));
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleBox::contains(const BoundingDoubleBox& box)const noexcept{
    return containsBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(box.center), LoadDouble(box.extents));
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleBox::contains(const BoundingDoubleOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleBox::contains(const BoundingDoubleFrustum& frustum)const noexcept{
    return containsFrustumValues(LoadDouble(center), LoadDouble(extents), LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingSphere& sphere)const noexcept{
    return sphere.intersectsBoxValues(LoadFloat(sphere.centerRadius), LoadFloat(center), LoadFloat(extents));
}

[[nodiscard]] inline bool BoundingBox::intersects(const BoundingBox& box)const noexcept{
    return intersectsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(box.center), LoadFloat(box.extents));
}

[[nodiscard]] inline bool BoundingBox::intersects(const BoundingOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}

[[nodiscard]] inline bool BoundingBox::intersects(const BoundingFrustum& frustum)const noexcept{
    return FrustumTests::IntersectsBox(LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadFloat(center), LoadFloat(extents));
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingBox::intersects(const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2)const noexcept{
    return IntersectsTriangleValue(LoadFloat(center), LoadFloat(extents), v0, v1, v2);
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingBox::intersects(const SIMDVectorSingle plane)const noexcept{
    return IntersectsPlaneValue(LoadFloat(center), LoadFloat(extents), plane);
}

[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingBox::intersects(SIMDVectorSingle origin, SIMDVectorSingle direction)const noexcept{
    return IntersectsRayValue(LoadFloat(center), LoadFloat(extents), origin, direction);
}

[[nodiscard]] inline bool BoundingDoubleBox::intersects(const BoundingDoubleSphere& sphere)const noexcept{
    return sphere.intersectsBoxValues(LoadDouble(sphere.centerRadius), LoadDouble(center), LoadDouble(extents));
}

[[nodiscard]] inline bool BoundingDoubleBox::intersects(const BoundingDoubleBox& box)const noexcept{
    return intersectsBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(box.center), LoadDouble(box.extents));
}

[[nodiscard]] inline bool BoundingDoubleBox::intersects(const BoundingDoubleOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}

[[nodiscard]] inline bool BoundingDoubleBox::intersects(const BoundingDoubleFrustum& frustum)const noexcept{
    return FrustumDoubleTests::IntersectsBox(LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadDouble(center), LoadDouble(extents));
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleBox::intersects(const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2)const noexcept{
    return IntersectsTriangleValue(LoadDouble(center), LoadDouble(extents), v0, v1, v2);
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleBox::intersects(const SIMDVectorDouble plane)const noexcept{
    return IntersectsPlaneValue(LoadDouble(center), LoadDouble(extents), plane);
}

[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleBox::intersects(SIMDVectorDouble origin, SIMDVectorDouble direction)const noexcept{
    return IntersectsRayValue(LoadDouble(center), LoadDouble(extents), origin, direction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingBox::containedBy(SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)const noexcept{
    return ContainedByValue(LoadFloat(center), LoadFloat(extents), plane0, plane1, plane2, plane3, plane4, plane5);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::containedBy(SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)const noexcept{
    return ContainedByValue(LoadDouble(center), LoadDouble(extents), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::CreateMerged(BoundingBox& outBox, const BoundingBox& box0, const BoundingBox& box1)noexcept{
    SIMDVectorSingle centerVector{};
    SIMDVectorSingle extentsVector{};
    MergeBoxValues(LoadFloat(box0.center), LoadFloat(box0.extents), LoadFloat(box1.center), LoadFloat(box1.extents), centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}

inline void BoundingDoubleBox::CreateMerged(BoundingDoubleBox& outBox, const BoundingDoubleBox& box0, const BoundingDoubleBox& box1)noexcept{
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    MergeBoxValues(LoadDouble(box0.center), LoadDouble(box0.extents), LoadDouble(box1.center), LoadDouble(box1.extents), centerVector, extentsVector);
    StoreDouble(centerVector, outBox.center);
    StoreDouble(extentsVector, outBox.extents);
}

inline void BoundingBox::CreateFromSphere(BoundingBox& outBox, const BoundingSphere& sphere)noexcept{
    const SIMDVectorSingle sphereValue = LoadFloat(sphere.centerRadius);
    StoreFloat(VectorSetW(CollisionDetail::SphereCenter(sphereValue), 0.0f), outBox.center);
    StoreFloat(VectorSetW(CollisionDetail::SphereRadius(sphereValue), 0.0f), outBox.extents);
}

inline void BoundingDoubleBox::CreateFromSphere(BoundingDoubleBox& outBox, const BoundingDoubleSphere& sphere)noexcept{
    const SIMDVectorDouble sphereValue = LoadDouble(sphere.centerRadius);
    StoreDouble(VectorDoubleSetW(CollisionDoubleDetail::SphereCenter(sphereValue), 0.0), outBox.center);
    StoreDouble(VectorDoubleSetW(CollisionDoubleDetail::SphereRadius(sphereValue), 0.0), outBox.extents);
}

inline void NWB_SIMD_CALL BoundingBox::CreateFromPoints(BoundingBox& outBox, const SIMDVectorSingle point0, const SIMDVectorSingle point1)noexcept{
    SIMDVectorSingle centerVector{};
    SIMDVectorSingle extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(VectorMin(point0, point1), VectorMax(point0, point1), centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}

inline void BoundingBox::CreateFromPoints(BoundingBox& outBox, const usize count, const Float3U& points, const usize stride)noexcept{
    NWB_ASSERT(count > 0u);
    SIMDVectorSingle minBounds = LoadFloat(*VectorScalarDetail::StridePointer(MakeNotNull(&points), stride, 0u));
    SIMDVectorSingle maxBounds = minBounds;
    for(usize i = 1u; i < count; ++i)
        CollisionDetail::ExpandMinMax(LoadFloat(*VectorScalarDetail::StridePointer(MakeNotNull(&points), stride, i)), minBounds, maxBounds);
    SIMDVectorSingle centerVector{};
    SIMDVectorSingle extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}

inline void NWB_SIMD_CALL BoundingDoubleBox::CreateFromPoints(BoundingDoubleBox& outBox, const SIMDVectorDouble point0, const SIMDVectorDouble point1)noexcept{
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    CollisionDoubleDetail::CenterExtentsFromMinMax(VectorDoubleMin(point0, point1), VectorDoubleMax(point0, point1), centerVector, extentsVector);
    StoreDouble(centerVector, outBox.center);
    StoreDouble(extentsVector, outBox.extents);
}

template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
inline void BoundingDoubleBox::CreateFromPoints(BoundingDoubleBox& outBox, const usize count, const PointT& points, const usize stride)noexcept{
    NWB_ASSERT(count > 0u);
    NWB_ASSERT(stride >= sizeof(PointT));
    NWB_ASSERT(stride % alignof(PointT) == 0u);
    SIMDVectorDouble minBounds = LoadDouble(*VectorScalarDetail::StridePointer(MakeNotNull(&points), stride, 0u));
    SIMDVectorDouble maxBounds = minBounds;
    for(usize i = 1u; i < count; ++i)
        CollisionDoubleDetail::ExpandMinMax(LoadDouble(*VectorScalarDetail::StridePointer(MakeNotNull(&points), stride, i)), minBounds, maxBounds);
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    CollisionDoubleDetail::CenterExtentsFromMinMax(minBounds, maxBounds, centerVector, extentsVector);
    StoreDouble(centerVector, outBox.center);
    StoreDouble(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

