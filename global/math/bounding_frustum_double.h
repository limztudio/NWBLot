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


inline BoundingDoubleFrustum::BoundingDoubleFrustum(const SIMDMatrixDouble& projection, const bool rightHandedCoordinates)noexcept{
    CreateFromMatrix(*this, projection, rightHandedCoordinates);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleFrustum::TransformFrustumValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, const SIMDMatrixDouble& matrix, SIMDVectorDouble& outOrigin, SIMDVectorDouble& outOrientation, f64& outRightSlope, f64& outLeftSlope, f64& outTopSlope, f64& outBottomSlope, f64& outNearPlane, f64& outFarPlane)noexcept{
    const auto decomposition = MatrixDoubleDecompose(matrix);
    const SIMDVectorDouble scale = decomposition ? decomposition->scale : s_DoubleSIMDOne;
    const SIMDVectorDouble rotation = decomposition ? decomposition->rotation : s_DoubleSIMDIdentityR3;

    const SIMDVectorDouble absScale = VectorDoubleAbs(scale);
    const SIMDVectorDouble maxScale = CollisionDoubleDetail::VectorDouble3MaxComponent(absScale);
    const SIMDVectorDouble scaledPlanes = VectorDoubleMultiply(VectorDoubleSet(nearPlaneValue, farPlaneValue, 0.0, 0.0), maxScale);
    outOrigin = VectorDouble3Transform(frustumOrigin, matrix);
    outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(frustumOrientation, rotation));
    outRightSlope = rightSlopeValue;
    outLeftSlope = leftSlopeValue;
    outTopSlope = topSlopeValue;
    outBottomSlope = bottomSlopeValue;
    outNearPlane = VectorDoubleGetX(scaledPlanes);
    outFarPlane = VectorDoubleGetY(scaledPlanes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleFrustum::transform(BoundingDoubleFrustum& outFrustum, const SIMDMatrixDouble& matrix)const noexcept{
    SIMDVectorDouble originVector{};
    SIMDVectorDouble orientationVector{};
    TransformFrustumValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, matrix, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope, outFrustum.bottomSlope, outFrustum.nearPlane, outFrustum.farPlane);
    StoreDouble(VectorDoubleSetW(originVector, 0.0), outFrustum.origin);
    StoreDouble(orientationVector, outFrustum.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleFrustum::TransformFrustumValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation, SIMDVectorDouble& outOrigin, SIMDVectorDouble& outOrientation, f64& outRightSlope, f64& outLeftSlope, f64& outTopSlope, f64& outBottomSlope, f64& outNearPlane, f64& outFarPlane)noexcept{
    outOrigin = VectorDoubleAdd(VectorDouble3Rotate(VectorDoubleScale(frustumOrigin, scale), rotation), translation);
    outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(frustumOrientation, rotation));
    const SIMDVectorDouble scaledPlanes = VectorDoubleMultiply(
        VectorDoubleSet(nearPlaneValue, farPlaneValue, 0.0, 0.0),
        VectorDoubleAbs(VectorDoubleReplicate(scale))
    );
    outRightSlope = rightSlopeValue;
    outLeftSlope = leftSlopeValue;
    outTopSlope = topSlopeValue;
    outBottomSlope = bottomSlopeValue;
    outNearPlane = VectorDoubleGetX(scaledPlanes);
    outFarPlane = VectorDoubleGetY(scaledPlanes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleFrustum::transform(
    BoundingDoubleFrustum& outFrustum,
    const f64 scale,
    const SIMDVectorDouble rotation,
    const SIMDVectorDouble translation
)const noexcept{
    SIMDVectorDouble originVector{};
    SIMDVectorDouble orientationVector{};
    TransformFrustumValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, scale, rotation, translation, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope, outFrustum.bottomSlope, outFrustum.nearPlane, outFrustum.farPlane);
    StoreDouble(VectorDoubleSetW(originVector, 0.0), outFrustum.origin);
    StoreDouble(orientationVector, outFrustum.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleFrustum::CornersValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble* outCorners)noexcept{
    CollisionDoubleDetail::FrustumCorners(
        frustumOrigin,
        frustumOrientation,
        rightSlopeValue,
        leftSlopeValue,
        topSlopeValue,
        bottomSlopeValue,
        nearPlaneValue,
        farPlaneValue,
        outCorners
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
inline void BoundingDoubleFrustum::getCorners(PointT* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVectorDouble cornerVectors[s_DoubleCornerCount];
    CornersValue(
        LoadDouble(origin),
        LoadDouble(orientation),
        rightSlope,
        leftSlope,
        topSlope,
        bottomSlope,
        nearPlane,
        farPlane,
        cornerVectors
    );
    for(u64 i = 0u; i < s_DoubleCornerCount; ++i)
        StoreDouble(VectorDoubleSetW(cornerVectors[i], 0.0), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::ContainsPointValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble point)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    for(const SIMDVectorDouble plane : planes){
        if(CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleLess(CollisionDoubleDetail::PlaneDoubleDistance(plane, point), VectorDoubleZero())))
            return ContainmentType::Disjoint;
    }
    return ContainmentType::Contains;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::contains(const SIMDVectorDouble point)const noexcept{
    return ContainsPointValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::ContainsTriangleValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    const SIMDVectorDouble points[CollisionDoubleDetail::s_DoubleTriangleVertexCount] = { v0, v1, v2 };
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::ContainmentFromPlaneTests(points, CollisionDoubleDetail::s_DoubleTriangleVertexCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::contains(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)const noexcept{
    return ContainsTriangleValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::containsSphereValues(const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue, const SIMDVectorDouble sphereValue)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::ContainmentFromSpherePlaneTests(sphereValue, planes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleFrustum::contains(const BoundingDoubleSphere& sphere)const noexcept{
    return containsSphereValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::containsBoxValues(const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble corners[BoundingDoubleBox::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDoubleDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    return CollisionDoubleDetail::ContainmentFromPlaneTests(corners, BoundingDoubleBox::s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleFrustum::contains(const BoundingDoubleBox& box)const noexcept{
    return containsBoxValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(box.center), LoadDouble(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::containsOrientedBoxValues(const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble corners[BoundingDoubleOrientedBox::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDoubleDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    return CollisionDoubleDetail::ContainmentFromPlaneTests(corners, BoundingDoubleOrientedBox::s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleFrustum::contains(const BoundingDoubleOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::containsFrustumValues(const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue, const SIMDVectorDouble otherFrustumOrigin, const SIMDVectorDouble otherFrustumOrientation, const f64 otherRightSlope, const f64 otherLeftSlope, const f64 otherTopSlope, const f64 otherBottomSlope, const f64 otherNearPlane, const f64 otherFarPlane)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble corners[BoundingDoubleFrustum::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDoubleDetail::FrustumCorners(
        otherFrustumOrigin,
        otherFrustumOrientation,
        otherRightSlope,
        otherLeftSlope,
        otherTopSlope,
        otherBottomSlope,
        otherNearPlane,
        otherFarPlane,
        corners
    );
    const ContainmentType::Enum containment = CollisionDoubleDetail::ContainmentFromPlaneTests(corners, BoundingDoubleFrustum::s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
    if(containment != ContainmentType::Intersects)
        return containment;

    SIMDVectorDouble otherPlanes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble thisCorners[BoundingDoubleFrustum::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumPlanes(
        otherFrustumOrigin,
        otherFrustumOrientation,
        otherRightSlope,
        otherLeftSlope,
        otherTopSlope,
        otherBottomSlope,
        otherNearPlane,
        otherFarPlane,
        otherPlanes
    );
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, thisCorners);
    return CollisionDoubleDetail::FrustumPlanesIntersectPoints(otherPlanes, thisCorners, BoundingDoubleFrustum::s_DoubleCornerCount) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleFrustum::contains(const BoundingDoubleFrustum& frustum)const noexcept{
    return containsFrustumValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleFrustum::intersectsSphereValues(const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue, const SIMDVectorDouble sphereValue)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::FrustumPlanesIntersectSphere(planes, sphereValue);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleFrustum::intersects(const BoundingDoubleSphere& sphere)const noexcept{
    return intersectsSphereValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleFrustum::intersectsBoxValues(const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::FrustumPlanesIntersectAxisAlignedBox(planes, otherBoxCenter, otherBoxExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleFrustum::intersects(const BoundingDoubleBox& box)const noexcept{
    return intersectsBoxValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(box.center), LoadDouble(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleFrustum::intersectsOrientedBoxValues(const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::FrustumPlanesIntersectOrientedBox(planes, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleFrustum::intersects(const BoundingDoubleOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleFrustum::intersectsFrustumValues(const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue, const SIMDVectorDouble otherFrustumOrigin, const SIMDVectorDouble otherFrustumOrientation, const f64 otherRightSlope, const f64 otherLeftSlope, const f64 otherTopSlope, const f64 otherBottomSlope, const f64 otherNearPlane, const f64 otherFarPlane)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble otherPlanes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDoubleDetail::FrustumPlanes(
        otherFrustumOrigin,
        otherFrustumOrientation,
        otherRightSlope,
        otherLeftSlope,
        otherTopSlope,
        otherBottomSlope,
        otherNearPlane,
        otherFarPlane,
        otherPlanes
    );

    SIMDVectorDouble corners[s_DoubleCornerCount];
    SIMDVectorDouble otherCorners[s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    CollisionDoubleDetail::FrustumCorners(
        otherFrustumOrigin,
        otherFrustumOrientation,
        otherRightSlope,
        otherLeftSlope,
        otherTopSlope,
        otherBottomSlope,
        otherNearPlane,
        otherFarPlane,
        otherCorners
    );

    return CollisionDoubleDetail::FrustumPlanesIntersectPoints(planes, otherCorners, s_DoubleCornerCount)
        && CollisionDoubleDetail::FrustumPlanesIntersectPoints(otherPlanes, corners, s_DoubleCornerCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleFrustum::intersects(const BoundingDoubleFrustum& frustum)const noexcept{
    return intersectsFrustumValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleFrustum::intersects(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)const noexcept{
    return contains(v0, v1, v2) != ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::IntersectsPlaneValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble plane)noexcept{
    SIMDVectorDouble corners[s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVectorDouble outside{};
    SIMDVectorDouble inside{};
    CollisionDoubleDetail::FastIntersectPointsPlane(corners, s_DoubleCornerCount, plane, outside, inside);
    if(CollisionDoubleDetail::VectorDouble4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDoubleDetail::VectorDouble4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::intersects(const SIMDVectorDouble plane)const noexcept{
    return IntersectsPlaneValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleFrustum::IntersectsRayValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble rayOrigin, SIMDVectorDouble direction)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble rayEpsilon = VectorDoubleReplicate(CollisionDoubleDetail::s_DoubleRayEpsilon);
    SIMDVectorDouble tMin = zero;
    SIMDVectorDouble tMax = VectorDoubleReplicate(Limit<f64>::s_Max);
    for(const SIMDVectorDouble plane : planes){
        const SIMDVectorDouble distance = CollisionDoubleDetail::PlaneDoubleDistance(plane, rayOrigin);
        const SIMDVectorDouble denominator = VectorDouble3Dot(plane, direction);
        if(VectorDouble4LessOrEqual(VectorDoubleAbs(denominator), rayEpsilon)){
            if(VectorDouble4Less(distance, zero))
                return MakeUnexpected(Failure{});
            continue;
        }

        const SIMDVectorDouble t = VectorDoubleDivide(VectorDoubleNegate(distance), denominator);
        if(VectorDouble4Greater(denominator, zero))
            tMin = VectorDoubleMax(tMin, t);
        else
            tMax = VectorDoubleMin(tMax, t);
        if(VectorDouble4Greater(tMin, tMax))
            return MakeUnexpected(Failure{});
    }

    return VectorDoubleGetX(tMin);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleFrustum::intersects(
    const SIMDVectorDouble rayOrigin,
    const SIMDVectorDouble direction
)const noexcept{
    return IntersectsRayValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, rayOrigin, direction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::ContainedByValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)noexcept{
    SIMDVectorDouble corners[s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    const SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDoubleDetail::ContainmentFromPlaneTests(corners, s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::containedBy(
    SIMDVectorDouble plane0,
    SIMDVectorDouble plane1,
    SIMDVectorDouble plane2,
    SIMDVectorDouble plane3,
    SIMDVectorDouble plane4,
    SIMDVectorDouble plane5
)const noexcept{
    return ContainedByValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane0, plane1, plane2, plane3, plane4, plane5);
}


inline void BoundingDoubleFrustum::getPlanes(
    SIMDVectorDouble* nearPlaneOut,
    SIMDVectorDouble* farPlaneOut,
    SIMDVectorDouble* rightPlaneOut,
    SIMDVectorDouble* leftPlaneOut,
    SIMDVectorDouble* topPlaneOut,
    SIMDVectorDouble* bottomPlaneOut
)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    if(nearPlaneOut)
        *nearPlaneOut = planes[CollisionDoubleDetail::FrustumPlaneIndex::Near];
    if(farPlaneOut)
        *farPlaneOut = planes[CollisionDoubleDetail::FrustumPlaneIndex::Far];
    if(rightPlaneOut)
        *rightPlaneOut = planes[CollisionDoubleDetail::FrustumPlaneIndex::Right];
    if(leftPlaneOut)
        *leftPlaneOut = planes[CollisionDoubleDetail::FrustumPlaneIndex::Left];
    if(topPlaneOut)
        *topPlaneOut = planes[CollisionDoubleDetail::FrustumPlaneIndex::Top];
    if(bottomPlaneOut)
        *bottomPlaneOut = planes[CollisionDoubleDetail::FrustumPlaneIndex::Bottom];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleFrustum::CreateFromMatrix(
    BoundingDoubleFrustum& outFrustum,
    const SIMDMatrixDouble& projection,
    const bool rightHandedCoordinates
)noexcept{
    outFrustum.origin = Double4(0.0, 0.0, 0.0, 0.0);
    outFrustum.orientation = Double4(0.0, 0.0, 0.0, 1.0);

    const SIMDVectorDouble m00 = VectorDoubleSplatX(projection.v[0]);
    const SIMDVectorDouble m11 = VectorDoubleSplatY(projection.v[1]);
    const SIMDVectorDouble m02 = VectorDoubleSplatZ(projection.v[0]);
    const SIMDVectorDouble m12 = VectorDoubleSplatZ(projection.v[1]);
    const SIMDVectorDouble slopes = VectorDoubleDivide(
        VectorDoubleMergeX(
            VectorDoubleSubtract(s_DoubleSIMDOne, m02),
            VectorDoubleSubtract(s_DoubleSIMDNegativeOne, m02),
            VectorDoubleSubtract(s_DoubleSIMDOne, m12),
            VectorDoubleSubtract(s_DoubleSIMDNegativeOne, m12)
        ),
        VectorDoubleMergeX(m00, m00, m11, m11)
    );
    outFrustum.rightSlope = VectorDoubleGetX(slopes);
    outFrustum.leftSlope = VectorDoubleGetY(slopes);
    outFrustum.topSlope = VectorDoubleGetZ(slopes);
    outFrustum.bottomSlope = VectorDoubleGetW(slopes);

    const SIMDVectorDouble m22 = VectorDoubleSplatZ(projection.v[2]);
    const SIMDVectorDouble m23 = VectorDoubleSplatW(projection.v[2]);
    const SIMDVectorDouble planeDistances = rightHandedCoordinates
        ? VectorDoubleDivide(
            VectorDoubleMergeX(m23, m23, VectorDoubleZero(), VectorDoubleZero()),
            VectorDoubleMergeX(m22, VectorDoubleAdd(m22, s_DoubleSIMDOne), s_DoubleSIMDOne, s_DoubleSIMDOne)
        )
        : VectorDoubleDivide(
            VectorDoubleMergeX(VectorDoubleNegate(m23), m23, VectorDoubleZero(), VectorDoubleZero()),
            VectorDoubleMergeX(m22, VectorDoubleSubtract(s_DoubleSIMDOne, m22), s_DoubleSIMDOne, s_DoubleSIMDOne)
        )
    ;
    outFrustum.nearPlane = VectorDoubleGetX(planeDistances);
    outFrustum.farPlane = VectorDoubleGetY(planeDistances);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

