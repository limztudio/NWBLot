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


inline void NWB_SIMD_CALL BoundingSphere::transform(BoundingSphere& outSphere, const SIMDMatrix& matrix)const noexcept{
    StoreFloat(TransformSphereValue(LoadFloat(centerRadius), matrix), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingSphere::transform(
    BoundingSphere& outSphere,
    const f32 scale,
    const SIMDVectorSingle rotation,
    const SIMDVectorSingle translation
)const noexcept{
    StoreFloat(TransformSphereValue(LoadFloat(centerRadius), scale, rotation, translation), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingSphere::ContainsPointValue(SIMDVectorSingle sphereValue, SIMDVectorSingle point)noexcept{
    const SIMDVectorSingle delta = VectorSubtract(point, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVectorSingle sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(delta), VectorMultiply(sphereRadius, sphereRadius))) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingSphere::contains(const SIMDVectorSingle point)const noexcept{
    return ContainsPointValue(LoadFloat(centerRadius), point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingSphere::ContainsTriangleValue(
    SIMDVectorSingle sphereValue,
    SIMDVectorSingle v0,
    SIMDVectorSingle v1,
    SIMDVectorSingle v2
)noexcept{
    const SIMDVectorSingle points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    if(CollisionDetail::PointsInsideSphere(points, CollisionDetail::s_TriangleVertexCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return IntersectsTriangleValue(sphereValue, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingSphere::contains(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)const noexcept{
    return ContainsTriangleValue(LoadFloat(centerRadius), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingSphere::containsSphereValue(const SIMDVectorSingle sphereValue, const SIMDVectorSingle otherSphereValue)const noexcept{
    const SIMDVectorSingle sphereRadius = VectorSplatW(sphereValue);
    const SIMDVectorSingle otherRadius = VectorSplatW(otherSphereValue);
    const SIMDVectorSingle delta = VectorSubtract(CollisionDetail::SphereCenter(otherSphereValue), CollisionDetail::SphereCenter(sphereValue));
    const SIMDVectorSingle distanceSq = Vector3LengthSq(delta);
    if(Vector4GreaterOrEqual(sphereRadius, otherRadius)){
        const SIMDVectorSingle radiusDelta = VectorSubtract(sphereRadius, otherRadius);
        if(Vector4LessOrEqual(distanceSq, VectorMultiply(radiusDelta, radiusDelta)))
            return ContainmentType::Contains;
    }

    const SIMDVectorSingle radiusSum = VectorAdd(sphereRadius, otherRadius);
    if(Vector4GreaterOrEqual(distanceSq, VectorMultiply(radiusSum, radiusSum)))
        return ContainmentType::Disjoint;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingSphere& sphere)const noexcept{
    return containsSphereValue(LoadFloat(centerRadius), LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingSphere::containsBoxValues(const SIMDVectorSingle sphereValue, const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents)const noexcept{
    if(!intersectsBoxValues(sphereValue, boxCenter, boxExtents))
        return ContainmentType::Disjoint;

    SIMDVectorSingle corners[BoundingBox::s_CornerCount];
    CollisionDetail::AabbCorners(boxCenter, boxExtents, corners);
    if(CollisionDetail::PointsInsideSphere(corners, BoundingBox::s_CornerCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingBox& box)const noexcept{
    return containsBoxValues(LoadFloat(centerRadius), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingSphere::containsOrientedBoxValues(const SIMDVectorSingle sphereValue, const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation)const noexcept{
    if(!intersectsOrientedBoxValues(sphereValue, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Disjoint;

    SIMDVectorSingle corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
    if(CollisionDetail::PointsInsideSphere(corners, BoundingOrientedBox::s_CornerCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadFloat(centerRadius), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingSphere::containsFrustumValues(const SIMDVectorSingle sphereValue, const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue)const noexcept{
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
    if(CollisionDetail::PointsInsideSphere(corners, BoundingFrustum::s_CornerCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingFrustum& frustum)const noexcept{
    if(!intersects(frustum))
        return ContainmentType::Disjoint;
    return containsFrustumValues(LoadFloat(centerRadius), LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingSphere::intersectsSphereValue(const SIMDVectorSingle sphereValue, const SIMDVectorSingle otherSphereValue)const noexcept{
    const SIMDVectorSingle radiusSum = VectorAdd(CollisionDetail::SphereRadius(sphereValue), CollisionDetail::SphereRadius(otherSphereValue));
    const SIMDVectorSingle delta = VectorSubtract(CollisionDetail::SphereCenter(otherSphereValue), CollisionDetail::SphereCenter(sphereValue));
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(delta), VectorMultiply(radiusSum, radiusSum)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingSphere& sphere)const noexcept{
    return intersectsSphereValue(LoadFloat(centerRadius), LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingSphere::intersectsBoxValues(const SIMDVectorSingle sphereValue, const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents)const noexcept{
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    const SIMDVectorSingle centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVectorSingle sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVectorSingle closestPoint = CollisionDetail::ClosestPointOnMinMax(centerVector, minBounds, maxBounds);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, centerVector)), VectorMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingBox& box)const noexcept{
    return intersectsBoxValues(LoadFloat(centerRadius), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingSphere::intersectsOrientedBoxValues(const SIMDVectorSingle sphereValue, const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation)const noexcept{
    const SIMDVectorSingle sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVectorSingle localCenter = CollisionDetail::PointToObbLocal(CollisionDetail::SphereCenter(sphereValue), boxCenter, boxOrientation);
    const SIMDVectorSingle closestPoint = CollisionDetail::ClosestPointOnMinMax(localCenter, VectorNegate(boxExtents), boxExtents);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, localCenter)), VectorMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadFloat(centerRadius), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingFrustum& frustum)const noexcept{
    return frustum.intersectsSphereValues(LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadFloat(centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingSphere::IntersectsTriangleValue(
    SIMDVectorSingle sphereValue,
    SIMDVectorSingle v0,
    SIMDVectorSingle v1,
    SIMDVectorSingle v2
)noexcept{
    const SIMDVectorSingle centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVectorSingle sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVectorSingle closestPoint = CollisionDetail::ClosestPointOnTriangle(centerVector, v0, v1, v2);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, centerVector)), VectorMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingSphere::intersects(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)const noexcept{
    return IntersectsTriangleValue(LoadFloat(centerRadius), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingSphere::IntersectsPlaneValue(SIMDVectorSingle sphereValue, SIMDVectorSingle plane)noexcept{
    const SIMDVectorSingle distance = CollisionDetail::PlaneDistance(plane, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVectorSingle sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(distance, sphereRadius)))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(VectorLess(distance, VectorNegate(sphereRadius))))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingSphere::intersects(const SIMDVectorSingle plane)const noexcept{
    return IntersectsPlaneValue(LoadFloat(centerRadius), plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingSphere::IntersectsRayValue(SIMDVectorSingle sphereValue, SIMDVectorSingle origin, SIMDVectorSingle direction)noexcept{
    const SIMDVectorSingle sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVectorSingle localOrigin = VectorSubtract(origin, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVectorSingle bVector = Vector3Dot(localOrigin, direction);
    const SIMDVectorSingle cVector = VectorSubtract(Vector3LengthSq(localOrigin), VectorMultiply(sphereRadius, sphereRadius));
    if(Vector4Greater(cVector, VectorZero()) && Vector4Greater(bVector, VectorZero()))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle discriminantVector = VectorSubtract(VectorMultiply(bVector, bVector), cVector);
    if(Vector4Less(discriminantVector, VectorZero()))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle distance = VectorSubtract(VectorNegate(bVector), VectorSqrt(discriminantVector));
    return Max(0.0f, VectorGetX(distance));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingSphere::intersects(
    SIMDVectorSingle origin,
    SIMDVectorSingle direction
)const noexcept{
    return IntersectsRayValue(LoadFloat(centerRadius), origin, direction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingSphere::ContainedByValue(SIMDVectorSingle sphereValue, SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)noexcept{
    const SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromSpherePlaneTests(sphereValue, planes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingSphere::containedBy(
    SIMDVectorSingle plane0,
    SIMDVectorSingle plane1,
    SIMDVectorSingle plane2,
    SIMDVectorSingle plane3,
    SIMDVectorSingle plane4,
    SIMDVectorSingle plane5
)const noexcept{
    return ContainedByValue(LoadFloat(centerRadius), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorSingle NWB_SIMD_CALL BoundingSphere::MergeSphereValues(SIMDVectorSingle sphereValue0, SIMDVectorSingle sphereValue1, bool& outDirectCopy, bool& outCopyFirst)noexcept{
    outDirectCopy = false;
    outCopyFirst = true;
    const SIMDVectorSingle center0 = CollisionDetail::SphereCenter(sphereValue0);
    const SIMDVectorSingle center1 = CollisionDetail::SphereCenter(sphereValue1);
    const SIMDVectorSingle radius0 = VectorSplatW(sphereValue0);
    const SIMDVectorSingle radius1 = VectorSplatW(sphereValue1);
    const SIMDVectorSingle delta = VectorSubtract(center1, center0);
    const SIMDVectorSingle distanceSquared = Vector3LengthSq(delta);
    const SIMDVectorSingle radiusDelta = VectorSubtract(radius0, radius1);

    if(Vector4GreaterOrEqual(VectorMultiply(radiusDelta, radiusDelta), distanceSquared)){
        outDirectCopy = true;
        outCopyFirst = Vector4GreaterOrEqual(radius0, radius1);
        return outCopyFirst ? sphereValue0 : sphereValue1;
    }

    const SIMDVectorSingle distance = VectorSqrt(distanceSquared);
    const SIMDVectorSingle newRadius = VectorMultiply(
        VectorAdd(distance, VectorAdd(radius0, radius1)),
        s_SIMDOneHalf
    );
    SIMDVectorSingle newCenter = center0;
    if(Vector4Greater(distance, VectorReplicate(CollisionDetail::s_RayEpsilon)))
        newCenter = VectorMultiplyAdd(delta, VectorDivide(VectorSubtract(newRadius, radius0), distance), center0);

    return CollisionDetail::SphereCenterRadius(newCenter, newRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::CreateMerged(
    BoundingSphere& outSphere,
    const BoundingSphere& sphere0,
    const BoundingSphere& sphere1
)noexcept{
    bool directCopy = false;
    bool copyFirst = true;
    const SIMDVectorSingle mergedValue = MergeSphereValues(LoadFloat(sphere0.centerRadius), LoadFloat(sphere1.centerRadius), directCopy, copyFirst);
    if(directCopy){
        outSphere = copyFirst ? sphere0 : sphere1;
        return;
    }

    StoreFloat(mergedValue, outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorSingle NWB_SIMD_CALL BoundingSphere::SphereFromCenterExtentsValue(SIMDVectorSingle centerValue, SIMDVectorSingle extentsValue)noexcept{
    return CollisionDetail::SphereCenterRadius(centerValue, Vector3Length(extentsValue));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorSingle NWB_SIMD_CALL BoundingSphere::SphereFromCornersValue(const SIMDVectorSingle* corners, usize cornerCount)noexcept{
    return CollisionDetail::CreateSphereFromVectorPoints(corners, cornerCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::CreateFromBoundingBox(BoundingSphere& outSphere, const BoundingBox& box)noexcept{
    StoreFloat(SphereFromCenterExtentsValue(LoadFloat(box.center), LoadFloat(box.extents)), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::CreateFromBoundingBox(BoundingSphere& outSphere, const BoundingOrientedBox& box)noexcept{
    StoreFloat(SphereFromCenterExtentsValue(LoadFloat(box.center), LoadFloat(box.extents)), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::CreateFromPoints(
    BoundingSphere& outSphere,
    const usize count,
    const Float3U* points,
    const usize stride
)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(count > 0u);
    SIMDVectorSingle centerVector = VectorZero();
    for(usize i = 0u; i < count; ++i)
        centerVector = VectorAdd(centerVector, LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, i)));
    centerVector = VectorDivide(centerVector, VectorReplicate(static_cast<f32>(count)));

    SIMDVectorSingle radiusSq = VectorZero();
    for(usize i = 0u; i < count; ++i){
        const SIMDVectorSingle delta = VectorSubtract(LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, i)), centerVector);
        radiusSq = VectorMax(radiusSq, Vector3LengthSq(delta));
    }

    StoreFloat(CollisionDetail::SphereCenterRadius(centerVector, VectorSqrt(radiusSq)), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::CreateFromFrustum(BoundingSphere& outSphere, const BoundingFrustum& frustum)noexcept{
    SIMDVectorSingle corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        corners
    );
    StoreFloat(SphereFromCornersValue(corners, BoundingFrustum::s_CornerCount), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

