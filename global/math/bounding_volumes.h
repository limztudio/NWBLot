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


inline void SIMDCALL BoundingSphere::transform(BoundingSphere& outSphere, const SIMDMatrix& matrix)const noexcept{
    SIMDVector scale{};
    SIMDVector rotation{};
    SIMDVector translation{};
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = VectorSplatW(sphereValue);
    if(MatrixDecompose(scale, rotation, translation, matrix)){
        const SIMDVector absScale = VectorAbs(scale);
        const SIMDVector maxScale = CollisionDetail::Vector3MaxComponent(absScale);
        StoreFloat(
            CollisionDetail::SphereCenterRadius(Vector3Transform(centerVector, matrix), VectorMultiply(sphereRadius, maxScale)),
            outSphere.centerRadius
        );
        return;
    }

    const SIMDVector maxScaleVector = VectorMax(Vector3Length(matrix.v[0]), VectorMax(Vector3Length(matrix.v[1]), Vector3Length(matrix.v[2])));
    StoreFloat(
        CollisionDetail::SphereCenterRadius(Vector3Transform(centerVector, matrix), VectorMultiply(sphereRadius, maxScaleVector)),
        outSphere.centerRadius
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingSphere::transform(
    BoundingSphere& outSphere,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector transformedCenter = VectorAdd(Vector3Rotate(VectorScale(CollisionDetail::SphereCenter(sphereValue), scale), rotation), translation);
    StoreFloat(
        CollisionDetail::SphereCenterRadius(transformedCenter, VectorScale(VectorSplatW(sphereValue), Abs(scale))),
        outSphere.centerRadius
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::contains(const SIMDVector point)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector delta = VectorSubtract(point, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(delta), VectorMultiply(sphereRadius, sphereRadius))) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    if(CollisionDetail::PointsInsideSphere(points, CollisionDetail::s_TriangleVertexCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return intersects(v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingSphere& sphere)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector otherSphereValue = LoadFloat(sphere.centerRadius);
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


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingBox& box)const noexcept{
    if(!intersects(box))
        return ContainmentType::Disjoint;

    SIMDVector corners[BoundingBox::s_CornerCount];
    CollisionDetail::AabbCorners(LoadFloat(box.center), LoadFloat(box.extents), corners);
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    if(CollisionDetail::PointsInsideSphere(corners, BoundingBox::s_CornerCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingOrientedBox& box)const noexcept{
    if(!intersects(box))
        return ContainmentType::Disjoint;

    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation), corners);
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    if(CollisionDetail::PointsInsideSphere(corners, BoundingOrientedBox::s_CornerCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingSphere::contains(const BoundingFrustum& frustum)const noexcept{
    if(!intersects(frustum))
        return ContainmentType::Disjoint;

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
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    if(CollisionDetail::PointsInsideSphere(corners, BoundingFrustum::s_CornerCount, CollisionDetail::SphereCenter(sphereValue), CollisionDetail::SphereRadius(sphereValue)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingSphere& sphere)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector otherSphereValue = LoadFloat(sphere.centerRadius);
    const SIMDVector radiusSum = VectorAdd(CollisionDetail::SphereRadius(sphereValue), CollisionDetail::SphereRadius(otherSphereValue));
    const SIMDVector delta = VectorSubtract(CollisionDetail::SphereCenter(otherSphereValue), CollisionDetail::SphereCenter(sphereValue));
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(delta), VectorMultiply(radiusSum, radiusSum)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingBox& box)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(box.center), LoadFloat(box.extents), minBounds, maxBounds);
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(centerVector, minBounds, maxBounds);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, centerVector)), VectorMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingOrientedBox& box)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector localCenter = CollisionDetail::PointToObbLocal(CollisionDetail::SphereCenter(sphereValue), LoadFloat(box.center), LoadFloat(box.orientation));
    const SIMDVector extents = LoadFloat(box.extents);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(localCenter, VectorNegate(extents), extents);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, localCenter)), VectorMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingSphere::intersects(const BoundingFrustum& frustum)const noexcept{
    return frustum.intersects(*this);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnTriangle(centerVector, v0, v1, v2);
    return CollisionDetail::Vector4AllTrue(VectorLessOrEqual(Vector3LengthSq(VectorSubtract(closestPoint, centerVector)), VectorMultiply(sphereRadius, sphereRadius)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingSphere::intersects(const SIMDVector plane)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
    const SIMDVector distance = CollisionDetail::PlaneDistance(plane, CollisionDetail::SphereCenter(sphereValue));
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(distance, sphereRadius)))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(VectorLess(distance, VectorNegate(sphereRadius))))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingSphere::intersects(
    const SIMDVector origin,
    const SIMDVector direction,
    f32& outDistance
)const noexcept{
    const SIMDVector sphereValue = LoadFloat(centerRadius);
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


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingSphere::containedBy(
    const SIMDVector plane0,
    const SIMDVector plane1,
    const SIMDVector plane2,
    const SIMDVector plane3,
    const SIMDVector plane4,
    const SIMDVector plane5
)const noexcept{
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    const SIMDVector sphereValue = LoadFloat(centerRadius);
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


inline void BoundingSphere::createMerged(
    BoundingSphere& outSphere,
    const BoundingSphere& sphere0,
    const BoundingSphere& sphere1
)noexcept{
    const SIMDVector sphereValue0 = LoadFloat(sphere0.centerRadius);
    const SIMDVector sphereValue1 = LoadFloat(sphere1.centerRadius);
    const SIMDVector center0 = CollisionDetail::SphereCenter(sphereValue0);
    const SIMDVector center1 = CollisionDetail::SphereCenter(sphereValue1);
    const SIMDVector radius0 = VectorSplatW(sphereValue0);
    const SIMDVector radius1 = VectorSplatW(sphereValue1);
    const SIMDVector delta = VectorSubtract(center1, center0);
    const SIMDVector distanceSquared = Vector3LengthSq(delta);
    const SIMDVector radiusDelta = VectorSubtract(radius0, radius1);

    if(Vector4GreaterOrEqual(VectorMultiply(radiusDelta, radiusDelta), distanceSquared)){
        outSphere = Vector4GreaterOrEqual(radius0, radius1) ? sphere0 : sphere1;
        return;
    }

    const SIMDVector distance = VectorSqrt(distanceSquared);
    const SIMDVector newRadius = VectorMultiply(
        VectorAdd(distance, VectorAdd(radius0, radius1)),
        s_SIMDOneHalf
    );
    SIMDVector newCenter = center0;
    if(Vector4Greater(distance, VectorReplicate(CollisionDetail::s_RayEpsilon)))
        newCenter = VectorMultiplyAdd(delta, VectorDivide(VectorSubtract(newRadius, radius0), distance), center0);

    StoreFloat(CollisionDetail::SphereCenterRadius(newCenter, newRadius), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createFromBoundingBox(BoundingSphere& outSphere, const BoundingBox& box)noexcept{
    StoreFloat(CollisionDetail::SphereCenterRadius(LoadFloat(box.center), Vector3Length(LoadFloat(box.extents))), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createFromBoundingBox(BoundingSphere& outSphere, const BoundingOrientedBox& box)noexcept{
    StoreFloat(CollisionDetail::SphereCenterRadius(LoadFloat(box.center), Vector3Length(LoadFloat(box.extents))), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingSphere::createFromPoints(
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


inline void BoundingSphere::createFromFrustum(BoundingSphere& outSphere, const BoundingFrustum& frustum)noexcept{
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
    StoreFloat(CollisionDetail::CreateSphereFromVectorPoints(corners, BoundingFrustum::s_CornerCount), outSphere.centerRadius);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingBox::transform(BoundingBox& outBox, const SIMDMatrix& matrix)const noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::AabbCorners(LoadFloat(center), LoadFloat(extents), corners);
    SIMDVector minBounds = Vector3Transform(corners[0], matrix);
    SIMDVector maxBounds = minBounds;
    for(u32 i = 1u; i < s_CornerCount; ++i)
        CollisionDetail::ExpandMinMax(Vector3Transform(corners[i], matrix), minBounds, maxBounds);
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingBox::transform(
    BoundingBox& outBox,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::AabbCorners(LoadFloat(center), LoadFloat(extents), corners);
    const SIMDVector scaleVector = VectorReplicate(scale);
    SIMDVector minBounds = VectorAdd(Vector3Rotate(VectorMultiply(corners[0], scaleVector), rotation), translation);
    SIMDVector maxBounds = minBounds;
    for(u32 i = 1u; i < s_CornerCount; ++i){
        const SIMDVector transformed = VectorAdd(Vector3Rotate(VectorMultiply(corners[i], scaleVector), rotation), translation);
        CollisionDetail::ExpandMinMax(transformed, minBounds, maxBounds);
    }
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::getCorners(Float3U* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVector cornerVectors[s_CornerCount];
    CollisionDetail::AabbCorners(LoadFloat(center), LoadFloat(extents), cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::contains(const SIMDVector point)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    return Vector3GreaterOrEqual(point, minBounds) && Vector3LessOrEqual(point, maxBounds) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(points, CollisionDetail::s_TriangleVertexCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return intersects(v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingSphere& sphere)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    const SIMDVector sphereValue = LoadFloat(sphere.centerRadius);
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


[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingBox& box)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    SIMDVector otherMin{};
    SIMDVector otherMax{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(box.center), LoadFloat(box.extents), otherMin, otherMax);
    if(!CollisionDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax))
        return ContainmentType::Disjoint;
    if(Vector3GreaterOrEqual(otherMin, minBounds) && Vector3LessOrEqual(otherMax, maxBounds))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingOrientedBox& box)const noexcept{
    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation), corners);
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(corners, BoundingOrientedBox::s_CornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return intersects(box) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingBox::contains(const BoundingFrustum& frustum)const noexcept{
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
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    if(CollisionDetail::PointsInsideMinMax(corners, BoundingFrustum::s_CornerCount, minBounds, maxBounds))
        return ContainmentType::Contains;
    return intersects(frustum) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingSphere& sphere)const noexcept{
    return sphere.intersects(*this);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingBox& box)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    SIMDVector otherMin{};
    SIMDVector otherMax{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(box.center), LoadFloat(box.extents), otherMin, otherMax);
    return CollisionDetail::MinMaxIntersects(minBounds, maxBounds, otherMin, otherMax);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingOrientedBox& box)const noexcept{
    return CollisionDetail::ObbIntersectsObb(
        LoadFloat(center),
        LoadFloat(extents),
        s_SIMDIdentityR3,
        LoadFloat(box.center),
        LoadFloat(box.extents),
        LoadFloat(box.orientation)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingBox::intersects(const BoundingFrustum& frustum)const noexcept{
    return frustum.intersects(*this);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingBox::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    return CollisionDetail::TriangleAabbOverlap(v0, v1, v2, minBounds, maxBounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingBox::intersects(const SIMDVector plane)const noexcept{
    SIMDVector outside{};
    SIMDVector inside{};
    CollisionDetail::FastIntersectAxisAlignedBoxPlane(LoadFloat(center), LoadFloat(extents), plane, outside, inside);
    if(CollisionDetail::Vector4AllTrue(inside))
        return PlaneIntersectionType::Front;
    if(CollisionDetail::Vector4AllTrue(outside))
        return PlaneIntersectionType::Back;
    return PlaneIntersectionType::Intersecting;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingBox::intersects(
    const SIMDVector origin,
    const SIMDVector direction,
    f32& outDistance
)const noexcept{
    SIMDVector minBounds{};
    SIMDVector maxBounds{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(center), LoadFloat(extents), minBounds, maxBounds);
    return CollisionDetail::RayIntersectsMinMax(origin, direction, minBounds, maxBounds, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingBox::containedBy(
    const SIMDVector plane0,
    const SIMDVector plane1,
    const SIMDVector plane2,
    const SIMDVector plane3,
    const SIMDVector plane4,
    const SIMDVector plane5
)const noexcept{
    SIMDVector points[s_CornerCount];
    CollisionDetail::AabbCorners(LoadFloat(center), LoadFloat(extents), points);
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(points, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::createMerged(BoundingBox& outBox, const BoundingBox& box0, const BoundingBox& box1)noexcept{
    SIMDVector min0{};
    SIMDVector max0{};
    SIMDVector min1{};
    SIMDVector max1{};
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(box0.center), LoadFloat(box0.extents), min0, max0);
    CollisionDetail::MinMaxFromCenterExtents(LoadFloat(box1.center), LoadFloat(box1.extents), min1, max1);
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(VectorMin(min0, min1), VectorMax(max0, max1), centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::createFromSphere(BoundingBox& outBox, const BoundingSphere& sphere)noexcept{
    const SIMDVector sphereValue = LoadFloat(sphere.centerRadius);
    const SIMDVector centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVector extentsVector = CollisionDetail::SphereRadius(sphereValue);
    StoreFloat(VectorSetW(centerVector, 0.0f), outBox.center);
    StoreFloat(VectorSetW(extentsVector, 0.0f), outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingBox::createFromPoints(BoundingBox& outBox, const SIMDVector point0, const SIMDVector point1)noexcept{
    SIMDVector centerVector{};
    SIMDVector extentsVector{};
    CollisionDetail::CenterExtentsFromMinMax(VectorMin(point0, point1), VectorMax(point0, point1), centerVector, extentsVector);
    StoreFloat(centerVector, outBox.center);
    StoreFloat(extentsVector, outBox.extents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingBox::createFromPoints(
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


inline void SIMDCALL BoundingOrientedBox::transform(BoundingOrientedBox& outBox, const SIMDMatrix& matrix)const noexcept{
    SIMDVector scale{};
    SIMDVector rotation{};
    SIMDVector translation{};
    if(!MatrixDecompose(scale, rotation, translation, matrix)){
        SIMDVector corners[s_CornerCount];
        CollisionDetail::ObbCorners(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), corners);
        SIMDVector minBounds = Vector3Transform(corners[0], matrix);
        SIMDVector maxBounds = minBounds;
        for(u32 i = 1u; i < s_CornerCount; ++i)
            CollisionDetail::ExpandMinMax(Vector3Transform(corners[i], matrix), minBounds, maxBounds);

        SIMDVector centerVector{};
        SIMDVector extentsVector{};
        CollisionDetail::CenterExtentsFromMinMax(minBounds, maxBounds, centerVector, extentsVector);
        StoreFloat(centerVector, outBox.center);
        StoreFloat(extentsVector, outBox.extents);
        outBox.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    const SIMDVector centerVector = Vector3Transform(LoadFloat(center), matrix);
    const SIMDVector extentsVector = VectorMultiply(LoadFloat(extents), VectorAbs(scale));
    const SIMDVector orientationVector = QuaternionNormalize(QuaternionMultiply(LoadFloat(orientation), rotation));
    StoreFloat(VectorSetW(centerVector, 0.0f), outBox.center);
    StoreFloat(VectorSetW(extentsVector, 0.0f), outBox.extents);
    StoreFloat(orientationVector, outBox.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingOrientedBox::transform(
    BoundingOrientedBox& outBox,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{
    const SIMDVector centerVector = VectorAdd(Vector3Rotate(VectorScale(LoadFloat(center), scale), rotation), translation);
    const SIMDVector extentsVector = VectorScale(LoadFloat(extents), Abs(scale));
    const SIMDVector orientationVector = QuaternionNormalize(QuaternionMultiply(LoadFloat(orientation), rotation));
    StoreFloat(VectorSetW(centerVector, 0.0f), outBox.center);
    StoreFloat(VectorSetW(extentsVector, 0.0f), outBox.extents);
    StoreFloat(orientationVector, outBox.orientation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::getCorners(Float3U* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVector cornerVectors[s_CornerCount];
    CollisionDetail::ObbCorners(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), cornerVectors);
    for(u32 i = 0u; i < s_CornerCount; ++i)
        StoreFloat(VectorSetW(cornerVectors[i], 0.0f), corners[i]);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::contains(const SIMDVector point)const noexcept{
    return CollisionDetail::PointInsideObb(point, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation)) ? ContainmentType::Contains : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    if(CollisionDetail::PointsInsideObb(points, CollisionDetail::s_TriangleVertexCount, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation)))
        return ContainmentType::Contains;
    return intersects(v0, v1, v2) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingSphere& sphere)const noexcept{
    const SIMDVector sphereValue = LoadFloat(sphere.centerRadius);
    const SIMDVector sphereRadius = CollisionDetail::SphereRadius(sphereValue);
    const SIMDVector localCenter = CollisionDetail::PointToObbLocal(CollisionDetail::SphereCenter(sphereValue), LoadFloat(center), LoadFloat(orientation));
    const SIMDVector extentsVector = LoadFloat(extents);
    const SIMDVector closestPoint = CollisionDetail::ClosestPointOnMinMax(localCenter, VectorNegate(extentsVector), extentsVector);
    if(CollisionDetail::Vector4AllTrue(VectorGreater(Vector3LengthSq(VectorSubtract(closestPoint, localCenter)), VectorMultiply(sphereRadius, sphereRadius))))
        return ContainmentType::Disjoint;

    if(Vector3LessOrEqual(VectorAbs(localCenter), VectorSubtract(extentsVector, sphereRadius)))
        return ContainmentType::Contains;
    return ContainmentType::Intersects;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingBox& box)const noexcept{
    SIMDVector corners[BoundingBox::s_CornerCount];
    CollisionDetail::AabbCorners(LoadFloat(box.center), LoadFloat(box.extents), corners);
    if(CollisionDetail::PointsInsideObb(corners, BoundingBox::s_CornerCount, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation)))
        return ContainmentType::Contains;
    return intersects(box) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingOrientedBox& box)const noexcept{
    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::ObbCorners(LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation), corners);
    if(CollisionDetail::PointsInsideObb(corners, BoundingOrientedBox::s_CornerCount, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation)))
        return ContainmentType::Contains;
    return intersects(box) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingOrientedBox::contains(const BoundingFrustum& frustum)const noexcept{
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
    if(CollisionDetail::PointsInsideObb(corners, BoundingFrustum::s_CornerCount, LoadFloat(center), LoadFloat(extents), LoadFloat(orientation)))
        return ContainmentType::Contains;
    return intersects(frustum) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingSphere& sphere)const noexcept{
    return sphere.intersects(*this);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingBox& box)const noexcept{
    return CollisionDetail::ObbIntersectsObb(
        LoadFloat(center),
        LoadFloat(extents),
        LoadFloat(orientation),
        LoadFloat(box.center),
        LoadFloat(box.extents),
        s_SIMDIdentityR3
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingOrientedBox& box)const noexcept{
    return CollisionDetail::ObbIntersectsObb(
        LoadFloat(center),
        LoadFloat(extents),
        LoadFloat(orientation),
        LoadFloat(box.center),
        LoadFloat(box.extents),
        LoadFloat(box.orientation)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingOrientedBox::intersects(const BoundingFrustum& frustum)const noexcept{
    return frustum.intersects(*this);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersects(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    const SIMDVector centerVector = LoadFloat(center);
    const SIMDVector orientationVector = LoadFloat(orientation);
    const SIMDVector localV0 = CollisionDetail::PointToObbLocal(v0, centerVector, orientationVector);
    const SIMDVector localV1 = CollisionDetail::PointToObbLocal(v1, centerVector, orientationVector);
    const SIMDVector localV2 = CollisionDetail::PointToObbLocal(v2, centerVector, orientationVector);
    const SIMDVector extentsVector = LoadFloat(extents);
    return CollisionDetail::TriangleAabbOverlap(localV0, localV1, localV2, VectorNegate(extentsVector), extentsVector);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingOrientedBox::intersects(const SIMDVector plane)const noexcept{
    SIMDVector outside{};
    SIMDVector inside{};
    SIMDVector axis0{};
    SIMDVector axis1{};
    SIMDVector axis2{};
    CollisionDetail::ObbAxes(LoadFloat(orientation), axis0, axis1, axis2);
    CollisionDetail::FastIntersectOrientedBoxPlane(
        LoadFloat(center),
        LoadFloat(extents),
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


[[nodiscard]] inline bool SIMDCALL BoundingOrientedBox::intersects(
    const SIMDVector origin,
    const SIMDVector direction,
    f32& outDistance
)const noexcept{
    const SIMDVector orientationVector = LoadFloat(orientation);
    const SIMDVector localOrigin = CollisionDetail::PointToObbLocal(origin, LoadFloat(center), orientationVector);
    const SIMDVector localDirection = Vector3InverseRotate(direction, orientationVector);
    const SIMDVector extentsVector = LoadFloat(extents);
    return CollisionDetail::RayIntersectsMinMax(localOrigin, localDirection, VectorNegate(extentsVector), extentsVector, outDistance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingOrientedBox::containedBy(
    const SIMDVector plane0,
    const SIMDVector plane1,
    const SIMDVector plane2,
    const SIMDVector plane3,
    const SIMDVector plane4,
    const SIMDVector plane5
)const noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::ObbCorners(LoadFloat(center), LoadFloat(extents), LoadFloat(orientation), corners);
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::createFromBoundingBox(BoundingOrientedBox& outBox, const BoundingBox& box)noexcept{
    outBox.center = box.center;
    outBox.extents = box.extents;
    outBox.orientation = Float4(0.0f, 0.0f, 0.0f, 1.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingOrientedBox::createFromPoints(
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


inline BoundingFrustum::BoundingFrustum(const SIMDMatrix& projection, const bool rightHandedCoordinates)noexcept{
    createFromMatrix(*this, projection, rightHandedCoordinates);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingFrustum::transform(BoundingFrustum& outFrustum, const SIMDMatrix& matrix)const noexcept{
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
    const SIMDVector scaledPlanes = VectorMultiply(VectorSet(nearPlane, farPlane, 0.0f, 0.0f), maxScale);
    const SIMDVector originVector = Vector3Transform(LoadFloat(origin), matrix);
    const SIMDVector orientationVector = QuaternionNormalize(QuaternionMultiply(LoadFloat(orientation), rotation));
    StoreFloat(VectorSetW(originVector, 0.0f), outFrustum.origin);
    StoreFloat(orientationVector, outFrustum.orientation);
    outFrustum.rightSlope = rightSlope;
    outFrustum.leftSlope = leftSlope;
    outFrustum.topSlope = topSlope;
    outFrustum.bottomSlope = bottomSlope;
    outFrustum.nearPlane = VectorGetX(scaledPlanes);
    outFrustum.farPlane = VectorGetY(scaledPlanes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SIMDCALL BoundingFrustum::transform(
    BoundingFrustum& outFrustum,
    const f32 scale,
    const SIMDVector rotation,
    const SIMDVector translation
)const noexcept{
    const SIMDVector originVector = VectorAdd(Vector3Rotate(VectorScale(LoadFloat(origin), scale), rotation), translation);
    const SIMDVector orientationVector = QuaternionNormalize(QuaternionMultiply(LoadFloat(orientation), rotation));
    const SIMDVector scaledPlanes = VectorMultiply(
        VectorSet(nearPlane, farPlane, 0.0f, 0.0f),
        VectorAbs(VectorReplicate(scale))
    );
    StoreFloat(VectorSetW(originVector, 0.0f), outFrustum.origin);
    StoreFloat(orientationVector, outFrustum.orientation);
    outFrustum.rightSlope = rightSlope;
    outFrustum.leftSlope = leftSlope;
    outFrustum.topSlope = topSlope;
    outFrustum.bottomSlope = bottomSlope;
    outFrustum.nearPlane = VectorGetX(scaledPlanes);
    outFrustum.farPlane = VectorGetY(scaledPlanes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingFrustum::getCorners(Float3U* corners)const noexcept{
    NWB_ASSERT(corners != nullptr);
    SIMDVector cornerVectors[s_CornerCount];
    CollisionDetail::FrustumCorners(
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


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::contains(const SIMDVector point)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    for(const SIMDVector plane : planes){
        if(CollisionDetail::Vector4AllTrue(VectorLess(CollisionDetail::PlaneDistance(plane, point), VectorZero())))
            return ContainmentType::Disjoint;
    }
    return ContainmentType::Contains;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::contains(
    const SIMDVector v0,
    const SIMDVector v1,
    const SIMDVector v2
)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    const SIMDVector points[CollisionDetail::s_TriangleVertexCount] = { v0, v1, v2 };
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    return CollisionDetail::ContainmentFromPlaneTests(points, CollisionDetail::s_TriangleVertexCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingSphere& sphere)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    const SIMDVector sphereValue = LoadFloat(sphere.centerRadius);
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


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingBox& box)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector corners[BoundingBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    CollisionDetail::AabbCorners(LoadFloat(box.center), LoadFloat(box.extents), corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingOrientedBox& box)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector corners[BoundingOrientedBox::s_CornerCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    CollisionDetail::ObbCorners(LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation), corners);
    return CollisionDetail::ContainmentFromPlaneTests(corners, BoundingOrientedBox::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ContainmentType::Enum BoundingFrustum::contains(const BoundingFrustum& frustum)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector corners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
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
    const ContainmentType::Enum containment = CollisionDetail::ContainmentFromPlaneTests(corners, BoundingFrustum::s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
    if(containment != ContainmentType::Intersects)
        return containment;

    SIMDVector otherPlanes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector thisCorners[BoundingFrustum::s_CornerCount];
    CollisionDetail::FrustumPlanes(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        otherPlanes
    );
    CollisionDetail::FrustumCorners(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, thisCorners);
    return CollisionDetail::FrustumPlanesIntersectPoints(otherPlanes, thisCorners, BoundingFrustum::s_CornerCount) ? ContainmentType::Intersects : ContainmentType::Disjoint;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingSphere& sphere)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    return CollisionDetail::FrustumPlanesIntersectSphere(planes, LoadFloat(sphere.centerRadius));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingBox& box)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    return CollisionDetail::FrustumPlanesIntersectAxisAlignedBox(planes, LoadFloat(box.center), LoadFloat(box.extents));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingOrientedBox& box)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    return CollisionDetail::FrustumPlanesIntersectOrientedBox(planes, LoadFloat(box.center), LoadFloat(box.extents), LoadFloat(box.orientation));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BoundingFrustum::intersects(const BoundingFrustum& frustum)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    SIMDVector otherPlanes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
    CollisionDetail::FrustumPlanes(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        otherPlanes
    );

    SIMDVector corners[s_CornerCount];
    SIMDVector otherCorners[s_CornerCount];
    CollisionDetail::FrustumCorners(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, corners);
    CollisionDetail::FrustumCorners(
        LoadFloat(frustum.origin),
        LoadFloat(frustum.orientation),
        frustum.rightSlope,
        frustum.leftSlope,
        frustum.topSlope,
        frustum.bottomSlope,
        frustum.nearPlane,
        frustum.farPlane,
        otherCorners
    );

    return CollisionDetail::FrustumPlanesIntersectPoints(planes, otherCorners, s_CornerCount)
        && CollisionDetail::FrustumPlanesIntersectPoints(otherPlanes, corners, s_CornerCount);
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


[[nodiscard]] inline PlaneIntersectionType::Enum SIMDCALL BoundingFrustum::intersects(const SIMDVector plane)const noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::FrustumCorners(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, corners);
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


[[nodiscard]] inline bool SIMDCALL BoundingFrustum::intersects(
    const SIMDVector rayOrigin,
    const SIMDVector direction,
    f32& outDistance
)const noexcept{
    SIMDVector planes[CollisionDetail::s_FrustumPlaneCount];
    CollisionDetail::FrustumPlanes(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, planes);
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


[[nodiscard]] inline ContainmentType::Enum SIMDCALL BoundingFrustum::containedBy(
    const SIMDVector plane0,
    const SIMDVector plane1,
    const SIMDVector plane2,
    const SIMDVector plane3,
    const SIMDVector plane4,
    const SIMDVector plane5
)const noexcept{
    SIMDVector corners[s_CornerCount];
    CollisionDetail::FrustumCorners(LoadFloat(origin), LoadFloat(orientation), rightSlope, leftSlope, topSlope, bottomSlope, nearPlane, farPlane, corners);
    const SIMDVector planes[CollisionDetail::s_FrustumPlaneCount] = { plane0, plane1, plane2, plane3, plane4, plane5 };
    return CollisionDetail::ContainmentFromPlaneTests(corners, s_CornerCount, planes, CollisionDetail::s_FrustumPlaneCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void BoundingFrustum::getPlanes(
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


inline void SIMDCALL BoundingFrustum::createFromMatrix(
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

