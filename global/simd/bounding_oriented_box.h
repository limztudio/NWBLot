// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "matrix.h"
#include "collision_detail_ray.h"
#include "collision_detail_volume.h"
#include "collision_plane.h"
#include "collision_sdf.h"
#include "collision_aabb.h"
#include "collision_triangle.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bounding_sphere_types.h"
#include "bounding_box_types.h"
#include "bounding_oriented_box_types.h"
#include "bounding_frustum_types.h"
#include "bounding_corners.h"
#include "bounding_frustum_tests.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void FOUNDATION_SIMD_CALL BoundingOrientedBox::TransformOrientedBoxValue(
    SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, const SIMDMatrix& matrix, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents, SIMDVectorSingle& outOrientation, b8& outCollapsedToAxisAligned
)noexcept{
    outCollapsedToAxisAligned = false;
    SIMDMatrix decomposition{};
    if(!MatrixDecompose(matrix, decomposition)){
        SIMDVectorSingle axis0{};
        SIMDVectorSingle axis1{};
        SIMDVectorSingle axis2{};
        CollisionDetail::ObbCornerAxes(boxExtents, boxOrientation, axis0, axis1, axis2);
        SIMDVectorSingle minBounds = Vector3Transform(CollisionDetail::ObbCornerFromAxes(boxCenter, axis0, axis1, axis2, 0u), matrix);
        SIMDVectorSingle maxBounds = minBounds;
        for(u32 i = 1u; i < s_CornerCount; ++i)
            CollisionDetail::ExpandMinMax(Vector3Transform(CollisionDetail::ObbCornerFromAxes(boxCenter, axis0, axis1, axis2, i), matrix), minBounds, maxBounds);

        CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
        outOrientation = s_SIMDIdentityR3;
        outCollapsedToAxisAligned = true;
        return;
    }

    outCenter = Vector3Transform(boxCenter, matrix);
    outOrientation = QuaternionNormalize(QuaternionMultiply(decomposition.v[1], boxOrientation));
    const SIMDVectorSingle projectedAxis0 = Vector3InverseRotate(Vector3TransformNormal(Vector3Rotate(s_SIMDIdentityR0, boxOrientation), matrix), outOrientation);
    const SIMDVectorSingle projectedAxis1 = Vector3InverseRotate(Vector3TransformNormal(Vector3Rotate(s_SIMDIdentityR1, boxOrientation), matrix), outOrientation);
    const SIMDVectorSingle projectedAxis2 = Vector3InverseRotate(Vector3TransformNormal(Vector3Rotate(s_SIMDIdentityR2, boxOrientation), matrix), outOrientation);
    const SIMDVectorSingle yzExtents = VectorAdd(VectorMultiply(VectorAbs(projectedAxis1), VectorSplatY(boxExtents)), VectorMultiply(VectorAbs(projectedAxis2), VectorSplatZ(boxExtents)));
    outExtents = VectorAdd(VectorMultiply(VectorAbs(projectedAxis0), VectorSplatX(boxExtents)), yzExtents);
}

inline void FOUNDATION_SIMD_CALL BoundingOrientedBox::TransformOrientedBoxValue(
    SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents, SIMDVectorSingle& outOrientation
)noexcept{
    outCenter = VectorAdd(Vector3Rotate(VectorScale(boxCenter, scale), rotation), translation);
    outExtents = VectorMultiply(boxExtents, scale < 0.0f ? VectorNegate(VectorReplicate(scale)) : VectorReplicate(scale));
    outOrientation = QuaternionNormalize(QuaternionMultiply(rotation, boxOrientation));
}

inline void FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::TransformOrientedBoxValue(
    SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, const SIMDMatrixDouble& matrix, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents, SIMDVectorDouble& outOrientation, b8& outCollapsedToAxisAligned
)noexcept{
    outCollapsedToAxisAligned = false;
    SIMDMatrixDouble decomposition{};
    if(!MatrixDoubleDecompose(matrix, decomposition)){
        SIMDVectorDouble axis0{};
        SIMDVectorDouble axis1{};
        SIMDVectorDouble axis2{};
        CollisionDoubleDetail::ObbCornerAxes(boxExtents, boxOrientation, axis0, axis1, axis2);
        SIMDVectorDouble minBounds = VectorDouble3Transform(CollisionDoubleDetail::ObbCornerFromAxes(boxCenter, axis0, axis1, axis2, 0u), matrix);
        SIMDVectorDouble maxBounds = minBounds;
        for(u64 i = 1u; i < s_DoubleCornerCount; ++i)
            CollisionDoubleDetail::ExpandMinMax(VectorDouble3Transform(CollisionDoubleDetail::ObbCornerFromAxes(boxCenter, axis0, axis1, axis2, i), matrix), minBounds, maxBounds);

        CollisionDoubleDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
        outOrientation = s_DoubleSIMDIdentityR3;
        outCollapsedToAxisAligned = true;
        return;
    }

    outCenter = VectorDouble3Transform(boxCenter, matrix);
    outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(decomposition.v[1], boxOrientation));
    const SIMDVectorDouble projectedAxis0 = VectorDouble3InverseRotate(VectorDouble3TransformNormal(VectorDouble3Rotate(s_DoubleSIMDIdentityR0, boxOrientation), matrix), outOrientation);
    const SIMDVectorDouble projectedAxis1 = VectorDouble3InverseRotate(VectorDouble3TransformNormal(VectorDouble3Rotate(s_DoubleSIMDIdentityR1, boxOrientation), matrix), outOrientation);
    const SIMDVectorDouble projectedAxis2 = VectorDouble3InverseRotate(VectorDouble3TransformNormal(VectorDouble3Rotate(s_DoubleSIMDIdentityR2, boxOrientation), matrix), outOrientation);
    const SIMDVectorDouble yzExtents = VectorDoubleAdd(VectorDoubleMultiply(VectorDoubleAbs(projectedAxis1), VectorDoubleSplatY(boxExtents)), VectorDoubleMultiply(VectorDoubleAbs(projectedAxis2), VectorDoubleSplatZ(boxExtents)));
    outExtents = VectorDoubleAdd(VectorDoubleMultiply(VectorDoubleAbs(projectedAxis0), VectorDoubleSplatX(boxExtents)), yzExtents);
}

inline void FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::TransformOrientedBoxValue(
    SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents, SIMDVectorDouble& outOrientation
)noexcept{
    outCenter = VectorDoubleAdd(VectorDouble3Rotate(VectorDoubleScale(boxCenter, scale), rotation), translation);
    outExtents = VectorDoubleMultiply(boxExtents, scale < 0.0 ? VectorDoubleNegate(VectorDoubleReplicate(scale)) : VectorDoubleReplicate(scale));
    outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(rotation, boxOrientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingOrientedBox::ContainsPointValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle point)noexcept{
    return CollisionDetail::PointInsideObb(point, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Contains : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::ContainsPointValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble point)noexcept{
    return CollisionDoubleDetail::PointInsideObb(point, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Contains : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingOrientedBox::ContainsTriangleValue(
    SIMDVectorSingle boxCenter,
    SIMDVectorSingle boxExtents,
    SIMDVectorSingle boxOrientation,
    SIMDVectorSingle v0,
    SIMDVectorSingle v1,
    SIMDVectorSingle v2
)noexcept{
    if(
        !CollisionDetail::Vector3AnyTrue(VectorGreater(VectorAbs(CollisionDetail::PointToObbLocal(v0, boxCenter, boxOrientation)), boxExtents))
        && !CollisionDetail::Vector3AnyTrue(VectorGreater(VectorAbs(CollisionDetail::PointToObbLocal(v1, boxCenter, boxOrientation)), boxExtents))
        && !CollisionDetail::Vector3AnyTrue(VectorGreater(VectorAbs(CollisionDetail::PointToObbLocal(v2, boxCenter, boxOrientation)), boxExtents))
    )
        return ContainmentType::Contains;
    return IntersectsTriangleValue(boxCenter, boxExtents, boxOrientation, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::ContainsTriangleValue(
    SIMDVectorDouble boxCenter,
    SIMDVectorDouble boxExtents,
    SIMDVectorDouble boxOrientation,
    SIMDVectorDouble v0,
    SIMDVectorDouble v1,
    SIMDVectorDouble v2
)noexcept{
    if(
        !CollisionDoubleDetail::VectorDouble3AnyTrue(VectorDoubleGreater(VectorDoubleAbs(CollisionDoubleDetail::PointToObbLocal(v0, boxCenter, boxOrientation)), boxExtents))
        && !CollisionDoubleDetail::VectorDouble3AnyTrue(VectorDoubleGreater(VectorDoubleAbs(CollisionDoubleDetail::PointToObbLocal(v1, boxCenter, boxOrientation)), boxExtents))
        && !CollisionDoubleDetail::VectorDouble3AnyTrue(VectorDoubleGreater(VectorDoubleAbs(CollisionDoubleDetail::PointToObbLocal(v2, boxCenter, boxOrientation)), boxExtents))
    )
        return ContainmentType::Contains;
    return IntersectsTriangleValue(boxCenter, boxExtents, boxOrientation, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL BoundingOrientedBox::IntersectsTriangleValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept{
    const SIMDVectorSingle localV0 = CollisionDetail::PointToObbLocal(v0, boxCenter, boxOrientation);
    const SIMDVectorSingle localV1 = CollisionDetail::PointToObbLocal(v1, boxCenter, boxOrientation);
    const SIMDVectorSingle localV2 = CollisionDetail::PointToObbLocal(v2, boxCenter, boxOrientation);
    return CollisionDetail::TriangleAabbOverlap(localV0, localV1, localV2, VectorNegate(boxExtents), boxExtents);
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::IntersectsTriangleValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
    const SIMDVectorDouble localV0 = CollisionDoubleDetail::PointToObbLocal(v0, boxCenter, boxOrientation);
    const SIMDVectorDouble localV1 = CollisionDoubleDetail::PointToObbLocal(v1, boxCenter, boxOrientation);
    const SIMDVectorDouble localV2 = CollisionDoubleDetail::PointToObbLocal(v2, boxCenter, boxOrientation);
    return CollisionDoubleDetail::TriangleAabbOverlap(localV0, localV1, localV2, VectorDoubleNegate(boxExtents), boxExtents);
}

[[nodiscard]] inline PlaneIntersectionType::Enum FOUNDATION_SIMD_CALL BoundingOrientedBox::IntersectsPlaneValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle plane)noexcept{
    SIMDVectorSingle outside{};
    SIMDVectorSingle inside{};
    SIMDVectorSingle axis0{};
    SIMDVectorSingle axis1{};
    SIMDVectorSingle axis2{};
    CollisionDetail::ObbAxes(boxOrientation, axis0, axis1, axis2);
    CollisionDetail::FastIntersectOrientedBoxPlane(boxCenter, boxExtents, axis0, axis1, axis2, plane, outside, inside);
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline PlaneIntersectionType::Enum FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::IntersectsPlaneValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble plane)noexcept{
    SIMDVectorDouble outside{};
    SIMDVectorDouble inside{};
    SIMDVectorDouble axis0{};
    SIMDVectorDouble axis1{};
    SIMDVectorDouble axis2{};
    CollisionDoubleDetail::ObbAxes(boxOrientation, axis0, axis1, axis2);
    CollisionDoubleDetail::FastIntersectOrientedBoxPlane(boxCenter, boxExtents, axis0, axis1, axis2, plane, outside, inside);
    if(CollisionDoubleDetail::VectorDouble4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDoubleDetail::VectorDouble4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline Expected<f32> FOUNDATION_SIMD_CALL BoundingOrientedBox::IntersectsRayValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle origin, SIMDVectorSingle direction)noexcept{
    const SIMDVectorSingle localOrigin = CollisionDetail::PointToObbLocal(origin, boxCenter, boxOrientation);
    const SIMDVectorSingle localDirection = Vector3InverseRotate(direction, boxOrientation);
    return CollisionDetail::RayIntersectsMinMax(localOrigin, localDirection, VectorNegate(boxExtents), boxExtents);
}

[[nodiscard]] inline Expected<f64> FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::IntersectsRayValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble origin, SIMDVectorDouble direction)noexcept{
    const SIMDVectorDouble localOrigin = CollisionDoubleDetail::PointToObbLocal(origin, boxCenter, boxOrientation);
    const SIMDVectorDouble localDirection = VectorDouble3InverseRotate(direction, boxOrientation);
    return CollisionDoubleDetail::RayIntersectsMinMax(localOrigin, localDirection, VectorDoubleNegate(boxExtents), boxExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingOrientedBox::ContainedByValue(
    SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5
)noexcept{
    SIMDVectorSingle axis0{};
    SIMDVectorSingle axis1{};
    SIMDVectorSingle axis2{};
    CollisionDetail::ObbCornerAxes(boxExtents, boxOrientation, axis0, axis1, axis2);
    const auto cornerAt = [&](const usize index)noexcept{ return CollisionDetail::ObbCornerFromAxes(boxCenter, axis0, axis1, axis2, static_cast<u32>(index)); };
    const auto planeAt = [&](const usize index)noexcept{ return CollisionDetail::SelectPlane6(index, plane0, plane1, plane2, plane3, plane4, plane5); };
    return CollisionDetail::ContainmentFromPlaneSeriesTests<s_CornerCount, CollisionDetail::s_FrustumPlaneCount>(cornerAt, planeAt);
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::ContainedByValue(
    SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5
)noexcept{
    SIMDVectorDouble axis0{};
    SIMDVectorDouble axis1{};
    SIMDVectorDouble axis2{};
    CollisionDoubleDetail::ObbCornerAxes(boxExtents, boxOrientation, axis0, axis1, axis2);
    const auto cornerAt = [&](const usize index)noexcept{ return CollisionDoubleDetail::ObbCornerFromAxes(boxCenter, axis0, axis1, axis2, index); };
    const auto planeAt = [&](const usize index)noexcept{ return CollisionDoubleDetail::SelectPlane6(index, plane0, plane1, plane2, plane3, plane4, plane5); };
    return CollisionDoubleDetail::ContainmentFromPlaneSeriesTests<s_DoubleCornerCount, CollisionDoubleDetail::s_DoubleFrustumPlaneCount>(cornerAt, planeAt);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename OrientedBoxT, typename BoxT>
    requires ((IsSame_V<OrientedBoxT, BoundingOrientedBox> && IsSame_V<BoxT, BoundingBox>) || (IsSame_V<OrientedBoxT, BoundingDoubleOrientedBox> && IsSame_V<BoxT, BoundingDoubleBox>))
inline void BoundingOrientedBoxFromBox(OrientedBoxT& outBox, const BoxT& box)noexcept{
    outBox.center = box.center;
    outBox.extents = box.extents;
    outBox.orientation = decltype(outBox.orientation)(0, 0, 0, 1);
}

inline void BoundingOrientedBox::CreateFromPoints(BoundingOrientedBox& outBox, const usize count, const Float3U& points, const usize stride)noexcept{
    BoundingBox box;
    BoundingBox::CreateFromPoints(box, count, points, stride);
    BoundingOrientedBoxFromBox(outBox, box);
}

template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
inline void BoundingDoubleOrientedBox::CreateFromPoints(BoundingDoubleOrientedBox& outBox, const usize count, const PointT& points, const usize stride)noexcept{
    BoundingDoubleBox box;
    BoundingDoubleBox::CreateFromPoints(box, count, points, stride);
    BoundingOrientedBoxFromBox(outBox, box);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingOrientedBox::containsSphereValues(const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle sphereValue)const noexcept{
    const SIMDVectorSingle sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVectorSingle localCenter = CollisionDetail::PointToObbLocal(CollisionDetail::SphereCenter(sphereValue), boxCenter, boxOrientation);
    const SIMDVectorSingle closestPoint = CollisionDetail::ClosestPointOnMinMax(localCenter, VectorNegate(boxExtents), boxExtents);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(Vector3LengthSq(VectorSubtract(closestPoint, localCenter)), VectorMultiply(sphereRadius, sphereRadius))))
        return ContainmentType::Disjoint;

    if(Vector3LessOrEqual(VectorAbs(localCenter), VectorSubtract(boxExtents, sphereRadius)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::containsSphereValues(
    const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble sphereValue
)const noexcept{
    const SIMDVectorDouble sphereRadius = CollisionDoubleDetail::SphereRadius(sphereValue);
    const SIMDVectorDouble localCenter = CollisionDoubleDetail::PointToObbLocal(CollisionDoubleDetail::SphereCenter(sphereValue), boxCenter, boxOrientation);
    const SIMDVectorDouble closestPoint = CollisionDoubleDetail::ClosestPointOnMinMax(localCenter, VectorDoubleNegate(boxExtents), boxExtents);
    if(CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleGreater(VectorDouble3LengthSq(VectorDoubleSubtract(closestPoint, localCenter)), VectorDoubleMultiply(sphereRadius, sphereRadius))))
        return ContainmentType::Disjoint;

    if(VectorDouble3LessOrEqual(VectorDoubleAbs(localCenter), VectorDoubleSubtract(boxExtents, sphereRadius)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingOrientedBox::containsBoxValues(
    const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents
)const noexcept{
    const auto cornerAt = [&](const usize index)noexcept{ return CollisionDetail::AabbCorner(otherBoxCenter, otherBoxExtents, static_cast<u32>(index)); };
    if(CollisionDetail::PointSeriesInsideObb<BoundingBox::s_CornerCount>(cornerAt, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::containsBoxValues(
    const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents
)const noexcept{
    const auto cornerAt = [&](const usize index)noexcept{ return CollisionDoubleDetail::AabbCorner(otherBoxCenter, otherBoxExtents, index); };
    if(CollisionDoubleDetail::PointSeriesInsideObb<BoundingDoubleBox::s_DoubleCornerCount>(cornerAt, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingOrientedBox::containsOrientedBoxValues(
    const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation
)const noexcept{
    SIMDVectorSingle axis0{};
    SIMDVectorSingle axis1{};
    SIMDVectorSingle axis2{};
    CollisionDetail::ObbCornerAxes(otherBoxExtents, otherBoxOrientation, axis0, axis1, axis2);
    const auto cornerAt = [&](const usize index)noexcept{ return CollisionDetail::ObbCornerFromAxes(otherBoxCenter, axis0, axis1, axis2, static_cast<u32>(index)); };
    if(CollisionDetail::PointSeriesInsideObb<BoundingOrientedBox::s_CornerCount>(cornerAt, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsOrientedBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents, otherBoxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::containsOrientedBoxValues(
    const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation
)const noexcept{
    SIMDVectorDouble axis0{};
    SIMDVectorDouble axis1{};
    SIMDVectorDouble axis2{};
    CollisionDoubleDetail::ObbCornerAxes(otherBoxExtents, otherBoxOrientation, axis0, axis1, axis2);
    const auto cornerAt = [&](const usize index)noexcept{ return CollisionDoubleDetail::ObbCornerFromAxes(otherBoxCenter, axis0, axis1, axis2, index); };
    if(CollisionDoubleDetail::PointSeriesInsideObb<BoundingDoubleOrientedBox::s_DoubleCornerCount>(cornerAt, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsOrientedBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents, otherBoxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingOrientedBox::containsFrustumValues(
    const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue,
    const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue
)const noexcept{
    const SIMDVectorSingle slopes = CollisionDetail::FrustumCornerSlopes(rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue);
    const SIMDVectorSingle nearDepth = VectorReplicate(nearPlaneValue);
    const SIMDVectorSingle farDepth = VectorReplicate(farPlaneValue);
    const auto cornerAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumCornerFromSlopes(frustumOrigin, frustumOrientation, slopes, nearDepth, farDepth, static_cast<u32>(index));
    };
    if(CollisionDetail::PointSeriesInsideObb<BoundingFrustum::s_CornerCount>(cornerAt, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDetail::FrustumPlaneSeriesIntersectOrientedBox(planeAt, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::containsFrustumValues(
    const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue,
    const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue
)const noexcept{
    const SIMDVectorDouble slopes = CollisionDoubleDetail::FrustumCornerSlopes(rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue);
    const SIMDVectorDouble nearDepth = VectorDoubleReplicate(nearPlaneValue);
    const SIMDVectorDouble farDepth = VectorDoubleReplicate(farPlaneValue);
    const auto cornerAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumCornerFromSlopes(frustumOrigin, frustumOrientation, slopes, nearDepth, farDepth, index);
    };
    if(CollisionDoubleDetail::PointSeriesInsideObb<BoundingDoubleFrustum::s_DoubleCornerCount>(cornerAt, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDoubleDetail::FrustumPlaneSeriesIntersectOrientedBox(planeAt, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL BoundingOrientedBox::intersectsBoxValues(
    const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents
)const noexcept{
    return CollisionDetail::ObbIntersectsObb(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents, s_SIMDIdentityR3);
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::intersectsBoxValues(
    const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents
)const noexcept{
    return CollisionDoubleDetail::ObbIntersectsObb(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents, s_DoubleSIMDIdentityR3);
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL BoundingOrientedBox::intersectsOrientedBoxValues(
    const SIMDVectorSingle boxCenter, const SIMDVectorSingle boxExtents, const SIMDVectorSingle boxOrientation, const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation
)const noexcept{
    return CollisionDetail::ObbIntersectsObb(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::intersectsOrientedBoxValues(
    const SIMDVectorDouble boxCenter, const SIMDVectorDouble boxExtents, const SIMDVectorDouble boxOrientation, const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation
)const noexcept{
    return CollisionDoubleDetail::ObbIntersectsObb(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void FOUNDATION_SIMD_CALL BoundingOrientedBox::transform(BoundingOrientedBox& outBox, const SIMDMatrix& matrix)const noexcept{
    SIMDVectorSingle centerVector{};
    SIMDVectorSingle extentsVector{};
    SIMDVectorSingle orientationVector{};
    b8 collapsedToAxisAligned = false;
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

inline void FOUNDATION_SIMD_CALL BoundingOrientedBox::transform(BoundingOrientedBox& outBox, const f32 scale, const SIMDVectorSingle rotation, const SIMDVectorSingle translation)const noexcept{
    SIMDVectorSingle centerVector{};
    SIMDVectorSingle extentsVector{};
    SIMDVectorSingle orientationVector{};
    TransformOrientedBoxValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), scale, rotation, translation, centerVector, extentsVector, orientationVector);
    StoreFloat(VectorSetW(centerVector, 0.0f), outBox.center);
    StoreFloat(VectorSetW(extentsVector, 0.0f), outBox.extents);
    StoreFloat(orientationVector, outBox.orientation);
}

inline void FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::transform(BoundingDoubleOrientedBox& outBox, const SIMDMatrixDouble& matrix)const noexcept{
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    SIMDVectorDouble orientationVector{};
    b8 collapsedToAxisAligned = false;
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

inline void FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::transform(BoundingDoubleOrientedBox& outBox, const f64 scale, const SIMDVectorDouble rotation, const SIMDVectorDouble translation)const noexcept{
    SIMDVectorDouble centerVector{};
    SIMDVectorDouble extentsVector{};
    SIMDVectorDouble orientationVector{};
    TransformOrientedBoxValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), scale, rotation, translation, centerVector, extentsVector, orientationVector);
    StoreDouble(VectorDoubleSetW(centerVector, 0.0), outBox.center);
    StoreDouble(VectorDoubleSetW(extentsVector, 0.0), outBox.extents);
    StoreDouble(orientationVector, outBox.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::getCorners(Float3U (&corners)[s_CornerCount])const noexcept{
    const SIMDVectorSingle boxCenter = LoadFloat(center);
    const SIMDVectorSingle boxExtents = LoadFloat(extents);
    const SIMDVectorSingle boxOrientation = LoadFloat(orientation);
    SIMDVectorSingle axis0{};
    SIMDVectorSingle axis1{};
    SIMDVectorSingle axis2{};
    CollisionDetail::ObbCornerAxes(boxExtents, boxOrientation, axis0, axis1, axis2);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(CollisionDetail::ObbCornerFromAxes(boxCenter, axis0, axis1, axis2, i), 0.0f), corners[i]);
}

template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
inline void BoundingDoubleOrientedBox::getCorners(PointT (&corners)[s_DoubleCornerCount])const noexcept{
    const SIMDVectorDouble boxCenter = LoadDouble(center);
    const SIMDVectorDouble boxExtents = LoadDouble(extents);
    const SIMDVectorDouble boxOrientation = LoadDouble(orientation);
    SIMDVectorDouble axis0{};
    SIMDVectorDouble axis1{};
    SIMDVectorDouble axis2{};
    CollisionDoubleDetail::ObbCornerAxes(boxExtents, boxOrientation, axis0, axis1, axis2);
    for(u64 i = 0u; i < s_DoubleCornerCount; ++i)
        StoreDouble(VectorDoubleSetW(CollisionDoubleDetail::ObbCornerFromAxes(boxCenter, axis0, axis1, axis2, i), 0.0), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingOrientedBox::contains(const SIMDVectorSingle point)const noexcept{
    return ContainsPointValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), point);
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingOrientedBox::contains(const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2)const noexcept{
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
    return containsFrustumValues(
        LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane
    );
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::contains(const SIMDVectorDouble point)const noexcept{
    return ContainsPointValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), point);
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::contains(const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2)const noexcept{
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
    return containsFrustumValues(
        LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 BoundingOrientedBox::intersects(const BoundingSphere& sphere)const noexcept{
    return sphere.intersectsOrientedBoxValues(LoadFloat(sphere.centerRadius), LoadFloat(center), LoadFloat(extents), LoadFloat(orientation));
}

[[nodiscard]] inline b8 BoundingOrientedBox::intersects(const BoundingBox& box)const noexcept{
    return intersectsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents));
}

[[nodiscard]] inline b8 BoundingOrientedBox::intersects(const BoundingOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}

[[nodiscard]] inline b8 BoundingOrientedBox::intersects(const BoundingFrustum& frustum)const noexcept{
    return FrustumTests::IntersectsOrientedBox(
        LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation)
    );
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL BoundingOrientedBox::intersects(const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2)const noexcept{
    return IntersectsTriangleValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), v0, v1, v2);
}

[[nodiscard]] inline PlaneIntersectionType::Enum FOUNDATION_SIMD_CALL BoundingOrientedBox::intersects(const SIMDVectorSingle plane)const noexcept{
    return IntersectsPlaneValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), plane);
}

[[nodiscard]] inline Expected<f32> FOUNDATION_SIMD_CALL BoundingOrientedBox::intersects(SIMDVectorSingle origin, SIMDVectorSingle direction)const noexcept{
    return IntersectsRayValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), origin, direction);
}

[[nodiscard]] inline b8 BoundingDoubleOrientedBox::intersects(const BoundingDoubleSphere& sphere)const noexcept{
    return sphere.intersectsOrientedBoxValues(LoadDouble(sphere.centerRadius), LoadDouble(center), LoadDouble(extents), LoadDouble(orientation));
}

[[nodiscard]] inline b8 BoundingDoubleOrientedBox::intersects(const BoundingDoubleBox& box)const noexcept{
    return intersectsBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(box.center), LoadDouble(box.extents));
}

[[nodiscard]] inline b8 BoundingDoubleOrientedBox::intersects(const BoundingDoubleOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}

[[nodiscard]] inline b8 BoundingDoubleOrientedBox::intersects(const BoundingDoubleFrustum& frustum)const noexcept{
    return FrustumDoubleTests::IntersectsOrientedBox(
        LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadDouble(center), LoadDouble(extents), LoadDouble(orientation)
    );
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::intersects(const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2)const noexcept{
    return IntersectsTriangleValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), v0, v1, v2);
}

[[nodiscard]] inline PlaneIntersectionType::Enum FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::intersects(const SIMDVectorDouble plane)const noexcept{
    return IntersectsPlaneValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), plane);
}

[[nodiscard]] inline Expected<f64> FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::intersects(SIMDVectorDouble origin, SIMDVectorDouble direction)const noexcept{
    return IntersectsRayValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), origin, direction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingOrientedBox::containedBy(SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)const noexcept{
    return ContainedByValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), plane0, plane1, plane2, plane3, plane4, plane5);
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL BoundingDoubleOrientedBox::containedBy(SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)const noexcept{
    return ContainedByValue(LoadDouble(center), LoadDouble(extents), LoadDouble(orientation), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

