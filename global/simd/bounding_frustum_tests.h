// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "matrix.h"
#include "collision_detail_volume.h"

#include "bounding_sphere_types.h"
#include "bounding_box_types.h"
#include "bounding_oriented_box_types.h"
#include "bounding_frustum_types.h"
#include "bounding_corners.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrustumTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void FOUNDATION_SIMD_CALL Transform(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, const SIMDMatrix& matrix, SIMDVectorSingle& outOrigin,
    SIMDVectorSingle& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane
)noexcept{
    SIMDMatrix decomposition{};
    const SIMDVectorSingle rotation = MatrixDecompose(matrix, decomposition) ? decomposition.v[1] : s_SIMDIdentityR3;
    outOrigin = Vector3Transform(frustumOrigin, matrix);
    outOrientation = QuaternionNormalize(QuaternionMultiply(rotation, frustumOrientation));

    const SIMDVectorSingle slopes = CollisionDetail::FrustumCornerSlopes(rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue);
    const SIMDVectorSingle nearDepth = VectorReplicate(nearPlaneValue);
    const SIMDVectorSingle farDepth = VectorReplicate(farPlaneValue);
    SIMDVectorSingle minBounds{};
    SIMDVectorSingle maxBounds{};
    SIMDVectorSingle fittedMin{};
    SIMDVectorSingle fittedMax{};
    b8 hasValidDepth = true;
    for(u32 index = 0u; index < BoundingFrustum::s_CornerCount; ++index){
        const SIMDVectorSingle corner = Vector3Transform(
            CollisionDetail::FrustumCornerFromSlopes(frustumOrigin, frustumOrientation, slopes, nearDepth, farDepth, index),
            matrix
        );
        if(index == 0u)
            minBounds = maxBounds = corner;
        else
            CollisionDetail::ExpandMinMax(corner, minBounds, maxBounds);

        if(hasValidDepth){
            const SIMDVectorSingle local = Vector3InverseRotate(VectorSubtract(corner, outOrigin), outOrientation);
            if(VectorGetZ(local) <= 0.0f)
                hasValidDepth = false;
            else{
                const SIMDVectorSingle slopesAndDepth = VectorSetZ(VectorDivide(local, VectorSplatZ(local)), VectorGetZ(local));
                if(index == 0u)
                    fittedMin = fittedMax = slopesAndDepth;
                else
                    CollisionDetail::ExpandMinMax(slopesAndDepth, fittedMin, fittedMax);
            }
        }
    }
    if(!hasValidDepth){
        const SIMDVectorSingle center = VectorScale(VectorAdd(minBounds, maxBounds), CollisionDetail::s_Half);
        const SIMDVectorSingle extents = VectorScale(VectorSubtract(maxBounds, minBounds), CollisionDetail::s_Half);
        const SIMDVectorSingle depth = VectorSubtract(VectorSplatZ(maxBounds), VectorSplatZ(minBounds));
        const SIMDVectorSingle padding = VectorMax(s_SIMDOne, depth);
        outOrigin = VectorSelect(center, VectorSubtract(VectorSplatZ(minBounds), padding), s_SIMDMaskZ);
        outOrientation = s_SIMDIdentityR3;
        const SIMDVectorSingle fittedSlopes = VectorDivide(VectorSelect(VectorZero(), extents, s_SIMDMaskXY), padding);
        const SIMDVectorSingle negativeSlopes = VectorNegate(fittedSlopes);
        outRightSlope = VectorGetX(fittedSlopes);
        outLeftSlope = VectorGetX(negativeSlopes);
        outTopSlope = VectorGetY(fittedSlopes);
        outBottomSlope = VectorGetY(negativeSlopes);
        outNearPlane = VectorGetX(padding);
        outFarPlane = VectorGetX(VectorAdd(padding, depth));
        return;
    }

    outRightSlope = VectorGetX(fittedMax);
    outLeftSlope = VectorGetX(fittedMin);
    outTopSlope = VectorGetY(fittedMax);
    outBottomSlope = VectorGetY(fittedMin);
    outNearPlane = VectorGetZ(fittedMin);
    outFarPlane = VectorGetZ(fittedMax);
}

inline void FOUNDATION_SIMD_CALL Transform(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, f32 scale, SIMDVectorSingle rotation,
    SIMDVectorSingle translation, SIMDVectorSingle& outOrigin, SIMDVectorSingle& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane
)noexcept{
    outOrigin = VectorAdd(Vector3Rotate(VectorScale(frustumOrigin, scale), rotation), translation);
    outOrientation = QuaternionNormalize(QuaternionMultiply(rotation, frustumOrientation));
    const SIMDVectorSingle scaledPlanes = VectorMultiply(VectorSet(nearPlaneValue, farPlaneValue, nearPlaneValue, farPlaneValue), VectorAbs(VectorReplicate(scale)));
    outRightSlope = rightSlopeValue;
    outLeftSlope = leftSlopeValue;
    outTopSlope = topSlopeValue;
    outBottomSlope = bottomSlopeValue;
    outNearPlane = VectorGetX(scaledPlanes);
    outFarPlane = VectorGetY(scaledPlanes);
    if(scale < 0.0f){
        outOrientation = QuaternionNormalize(QuaternionMultiply(outOrientation, VectorSet(0.0f, 1.0f, 0.0f, 0.0f)));
        const SIMDVectorSingle negatedSlopes = VectorNegate(VectorSet(bottomSlopeValue, topSlopeValue, bottomSlopeValue, topSlopeValue));
        outTopSlope = VectorGetX(negatedSlopes);
        outBottomSlope = VectorGetY(negatedSlopes);
    }
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainsPoint(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle point
)noexcept{
    for(usize planeIndex = 0u; planeIndex < CollisionDetail::s_FrustumPlaneCount; ++planeIndex){
        const SIMDVectorSingle plane = CollisionDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planeIndex);
        if(CollisionDetail::Vector4AllTrue(VectorLess(CollisionDetail::PlaneDistance(plane, point), VectorZero())))
            return ContainmentType::Disjoint;
    }
    return ContainmentType::Contains;
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainsTriangle(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2
)noexcept{
    const auto pointAt = [&](const usize index)noexcept{ return CollisionDetail::SelectVector3(index, v0, v1, v2); };
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDetail::ContainmentFromPlaneSeriesTests<CollisionDetail::s_TriangleVertexCount, CollisionDetail::s_FrustumPlaneCount>(pointAt, planeAt);
}

[[nodiscard]] inline PlaneIntersectionType::Enum FOUNDATION_SIMD_CALL IntersectsPlane(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle plane
)noexcept{
    const SIMDVectorSingle slopes = CollisionDetail::FrustumCornerSlopes(rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue);
    const SIMDVectorSingle nearDepth = VectorReplicate(nearPlaneValue);
    const SIMDVectorSingle farDepth = VectorReplicate(farPlaneValue);
    const auto cornerAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumCornerFromSlopes(frustumOrigin, frustumOrientation, slopes, nearDepth, farDepth, static_cast<u32>(index));
    };
    SIMDVectorSingle outside{};
    SIMDVectorSingle inside{};
    CollisionDetail::FastIntersectPointSeriesPlane<BoundingFrustum::s_CornerCount>(cornerAt, plane, outside, inside);
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline Expected<f32> FOUNDATION_SIMD_CALL IntersectsRay(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle rayOrigin, SIMDVectorSingle direction
)noexcept{
    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle rayEpsilon = VectorReplicate(CollisionDetail::s_RayEpsilon);
    SIMDVectorSingle tMin = zero;
    SIMDVectorSingle tMax = VectorReplicate(s_MaxF32);
    for(usize planeIndex = 0u; planeIndex < CollisionDetail::s_FrustumPlaneCount; ++planeIndex){
        const SIMDVectorSingle plane = CollisionDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planeIndex);
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

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainedBy(
    SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle plane0, SIMDVectorSingle plane1,
    SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5
)noexcept{
    const SIMDVectorSingle slopes = CollisionDetail::FrustumCornerSlopes(rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue);
    const SIMDVectorSingle nearDepth = VectorReplicate(nearPlaneValue);
    const SIMDVectorSingle farDepth = VectorReplicate(farPlaneValue);
    const auto cornerAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumCornerFromSlopes(frustumOrigin, frustumOrientation, slopes, nearDepth, farDepth, static_cast<u32>(index));
    };
    const auto planeAt = [&](const usize index)noexcept{ return CollisionDetail::SelectPlane6(index, plane0, plane1, plane2, plane3, plane4, plane5); };
    return CollisionDetail::ContainmentFromPlaneSeriesTests<BoundingFrustum::s_CornerCount, CollisionDetail::s_FrustumPlaneCount>(cornerAt, planeAt);
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainsSphere(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle sphereValue
)noexcept{
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDetail::ContainmentFromSpherePlaneSeries(sphereValue, planeAt);
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainsBox(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents
)noexcept{
    const auto cornerAt = [&](const usize index)noexcept{ return CollisionDetail::AabbCorner(otherBoxCenter, otherBoxExtents, static_cast<u32>(index)); };
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDetail::ContainmentFromPlaneSeriesTests<BoundingBox::s_CornerCount, CollisionDetail::s_FrustumPlaneCount>(cornerAt, planeAt);
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainsOrientedBox(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation
)noexcept{
    SIMDVectorSingle axis0{};
    SIMDVectorSingle axis1{};
    SIMDVectorSingle axis2{};
    CollisionDetail::ObbCornerAxes(otherBoxExtents, otherBoxOrientation, axis0, axis1, axis2);
    const auto cornerAt = [&](const usize index)noexcept{ return CollisionDetail::ObbCornerFromAxes(otherBoxCenter, axis0, axis1, axis2, static_cast<u32>(index)); };
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDetail::ContainmentFromPlaneSeriesTests<BoundingOrientedBox::s_CornerCount, CollisionDetail::s_FrustumPlaneCount>(cornerAt, planeAt);
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainsFrustum(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherFrustumOrigin, const SIMDVectorSingle otherFrustumOrientation, const f32 otherRightSlope, const f32 otherLeftSlope, const f32 otherTopSlope, const f32 otherBottomSlope, const f32 otherNearPlane, const f32 otherFarPlane
)noexcept{
    const SIMDVectorSingle otherSlopes = CollisionDetail::FrustumCornerSlopes(otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope);
    const SIMDVectorSingle otherNearDepth = VectorReplicate(otherNearPlane);
    const SIMDVectorSingle otherFarDepth = VectorReplicate(otherFarPlane);
    const auto otherCornerAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumCornerFromSlopes(otherFrustumOrigin, otherFrustumOrientation, otherSlopes, otherNearDepth, otherFarDepth, static_cast<u32>(index));
    };
    const auto thisPlaneAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    const ContainmentType::Enum containment = CollisionDetail::ContainmentFromPlaneSeriesTests<BoundingFrustum::s_CornerCount, CollisionDetail::s_FrustumPlaneCount>(otherCornerAt, thisPlaneAt);
    if(containment != ContainmentType::Intersects)
        return containment;

    const SIMDVectorSingle thisSlopes = CollisionDetail::FrustumCornerSlopes(rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue);
    const SIMDVectorSingle thisNearDepth = VectorReplicate(nearPlaneValue);
    const SIMDVectorSingle thisFarDepth = VectorReplicate(farPlaneValue);
    const auto thisCornerAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumCornerFromSlopes(frustumOrigin, frustumOrientation, thisSlopes, thisNearDepth, thisFarDepth, static_cast<u32>(index));
    };
    const auto otherPlaneAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumPlane(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, index);
    };
    return CollisionDetail::FrustumPlaneSeriesIntersectPointSeries<BoundingFrustum::s_CornerCount>(otherPlaneAt, thisCornerAt) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL IntersectsSphere(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle sphereValue
)noexcept{
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDetail::FrustumPlaneSeriesIntersectSphere(planeAt, sphereValue);
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL IntersectsBox(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents
)noexcept{
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDetail::FrustumPlaneSeriesIntersectAxisAlignedBox(planeAt, otherBoxCenter, otherBoxExtents);
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL IntersectsOrientedBox(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherBoxCenter, const SIMDVectorSingle otherBoxExtents, const SIMDVectorSingle otherBoxOrientation
)noexcept{
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDetail::FrustumPlaneSeriesIntersectOrientedBox(planeAt, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL IntersectsFrustum(
    const SIMDVectorSingle frustumOrigin, const SIMDVectorSingle frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue,
    const SIMDVectorSingle otherFrustumOrigin, const SIMDVectorSingle otherFrustumOrientation, const f32 otherRightSlope, const f32 otherLeftSlope, const f32 otherTopSlope, const f32 otherBottomSlope, const f32 otherNearPlane, const f32 otherFarPlane
)noexcept{
    const auto thisPlaneAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    const auto otherPlaneAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumPlane(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, index);
    };

    const SIMDVectorSingle thisSlopes = CollisionDetail::FrustumCornerSlopes(rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue);
    const SIMDVectorSingle thisNearDepth = VectorReplicate(nearPlaneValue);
    const SIMDVectorSingle thisFarDepth = VectorReplicate(farPlaneValue);
    const auto thisCornerAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumCornerFromSlopes(frustumOrigin, frustumOrientation, thisSlopes, thisNearDepth, thisFarDepth, static_cast<u32>(index));
    };

    const SIMDVectorSingle otherSlopes = CollisionDetail::FrustumCornerSlopes(otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope);
    const SIMDVectorSingle otherNearDepth = VectorReplicate(otherNearPlane);
    const SIMDVectorSingle otherFarDepth = VectorReplicate(otherFarPlane);
    const auto otherCornerAt = [&](const usize index)noexcept{
        return CollisionDetail::FrustumCornerFromSlopes(otherFrustumOrigin, otherFrustumOrientation, otherSlopes, otherNearDepth, otherFarDepth, static_cast<u32>(index));
    };

    return
        CollisionDetail::FrustumPlaneSeriesIntersectPointSeries<BoundingFrustum::s_CornerCount>(thisPlaneAt, otherCornerAt)
        && CollisionDetail::FrustumPlaneSeriesIntersectPointSeries<BoundingFrustum::s_CornerCount>(otherPlaneAt, thisCornerAt);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrustumDoubleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void FOUNDATION_SIMD_CALL Transform(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, const SIMDMatrixDouble& matrix, SIMDVectorDouble& outOrigin,
    SIMDVectorDouble& outOrientation, f64& outRightSlope, f64& outLeftSlope, f64& outTopSlope, f64& outBottomSlope, f64& outNearPlane, f64& outFarPlane
)noexcept{
    SIMDMatrixDouble decomposition{};
    const SIMDVectorDouble rotation = MatrixDoubleDecompose(matrix, decomposition) ? decomposition.v[1] : s_DoubleSIMDIdentityR3;
    outOrigin = VectorDouble3Transform(frustumOrigin, matrix);
    outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(rotation, frustumOrientation));

    const SIMDVectorDouble slopes = CollisionDoubleDetail::FrustumCornerSlopes(rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue);
    const SIMDVectorDouble nearDepth = VectorDoubleReplicate(nearPlaneValue);
    const SIMDVectorDouble farDepth = VectorDoubleReplicate(farPlaneValue);
    SIMDVectorDouble minBounds{};
    SIMDVectorDouble maxBounds{};
    SIMDVectorDouble fittedMin{};
    SIMDVectorDouble fittedMax{};
    b8 hasValidDepth = true;
    for(u64 index = 0u; index < BoundingDoubleFrustum::s_DoubleCornerCount; ++index){
        const SIMDVectorDouble corner = VectorDouble3Transform(
            CollisionDoubleDetail::FrustumCornerFromSlopes(frustumOrigin, frustumOrientation, slopes, nearDepth, farDepth, index),
            matrix
        );
        if(index == 0u)
            minBounds = maxBounds = corner;
        else
            CollisionDoubleDetail::ExpandMinMax(corner, minBounds, maxBounds);

        if(hasValidDepth){
            const SIMDVectorDouble local = VectorDouble3InverseRotate(VectorDoubleSubtract(corner, outOrigin), outOrientation);
            if(VectorDoubleGetZ(local) <= 0.0)
                hasValidDepth = false;
            else{
                const SIMDVectorDouble slopesAndDepth = VectorDoubleSetZ(VectorDoubleDivide(local, VectorDoubleSplatZ(local)), VectorDoubleGetZ(local));
                if(index == 0u)
                    fittedMin = fittedMax = slopesAndDepth;
                else
                    CollisionDoubleDetail::ExpandMinMax(slopesAndDepth, fittedMin, fittedMax);
            }
        }
    }
    if(!hasValidDepth){
        const SIMDVectorDouble center = VectorDoubleScale(VectorDoubleAdd(minBounds, maxBounds), CollisionDoubleDetail::s_DoubleHalf);
        const SIMDVectorDouble extents = VectorDoubleScale(VectorDoubleSubtract(maxBounds, minBounds), CollisionDoubleDetail::s_DoubleHalf);
        const SIMDVectorDouble depth = VectorDoubleSubtract(VectorDoubleSplatZ(maxBounds), VectorDoubleSplatZ(minBounds));
        const SIMDVectorDouble padding = VectorDoubleMax(s_DoubleSIMDOne, depth);
        outOrigin = VectorDoubleSelect(center, VectorDoubleSubtract(VectorDoubleSplatZ(minBounds), padding), s_DoubleSIMDMaskZ);
        outOrientation = s_DoubleSIMDIdentityR3;
        const SIMDVectorDouble fittedSlopes = VectorDoubleDivide(VectorDoubleSelect(VectorDoubleZero(), extents, s_DoubleSIMDMaskXY), padding);
        const SIMDVectorDouble negativeSlopes = VectorDoubleNegate(fittedSlopes);
        outRightSlope = VectorDoubleGetX(fittedSlopes);
        outLeftSlope = VectorDoubleGetX(negativeSlopes);
        outTopSlope = VectorDoubleGetY(fittedSlopes);
        outBottomSlope = VectorDoubleGetY(negativeSlopes);
        outNearPlane = VectorDoubleGetX(padding);
        outFarPlane = VectorDoubleGetX(VectorDoubleAdd(padding, depth));
        return;
    }

    outRightSlope = VectorDoubleGetX(fittedMax);
    outLeftSlope = VectorDoubleGetX(fittedMin);
    outTopSlope = VectorDoubleGetY(fittedMax);
    outBottomSlope = VectorDoubleGetY(fittedMin);
    outNearPlane = VectorDoubleGetZ(fittedMin);
    outFarPlane = VectorDoubleGetZ(fittedMax);
}

inline void FOUNDATION_SIMD_CALL Transform(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, f64 scale, SIMDVectorDouble rotation,
    SIMDVectorDouble translation, SIMDVectorDouble& outOrigin, SIMDVectorDouble& outOrientation, f64& outRightSlope, f64& outLeftSlope, f64& outTopSlope, f64& outBottomSlope, f64& outNearPlane, f64& outFarPlane
)noexcept{
    outOrigin = VectorDoubleAdd(VectorDouble3Rotate(VectorDoubleScale(frustumOrigin, scale), rotation), translation);
    outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(rotation, frustumOrientation));
    const SIMDVectorDouble scaledPlanes = VectorDoubleMultiply(VectorDoubleSet(nearPlaneValue, farPlaneValue, nearPlaneValue, farPlaneValue), VectorDoubleAbs(VectorDoubleReplicate(scale)));
    outRightSlope = rightSlopeValue;
    outLeftSlope = leftSlopeValue;
    outTopSlope = topSlopeValue;
    outBottomSlope = bottomSlopeValue;
    outNearPlane = VectorDoubleGetX(scaledPlanes);
    outFarPlane = VectorDoubleGetY(scaledPlanes);
    if(scale < 0.0){
        outOrientation = QuaternionDoubleNormalize(QuaternionDoubleMultiply(outOrientation, VectorDoubleSet(0.0, 1.0, 0.0, 0.0)));
        const SIMDVectorDouble negatedSlopes = VectorDoubleNegate(VectorDoubleSet(bottomSlopeValue, topSlopeValue, bottomSlopeValue, topSlopeValue));
        outTopSlope = VectorDoubleGetX(negatedSlopes);
        outBottomSlope = VectorDoubleGetY(negatedSlopes);
    }
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainsPoint(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble point
)noexcept{
    for(usize planeIndex = 0u; planeIndex < CollisionDoubleDetail::s_DoubleFrustumPlaneCount; ++planeIndex){
        const SIMDVectorDouble plane = CollisionDoubleDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planeIndex);
        if(CollisionDoubleDetail::VectorDouble4AllTrue(VectorDoubleLess(CollisionDoubleDetail::PlaneDoubleDistance(plane, point), VectorDoubleZero())))
            return ContainmentType::Disjoint;
    }
    return ContainmentType::Contains;
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainsTriangle(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2
)noexcept{
    const auto pointAt = [&](const usize index)noexcept{ return CollisionDoubleDetail::SelectVector3(index, v0, v1, v2); };
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDoubleDetail::ContainmentFromPlaneSeriesTests<CollisionDoubleDetail::s_DoubleTriangleVertexCount, CollisionDoubleDetail::s_DoubleFrustumPlaneCount>(pointAt, planeAt);
}

[[nodiscard]] inline PlaneIntersectionType::Enum FOUNDATION_SIMD_CALL IntersectsPlane(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble plane
)noexcept{
    const SIMDVectorDouble slopes = CollisionDoubleDetail::FrustumCornerSlopes(rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue);
    const SIMDVectorDouble nearDepth = VectorDoubleReplicate(nearPlaneValue);
    const SIMDVectorDouble farDepth = VectorDoubleReplicate(farPlaneValue);
    const auto cornerAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumCornerFromSlopes(frustumOrigin, frustumOrientation, slopes, nearDepth, farDepth, index);
    };
    SIMDVectorDouble outside{};
    SIMDVectorDouble inside{};
    CollisionDoubleDetail::FastIntersectPointSeriesPlane<BoundingDoubleFrustum::s_DoubleCornerCount>(cornerAt, plane, outside, inside);
    if(CollisionDoubleDetail::VectorDouble4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDoubleDetail::VectorDouble4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}

[[nodiscard]] inline Expected<f64> FOUNDATION_SIMD_CALL IntersectsRay(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble rayOrigin, SIMDVectorDouble direction
)noexcept{
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble rayEpsilon = VectorDoubleReplicate(CollisionDoubleDetail::s_DoubleRayEpsilon);
    SIMDVectorDouble tMin = zero;
    SIMDVectorDouble tMax = VectorDoubleReplicate(Limit<f64>::s_Max);
    for(usize planeIndex = 0u; planeIndex < CollisionDoubleDetail::s_DoubleFrustumPlaneCount; ++planeIndex){
        const SIMDVectorDouble plane = CollisionDoubleDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planeIndex);
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

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainedBy(
    SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble plane0, SIMDVectorDouble plane1,
    SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5
)noexcept{
    const SIMDVectorDouble slopes = CollisionDoubleDetail::FrustumCornerSlopes(rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue);
    const SIMDVectorDouble nearDepth = VectorDoubleReplicate(nearPlaneValue);
    const SIMDVectorDouble farDepth = VectorDoubleReplicate(farPlaneValue);
    const auto cornerAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumCornerFromSlopes(frustumOrigin, frustumOrientation, slopes, nearDepth, farDepth, index);
    };
    const auto planeAt = [&](const usize index)noexcept{ return CollisionDoubleDetail::SelectPlane6(index, plane0, plane1, plane2, plane3, plane4, plane5); };
    return CollisionDoubleDetail::ContainmentFromPlaneSeriesTests<BoundingDoubleFrustum::s_DoubleCornerCount, CollisionDoubleDetail::s_DoubleFrustumPlaneCount>(cornerAt, planeAt);
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainsSphere(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble sphereValue
)noexcept{
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDoubleDetail::ContainmentFromSpherePlaneSeries(sphereValue, planeAt);
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainsBox(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents
)noexcept{
    const auto cornerAt = [&](const usize index)noexcept{ return CollisionDoubleDetail::AabbCorner(otherBoxCenter, otherBoxExtents, index); };
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDoubleDetail::ContainmentFromPlaneSeriesTests<BoundingDoubleBox::s_DoubleCornerCount, CollisionDoubleDetail::s_DoubleFrustumPlaneCount>(cornerAt, planeAt);
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainsOrientedBox(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation
)noexcept{
    SIMDVectorDouble axis0{};
    SIMDVectorDouble axis1{};
    SIMDVectorDouble axis2{};
    CollisionDoubleDetail::ObbCornerAxes(otherBoxExtents, otherBoxOrientation, axis0, axis1, axis2);
    const auto cornerAt = [&](const usize index)noexcept{ return CollisionDoubleDetail::ObbCornerFromAxes(otherBoxCenter, axis0, axis1, axis2, index); };
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDoubleDetail::ContainmentFromPlaneSeriesTests<BoundingDoubleOrientedBox::s_DoubleCornerCount, CollisionDoubleDetail::s_DoubleFrustumPlaneCount>(cornerAt, planeAt);
}

[[nodiscard]] inline ContainmentType::Enum FOUNDATION_SIMD_CALL ContainsFrustum(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherFrustumOrigin, const SIMDVectorDouble otherFrustumOrientation, const f64 otherRightSlope, const f64 otherLeftSlope, const f64 otherTopSlope, const f64 otherBottomSlope, const f64 otherNearPlane, const f64 otherFarPlane
)noexcept{
    const SIMDVectorDouble otherSlopes = CollisionDoubleDetail::FrustumCornerSlopes(otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope);
    const SIMDVectorDouble otherNearDepth = VectorDoubleReplicate(otherNearPlane);
    const SIMDVectorDouble otherFarDepth = VectorDoubleReplicate(otherFarPlane);
    const auto otherCornerAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumCornerFromSlopes(otherFrustumOrigin, otherFrustumOrientation, otherSlopes, otherNearDepth, otherFarDepth, index);
    };
    const auto thisPlaneAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    const ContainmentType::Enum containment = CollisionDoubleDetail::ContainmentFromPlaneSeriesTests<BoundingDoubleFrustum::s_DoubleCornerCount, CollisionDoubleDetail::s_DoubleFrustumPlaneCount>(otherCornerAt, thisPlaneAt);
    if(containment != ContainmentType::Intersects)
        return containment;

    const SIMDVectorDouble thisSlopes = CollisionDoubleDetail::FrustumCornerSlopes(rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue);
    const SIMDVectorDouble thisNearDepth = VectorDoubleReplicate(nearPlaneValue);
    const SIMDVectorDouble thisFarDepth = VectorDoubleReplicate(farPlaneValue);
    const auto thisCornerAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumCornerFromSlopes(frustumOrigin, frustumOrientation, thisSlopes, thisNearDepth, thisFarDepth, index);
    };
    const auto otherPlaneAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumPlane(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, index);
    };
    return CollisionDoubleDetail::FrustumPlaneSeriesIntersectPointSeries<BoundingDoubleFrustum::s_DoubleCornerCount>(otherPlaneAt, thisCornerAt) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL IntersectsSphere(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble sphereValue
)noexcept{
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDoubleDetail::FrustumPlaneSeriesIntersectSphere(planeAt, sphereValue);
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL IntersectsBox(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents
)noexcept{
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDoubleDetail::FrustumPlaneSeriesIntersectAxisAlignedBox(planeAt, otherBoxCenter, otherBoxExtents);
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL IntersectsOrientedBox(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherBoxCenter, const SIMDVectorDouble otherBoxExtents, const SIMDVectorDouble otherBoxOrientation
)noexcept{
    const auto planeAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    return CollisionDoubleDetail::FrustumPlaneSeriesIntersectOrientedBox(planeAt, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}

[[nodiscard]] inline b8 FOUNDATION_SIMD_CALL IntersectsFrustum(
    const SIMDVectorDouble frustumOrigin, const SIMDVectorDouble frustumOrientation, const f64 rightSlopeValue, const f64 leftSlopeValue, const f64 topSlopeValue, const f64 bottomSlopeValue, const f64 nearPlaneValue, const f64 farPlaneValue,
    const SIMDVectorDouble otherFrustumOrigin, const SIMDVectorDouble otherFrustumOrientation, const f64 otherRightSlope, const f64 otherLeftSlope, const f64 otherTopSlope, const f64 otherBottomSlope, const f64 otherNearPlane, const f64 otherFarPlane
)noexcept{
    const auto thisPlaneAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumPlane(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, index);
    };
    const auto otherPlaneAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumPlane(otherFrustumOrigin, otherFrustumOrientation, otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope, otherNearPlane, otherFarPlane, index);
    };

    const SIMDVectorDouble thisSlopes = CollisionDoubleDetail::FrustumCornerSlopes(rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue);
    const SIMDVectorDouble thisNearDepth = VectorDoubleReplicate(nearPlaneValue);
    const SIMDVectorDouble thisFarDepth = VectorDoubleReplicate(farPlaneValue);
    const auto thisCornerAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumCornerFromSlopes(frustumOrigin, frustumOrientation, thisSlopes, thisNearDepth, thisFarDepth, index);
    };

    const SIMDVectorDouble otherSlopes = CollisionDoubleDetail::FrustumCornerSlopes(otherRightSlope, otherLeftSlope, otherTopSlope, otherBottomSlope);
    const SIMDVectorDouble otherNearDepth = VectorDoubleReplicate(otherNearPlane);
    const SIMDVectorDouble otherFarDepth = VectorDoubleReplicate(otherFarPlane);
    const auto otherCornerAt = [&](const usize index)noexcept{
        return CollisionDoubleDetail::FrustumCornerFromSlopes(otherFrustumOrigin, otherFrustumOrientation, otherSlopes, otherNearDepth, otherFarDepth, index);
    };

    return
        CollisionDoubleDetail::FrustumPlaneSeriesIntersectPointSeries<BoundingDoubleFrustum::s_DoubleCornerCount>(thisPlaneAt, otherCornerAt)
        && CollisionDoubleDetail::FrustumPlaneSeriesIntersectPointSeries<BoundingDoubleFrustum::s_DoubleCornerCount>(otherPlaneAt, thisCornerAt);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

