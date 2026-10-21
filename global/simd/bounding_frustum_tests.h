// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "matrix.h"
#include "collision_detail_volume.h"

#include "bounding_shape_decls.h"
#include "bounding_corners.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stateless frustum math shared by the frustum bounding types. Inputs are the loaded origin, orientation, four slopes, and near/far planes.
namespace FrustumTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL Transform(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, const SIMDMatrix& matrix, SIMDVectorSingle& outOrigin,
    SIMDVectorSingle& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane
)noexcept{
    const auto decomposition = MatrixDecompose(matrix);
    const SIMDVectorSingle rotation = decomposition ? decomposition->rotation : s_SIMDIdentityR3;
    outOrigin = Vector3Transform(frustumOrigin, matrix);
    outOrientation = QuaternionNormalize(QuaternionMultiply(rotation, frustumOrientation));

    SIMDVectorSingle corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    for(usize index = 0u; index < BoundingFrustum::s_CornerCount; ++index){
        corners[index] = Vector3Transform(corners[index], matrix);
        if(index == 0u)
            minBounds = maxBounds = corners[index];
        else
            CollisionDetail::ExpandMinMax(corners[index], minBounds, maxBounds);
    }

    struct alignas(SIMDVectorSingle) LocalBounds{
        SIMDVectorSingle minimum;
        SIMDVectorSingle maximum;
    };
    static_assert(alignof(Expected<LocalBounds>) >= alignof(LocalBounds));
    const auto fitted = [&]()noexcept -> Expected<LocalBounds>{
        LocalBounds bounds{};
        for(usize index = 0u; index < BoundingFrustum::s_CornerCount; ++index){
            const SIMDVectorSingle local = Vector3InverseRotate(VectorSubtract(corners[index], outOrigin), outOrientation);
            if(VectorGetZ(local) <= 0.0f)
                return MakeUnexpected(Failure{});
            const SIMDVectorSingle slopesAndDepth = VectorSetZ(VectorDivide(local, VectorSplatZ(local)), VectorGetZ(local));
            if(index == 0u)
                bounds.minimum = bounds.maximum = slopesAndDepth;
            else
                CollisionDetail::ExpandMinMax(slopesAndDepth, bounds.minimum, bounds.maximum);
        }
        return bounds;
    }();
    if(!fitted){
        // A reflected or collapsed cone may cross the chosen forward plane; enclose its transformed corners with a forward axis-aligned cone.
        const SIMDVectorSingle center = VectorScale(VectorAdd(minBounds, maxBounds), CollisionDetail::s_Half);
        const SIMDVectorSingle extents = VectorScale(VectorSubtract(maxBounds, minBounds), CollisionDetail::s_Half);
        const SIMDVectorSingle depth = VectorSubtract(VectorSplatZ(maxBounds), VectorSplatZ(minBounds));
        const SIMDVectorSingle padding = VectorMax(s_SIMDOne, depth);
        outOrigin = VectorSelect(center, VectorSubtract(VectorSplatZ(minBounds), padding), s_SIMDMaskZ);
        outOrientation = s_SIMDIdentityR3;
        const SIMDVectorSingle slopes = VectorDivide(VectorSelect(VectorZero(), extents, s_SIMDMaskXY), padding);
        const SIMDVectorSingle negativeSlopes = VectorNegate(slopes);
        outRightSlope = VectorGetX(slopes);
        outLeftSlope = VectorGetX(negativeSlopes);
        outTopSlope = VectorGetY(slopes);
        outBottomSlope = VectorGetY(negativeSlopes);
        outNearPlane = VectorGetX(padding);
        outFarPlane = VectorGetX(VectorAdd(padding, depth));
        return;
    }

    outRightSlope = VectorGetX(fitted->maximum);
    outLeftSlope = VectorGetX(fitted->minimum);
    outTopSlope = VectorGetY(fitted->maximum);
    outBottomSlope = VectorGetY(fitted->minimum);
    outNearPlane = VectorGetZ(fitted->minimum);
    outFarPlane = VectorGetZ(fitted->maximum);
}

inline void NWB_SIMD_CALL Transform(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, f32 scale, SIMDVectorSingle rotation,
    SIMDVectorSingle translation, SIMDVectorSingle& outOrigin, SIMDVectorSingle& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane
)noexcept{
    outOrigin = VectorAdd(Vector3Rotate(VectorScale(frustumOrigin, scale), rotation), translation);
    outOrientation = QuaternionNormalize(QuaternionMultiply(rotation, frustumOrientation));
    const SIMDVectorSingle scaledPlanes = VectorMultiply(VectorSet(nearPlaneValue, farPlaneValue, 0.0f, 0.0f), VectorAbs(VectorReplicate(scale)));
    outRightSlope = rightSlopeValue;
    outLeftSlope = leftSlopeValue;
    outTopSlope = topSlopeValue;
    outBottomSlope = bottomSlopeValue;
    outNearPlane = VectorGetX(scaledPlanes);
    outFarPlane = VectorGetY(scaledPlanes);
    if(scale < 0.0f){
        outOrientation = QuaternionNormalize(QuaternionMultiply(outOrientation, VectorSet(0.0f, 1.0f, 0.0f, 0.0f)));
        const SIMDVectorSingle flippedSlopes = VectorNegate(VectorSet(bottomSlopeValue, topSlopeValue, 0.0f, 0.0f));
        outTopSlope = VectorGetX(flippedSlopes);
        outBottomSlope = VectorGetY(flippedSlopes);
    }
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainsPoint(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle point
)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    for(const SIMDVectorSingle plane : planes){
        if(CollisionDetail::Vector4AllTrue(VectorLess(CollisionDetail::PlaneDistance(plane, point), VectorZero())))
            return ContainmentType::Disjoint;
    }
    return ContainmentType::Contains;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainsTriangle(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2
)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    const SIMDVectorSingle points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::ContainmentFromPlaneTests(points, CollisionDetail::s_TriangleVertexCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlane(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle plane
)noexcept{
    SIMDVectorSingle corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVectorSingle outside{};
    SIMDVectorSingle inside{};
    CollisionDetail::FastIntersectPointsPlane(NotNull<const SIMDVectorSingle*>(corners), BoundingFrustum::s_CornerCount, plane, outside, inside);
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL IntersectsRay(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle rayOrigin, SIMDVectorSingle direction
)noexcept{
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

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainedBy(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle plane0, SIMDVectorSingle plane1,
    SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5
)noexcept{
    SIMDVectorSingle corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    const SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingFrustum::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainsSphere(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle sphereValue
)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::ContainmentFromSpherePlaneTests(sphereValue, planes);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainsBox(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents
)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle corners[BoundingBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainsOrientedBox(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation
)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingOrientedBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainsFrustum(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherFrustumOrigin, const SIMDVectorSingle otherFrustumOrientation, const f32 otherRightSlope, const f32 otherLeftSlope, const f32 otherTopSlope, const f32 otherBottomSlope, const f32 otherNearPlane, const f32 otherFarPlane
)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::FrustumCorners(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, corners);
    const ContainmentType::Enum containment = CollisionDetail::ContainmentFromPlaneTests(corners, BoundingFrustum::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
    if(containment != ContainmentType::Intersects)
        return containment;

    SIMDVectorSingle otherPlanes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle thisCorners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumPlanes(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, otherPlanes);
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, thisCorners);
    return CollisionDetail::FrustumPlanesIntersectPoints(otherPlanes, NotNull<const SIMDVectorSingle*>(thisCorners), BoundingFrustum::s_CornerCount) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline bool NWB_SIMD_CALL IntersectsSphere(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle sphereValue
)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectSphere(planes, sphereValue);
}

[[nodiscard]] inline bool NWB_SIMD_CALL IntersectsBox(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents
)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectAxisAlignedBox(planes, otherBoxCenter, otherBoxExtents);
}

[[nodiscard]] inline bool NWB_SIMD_CALL IntersectsOrientedBox(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation
)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectOrientedBox(planes, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}

[[nodiscard]] inline bool NWB_SIMD_CALL IntersectsFrustum(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherFrustumOrigin, const SIMDVectorSingle otherFrustumOrientation, const f32 otherRightSlope, const f32 otherLeftSlope, const f32 otherTopSlope, const f32 otherBottomSlope, const f32 otherNearPlane, const f32 otherFarPlane
)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle otherPlanes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::FrustumPlanes(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, otherPlanes);

    SIMDVectorSingle corners[BoundingFrustum::s_CornerCount];
    SIMDVectorSingle otherCorners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    CollisionDetail::FrustumCorners(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, otherCorners);

    return CollisionDetail::FrustumPlanesIntersectPoints(planes, NotNull<const SIMDVectorSingle*>(otherCorners), BoundingFrustum::s_CornerCount) && CollisionDetail::FrustumPlanesIntersectPoints(otherPlanes, NotNull<const SIMDVectorSingle*>(corners), BoundingFrustum::s_CornerCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrustumDoubleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL Transform(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, const SIMDMatrixDouble& matrix, SIMDVectorDouble& outOrigin,
    SIMDVectorDouble& outOrientation, f64& outRightSlope, f64& outLeftSlope, f64& outTopSlope, f64& outBottomSlope, f64& outNearPlane, f64& outFarPlane
)noexcept{
    const auto decomposition = MatrixDoubleDecompose(matrix);
    const SIMDVectorDouble rotation = decomposition ? decomposition->rotation : s_DoubleSIMDIdentityR3;
    outOrigin = VectorDouble3Transform(frustumOrigin, matrix);
    outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(rotation, frustumOrientation));

    SIMDVectorDouble corners[BoundingDoubleFrustum::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    for(usize index = 0u; index < BoundingDoubleFrustum::s_DoubleCornerCount; ++index){
        corners[index] = VectorDouble3Transform(corners[index], matrix);
        if(index == 0u)
            minBounds = maxBounds = corners[index];
        else
            CollisionDoubleDetail::ExpandMinMax(corners[index], minBounds, maxBounds);
    }

    struct alignas(SIMDVectorDouble) LocalBounds{
        SIMDVectorDouble minimum;
        SIMDVectorDouble maximum;
    };
    static_assert(alignof(Expected<LocalBounds>) >= alignof(LocalBounds));
    const auto fitted = [&]()noexcept -> Expected<LocalBounds>{
        LocalBounds bounds{};
        for(usize index = 0u; index < BoundingDoubleFrustum::s_DoubleCornerCount; ++index){
            const SIMDVectorDouble local = VectorDouble3InverseRotate(VectorDoubleSubtract(corners[index], outOrigin), outOrientation);
            if(VectorDoubleGetZ(local) <= 0.0)
                return MakeUnexpected(Failure{});
            const SIMDVectorDouble slopesAndDepth = VectorDoubleSetZ(VectorDoubleDivide(local, VectorDoubleSplatZ(local)), VectorDoubleGetZ(local));
            if(index == 0u)
                bounds.minimum = bounds.maximum = slopesAndDepth;
            else
                CollisionDoubleDetail::ExpandMinMax(slopesAndDepth, bounds.minimum, bounds.maximum);
        }
        return bounds;
    }();
    if(!fitted){
        // A reflected or collapsed cone may cross the chosen forward plane; enclose its transformed corners with a forward axis-aligned cone.
        const SIMDVectorDouble center = VectorDoubleScale(VectorDoubleAdd(minBounds, maxBounds), CollisionDoubleDetail::s_DoubleHalf);
        const SIMDVectorDouble extents = VectorDoubleScale(VectorDoubleSubtract(maxBounds, minBounds), CollisionDoubleDetail::s_DoubleHalf);
        const SIMDVectorDouble depth = VectorDoubleSubtract(VectorDoubleSplatZ(maxBounds), VectorDoubleSplatZ(minBounds));
        const SIMDVectorDouble padding = VectorDoubleMax(s_DoubleSIMDOne, depth);
        outOrigin = VectorDoubleSelect(center, VectorDoubleSubtract(VectorDoubleSplatZ(minBounds), padding), s_DoubleSIMDMaskZ);
        outOrientation = s_DoubleSIMDIdentityR3;
        const SIMDVectorDouble slopes = VectorDoubleDivide(VectorDoubleSelect(VectorDoubleZero(), extents, s_DoubleSIMDMaskXY), padding);
        const SIMDVectorDouble negativeSlopes = VectorDoubleNegate(slopes);
        outRightSlope = VectorDoubleGetX(slopes);
        outLeftSlope = VectorDoubleGetX(negativeSlopes);
        outTopSlope = VectorDoubleGetY(slopes);
        outBottomSlope = VectorDoubleGetY(negativeSlopes);
        outNearPlane = VectorDoubleGetX(padding);
        outFarPlane = VectorDoubleGetX(VectorDoubleAdd(padding, depth));
        return;
    }

    outRightSlope = VectorDoubleGetX(fitted->maximum);
    outLeftSlope = VectorDoubleGetX(fitted->minimum);
    outTopSlope = VectorDoubleGetY(fitted->maximum);
    outBottomSlope = VectorDoubleGetY(fitted->minimum);
    outNearPlane = VectorDoubleGetZ(fitted->minimum);
    outFarPlane = VectorDoubleGetZ(fitted->maximum);
}

inline void NWB_SIMD_CALL Transform(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, f64 scale, SIMDVectorDouble rotation,
    SIMDVectorDouble translation, SIMDVectorDouble& outOrigin, SIMDVectorDouble& outOrientation, f64& outRightSlope, f64& outLeftSlope, f64& outTopSlope, f64& outBottomSlope, f64& outNearPlane, f64& outFarPlane
)noexcept{
    outOrigin = VectorDoubleAdd(VectorDouble3Rotate(VectorDoubleScale(frustumOrigin, scale), rotation), translation);
    outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(rotation, frustumOrientation));
    const SIMDVectorDouble scaledPlanes = VectorDoubleMultiply(VectorDoubleSet(nearPlaneValue, farPlaneValue, 0.0, 0.0), VectorDoubleAbs(VectorDoubleReplicate(scale)));
    outRightSlope = rightSlopeValue;
    outLeftSlope = leftSlopeValue;
    outTopSlope = topSlopeValue;
    outBottomSlope = bottomSlopeValue;
    outNearPlane = VectorDoubleGetX(scaledPlanes);
    outFarPlane = VectorDoubleGetY(scaledPlanes);
    if(scale < 0.0){
        outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(outOrientation, VectorDoubleSet(0.0, 1.0, 0.0, 0.0)));
        const SIMDVectorDouble flippedSlopes = VectorDoubleNegate(VectorDoubleSet(bottomSlopeValue, topSlopeValue, 0.0, 0.0));
        outTopSlope = VectorDoubleGetX(flippedSlopes);
        outBottomSlope = VectorDoubleGetY(flippedSlopes);
    }
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainsPoint(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble point
)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    for(const SIMDVectorDouble plane : planes){
        if(CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleLess(CollisionDoubleDetail::PlaneDoubleDistance(plane, point), VectorDoubleZero())))
            return ContainmentType::Disjoint;
    }
    return ContainmentType::Contains;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainsTriangle(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2
)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    const SIMDVectorDouble points[CollisionDoubleDetail::s_DoubleTriangleVertexCount] = { v0, v1, v2 };
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::ContainmentFromPlaneTests(points, CollisionDoubleDetail::s_DoubleTriangleVertexCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlane(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble plane
)noexcept{
    SIMDVectorDouble corners[BoundingDoubleFrustum::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVectorDouble outside{};
    SIMDVectorDouble inside{};
    CollisionDoubleDetail::FastIntersectPointsPlane(NotNull<const SIMDVectorDouble*>(corners), BoundingDoubleFrustum::s_DoubleCornerCount, plane, outside, inside);
    if(CollisionDoubleDetail::VectorDouble4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDoubleDetail::VectorDouble4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL IntersectsRay(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble rayOrigin, SIMDVectorDouble direction
)noexcept{
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

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainedBy(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble plane0, SIMDVectorDouble plane1,
    SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5
)noexcept{
    SIMDVectorDouble corners[BoundingDoubleFrustum::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    const SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDoubleDetail::ContainmentFromPlaneTests(corners, BoundingDoubleFrustum::s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainsSphere(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble sphereValue
)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::ContainmentFromSpherePlaneTests(sphereValue, planes);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainsBox(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents
)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble corners[BoundingDoubleBox::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDoubleDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    return CollisionDoubleDetail::ContainmentFromPlaneTests(corners, BoundingDoubleBox::s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainsOrientedBox(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation
)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble corners[BoundingDoubleOrientedBox::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDoubleDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    return CollisionDoubleDetail::ContainmentFromPlaneTests(corners, BoundingDoubleOrientedBox::s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL ContainsFrustum(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherFrustumOrigin, const SIMDVectorDouble otherFrustumOrientation, const f64 otherRightSlope, const f64 otherLeftSlope, const f64 otherTopSlope, const f64 otherBottomSlope, const f64 otherNearPlane, const f64 otherFarPlane
)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble corners[BoundingDoubleFrustum::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDoubleDetail::FrustumCorners(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, corners);
    const ContainmentType::Enum containment = CollisionDoubleDetail::ContainmentFromPlaneTests(corners, BoundingDoubleFrustum::s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
    if(containment != ContainmentType::Intersects)
        return containment;

    SIMDVectorDouble otherPlanes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble thisCorners[BoundingDoubleFrustum::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumPlanes(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, otherPlanes);
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, thisCorners);
    return CollisionDoubleDetail::FrustumPlanesIntersectPoints(otherPlanes, NotNull<const SIMDVectorDouble*>(thisCorners), BoundingDoubleFrustum::s_DoubleCornerCount) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline bool NWB_SIMD_CALL IntersectsSphere(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble sphereValue
)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::FrustumPlanesIntersectSphere(planes, sphereValue);
}

[[nodiscard]] inline bool NWB_SIMD_CALL IntersectsBox(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents
)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::FrustumPlanesIntersectAxisAlignedBox(planes, otherBoxCenter, otherBoxExtents);
}

[[nodiscard]] inline bool NWB_SIMD_CALL IntersectsOrientedBox(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation
)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::FrustumPlanesIntersectOrientedBox(planes, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}

[[nodiscard]] inline bool NWB_SIMD_CALL IntersectsFrustum(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherFrustumOrigin, const SIMDVectorDouble otherFrustumOrientation, const f64 otherRightSlope, const f64 otherLeftSlope, const f64 otherTopSlope, const f64 otherBottomSlope, const f64 otherNearPlane, const f64 otherFarPlane
)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble otherPlanes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDoubleDetail::FrustumPlanes(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, otherPlanes);

    SIMDVectorDouble corners[BoundingDoubleFrustum::s_DoubleCornerCount];
    SIMDVectorDouble otherCorners[BoundingDoubleFrustum::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    CollisionDoubleDetail::FrustumCorners(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, otherCorners);

    return CollisionDoubleDetail::FrustumPlanesIntersectPoints(planes, NotNull<const SIMDVectorDouble*>(otherCorners), BoundingDoubleFrustum::s_DoubleCornerCount) && CollisionDoubleDetail::FrustumPlanesIntersectPoints(otherPlanes, NotNull<const SIMDVectorDouble*>(corners), BoundingDoubleFrustum::s_DoubleCornerCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

