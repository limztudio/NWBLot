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

