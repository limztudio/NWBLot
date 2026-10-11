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


inline BoundingFrustum::BoundingFrustum(const SIMDMatrix& projection, const bool rightHandedCoordinates)noexcept{
    CreateFromMatrix(*this, projection, rightHandedCoordinates);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingFrustum::TransformFrustumValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, const SIMDMatrix& matrix, SIMDVectorSingle& outOrigin, SIMDVectorSingle& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane)noexcept{
    const auto decomposition = MatrixDecompose(matrix);
    const SIMDVectorSingle scale = decomposition ? decomposition->scale : s_SIMDOne;
    const SIMDVectorSingle rotation = decomposition ? decomposition->rotation : s_SIMDIdentityR3;

    const SIMDVectorSingle absScale = VectorAbs(scale);
    const SIMDVectorSingle maxScale = CollisionDetail::Vector3MaxComponent(absScale);
    const SIMDVectorSingle scaledPlanes = VectorMultiply(VectorSet(nearPlaneValue, farPlaneValue, 0.0f, 0.0f), maxScale);
    outOrigin = Vector3Transform(frustumOrigin, matrix);
    outOrientation = QuaternionNormalize(QuaternionMultiply(frustumOrientation, rotation));
    outRightSlope = rightSlopeValue;
    outLeftSlope = leftSlopeValue;
    outTopSlope = topSlopeValue;
    outBottomSlope = bottomSlopeValue;
    outNearPlane = VectorGetX(scaledPlanes);
    outFarPlane = VectorGetY(scaledPlanes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingFrustum::transform(BoundingFrustum& outFrustum, const SIMDMatrix& matrix)const noexcept{
    SIMDVectorSingle originVector{};
    SIMDVectorSingle orientationVector{};
    TransformFrustumValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, matrix, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope, outFrustum.bottomSlope, outFrustum.nearPlane, outFrustum.farPlane);
    StoreFloat(VectorSetW(originVector, 0.0f), outFrustum.origin);
    StoreFloat(orientationVector, outFrustum.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingFrustum::TransformFrustumValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation, SIMDVectorSingle& outOrigin, SIMDVectorSingle& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane)noexcept{
    outOrigin = VectorAdd(Vector3Rotate(VectorScale(frustumOrigin, scale), rotation), translation);
    outOrientation = QuaternionNormalize(QuaternionMultiply(frustumOrientation, rotation));
    const SIMDVectorSingle scaledPlanes = VectorMultiply(
        VectorSet(nearPlaneValue, farPlaneValue, 0.0f, 0.0f),
        VectorAbs(VectorReplicate(scale))
    );
    outRightSlope = rightSlopeValue;
    outLeftSlope = leftSlopeValue;
    outTopSlope = topSlopeValue;
    outBottomSlope = bottomSlopeValue;
    outNearPlane = VectorGetX(scaledPlanes);
    outFarPlane = VectorGetY(scaledPlanes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingFrustum::transform(
    BoundingFrustum& outFrustum,
    const f32 scale,
    const SIMDVectorSingle rotation,
    const SIMDVectorSingle translation
)const noexcept{
    SIMDVectorSingle originVector{};
    SIMDVectorSingle orientationVector{};
    TransformFrustumValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, scale, rotation, translation, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope, outFrustum.bottomSlope, outFrustum.nearPlane, outFrustum.farPlane);
    StoreFloat(VectorSetW(originVector, 0.0f), outFrustum.origin);
    StoreFloat(orientationVector, outFrustum.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingFrustum::CornersValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle* outCorners)noexcept{
    CollisionDetail::FrustumCorners(
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


inline void BoundingFrustum::getCorners(Float3U* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVectorSingle cornerVectors[s_CornerCount];
    CornersValue(
        LoadFloat(origin),
        LoadFloat(orientation),
        rightSlope,
        leftSlope,
        topSlope,
        bottomSlope,
        nearPlane,
        farPlane,
        cornerVectors
    );
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::ContainsPointValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle point)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    for(const SIMDVectorSingle plane : planes){
        if(CollisionDetail::Vector4AllTrue(VectorLess(CollisionDetail::PlaneDistance(plane, point), VectorZero())))
            return ContainmentType::Disjoint;
    }
    return ContainmentType::Contains;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::contains(const SIMDVectorSingle point)const noexcept{
    return ContainsPointValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::ContainsTriangleValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    const SIMDVectorSingle points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::ContainmentFromPlaneTests(points, CollisionDetail::s_TriangleVertexCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::contains(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)const noexcept{
    return ContainsTriangleValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::containsSphereValues(const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVectorSingle sphereValue)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::ContainmentFromSpherePlaneTests(sphereValue, planes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingSphere& sphere)const noexcept{
    return containsSphereValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::containsBoxValues(const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle corners[BoundingBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingBox& box)const noexcept{
    return containsBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::containsOrientedBoxValues(const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingOrientedBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::containsFrustumValues(const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVectorSingle otherFrustumOrigin, const SIMDVectorSingle otherFrustumOrientation, const f32 otherRightSlope, const f32 otherLeftSlope, const f32 otherTopSlope, const f32 otherBottomSlope, const f32 otherNearPlane, const f32 otherFarPlane)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::FrustumCorners(
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
    const ContainmentType::Enum containment = CollisionDetail::ContainmentFromPlaneTests(corners, BoundingFrustum::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
    if(containment != ContainmentType::Intersects)
        return containment;

    SIMDVectorSingle otherPlanes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle thisCorners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumPlanes(
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
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, thisCorners);
    return CollisionDetail::FrustumPlanesIntersectPoints(otherPlanes, thisCorners, BoundingFrustum::s_CornerCount) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingFrustum& frustum)const noexcept{
    return containsFrustumValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingFrustum::intersectsSphereValues(const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVectorSingle sphereValue)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectSphere(planes, sphereValue);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingSphere& sphere)const noexcept{
    return intersectsSphereValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingFrustum::intersectsBoxValues(const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectAxisAlignedBox(planes, otherBoxCenter, otherBoxExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingBox& box)const noexcept{
    return intersectsBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingFrustum::intersectsOrientedBoxValues(const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectOrientedBox(planes, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingFrustum::intersectsFrustumValues(const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVectorSingle otherFrustumOrigin, const SIMDVectorSingle otherFrustumOrientation, const f32 otherRightSlope, const f32 otherLeftSlope, const f32 otherTopSlope, const f32 otherBottomSlope, const f32 otherNearPlane, const f32 otherFarPlane)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle otherPlanes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::FrustumPlanes(
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

    SIMDVectorSingle corners[s_CornerCount];
    SIMDVectorSingle otherCorners[s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    CollisionDetail::FrustumCorners(
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

    return CollisionDetail::FrustumPlanesIntersectPoints(planes, otherCorners, s_CornerCount)
        && CollisionDetail::FrustumPlanesIntersectPoints(otherPlanes, corners, s_CornerCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingFrustum& frustum)const noexcept{
    return intersectsFrustumValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingFrustum::intersects(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)const noexcept{
    return contains(v0, v1, v2) != ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingFrustum::IntersectsPlaneValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle plane)noexcept{
    SIMDVectorSingle corners[s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVectorSingle outside{};
    SIMDVectorSingle inside{};
    CollisionDetail::FastIntersectPointsPlane(corners, s_CornerCount, plane, outside, inside);
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingFrustum::intersects(const SIMDVectorSingle plane)const noexcept{
    return IntersectsPlaneValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingFrustum::IntersectsRayValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle rayOrigin, SIMDVectorSingle direction)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle rayEpsilon = VectorReplicate(CollisionDetail::s_RayEpsilon);
    SIMDVectorSingle tMin = zero;
    SIMDVectorSingle tMax = VectorReplicate(s_MaxF32);
    for(const SIMDVectorSingle plane : planes){
        const SIMDVectorSingle distance = CollisionDetail::PlaneDistance(plane, rayOrigin);
        const SIMDVectorSingle denominator = Vector3Dot(plane, direction);
        if(Vector4LessOrEqual(VectorAbs(denominator), rayEpsilon)){
            if(Vector4Less(distance, zero))
                return MakeUnexpected(Failure{});
            continue;
        }

        const SIMDVectorSingle t = VectorDivide(VectorNegate(distance), denominator);
        if(Vector4Greater(denominator, zero))
            tMin = VectorMax(tMin, t);
        else
            tMax = VectorMin(tMax, t);
        if(Vector4Greater(tMin, tMax))
            return MakeUnexpected(Failure{});
    }

    return VectorGetX(tMin);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingFrustum::intersects(
    const SIMDVectorSingle rayOrigin,
    const SIMDVectorSingle direction
)const noexcept{
    return IntersectsRayValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, rayOrigin, direction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::ContainedByValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)noexcept{
    SIMDVectorSingle corners[s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    const SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::containedBy(
    SIMDVectorSingle plane0,
    SIMDVectorSingle plane1,
    SIMDVectorSingle plane2,
    SIMDVectorSingle plane3,
    SIMDVectorSingle plane4,
    SIMDVectorSingle plane5
)const noexcept{
    return ContainedByValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane0, plane1, plane2, plane3, plane4, plane5);
}


inline void BoundingFrustum::getPlanes(
    SIMDVectorSingle* nearPlaneOut,
    SIMDVectorSingle* farPlaneOut,
    SIMDVectorSingle* rightPlaneOut,
    SIMDVectorSingle* leftPlaneOut,
    SIMDVectorSingle* topPlaneOut,
    SIMDVectorSingle* bottomPlaneOut
)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    if(nearPlaneOut)
        *nearPlaneOut = planes[CollisionDetail::FrustumPlaneIndex::Near];
    if(farPlaneOut)
        *farPlaneOut = planes[CollisionDetail::FrustumPlaneIndex::Far];
    if(rightPlaneOut)
        *rightPlaneOut = planes[CollisionDetail::FrustumPlaneIndex::Right];
    if(leftPlaneOut)
        *leftPlaneOut = planes[CollisionDetail::FrustumPlaneIndex::Left];
    if(topPlaneOut)
        *topPlaneOut = planes[CollisionDetail::FrustumPlaneIndex::Top];
    if(bottomPlaneOut)
        *bottomPlaneOut = planes[CollisionDetail::FrustumPlaneIndex::Bottom];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingFrustum::CreateFromMatrix(
    BoundingFrustum& outFrustum,
    const SIMDMatrix& projection,
    const bool rightHandedCoordinates
)noexcept{
    outFrustum.origin = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    outFrustum.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    const SIMDVectorSingle m00 = VectorSplatX(projection.v[0]);
    const SIMDVectorSingle m11 = VectorSplatY(projection.v[1]);
    const SIMDVectorSingle m02 = VectorSplatZ(projection.v[0]);
    const SIMDVectorSingle m12 = VectorSplatZ(projection.v[1]);
    const SIMDVectorSingle slopes = VectorDivide(
        VectorMergeX(
            VectorSubtract(s_SIMDOne, m02),
            VectorSubtract(s_SIMDNegativeOne, m02),
            VectorSubtract(s_SIMDOne, m12),
            VectorSubtract(s_SIMDNegativeOne, m12)
        ),
        VectorMergeX(m00, m00, m11, m11)
    );
    outFrustum.rightSlope = VectorGetX(slopes);
    outFrustum.leftSlope = VectorGetY(slopes);
    outFrustum.topSlope = VectorGetZ(slopes);
    outFrustum.bottomSlope = VectorGetW(slopes);

    const SIMDVectorSingle m22 = VectorSplatZ(projection.v[2]);
    const SIMDVectorSingle m23 = VectorSplatW(projection.v[2]);
    const SIMDVectorSingle planeDistances = rightHandedCoordinates
        ? VectorDivide(
            VectorMergeX(m23, m23, VectorZero(), VectorZero()),
            VectorMergeX(m22, VectorAdd(m22, s_SIMDOne), s_SIMDOne, s_SIMDOne)
        )
        : VectorDivide(
            VectorMergeX(VectorNegate(m23), m23, VectorZero(), VectorZero()),
            VectorMergeX(m22, VectorSubtract(s_SIMDOne, m22), s_SIMDOne, s_SIMDOne)
        )
    ;
    outFrustum.nearPlane = VectorGetX(planeDistances);
    outFrustum.farPlane = VectorGetY(planeDistances);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

