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


inline void NWB_SIMD_CALL BoundingDoubleOrientedBox::TransformOrientedBoxValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, const SIMDMatrixDouble& matrix, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents, SIMDVectorDouble& outOrientation, bool& outCollapsedToAxisAligned)noexcept{
    outCollapsedToAxisAligned = false;
    const auto decomposition = MatrixDoubleDecompose(matrix);
    if(!decomposition){
        SIMDVectorDouble corners[s_DoubleCornerCount];
        CollisionDoubleDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
        SIMDVectorDouble minBounds = VectorDouble3Transform(corners[0], matrix);
        SIMDVectorDouble maxBounds = minBounds;
        for(u64 i = 1u; i < s_DoubleCornerCount; ++i)
            CollisionDoubleDetail::ExpandMinMax(VectorDouble3Transform(corners[i], matrix), minBounds, maxBounds);

        CollisionDoubleDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
        outOrientation = s_DoubleSIMDIdentityR3;
        outCollapsedToAxisAligned = true;
        return;
    }

    outCenter = VectorDouble3Transform(boxCenter, matrix);
    outExtents = VectorDoubleMultiply(boxExtents, VectorDoubleAbs(decomposition->scale));
    outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(boxOrientation, decomposition->rotation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleOrientedBox::transform(BoundingDoubleOrientedBox& outBox, const SIMDMatrixDouble& matrix)const noexcept{
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    SIMDVectorDouble orientationVector{};
    bool collapsedToAxisAligned = false;
    TransformOrientedBoxValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), matrix, centerVector, extentsVector, orientationVector, collapsedToAxisAligned);
    if(collapsedToAxisAligned){
        StoreDouble(centerVector, outBox.center);
        StoreDouble(extentsVector, outBox.extents);
        outBox.orientation = Double4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    StoreDouble(VectorDoubleSetW(centerVector, 0.0), outBox.center);
    StoreDouble(VectorDoubleSetW(extentsVector, 0.0), outBox.extents);
    StoreDouble(orientationVector, outBox.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleOrientedBox::TransformOrientedBoxValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents, SIMDVectorDouble& outOrientation)noexcept{
    outCenter = VectorDoubleAdd(VectorDouble3Rotate(VectorDoubleScale(boxCenter, scale), rotation), translation);
    outExtents = VectorDoubleScale(boxExtents, Abs(scale));
    outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(boxOrientation, rotation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleOrientedBox::transform(
    BoundingDoubleOrientedBox& outBox,
    const f64 scale,
    const SIMDVectorDouble rotation,
    const SIMDVectorDouble translation
)const noexcept{
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    SIMDVectorDouble orientationVector{};
    TransformOrientedBoxValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), scale, rotation, translation, centerVector, extentsVector, orientationVector);
    StoreDouble(VectorDoubleSetW(centerVector, 0.0), outBox.center);
    StoreDouble(VectorDoubleSetW(extentsVector, 0.0), outBox.extents);
    StoreDouble(orientationVector, outBox.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleOrientedBox::CornersValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble* outCorners)noexcept{
    CollisionDoubleDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, outCorners);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingDoubleOrientedBox::getCorners(Double3U* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVectorDouble cornerVectors[s_DoubleCornerCount];
    CornersValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), cornerVectors);
    for(u64 i = 0u; i < s_DoubleCornerCount; ++i)
        StoreDouble(VectorDoubleSetW(cornerVectors[i], 0.0), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::ContainsPointValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble point)noexcept{
    return CollisionDoubleDetail::PointInsideObb(point, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::contains(const SIMDVectorDouble point)const noexcept{
    return ContainsPointValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::ContainsTriangleValue(
    SIMDVectorDouble boxCenter,
    SIMDVectorDouble boxExtents,
    SIMDVectorDouble boxOrientation,
    SIMDVectorDouble v0,
    SIMDVectorDouble v1,
    SIMDVectorDouble v2
)noexcept{
    const SIMDVectorDouble points[CollisionDoubleDetail::s_DoubleTriangleVertexCount] = { v0, v1, v2 };
    if(CollisionDoubleDetail::PointsInsideObb(points, CollisionDoubleDetail::s_DoubleTriangleVertexCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return IntersectsTriangleValue(boxCenter, boxExtents, boxOrientation, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::contains(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)const noexcept{
    return ContainsTriangleValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::containsSphereValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble sphereValue)const noexcept{
    const SIMDVectorDouble sphereRadius = CollisionDoubleDetail::SphereRadius(sphereValue);
    const SIMDVectorDouble localCenter = CollisionDoubleDetail::PointToObbLocal(CollisionDoubleDetail::SphereCenter(sphereValue), boxCenter, boxOrientation);
    const SIMDVectorDouble closestPoint = CollisionDoubleDetail::ClosestPointOnMinMax(localCenter, VectorDoubleNegate(boxExtents), boxExtents);
    if(CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleGreater(VectorDouble3LengthSq(VectorDoubleSubtract(closestPoint, localCenter)), VectorDoubleMultiply(sphereRadius, sphereRadius))))
        return ContainmentType::Disjoint;

    if(VectorDouble3LessOrEqual(VectorDoubleAbs(localCenter), VectorDoubleSubtract(boxExtents, sphereRadius)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleOrientedBox::contains(const BoundingDoubleSphere& sphere)const noexcept{
    return containsSphereValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::containsBoxValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents)const noexcept{
    SIMDVectorDouble corners[BoundingDoubleBox::s_DoubleCornerCount];
    CollisionDoubleDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    if(CollisionDoubleDetail::PointsInsideObb(corners, BoundingDoubleBox::s_DoubleCornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleOrientedBox::contains(const BoundingDoubleBox& box)const noexcept{
    return containsBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(box.center), LoadDouble(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::containsOrientedBoxValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation)const noexcept{
    SIMDVectorDouble corners[BoundingDoubleOrientedBox::s_DoubleCornerCount];
    CollisionDoubleDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    if(CollisionDoubleDetail::PointsInsideObb(corners, BoundingDoubleOrientedBox::s_DoubleCornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsOrientedBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents, otherBoxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleOrientedBox::contains(const BoundingDoubleOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::containsFrustumValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue)const noexcept{
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
    if(CollisionDoubleDetail::PointsInsideObb(corners, BoundingDoubleFrustum::s_DoubleCornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::FrustumPlanesIntersectOrientedBox(planes, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleOrientedBox::contains(const BoundingDoubleFrustum& frustum)const noexcept{
    return containsFrustumValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleOrientedBox::intersects(const BoundingDoubleSphere& sphere)const noexcept{
    return sphere.intersectsOrientedBoxValues(LoadDouble(sphere.centerRadius), LoadDouble(center), LoadDouble(extents), LoadDouble(orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleOrientedBox::intersectsBoxValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents)const noexcept{
    return CollisionDoubleDetail::ObbIntersectsObb(
        boxCenter,
        boxExtents,
        boxOrientation,
        otherBoxCenter,
        otherBoxExtents,
        s_DoubleSIMDIdentityR3
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleOrientedBox::intersects(const BoundingDoubleBox& box)const noexcept{
    return intersectsBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(box.center), LoadDouble(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleOrientedBox::intersectsOrientedBoxValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation)const noexcept{
    return CollisionDoubleDetail::ObbIntersectsObb(
        boxCenter,
        boxExtents,
        boxOrientation,
        otherBoxCenter,
        otherBoxExtents,
        otherBoxOrientation
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleOrientedBox::intersects(const BoundingDoubleOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleOrientedBox::intersects(const BoundingDoubleFrustum& frustum)const noexcept{
    return frustum.intersectsOrientedBoxValues(LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadDouble(center), LoadDouble(extents), LoadDouble(orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleOrientedBox::IntersectsTriangleValue(
    SIMDVectorDouble boxCenter,
    SIMDVectorDouble boxExtents,
    SIMDVectorDouble boxOrientation,
    SIMDVectorDouble v0,
    SIMDVectorDouble v1,
    SIMDVectorDouble v2
)noexcept{
    const SIMDVectorDouble localV0 = CollisionDoubleDetail::PointToObbLocal(v0, boxCenter, boxOrientation);
    const SIMDVectorDouble localV1 = CollisionDoubleDetail::PointToObbLocal(v1, boxCenter, boxOrientation);
    const SIMDVectorDouble localV2 = CollisionDoubleDetail::PointToObbLocal(v2, boxCenter, boxOrientation);
    return CollisionDoubleDetail::TriangleAabbOverlap(localV0, localV1, localV2, VectorDoubleNegate(boxExtents), boxExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleOrientedBox::intersects(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)const noexcept{
    return IntersectsTriangleValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::IntersectsPlaneValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble plane)noexcept{
    SIMDVectorDouble outside{};
    SIMDVectorDouble inside{};
    SIMDVectorDouble axis0{};
    SIMDVectorDouble axis1{};
    SIMDVectorDouble axis2{};
    CollisionDoubleDetail::ObbAxes(boxOrientation, axis0, axis1, axis2);
    CollisionDoubleDetail::FastIntersectOrientedBoxPlane(
        boxCenter,
        boxExtents,
        axis0,
        axis1,
        axis2,
        plane,
        outside,
        inside
    );
    if(CollisionDoubleDetail::VectorDouble4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDoubleDetail::VectorDouble4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::intersects(const SIMDVectorDouble plane)const noexcept{
    return IntersectsPlaneValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleOrientedBox::IntersectsRayValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble origin, SIMDVectorDouble direction)noexcept{
    const SIMDVectorDouble localOrigin = CollisionDoubleDetail::PointToObbLocal(origin, boxCenter, boxOrientation);
    const SIMDVectorDouble localDirection = VectorDouble3InverseRotate(direction, boxOrientation);
    return CollisionDoubleDetail::RayIntersectsMinMax(localOrigin, localDirection, VectorDoubleNegate(boxExtents), boxExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleOrientedBox::intersects(
    SIMDVectorDouble origin,
    SIMDVectorDouble direction
)const noexcept{
    return IntersectsRayValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), origin, direction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::ContainedByValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)noexcept{
    SIMDVectorDouble corners[s_DoubleCornerCount];
    CollisionDoubleDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
    const SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDoubleDetail::ContainmentFromPlaneTests(corners, s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::containedBy(
    SIMDVectorDouble plane0,
    SIMDVectorDouble plane1,
    SIMDVectorDouble plane2,
    SIMDVectorDouble plane3,
    SIMDVectorDouble plane4,
    SIMDVectorDouble plane5
)const noexcept{
    return ContainedByValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingDoubleOrientedBox::CreateFromBoundingBox(BoundingDoubleOrientedBox& outBox, const BoundingDoubleBox& box)noexcept{
    outBox.center = box.center;
    outBox.extents = box.extents;
    outBox.orientation = Double4(0.0, 0.0, 0.0, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingDoubleOrientedBox::CreateFromPoints(
    BoundingDoubleOrientedBox& outBox,
    const usize count,
    const Double3U* points,
    const usize stride
)noexcept{
    BoundingDoubleBox box;
    BoundingDoubleBox::CreateFromPoints(box, count, points, stride);
    CreateFromBoundingBox(outBox, box);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

