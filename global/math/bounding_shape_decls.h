// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


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

    [[nodiscard]] static SIMDVector GLB_SIMD_CALL transformSphereValue(SIMDVector sphereValue, const SIMDMatrix& matrix)noexcept;
    [[nodiscard]] static SIMDVector GLB_SIMD_CALL transformSphereValue(SIMDVector sphereValue, f32 scale, SIMDVector rotation, SIMDVector translation)noexcept;
    [[nodiscard]] static ContainmentType::Enum GLB_SIMD_CALL containsPointValue(SIMDVector sphereValue, SIMDVector point)noexcept;
    [[nodiscard]] static ContainmentType::Enum GLB_SIMD_CALL containsTriangleValue(SIMDVector sphereValue, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static bool GLB_SIMD_CALL intersectsTriangleValue(SIMDVector sphereValue, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum GLB_SIMD_CALL intersectsPlaneValue(SIMDVector sphereValue, SIMDVector plane)noexcept;
    [[nodiscard]] static bool GLB_SIMD_CALL intersectsRayValue(SIMDVector sphereValue, SIMDVector origin, SIMDVector direction, f32& outDistance)noexcept;
    [[nodiscard]] static ContainmentType::Enum GLB_SIMD_CALL containedByValue(SIMDVector sphereValue, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept;
    [[nodiscard]] static SIMDVector GLB_SIMD_CALL mergeSphereValues(SIMDVector sphereValue0, SIMDVector sphereValue1, bool& outDirectCopy, bool& outCopyFirst)noexcept;
    [[nodiscard]] static SIMDVector GLB_SIMD_CALL sphereFromCenterExtentsValue(SIMDVector centerValue, SIMDVector extentsValue)noexcept;
    [[nodiscard]] static SIMDVector GLB_SIMD_CALL sphereFromCornersValue(const SIMDVector* corners, usize cornerCount)noexcept;

    void GLB_SIMD_CALL transform(BoundingSphere& outSphere, const SIMDMatrix& matrix)const noexcept;
    void GLB_SIMD_CALL transform(BoundingSphere& outSphere, f32 scale, SIMDVector rotation, SIMDVector translation)const noexcept;

    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL contains(SIMDVector point)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL contains(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsSphereValue(SIMDVector sphereValue, SIMDVector otherSphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsBoxValues(SIMDVector sphereValue, SIMDVector boxCenter, SIMDVector boxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsOrientedBoxValues(SIMDVector sphereValue, SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsFrustumValues(SIMDVector sphereValue, SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersectsSphereValue(SIMDVector sphereValue, SIMDVector otherSphereValue)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersectsBoxValues(SIMDVector sphereValue, SIMDVector boxCenter, SIMDVector boxExtents)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersectsOrientedBoxValues(SIMDVector sphereValue, SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersects(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum GLB_SIMD_CALL intersects(SIMDVector plane)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersects(SIMDVector origin, SIMDVector direction, f32& outDistance)const noexcept;

    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containedBy(
        SIMDVector plane0,
        SIMDVector plane1,
        SIMDVector plane2,
        SIMDVector plane3,
        SIMDVector plane4,
        SIMDVector plane5
    )const noexcept;

    static void createMerged(BoundingSphere& outSphere, const BoundingSphere& sphere0, const BoundingSphere& sphere1)noexcept;
    static void createFromBoundingBox(BoundingSphere& outSphere, const BoundingBox& box)noexcept;
    static void createFromBoundingBox(BoundingSphere& outSphere, const BoundingOrientedBox& box)noexcept;
    static void createFromPoints(BoundingSphere& outSphere, usize count, const Float3U* points, usize stride)noexcept;
    static void createFromFrustum(BoundingSphere& outSphere, const BoundingFrustum& frustum)noexcept;
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

    static void GLB_SIMD_CALL transformBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, const SIMDMatrix& matrix, SIMDVector& outCenter, SIMDVector& outExtents)noexcept;
    static void GLB_SIMD_CALL transformBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, f32 scale, SIMDVector rotation, SIMDVector translation, SIMDVector& outCenter, SIMDVector& outExtents)noexcept;
    [[nodiscard]] static ContainmentType::Enum GLB_SIMD_CALL containsPointValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector point)noexcept;
    [[nodiscard]] static ContainmentType::Enum GLB_SIMD_CALL containsTriangleValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static bool GLB_SIMD_CALL intersectsTriangleValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum GLB_SIMD_CALL intersectsPlaneValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector plane)noexcept;
    [[nodiscard]] static bool GLB_SIMD_CALL intersectsRayValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector origin, SIMDVector direction, f32& outDistance)noexcept;
    [[nodiscard]] static ContainmentType::Enum GLB_SIMD_CALL containedByValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept;
    static void GLB_SIMD_CALL mergeBoxValues(SIMDVector boxCenter0, SIMDVector boxExtents0, SIMDVector boxCenter1, SIMDVector boxExtents1, SIMDVector& outCenter, SIMDVector& outExtents)noexcept;
    static void GLB_SIMD_CALL cornersValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector* outCorners)noexcept;

    void GLB_SIMD_CALL transform(BoundingBox& outBox, const SIMDMatrix& matrix)const noexcept;
    void GLB_SIMD_CALL transform(BoundingBox& outBox, f32 scale, SIMDVector rotation, SIMDVector translation)const noexcept;
    void getCorners(Float3U* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL contains(SIMDVector point)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL contains(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsSphereValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsOrientedBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents, SIMDVector otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsFrustumValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersectsBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersectsOrientedBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents, SIMDVector otherBoxOrientation)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersects(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum GLB_SIMD_CALL intersects(SIMDVector plane)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersects(SIMDVector origin, SIMDVector direction, f32& outDistance)const noexcept;

    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containedBy(
        SIMDVector plane0,
        SIMDVector plane1,
        SIMDVector plane2,
        SIMDVector plane3,
        SIMDVector plane4,
        SIMDVector plane5
    )const noexcept;

    static void createMerged(BoundingBox& outBox, const BoundingBox& box0, const BoundingBox& box1)noexcept;
    static void createFromSphere(BoundingBox& outBox, const BoundingSphere& sphere)noexcept;
    static void GLB_SIMD_CALL createFromPoints(BoundingBox& outBox, SIMDVector point0, SIMDVector point1)noexcept;
    static void createFromPoints(BoundingBox& outBox, usize count, const Float3U* points, usize stride)noexcept;
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

    static void GLB_SIMD_CALL transformOrientedBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, const SIMDMatrix& matrix, SIMDVector& outCenter, SIMDVector& outExtents, SIMDVector& outOrientation, bool& outCollapsedToAxisAligned)noexcept;
    static void GLB_SIMD_CALL transformOrientedBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, f32 scale, SIMDVector rotation, SIMDVector translation, SIMDVector& outCenter, SIMDVector& outExtents, SIMDVector& outOrientation)noexcept;
    static void GLB_SIMD_CALL cornersValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector* outCorners)noexcept;
    [[nodiscard]] static ContainmentType::Enum GLB_SIMD_CALL containsPointValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector point)noexcept;
    [[nodiscard]] static ContainmentType::Enum GLB_SIMD_CALL containsTriangleValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static bool GLB_SIMD_CALL intersectsTriangleValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum GLB_SIMD_CALL intersectsPlaneValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector plane)noexcept;
    [[nodiscard]] static bool GLB_SIMD_CALL intersectsRayValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector origin, SIMDVector direction, f32& outDistance)noexcept;
    [[nodiscard]] static ContainmentType::Enum GLB_SIMD_CALL containedByValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept;

    void GLB_SIMD_CALL transform(BoundingOrientedBox& outBox, const SIMDMatrix& matrix)const noexcept;
    void GLB_SIMD_CALL transform(BoundingOrientedBox& outBox, f32 scale, SIMDVector rotation, SIMDVector translation)const noexcept;
    void getCorners(Float3U* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL contains(SIMDVector point)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL contains(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsSphereValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsOrientedBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents, SIMDVector otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsFrustumValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersectsBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersectsOrientedBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents, SIMDVector otherBoxOrientation)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersects(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum GLB_SIMD_CALL intersects(SIMDVector plane)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersects(SIMDVector origin, SIMDVector direction, f32& outDistance)const noexcept;

    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containedBy(
        SIMDVector plane0,
        SIMDVector plane1,
        SIMDVector plane2,
        SIMDVector plane3,
        SIMDVector plane4,
        SIMDVector plane5
    )const noexcept;

    static void createFromBoundingBox(BoundingOrientedBox& outBox, const BoundingBox& box)noexcept;
    static void createFromPoints(BoundingOrientedBox& outBox, usize count, const Float3U* points, usize stride)noexcept;
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

    static void GLB_SIMD_CALL transformFrustumValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, const SIMDMatrix& matrix, SIMDVector& outOrigin, SIMDVector& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane)noexcept;
    static void GLB_SIMD_CALL transformFrustumValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, f32 scale, SIMDVector rotation, SIMDVector translation, SIMDVector& outOrigin, SIMDVector& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane)noexcept;
    static void GLB_SIMD_CALL cornersValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector* outCorners)noexcept;
    [[nodiscard]] static ContainmentType::Enum GLB_SIMD_CALL containsPointValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector point)noexcept;
    [[nodiscard]] static ContainmentType::Enum GLB_SIMD_CALL containsTriangleValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum GLB_SIMD_CALL intersectsPlaneValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector plane)noexcept;
    [[nodiscard]] static bool GLB_SIMD_CALL intersectsRayValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector rayOrigin, SIMDVector direction, f32& outDistance)noexcept;
    [[nodiscard]] static ContainmentType::Enum GLB_SIMD_CALL containedByValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept;

    void GLB_SIMD_CALL transform(BoundingFrustum& outFrustum, const SIMDMatrix& matrix)const noexcept;
    void GLB_SIMD_CALL transform(BoundingFrustum& outFrustum, f32 scale, SIMDVector rotation, SIMDVector translation)const noexcept;
    void getCorners(Float3U* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL contains(SIMDVector point)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL contains(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsSphereValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsBoxValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsOrientedBoxValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents, SIMDVector otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containsFrustumValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector otherFrustumOrigin, SIMDVector otherFrustumOrientation, f32 otherRightSlope, f32 otherLeftSlope, f32 otherTopSlope, f32 otherBottomSlope, f32 otherNearPlane, f32 otherFarPlane)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersectsSphereValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector sphereValue)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersectsBoxValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersectsOrientedBoxValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents, SIMDVector otherBoxOrientation)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersectsFrustumValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector otherFrustumOrigin, SIMDVector otherFrustumOrientation, f32 otherRightSlope, f32 otherLeftSlope, f32 otherTopSlope, f32 otherBottomSlope, f32 otherNearPlane, f32 otherFarPlane)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersects(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum GLB_SIMD_CALL intersects(SIMDVector plane)const noexcept;
    [[nodiscard]] bool GLB_SIMD_CALL intersects(SIMDVector rayOrigin, SIMDVector direction, f32& outDistance)const noexcept;

    [[nodiscard]] ContainmentType::Enum GLB_SIMD_CALL containedBy(
        SIMDVector plane0,
        SIMDVector plane1,
        SIMDVector plane2,
        SIMDVector plane3,
        SIMDVector plane4,
        SIMDVector plane5
    )const noexcept;

    void getPlanes(
        SIMDVector* nearPlaneOut,
        SIMDVector* farPlaneOut,
        SIMDVector* rightPlaneOut,
        SIMDVector* leftPlaneOut,
        SIMDVector* topPlaneOut,
        SIMDVector* bottomPlaneOut
    )const noexcept;

    static void GLB_SIMD_CALL createFromMatrix(
        BoundingFrustum& outFrustum,
        const SIMDMatrix& projection,
        bool rightHandedCoordinates = false
    )noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

