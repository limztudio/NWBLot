// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix.h"
#include "collision_detail.h"
#include "collision_plane.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SdfTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVector SIMDCALL Plane(SIMDVector position, SIMDVector normalDistance)noexcept;
[[nodiscard]] SIMDVector SIMDCALL Box(SIMDVector position, SIMDVector halfExtents)noexcept;
[[nodiscard]] SIMDVector SIMDCALL Sphere(SIMDVector position, SIMDVector radius)noexcept;
[[nodiscard]] SIMDVector SIMDCALL CapsuleY(SIMDVector position, SIMDVector radiusHalfHeight)noexcept;
[[nodiscard]] SIMDVector SIMDCALL PlaneNormal(
    SIMDVector normalDistance,
    SIMDVector fallback,
    f32 minLengthSquared
)noexcept;
[[nodiscard]] SIMDVector SIMDCALL BoxNormal(
    SIMDVector position,
    SIMDVector halfExtents,
    SIMDVector fallback,
    f32 minLengthSquared
)noexcept;
[[nodiscard]] SIMDVector SIMDCALL SphereNormal(
    SIMDVector position,
    SIMDVector fallback,
    f32 minLengthSquared
)noexcept;
[[nodiscard]] SIMDVector SIMDCALL CapsuleYNormal(
    SIMDVector position,
    SIMDVector radiusHalfHeight,
    f32 minLengthSquared
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::Plane(
    const SIMDVector position,
    const SIMDVector normalDistance
)noexcept{
    return PlaneTests::Distance(normalDistance, position);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::Box(
    const SIMDVector position,
    const SIMDVector halfExtents
)noexcept{
    const SIMDVector q = VectorSubtract(VectorAbs(position), halfExtents);
    const SIMDVector outside = VectorMax(q, VectorZero());
    const SIMDVector insideDistance = VectorMin(CollisionDetail::Vector3MaxComponent(q), VectorZero());
    return VectorAdd(Vector3Length(outside), insideDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::Sphere(
    const SIMDVector position,
    const SIMDVector radius
)noexcept{
    return VectorSubtract(Vector3Length(position), VectorSplatX(radius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::CapsuleY(
    const SIMDVector position,
    const SIMDVector radiusHalfHeight
)noexcept{
    const SIMDVector segmentPoint = CollisionDetail::CapsuleYSegmentPoint(position, radiusHalfHeight);
    return VectorSubtract(Vector3Length(VectorSubtract(position, segmentPoint)), VectorSplatX(radiusHalfHeight));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::PlaneNormal(
    const SIMDVector normalDistance,
    const SIMDVector fallback,
    const f32 minLengthSquared
)noexcept{
    return Vector3NormalizeOr(VectorSetW(normalDistance, 0.0f), fallback, minLengthSquared);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVector SIMDCALL SdfTests::BoxNormal(
    const SIMDVector position,
    const SIMDVector halfExtents,
    const SIMDVector fallback,
    const f32 minLengthSquared
)noexcept{
    const SIMDVector q = VectorSubtract(VectorAbs(position), halfExtents);
    const SIMDVector outside = VectorMax(q, VectorZero());
    const SIMDVector signedOutside = VectorSelect(VectorNegate(outside), outside, VectorGreaterOrEqual(position, VectorZero()));
    const SIMDVector outsideNormal = Vector3NormalizeOr(signedOutside, fallback, minLengthSquared);
    const SIMDVector useOutside = VectorGreater(Vector3LengthSq(outside), VectorReplicate(minLengthSquared));

    const SIMDVector qX = VectorSplatX(q);
    const SIMDVector qY = VectorSplatY(q);
    const SIMDVector qZ = VectorSplatZ(q);
    const SIMDVector xIsMax = VectorAndInt(VectorGreaterOrEqual(qX, qY), VectorGreaterOrEqual(qX, qZ));
    const SIMDVector yIsMax = VectorGreaterOrEqual(qY, qZ);
    const SIMDVector xNormal = CollisionDetail::Vector3SignedUnitMask(position, s_SIMDMaskX);
    const SIMDVector yNormal = CollisionDetail::Vector3SignedUnitMask(position, s_SIMDMaskY);
    const SIMDVector zNormal = CollisionDetail::Vector3SignedUnitMask(position, s_SIMDMaskZ);
    SIMDVector faceNormal = VectorSelect(zNormal, yNormal, yIsMax);
    faceNormal = VectorSelect(faceNormal, xNormal, xIsMax);
    return VectorSelect(faceNormal, outsideNormal, useOutside);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::SphereNormal(
    const SIMDVector position,
    const SIMDVector fallback,
    const f32 minLengthSquared
)noexcept{
    return Vector3NormalizeOr(position, fallback, minLengthSquared);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL SdfTests::CapsuleYNormal(
    const SIMDVector position,
    const SIMDVector radiusHalfHeight,
    const f32 minLengthSquared
)noexcept{
    const SIMDVector segmentPoint = CollisionDetail::CapsuleYSegmentPoint(position, radiusHalfHeight);
    const SIMDVector fallback = VectorSelect(
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

