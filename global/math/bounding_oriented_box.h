// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
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


inline void NWB_SIMD_CALL BoundingOrientedBox::TransformOrientedBoxValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, const SIMDMatrix& matrix, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents, SIMDVectorSingle& outOrientation, bool& outCollapsedToAxisAligned)noexcept{
    outCollapsedToAxisAligned = false;
    const auto decomposition = MatrixDecompose(matrix);
    if(!decomposition){
        SIMDVectorSingle corners[s_CornerCount];
        CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
        SIMDVectorSingle minBounds = Vector3Transform(corners[0], matrix);
        SIMDVectorSingle maxBounds = minBounds;
        for(u32 i = 1u; i < s_CornerCount; ++i)
            CollisionDetail::ExpandMinMax(Vector3Transform(corners[i], matrix), minBounds, maxBounds);

        CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
        outOrientation = s_SIMDIdentityR3;
        outCollapsedToAxisAligned = true;
        return;
    }

    outCenter = Vector3Transform(boxCenter, matrix);
    outExtents = VectorMultiply(boxExtents, VectorAbs(decomposition->scale));
    outOrientation = QuaternionNormalize(QuaternionMultiply(boxOrientation, decomposition->rotation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingOrientedBox::transform(BoundingOrientedBox& outBox, const SIMDMatrix& matrix)const noexcept{
    SIMDVectorSingle centerVector{};
    SIMDVectorSingle extentsVector{};
    SIMDVectorSingle orientationVector{};
    bool collapsedToAxisAligned = false;
    TransformOrientedBoxValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), matrix, centerVector, extentsVector, orientationVector, collapsedToAxisAligned);
    if(collapsedToAxisAligned){
        StoreFloat(centerVector, outBox.center);
        StoreFloat(extentsVector, outBox.extents);
        outBox.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    StoreFloat(VectorSetW(centerVector, 0.0f), outBox.center);
    StoreFloat(VectorSetW(extentsVector, 0.0f), outBox.extents);
    StoreFloat(orientationVector, outBox.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingOrientedBox::TransformOrientedBoxValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents, SIMDVectorSingle& outOrientation)noexcept{
    outCenter = VectorAdd(Vector3Rotate(VectorScale(boxCenter, scale), rotation), translation);
    outExtents = VectorScale(boxExtents, Abs(scale));
    outOrientation = QuaternionNormalize(QuaternionMultiply(boxOrientation, rotation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingOrientedBox::transform(
    BoundingOrientedBox& outBox,
    const f32 scale,
    const SIMDVectorSingle rotation,
    const SIMDVectorSingle translation
)const noexcept{
    SIMDVectorSingle centerVector{};
    SIMDVectorSingle extentsVector{};
    SIMDVectorSingle orientationVector{};
    TransformOrientedBoxValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), scale, rotation, translation, centerVector, extentsVector, orientationVector);
    StoreFloat(VectorSetW(centerVector, 0.0f), outBox.center);
    StoreFloat(VectorSetW(extentsVector, 0.0f), outBox.extents);
    StoreFloat(orientationVector, outBox.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingOrientedBox::CornersValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle* outCorners)noexcept{
    CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, outCorners);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::getCorners(Float3U* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVectorSingle cornerVectors[s_CornerCount];
    CornersValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::ContainsPointValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle point)noexcept{
    return CollisionDetail::PointInsideObb(point, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::contains(const SIMDVectorSingle point)const noexcept{
    return ContainsPointValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::ContainsTriangleValue(
    SIMDVectorSingle boxCenter,
    SIMDVectorSingle boxExtents,
    SIMDVectorSingle boxOrientation,
    SIMDVectorSingle v0,
    SIMDVectorSingle v1,
    SIMDVectorSingle v2
)noexcept{
    const SIMDVectorSingle points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    if(CollisionDetail::PointsInsideObb(points, CollisionDetail::s_TriangleVertexCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return IntersectsTriangleValue(boxCenter, boxExtents, boxOrientation, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::contains(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)const noexcept{
    return ContainsTriangleValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::containsSphereValues(const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle sphereValue)const noexcept{
    const SIMDVectorSingle sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVectorSingle localCenter = CollisionDetail::PointToObbLocal(CollisionDetail::SphereCenter(sphereValue), boxCenter, boxOrientation);
    const SIMDVectorSingle closestPoint = CollisionDetail::ClosestPointOnMinMax(localCenter, VectorNegate(boxExtents), boxExtents);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(Vector3LengthSq(VectorSubtract(closestPoint, localCenter)), VectorMultiply(sphereRadius, sphereRadius))))
        return ContainmentType::Disjoint;

    if(Vector3LessOrEqual(VectorAbs(localCenter), VectorSubtract(boxExtents, sphereRadius)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingSphere& sphere)const noexcept{
    return containsSphereValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::containsBoxValues(const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents)const noexcept{
    SIMDVectorSingle corners[BoundingBox::s_CornerCount];
    CollisionDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    if(CollisionDetail::PointsInsideObb(corners, BoundingBox::s_CornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingBox& box)const noexcept{
    return containsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::containsOrientedBoxValues(const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation)const noexcept{
    SIMDVectorSingle corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    if(CollisionDetail::PointsInsideObb(corners, BoundingOrientedBox::s_CornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsOrientedBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents, otherBoxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::containsFrustumValues(const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue)const noexcept{
    SIMDVectorSingle corners[BoundingFrustum::s_CornerCount];
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
    if(CollisionDetail::PointsInsideObb(corners, BoundingFrustum::s_CornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectOrientedBox(planes, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingFrustum& frustum)const noexcept{
    return containsFrustumValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingSphere& sphere)const noexcept{
    return sphere.intersectsOrientedBoxValues(LoadFloat(sphere.centerRadius), LoadFloat(center), LoadFloat(extents), LoadFloat(orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingOrientedBox::intersectsBoxValues(const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents)const noexcept{
    return CollisionDetail::ObbIntersectsObb(
        boxCenter,
        boxExtents,
        boxOrientation,
        otherBoxCenter,
        otherBoxExtents,
        s_SIMDIdentityR3
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingBox& box)const noexcept{
    return intersectsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingOrientedBox::intersectsOrientedBoxValues(const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation)const noexcept{
    return CollisionDetail::ObbIntersectsObb(
        boxCenter,
        boxExtents,
        boxOrientation,
        otherBoxCenter,
        otherBoxExtents,
        otherBoxOrientation
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingFrustum& frustum)const noexcept{
    return frustum.intersectsOrientedBoxValues(LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingOrientedBox::IntersectsTriangleValue(
    SIMDVectorSingle boxCenter,
    SIMDVectorSingle boxExtents,
    SIMDVectorSingle boxOrientation,
    SIMDVectorSingle v0,
    SIMDVectorSingle v1,
    SIMDVectorSingle v2
)noexcept{
    const SIMDVectorSingle localV0 = CollisionDetail::PointToObbLocal(v0, boxCenter, boxOrientation);
    const SIMDVectorSingle localV1 = CollisionDetail::PointToObbLocal(v1, boxCenter, boxOrientation);
    const SIMDVectorSingle localV2 = CollisionDetail::PointToObbLocal(v2, boxCenter, boxOrientation);
    return CollisionDetail::TriangleAabbOverlap(localV0, localV1, localV2, VectorNegate(boxExtents), boxExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingOrientedBox::intersects(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)const noexcept{
    return IntersectsTriangleValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingOrientedBox::IntersectsPlaneValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle plane)noexcept{
    SIMDVectorSingle outside{};
    SIMDVectorSingle inside{};
    SIMDVectorSingle axis0{};
    SIMDVectorSingle axis1{};
    SIMDVectorSingle axis2{};
    CollisionDetail::ObbAxes(boxOrientation, axis0, axis1, axis2);
    CollisionDetail::FastIntersectOrientedBoxPlane(
        boxCenter,
        boxExtents,
        axis0,
        axis1,
        axis2,
        plane,
        outside,
        inside
    );
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingOrientedBox::intersects(const SIMDVectorSingle plane)const noexcept{
    return IntersectsPlaneValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingOrientedBox::IntersectsRayValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle origin, SIMDVectorSingle direction)noexcept{
    const SIMDVectorSingle localOrigin = CollisionDetail::PointToObbLocal(origin, boxCenter, boxOrientation);
    const SIMDVectorSingle localDirection = Vector3InverseRotate(direction, boxOrientation);
    return CollisionDetail::RayIntersectsMinMax(localOrigin, localDirection, VectorNegate(boxExtents), boxExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingOrientedBox::intersects(
    SIMDVectorSingle origin,
    SIMDVectorSingle direction
)const noexcept{
    return IntersectsRayValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), origin, direction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::ContainedByValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)noexcept{
    SIMDVectorSingle corners[s_CornerCount];
    CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
    const SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::containedBy(
    SIMDVectorSingle plane0,
    SIMDVectorSingle plane1,
    SIMDVectorSingle plane2,
    SIMDVectorSingle plane3,
    SIMDVectorSingle plane4,
    SIMDVectorSingle plane5
)const noexcept{
    return ContainedByValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::CreateFromBoundingBox(BoundingOrientedBox& outBox, const BoundingBox& box)noexcept{
    outBox.center = box.center;
    outBox.extents = box.extents;
    outBox.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::CreateFromPoints(
    BoundingOrientedBox& outBox,
    const usize count,
    const Float3U* points,
    const usize stride
)noexcept{
    BoundingBox box;
    BoundingBox::CreateFromPoints(box, count, points, stride);
    CreateFromBoundingBox(outBox, box);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

