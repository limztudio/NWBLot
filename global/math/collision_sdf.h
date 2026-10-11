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


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SdfTests::Plane(
    const SIMDVectorSingle position,
    const SIMDVectorSingle normalDistance
)noexcept{
    return PlaneTests::Distance(normalDistance, position);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::Plane(
    const SIMDVectorDouble position,
    const SIMDVectorDouble normalDistance
)noexcept{
    return PlaneDoubleTests::Distance(normalDistance, position);
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


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SdfTests::Sphere(
    const SIMDVectorSingle position,
    const SIMDVectorSingle radius
)noexcept{
    return VectorSubtract(Vector3Length(position), VectorSplatX(radius));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::Sphere(
    const SIMDVectorDouble position,
    const SIMDVectorDouble radius
)noexcept{
    return VectorDoubleSubtract(VectorDouble3Length(position), VectorDoubleSplatX(radius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SdfTests::CapsuleY(
    const SIMDVectorSingle position,
    const SIMDVectorSingle radiusHalfHeight
)noexcept{
    const SIMDVectorSingle segmentPoint = CollisionDetail::CapsuleYSegmentPoint(position, radiusHalfHeight);
    return VectorSubtract(Vector3Length(VectorSubtract(position, segmentPoint)), VectorSplatX(radiusHalfHeight));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::CapsuleY(
    const SIMDVectorDouble position,
    const SIMDVectorDouble radiusHalfHeight
)noexcept{
    const SIMDVectorDouble segmentPoint = CollisionDoubleDetail::CapsuleYSegmentPoint(position, radiusHalfHeight);
    return VectorDoubleSubtract(VectorDouble3Length(VectorDoubleSubtract(position, segmentPoint)), VectorDoubleSplatX(radiusHalfHeight));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SdfTests::PlaneNormal(
    const SIMDVectorSingle normalDistance,
    const SIMDVectorSingle fallback,
    const f32 minLengthSquared
)noexcept{
    return Vector3NormalizeOr(VectorSetW(normalDistance, 0.0f), fallback, minLengthSquared);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::PlaneNormal(
    const SIMDVectorDouble normalDistance,
    const SIMDVectorDouble fallback,
    const f64 minLengthSquared
)noexcept{
    return VectorDouble3NormalizeOr(VectorDoubleSetW(normalDistance, 0.0), fallback, minLengthSquared);
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


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL SdfTests::SphereNormal(
    const SIMDVectorSingle position,
    const SIMDVectorSingle fallback,
    const f32 minLengthSquared
)noexcept{
    return Vector3NormalizeOr(position, fallback, minLengthSquared);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SdfDoubleTests::SphereNormal(
    const SIMDVectorDouble position,
    const SIMDVectorDouble fallback,
    const f64 minLengthSquared
)noexcept{
    return VectorDouble3NormalizeOr(position, fallback, minLengthSquared);
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

