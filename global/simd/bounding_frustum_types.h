// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "../span.h"
#include "bounding_shape_fwd.h"
#include "matrix.h"
#include "collision_detail.h"
#include "collision_plane.h"
#include "collision_sdf.h"
#include "collision_aabb.h"
#include "collision_triangle.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BoundingFrustum{
    static constexpr usize s_CornerCount = 8u;


public:
    Float4 origin;
    Float4 orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    f32 rightSlope = 1.0f;
    f32 leftSlope = -1.0f;
    f32 topSlope = 1.0f;
    f32 bottomSlope = -1.0f;
    f32 nearPlane = 0.0f;
    f32 farPlane = 1.0f;


public:
    static void NWB_SIMD_CALL CreateFromMatrix(BoundingFrustum& outFrustum, const SIMDMatrix& projection, b8 rightHandedCoordinates = false)noexcept;


public:
    constexpr BoundingFrustum()noexcept = default;
    constexpr BoundingFrustum(const Float3U& originValue, const Float4& orientationValue, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue)noexcept
        : origin(originValue.x, originValue.y, originValue.z, 0.0f)
        , orientation(orientationValue)
        , rightSlope(rightSlopeValue)
        , leftSlope(leftSlopeValue)
        , topSlope(topSlopeValue)
        , bottomSlope(bottomSlopeValue)
        , nearPlane(nearPlaneValue)
        , farPlane(farPlaneValue)
    {}
    constexpr BoundingFrustum(const Float4& originValue, const Float4& orientationValue, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue)noexcept
        : origin(originValue)
        , orientation(orientationValue)
        , rightSlope(rightSlopeValue)
        , leftSlope(leftSlopeValue)
        , topSlope(topSlopeValue)
        , bottomSlope(bottomSlopeValue)
        , nearPlane(nearPlaneValue)
        , farPlane(farPlaneValue)
    {}
    explicit BoundingFrustum(const SIMDMatrix& projection, b8 rightHandedCoordinates = false)noexcept;


public:
    void NWB_SIMD_CALL transform(BoundingFrustum& outFrustum, const SIMDMatrix& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingFrustum& outFrustum, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation)const noexcept;
    void getCorners(Float3U (&corners)[s_CornerCount])const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorSingle point)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] b8 intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] b8 intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] b8 intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] b8 intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] b8 NWB_SIMD_CALL intersects(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL intersects(SIMDVectorSingle plane)const noexcept;
    [[nodiscard]] Expected<f32> NWB_SIMD_CALL intersects(SIMDVectorSingle rayOrigin, SIMDVectorSingle direction)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)const noexcept;
    void getPlanes(SIMDVectorSingle* nearPlaneOut, SIMDVectorSingle* farPlaneOut, SIMDVectorSingle* rightPlaneOut, SIMDVectorSingle* leftPlaneOut, SIMDVectorSingle* topPlaneOut, SIMDVectorSingle* bottomPlaneOut)const noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BoundingDoubleFrustum{
    static constexpr usize s_DoubleCornerCount = 8u;


public:
    Double4 origin;
    Double4 orientation = Double4(0.0, 0.0, 0.0, 1.0);
    f64 rightSlope = 1.0;
    f64 leftSlope = -1.0;
    f64 topSlope = 1.0;
    f64 bottomSlope = -1.0;
    f64 nearPlane = 0.0;
    f64 farPlane = 1.0;


public:
    static void NWB_SIMD_CALL CreateFromMatrix(BoundingDoubleFrustum& outFrustum, const SIMDMatrixDouble& projection, b8 rightHandedCoordinates = false)noexcept;


public:
    constexpr BoundingDoubleFrustum()noexcept = default;
    template<typename OriginT> requires (IsSame_V<OriginT, Double3> || IsSame_V<OriginT, Double3U>)
    constexpr BoundingDoubleFrustum(
        const OriginT& originValue,
        const Double4& orientationValue,
        const f64 rightSlopeValue,
        const f64 leftSlopeValue,
        const f64 topSlopeValue,
        const f64 bottomSlopeValue,
        const f64 nearPlaneValue,
        const f64 farPlaneValue
    )noexcept
        : origin(originValue.x, originValue.y, originValue.z, 0.0)
        , orientation(orientationValue)
        , rightSlope(rightSlopeValue)
        , leftSlope(leftSlopeValue)
        , topSlope(topSlopeValue)
        , bottomSlope(bottomSlopeValue)
        , nearPlane(nearPlaneValue)
        , farPlane(farPlaneValue)
    {}
    constexpr BoundingDoubleFrustum(
        const Double4& originValue,
        const Double4& orientationValue,
        const f64 rightSlopeValue,
        const f64 leftSlopeValue,
        const f64 topSlopeValue,
        const f64 bottomSlopeValue,
        const f64 nearPlaneValue,
        const f64 farPlaneValue
    )noexcept
        : origin(originValue)
        , orientation(orientationValue)
        , rightSlope(rightSlopeValue)
        , leftSlope(leftSlopeValue)
        , topSlope(topSlopeValue)
        , bottomSlope(bottomSlopeValue)
        , nearPlane(nearPlaneValue)
        , farPlane(farPlaneValue)
    {}
    explicit BoundingDoubleFrustum(const SIMDMatrixDouble& projection, b8 rightHandedCoordinates = false)noexcept;


public:
    void NWB_SIMD_CALL transform(BoundingDoubleFrustum& outFrustum, const SIMDMatrixDouble& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingDoubleFrustum& outFrustum, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation)const noexcept;
    template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
    void getCorners(PointT (&corners)[s_DoubleCornerCount])const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorDouble point)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleFrustum& frustum)const noexcept;
    [[nodiscard]] b8 intersects(const BoundingDoubleSphere& sphere)const noexcept;
    [[nodiscard]] b8 intersects(const BoundingDoubleBox& box)const noexcept;
    [[nodiscard]] b8 intersects(const BoundingDoubleOrientedBox& box)const noexcept;
    [[nodiscard]] b8 intersects(const BoundingDoubleFrustum& frustum)const noexcept;
    [[nodiscard]] b8 NWB_SIMD_CALL intersects(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL intersects(SIMDVectorDouble plane)const noexcept;
    [[nodiscard]] Expected<f64> NWB_SIMD_CALL intersects(SIMDVectorDouble rayOrigin, SIMDVectorDouble direction)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)const noexcept;
    void getPlanes(SIMDVectorDouble* nearPlaneOut, SIMDVectorDouble* farPlaneOut, SIMDVectorDouble* rightPlaneOut, SIMDVectorDouble* leftPlaneOut, SIMDVectorDouble* topPlaneOut, SIMDVectorDouble* bottomPlaneOut)const noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

