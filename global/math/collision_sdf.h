// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix.h"
#include "collision_detail.h"
#include "collision_plane.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SdfTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL Plane(SIMDVectorSingle position, SIMDVectorSingle normalDistance)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL Box(SIMDVectorSingle position, SIMDVectorSingle halfExtents)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL Sphere(SIMDVectorSingle position, SIMDVectorSingle radius)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL CapsuleY(SIMDVectorSingle position, SIMDVectorSingle radiusHalfHeight)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL PlaneNormal(
    SIMDVectorSingle normalDistance,
    SIMDVectorSingle fallback,
    f32 minLengthSquared
)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL BoxNormal(
    SIMDVectorSingle position,
    SIMDVectorSingle halfExtents,
    SIMDVectorSingle fallback,
    f32 minLengthSquared
)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL SphereNormal(
    SIMDVectorSingle position,
    SIMDVectorSingle fallback,
    f32 minLengthSquared
)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL CapsuleYNormal(
    SIMDVectorSingle position,
    SIMDVectorSingle radiusHalfHeight,
    f32 minLengthSquared
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SdfTests::Plane(
    const SIMDVectorSingle position,
    const SIMDVectorSingle normalDistance
)noexcept{
    return PlaneTests::Distance(normalDistance, position);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SdfTests::Box(
    const SIMDVectorSingle position,
    const SIMDVectorSingle halfExtents
)noexcept{
    const SIMDVectorSingle q = VectorSubtract(VectorAbs(position), halfExtents);
    const SIMDVectorSingle outside = VectorMax(q, VectorZero());
    const SIMDVectorSingle insideDistance = VectorMin(CollisionDetail::Vector3MaxComponent(q), VectorZero());
    return VectorAdd(Vector3Length(outside), insideDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SdfTests::Sphere(
    const SIMDVectorSingle position,
    const SIMDVectorSingle radius
)noexcept{
    return VectorSubtract(Vector3Length(position), VectorSplatX(radius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SdfTests::CapsuleY(
    const SIMDVectorSingle position,
    const SIMDVectorSingle radiusHalfHeight
)noexcept{
    const SIMDVectorSingle segmentPoint = CollisionDetail::CapsuleYSegmentPoint(position, radiusHalfHeight);
    return VectorSubtract(Vector3Length(VectorSubtract(position, segmentPoint)), VectorSplatX(radiusHalfHeight));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SdfTests::PlaneNormal(
    const SIMDVectorSingle normalDistance,
    const SIMDVectorSingle fallback,
    const f32 minLengthSquared
)noexcept{
    return Vector3NormalizeOr(VectorSetW(normalDistance, 0.0f), fallback, minLengthSquared);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorSingle NWB_SIMD_CALL SdfTests::BoxNormal(
    const SIMDVectorSingle position,
    const SIMDVectorSingle halfExtents,
    const SIMDVectorSingle fallback,
    const f32 minLengthSquared
)noexcept{
    const SIMDVectorSingle q = VectorSubtract(VectorAbs(position), halfExtents);
    const SIMDVectorSingle outside = VectorMax(q, VectorZero());
    const SIMDVectorSingle signedOutside = VectorSelect(VectorNegate(outside), outside, VectorGreaterOrEqual(position, VectorZero()));
    const SIMDVectorSingle outsideNormal = Vector3NormalizeOr(signedOutside, fallback, minLengthSquared);
    const SIMDVectorSingle useOutside = VectorGreater(Vector3LengthSq(outside), VectorReplicate(minLengthSquared));

    const SIMDVectorSingle qX = VectorSplatX(q);
    const SIMDVectorSingle qY = VectorSplatY(q);
    const SIMDVectorSingle qZ = VectorSplatZ(q);
    const SIMDVectorSingle xIsMax = VectorAndInt(VectorGreaterOrEqual(qX, qY), VectorGreaterOrEqual(qX, qZ));
    const SIMDVectorSingle yIsMax = VectorGreaterOrEqual(qY, qZ);
    const SIMDVectorSingle xNormal = CollisionDetail::Vector3SignedUnitMask(position, s_SIMDMaskX);
    const SIMDVectorSingle yNormal = CollisionDetail::Vector3SignedUnitMask(position, s_SIMDMaskY);
    const SIMDVectorSingle zNormal = CollisionDetail::Vector3SignedUnitMask(position, s_SIMDMaskZ);
    SIMDVectorSingle faceNormal = VectorSelect(zNormal, yNormal, yIsMax);
    faceNormal = VectorSelect(faceNormal, xNormal, xIsMax);
    return VectorSelect(faceNormal, outsideNormal, useOutside);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SdfTests::SphereNormal(
    const SIMDVectorSingle position,
    const SIMDVectorSingle fallback,
    const f32 minLengthSquared
)noexcept{
    return Vector3NormalizeOr(position, fallback, minLengthSquared);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SdfTests::CapsuleYNormal(
    const SIMDVectorSingle position,
    const SIMDVectorSingle radiusHalfHeight,
    const f32 minLengthSquared
)noexcept{
    const SIMDVectorSingle segmentPoint = CollisionDetail::CapsuleYSegmentPoint(position, radiusHalfHeight);
    const SIMDVectorSingle fallback = VectorSelect(
        VectorSet(0.0f, -1.0f, 0.0f, 0.0f),
        VectorSet(0.0f, 1.0f, 0.0f, 0.0f),
        VectorGreaterOrEqual(VectorSplatY(position), VectorZero())
    );
    return Vector3NormalizeOr(
        VectorSubtract(position, segmentPoint),
        fallback,
        minLengthSquared
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

