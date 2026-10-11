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


inline void NWB_SIMD_CALL BoundingDoubleSphere::transform(BoundingDoubleSphere& outSphere, const SIMDMatrixDouble& matrix)const noexcept{
    StoreDouble(TransformSphereValue(LoadDouble(centerRadius), matrix), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingDoubleSphere::transform(
    BoundingDoubleSphere& outSphere,
    const f64 scale,
    const SIMDVectorDouble rotation,
    const SIMDVectorDouble translation
)const noexcept{
    StoreDouble(TransformSphereValue(LoadDouble(centerRadius), scale, rotation, translation), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleSphere::ContainsPointValue(SIMDVectorDouble sphereValue, SIMDVectorDouble point)noexcept{
    const SIMDVectorDouble delta = VectorDoubleSubtract(point, CollisionDoubleDetail::SphereCenter(sphereValue));
    const SIMDVectorDouble sphereRadius = CollisionDoubleDetail::SphereRadius(sphereValue);
    return CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleLessOrEqual(VectorDouble3LengthSq(delta), VectorDoubleMultiply(sphereRadius, sphereRadius))) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleSphere::contains(const SIMDVectorDouble point)const noexcept{
    return ContainsPointValue(LoadDouble(centerRadius), point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleSphere::ContainsTriangleValue(
    SIMDVectorDouble sphereValue,
    SIMDVectorDouble v0,
    SIMDVectorDouble v1,
    SIMDVectorDouble v2
)noexcept{
    const SIMDVectorDouble points[CollisionDoubleDetail::s_DoubleTriangleVertexCount] = { v0, v1, v2 };
    if(CollisionDoubleDetail::PointsInsideSphere(points, CollisionDoubleDetail::s_DoubleTriangleVertexCount, CollisionDoubleDetail::SphereCenter(sphereValue), CollisionDoubleDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return IntersectsTriangleValue(sphereValue, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleSphere::contains(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)const noexcept{
    return ContainsTriangleValue(LoadDouble(centerRadius), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleSphere::containsSphereValue(const SIMDVectorDouble sphereValue, const SIMDVectorDouble otherSphereValue)const noexcept{
    const SIMDVectorDouble sphereRadius = VectorDoubleSplatW(sphereValue);
    const SIMDVectorDouble otherRadius = VectorDoubleSplatW(otherSphereValue);
    const SIMDVectorDouble delta = VectorDoubleSubtract(CollisionDoubleDetail::SphereCenter(otherSphereValue), CollisionDoubleDetail::SphereCenter(sphereValue));
    const SIMDVectorDouble distanceSq = VectorDouble3LengthSq(delta);
    if(VectorDouble4GreaterOrEqual(sphereRadius, otherRadius)){
        const SIMDVectorDouble radiusDelta = VectorDoubleSubtract(sphereRadius, otherRadius);
        if(VectorDouble4LessOrEqual(distanceSq, VectorDoubleMultiply(radiusDelta, radiusDelta)))
            return ContainmentType::Contains;
    }

    const SIMDVectorDouble radiusSum = VectorDoubleAdd(sphereRadius, otherRadius);
    if(VectorDouble4GreaterOrEqual(distanceSq, VectorDoubleMultiply(radiusSum, radiusSum)))
        return ContainmentType::Disjoint;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleSphere::contains(const BoundingDoubleSphere& sphere)const noexcept{
    return containsSphereValue(LoadDouble(centerRadius), LoadDouble(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleSphere::containsBoxValues(const SIMDVectorDouble sphereValue, const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents)const noexcept{
    if(!intersectsBoxValues(sphereValue, boxCenter, boxExtents))
        return ContainmentType::Disjoint;

    SIMDVectorDouble corners[BoundingDoubleBox::s_DoubleCornerCount];
    CollisionDoubleDetail::AabbCorners(boxCenter, boxExtents, corners);
    if(CollisionDoubleDetail::PointsInsideSphere(corners, BoundingDoubleBox::s_DoubleCornerCount, CollisionDoubleDetail::SphereCenter(sphereValue), CollisionDoubleDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleSphere::contains(const BoundingDoubleBox& box)const noexcept{
    return containsBoxValues(LoadDouble(centerRadius), LoadDouble(box.center), LoadDouble(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleSphere::containsOrientedBoxValues(const SIMDVectorDouble sphereValue, const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation)const noexcept{
    if(!intersectsOrientedBoxValues(sphereValue, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Disjoint;

    SIMDVectorDouble corners[BoundingDoubleOrientedBox::s_DoubleCornerCount];
    CollisionDoubleDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
    if(CollisionDoubleDetail::PointsInsideSphere(corners, BoundingDoubleOrientedBox::s_DoubleCornerCount, CollisionDoubleDetail::SphereCenter(sphereValue), CollisionDoubleDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleSphere::contains(const BoundingDoubleOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadDouble(centerRadius), LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleSphere::containsFrustumValues(const SIMDVectorDouble sphereValue, const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue)const noexcept{
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
    if(CollisionDoubleDetail::PointsInsideSphere(corners, BoundingDoubleFrustum::s_DoubleCornerCount, CollisionDoubleDetail::SphereCenter(sphereValue), CollisionDoubleDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingDoubleSphere::contains(const BoundingDoubleFrustum& frustum)const noexcept{
    if(!intersects(frustum))
        return ContainmentType::Disjoint;
    return containsFrustumValues(LoadDouble(centerRadius), LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleSphere::intersectsSphereValue(const SIMDVectorDouble sphereValue, const SIMDVectorDouble otherSphereValue)const noexcept{
    const SIMDVectorDouble radiusSum = VectorDoubleAdd(CollisionDoubleDetail::SphereRadius(sphereValue), CollisionDoubleDetail::SphereRadius(otherSphereValue));
    const SIMDVectorDouble delta = VectorDoubleSubtract(CollisionDoubleDetail::SphereCenter(otherSphereValue), CollisionDoubleDetail::SphereCenter(sphereValue));
    return CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleLessOrEqual(VectorDouble3LengthSq(delta), VectorDoubleMultiply(radiusSum, radiusSum)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleSphere::intersects(const BoundingDoubleSphere& sphere)const noexcept{
    return intersectsSphereValue(LoadDouble(centerRadius), LoadDouble(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleSphere::intersectsBoxValues(const SIMDVectorDouble sphereValue, const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents)const noexcept{
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    CollisionDoubleDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    const SIMDVectorDouble centerVector = CollisionDoubleDetail::SphereCenter(sphereValue);
    const SIMDVectorDouble sphereRadius = CollisionDoubleDetail::SphereRadius(sphereValue);
    const SIMDVectorDouble closestPoint = CollisionDoubleDetail::ClosestPointOnMinMax(centerVector, minBounds, maxBounds);
    return CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleLessOrEqual(VectorDouble3LengthSq(VectorDoubleSubtract(closestPoint, centerVector)), VectorDoubleMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleSphere::intersects(const BoundingDoubleBox& box)const noexcept{
    return intersectsBoxValues(LoadDouble(centerRadius), LoadDouble(box.center), LoadDouble(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleSphere::intersectsOrientedBoxValues(const SIMDVectorDouble sphereValue, const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation)const noexcept{
    const SIMDVectorDouble sphereRadius = CollisionDoubleDetail::SphereRadius(sphereValue);
    const SIMDVectorDouble localCenter = CollisionDoubleDetail::PointToObbLocal(CollisionDoubleDetail::SphereCenter(sphereValue), boxCenter, boxOrientation);
    const SIMDVectorDouble closestPoint = CollisionDoubleDetail::ClosestPointOnMinMax(localCenter, VectorDoubleNegate(boxExtents), boxExtents);
    return CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleLessOrEqual(VectorDouble3LengthSq(VectorDoubleSubtract(closestPoint, localCenter)), VectorDoubleMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleSphere::intersects(const BoundingDoubleOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadDouble(centerRadius), LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingDoubleSphere::intersects(const BoundingDoubleFrustum& frustum)const noexcept{
    return frustum.intersectsSphereValues(LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadDouble(centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleSphere::IntersectsTriangleValue(
    SIMDVectorDouble sphereValue,
    SIMDVectorDouble v0,
    SIMDVectorDouble v1,
    SIMDVectorDouble v2
)noexcept{
    const SIMDVectorDouble centerVector = CollisionDoubleDetail::SphereCenter(sphereValue);
    const SIMDVectorDouble sphereRadius = CollisionDoubleDetail::SphereRadius(sphereValue);
    const SIMDVectorDouble closestPoint = CollisionDoubleDetail::ClosestPointOnTriangle(centerVector, v0, v1, v2);
    return CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleLessOrEqual(VectorDouble3LengthSq(VectorDoubleSubtract(closestPoint, centerVector)), VectorDoubleMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleSphere::intersects(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)const noexcept{
    return IntersectsTriangleValue(LoadDouble(centerRadius), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleSphere::IntersectsPlaneValue(SIMDVectorDouble sphereValue, SIMDVectorDouble plane)noexcept{
    const SIMDVectorDouble distance = CollisionDoubleDetail::PlaneDoubleDistance(plane, CollisionDoubleDetail::SphereCenter(sphereValue));
    const SIMDVectorDouble sphereRadius = CollisionDoubleDetail::SphereRadius(sphereValue);
    if(CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleGreater(distance, sphereRadius)))
        return PlaneIntersectionType::Front;
    if(CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleLess(distance, VectorDoubleNegate(sphereRadius))))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleSphere::intersects(const SIMDVectorDouble plane)const noexcept{
    return IntersectsPlaneValue(LoadDouble(centerRadius), plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleSphere::IntersectsRayValue(SIMDVectorDouble sphereValue, SIMDVectorDouble origin, SIMDVectorDouble direction)noexcept{
    const SIMDVectorDouble sphereRadius = CollisionDoubleDetail::SphereRadius(sphereValue);
    const SIMDVectorDouble localOrigin = VectorDoubleSubtract(origin, CollisionDoubleDetail::SphereCenter(sphereValue));
    const SIMDVectorDouble bVector = VectorDouble3Dot(localOrigin, direction);
    const SIMDVectorDouble cVector = VectorDoubleSubtract(VectorDouble3LengthSq(localOrigin), VectorDoubleMultiply(sphereRadius, sphereRadius));
    if(VectorDouble4Greater(cVector, VectorDoubleZero()) && VectorDouble4Greater(bVector, VectorDoubleZero()))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble discriminantVector = VectorDoubleSubtract(VectorDoubleMultiply(bVector, bVector), cVector);
    if(VectorDouble4Less(discriminantVector, VectorDoubleZero()))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble distance = VectorDoubleSubtract(VectorDoubleNegate(bVector), VectorDoubleSqrt(discriminantVector));
    return Max(0.0, VectorDoubleGetX(distance));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleSphere::intersects(
    SIMDVectorDouble origin,
    SIMDVectorDouble direction
)const noexcept{
    return IntersectsRayValue(LoadDouble(centerRadius), origin, direction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleSphere::ContainedByValue(SIMDVectorDouble sphereValue, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)noexcept{
    const SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDoubleDetail::ContainmentFromSpherePlaneTests(sphereValue, planes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleSphere::containedBy(
    SIMDVectorDouble plane0,
    SIMDVectorDouble plane1,
    SIMDVectorDouble plane2,
    SIMDVectorDouble plane3,
    SIMDVectorDouble plane4,
    SIMDVectorDouble plane5
)const noexcept{
    return ContainedByValue(LoadDouble(centerRadius), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorDouble NWB_SIMD_CALL BoundingDoubleSphere::MergeSphereValues(SIMDVectorDouble sphereValue0, SIMDVectorDouble sphereValue1, bool& outDirectCopy, bool& outCopyFirst)noexcept{
    outDirectCopy = false;
    outCopyFirst = true;
    const SIMDVectorDouble center0 = CollisionDoubleDetail::SphereCenter(sphereValue0);
    const SIMDVectorDouble center1 = CollisionDoubleDetail::SphereCenter(sphereValue1);
    const SIMDVectorDouble radius0 = VectorDoubleSplatW(sphereValue0);
    const SIMDVectorDouble radius1 = VectorDoubleSplatW(sphereValue1);
    const SIMDVectorDouble delta = VectorDoubleSubtract(center1, center0);
    const SIMDVectorDouble distanceSquared = VectorDouble3LengthSq(delta);
    const SIMDVectorDouble radiusDelta = VectorDoubleSubtract(radius0, radius1);

    if(VectorDouble4GreaterOrEqual(VectorDoubleMultiply(radiusDelta, radiusDelta), distanceSquared)){
        outDirectCopy = true;
        outCopyFirst = VectorDouble4GreaterOrEqual(radius0, radius1);
        return outCopyFirst ? sphereValue0 : sphereValue1;
    }

    const SIMDVectorDouble distance = VectorDoubleSqrt(distanceSquared);
    const SIMDVectorDouble newRadius = VectorDoubleMultiply(
        VectorDoubleAdd(distance, VectorDoubleAdd(radius0, radius1)),
        s_DoubleSIMDOneHalf
    );
    SIMDVectorDouble newCenter = center0;
    if(VectorDouble4Greater(distance, VectorDoubleReplicate(CollisionDoubleDetail::s_DoubleRayEpsilon)))
        newCenter = VectorDoubleMultiplyAdd(delta, VectorDoubleDivide(VectorDoubleSubtract(newRadius, radius0), distance), center0);

    return CollisionDoubleDetail::SphereCenterRadius(newCenter, newRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingDoubleSphere::CreateMerged(
    BoundingDoubleSphere& outSphere,
    const BoundingDoubleSphere& sphere0,
    const BoundingDoubleSphere& sphere1
)noexcept{
    bool directCopy = false;
    bool copyFirst = true;
    const SIMDVectorDouble mergedValue = MergeSphereValues(LoadDouble(sphere0.centerRadius), LoadDouble(sphere1.centerRadius), directCopy, copyFirst);
    if(directCopy){
        outSphere = copyFirst ? sphere0 : sphere1;
        return;
    }

    StoreDouble(mergedValue, outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorDouble NWB_SIMD_CALL BoundingDoubleSphere::SphereFromCenterExtentsValue(SIMDVectorDouble centerValue, SIMDVectorDouble extentsValue)noexcept{
    return CollisionDoubleDetail::SphereCenterRadius(centerValue, VectorDouble3Length(extentsValue));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorDouble NWB_SIMD_CALL BoundingDoubleSphere::SphereFromCornersValue(const SIMDVectorDouble* corners, usize cornerCount)noexcept{
    return CollisionDoubleDetail::CreateSphereFromVectorPoints(corners, cornerCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingDoubleSphere::CreateFromBoundingBox(BoundingDoubleSphere& outSphere, const BoundingDoubleBox& box)noexcept{
    StoreDouble(SphereFromCenterExtentsValue(LoadDouble(box.center), LoadDouble(box.extents)), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingDoubleSphere::CreateFromBoundingBox(BoundingDoubleSphere& outSphere, const BoundingDoubleOrientedBox& box)noexcept{
    StoreDouble(SphereFromCenterExtentsValue(LoadDouble(box.center), LoadDouble(box.extents)), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
inline void BoundingDoubleSphere::CreateFromPoints(
    BoundingDoubleSphere& outSphere,
    const usize count,
    const PointT* points,
    const usize stride
)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(count > 0u);
    NWB_ASSERT(stride >= sizeof(PointT));
    NWB_ASSERT(stride % alignof(PointT) == 0u);
    SIMDVectorDouble centerVector = VectorDoubleZero();
    for(usize i = 0u; i < count; ++i)
        centerVector = VectorDoubleAdd(centerVector, LoadDouble(*SIMDVectorDoubleDetail::StridePointer(points, stride, i)));
    centerVector = VectorDoubleDivide(centerVector, VectorDoubleReplicate(static_cast<f64>(count)));

    SIMDVectorDouble radiusSq = VectorDoubleZero();
    for(usize i = 0u; i < count; ++i){
        const SIMDVectorDouble delta = VectorDoubleSubtract(LoadDouble(*SIMDVectorDoubleDetail::StridePointer(points, stride, i)), centerVector);
        radiusSq = VectorDoubleMax(radiusSq, VectorDouble3LengthSq(delta));
    }

    StoreDouble(CollisionDoubleDetail::SphereCenterRadius(centerVector, VectorDoubleSqrt(radiusSq)), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingDoubleSphere::CreateFromFrustum(BoundingDoubleSphere& outSphere, const BoundingDoubleFrustum& frustum)noexcept{
    SIMDVectorDouble corners[BoundingDoubleFrustum::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(
        LoadDouble(frustum.origin),
        LoadDouble(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        corners
    );
    StoreDouble(SphereFromCornersValue(corners, BoundingDoubleFrustum::s_DoubleCornerCount), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

