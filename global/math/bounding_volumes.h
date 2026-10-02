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

    [[nodiscard]] static SIMDVector SIMDCALL transformSphereValue(SIMDVector sphereValue, const SIMDMatrix& matrix)noexcept;
    [[nodiscard]] static SIMDVector SIMDCALL transformSphereValue(SIMDVector sphereValue, f32 scale, SIMDVector rotation, SIMDVector translation)noexcept;
    [[nodiscard]] static ContainmentType::Enum SIMDCALL containsPointValue(SIMDVector sphereValue, SIMDVector point)noexcept;
    [[nodiscard]] static ContainmentType::Enum SIMDCALL containsTriangleValue(SIMDVector sphereValue, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static bool SIMDCALL intersectsTriangleValue(SIMDVector sphereValue, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum SIMDCALL intersectsPlaneValue(SIMDVector sphereValue, SIMDVector plane)noexcept;
    [[nodiscard]] static bool SIMDCALL intersectsRayValue(SIMDVector sphereValue, SIMDVector origin, SIMDVector direction, f32& outDistance)noexcept;
    [[nodiscard]] static ContainmentType::Enum SIMDCALL containedByValue(SIMDVector sphereValue, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept;
    [[nodiscard]] static SIMDVector SIMDCALL mergeSphereValues(SIMDVector sphereValue0, SIMDVector sphereValue1, bool& outDirectCopy, bool& outCopyFirst)noexcept;
    [[nodiscard]] static SIMDVector SIMDCALL sphereFromCenterExtentsValue(SIMDVector centerValue, SIMDVector extentsValue)noexcept;
    [[nodiscard]] static SIMDVector SIMDCALL sphereFromCornersValue(const SIMDVector* corners, usize cornerCount)noexcept;

    void SIMDCALL transform(BoundingSphere& outSphere, const SIMDMatrix& matrix)const noexcept;
    void SIMDCALL transform(BoundingSphere& outSphere, f32 scale, SIMDVector rotation, SIMDVector translation)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector point)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL containsSphereValue(SIMDVector sphereValue, SIMDVector otherSphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL containsBoxValues(SIMDVector sphereValue, SIMDVector boxCenter, SIMDVector boxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL containsOrientedBoxValues(SIMDVector sphereValue, SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL containsFrustumValues(SIMDVector sphereValue, SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] bool SIMDCALL intersectsSphereValue(SIMDVector sphereValue, SIMDVector otherSphereValue)const noexcept;
    [[nodiscard]] bool SIMDCALL intersectsBoxValues(SIMDVector sphereValue, SIMDVector boxCenter, SIMDVector boxExtents)const noexcept;
    [[nodiscard]] bool SIMDCALL intersectsOrientedBoxValues(SIMDVector sphereValue, SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation)const noexcept;
    [[nodiscard]] bool SIMDCALL intersects(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum SIMDCALL intersects(SIMDVector plane)const noexcept;
    [[nodiscard]] bool SIMDCALL intersects(SIMDVector origin, SIMDVector direction, f32& outDistance)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL containedBy(
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

    static void SIMDCALL transformBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, const SIMDMatrix& matrix, SIMDVector& outCenter, SIMDVector& outExtents)noexcept;
    static void SIMDCALL transformBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, f32 scale, SIMDVector rotation, SIMDVector translation, SIMDVector& outCenter, SIMDVector& outExtents)noexcept;
    [[nodiscard]] static ContainmentType::Enum SIMDCALL containsPointValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector point)noexcept;
    [[nodiscard]] static ContainmentType::Enum SIMDCALL containsTriangleValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static bool SIMDCALL intersectsTriangleValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum SIMDCALL intersectsPlaneValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector plane)noexcept;
    [[nodiscard]] static bool SIMDCALL intersectsRayValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector origin, SIMDVector direction, f32& outDistance)noexcept;
    [[nodiscard]] static ContainmentType::Enum SIMDCALL containedByValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept;
    static void SIMDCALL mergeBoxValues(SIMDVector boxCenter0, SIMDVector boxExtents0, SIMDVector boxCenter1, SIMDVector boxExtents1, SIMDVector& outCenter, SIMDVector& outExtents)noexcept;
    static void SIMDCALL cornersValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector* outCorners)noexcept;

    void SIMDCALL transform(BoundingBox& outBox, const SIMDMatrix& matrix)const noexcept;
    void SIMDCALL transform(BoundingBox& outBox, f32 scale, SIMDVector rotation, SIMDVector translation)const noexcept;
    void getCorners(Float3U* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector point)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL containsSphereValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL containsBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL containsOrientedBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents, SIMDVector otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL containsFrustumValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] bool SIMDCALL intersectsBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents)const noexcept;
    [[nodiscard]] bool SIMDCALL intersectsOrientedBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents, SIMDVector otherBoxOrientation)const noexcept;
    [[nodiscard]] bool SIMDCALL intersects(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum SIMDCALL intersects(SIMDVector plane)const noexcept;
    [[nodiscard]] bool SIMDCALL intersects(SIMDVector origin, SIMDVector direction, f32& outDistance)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL containedBy(
        SIMDVector plane0,
        SIMDVector plane1,
        SIMDVector plane2,
        SIMDVector plane3,
        SIMDVector plane4,
        SIMDVector plane5
    )const noexcept;

    static void createMerged(BoundingBox& outBox, const BoundingBox& box0, const BoundingBox& box1)noexcept;
    static void createFromSphere(BoundingBox& outBox, const BoundingSphere& sphere)noexcept;
    static void SIMDCALL createFromPoints(BoundingBox& outBox, SIMDVector point0, SIMDVector point1)noexcept;
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

    static void SIMDCALL transformOrientedBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, const SIMDMatrix& matrix, SIMDVector& outCenter, SIMDVector& outExtents, SIMDVector& outOrientation, bool& outCollapsedToAxisAligned)noexcept;
    static void SIMDCALL transformOrientedBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, f32 scale, SIMDVector rotation, SIMDVector translation, SIMDVector& outCenter, SIMDVector& outExtents, SIMDVector& outOrientation)noexcept;
    static void SIMDCALL cornersValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector* outCorners)noexcept;
    [[nodiscard]] static ContainmentType::Enum SIMDCALL containsPointValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector point)noexcept;
    [[nodiscard]] static ContainmentType::Enum SIMDCALL containsTriangleValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static bool SIMDCALL intersectsTriangleValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum SIMDCALL intersectsPlaneValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector plane)noexcept;
    [[nodiscard]] static bool SIMDCALL intersectsRayValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector origin, SIMDVector direction, f32& outDistance)noexcept;
    [[nodiscard]] static ContainmentType::Enum SIMDCALL containedByValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept;

    void SIMDCALL transform(BoundingOrientedBox& outBox, const SIMDMatrix& matrix)const noexcept;
    void SIMDCALL transform(BoundingOrientedBox& outBox, f32 scale, SIMDVector rotation, SIMDVector translation)const noexcept;
    void getCorners(Float3U* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector point)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL containsSphereValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL containsBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL containsOrientedBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents, SIMDVector otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL containsFrustumValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] bool SIMDCALL intersectsBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents)const noexcept;
    [[nodiscard]] bool SIMDCALL intersectsOrientedBoxValues(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents, SIMDVector otherBoxOrientation)const noexcept;
    [[nodiscard]] bool SIMDCALL intersects(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum SIMDCALL intersects(SIMDVector plane)const noexcept;
    [[nodiscard]] bool SIMDCALL intersects(SIMDVector origin, SIMDVector direction, f32& outDistance)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL containedBy(
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

    static void SIMDCALL transformFrustumValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, const SIMDMatrix& matrix, SIMDVector& outOrigin, SIMDVector& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane)noexcept;
    static void SIMDCALL transformFrustumValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, f32 scale, SIMDVector rotation, SIMDVector translation, SIMDVector& outOrigin, SIMDVector& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane)noexcept;
    static void SIMDCALL cornersValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector* outCorners)noexcept;
    [[nodiscard]] static ContainmentType::Enum SIMDCALL containsPointValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector point)noexcept;
    [[nodiscard]] static ContainmentType::Enum SIMDCALL containsTriangleValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum SIMDCALL intersectsPlaneValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector plane)noexcept;
    [[nodiscard]] static bool SIMDCALL intersectsRayValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector rayOrigin, SIMDVector direction, f32& outDistance)noexcept;
    [[nodiscard]] static ContainmentType::Enum SIMDCALL containedByValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept;

    void SIMDCALL transform(BoundingFrustum& outFrustum, const SIMDMatrix& matrix)const noexcept;
    void SIMDCALL transform(BoundingFrustum& outFrustum, f32 scale, SIMDVector rotation, SIMDVector translation)const noexcept;
    void getCorners(Float3U* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector point)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL containsSphereValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL containsBoxValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL containsOrientedBoxValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents, SIMDVector otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL containsFrustumValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector otherFrustumOrigin, SIMDVector otherFrustumOrientation, f32 otherRightSlope, f32 otherLeftSlope, f32 otherTopSlope, f32 otherBottomSlope, f32 otherNearPlane, f32 otherFarPlane)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
    [[nodiscard]] bool SIMDCALL intersectsSphereValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector sphereValue)const noexcept;
    [[nodiscard]] bool SIMDCALL intersectsBoxValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents)const noexcept;
    [[nodiscard]] bool SIMDCALL intersectsOrientedBoxValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector otherBoxCenter, SIMDVector otherBoxExtents, SIMDVector otherBoxOrientation)const noexcept;
    [[nodiscard]] bool SIMDCALL intersectsFrustumValues(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector otherFrustumOrigin, SIMDVector otherFrustumOrientation, f32 otherRightSlope, f32 otherLeftSlope, f32 otherTopSlope, f32 otherBottomSlope, f32 otherNearPlane, f32 otherFarPlane)const noexcept;
    [[nodiscard]] bool SIMDCALL intersects(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum SIMDCALL intersects(SIMDVector plane)const noexcept;
    [[nodiscard]] bool SIMDCALL intersects(SIMDVector rayOrigin, SIMDVector direction, f32& outDistance)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL containedBy(
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

    static void SIMDCALL createFromMatrix(
        BoundingFrustum& outFrustum,
        const SIMDMatrix& projection,
        bool rightHandedCoordinates = false
    )noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CollisionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void AabbCorners(const SIMDVector center, const SIMDVector extents, SIMDVector* outCorners)noexcept{
    for(u32 i = 0u; i < BoundingBox::s_CornerCount; ++i)
        outCorners[i] = VectorMultiplyAdd(extents, BoxCornerOffset(i), center);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void ObbCorners(const SIMDVector center, const SIMDVector extents, const SIMDVector orientation, SIMDVector* outCorners)noexcept{
    SIMDVector axis0{};
    SIMDVector axis1{};
    SIMDVector axis2{};
    ObbAxes(orientation, axis0, axis1, axis2);
    axis0 = VectorMultiply(axis0, VectorSplatX(extents));
    axis1 = VectorMultiply(axis1, VectorSplatY(extents));
    axis2 = VectorMultiply(axis2, VectorSplatZ(extents));
    for(u32 i = 0u; i < BoundingOrientedBox::s_CornerCount; ++i){
        const SIMDVector offset = BoxCornerOffset(i);
        SIMDVector corner = VectorMultiply(axis0, VectorSplatX(offset));
        corner = VectorMultiplyAdd(axis1, VectorSplatY(offset), corner);
        corner = VectorMultiplyAdd(axis2, VectorSplatZ(offset), corner);
        outCorners[i] = VectorAdd(corner, center);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void FrustumCorners(
    const SIMDVector origin,
    const SIMDVector orientation,
    const f32 rightSlope,
    const f32 leftSlope,
    const f32 topSlope,
    const f32 bottomSlope,
    const f32 nearPlane,
    const f32 farPlane,
    SIMDVector* outCorners
)noexcept{
    const SIMDVector slopes = VectorSet(rightSlope, leftSlope, bottomSlope, topSlope);
    const SIMDVector depths[2] = {
        VectorReplicate(nearPlane),
        VectorReplicate(farPlane),
    };
    const SIMDVector zero = VectorZero();
    u32 corner = 0u;
    for(const SIMDVector depth : depths){
        const SIMDVector scaledSlopes = VectorMultiply(slopes, depth);
        const SIMDVector right = VectorSplatX(scaledSlopes);
        const SIMDVector left = VectorSplatY(scaledSlopes);
        const SIMDVector bottom = VectorSplatZ(scaledSlopes);
        const SIMDVector top = VectorSplatW(scaledSlopes);
        const SIMDVector localCorners[4] = {
            VectorMergeX(right, top, depth, zero),
            VectorMergeX(left, top, depth, zero),
            VectorMergeX(left, bottom, depth, zero),
            VectorMergeX(right, bottom, depth, zero),
        };
        for(const SIMDVector localCorner : localCorners){
            outCorners[corner] = VectorAdd(Vector3Rotate(localCorner, orientation), origin);
            ++corner;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVector SIMDCALL BoundingSphere::transformSphereValue(SIMDVector sphereValue, const SIMDMatrix& matrix)noexcept{
    SIMDVector scale{};
    SIMDVector rotation{};
    SIMDVector translation{};
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = VectorSplatW(sphereValue);
    if(MatrixDecompose(scale, rotation, translation, matrix)){
        const SIMDVector absScale = VectorAbs(scale);
        const SIMDVector maxScale = CollisionDetail::Vector3MaxComponent(absScale);
        return CollisionDetail::SphereCenterRadius(Vector3Transform(centerVector, matrix), VectorMultiply(sphereRadius, maxScale));
    }

    const SIMDVector maxScaleVector = VectorMax(Vector3Length(matrix.v[0]), VectorMax(Vector3Length(matrix.v[1]), Vector3Length(matrix.v[2])));
    return CollisionDetail::SphereCenterRadius(Vector3Transform(centerVector, matrix), VectorMultiply(sphereRadius, maxScaleVector));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVector SIMDCALL BoundingSphere::transformSphereValue(
    SIMDVector sphereValue,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)noexcept{
    const SIMDVector transformedCenter = VectorAdd(Vector3Rotate(VectorScale(CollisionDetail::SphereCenter(sphereValue), scale), rotation), translation);
    return CollisionDetail::SphereCenterRadius(transformedCenter, VectorScale(VectorSplatW(sphereValue), Abs(scale)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingSphere::transform(BoundingSphere& outSphere, const SIMDMatrix& matrix)const noexcept{ // beginner: Loads storage once, Stores result once.
    StoreFloat(transformSphereValue(LoadFloat(centerRadius), matrix), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingSphere::transform(
    BoundingSphere& outSphere,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    StoreFloat(transformSphereValue(LoadFloat(centerRadius), scale, rotation, translation), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containsPointValue(SIMDVector sphereValue, SIMDVector point)noexcept{
    const SIMDVector delta = VectorSubtract(point, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(delta), VectorMultiply(sphereRadius, sphereRadius))) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::contains(const SIMDVector point)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsPointValue(LoadFloat(centerRadius), point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containsTriangleValue(
    SIMDVector sphereValue,
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2
)noexcept{
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    if(CollisionDetail::PointsInsideSphere(points, CollisionDetail::s_TriangleVertexCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return intersectsTriangleValue(sphereValue, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsTriangleValue(LoadFloat(centerRadius), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containsSphereValue(const SIMDVector sphereValue, const SIMDVector otherSphereValue)const noexcept{
    const SIMDVector sphereRadius = VectorSplatW(sphereValue);
    const SIMDVector otherRadius = VectorSplatW(otherSphereValue);
    const SIMDVector delta = VectorSubtract(CollisionDetail::SphereCenter(otherSphereValue), CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector distanceSq = Vector3LengthSq(delta);
    if(Vector4GreaterOrEqual(sphereRadius, otherRadius)){
        const SIMDVector radiusDelta = VectorSubtract(sphereRadius, otherRadius);
        if(Vector4LessOrEqual(distanceSq, VectorMultiply(radiusDelta, radiusDelta)))
            return ContainmentType::Contains;
    }

    const SIMDVector radiusSum = VectorAdd(sphereRadius, otherRadius);
    if(Vector4GreaterOrEqual(distanceSq, VectorMultiply(radiusSum, radiusSum)))
        return ContainmentType::Disjoint;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingSphere& sphere)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsSphereValue(LoadFloat(centerRadius), LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containsBoxValues(const SIMDVector sphereValue, const SIMDVector boxCenter, const SIMDVector boxExtents)const noexcept{
    if(!intersectsBoxValues(sphereValue, boxCenter, boxExtents))
        return ContainmentType::Disjoint;

    SIMDVector corners[BoundingBox::s_CornerCount];
    CollisionDetail::AabbCorners(boxCenter, boxExtents, corners);
    if(CollisionDetail::PointsInsideSphere(corners, BoundingBox::s_CornerCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsBoxValues(LoadFloat(centerRadius), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containsOrientedBoxValues(const SIMDVector sphereValue, const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation)const noexcept{
    if(!intersectsOrientedBoxValues(sphereValue, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Disjoint;

    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
    if(CollisionDetail::PointsInsideSphere(corners, BoundingOrientedBox::s_CornerCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingOrientedBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsOrientedBoxValues(LoadFloat(centerRadius), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containsFrustumValues(const SIMDVector sphereValue, const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue)const noexcept{
    SIMDVector corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(
        frustumOrigin,
        frustumOrientation,
        rightSlopeValue,
        leftSlopeValue,
        topSlopeValue,
        bottomSlopeValue,
        nearPlaneValue,
        farPlaneValue,
        corners
    );
    if(CollisionDetail::PointsInsideSphere(corners, BoundingFrustum::s_CornerCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingFrustum& frustum)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    // Beginner boundary: load once, then run the SIMD-domain core.
    if(!intersects(frustum))
        return ContainmentType::Disjoint;
    return containsFrustumValues(LoadFloat(centerRadius), LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersectsSphereValue(const SIMDVector sphereValue, const SIMDVector otherSphereValue)const noexcept{
    const SIMDVector radiusSum = VectorAdd(CollisionDetail::SphereRadius(sphereValue), CollisionDetail::SphereRadius(otherSphereValue));
    const SIMDVector delta = VectorSubtract(CollisionDetail::SphereCenter(otherSphereValue), CollisionDetail::SphereCenter(sphereValue));
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(delta), VectorMultiply(radiusSum, radiusSum)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingSphere& sphere)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsSphereValue(LoadFloat(centerRadius), LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersectsBoxValues(const SIMDVector sphereValue, const SIMDVector boxCenter, const SIMDVector boxExtents)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(centerVector, minBounds, maxBounds);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, centerVector)), VectorMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsBoxValues(LoadFloat(centerRadius), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersectsOrientedBoxValues(const SIMDVector sphereValue, const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation)const noexcept{
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector localCenter = CollisionDetail::PointToObbLocal(CollisionDetail::SphereCenter(sphereValue), boxCenter, boxOrientation);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(localCenter, VectorNegate(boxExtents), boxExtents);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, localCenter)), VectorMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingOrientedBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsOrientedBoxValues(LoadFloat(centerRadius), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingFrustum& frustum)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return frustum.intersectsSphereValues(LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadFloat(centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersectsTriangleValue(
    SIMDVector sphereValue,
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2
)noexcept{
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnTriangle(centerVector, v0, v1, v2);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, centerVector)), VectorMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsTriangleValue(LoadFloat(centerRadius), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingSphere::intersectsPlaneValue(SIMDVector sphereValue, SIMDVector plane)noexcept{
    const SIMDVector distance = CollisionDetail::PlaneDistance(plane, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(distance, sphereRadius)))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(VectorLess(distance, VectorNegate(sphereRadius))))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingSphere::intersects(const SIMDVector plane)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsPlaneValue(LoadFloat(centerRadius), plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersectsRayValue(SIMDVector sphereValue, SIMDVector origin, SIMDVector direction, f32& outDistance)noexcept{
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector localOrigin = VectorSubtract(origin, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector bVector = Vector3Dot(localOrigin, direction);
    const SIMDVector cVector = VectorSubtract(Vector3LengthSq(localOrigin), VectorMultiply(sphereRadius, sphereRadius));
    if(Vector4Greater(cVector, VectorZero()) && Vector4Greater(bVector, VectorZero()))
        return false;

    const SIMDVector discriminantVector = VectorSubtract(VectorMultiply(bVector, bVector), cVector);
    if(Vector4Less(discriminantVector, VectorZero()))
        return false;

    const SIMDVector distance = VectorSubtract(VectorNegate(bVector), VectorSqrt(discriminantVector));
    outDistance = Max(0.0f, VectorGetX(distance));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersects(
    SIMDVector origin,
    SIMDVector direction,
    f32& outDistance
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsRayValue(LoadFloat(centerRadius), origin, direction, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containedByValue(SIMDVector sphereValue, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept{
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    SIMDVector anyIntersecting = VectorFalseInt();
    for(const SIMDVector plane : planes){
        SIMDVector outside{};
        SIMDVector inside{};
        CollisionDetail::FastIntersectSpherePlane(centerVector, sphereRadius, plane, outside, inside);
        if(CollisionDetail::Vector4AllTrue(outside))
            return ContainmentType::Disjoint;
        anyIntersecting = VectorOrInt(anyIntersecting, VectorEqualInt(inside, VectorFalseInt()));
    }
    return CollisionDetail::Vector4AllTrue(anyIntersecting) ? ContainmentType::Intersects : ContainmentType::Contains;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containedBy(
    SIMDVector plane0,
    SIMDVector plane1,
    SIMDVector plane2,
    SIMDVector plane3,
    SIMDVector plane4,
    SIMDVector plane5
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containedByValue(LoadFloat(centerRadius), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVector SIMDCALL BoundingSphere::mergeSphereValues(SIMDVector sphereValue0, SIMDVector sphereValue1, bool& outDirectCopy, bool& outCopyFirst)noexcept{
    outDirectCopy = false;
    outCopyFirst = true;
    const SIMDVector center0 = CollisionDetail::SphereCenter(sphereValue0);
    const SIMDVector center1 = CollisionDetail::SphereCenter(sphereValue1);
    const SIMDVector radius0 = VectorSplatW(sphereValue0);
    const SIMDVector radius1 = VectorSplatW(sphereValue1);
    const SIMDVector delta = VectorSubtract(center1, center0);
    const SIMDVector distanceSquared = Vector3LengthSq(delta);
    const SIMDVector radiusDelta = VectorSubtract(radius0, radius1);

    if(Vector4GreaterOrEqual(VectorMultiply(radiusDelta, radiusDelta), distanceSquared)){
        outDirectCopy = true;
        outCopyFirst = Vector4GreaterOrEqual(radius0, radius1);
        return outCopyFirst ? sphereValue0 : sphereValue1;
    }

    const SIMDVector distance = VectorSqrt(distanceSquared);
    const SIMDVector newRadius = VectorMultiply(
        VectorAdd(distance, VectorAdd(radius0, radius1)),
        s_SIMDOneHalf
    );
    SIMDVector newCenter = center0;
    if(Vector4Greater(distance, VectorReplicate(CollisionDetail::s_RayEpsilon)))
        newCenter = VectorMultiplyAdd(delta, VectorDivide(VectorSubtract(newRadius, radius0), distance), center0);

    return CollisionDetail::SphereCenterRadius(newCenter, newRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createMerged(
    BoundingSphere& outSphere,
    const BoundingSphere& sphere0,
    const BoundingSphere& sphere1
)noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    bool directCopy = false;
    bool copyFirst = true;
    const SIMDVector mergedValue = mergeSphereValues(LoadFloat(sphere0.centerRadius), LoadFloat(sphere1.centerRadius), directCopy, copyFirst);
    if(directCopy){
        outSphere = copyFirst ? sphere0 : sphere1;
        return;
    }

    StoreFloat(mergedValue, outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVector SIMDCALL BoundingSphere::sphereFromCenterExtentsValue(SIMDVector centerValue, SIMDVector extentsValue)noexcept{
    return CollisionDetail::SphereCenterRadius(centerValue, Vector3Length(extentsValue));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVector SIMDCALL BoundingSphere::sphereFromCornersValue(const SIMDVector* corners, usize cornerCount)noexcept{
    return CollisionDetail::CreateSphereFromVectorPoints(corners, cornerCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createFromBoundingBox(BoundingSphere& outSphere, const BoundingBox& box)noexcept{ // beginner: Loads box storage once, Stores sphere once.
    StoreFloat(sphereFromCenterExtentsValue(LoadFloat(box.center), LoadFloat(box.extents)), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createFromBoundingBox(BoundingSphere& outSphere, const BoundingOrientedBox& box)noexcept{ // beginner: Loads box storage once, Stores sphere once.
    StoreFloat(sphereFromCenterExtentsValue(LoadFloat(box.center), LoadFloat(box.extents)), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createFromPoints( // beginner: streams point storage, Stores sphere once.
    BoundingSphere& outSphere,
    const usize count,
    const Float3U* points,
    const usize stride
)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(count > 0u);
    SIMDVector centerVector = VectorZero();
    for(usize i = 0u; i < count; ++i)
        centerVector = VectorAdd(centerVector, LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, i)));
    centerVector = VectorDivide(centerVector, VectorReplicate(static_cast<f32>(count)));

    SIMDVector radiusSq = VectorZero();
    for(usize i = 0u; i < count; ++i){
        const SIMDVector delta = VectorSubtract(LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, i)), centerVector);
        radiusSq = VectorMax(radiusSq, Vector3LengthSq(delta));
    }

    StoreFloat(CollisionDetail::SphereCenterRadius(centerVector, VectorSqrt(radiusSq)), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createFromFrustum(BoundingSphere& outSphere, const BoundingFrustum& frustum)noexcept{ // beginner: Loads frustum storage once, Stores sphere once.
    SIMDVector corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        corners
    );
    StoreFloat(sphereFromCornersValue(corners, BoundingFrustum::s_CornerCount), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingBox::transformBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, const SIMDMatrix& matrix, SIMDVector& outCenter, SIMDVector& outExtents)noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::AabbCorners(boxCenter, boxExtents, corners);
    SIMDVector minBounds = Vector3Transform(corners[0], matrix);
    SIMDVector maxBounds = minBounds;
    for(u32 i = 1u; i < s_CornerCount; ++i)
        CollisionDetail::ExpandMinMax(Vector3Transform(corners[i], matrix), minBounds, maxBounds);
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingBox::transformBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, f32 scale, SIMDVector rotation, SIMDVector translation, SIMDVector& outCenter, SIMDVector& outExtents)noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::AabbCorners(boxCenter, boxExtents, corners);
    const SIMDVector scaleVector = VectorReplicate(scale);
    SIMDVector minBounds = VectorAdd(Vector3Rotate(VectorMultiply(corners[0], scaleVector), rotation), translation);
    SIMDVector maxBounds = minBounds;
    for(u32 i = 1u; i < s_CornerCount; ++i){
        const SIMDVector transformed = VectorAdd(Vector3Rotate(VectorMultiply(corners[i], scaleVector), rotation), translation);
        CollisionDetail::ExpandMinMax(transformed, minBounds, maxBounds);
    }
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingBox::transform(BoundingBox& outBox, const SIMDMatrix& matrix)const noexcept{ // beginner: Loads box storage once, Stores box once.
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    transformBoxValue(LoadFloat(center), LoadFloat(extents), matrix, centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingBox::transform(
    BoundingBox& outBox,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{ // beginner: Loads box storage once, Stores box once.
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    transformBoxValue(LoadFloat(center), LoadFloat(extents), scale, rotation, translation, centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingBox::cornersValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector* outCorners)noexcept{
    CollisionDetail::AabbCorners(boxCenter, boxExtents, outCorners);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::getCorners(Float3U* corners)const noexcept{ // beginner: Loads box once, Streams corners out.
    NWB_ASSERT(corners != nullptr);
    SIMDVector cornerVectors[s_CornerCount];
    cornersValue(LoadFloat(center), LoadFloat(extents), cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::containsPointValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector point)noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return Vector3GreaterOrEqual(point, minBounds) && Vector3LessOrEqual(point, maxBounds) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::contains(const SIMDVector point)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsPointValue(LoadFloat(center), LoadFloat(extents), point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::containsTriangleValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(points, CollisionDetail::s_TriangleVertexCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return intersectsTriangleValue(boxCenter, boxExtents, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsTriangleValue(LoadFloat(center), LoadFloat(extents), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::containsSphereValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector sphereValue)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    const SIMDVector sphereCenter = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(sphereCenter, minBounds, maxBounds);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(Vector3LengthSq(VectorSubtract(closestPoint, sphereCenter)), VectorMultiply(sphereRadius, sphereRadius))))
        return ContainmentType::Disjoint;

    if(Vector3GreaterOrEqual(VectorSubtract(sphereCenter, sphereRadius), minBounds) && Vector3LessOrEqual(VectorAdd(sphereCenter, sphereRadius), maxBounds))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingSphere& sphere)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsSphereValues(LoadFloat(center), LoadFloat(extents), LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::containsBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    SIMDVector otherMin{};
    SIMDVector otherMax{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    CollisionDetail::MinMaxFromCenterExtents(otherBoxCenter, otherBoxExtents, otherMin, otherMax);
    if(!CollisionDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax))
        return ContainmentType::Disjoint;
    if(Vector3GreaterOrEqual(otherMin, minBounds) && Vector3LessOrEqual(otherMax, maxBounds))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::containsOrientedBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents, const SIMDVector otherBoxOrientation)const noexcept{
    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(corners, BoundingOrientedBox::s_CornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return intersectsOrientedBoxValues(boxCenter, boxExtents, otherBoxCenter, otherBoxExtents, otherBoxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingOrientedBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::containsFrustumValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue)const noexcept{
    SIMDVector corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(
        frustumOrigin,
        frustumOrientation,
        rightSlopeValue,
        leftSlopeValue,
        topSlopeValue,
        bottomSlopeValue,
        nearPlaneValue,
        farPlaneValue,
        corners
    );
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(corners, BoundingFrustum::s_CornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectAxisAlignedBox(planes, boxCenter, boxExtents) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingFrustum& frustum)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsFrustumValues(LoadFloat(center), LoadFloat(extents), LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingSphere& sphere)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return sphere.intersectsBoxValues(LoadFloat(sphere.centerRadius), LoadFloat(center), LoadFloat(extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingBox::intersectsBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    SIMDVector otherMin{};
    SIMDVector otherMax{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    CollisionDetail::MinMaxFromCenterExtents(otherBoxCenter, otherBoxExtents, otherMin, otherMax);
    return CollisionDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingBox::intersectsOrientedBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents, const SIMDVector otherBoxOrientation)const noexcept{
    return CollisionDetail::ObbIntersectsObb(
        boxCenter,
        boxExtents,
        s_SIMDIdentityR3,
        otherBoxCenter,
        otherBoxExtents,
        otherBoxOrientation
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingOrientedBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingFrustum& frustum)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return frustum.intersectsBoxValues(LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadFloat(center), LoadFloat(extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingBox::intersectsTriangleValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return CollisionDetail::TriangleAabbOverlap(v0, v1, v2, minBounds, maxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingBox::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsTriangleValue(LoadFloat(center), LoadFloat(extents), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingBox::intersectsPlaneValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector plane)noexcept{
    SIMDVector outside{};
    SIMDVector inside{};
    CollisionDetail::FastIntersectAxisAlignedBoxPlane(boxCenter, boxExtents, plane, outside, inside);
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingBox::intersects(const SIMDVector plane)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsPlaneValue(LoadFloat(center), LoadFloat(extents), plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingBox::intersectsRayValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector origin, SIMDVector direction, f32& outDistance)noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter, boxExtents, minBounds, maxBounds);
    return CollisionDetail::RayIntersectsMinMax(origin, direction, minBounds, maxBounds, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingBox::intersects(
    SIMDVector origin,
    SIMDVector direction,
    f32& outDistance
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsRayValue(LoadFloat(center), LoadFloat(extents), origin, direction, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::containedByValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept{
    SIMDVector points[s_CornerCount];
    CollisionDetail::AabbCorners(boxCenter, boxExtents, points);
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(points, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::containedBy(
    SIMDVector plane0,
    SIMDVector plane1,
    SIMDVector plane2,
    SIMDVector plane3,
    SIMDVector plane4,
    SIMDVector plane5
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containedByValue(LoadFloat(center), LoadFloat(extents), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingBox::mergeBoxValues(SIMDVector boxCenter0, SIMDVector boxExtents0, SIMDVector boxCenter1, SIMDVector boxExtents1, SIMDVector& outCenter, SIMDVector& outExtents)noexcept{
    SIMDVector min0{};
    SIMDVector max0{};
    SIMDVector min1{};
    SIMDVector max1{};
    CollisionDetail::MinMaxFromCenterExtents(boxCenter0, boxExtents0, min0, max0);
    CollisionDetail::MinMaxFromCenterExtents(boxCenter1, boxExtents1, min1, max1);
    CollisionDetail::CenterExtentsFromMinMax(VectorMin(min0, min1), VectorMax(max0, max1), outCenter, outExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::createMerged(BoundingBox& outBox, const BoundingBox& box0, const BoundingBox& box1)noexcept{ // beginner: Loads boxes once, Stores box once.
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    mergeBoxValues(LoadFloat(box0.center), LoadFloat(box0.extents), LoadFloat(box1.center), LoadFloat(box1.extents), centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::createFromSphere(BoundingBox& outBox, const BoundingSphere& sphere)noexcept{ // beginner: Loads sphere storage once, Stores box once.
    const SIMDVector sphereValue = LoadFloat(sphere.centerRadius);
    StoreFloat(VectorSetW(CollisionDetail::SphereCenter(sphereValue), 0.0f), outBox.center);
    StoreFloat(VectorSetW(CollisionDetail::SphereRadius(sphereValue), 0.0f), outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingBox::createFromPoints(BoundingBox& outBox, const SIMDVector point0, const SIMDVector point1)noexcept{ // beginner: Stores box once from SIMD lanes.
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(VectorMin(point0, point1), VectorMax(point0, point1), centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::createFromPoints( // beginner: streams point storage, Stores box once.
    BoundingBox& outBox,
    const usize count,
    const Float3U* points,
    const usize stride
)noexcept{
    NWB_ASSERT(points != nullptr);
    NWB_ASSERT(count > 0u);
    SIMDVector minBounds = LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, 0u));
    SIMDVector maxBounds = minBounds;
    for(usize i = 1u; i < count; ++i)
        CollisionDetail::ExpandMinMax(LoadFloat(*CollisionDetail::StrideFloat3Pointer(points, stride, i)), minBounds, maxBounds);
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingOrientedBox::transformOrientedBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, const SIMDMatrix& matrix, SIMDVector& outCenter, SIMDVector& outExtents, SIMDVector& outOrientation, bool& outCollapsedToAxisAligned)noexcept{
    outCollapsedToAxisAligned = false;
    SIMDVector scale{};
    SIMDVector rotation{};
    SIMDVector translation{};
    if(!MatrixDecompose(scale, rotation, translation, matrix)){
        SIMDVector corners[s_CornerCount];
        CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
        SIMDVector minBounds = Vector3Transform(corners[0], matrix);
        SIMDVector maxBounds = minBounds;
        for(u32 i = 1u; i < s_CornerCount; ++i)
            CollisionDetail::ExpandMinMax(Vector3Transform(corners[i], matrix), minBounds, maxBounds);

        CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, outCenter, outExtents);
        outOrientation = s_SIMDIdentityR3;
        outCollapsedToAxisAligned = true;
        return;
    }

    outCenter = Vector3Transform(boxCenter, matrix);
    outExtents = VectorMultiply(boxExtents, VectorAbs(scale));
    outOrientation = QuaternionNormalize(QuaternionMultiply(boxOrientation, rotation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingOrientedBox::transform(BoundingOrientedBox& outBox, const SIMDMatrix& matrix)const noexcept{ // beginner: Loads box once, Stores box once.
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    SIMDVector orientationVector{};
    bool collapsedToAxisAligned = false;
    transformOrientedBoxValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), matrix, centerVector, extentsVector, orientationVector, collapsedToAxisAligned);
    if(collapsedToAxisAligned){
        StoreFloat(centerVector, outBox.center);
        StoreFloat(extentsVector, outBox.extents);
        outBox.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    StoreFloat(VectorSetW(centerVector, 0.0f), outBox.center);
    StoreFloat(VectorSetW(extentsVector, 0.0f), outBox.extents);
    StoreFloat(orientationVector, outBox.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingOrientedBox::transformOrientedBoxValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, f32 scale, SIMDVector rotation, SIMDVector translation, SIMDVector& outCenter, SIMDVector& outExtents, SIMDVector& outOrientation)noexcept{
    outCenter = VectorAdd(Vector3Rotate(VectorScale(boxCenter, scale), rotation), translation);
    outExtents = VectorScale(boxExtents, Abs(scale));
    outOrientation = QuaternionNormalize(QuaternionMultiply(boxOrientation, rotation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingOrientedBox::transform(
    BoundingOrientedBox& outBox,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{ // beginner: Loads box once, Stores box once.
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    SIMDVector orientationVector{};
    transformOrientedBoxValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), scale, rotation, translation, centerVector, extentsVector, orientationVector);
    StoreFloat(VectorSetW(centerVector, 0.0f), outBox.center);
    StoreFloat(VectorSetW(extentsVector, 0.0f), outBox.extents);
    StoreFloat(orientationVector, outBox.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingOrientedBox::cornersValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector* outCorners)noexcept{
    CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, outCorners);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::getCorners(Float3U* corners)const noexcept{ // beginner: Loads box once, Streams corners out.
    NWB_ASSERT(corners != nullptr);
    SIMDVector cornerVectors[s_CornerCount];
    cornersValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containsPointValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector point)noexcept{
    return CollisionDetail::PointInsideObb(point, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::contains(const SIMDVector point)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsPointValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containsTriangleValue(
    SIMDVector boxCenter,
    SIMDVector boxExtents,
    SIMDVector boxOrientation,
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2
)noexcept{
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    if(CollisionDetail::PointsInsideObb(points, CollisionDetail::s_TriangleVertexCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsTriangleValue(boxCenter, boxExtents, boxOrientation, v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsTriangleValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containsSphereValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation, const SIMDVector sphereValue)const noexcept{
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector localCenter = CollisionDetail::PointToObbLocal(CollisionDetail::SphereCenter(sphereValue), boxCenter, boxOrientation);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(localCenter, VectorNegate(boxExtents), boxExtents);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(Vector3LengthSq(VectorSubtract(closestPoint, localCenter)), VectorMultiply(sphereRadius, sphereRadius))))
        return ContainmentType::Disjoint;

    if(Vector3LessOrEqual(VectorAbs(localCenter), VectorSubtract(boxExtents, sphereRadius)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingSphere& sphere)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsSphereValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containsBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents)const noexcept{
    SIMDVector corners[BoundingBox::s_CornerCount];
    CollisionDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    if(CollisionDetail::PointsInsideObb(corners, BoundingBox::s_CornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containsOrientedBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents, const SIMDVector otherBoxOrientation)const noexcept{
    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    if(CollisionDetail::PointsInsideObb(corners, BoundingOrientedBox::s_CornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    return intersectsOrientedBoxValues(boxCenter, boxExtents, boxOrientation, otherBoxCenter, otherBoxExtents, otherBoxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingOrientedBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containsFrustumValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation, const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue)const noexcept{
    SIMDVector corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumCorners(
        frustumOrigin,
        frustumOrientation,
        rightSlopeValue,
        leftSlopeValue,
        topSlopeValue,
        bottomSlopeValue,
        nearPlaneValue,
        farPlaneValue,
        corners
    );
    if(CollisionDetail::PointsInsideObb(corners, BoundingFrustum::s_CornerCount, boxCenter, boxExtents, boxOrientation))
        return ContainmentType::Contains;
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectOrientedBox(planes, boxCenter, boxExtents, boxOrientation) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingFrustum& frustum)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsFrustumValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingSphere& sphere)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return sphere.intersectsOrientedBoxValues(LoadFloat(sphere.centerRadius), LoadFloat(center), LoadFloat(extents), LoadFloat(orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersectsBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents)const noexcept{
    return CollisionDetail::ObbIntersectsObb(
        boxCenter,
        boxExtents,
        boxOrientation,
        otherBoxCenter,
        otherBoxExtents,
        s_SIMDIdentityR3
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersectsOrientedBoxValues(const SIMDVector boxCenter, const SIMDVector boxExtents, const SIMDVector boxOrientation, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents, const SIMDVector otherBoxOrientation)const noexcept{
    return CollisionDetail::ObbIntersectsObb(
        boxCenter,
        boxExtents,
        boxOrientation,
        otherBoxCenter,
        otherBoxExtents,
        otherBoxOrientation
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingOrientedBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsOrientedBoxValues(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingFrustum& frustum)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return frustum.intersectsOrientedBoxValues(LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersectsTriangleValue(
    SIMDVector boxCenter,
    SIMDVector boxExtents,
    SIMDVector boxOrientation,
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2
)noexcept{
    const SIMDVector localV0 = CollisionDetail::PointToObbLocal(v0, boxCenter, boxOrientation);
    const SIMDVector localV1 = CollisionDetail::PointToObbLocal(v1, boxCenter, boxOrientation);
    const SIMDVector localV2 = CollisionDetail::PointToObbLocal(v2, boxCenter, boxOrientation);
    return CollisionDetail::TriangleAabbOverlap(localV0, localV1, localV2, VectorNegate(boxExtents), boxExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsTriangleValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingOrientedBox::intersectsPlaneValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector plane)noexcept{
    SIMDVector outside{};
    SIMDVector inside{};
    SIMDVector axis0{};
    SIMDVector axis1{};
    SIMDVector axis2{};
    CollisionDetail::ObbAxes(boxOrientation, axis0, axis1, axis2);
    CollisionDetail::FastIntersectOrientedBoxPlane(
        boxCenter,
        boxExtents,
        axis0,
        axis1,
        axis2,
        plane,
        outside,
        inside
    );
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingOrientedBox::intersects(const SIMDVector plane)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsPlaneValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersectsRayValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector origin, SIMDVector direction, f32& outDistance)noexcept{
    const SIMDVector localOrigin = CollisionDetail::PointToObbLocal(origin, boxCenter, boxOrientation);
    const SIMDVector localDirection = Vector3InverseRotate(direction, boxOrientation);
    return CollisionDetail::RayIntersectsMinMax(localOrigin, localDirection, VectorNegate(boxExtents), boxExtents, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersects(
    SIMDVector origin,
    SIMDVector direction,
    f32& outDistance
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsRayValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), origin, direction, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containedByValue(SIMDVector boxCenter, SIMDVector boxExtents, SIMDVector boxOrientation, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::ObbCorners(boxCenter, boxExtents, boxOrientation, corners);
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containedBy(
    SIMDVector plane0,
    SIMDVector plane1,
    SIMDVector plane2,
    SIMDVector plane3,
    SIMDVector plane4,
    SIMDVector plane5
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containedByValue(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), plane0, plane1, plane2, plane3, plane4, plane5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::createFromBoundingBox(BoundingOrientedBox& outBox, const BoundingBox& box)noexcept{ // beginner: Copies Float# storage only, no SIMD lanes.
    outBox.center = box.center;
    outBox.extents = box.extents;
    outBox.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::createFromPoints( // beginner: Delegates point-storage streaming to BoundingBox core.
    BoundingOrientedBox& outBox,
    const usize count,
    const Float3U* points,
    const usize stride
)noexcept{
    BoundingBox box;
    BoundingBox::createFromPoints(box, count, points, stride);
    createFromBoundingBox(outBox, box);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline BoundingFrustum::BoundingFrustum(const SIMDMatrix& projection, const bool rightHandedCoordinates)noexcept{ // beginner: Publishes SIMD lanes into Float# storage via createFromMatrix core.
    createFromMatrix(*this, projection, rightHandedCoordinates);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingFrustum::transformFrustumValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, const SIMDMatrix& matrix, SIMDVector& outOrigin, SIMDVector& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane)noexcept{
    SIMDVector scale{};
    SIMDVector rotation{};
    SIMDVector translation{};
    if(!MatrixDecompose(scale, rotation, translation, matrix)){
        scale = s_SIMDOne;
        rotation = s_SIMDIdentityR3;
        translation = Vector3Transform(VectorZero(), matrix);
    }

    const SIMDVector absScale = VectorAbs(scale);
    const SIMDVector maxScale = CollisionDetail::Vector3MaxComponent(absScale);
    const SIMDVector scaledPlanes = VectorMultiply(VectorSet(nearPlaneValue, farPlaneValue, 0.0f, 0.0f), maxScale);
    outOrigin = Vector3Transform(frustumOrigin, matrix);
    outOrientation = QuaternionNormalize(QuaternionMultiply(frustumOrientation, rotation));
    outRightSlope = rightSlopeValue;
    outLeftSlope = leftSlopeValue;
    outTopSlope = topSlopeValue;
    outBottomSlope = bottomSlopeValue;
    outNearPlane = VectorGetX(scaledPlanes);
    outFarPlane = VectorGetY(scaledPlanes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingFrustum::transform(BoundingFrustum& outFrustum, const SIMDMatrix& matrix)const noexcept{ // beginner: Loads frustum once, Stores frustum once.
    SIMDVector originVector{};
    SIMDVector orientationVector{};
    transformFrustumValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, matrix, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope, outFrustum.bottomSlope, outFrustum.nearPlane, outFrustum.farPlane);
    StoreFloat(VectorSetW(originVector, 0.0f), outFrustum.origin);
    StoreFloat(orientationVector, outFrustum.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingFrustum::transformFrustumValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, f32 scale, SIMDVector rotation, SIMDVector translation, SIMDVector& outOrigin, SIMDVector& outOrientation, f32& outRightSlope, f32& outLeftSlope, f32& outTopSlope, f32& outBottomSlope, f32& outNearPlane, f32& outFarPlane)noexcept{
    outOrigin = VectorAdd(Vector3Rotate(VectorScale(frustumOrigin, scale), rotation), translation);
    outOrientation = QuaternionNormalize(QuaternionMultiply(frustumOrientation, rotation));
    const SIMDVector scaledPlanes = VectorMultiply(
        VectorSet(nearPlaneValue, farPlaneValue, 0.0f, 0.0f),
        VectorAbs(VectorReplicate(scale))
    );
    outRightSlope = rightSlopeValue;
    outLeftSlope = leftSlopeValue;
    outTopSlope = topSlopeValue;
    outBottomSlope = bottomSlopeValue;
    outNearPlane = VectorGetX(scaledPlanes);
    outFarPlane = VectorGetY(scaledPlanes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingFrustum::transform(
    BoundingFrustum& outFrustum,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{ // beginner: Loads frustum once, Stores frustum once.
    SIMDVector originVector{};
    SIMDVector orientationVector{};
    transformFrustumValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, scale, rotation, translation, originVector, orientationVector, outFrustum.rightSlope, outFrustum.leftSlope, outFrustum.topSlope, outFrustum.bottomSlope, outFrustum.nearPlane, outFrustum.farPlane);
    StoreFloat(VectorSetW(originVector, 0.0f), outFrustum.origin);
    StoreFloat(orientationVector, outFrustum.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingFrustum::cornersValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector* outCorners)noexcept{
    CollisionDetail::FrustumCorners(
        frustumOrigin,
        frustumOrientation,
        rightSlopeValue,
        leftSlopeValue,
        topSlopeValue,
        bottomSlopeValue,
        nearPlaneValue,
        farPlaneValue,
        outCorners
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingFrustum::getCorners(Float3U* corners)const noexcept{ // beginner: Loads frustum once, Streams corners out.
    NWB_ASSERT(corners != nullptr);
    SIMDVector cornerVectors[s_CornerCount];
    cornersValue(
        LoadFloat(origin),
        LoadFloat(orientation),
        rightSlope,
        leftSlope,
        topSlope,
        bottomSlope,
        nearPlane,
        farPlane,
        cornerVectors
    );
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::containsPointValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector point)noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    for(const SIMDVector plane : planes){
        if(CollisionDetail::Vector4AllTrue(VectorLess(CollisionDetail::PlaneDistance(plane, point), VectorZero())))
            return ContainmentType::Disjoint;
    }
    return ContainmentType::Contains;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::contains(const SIMDVector point)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsPointValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::containsTriangleValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::ContainmentFromPlaneTests(points, CollisionDetail::s_TriangleVertexCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsTriangleValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, v0, v1, v2);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::containsSphereValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector sphereValue)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    SIMDVector anyIntersecting = VectorFalseInt();
    for(const SIMDVector plane : planes){
        SIMDVector outside{};
        SIMDVector inside{};
        CollisionDetail::FastIntersectSpherePlane(centerVector, sphereRadius, plane, outside, inside);
        if(CollisionDetail::Vector4AllTrue(outside))
            return ContainmentType::Disjoint;
        anyIntersecting = VectorOrInt(anyIntersecting, VectorEqualInt(inside, VectorFalseInt()));
    }
    return CollisionDetail::Vector4AllTrue(anyIntersecting) ? ContainmentType::Intersects : ContainmentType::Contains;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingSphere& sphere)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsSphereValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::containsBoxValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector corners[BoundingBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::AabbCorners(otherBoxCenter, otherBoxExtents, corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::containsOrientedBoxValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents, const SIMDVector otherBoxOrientation)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::ObbCorners(otherBoxCenter, otherBoxExtents, otherBoxOrientation, corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingOrientedBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingOrientedBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsOrientedBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::containsFrustumValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector otherFrustumOrigin, const SIMDVector otherFrustumOrientation, const f32 otherRightSlope, const f32 otherLeftSlope, const f32 otherTopSlope, const f32 otherBottomSlope, const f32 otherNearPlane, const f32 otherFarPlane)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::FrustumCorners(
        otherFrustumOrigin,
        otherFrustumOrientation,
        otherRightSlope,
        otherLeftSlope,
        otherTopSlope,
        otherBottomSlope,
        otherNearPlane,
        otherFarPlane,
        corners
    );
    const ContainmentType::Enum containment = CollisionDetail::ContainmentFromPlaneTests(corners, BoundingFrustum::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
    if(containment != ContainmentType::Intersects)
        return containment;

    SIMDVector otherPlanes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector thisCorners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumPlanes(
        otherFrustumOrigin,
        otherFrustumOrientation,
        otherRightSlope,
        otherLeftSlope,
        otherTopSlope,
        otherBottomSlope,
        otherNearPlane,
        otherFarPlane,
        otherPlanes
    );
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, thisCorners);
    return CollisionDetail::FrustumPlanesIntersectPoints(otherPlanes, thisCorners, BoundingFrustum::s_CornerCount) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingFrustum& frustum)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containsFrustumValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingFrustum::intersectsSphereValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector sphereValue)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectSphere(planes, sphereValue);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingSphere& sphere)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsSphereValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingFrustum::intersectsBoxValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectAxisAlignedBox(planes, otherBoxCenter, otherBoxExtents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingFrustum::intersectsOrientedBoxValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector otherBoxCenter, const SIMDVector otherBoxExtents, const SIMDVector otherBoxOrientation)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    return CollisionDetail::FrustumPlanesIntersectOrientedBox(planes, otherBoxCenter, otherBoxExtents, otherBoxOrientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingOrientedBox& box)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsOrientedBoxValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingFrustum::intersectsFrustumValues(const SIMDVector frustumOrigin, const SIMDVector frustumOrientation, const f32 rightSlopeValue, const f32 leftSlopeValue, const f32 topSlopeValue, const f32 bottomSlopeValue, const f32 nearPlaneValue, const f32 farPlaneValue, const SIMDVector otherFrustumOrigin, const SIMDVector otherFrustumOrientation, const f32 otherRightSlope, const f32 otherLeftSlope, const f32 otherTopSlope, const f32 otherBottomSlope, const f32 otherNearPlane, const f32 otherFarPlane)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector otherPlanes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    CollisionDetail::FrustumPlanes(
        otherFrustumOrigin,
        otherFrustumOrientation,
        otherRightSlope,
        otherLeftSlope,
        otherTopSlope,
        otherBottomSlope,
        otherNearPlane,
        otherFarPlane,
        otherPlanes
    );

    SIMDVector corners[s_CornerCount];
    SIMDVector otherCorners[s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    CollisionDetail::FrustumCorners(
        otherFrustumOrigin,
        otherFrustumOrientation,
        otherRightSlope,
        otherLeftSlope,
        otherTopSlope,
        otherBottomSlope,
        otherNearPlane,
        otherFarPlane,
        otherCorners
    );

    return CollisionDetail::FrustumPlanesIntersectPoints(planes, otherCorners, s_CornerCount)
        && CollisionDetail::FrustumPlanesIntersectPoints(otherPlanes, corners, s_CornerCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingFrustum& frustum)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsFrustumValues(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, LoadFloat(frustum.origin), LoadFloat(frustum.orientation), frustum.rightSlope, frustum.leftSlope, frustum.topSlope, frustum.bottomSlope, frustum.nearPlane, frustum.farPlane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingFrustum::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    return contains(v0, v1, v2) != ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingFrustum::intersectsPlaneValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector plane)noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    SIMDVector outside{};
    SIMDVector inside{};
    CollisionDetail::FastIntersectPointsPlane(corners, s_CornerCount, plane, outside, inside);
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingFrustum::intersects(const SIMDVector plane)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsPlaneValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingFrustum::intersectsRayValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector rayOrigin, SIMDVector direction, f32& outDistance)noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, planes);
    const SIMDVector zero = VectorZero();
    const SIMDVector rayEpsilon = VectorReplicate(CollisionDetail::s_RayEpsilon);
    SIMDVector tMin = zero;
    SIMDVector tMax = VectorReplicate(s_MaxF32);
    for(const SIMDVector plane : planes){
        const SIMDVector distance = CollisionDetail::PlaneDistance(plane, rayOrigin);
        const SIMDVector denominator = Vector3Dot(plane, direction);
        if(Vector4LessOrEqual(VectorAbs(denominator), rayEpsilon)){
            if(Vector4Less(distance, zero))
                return false;
            continue;
        }

        const SIMDVector t = VectorDivide(VectorNegate(distance), denominator);
        if(Vector4Greater(denominator, zero))
            tMin = VectorMax(tMin, t);
        else
            tMax = VectorMin(tMax, t);
        if(Vector4Greater(tMin, tMax))
            return false;
    }

    outDistance = VectorGetX(tMin);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingFrustum::intersects(
    const SIMDVector rayOrigin,
    const SIMDVector direction,
    f32& outDistance
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return intersectsRayValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, rayOrigin, direction, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::containedByValue(SIMDVector frustumOrigin, SIMDVector frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue, SIMDVector plane0, SIMDVector plane1, SIMDVector plane2, SIMDVector plane3, SIMDVector plane4, SIMDVector plane5)noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::FrustumCorners(frustumOrigin, frustumOrientation, rightSlopeValue, leftSlopeValue, topSlopeValue, bottomSlopeValue, nearPlaneValue, farPlaneValue, corners);
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::containedBy(
    SIMDVector plane0,
    SIMDVector plane1,
    SIMDVector plane2,
    SIMDVector plane3,
    SIMDVector plane4,
    SIMDVector plane5
)const noexcept{ // beginner: Loads member storage once into pure-SIMD core.
    return containedByValue(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, plane0, plane1, plane2, plane3, plane4, plane5);
}


inline void BoundingFrustum::getPlanes( // beginner: Loads frustum once, publishes SIMD planes to caller lanes.
    SIMDVector* nearPlaneOut,
    SIMDVector* farPlaneOut,
    SIMDVector* rightPlaneOut,
    SIMDVector* leftPlaneOut,
    SIMDVector* topPlaneOut,
    SIMDVector* bottomPlaneOut
)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingFrustum::createFromMatrix( // beginner: Reads SIMDMatrix lanes, publishes Float# slopes/planes.
    BoundingFrustum& outFrustum,
    const SIMDMatrix& projection,
    const bool rightHandedCoordinates
)noexcept{
    outFrustum.origin = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    outFrustum.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);

    const SIMDVector m00 = VectorSplatX(projection.v[0]);
    const SIMDVector m11 = VectorSplatY(projection.v[1]);
    const SIMDVector m02 = VectorSplatZ(projection.v[0]);
    const SIMDVector m12 = VectorSplatZ(projection.v[1]);
    const SIMDVector slopes = VectorDivide(
        VectorMergeX(
            VectorSubtract(s_SIMDOne, m02),
            VectorSubtract(s_SIMDNegativeOne, m02),
            VectorSubtract(s_SIMDOne, m12),
            VectorSubtract(s_SIMDNegativeOne, m12)
        ),
        VectorMergeX(m00, m00, m11, m11)
    );
    outFrustum.rightSlope = VectorGetX(slopes);
    outFrustum.leftSlope = VectorGetY(slopes);
    outFrustum.topSlope = VectorGetZ(slopes);
    outFrustum.bottomSlope = VectorGetW(slopes);

    const SIMDVector m22 = VectorSplatZ(projection.v[2]);
    const SIMDVector m23 = VectorSplatW(projection.v[2]);
    const SIMDVector planeDistances = rightHandedCoordinates
        ? VectorDivide(
            VectorMergeX(m23, m23, VectorZero(), VectorZero()),
            VectorMergeX(m22, VectorAdd(m22, s_SIMDOne), s_SIMDOne, s_SIMDOne)
        )
        : VectorDivide(
            VectorMergeX(VectorNegate(m23), m23, VectorZero(), VectorZero()),
            VectorMergeX(m22, VectorSubtract(s_SIMDOne, m22), s_SIMDOne, s_SIMDOne)
        )
    ;
    outFrustum.nearPlane = VectorGetX(planeDistances);
    outFrustum.farPlane = VectorGetY(planeDistances);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

