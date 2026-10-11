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

struct BoundingBox;
struct BoundingOrientedBox;
struct BoundingFrustum;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct BoundingSphere{
    Float4 centerRadius = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    constexpr BoundingSphere()noexcept = default;
    constexpr BoundingSphere(const Float3U& centerValue, const f32 radiusValue)noexcept
        : centerRadius(centerValue.x, centerValue.y, centerValue.z, radiusValue)
    {}
    constexpr explicit BoundingSphere(const Float4& centerRadiusValue)noexcept
        : centerRadius(centerRadiusValue)
    {}

    [[nodiscard]] static SIMDVectorSingle NWB_SIMD_CALL TransformSphereValue(SIMDVectorSingle sphereValue, const SIMDMatrix& matrix)noexcept;
    [[nodiscard]] static SIMDVectorSingle NWB_SIMD_CALL TransformSphereValue(SIMDVectorSingle sphereValue, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsPointValue(SIMDVectorSingle sphereValue, SIMDVectorSingle point)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsTriangleValue(SIMDVectorSingle sphereValue, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;
    [[nodiscard]] static bool NWB_SIMD_CALL IntersectsTriangleValue(SIMDVectorSingle sphereValue, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlaneValue(SIMDVectorSingle sphereValue, SIMDVectorSingle plane)noexcept;
    [[nodiscard]] static Expected<f32> NWB_SIMD_CALL IntersectsRayValue(SIMDVectorSingle sphereValue, SIMDVectorSingle origin, SIMDVectorSingle direction)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainedByValue(SIMDVectorSingle sphereValue, SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)noexcept;
    [[nodiscard]] static SIMDVectorSingle NWB_SIMD_CALL MergeSphereValues(SIMDVectorSingle sphereValue0, SIMDVectorSingle sphereValue1, bool& outDirectCopy, bool& outCopyFirst)noexcept;
    [[nodiscard]] static SIMDVectorSingle NWB_SIMD_CALL SphereFromCenterExtentsValue(SIMDVectorSingle centerValue, SIMDVectorSingle extentsValue)noexcept;
    [[nodiscard]] static SIMDVectorSingle NWB_SIMD_CALL SphereFromCornersValue(const SIMDVectorSingle* corners, usize cornerCount)noexcept;

    void NWB_SIMD_CALL transform(BoundingSphere& outSphere, const SIMDMatrix& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingSphere& outSphere, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorSingle point)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsSphereValue(SIMDVectorSingle sphereValue, SIMDVectorSingle otherSphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsBoxValues(SIMDVectorSingle sphereValue, SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsOrientedBoxValues(SIMDVectorSingle sphereValue, SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsFrustumValues(SIMDVectorSingle sphereValue, SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsSphereValue(SIMDVectorSingle sphereValue, SIMDVectorSingle otherSphereValue)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsBoxValues(SIMDVectorSingle sphereValue, SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsOrientedBoxValues(SIMDVectorSingle sphereValue, SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersects(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL intersects(SIMDVectorSingle plane)const noexcept;
    [[nodiscard]] Expected<f32> NWB_SIMD_CALL intersects(SIMDVectorSingle origin, SIMDVectorSingle direction)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(
        SIMDVectorSingle plane0,
        SIMDVectorSingle plane1,
        SIMDVectorSingle plane2,
        SIMDVectorSingle plane3,
        SIMDVectorSingle plane4,
        SIMDVectorSingle plane5
    )const noexcept;

    static void CreateMerged(BoundingSphere& outSphere, const BoundingSphere& sphere0, const BoundingSphere& sphere1)noexcept;
    static void CreateFromBoundingBox(BoundingSphere& outSphere, const BoundingBox& box)noexcept;
    static void CreateFromBoundingBox(BoundingSphere& outSphere, const BoundingOrientedBox& box)noexcept;
    static void CreateFromPoints(BoundingSphere& outSphere, usize count, const Float3U* points, usize stride)noexcept;
    static void CreateFromFrustum(BoundingSphere& outSphere, const BoundingFrustum& frustum)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct BoundingBox{
    static constexpr usize s_CornerCount = 8u;

    Float4 center;
    Float4 extents = Float4(1.0f, 1.0f, 1.0f, 0.0f);

    constexpr BoundingBox()noexcept = default;
    constexpr BoundingBox(const Float3U& centerValue, const Float3U& extentsValue)noexcept
        : center(centerValue.x, centerValue.y, centerValue.z, 0.0f)
        , extents(extentsValue.x, extentsValue.y, extentsValue.z, 0.0f)
    {}
    constexpr BoundingBox(const Float4& centerValue, const Float4& extentsValue)noexcept
        : center(centerValue)
        , extents(extentsValue)
    {}

    static void NWB_SIMD_CALL TransformBoxValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, const SIMDMatrix& matrix, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents)noexcept;
    static void NWB_SIMD_CALL TransformBoxValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsPointValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle point)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsTriangleValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;
    [[nodiscard]] static bool NWB_SIMD_CALL IntersectsTriangleValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlaneValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle plane)noexcept;
    [[nodiscard]] static Expected<f32> NWB_SIMD_CALL IntersectsRayValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle origin, SIMDVectorSingle direction)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainedByValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)noexcept;
    static void NWB_SIMD_CALL MergeBoxValues(SIMDVectorSingle boxCenter0, SIMDVectorSingle boxExtents0, SIMDVectorSingle boxCenter1, SIMDVectorSingle boxExtents1, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents)noexcept;
    static void NWB_SIMD_CALL CornersValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle* outCorners)noexcept;

    void NWB_SIMD_CALL transform(BoundingBox& outBox, const SIMDMatrix& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingBox& outBox, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation)const noexcept;
    void getCorners(Float3U* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorSingle point)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsSphereValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsBoxValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsOrientedBoxValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents, SIMDVectorSingle otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsFrustumValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsBoxValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsOrientedBoxValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents, SIMDVectorSingle otherBoxOrientation)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersects(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL intersects(SIMDVectorSingle plane)const noexcept;
    [[nodiscard]] Expected<f32> NWB_SIMD_CALL intersects(SIMDVectorSingle origin, SIMDVectorSingle direction)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(
        SIMDVectorSingle plane0,
        SIMDVectorSingle plane1,
        SIMDVectorSingle plane2,
        SIMDVectorSingle plane3,
        SIMDVectorSingle plane4,
        SIMDVectorSingle plane5
    )const noexcept;

    static void CreateMerged(BoundingBox& outBox, const BoundingBox& box0, const BoundingBox& box1)noexcept;
    static void CreateFromSphere(BoundingBox& outBox, const BoundingSphere& sphere)noexcept;
    static void NWB_SIMD_CALL CreateFromPoints(BoundingBox& outBox, SIMDVectorSingle point0, SIMDVectorSingle point1)noexcept;
    static void CreateFromPoints(BoundingBox& outBox, usize count, const Float3U* points, usize stride)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct BoundingOrientedBox{
    static constexpr usize s_CornerCount = 8u;

    Float4 center;
    Float4 extents = Float4(1.0f, 1.0f, 1.0f, 0.0f);
    Float4 orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    constexpr BoundingOrientedBox()noexcept = default;
    constexpr BoundingOrientedBox(
        const Float3U& centerValue,
        const Float3U& extentsValue,
        const Float4& orientationValue
    )noexcept
        : center(centerValue.x, centerValue.y, centerValue.z, 0.0f)
        , extents(extentsValue.x, extentsValue.y, extentsValue.z, 0.0f)
        , orientation(orientationValue)
    {}
    constexpr BoundingOrientedBox(
        const Float4& centerValue,
        const Float4& extentsValue,
        const Float4& orientationValue
    )noexcept
        : center(centerValue)
        , extents(extentsValue)
        , orientation(orientationValue)
    {}

    static void NWB_SIMD_CALL TransformOrientedBoxValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, const SIMDMatrix& matrix, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents, SIMDVectorSingle& outOrientation, bool& outCollapsedToAxisAligned)noexcept;
    static void NWB_SIMD_CALL TransformOrientedBoxValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents, SIMDVectorSingle& outOrientation)noexcept;
    static void NWB_SIMD_CALL CornersValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle* outCorners)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsPointValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle point)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsTriangleValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;
    [[nodiscard]] static bool NWB_SIMD_CALL IntersectsTriangleValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlaneValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle plane)noexcept;
    [[nodiscard]] static Expected<f32> NWB_SIMD_CALL IntersectsRayValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle origin, SIMDVectorSingle direction)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainedByValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)noexcept;

    void NWB_SIMD_CALL transform(BoundingOrientedBox& outBox, const SIMDMatrix& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingOrientedBox& outBox, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation)const noexcept;
    void getCorners(Float3U* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorSingle point)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsSphereValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsBoxValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsOrientedBoxValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents, SIMDVectorSingle otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsFrustumValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsBoxValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsOrientedBoxValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents, SIMDVectorSingle otherBoxOrientation)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersects(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL intersects(SIMDVectorSingle plane)const noexcept;
    [[nodiscard]] Expected<f32> NWB_SIMD_CALL intersects(SIMDVectorSingle origin, SIMDVectorSingle direction)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(
        SIMDVectorSingle plane0,
        SIMDVectorSingle plane1,
        SIMDVectorSingle plane2,
        SIMDVectorSingle plane3,
        SIMDVectorSingle plane4,
        SIMDVectorSingle plane5
    )const noexcept;

    static void CreateFromBoundingBox(BoundingOrientedBox& outBox, const BoundingBox& box)noexcept;
    static void CreateFromPoints(BoundingOrientedBox& outBox, usize count, const Float3U* points, usize stride)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct BoundingFrustum{
    static constexpr usize s_CornerCount = 8u;

    Float4 origin;
    Float4 orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    f32 rightSlope = 1.0f;
    f32 leftSlope = -1.0f;
    f32 topSlope = 1.0f;
    f32 bottomSlope = -1.0f;
    f32 nearPlane = 0.0f;
    f32 farPlane = 1.0f;

    constexpr BoundingFrustum()noexcept = default;
    constexpr BoundingFrustum(
        const Float3U& originValue,
        const Float4& orientationValue,
        const f32 rightSlopeValue,
        const f32 leftSlopeValue,
        const f32 topSlopeValue,
        const f32 bottomSlopeValue,
        const f32 nearPlaneValue,
        const f32 farPlaneValue
    )noexcept
        : origin(originValue.x, originValue.y, originValue.z, 0.0f)
        , orientation(orientationValue)
        , rightSlope(rightSlopeValue)
        , leftSlope(leftSlopeValue)
        , topSlope(topSlopeValue)
        , bottomSlope(bottomSlopeValue)
        , nearPlane(nearPlaneValue)
        , farPlane(farPlaneValue)
    {}
    constexpr BoundingFrustum(
        const Float4& originValue,
        const Float4& orientationValue,
        const f32 rightSlopeValue,
        const f32 leftSlopeValue,
        const f32 topSlopeValue,
        const f32 bottomSlopeValue,
        const f32 nearPlaneValue,
        const f32 farPlaneValue
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
    explicit BoundingFrustum(const SIMDMatrix& projection, bool rightHandedCoordinates = false)noexcept;

    static void NWB_SIMD_CALL TransformFrustumValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, const SIMDMatrix& matrix, SIMDVectorSingle& outOrigin, SIMDVectorSingle& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane)noexcept;
    static void NWB_SIMD_CALL TransformFrustumValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation, SIMDVectorSingle& outOrigin, SIMDVectorSingle& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane)noexcept;
    static void NWB_SIMD_CALL CornersValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle* outCorners)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsPointValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle point)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsTriangleValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlaneValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle plane)noexcept;
    [[nodiscard]] static Expected<f32> NWB_SIMD_CALL IntersectsRayValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle rayOrigin, SIMDVectorSingle direction)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainedByValue(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)noexcept;

    void NWB_SIMD_CALL transform(BoundingFrustum& outFrustum, const SIMDMatrix& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingFrustum& outFrustum, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation)const noexcept;
    void getCorners(Float3U* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorSingle point)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsSphereValues(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsBoxValues(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsOrientedBoxValues(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents, SIMDVectorSingle otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsFrustumValues(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle otherFrustumOrigin, SIMDVectorSingle otherFrustumOrientation, f32 otherRightSlope, f32 otherLeftSlope, f32 otherTopSlope, f32 otherBottomSlope, f32 otherNearPlane, f32 otherFarPlane)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsSphereValues(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle sphereValue)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsBoxValues(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsOrientedBoxValues(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents, SIMDVectorSingle otherBoxOrientation)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsFrustumValues(SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVectorSingle otherFrustumOrigin, SIMDVectorSingle otherFrustumOrientation, f32 otherRightSlope, f32 otherLeftSlope, f32 otherTopSlope, f32 otherBottomSlope, f32 otherNearPlane, f32 otherFarPlane)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersects(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL intersects(SIMDVectorSingle plane)const noexcept;
    [[nodiscard]] Expected<f32> NWB_SIMD_CALL intersects(SIMDVectorSingle rayOrigin, SIMDVectorSingle direction)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(
        SIMDVectorSingle plane0,
        SIMDVectorSingle plane1,
        SIMDVectorSingle plane2,
        SIMDVectorSingle plane3,
        SIMDVectorSingle plane4,
        SIMDVectorSingle plane5
    )const noexcept;

    void getPlanes(
        SIMDVectorSingle* nearPlaneOut,
        SIMDVectorSingle* farPlaneOut,
        SIMDVectorSingle* rightPlaneOut,
        SIMDVectorSingle* leftPlaneOut,
        SIMDVectorSingle* topPlaneOut,
        SIMDVectorSingle* bottomPlaneOut
    )const noexcept;

    static void NWB_SIMD_CALL CreateFromMatrix(
        BoundingFrustum& outFrustum,
        const SIMDMatrix& projection,
        bool rightHandedCoordinates = false
    )noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

