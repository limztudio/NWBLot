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


inline void SIMDCALL BoundingSphere::transform(BoundingSphere& outSphere, const SIMDMatrix& matrix)const noexcept{
    StoreFloat(transformSphereValue(LoadFloat(centerRadius), matrix), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingSphere::transform(
    BoundingSphere& outSphere,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{
    StoreFloat(transformSphereValue(LoadFloat(centerRadius), scale, rotation, translation), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containsPointValue(SIMDVector sphereValue, SIMDVector point)noexcept{
    const SIMDVector delta = VectorSubtract(point, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(delta), VectorMultiply(sphereRadius, sphereRadius))) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::contains(const SIMDVector point)const noexcept{
    return containsPointValue(LoadFloat(centerRadius), point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containsTriangleValue(
    SIMDVector sphereValue,
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2
)noexcept{
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    if(CollisionDetail::PointsInsideSphere(points, CollisionDetail::s_TriangleVertexCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return intersectsTriangleValue(sphereValue, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    return containsTriangleValue(LoadFloat(centerRadius), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containsSphereValue(const SIMDVector sphereValue, const SIMDVector otherSphereValue)const noexcept{
    const SIMDVector sphereRadius = VectorSplatW(sphereValue);
    const SIMDVector otherRadius = VectorSplatW(otherSphereValue);
    const SIMDVector delta = VectorSubtract(CollisionDetail::SphereCenter(otherSphereValue), CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector distanceSq = Vector3LengthSq(delta);
    if(Vector4GreaterOrEqual(sphereRadius, otherRadius)){
        const SIMDVector radiusDelta = VectorSubtract(sphereRadius, otherRadius);
        if(Vector4LessOrEqual(distanceSq, VectorMultiply(radiusDelta, radiusDelta)))
            return ContainmentType::Contains;
    }

    const SIMDVector radiusSum = VectorAdd(sphereRadius, otherRadius);
    if(Vector4GreaterOrEqual(distanceSq, VectorMultiply(radiusSum, radiusSum)))
        return ContainmentType::Disjoint;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingSphere& sphere)const noexcept{
    return containsSphereValue(LoadFloat(centerRadius), LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containsBoxValues(const SIMDVector sphereValue, const SIMDVector boxCenter, const SIMDVector boxExtents)const noexcept{
    if(!intersectsBoxValues(sphereValue, boxCenter, boxExtents))
        return ContainmentType::Disjoint;

    SIMDVector corners[BoundingBox::s_CornerCount];
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


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containsOrientedBoxValues(const SIMDVector sphereValue, const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation)const noexcept{
    if(!intersectsOrientedBoxValues(sphereValue, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Disjoint;

    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
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


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containsFrustumValues(const SIMDVector sphereValue, const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue)const noexcept{
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


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersectsSphereValue(const SIMDVector sphereValue, const SIMDVector otherSphereValue)const noexcept{
    const SIMDVector radiusSum = VectorAdd(CollisionDetail::SphereRadius(sphereValue), CollisionDetail::SphereRadius(otherSphereValue));
    const SIMDVector delta = VectorSubtract(CollisionDetail::SphereCenter(otherSphereValue), CollisionDetail::SphereCenter(sphereValue));
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(delta), VectorMultiply(radiusSum, radiusSum)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingSphere& sphere)const noexcept{
    return intersectsSphereValue(LoadFloat(centerRadius), LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersectsBoxValues(const SIMDVector sphereValue, const SIMDVector boxCenter, const SIMDVector boxExtents)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(centerVector, minBounds, maxBounds);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, centerVector)), VectorMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingBox& box)const noexcept{
    return intersectsBoxValues(LoadFloat(centerRadius), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersectsOrientedBoxValues(const SIMDVector sphereValue, const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation)const noexcept{
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector localCenter = CollisionDetail::PointToObbLocal(CollisionDetail::SphereCenter(sphereValue), boxCenter, boxOrientation);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(localCenter, VectorNegate(boxExtents), boxExtents);
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


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersectsTriangleValue(
    SIMDVector sphereValue,
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2
)noexcept{
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnTriangle(centerVector, v0, v1, v2);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, centerVector)), VectorMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    return intersectsTriangleValue(LoadFloat(centerRadius), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingSphere::intersectsPlaneValue(SIMDVector sphereValue, SIMDVector plane)noexcept{
    const SIMDVector distance = CollisionDetail::PlaneDistance(plane, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(distance, sphereRadius)))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(VectorLess(distance, VectorNegate(sphereRadius))))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingSphere::intersects(const SIMDVector plane)const noexcept{
    return intersectsPlaneValue(LoadFloat(centerRadius), plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersectsRayValue(SIMDVector sphereValue, SIMDVector origin, SIMDVector direction, f32& outDistance)noexcept{
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector localOrigin = VectorSubtract(origin, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector bVector = Vector3Dot(localOrigin, direction);
    const SIMDVector cVector = VectorSubtract(Vector3LengthSq(localOrigin), VectorMultiply(sphereRadius, sphereRadius));
    if(Vector4Greater(cVector, VectorZero()) && Vector4Greater(bVector, VectorZero()))
        return false;

    const SIMDVector discriminantVector = VectorSubtract(VectorMultiply(bVector, bVector), cVector);
    if(Vector4Less(discriminantVector, VectorZero()))
        return false;

    const SIMDVector distance = VectorSubtract(VectorNegate(bVector), VectorSqrt(discriminantVector));
    outDistance = Max(0.0f, VectorGetX(distance));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersects(
    SIMDVector origin,
    SIMDVector direction,
    f32& outDistance
)const noexcept{
    return intersectsRayValue(LoadFloat(centerRadius), origin, direction, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containedByValue(SIMDVector sphereValue, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept{
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromSpherePlaneTests(sphereValue, planes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containedBy(
    SIMDVector plane0,
    SIMDVector plane1,
    SIMDVector plane2,
    SIMDVector plane3,
    SIMDVector plane4,
    SIMDVector plane5
)const noexcept{
    return containedByValue(LoadFloat(centerRadius), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVector SIMDCALL BoundingSphere::mergeSphereValues(SIMDVector sphereValue0, SIMDVector sphereValue1, bool& outDirectCopy, bool& outCopyFirst)noexcept{
    outDirectCopy = false;
    outCopyFirst = true;
    const SIMDVector center0 = CollisionDetail::SphereCenter(sphereValue0);
    const SIMDVector center1 = CollisionDetail::SphereCenter(sphereValue1);
    const SIMDVector radius0 = VectorSplatW(sphereValue0);
    const SIMDVector radius1 = VectorSplatW(sphereValue1);
    const SIMDVector delta = VectorSubtract(center1, center0);
    const SIMDVector distanceSquared = Vector3LengthSq(delta);
    const SIMDVector radiusDelta = VectorSubtract(radius0, radius1);

    if(Vector4GreaterOrEqual(VectorMultiply(radiusDelta, radiusDelta), distanceSquared)){
        outDirectCopy = true;
        outCopyFirst = Vector4GreaterOrEqual(radius0, radius1);
        return outCopyFirst ? sphereValue0 : sphereValue1;
    }

    const SIMDVector distance = VectorSqrt(distanceSquared);
    const SIMDVector newRadius = VectorMultiply(
        VectorAdd(distance, VectorAdd(radius0, radius1)),
        s_SIMDOneHalf
    );
    SIMDVector newCenter = center0;
    if(Vector4Greater(distance, VectorReplicate(CollisionDetail::s_RayEpsilon)))
        newCenter = VectorMultiplyAdd(delta, VectorDivide(VectorSubtract(newRadius, radius0), distance), center0);

    return CollisionDetail::SphereCenterRadius(newCenter, newRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createMerged(
    BoundingSphere& outSphere,
    const BoundingSphere& sphere0,
    const BoundingSphere& sphere1
)noexcept{
    bool directCopy = false;
    bool copyFirst = true;
    const SIMDVector mergedValue = mergeSphereValues(LoadFloat(sphere0.centerRadius), LoadFloat(sphere1.centerRadius), directCopy, copyFirst);
    if(directCopy){
        outSphere = copyFirst ? sphere0 : sphere1;
        return;
    }

    StoreFloat(mergedValue, outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVector SIMDCALL BoundingSphere::sphereFromCenterExtentsValue(SIMDVector centerValue, SIMDVector extentsValue)noexcept{
    return CollisionDetail::SphereCenterRadius(centerValue, Vector3Length(extentsValue));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVector SIMDCALL BoundingSphere::sphereFromCornersValue(const SIMDVector* corners, usize cornerCount)noexcept{
    return CollisionDetail::CreateSphereFromVectorPoints(corners, cornerCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createFromBoundingBox(BoundingSphere& outSphere, const BoundingBox& box)noexcept{
    StoreFloat(sphereFromCenterExtentsValue(LoadFloat(box.center), LoadFloat(box.extents)), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createFromBoundingBox(BoundingSphere& outSphere, const BoundingOrientedBox& box)noexcept{
    StoreFloat(sphereFromCenterExtentsValue(LoadFloat(box.center), LoadFloat(box.extents)), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createFromPoints(
    BoundingSphere& outSphere,
    const usize count,
    const Float3U* points,
    const usize stride
)noexcept{
    GLB_ASSERT(points != nullptr);
    GLB_ASSERT(count > 0u);
    SIMDVector centerVector = VectorZero();
    for(usize i = 0u; i < count; ++i)
        centerVector = VectorAdd(centerVector, LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, i)));
    centerVector = VectorDivide(centerVector, VectorReplicate(static_cast<f32>(count)));

    SIMDVector radiusSq = VectorZero();
    for(usize i = 0u; i < count; ++i){
        const SIMDVector delta = VectorSubtract(LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, i)), centerVector);
        radiusSq = VectorMax(radiusSq, Vector3LengthSq(delta));
    }

    StoreFloat(CollisionDetail::SphereCenterRadius(centerVector, VectorSqrt(radiusSq)), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createFromFrustum(BoundingSphere& outSphere, const BoundingFrustum& frustum)noexcept{
    SIMDVector corners[BoundingFrustum::s_CornerCount];
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
    StoreFloat(sphereFromCornersValue(corners, BoundingFrustum::s_CornerCount), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

