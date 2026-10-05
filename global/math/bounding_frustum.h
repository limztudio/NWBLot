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


inline BoundingFrustum::BoundingFrustum(const SIMDMatrix& projection, const bool rightHandedCoordinates)noexcept{
    createFromMatrix(*this, projection, rightHandedCoordinates);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void GLB_SIMD_CALL BoundingFrustum::transformFrustumValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, const SIMDMatrix& matrix, SIMDVector& outOrigin, SIMDVector& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane)noexcept{
    SIMDVector scale{};
    SIMDVector rotation{};
    SIMDVector translation{};
    if(!MatrixDecompose(scale, rotation, translation, matrix)){
        scale = s_SIMDOne;
        rotation = s_SIMDIdentityR3;
        translation = Vector3Transform(VectorZero(), matrix);
    }

    const SIMDVector absScale = VectorAbs(scale);
    const SIMDVector maxScale = CollisionDetail::Vector3MaxComponent(absScale);
    const SIMDVector scaledPlanes = VectorMultiply(VectorSet(nearPlaneValue, farPlaneValue, 0.0f, 0.0f), maxScale);
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


inline void GLB_SIMD_CALL BoundingFrustum::transform(BoundingFrustum& outFrustum, const SIMDMatrix& matrix)const noexcept{
    SIMDVector originVector{};
    SIMDVector orientationVector{};
    transformFrustumValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, matrix, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope, outFrustum.bottomSlope, outFrustum.nearPlane, outFrustum.farPlane);
    StoreFloat(VectorSetW(originVector, 0.0f), outFrustum.origin);
    StoreFloat(orientationVector, outFrustum.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void GLB_SIMD_CALL BoundingFrustum::transformFrustumValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, f32 scale, SIMDVector rotation, SIMDVector translation, SIMDVector& outOrigin, SIMDVector& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane)noexcept{
    outOrigin = VectorAdd(Vector3Rotate(VectorScale(frustumOrigin, scale), rotation), translation);
    outOrientation = QuaternionNormalize(QuaternionMultiply(frustumOrientation, rotation));
    const SIMDVector scaledPlanes = VectorMultiply(
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


inline void GLB_SIMD_CALL BoundingFrustum::transform(
    BoundingFrustum& outFrustum,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{
    SIMDVector originVector{};
    SIMDVector orientationVector{};
    transformFrustumValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, scale, rotation, translation, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope, outFrustum.bottomSlope, outFrustum.nearPlane, outFrustum.farPlane);
    StoreFloat(VectorSetW(originVector, 0.0f), outFrustum.origin);
    StoreFloat(orientationVector, outFrustum.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void GLB_SIMD_CALL BoundingFrustum::cornersValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector* outCorners)noexcept{
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
    GLB_ASSERT(corners != nullptr);
    SIMDVector cornerVectors[s_CornerCount];
    cornersValue(
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


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingFrustum::containsPointValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector point)noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    for(const SIMDVector plane : planes){
        if(CollisionDetail::Vector4AllTrue(VectorLess(CollisionDetail::PlaneDistance(plane, point), VectorZero())))
            return ContainmentType::Disjoint;
    }
    return ContainmentType::Contains;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingFrustum::contains(const SIMDVector point)const noexcept{
    return containsPointValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingFrustum::containsTriangleValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::ContainmentFromPlaneTests(points, CollisionDetail::s_TriangleVertexCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingFrustum::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    return containsTriangleValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingFrustum::containsSphereValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector sphereValue)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::ContainmentFromSpherePlaneTests(sphereValue, planes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingSphere& sphere)const noexcept{
    return containsSphereValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingFrustum::containsBoxValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector corners[BoundingBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingBox& box)const noexcept{
    return containsBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingFrustum::containsOrientedBoxValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents, const SIMDVector otherBoxOrientation)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingOrientedBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingFrustum::containsFrustumValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector otherFrustumOrigin, const SIMDVector otherFrustumOrientation, const f32 otherRightSlope, const f32 otherLeftSlope, const f32 otherTopSlope, const f32 otherBottomSlope, const f32 otherNearPlane, const f32 otherFarPlane)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector corners[BoundingFrustum::s_CornerCount];
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

    SIMDVector otherPlanes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector thisCorners[BoundingFrustum::s_CornerCount];
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


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingFrustum::intersectsSphereValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector sphereValue)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectSphere(planes, sphereValue);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingSphere& sphere)const noexcept{
    return intersectsSphereValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingFrustum::intersectsBoxValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectAxisAlignedBox(planes, otherBoxCenter, otherBoxExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingBox& box)const noexcept{
    return intersectsBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingFrustum::intersectsOrientedBoxValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents, const SIMDVector otherBoxOrientation)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectOrientedBox(planes, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingFrustum::intersectsFrustumValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector otherFrustumOrigin, const SIMDVector otherFrustumOrientation, const f32 otherRightSlope, const f32 otherLeftSlope, const f32 otherTopSlope, const f32 otherBottomSlope, const f32 otherNearPlane, const f32 otherFarPlane)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector otherPlanes[CollisionDetail::s_FrustumPlaneCount];
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

    SIMDVector corners[s_CornerCount];
    SIMDVector otherCorners[s_CornerCount];
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


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingFrustum::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    return contains(v0, v1, v2) != ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum GLB_SIMD_CALL BoundingFrustum::intersectsPlaneValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector plane)noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVector outside{};
    SIMDVector inside{};
    CollisionDetail::FastIntersectPointsPlane(corners, s_CornerCount, plane, outside, inside);
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum GLB_SIMD_CALL BoundingFrustum::intersects(const SIMDVector plane)const noexcept{
    return intersectsPlaneValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingFrustum::intersectsRayValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector rayOrigin, SIMDVector direction, f32& outDistance)noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    const SIMDVector zero = VectorZero();
    const SIMDVector rayEpsilon = VectorReplicate(CollisionDetail::s_RayEpsilon);
    SIMDVector tMin = zero;
    SIMDVector tMax = VectorReplicate(s_MaxF32);
    for(const SIMDVector plane : planes){
        const SIMDVector distance = CollisionDetail::PlaneDistance(plane, rayOrigin);
        const SIMDVector denominator = Vector3Dot(plane, direction);
        if(Vector4LessOrEqual(VectorAbs(denominator), rayEpsilon)){
            if(Vector4Less(distance, zero))
                return false;
            continue;
        }

        const SIMDVector t = VectorDivide(VectorNegate(distance), denominator);
        if(Vector4Greater(denominator, zero))
            tMin = VectorMax(tMin, t);
        else
            tMax = VectorMin(tMax, t);
        if(Vector4Greater(tMin, tMax))
            return false;
    }

    outDistance = VectorGetX(tMin);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GLB_SIMD_CALL BoundingFrustum::intersects(
    const SIMDVector rayOrigin,
    const SIMDVector direction,
    f32& outDistance
)const noexcept{
    return intersectsRayValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, rayOrigin, direction, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingFrustum::containedByValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum GLB_SIMD_CALL BoundingFrustum::containedBy(
    SIMDVector plane0,
    SIMDVector plane1,
    SIMDVector plane2,
    SIMDVector plane3,
    SIMDVector plane4,
    SIMDVector plane5
)const noexcept{
    return containedByValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane0, plane1, plane2, plane3, plane4, plane5);
}


inline void BoundingFrustum::getPlanes(
    SIMDVector* nearPlaneOut,
    SIMDVector* farPlaneOut,
    SIMDVector* rightPlaneOut,
    SIMDVector* leftPlaneOut,
    SIMDVector* topPlaneOut,
    SIMDVector* bottomPlaneOut
)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
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


inline void GLB_SIMD_CALL BoundingFrustum::createFromMatrix(
    BoundingFrustum& outFrustum,
    const SIMDMatrix& projection,
    const bool rightHandedCoordinates
)noexcept{
    outFrustum.origin = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    outFrustum.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    const SIMDVector m00 = VectorSplatX(projection.v[0]);
    const SIMDVector m11 = VectorSplatY(projection.v[1]);
    const SIMDVector m02 = VectorSplatZ(projection.v[0]);
    const SIMDVector m12 = VectorSplatZ(projection.v[1]);
    const SIMDVector slopes = VectorDivide(
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

    const SIMDVector m22 = VectorSplatZ(projection.v[2]);
    const SIMDVector m23 = VectorSplatW(projection.v[2]);
    const SIMDVector planeDistances = rightHandedCoordinates
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

