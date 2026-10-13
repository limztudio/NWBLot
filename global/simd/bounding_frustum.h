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


inline void NWB_SIMD_CALL BoundingFrustum::TransformFrustumValue(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, const SIMDMatrix& matrix, SIMDVectorSingle& outOrigin,
    SIMDVectorSingle& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane
)noexcept{
    const auto decomposition = MatrixDecompose(matrix);
    const SIMDVectorSingle rotation = decomposition ? decomposition->rotation : s_SIMDIdentityR3;
    outOrigin = Vector3Transform(frustumOrigin, matrix);
    outOrientation = QuaternionNormalize(QuaternionMultiply(rotation, frustumOrientation));

    SIMDVectorSingle corners[s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    for(usize index = 0u; index < s_CornerCount; ++index){
        corners[index] = Vector3Transform(corners[index], matrix);
        if(index == 0u)
            minBounds = maxBounds = corners[index];
        else
            CollisionDetail::ExpandMinMax(corners[index], minBounds, maxBounds);
    }

    struct LocalBounds{
        SIMDVectorSingle minimum;
        SIMDVectorSingle maximum;
    };
    const auto fitted = [&]()noexcept -> Expected<LocalBounds>{
        LocalBounds bounds{};
        for(usize index = 0u; index < s_CornerCount; ++index){
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
        const SIMDVectorSingle center = VectorScale(VectorAdd(minBounds, maxBounds), 0.5f);
        const SIMDVectorSingle extents = VectorScale(VectorSubtract(maxBounds, minBounds), 0.5f);
        const f32 depth = VectorGetZ(maxBounds) - VectorGetZ(minBounds);
        const f32 padding = Max(1.0f, depth);
        outOrigin = VectorSetZ(center, VectorGetZ(minBounds) - padding);
        outOrientation = s_SIMDIdentityR3;
        outRightSlope = VectorGetX(extents) / padding;
        outLeftSlope = -outRightSlope;
        outTopSlope = VectorGetY(extents) / padding;
        outBottomSlope = -outTopSlope;
        outNearPlane = padding;
        outFarPlane = padding + depth;
        return;
    }

    outRightSlope = VectorGetX(fitted->maximum);
    outLeftSlope = VectorGetX(fitted->minimum);
    outTopSlope = VectorGetY(fitted->maximum);
    outBottomSlope = VectorGetY(fitted->minimum);
    outNearPlane = VectorGetZ(fitted->minimum);
    outFarPlane = VectorGetZ(fitted->maximum);
}

inline void NWB_SIMD_CALL BoundingFrustum::TransformFrustumValue(
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
        outTopSlope = -bottomSlopeValue;
        outBottomSlope = -topSlopeValue;
    }
}

inline void NWB_SIMD_CALL BoundingDoubleFrustum::TransformFrustumValue(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, const SIMDMatrixDouble& matrix, SIMDVectorDouble& outOrigin,
    SIMDVectorDouble& outOrientation, f64& outRightSlope, f64& outLeftSlope, f64& outTopSlope, f64& outBottomSlope, f64& outNearPlane, f64& outFarPlane
)noexcept{
    const auto decomposition = MatrixDoubleDecompose(matrix);
    const SIMDVectorDouble rotation = decomposition ? decomposition->rotation : s_DoubleSIMDIdentityR3;
    outOrigin = VectorDouble3Transform(frustumOrigin, matrix);
    outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(rotation, frustumOrientation));

    SIMDVectorDouble corners[s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    for(usize index = 0u; index < s_DoubleCornerCount; ++index){
        corners[index] = VectorDouble3Transform(corners[index], matrix);
        if(index == 0u)
            minBounds = maxBounds = corners[index];
        else
            CollisionDoubleDetail::ExpandMinMax(corners[index], minBounds, maxBounds);
    }

    struct LocalBounds{
        SIMDVectorDouble minimum;
        SIMDVectorDouble maximum;
    };
    const auto fitted = [&]()noexcept -> Expected<LocalBounds>{
        LocalBounds bounds{};
        for(usize index = 0u; index < s_DoubleCornerCount; ++index){
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
        const SIMDVectorDouble center = VectorDoubleScale(VectorDoubleAdd(minBounds, maxBounds), 0.5);
        const SIMDVectorDouble extents = VectorDoubleScale(VectorDoubleSubtract(maxBounds, minBounds), 0.5);
        const f64 depth = VectorDoubleGetZ(maxBounds) - VectorDoubleGetZ(minBounds);
        const f64 padding = Max(1.0, depth);
        outOrigin = VectorDoubleSetZ(center, VectorDoubleGetZ(minBounds) - padding);
        outOrientation = s_DoubleSIMDIdentityR3;
        outRightSlope = VectorDoubleGetX(extents) / padding;
        outLeftSlope = -outRightSlope;
        outTopSlope = VectorDoubleGetY(extents) / padding;
        outBottomSlope = -outTopSlope;
        outNearPlane = padding;
        outFarPlane = padding + depth;
        return;
    }

    outRightSlope = VectorDoubleGetX(fitted->maximum);
    outLeftSlope = VectorDoubleGetX(fitted->minimum);
    outTopSlope = VectorDoubleGetY(fitted->maximum);
    outBottomSlope = VectorDoubleGetY(fitted->minimum);
    outNearPlane = VectorDoubleGetZ(fitted->minimum);
    outFarPlane = VectorDoubleGetZ(fitted->maximum);
}

inline void NWB_SIMD_CALL BoundingDoubleFrustum::TransformFrustumValue(
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
        outTopSlope = -bottomSlopeValue;
        outBottomSlope = -topSlopeValue;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingFrustum::CornersValue(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle* outCorners
)noexcept{
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, outCorners);
}

inline void NWB_SIMD_CALL BoundingDoubleFrustum::CornersValue(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble* outCorners
)noexcept{
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, outCorners);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::ContainsPointValue(
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

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::ContainsPointValue(
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

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::ContainsTriangleValue(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2
)noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    const SIMDVectorSingle points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::ContainmentFromPlaneTests(points, CollisionDetail::s_TriangleVertexCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::ContainsTriangleValue(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2
)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    const SIMDVectorDouble points[CollisionDoubleDetail::s_DoubleTriangleVertexCount] = { v0, v1, v2 };
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::ContainmentFromPlaneTests(points, CollisionDoubleDetail::s_DoubleTriangleVertexCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingFrustum::IntersectsPlaneValue(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle plane
)noexcept{
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

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::IntersectsPlaneValue(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble plane
)noexcept{
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

[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingFrustum::IntersectsRayValue(
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

[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleFrustum::IntersectsRayValue(
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::ContainedByValue(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle plane0, SIMDVectorSingle plane1,
    SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5
)noexcept{
    SIMDVectorSingle corners[s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    const SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::ContainedByValue(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble plane0, SIMDVectorDouble plane1,
    SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5
)noexcept{
    SIMDVectorDouble corners[s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    const SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDoubleDetail::ContainmentFromPlaneTests(corners, s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::containsSphereValues(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle sphereValue
)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::ContainmentFromSpherePlaneTests(sphereValue, planes);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::containsSphereValues(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble sphereValue
)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::ContainmentFromSpherePlaneTests(sphereValue, planes);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::containsBoxValues(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents
)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle corners[BoundingBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::containsBoxValues(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents
)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble corners[BoundingDoubleBox::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDoubleDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    return CollisionDoubleDetail::ContainmentFromPlaneTests(corners, BoundingDoubleBox::s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::containsOrientedBoxValues(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation
)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingOrientedBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::containsOrientedBoxValues(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation
)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble corners[BoundingDoubleOrientedBox::s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDoubleDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    return CollisionDoubleDetail::ContainmentFromPlaneTests(corners, BoundingDoubleOrientedBox::s_DoubleCornerCount, planes, CollisionDoubleDetail::s_DoubleFrustumPlaneCount);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::containsFrustumValues(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherFrustumOrigin, const SIMDVectorSingle otherFrustumOrientation, const f32 otherRightSlope, const f32 otherLeftSlope, const f32 otherTopSlope, const f32 otherBottomSlope, const f32 otherNearPlane, const f32 otherFarPlane
)const noexcept{
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
    return CollisionDetail::FrustumPlanesIntersectPoints(otherPlanes, thisCorners, BoundingFrustum::s_CornerCount) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::containsFrustumValues(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherFrustumOrigin, const SIMDVectorDouble otherFrustumOrientation, const f64 otherRightSlope, const f64 otherLeftSlope, const f64 otherTopSlope, const f64 otherBottomSlope, const f64 otherNearPlane, const f64 otherFarPlane
)const noexcept{
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
    return CollisionDoubleDetail::FrustumPlanesIntersectPoints(otherPlanes, thisCorners, BoundingDoubleFrustum::s_DoubleCornerCount) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool NWB_SIMD_CALL BoundingFrustum::intersectsSphereValues(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle sphereValue
)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectSphere(planes, sphereValue);
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleFrustum::intersectsSphereValues(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble sphereValue
)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::FrustumPlanesIntersectSphere(planes, sphereValue);
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingFrustum::intersectsBoxValues(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents
)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectAxisAlignedBox(planes, otherBoxCenter, otherBoxExtents);
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleFrustum::intersectsBoxValues(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents
)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::FrustumPlanesIntersectAxisAlignedBox(planes, otherBoxCenter, otherBoxExtents);
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingFrustum::intersectsOrientedBoxValues(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation
)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectOrientedBox(planes, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleFrustum::intersectsOrientedBoxValues(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation
)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::FrustumPlanesIntersectOrientedBox(planes, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingFrustum::intersectsFrustumValues(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherFrustumOrigin, const SIMDVectorSingle otherFrustumOrientation, const f32 otherRightSlope, const f32 otherLeftSlope, const f32 otherTopSlope, const f32 otherBottomSlope, const f32 otherNearPlane, const f32 otherFarPlane
)const noexcept{
    SIMDVectorSingle planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVectorSingle otherPlanes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::FrustumPlanes(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, otherPlanes);

    SIMDVectorSingle corners[s_CornerCount];
    SIMDVectorSingle otherCorners[s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    CollisionDetail::FrustumCorners(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, otherCorners);

    return CollisionDetail::FrustumPlanesIntersectPoints(planes, otherCorners, s_CornerCount) && CollisionDetail::FrustumPlanesIntersectPoints(otherPlanes, corners, s_CornerCount);
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleFrustum::intersectsFrustumValues(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherFrustumOrigin, const SIMDVectorDouble otherFrustumOrientation, const f64 otherRightSlope, const f64 otherLeftSlope, const f64 otherTopSlope, const f64 otherBottomSlope, const f64 otherNearPlane, const f64 otherFarPlane
)const noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    SIMDVectorDouble otherPlanes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDoubleDetail::FrustumPlanes(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, otherPlanes);

    SIMDVectorDouble corners[s_DoubleCornerCount];
    SIMDVectorDouble otherCorners[s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    CollisionDoubleDetail::FrustumCorners(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, otherCorners);

    return CollisionDoubleDetail::FrustumPlanesIntersectPoints(planes, otherCorners, s_DoubleCornerCount) && CollisionDoubleDetail::FrustumPlanesIntersectPoints(otherPlanes, corners, s_DoubleCornerCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline BoundingFrustum::BoundingFrustum(const SIMDMatrix& projection, const bool rightHandedCoordinates)noexcept{
    CreateFromMatrix(*this, projection, rightHandedCoordinates);
}

inline BoundingDoubleFrustum::BoundingDoubleFrustum(const SIMDMatrixDouble& projection, const bool rightHandedCoordinates)noexcept{
    CreateFromMatrix(*this, projection, rightHandedCoordinates);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingFrustum::transform(BoundingFrustum& outFrustum, const SIMDMatrix& matrix)const noexcept{
    SIMDVectorSingle originVector{};
    SIMDVectorSingle orientationVector{};
    TransformFrustumValue(
        LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, matrix, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope, outFrustum.bottomSlope,
        outFrustum.nearPlane, outFrustum.farPlane
    );
    StoreFloat(VectorSetW(originVector, 0.0f), outFrustum.origin);
    StoreFloat(orientationVector, outFrustum.orientation);
}

inline void NWB_SIMD_CALL BoundingFrustum::transform(BoundingFrustum& outFrustum, const f32 scale, const SIMDVectorSingle rotation, const SIMDVectorSingle translation)const noexcept{
    SIMDVectorSingle originVector{};
    SIMDVectorSingle orientationVector{};
    TransformFrustumValue(
        LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, scale, rotation, translation, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope,
        outFrustum.bottomSlope, outFrustum.nearPlane, outFrustum.farPlane
    );
    StoreFloat(VectorSetW(originVector, 0.0f), outFrustum.origin);
    StoreFloat(orientationVector, outFrustum.orientation);
}

inline void NWB_SIMD_CALL BoundingDoubleFrustum::transform(BoundingDoubleFrustum& outFrustum, const SIMDMatrixDouble& matrix)const noexcept{
    SIMDVectorDouble originVector{};
    SIMDVectorDouble orientationVector{};
    TransformFrustumValue(
        LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, matrix, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope, outFrustum.bottomSlope,
        outFrustum.nearPlane, outFrustum.farPlane
    );
    StoreDouble(VectorDoubleSetW(originVector, 0.0), outFrustum.origin);
    StoreDouble(orientationVector, outFrustum.orientation);
}

inline void NWB_SIMD_CALL BoundingDoubleFrustum::transform(BoundingDoubleFrustum& outFrustum, const f64 scale, const SIMDVectorDouble rotation, const SIMDVectorDouble translation)const noexcept{
    SIMDVectorDouble originVector{};
    SIMDVectorDouble orientationVector{};
    TransformFrustumValue(
        LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, scale, rotation, translation, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope,
        outFrustum.bottomSlope, outFrustum.nearPlane, outFrustum.farPlane
    );
    StoreDouble(VectorDoubleSetW(originVector, 0.0), outFrustum.origin);
    StoreDouble(orientationVector, outFrustum.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingFrustum::getCorners(Float3U* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVectorSingle cornerVectors[s_CornerCount];
    CornersValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}

template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
inline void BoundingDoubleFrustum::getCorners(PointT* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVectorDouble cornerVectors[s_DoubleCornerCount];
    CornersValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, cornerVectors);
    for(u64 i = 0u; i < s_DoubleCornerCount; ++i)
        StoreDouble(VectorDoubleSetW(cornerVectors[i], 0.0), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::contains(const SIMDVectorSingle point)const noexcept{
    return ContainsPointValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, point);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::contains(const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2)const noexcept{
    return ContainsTriangleValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, v0, v1, v2);
}

[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingSphere& sphere)const noexcept{
    return containsSphereValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(sphere.centerRadius));
}

[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingBox& box)const noexcept{
    return containsBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents));
}

[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}

[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingFrustum& frustum)const noexcept{
    return containsFrustumValues(
        LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope,
        frustum.nearPlane, frustum.farPlane
    );
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::contains(const SIMDVectorDouble point)const noexcept{
    return ContainsPointValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, point);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::contains(const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2)const noexcept{
    return ContainsTriangleValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, v0, v1, v2);
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleFrustum::contains(const BoundingDoubleSphere& sphere)const noexcept{
    return containsSphereValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(sphere.centerRadius));
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleFrustum::contains(const BoundingDoubleBox& box)const noexcept{
    return containsBoxValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(box.center), LoadDouble(box.extents));
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleFrustum::contains(const BoundingDoubleOrientedBox& box)const noexcept{
    return containsOrientedBoxValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleFrustum::contains(const BoundingDoubleFrustum& frustum)const noexcept{
    return containsFrustumValues(
        LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope,
        frustum.nearPlane, frustum.farPlane
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingSphere& sphere)const noexcept{
    return intersectsSphereValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(sphere.centerRadius));
}

[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingBox& box)const noexcept{
    return intersectsBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents));
}

[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}

[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingFrustum& frustum)const noexcept{
    return intersectsFrustumValues(
        LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope,
        frustum.nearPlane, frustum.farPlane
    );
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingFrustum::intersects(const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2)const noexcept{
    return contains(v0, v1, v2) != ContainmentType::Disjoint;
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingFrustum::intersects(const SIMDVectorSingle plane)const noexcept{
    return IntersectsPlaneValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane);
}

[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingFrustum::intersects(const SIMDVectorSingle rayOrigin, const SIMDVectorSingle direction)const noexcept{
    return IntersectsRayValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, rayOrigin, direction);
}

[[nodiscard]] inline bool BoundingDoubleFrustum::intersects(const BoundingDoubleSphere& sphere)const noexcept{
    return intersectsSphereValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(sphere.centerRadius));
}

[[nodiscard]] inline bool BoundingDoubleFrustum::intersects(const BoundingDoubleBox& box)const noexcept{
    return intersectsBoxValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(box.center), LoadDouble(box.extents));
}

[[nodiscard]] inline bool BoundingDoubleFrustum::intersects(const BoundingDoubleOrientedBox& box)const noexcept{
    return intersectsOrientedBoxValues(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}

[[nodiscard]] inline bool BoundingDoubleFrustum::intersects(const BoundingDoubleFrustum& frustum)const noexcept{
    return intersectsFrustumValues(
        LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope,
        frustum.nearPlane, frustum.farPlane
    );
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleFrustum::intersects(const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2)const noexcept{
    return contains(v0, v1, v2) != ContainmentType::Disjoint;
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::intersects(const SIMDVectorDouble plane)const noexcept{
    return IntersectsPlaneValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane);
}

[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleFrustum::intersects(const SIMDVectorDouble rayOrigin, const SIMDVectorDouble direction)const noexcept{
    return IntersectsRayValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, rayOrigin, direction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::containedBy(SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)const noexcept{
    return ContainedByValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane0, plane1, plane2, plane3, plane4, plane5);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::containedBy(SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)const noexcept{
    return ContainedByValue(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingFrustum::getPlanes(SIMDVectorSingle* nearPlaneOut, SIMDVectorSingle* farPlaneOut, SIMDVectorSingle* rightPlaneOut, SIMDVectorSingle* leftPlaneOut, SIMDVectorSingle* topPlaneOut, SIMDVectorSingle* bottomPlaneOut)const noexcept{
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

inline void BoundingDoubleFrustum::getPlanes(SIMDVectorDouble* nearPlaneOut, SIMDVectorDouble* farPlaneOut, SIMDVectorDouble* rightPlaneOut, SIMDVectorDouble* leftPlaneOut, SIMDVectorDouble* topPlaneOut, SIMDVectorDouble* bottomPlaneOut)const noexcept{
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


inline void NWB_SIMD_CALL BoundingFrustum::CreateFromMatrix(BoundingFrustum& outFrustum, const SIMDMatrix& projection, const bool rightHandedCoordinates)noexcept{
    outFrustum.origin = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    outFrustum.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    const SIMDVectorSingle m00 = VectorSplatX(projection.v[0]);
    const SIMDVectorSingle m11 = VectorSplatY(projection.v[1]);
    const SIMDVectorSingle m02 = VectorSplatZ(projection.v[0]);
    const SIMDVectorSingle m12 = VectorSplatZ(projection.v[1]);
    const SIMDVectorSingle slopes = VectorDivide(VectorMergeX(VectorSubtract(s_SIMDOne, m02), VectorSubtract(s_SIMDNegativeOne, m02), VectorSubtract(s_SIMDOne, m12), VectorSubtract(s_SIMDNegativeOne, m12)), VectorMergeX(m00, m00, m11, m11));
    outFrustum.rightSlope = VectorGetX(slopes);
    outFrustum.leftSlope = VectorGetY(slopes);
    outFrustum.topSlope = VectorGetZ(slopes);
    outFrustum.bottomSlope = VectorGetW(slopes);

    const SIMDVectorSingle m22 = VectorSplatZ(projection.v[2]);
    const SIMDVectorSingle m23 = VectorSplatW(projection.v[2]);
    const SIMDVectorSingle planeDistances = rightHandedCoordinates
        ? VectorDivide(VectorMergeX(m23, m23, VectorZero(), VectorZero()), VectorMergeX(m22, VectorAdd(m22, s_SIMDOne), s_SIMDOne, s_SIMDOne))
        : VectorDivide(VectorMergeX(VectorNegate(m23), m23, VectorZero(), VectorZero()), VectorMergeX(m22, VectorSubtract(s_SIMDOne, m22), s_SIMDOne, s_SIMDOne))
    ;
    outFrustum.nearPlane = VectorGetX(planeDistances);
    outFrustum.farPlane = VectorGetY(planeDistances);
}

inline void NWB_SIMD_CALL BoundingDoubleFrustum::CreateFromMatrix(BoundingDoubleFrustum& outFrustum, const SIMDMatrixDouble& projection, const bool rightHandedCoordinates)noexcept{
    outFrustum.origin = Double4(0.0, 0.0, 0.0, 0.0);
    outFrustum.orientation = Double4(0.0, 0.0, 0.0, 1.0);

    const SIMDVectorDouble m00 = VectorDoubleSplatX(projection.v[0]);
    const SIMDVectorDouble m11 = VectorDoubleSplatY(projection.v[1]);
    const SIMDVectorDouble m02 = VectorDoubleSplatZ(projection.v[0]);
    const SIMDVectorDouble m12 = VectorDoubleSplatZ(projection.v[1]);
    const SIMDVectorDouble slopes = VectorDoubleDivide(
        VectorDoubleMergeX(VectorDoubleSubtract(s_DoubleSIMDOne, m02), VectorDoubleSubtract(s_DoubleSIMDNegativeOne, m02), VectorDoubleSubtract(s_DoubleSIMDOne, m12), VectorDoubleSubtract(s_DoubleSIMDNegativeOne, m12)),
        VectorDoubleMergeX(m00, m00, m11, m11)
    );
    outFrustum.rightSlope = VectorDoubleGetX(slopes);
    outFrustum.leftSlope = VectorDoubleGetY(slopes);
    outFrustum.topSlope = VectorDoubleGetZ(slopes);
    outFrustum.bottomSlope = VectorDoubleGetW(slopes);

    const SIMDVectorDouble m22 = VectorDoubleSplatZ(projection.v[2]);
    const SIMDVectorDouble m23 = VectorDoubleSplatW(projection.v[2]);
    const SIMDVectorDouble planeDistances = rightHandedCoordinates
        ? VectorDoubleDivide(VectorDoubleMergeX(m23, m23, VectorDoubleZero(), VectorDoubleZero()), VectorDoubleMergeX(m22, VectorDoubleAdd(m22, s_DoubleSIMDOne), s_DoubleSIMDOne, s_DoubleSIMDOne))
        : VectorDoubleDivide(VectorDoubleMergeX(VectorDoubleNegate(m23), m23, VectorDoubleZero(), VectorDoubleZero()), VectorDoubleMergeX(m22, VectorDoubleSubtract(s_DoubleSIMDOne, m22), s_DoubleSIMDOne, s_DoubleSIMDOne))
    ;
    outFrustum.nearPlane = VectorDoubleGetX(planeDistances);
    outFrustum.farPlane = VectorDoubleGetY(planeDistances);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

