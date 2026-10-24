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
#include "bounding_frustum_tests.h"


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

