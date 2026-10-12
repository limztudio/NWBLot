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

inline void NWB_SIMD_CALL BoundingOrientedBox::TransformOrientedBoxValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents, SIMDVectorSingle& outOrientation)noexcept{
    outCenter = VectorAdd(Vector3Rotate(VectorScale(boxCenter, scale), rotation), translation);
    outExtents = VectorScale(boxExtents, Abs(scale));
    outOrientation = QuaternionNormalize(QuaternionMultiply(boxOrientation, rotation));
}

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

inline void NWB_SIMD_CALL BoundingDoubleOrientedBox::TransformOrientedBoxValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents, SIMDVectorDouble& outOrientation)noexcept{
    outCenter = VectorDoubleAdd(VectorDouble3Rotate(VectorDoubleScale(boxCenter, scale), rotation), translation);
    outExtents = VectorDoubleScale(boxExtents, Abs(scale));
    outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(boxOrientation, rotation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingOrientedBox::CornersValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle* outCorners)noexcept{
    CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, outCorners);
}

inline void NWB_SIMD_CALL BoundingDoubleOrientedBox::CornersValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble* outCorners)noexcept{
    CollisionDoubleDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, outCorners);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::ContainsPointValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle point)noexcept{
    return CollisionDetail::PointInsideObb(point, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Contains : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::ContainsPointValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble point)noexcept{
    return CollisionDoubleDetail::PointInsideObb(point, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Contains : ContainmentType::Disjoint;
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


[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingOrientedBox::IntersectsRayValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle origin, SIMDVectorSingle direction)noexcept{
    const SIMDVectorSingle localOrigin = CollisionDetail::PointToObbLocal(origin, boxCenter, boxOrientation);
    const SIMDVectorSingle localDirection = Vector3InverseRotate(direction, boxOrientation);
    return CollisionDetail::RayIntersectsMinMax(localOrigin, localDirection, VectorNegate(boxExtents), boxExtents);
}

[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleOrientedBox::IntersectsRayValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble origin, SIMDVectorDouble direction)noexcept{
    const SIMDVectorDouble localOrigin = CollisionDoubleDetail::PointToObbLocal(origin, boxCenter, boxOrientation);
    const SIMDVectorDouble localDirection = VectorDouble3InverseRotate(direction, boxOrientation);
    return CollisionDoubleDetail::RayIntersectsMinMax(localOrigin, localDirection, VectorDoubleNegate(boxExtents), boxExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::ContainedByValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)noexcept{
    SIMDVectorSingle corners[s_CornerCount];
    CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
    const SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::ContainedByValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)noexcept{
    SIMDVectorDouble corners[s_DoubleCornerCount];
    CollisionDoubleDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
    const SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDoubleDetail::ContainmentFromPlaneTests(corners, s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
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


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::containsBoxValues(const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents)const noexcept{
    SIMDVectorSingle corners[BoundingBox::s_CornerCount];
    CollisionDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    if(CollisionDetail::PointsInsideObb(corners, BoundingBox::s_CornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::containsBoxValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents)const noexcept{
    SIMDVectorDouble corners[BoundingDoubleBox::s_DoubleCornerCount];
    CollisionDoubleDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    if(CollisionDoubleDetail::PointsInsideObb(corners, BoundingDoubleBox::s_DoubleCornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::containsOrientedBoxValues(const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation)const noexcept{
    SIMDVectorSingle corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    if(CollisionDetail::PointsInsideObb(corners, BoundingOrientedBox::s_CornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsOrientedBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents, otherBoxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::containsOrientedBoxValues(const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation)const noexcept{
    SIMDVectorDouble corners[BoundingDoubleOrientedBox::s_DoubleCornerCount];
    CollisionDoubleDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    if(CollisionDoubleDetail::PointsInsideObb(corners, BoundingDoubleOrientedBox::s_DoubleCornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsOrientedBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents, otherBoxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
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


inline void BoundingOrientedBox::getCorners(Float3U* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVectorSingle cornerVectors[s_CornerCount];
    CornersValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}

template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
inline void BoundingDoubleOrientedBox::getCorners(PointT* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVectorDouble cornerVectors[s_DoubleCornerCount];
    CornersValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), cornerVectors);
    for(u64 i = 0u; i < s_DoubleCornerCount; ++i)
        StoreDouble(VectorDoubleSetW(cornerVectors[i], 0.0), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::contains(const SIMDVectorSingle point)const noexcept{
    return ContainsPointValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), point);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingOrientedBox::contains(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)const noexcept{
    return ContainsTriangleValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), v0, v1, v2);
}

[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingSphere& sphere)const noexcept{
    return containsSphereValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(sphere.centerRadius));
}

[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingBox& box)const noexcept{
    return containsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents));
}

[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}

[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingFrustum& frustum)const noexcept{
    return containsFrustumValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::contains(const SIMDVectorDouble point)const noexcept{
    return ContainsPointValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), point);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::contains(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)const noexcept{
    return ContainsTriangleValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), v0, v1, v2);
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleOrientedBox::contains(const BoundingDoubleSphere& sphere)const noexcept{
    return containsSphereValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(sphere.centerRadius));
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleOrientedBox::contains(const BoundingDoubleBox& box)const noexcept{
    return containsBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(box.center), LoadDouble(box.extents));
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleOrientedBox::contains(const BoundingDoubleOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleOrientedBox::contains(const BoundingDoubleFrustum& frustum)const noexcept{
    return containsFrustumValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingSphere& sphere)const noexcept{
    return sphere.intersectsOrientedBoxValues(LoadFloat(sphere.centerRadius), LoadFloat(center), LoadFloat(extents), LoadFloat(orientation));
}

[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingBox& box)const noexcept{
    return intersectsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents));
}

[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}

[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingFrustum& frustum)const noexcept{
    return frustum.intersectsOrientedBoxValues(LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation));
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingOrientedBox::intersects(
    const SIMDVectorSingle v0,
    const SIMDVectorSingle v1,
    const SIMDVectorSingle v2
)const noexcept{
    return IntersectsTriangleValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), v0, v1, v2);
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingOrientedBox::intersects(const SIMDVectorSingle plane)const noexcept{
    return IntersectsPlaneValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), plane);
}

[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingOrientedBox::intersects(
    SIMDVectorSingle origin,
    SIMDVectorSingle direction
)const noexcept{
    return IntersectsRayValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), origin, direction);
}

[[nodiscard]] inline bool BoundingDoubleOrientedBox::intersects(const BoundingDoubleSphere& sphere)const noexcept{
    return sphere.intersectsOrientedBoxValues(LoadDouble(sphere.centerRadius), LoadDouble(center), LoadDouble(extents), LoadDouble(orientation));
}

[[nodiscard]] inline bool BoundingDoubleOrientedBox::intersects(const BoundingDoubleBox& box)const noexcept{
    return intersectsBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(box.center), LoadDouble(box.extents));
}

[[nodiscard]] inline bool BoundingDoubleOrientedBox::intersects(const BoundingDoubleOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}

[[nodiscard]] inline bool BoundingDoubleOrientedBox::intersects(const BoundingDoubleFrustum& frustum)const noexcept{
    return frustum.intersectsOrientedBoxValues(LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadDouble(center), LoadDouble(extents), LoadDouble(orientation));
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleOrientedBox::intersects(
    const SIMDVectorDouble v0,
    const SIMDVectorDouble v1,
    const SIMDVectorDouble v2
)const noexcept{
    return IntersectsTriangleValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), v0, v1, v2);
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleOrientedBox::intersects(const SIMDVectorDouble plane)const noexcept{
    return IntersectsPlaneValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), plane);
}

[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleOrientedBox::intersects(
    SIMDVectorDouble origin,
    SIMDVectorDouble direction
)const noexcept{
    return IntersectsRayValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), origin, direction);
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


template<typename OrientedBoxT, typename BoxT>
    requires ((IsSame_V<OrientedBoxT, BoundingOrientedBox> && IsSame_V<BoxT, BoundingBox>)
        || (IsSame_V<OrientedBoxT, BoundingDoubleOrientedBox> && IsSame_V<BoxT, BoundingDoubleBox>))
inline void BoundingOrientedBoxFromBox(OrientedBoxT& outBox, const BoxT& box)noexcept{
    outBox.center = box.center;
    outBox.extents = box.extents;
    outBox.orientation = decltype(outBox.orientation)(0, 0, 0, 1);
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
    BoundingOrientedBoxFromBox(outBox, box);
}

template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
inline void BoundingDoubleOrientedBox::CreateFromPoints(
    BoundingDoubleOrientedBox& outBox,
    const usize count,
    const PointT* points,
    const usize stride
)noexcept{
    BoundingDoubleBox box;
    BoundingDoubleBox::CreateFromPoints(box, count, points, stride);
    BoundingOrientedBoxFromBox(outBox, box);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

