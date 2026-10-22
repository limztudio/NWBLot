// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "matrix.h"
#include "collision_detail_volume.h"
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

    struct alignas(SIMDVectorSingle) LocalBounds{
        SIMDVectorSingle minimum;
        SIMDVectorSingle maximum;
    };
    static_assert(alignof(Expected<LocalBounds>) >= alignof(LocalBounds));
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
        const SIMDVectorSingle flippedSlopes = VectorNegate(VectorSet(bottomSlopeValue, topSlopeValue, 0.0f, 0.0f));
        outTopSlope = VectorGetX(flippedSlopes);
        outBottomSlope = VectorGetY(flippedSlopes);
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

    struct alignas(SIMDVectorDouble) LocalBounds{
        SIMDVectorDouble minimum;
        SIMDVectorDouble maximum;
    };
    static_assert(alignof(Expected<LocalBounds>) >= alignof(LocalBounds));
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
        const SIMDVectorDouble flippedSlopes = VectorDoubleNegate(VectorDoubleSet(bottomSlopeValue, topSlopeValue, 0.0, 0.0));
        outTopSlope = VectorDoubleGetX(flippedSlopes);
        outBottomSlope = VectorDoubleGetY(flippedSlopes);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void NWB_SIMD_CALL BoundingFrustum::CornersValue(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle (&outCorners)[s_CornerCount]
)noexcept{
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, outCorners);
}

inline void NWB_SIMD_CALL BoundingDoubleFrustum::CornersValue(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble (&outCorners)[s_DoubleCornerCount]
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
    return CollisionDetail::ContainmentFromPlaneTests(points, planes);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::ContainsTriangleValue(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2
)noexcept{
    SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount];
    const SIMDVectorDouble points[CollisionDoubleDetail::s_DoubleTriangleVertexCount] = { v0, v1, v2 };
    CollisionDoubleDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDoubleDetail::ContainmentFromPlaneTests(points, planes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingFrustum::IntersectsPlaneValue(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle plane
)noexcept{
    SIMDVectorSingle corners[s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVectorSingle outside{};
    SIMDVectorSingle inside{};
    CollisionDetail::FastIntersectPointsPlane(corners, plane, outside, inside);
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
    CollisionDoubleDetail::FastIntersectPointsPlane(corners, plane, outside, inside);
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
    return CollisionDetail::ContainmentFromPlaneTests(corners, planes);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::ContainedByValue(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble plane0, SIMDVectorDouble plane1,
    SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5
)noexcept{
    SIMDVectorDouble corners[s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    const SIMDVectorDouble planes[CollisionDoubleDetail::s_DoubleFrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDoubleDetail::ContainmentFromPlaneTests(corners, planes);
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
    FrustumTests::Transform(
        LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, matrix, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope, outFrustum.bottomSlope,
        outFrustum.nearPlane, outFrustum.farPlane
    );
    StoreFloat(VectorSetW(originVector, 0.0f), outFrustum.origin);
    StoreFloat(orientationVector, outFrustum.orientation);
}

inline void NWB_SIMD_CALL BoundingFrustum::transform(BoundingFrustum& outFrustum, const f32 scale, const SIMDVectorSingle rotation, const SIMDVectorSingle translation)const noexcept{
    SIMDVectorSingle originVector{};
    SIMDVectorSingle orientationVector{};
    FrustumTests::Transform(
        LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, scale, rotation, translation, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope,
        outFrustum.bottomSlope, outFrustum.nearPlane, outFrustum.farPlane
    );
    StoreFloat(VectorSetW(originVector, 0.0f), outFrustum.origin);
    StoreFloat(orientationVector, outFrustum.orientation);
}

inline void NWB_SIMD_CALL BoundingDoubleFrustum::transform(BoundingDoubleFrustum& outFrustum, const SIMDMatrixDouble& matrix)const noexcept{
    SIMDVectorDouble originVector{};
    SIMDVectorDouble orientationVector{};
    FrustumDoubleTests::Transform(
        LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, matrix, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope, outFrustum.bottomSlope,
        outFrustum.nearPlane, outFrustum.farPlane
    );
    StoreDouble(VectorDoubleSetW(originVector, 0.0), outFrustum.origin);
    StoreDouble(orientationVector, outFrustum.orientation);
}

inline void NWB_SIMD_CALL BoundingDoubleFrustum::transform(BoundingDoubleFrustum& outFrustum, const f64 scale, const SIMDVectorDouble rotation, const SIMDVectorDouble translation)const noexcept{
    SIMDVectorDouble originVector{};
    SIMDVectorDouble orientationVector{};
    FrustumDoubleTests::Transform(
        LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, scale, rotation, translation, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope,
        outFrustum.bottomSlope, outFrustum.nearPlane, outFrustum.farPlane
    );
    StoreDouble(VectorDoubleSetW(originVector, 0.0), outFrustum.origin);
    StoreDouble(orientationVector, outFrustum.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingFrustum::getCorners(Float3U (&corners)[s_CornerCount])const noexcept{
    SIMDVectorSingle cornerVectors[s_CornerCount];
    CollisionDetail::FrustumCorners(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}

template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
inline void BoundingDoubleFrustum::getCorners(PointT (&corners)[s_DoubleCornerCount])const noexcept{
    SIMDVectorDouble cornerVectors[s_DoubleCornerCount];
    CollisionDoubleDetail::FrustumCorners(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, cornerVectors);
    for(u64 i = 0u; i < s_DoubleCornerCount; ++i)
        StoreDouble(VectorDoubleSetW(cornerVectors[i], 0.0), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::contains(const SIMDVectorSingle point)const noexcept{
    return FrustumTests::ContainsPoint(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, point);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::contains(const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2)const noexcept{
    return FrustumTests::ContainsTriangle(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, v0, v1, v2);
}

[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingSphere& sphere)const noexcept{
    return FrustumTests::ContainsSphere(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(sphere.centerRadius));
}

[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingBox& box)const noexcept{
    return FrustumTests::ContainsBox(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents));
}

[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingOrientedBox& box)const noexcept{
    return FrustumTests::ContainsOrientedBox(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}

[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingFrustum& frustum)const noexcept{
    return FrustumTests::ContainsFrustum(
        LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope,
        frustum.nearPlane, frustum.farPlane
    );
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::contains(const SIMDVectorDouble point)const noexcept{
    return FrustumDoubleTests::ContainsPoint(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, point);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::contains(const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2)const noexcept{
    return FrustumDoubleTests::ContainsTriangle(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, v0, v1, v2);
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleFrustum::contains(const BoundingDoubleSphere& sphere)const noexcept{
    return FrustumDoubleTests::ContainsSphere(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(sphere.centerRadius));
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleFrustum::contains(const BoundingDoubleBox& box)const noexcept{
    return FrustumDoubleTests::ContainsBox(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(box.center), LoadDouble(box.extents));
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleFrustum::contains(const BoundingDoubleOrientedBox& box)const noexcept{
    return FrustumDoubleTests::ContainsOrientedBox(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}

[[nodiscard]] inline ContainmentType::Enum BoundingDoubleFrustum::contains(const BoundingDoubleFrustum& frustum)const noexcept{
    return FrustumDoubleTests::ContainsFrustum(
        LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope,
        frustum.nearPlane, frustum.farPlane
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingSphere& sphere)const noexcept{
    return FrustumTests::IntersectsSphere(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(sphere.centerRadius));
}

[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingBox& box)const noexcept{
    return FrustumTests::IntersectsBox(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents));
}

[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingOrientedBox& box)const noexcept{
    return FrustumTests::IntersectsOrientedBox(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}

[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingFrustum& frustum)const noexcept{
    return FrustumTests::IntersectsFrustum(
        LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope,
        frustum.nearPlane, frustum.farPlane
    );
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingFrustum::intersects(const SIMDVectorSingle v0, const SIMDVectorSingle v1, const SIMDVectorSingle v2)const noexcept{
    return contains(v0, v1, v2) != ContainmentType::Disjoint;
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingFrustum::intersects(const SIMDVectorSingle plane)const noexcept{
    return FrustumTests::IntersectsPlane(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane);
}

[[nodiscard]] inline Expected<f32> NWB_SIMD_CALL BoundingFrustum::intersects(const SIMDVectorSingle rayOrigin, const SIMDVectorSingle direction)const noexcept{
    return FrustumTests::IntersectsRay(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, rayOrigin, direction);
}

[[nodiscard]] inline bool BoundingDoubleFrustum::intersects(const BoundingDoubleSphere& sphere)const noexcept{
    return FrustumDoubleTests::IntersectsSphere(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(sphere.centerRadius));
}

[[nodiscard]] inline bool BoundingDoubleFrustum::intersects(const BoundingDoubleBox& box)const noexcept{
    return FrustumDoubleTests::IntersectsBox(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(box.center), LoadDouble(box.extents));
}

[[nodiscard]] inline bool BoundingDoubleFrustum::intersects(const BoundingDoubleOrientedBox& box)const noexcept{
    return FrustumDoubleTests::IntersectsOrientedBox(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(box.center), LoadDouble(box.extents), LoadDouble(box.orientation));
}

[[nodiscard]] inline bool BoundingDoubleFrustum::intersects(const BoundingDoubleFrustum& frustum)const noexcept{
    return FrustumDoubleTests::IntersectsFrustum(
        LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadDouble(frustum.origin), LoadDouble(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope,
        frustum.nearPlane, frustum.farPlane
    );
}

[[nodiscard]] inline bool NWB_SIMD_CALL BoundingDoubleFrustum::intersects(const SIMDVectorDouble v0, const SIMDVectorDouble v1, const SIMDVectorDouble v2)const noexcept{
    return contains(v0, v1, v2) != ContainmentType::Disjoint;
}

[[nodiscard]] inline PlaneIntersectionType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::intersects(const SIMDVectorDouble plane)const noexcept{
    return FrustumDoubleTests::IntersectsPlane(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane);
}

[[nodiscard]] inline Expected<f64> NWB_SIMD_CALL BoundingDoubleFrustum::intersects(const SIMDVectorDouble rayOrigin, const SIMDVectorDouble direction)const noexcept{
    return FrustumDoubleTests::IntersectsRay(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, rayOrigin, direction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingFrustum::containedBy(SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)const noexcept{
    return FrustumTests::ContainedBy(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane0, plane1, plane2, plane3, plane4, plane5);
}

[[nodiscard]] inline ContainmentType::Enum NWB_SIMD_CALL BoundingDoubleFrustum::containedBy(SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)const noexcept{
    return FrustumDoubleTests::ContainedBy(LoadDouble(origin), LoadDouble(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane0, plane1, plane2, plane3, plane4, plane5);
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

