// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix_double.h"
#include "collision_detail_double.h"
#include "collision_plane_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SdfDoubleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL Plane(SIMDVectorDouble position, SIMDVectorDouble normalDistance)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL Box(SIMDVectorDouble position, SIMDVectorDouble halfExtents)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL Sphere(SIMDVectorDouble position, SIMDVectorDouble radius)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL CapsuleY(SIMDVectorDouble position, SIMDVectorDouble radiusHalfHeight)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL PlaneNormal(
    SIMDVectorDouble normalDistance,
    SIMDVectorDouble fallback,
    f64 minLengthSquared
)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL BoxNormal(
    SIMDVectorDouble position,
    SIMDVectorDouble halfExtents,
    SIMDVectorDouble fallback,
    f64 minLengthSquared
)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL SphereNormal(
    SIMDVectorDouble position,
    SIMDVectorDouble fallback,
    f64 minLengthSquared
)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL CapsuleYNormal(
    SIMDVectorDouble position,
    SIMDVectorDouble radiusHalfHeight,
    f64 minLengthSquared
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::Plane(
    const SIMDVectorDouble position,
    const SIMDVectorDouble normalDistance
)noexcept{
    return PlaneDoubleTests::Distance(normalDistance, position);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::Box(
    const SIMDVectorDouble position,
    const SIMDVectorDouble halfExtents
)noexcept{
    const SIMDVectorDouble q = VectorDoubleSubtract(VectorDoubleAbs(position), halfExtents);
    const SIMDVectorDouble outside = VectorDoubleMax(q, VectorDoubleZero());
    const SIMDVectorDouble insideDistance = VectorDoubleMin(CollisionDoubleDetail::VectorDouble3MaxComponent(q), VectorDoubleZero());
    return VectorDoubleAdd(VectorDouble3Length(outside), insideDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::Sphere(
    const SIMDVectorDouble position,
    const SIMDVectorDouble radius
)noexcept{
    return VectorDoubleSubtract(VectorDouble3Length(position), VectorDoubleSplatX(radius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::CapsuleY(
    const SIMDVectorDouble position,
    const SIMDVectorDouble radiusHalfHeight
)noexcept{
    const SIMDVectorDouble segmentPoint = CollisionDoubleDetail::CapsuleYSegmentPoint(position, radiusHalfHeight);
    return VectorDoubleSubtract(VectorDouble3Length(VectorDoubleSubtract(position, segmentPoint)), VectorDoubleSplatX(radiusHalfHeight));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::PlaneNormal(
    const SIMDVectorDouble normalDistance,
    const SIMDVectorDouble fallback,
    const f64 minLengthSquared
)noexcept{
    return VectorDouble3NormalizeOr(VectorDoubleSetW(normalDistance, 0.0), fallback, minLengthSquared);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::BoxNormal(
    const SIMDVectorDouble position,
    const SIMDVectorDouble halfExtents,
    const SIMDVectorDouble fallback,
    const f64 minLengthSquared
)noexcept{
    const SIMDVectorDouble q = VectorDoubleSubtract(VectorDoubleAbs(position), halfExtents);
    const SIMDVectorDouble outside = VectorDoubleMax(q, VectorDoubleZero());
    const SIMDVectorDouble signedOutside = VectorDoubleSelect(VectorDoubleNegate(outside), outside, VectorDoubleGreaterOrEqual(position, VectorDoubleZero()));
    const SIMDVectorDouble outsideNormal = VectorDouble3NormalizeOr(signedOutside, fallback, minLengthSquared);
    const SIMDVectorDouble useOutside = VectorDoubleGreater(VectorDouble3LengthSq(outside), VectorDoubleReplicate(minLengthSquared));

    const SIMDVectorDouble qX = VectorDoubleSplatX(q);
    const SIMDVectorDouble qY = VectorDoubleSplatY(q);
    const SIMDVectorDouble qZ = VectorDoubleSplatZ(q);
    const SIMDVectorDouble xIsMax = VectorDoubleAndInt(VectorDoubleGreaterOrEqual(qX, qY), VectorDoubleGreaterOrEqual(qX, qZ));
    const SIMDVectorDouble yIsMax = VectorDoubleGreaterOrEqual(qY, qZ);
    const SIMDVectorDouble xNormal = CollisionDoubleDetail::VectorDouble3SignedUnitMask(position, s_DoubleSIMDMaskX);
    const SIMDVectorDouble yNormal = CollisionDoubleDetail::VectorDouble3SignedUnitMask(position, s_DoubleSIMDMaskY);
    const SIMDVectorDouble zNormal = CollisionDoubleDetail::VectorDouble3SignedUnitMask(position, s_DoubleSIMDMaskZ);
    SIMDVectorDouble faceNormal = VectorDoubleSelect(zNormal, yNormal, yIsMax);
    faceNormal = VectorDoubleSelect(faceNormal, xNormal, xIsMax);
    return VectorDoubleSelect(faceNormal, outsideNormal, useOutside);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::SphereNormal(
    const SIMDVectorDouble position,
    const SIMDVectorDouble fallback,
    const f64 minLengthSquared
)noexcept{
    return VectorDouble3NormalizeOr(position, fallback, minLengthSquared);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::CapsuleYNormal(
    const SIMDVectorDouble position,
    const SIMDVectorDouble radiusHalfHeight,
    const f64 minLengthSquared
)noexcept{
    const SIMDVectorDouble segmentPoint = CollisionDoubleDetail::CapsuleYSegmentPoint(position, radiusHalfHeight);
    const SIMDVectorDouble fallback = VectorDoubleSelect(
        VectorDoubleSet(0.0, -1.0, 0.0, 0.0),
        VectorDoubleSet(0.0, 1.0, 0.0, 0.0),
        VectorDoubleGreaterOrEqual(VectorDoubleSplatY(position), VectorDoubleZero())
    );
    return VectorDouble3NormalizeOr(
        VectorDoubleSubtract(position, segmentPoint),
        fallback,
        minLengthSquared
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

