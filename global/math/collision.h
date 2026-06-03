// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ContainmentType{
    enum Enum : u8{
        Disjoint = 0u,
        Intersects = 1u,
        Contains = 2u,
    };
};

namespace PlaneIntersectionType{
    enum Enum : u8{
        Front = 0u,
        Intersecting = 1u,
        Back = 2u,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BoundingBox;
struct BoundingOrientedBox;
struct BoundingFrustum;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PlaneTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVector SIMDCALL Distance(SIMDVector plane, SIMDVector point)noexcept;
[[nodiscard]] SIMDVector SIMDCALL FromPointNormal(SIMDVector normal, SIMDVector point, SIMDVector fallbackNormal)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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


namespace AabbTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool SIMDCALL Valid(SIMDVector minBounds, SIMDVector maxBounds)noexcept;
void SIMDCALL Reset(SIMDVector& outMinBounds, SIMDVector& outMaxBounds)noexcept;
void SIMDCALL Expand(SIMDVector point, SIMDVector& inOutMinBounds, SIMDVector& inOutMaxBounds)noexcept;
void SIMDCALL ExpandTriangle(
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2,
    SIMDVector& inOutMinBounds,
    SIMDVector& inOutMaxBounds
)noexcept;
[[nodiscard]] bool SIMDCALL Intersects(
    SIMDVector lhsMinBounds,
    SIMDVector lhsMaxBounds,
    SIMDVector rhsMinBounds,
    SIMDVector rhsMaxBounds
)noexcept;
[[nodiscard]] SIMDVector SIMDCALL Center(SIMDVector minBounds, SIMDVector maxBounds)noexcept;
[[nodiscard]] SIMDVector SIMDCALL Extents(SIMDVector minBounds, SIMDVector maxBounds)noexcept;
[[nodiscard]] f32 SIMDCALL Radius(SIMDVector minBounds, SIMDVector maxBounds)noexcept;
[[nodiscard]] bool SIMDCALL Transform(
    const SIMDMatrix& localToWorld,
    SIMDVector localMinBounds,
    SIMDVector localMaxBounds,
    SIMDVector& outMinBounds,
    SIMDVector& outMaxBounds
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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

    void SIMDCALL transform(BoundingSphere& outSphere, const SIMDMatrix& matrix)const noexcept;
    void SIMDCALL transform(BoundingSphere& outSphere, f32 scale, SIMDVector rotation, SIMDVector translation)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector point)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
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

    void SIMDCALL transform(BoundingBox& outBox, const SIMDMatrix& matrix)const noexcept;
    void SIMDCALL transform(BoundingBox& outBox, f32 scale, SIMDVector rotation, SIMDVector translation)const noexcept;
    void getCorners(Float3U* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector point)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
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

    void SIMDCALL transform(BoundingOrientedBox& outBox, const SIMDMatrix& matrix)const noexcept;
    void SIMDCALL transform(BoundingOrientedBox& outBox, f32 scale, SIMDVector rotation, SIMDVector translation)const noexcept;
    void getCorners(Float3U* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector point)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
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

    void SIMDCALL transform(BoundingFrustum& outFrustum, const SIMDMatrix& matrix)const noexcept;
    void SIMDCALL transform(BoundingFrustum& outFrustum, f32 scale, SIMDVector rotation, SIMDVector translation)const noexcept;
    void getCorners(Float3U* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector point)const noexcept;
    [[nodiscard]] ContainmentType::Enum SIMDCALL contains(SIMDVector v0, SIMDVector v1, SIMDVector v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingFrustum& frustum)const noexcept;

    [[nodiscard]] bool intersects(const BoundingSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingFrustum& frustum)const noexcept;
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


namespace TriangleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVector SIMDCALL EdgeCross2D(SIMDVector a, SIMDVector b, SIMDVector c)noexcept;
[[nodiscard]] SIMDVector SIMDCALL SignedArea2D(SIMDVector a, SIMDVector b, SIMDVector c)noexcept;
[[nodiscard]] SIMDVector SIMDCALL AreaNormal(SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept;
[[nodiscard]] bool SIMDCALL ContainsPoint2D(
    SIMDVector point,
    SIMDVector a,
    SIMDVector b,
    SIMDVector c,
    SIMDVector tolerance
)noexcept;

[[nodiscard]] bool SIMDCALL Intersects(
    SIMDVector origin,
    SIMDVector direction,
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2,
    f32& outDistance
)noexcept;

[[nodiscard]] bool SIMDCALL Intersects(
    SIMDVector a0,
    SIMDVector a1,
    SIMDVector a2,
    SIMDVector b0,
    SIMDVector b1,
    SIMDVector b2
)noexcept;

[[nodiscard]] PlaneIntersectionType::Enum SIMDCALL Intersects(
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2,
    SIMDVector plane
)noexcept;

[[nodiscard]] ContainmentType::Enum SIMDCALL ContainedBy(
    SIMDVector v0,
    SIMDVector v1,
    SIMDVector v2,
    SIMDVector plane0,
    SIMDVector plane1,
    SIMDVector plane2,
    SIMDVector plane3,
    SIMDVector plane4,
    SIMDVector plane5
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_MATH_COLLISION_INCLUDE_INLINE
#include "collision.inl"
#undef NWB_MATH_COLLISION_INCLUDE_INLINE


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
