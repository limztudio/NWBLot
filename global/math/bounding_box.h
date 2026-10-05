// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix.h"
#include "collision_detail.h"
#include "collision_plane.h"
#include "collision_sdf.h"
#include "collision_aabb.h"
#include "collision_triangle.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bounding_shape_decls.h"
#include "bounding_corners.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void GLB_SIMD_CALL BoundingBox::TransformBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, const SIMDMatrix& matrix, SIMDVector& outCenter, SIMDVector& outExtents)noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::AabbCorners(boxCenter, boxExtents, corners);
    SIMDVector minBounds = Vector3Transform(corners[0], matrix);
    SIMDVector maxBounds = minBounds;
    for(u32 i = 1u; i < s_CornerCount; ++i)
        CollisionDetail::ExpandMinMax(Vector3Transform(corners[i], matrix), minBounds, maxBounds);
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void GLB_SIMD_CALL BoundingBox::TransformBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, f32 scale, SIMDVector rotation, SIMDVector translation, SIMDVector& outCenter, SIMDVector& outExtents)noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::AabbCorners(boxCenter, boxExtents, corners);
    const SIMDVector scaleVector = VectorReplicate(scale);
    SIMDVector minBounds = VectorAdd(Vector3Rotate(VectorMultiply(corners[0], scaleVector), rotation), translation);
    SIMDVector maxBounds = minBounds;
    for(u32 i = 1u; i < s_CornerCount; ++i){
        const SIMDVector transformed = VectorAdd(Vector3Rotate(VectorMultiply(corners[i], scaleVector), rotation), translation);
        CollisionDetail::ExpandMinMax(transformed, minBounds, maxBounds);
    }
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void GLB_SIMD_CALL BoundingBox::transform(BoundingBox& outBox, const SIMDMatrix& matrix)const noexcept{
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    TransformBoxValue(LoadFloat(center), LoadFloat(extents), matrix, centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void GLB_SIMD_CALL BoundingBox::transform(
    BoundingBox& outBox,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    TransformBoxValue(LoadFloat(center), LoadFloat(extents), scale, rotation, translation, centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void GLB_SIMD_CALL BoundingBox::CornersValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector* outCorners)noexcept{
    CollisionDetail::AabbCorners(boxCenter, boxExtents, outCorners);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::getCorners(Float3U* corners)const noexcept{
    GLB_ASSERT(corners != nullptr);
    SIMDVector cornerVectors[s_CornerCount];
    CornersValue(LoadFloat(center), LoadFloat(extents), cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingBox::ContainsPointValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector point)noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return Vector3GreaterOrEqual(point, minBounds) && Vector3LessOrEqual(point, maxBounds) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingBox::contains(const SIMDVector point)const noexcept{
    return ContainsPointValue(LoadFloat(center), LoadFloat(extents), point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingBox::ContainsTriangleValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(points, CollisionDetail::s_TriangleVertexCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return IntersectsTriangleValue(boxCenter, boxExtents, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingBox::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    return ContainsTriangleValue(LoadFloat(center), LoadFloat(extents), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingBox::containsSphereValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector sphereValue)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    const SIMDVector sphereCenter = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(sphereCenter, minBounds, maxBounds);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(Vector3LengthSq(VectorSubtract(closestPoint, sphereCenter)), VectorMultiply(sphereRadius, sphereRadius))))
        return ContainmentType::Disjoint;

    if(Vector3GreaterOrEqual(VectorSubtract(sphereCenter, sphereRadius), minBounds) && Vector3LessOrEqual(VectorAdd(sphereCenter, sphereRadius), maxBounds))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingSphere& sphere)const noexcept{
    return containsSphereValues(LoadFloat(center), LoadFloat(extents), LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingBox::containsBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    SIMDVector otherMin{};
    SIMDVector otherMax{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    CollisionDetail::MinMaxFromCenterExtents(otherBoxCenter, otherBoxExtents, otherMin, otherMax);
    if(!CollisionDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax))
        return ContainmentType::Disjoint;
    if(Vector3GreaterOrEqual(otherMin, minBounds) && Vector3LessOrEqual(otherMax, maxBounds))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingBox& box)const noexcept{
    return containsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingBox::containsOrientedBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents, const SIMDVector otherBoxOrientation)const noexcept{
    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(corners, BoundingOrientedBox::s_CornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return intersectsOrientedBoxValues(boxCenter, boxExtents, otherBoxCenter, otherBoxExtents, otherBoxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingBox::containsFrustumValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue)const noexcept{
    SIMDVector corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(
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
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(corners, BoundingFrustum::s_CornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectAxisAlignedBox(planes, boxCenter, boxExtents) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingFrustum& frustum)const noexcept{
    return containsFrustumValues(LoadFloat(center), LoadFloat(extents), LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingSphere& sphere)const noexcept{
    return sphere.intersectsBoxValues(LoadFloat(sphere.centerRadius), LoadFloat(center), LoadFloat(extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingBox::intersectsBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    SIMDVector otherMin{};
    SIMDVector otherMax{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    CollisionDetail::MinMaxFromCenterExtents(otherBoxCenter, otherBoxExtents, otherMin, otherMax);
    return CollisionDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingBox& box)const noexcept{
    return intersectsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingBox::intersectsOrientedBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents, const SIMDVector otherBoxOrientation)const noexcept{
    return CollisionDetail::ObbIntersectsObb(
        boxCenter,
        boxExtents,
        s_SIMDIdentityR3,
        otherBoxCenter,
        otherBoxExtents,
        otherBoxOrientation
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingFrustum& frustum)const noexcept{
    return frustum.intersectsBoxValues(LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadFloat(center), LoadFloat(extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingBox::IntersectsTriangleValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return CollisionDetail::TriangleAabbOverlap(v0, v1, v2, minBounds, maxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingBox::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    return IntersectsTriangleValue(LoadFloat(center), LoadFloat(extents), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum GLB_SIMD_CALL BoundingBox::IntersectsPlaneValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector plane)noexcept{
    SIMDVector outside{};
    SIMDVector inside{};
    CollisionDetail::FastIntersectAxisAlignedBoxPlane(boxCenter, boxExtents, plane, outside, inside);
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum GLB_SIMD_CALL BoundingBox::intersects(const SIMDVector plane)const noexcept{
    return IntersectsPlaneValue(LoadFloat(center), LoadFloat(extents), plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingBox::IntersectsRayValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector origin, SIMDVector direction, f32& outDistance)noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return CollisionDetail::RayIntersectsMinMax(origin, direction, minBounds, maxBounds, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingBox::intersects(
    SIMDVector origin,
    SIMDVector direction,
    f32& outDistance
)const noexcept{
    return IntersectsRayValue(LoadFloat(center), LoadFloat(extents), origin, direction, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingBox::ContainedByValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept{
    SIMDVector points[s_CornerCount];
    CollisionDetail::AabbCorners(boxCenter, boxExtents, points);
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(points, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingBox::containedBy(
    SIMDVector plane0,
    SIMDVector plane1,
    SIMDVector plane2,
    SIMDVector plane3,
    SIMDVector plane4,
    SIMDVector plane5
)const noexcept{
    return ContainedByValue(LoadFloat(center), LoadFloat(extents), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void GLB_SIMD_CALL BoundingBox::MergeBoxValues(SIMDVector boxCenter0, SIMDVector boxExtents0, SIMDVector boxCenter1, SIMDVector boxExtents1, SIMDVector& outCenter, SIMDVector& outExtents)noexcept{
    SIMDVector min0{};
    SIMDVector max0{};
    SIMDVector min1{};
    SIMDVector max1{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter0, boxExtents0, min0, max0);
    CollisionDetail::MinMaxFromCenterExtents(boxCenter1, boxExtents1, min1, max1);
    CollisionDetail::CenterExtentsFromMinMax(VectorMin(min0, min1), VectorMax(max0, max1), outCenter, outExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::CreateMerged(BoundingBox& outBox, const BoundingBox& box0, const BoundingBox& box1)noexcept{
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    MergeBoxValues(LoadFloat(box0.center), LoadFloat(box0.extents), LoadFloat(box1.center), LoadFloat(box1.extents), centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::CreateFromSphere(BoundingBox& outBox, const BoundingSphere& sphere)noexcept{
    const SIMDVector sphereValue = LoadFloat(sphere.centerRadius);
    StoreFloat(VectorSetW(CollisionDetail::SphereCenter(sphereValue), 0.0f), outBox.center);
    StoreFloat(VectorSetW(CollisionDetail::SphereRadius(sphereValue), 0.0f), outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void GLB_SIMD_CALL BoundingBox::CreateFromPoints(BoundingBox& outBox, const SIMDVector point0, const SIMDVector point1)noexcept{
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(VectorMin(point0, point1), VectorMax(point0, point1), centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::CreateFromPoints(
    BoundingBox& outBox,
    const usize count,
    const Float3U* points,
    const usize stride
)noexcept{
    GLB_ASSERT(points != nullptr);
    GLB_ASSERT(count > 0u);
    SIMDVector minBounds = LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, 0u));
    SIMDVector maxBounds = minBounds;
    for(usize i = 1u; i < count; ++i)
        CollisionDetail::ExpandMinMax(LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, i)), minBounds, maxBounds);
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

