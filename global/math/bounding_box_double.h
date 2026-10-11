// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "matrix_double.h"
#include "collision_detail_double.h"
#include "collision_plane_double.h"
#include "collision_sdf_double.h"
#include "collision_aabb_double.h"
#include "collision_triangle_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bounding_shape_decls_double.h"
#include "bounding_corners_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleBox::TransformBoxValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, const SIMDMatrixDouble& matrix, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents)noexcept{
    SIMDVectorDouble corners[s_DoubleCornerCount];
    CollisionDoubleDetail::AabbCorners(boxCenter, boxExtents, corners);
    SIMDVectorDouble minBounds = VectorDouble3Transform(corners[0], matrix);
    SIMDVectorDouble maxBounds = minBounds;
    for(u64 i = 1u; i < s_DoubleCornerCount; ++i)
        CollisionDoubleDetail::ExpandMinMax(VectorDouble3Transform(corners[i], matrix), minBounds, maxBounds);
    CollisionDoubleDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


inline void NWB_SIMD_CALL BoundingDoubleBox::transform(BoundingDoubleBox& outBox, const SIMDMatrixDouble& matrix)const noexcept{
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    TransformBoxValue(LoadDouble(center), LoadDouble(extents), matrix, centerVector, extentsVector);
    StoreDouble(centerVector, outBox.center);
    StoreDouble(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleBox::transform(
    BoundingDoubleBox& outBox,
    const f64 scale,
    const SIMDVectorDouble rotation,
    const SIMDVectorDouble translation
)const noexcept{
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    TransformBoxValue(LoadDouble(center), LoadDouble(extents), scale, rotation, translation, centerVector, extentsVector);
    StoreDouble(centerVector, outBox.center);
    StoreDouble(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleBox::CornersValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble* outCorners)noexcept{
    CollisionDoubleDetail::AabbCorners(boxCenter, boxExtents, outCorners);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingDoubleBox::getCorners(Double3U* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVectorDouble cornerVectors[s_DoubleCornerCount];
    CornersValue(LoadDouble(center), LoadDouble(extents), cornerVectors);
    for(u64 i = 0u; i < s_DoubleCornerCount; ++i)
        StoreDouble(VectorDoubleSetW(cornerVectors[i], 0.0), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::ContainsPointValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble point)noexcept{
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return VectorDouble3GreaterOrEqual(point, minBounds) && VectorDouble3LessOrEqual(point, maxBounds) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::contains(const SIMDVectorDouble point)const noexcept{
    return ContainsPointValue(LoadDouble(center), LoadDouble(extents), point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::contains(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)const noexcept{
    return ContainsTriangleValue(LoadDouble(center), LoadDouble(extents), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleBox::contains(const BoundingDoubleSphere& sphere)const noexcept{
    return containsSphereValues(LoadDouble(center), LoadDouble(extents), LoadDouble(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleBox::contains(const BoundingDoubleBox& box)const noexcept{
    return containsBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(box.center), LoadDouble(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::containsOrientedBoxValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation)const noexcept{
    SIMDVectorDouble corners[BoundingDoubleOrientedBox::s_DoubleCornerCount];
    CollisionDoubleDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDoubleDetail::PointsInsideMinMax(corners, BoundingDoubleOrientedBox::s_DoubleCornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return intersectsOrientedBoxValues(boxCenter, boxExtents, otherBoxCenter, otherBoxExtents, otherBoxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleBox::contains(const BoundingDoubleOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::containsFrustumValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue)const noexcept{
    SIMDVectorDouble corners[BoundingDoubleFrustum::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(
        frustumOrigin,
        frustumOrientation,
        rightSlopeValue,
        leftSlopeValue,
        topSlopeValue,
        bottomSlopeValue,
        nearPlaneValue,
        farPlaneValue,
        corners
    );
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


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleBox::contains(const BoundingDoubleFrustum& frustum)const noexcept{
    return containsFrustumValues(LoadDouble(center), LoadDouble(extents), LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleBox::intersects(const BoundingDoubleSphere& sphere)const noexcept{
    return sphere.intersectsBoxValues(LoadDouble(sphere.centerRadius), LoadDouble(center), LoadDouble(extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleBox::intersectsBoxValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents)const noexcept{
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    SIMDVectorDouble otherMin{};
    SIMDVectorDouble otherMax{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    CollisionDoubleDetail::MinMaxFromCenterExtents(otherBoxCenter, otherBoxExtents, otherMin, otherMax);
    return CollisionDoubleDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleBox::intersects(const BoundingDoubleBox& box)const noexcept{
    return intersectsBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(box.center), LoadDouble(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleBox::intersectsOrientedBoxValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation)const noexcept{
    return CollisionDoubleDetail::ObbIntersectsObb(
        boxCenter,
        boxExtents,
        s_DoubleSIMDIdentityR3,
        otherBoxCenter,
        otherBoxExtents,
        otherBoxOrientation
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleBox::intersects(const BoundingDoubleOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleBox::intersects(const BoundingDoubleFrustum& frustum)const noexcept{
    return frustum.intersectsBoxValues(LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadDouble(center), LoadDouble(extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleBox::IntersectsTriangleValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return CollisionDoubleDetail::TriangleAabbOverlap(v0, v1, v2, minBounds, maxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleBox::intersects(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)const noexcept{
    return IntersectsTriangleValue(LoadDouble(center), LoadDouble(extents), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleBox::intersects(const SIMDVectorDouble plane)const noexcept{
    return IntersectsPlaneValue(LoadDouble(center), LoadDouble(extents), plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleBox::IntersectsRayValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble origin, SIMDVectorDouble direction)noexcept{
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return CollisionDoubleDetail::RayIntersectsMinMax(origin, direction, minBounds, maxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleBox::intersects(
    SIMDVectorDouble origin,
    SIMDVectorDouble direction
)const noexcept{
    return IntersectsRayValue(LoadDouble(center), LoadDouble(extents), origin, direction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::ContainedByValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)noexcept{
    SIMDVectorDouble points[s_DoubleCornerCount];
    CollisionDoubleDetail::AabbCorners(boxCenter, boxExtents, points);
    const SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDoubleDetail::ContainmentFromPlaneTests(points, s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleBox::containedBy(
    SIMDVectorDouble plane0,
    SIMDVectorDouble plane1,
    SIMDVectorDouble plane2,
    SIMDVectorDouble plane3,
    SIMDVectorDouble plane4,
    SIMDVectorDouble plane5
)const noexcept{
    return ContainedByValue(LoadDouble(center), LoadDouble(extents), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


inline void BoundingDoubleBox::CreateMerged(BoundingDoubleBox& outBox, const BoundingDoubleBox& box0, const BoundingDoubleBox& box1)noexcept{
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    MergeBoxValues(LoadDouble(box0.center), LoadDouble(box0.extents), LoadDouble(box1.center), LoadDouble(box1.extents), centerVector, extentsVector);
    StoreDouble(centerVector, outBox.center);
    StoreDouble(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingDoubleBox::CreateFromSphere(BoundingDoubleBox& outBox, const BoundingDoubleSphere& sphere)noexcept{
    const SIMDVectorDouble sphereValue = LoadDouble(sphere.centerRadius);
    StoreDouble(VectorDoubleSetW(CollisionDoubleDetail::SphereCenter(sphereValue), 0.0), outBox.center);
    StoreDouble(VectorDoubleSetW(CollisionDoubleDetail::SphereRadius(sphereValue), 0.0), outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleBox::CreateFromPoints(BoundingDoubleBox& outBox, const SIMDVectorDouble point0, const SIMDVectorDouble point1)noexcept{
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    CollisionDoubleDetail::CenterExtentsFromMinMax(VectorDoubleMin(point0, point1), VectorDoubleMax(point0, point1), centerVector, extentsVector);
    StoreDouble(centerVector, outBox.center);
    StoreDouble(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingDoubleBox::CreateFromPoints(
    BoundingDoubleBox& outBox,
    const usize count,
    const Double3U* points,
    const usize stride
)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(count > 0u);
    SIMDVectorDouble minBounds = LoadDouble(*CollisionDoubleDetail::StrideDouble3Pointer(points, stride, 0u));
    SIMDVectorDouble maxBounds = minBounds;
    for(usize i = 1u; i < count; ++i)
        CollisionDoubleDetail::ExpandMinMax(LoadDouble(*CollisionDoubleDetail::StrideDouble3Pointer(points, stride, i)), minBounds, maxBounds);
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    CollisionDoubleDetail::CenterExtentsFromMinMax(minBounds, maxBounds, centerVector, extentsVector);
    StoreDouble(centerVector, outBox.center);
    StoreDouble(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

