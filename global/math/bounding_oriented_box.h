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


inline void SIMDCALL BoundingOrientedBox::transformOrientedBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, const SIMDMatrix& matrix, SIMDVector& outCenter, SIMDVector& outExtents, SIMDVector& outOrientation, bool& outCollapsedToAxisAligned)noexcept{
    outCollapsedToAxisAligned = false;
    SIMDVector scale{};
    SIMDVector rotation{};
    SIMDVector translation{};
    if(!MatrixDecompose(scale, rotation, translation, matrix)){
        SIMDVector corners[s_CornerCount];
        CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
        SIMDVector minBounds = Vector3Transform(corners[0], matrix);
        SIMDVector maxBounds = minBounds;
        for(u32 i = 1u; i < s_CornerCount; ++i)
            CollisionDetail::ExpandMinMax(Vector3Transform(corners[i], matrix), minBounds, maxBounds);

        CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
        outOrientation = s_SIMDIdentityR3;
        outCollapsedToAxisAligned = true;
        return;
    }

    outCenter = Vector3Transform(boxCenter, matrix);
    outExtents = VectorMultiply(boxExtents, VectorAbs(scale));
    outOrientation = QuaternionNormalize(QuaternionMultiply(boxOrientation, rotation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingOrientedBox::transform(BoundingOrientedBox& outBox, const SIMDMatrix& matrix)const noexcept{
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    SIMDVector orientationVector{};
    bool collapsedToAxisAligned = false;
    transformOrientedBoxValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), matrix, centerVector, extentsVector, orientationVector, collapsedToAxisAligned);
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


inline void SIMDCALL BoundingOrientedBox::transformOrientedBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, f32 scale, SIMDVector rotation, SIMDVector translation, SIMDVector& outCenter, SIMDVector& outExtents, SIMDVector& outOrientation)noexcept{
    outCenter = VectorAdd(Vector3Rotate(VectorScale(boxCenter, scale), rotation), translation);
    outExtents = VectorScale(boxExtents, Abs(scale));
    outOrientation = QuaternionNormalize(QuaternionMultiply(boxOrientation, rotation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingOrientedBox::transform(
    BoundingOrientedBox& outBox,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    SIMDVector orientationVector{};
    transformOrientedBoxValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), scale, rotation, translation, centerVector, extentsVector, orientationVector);
    StoreFloat(VectorSetW(centerVector, 0.0f), outBox.center);
    StoreFloat(VectorSetW(extentsVector, 0.0f), outBox.extents);
    StoreFloat(orientationVector, outBox.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingOrientedBox::cornersValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector* outCorners)noexcept{
    CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, outCorners);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::getCorners(Float3U* corners)const noexcept{
    GLB_ASSERT(corners != nullptr);
    SIMDVector cornerVectors[s_CornerCount];
    cornersValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containsPointValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector point)noexcept{
    return CollisionDetail::PointInsideObb(point, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::contains(const SIMDVector point)const noexcept{
    return containsPointValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containsTriangleValue(
    SIMDVector boxCenter,
    SIMDVector boxExtents,
    SIMDVector boxOrientation,
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2
)noexcept{
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    if(CollisionDetail::PointsInsideObb(points, CollisionDetail::s_TriangleVertexCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsTriangleValue(boxCenter, boxExtents, boxOrientation, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    return containsTriangleValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containsSphereValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation, const SIMDVector sphereValue)const noexcept{
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector localCenter = CollisionDetail::PointToObbLocal(CollisionDetail::SphereCenter(sphereValue), boxCenter, boxOrientation);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(localCenter, VectorNegate(boxExtents), boxExtents);
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


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containsBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents)const noexcept{
    SIMDVector corners[BoundingBox::s_CornerCount];
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


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containsOrientedBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents, const SIMDVector otherBoxOrientation)const noexcept{
    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
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


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containsFrustumValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation, const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue)const noexcept{
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
    if(CollisionDetail::PointsInsideObb(corners, BoundingFrustum::s_CornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
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


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersectsBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents)const noexcept{
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


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersectsOrientedBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents, const SIMDVector otherBoxOrientation)const noexcept{
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


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersectsTriangleValue(
    SIMDVector boxCenter,
    SIMDVector boxExtents,
    SIMDVector boxOrientation,
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2
)noexcept{
    const SIMDVector localV0 = CollisionDetail::PointToObbLocal(v0, boxCenter, boxOrientation);
    const SIMDVector localV1 = CollisionDetail::PointToObbLocal(v1, boxCenter, boxOrientation);
    const SIMDVector localV2 = CollisionDetail::PointToObbLocal(v2, boxCenter, boxOrientation);
    return CollisionDetail::TriangleAabbOverlap(localV0, localV1, localV2, VectorNegate(boxExtents), boxExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    return intersectsTriangleValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingOrientedBox::intersectsPlaneValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector plane)noexcept{
    SIMDVector outside{};
    SIMDVector inside{};
    SIMDVector axis0{};
    SIMDVector axis1{};
    SIMDVector axis2{};
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


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingOrientedBox::intersects(const SIMDVector plane)const noexcept{
    return intersectsPlaneValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersectsRayValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector origin, SIMDVector direction, f32& outDistance)noexcept{
    const SIMDVector localOrigin = CollisionDetail::PointToObbLocal(origin, boxCenter, boxOrientation);
    const SIMDVector localDirection = Vector3InverseRotate(direction, boxOrientation);
    return CollisionDetail::RayIntersectsMinMax(localOrigin, localDirection, VectorNegate(boxExtents), boxExtents, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersects(
    SIMDVector origin,
    SIMDVector direction,
    f32& outDistance
)const noexcept{
    return intersectsRayValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), origin, direction, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containedByValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containedBy(
    SIMDVector plane0,
    SIMDVector plane1,
    SIMDVector plane2,
    SIMDVector plane3,
    SIMDVector plane4,
    SIMDVector plane5
)const noexcept{
    return containedByValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::createFromBoundingBox(BoundingOrientedBox& outBox, const BoundingBox& box)noexcept{
    outBox.center = box.center;
    outBox.extents = box.extents;
    outBox.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::createFromPoints(
    BoundingOrientedBox& outBox,
    const usize count,
    const Float3U* points,
    const usize stride
)noexcept{
    BoundingBox box;
    BoundingBox::createFromPoints(box, count, points, stride);
    createFromBoundingBox(outBox, box);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

