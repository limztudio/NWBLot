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


struct BoundingSphere{
    Float4 centerRadius = Float4(0.0f, 0.0f, 0.0f, 1.0f);


public:
    [[nodiscard]] static SIMDVectorSingle NWB_SIMD_CALL TransformSphereValue(SIMDVectorSingle sphereValue, const SIMDMatrix& matrix)noexcept;
    [[nodiscard]] static SIMDVectorSingle NWB_SIMD_CALL TransformSphereValue(SIMDVectorSingle sphereValue, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsPointValue(SIMDVectorSingle sphereValue, SIMDVectorSingle point)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsTriangleValue(SIMDVectorSingle sphereValue, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;
    [[nodiscard]] static b8 NWB_SIMD_CALL IntersectsTriangleValue(SIMDVectorSingle sphereValue, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlaneValue(SIMDVectorSingle sphereValue, SIMDVectorSingle plane)noexcept;
    [[nodiscard]] static Expected<f32> NWB_SIMD_CALL IntersectsRayValue(SIMDVectorSingle sphereValue, SIMDVectorSingle origin, SIMDVectorSingle direction)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainedByValue(
        SIMDVectorSingle sphereValue, SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5
    )noexcept;
    [[nodiscard]] static SIMDVectorSingle NWB_SIMD_CALL MergeSphereValues(SIMDVectorSingle sphereValue0, SIMDVectorSingle sphereValue1, b8& outDirectCopy, b8& outCopyFirst)noexcept;
    [[nodiscard]] static SIMDVectorSingle NWB_SIMD_CALL SphereFromCenterExtentsValue(SIMDVectorSingle centerValue, SIMDVectorSingle extentsValue)noexcept;
    static void CreateMerged(BoundingSphere& outSphere, const BoundingSphere& sphere0, const BoundingSphere& sphere1)noexcept;
    static void CreateFromBoundingBox(BoundingSphere& outSphere, const BoundingBox& box)noexcept;
    static void CreateFromBoundingBox(BoundingSphere& outSphere, const BoundingOrientedBox& box)noexcept;
    static void CreateFromPoints(BoundingSphere& outSphere, usize count, const Float3U& points, usize stride)noexcept;
    static void CreateFromFrustum(BoundingSphere& outSphere, const BoundingFrustum& frustum)noexcept;


public:
    constexpr BoundingSphere()noexcept = default;
    constexpr BoundingSphere(const Float3U& centerValue, const f32 radiusValue)noexcept
        : centerRadius(centerValue.x, centerValue.y, centerValue.z, radiusValue)
    {}
    constexpr explicit BoundingSphere(const Float4& centerRadiusValue)noexcept
        : centerRadius(centerRadiusValue)
    {}


public:
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsSphereValue(SIMDVectorSingle sphereValue, SIMDVectorSingle otherSphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsBoxValues(SIMDVectorSingle sphereValue, SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsOrientedBoxValues(SIMDVectorSingle sphereValue, SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsFrustumValues(
        SIMDVectorSingle sphereValue, SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue
    )const noexcept;
    [[nodiscard]] b8 NWB_SIMD_CALL intersectsSphereValue(SIMDVectorSingle sphereValue, SIMDVectorSingle otherSphereValue)const noexcept;
    [[nodiscard]] b8 NWB_SIMD_CALL intersectsBoxValues(SIMDVectorSingle sphereValue, SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents)const noexcept;
    [[nodiscard]] b8 NWB_SIMD_CALL intersectsOrientedBoxValues(SIMDVectorSingle sphereValue, SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle boxOrientation)const noexcept;
    void NWB_SIMD_CALL transform(BoundingSphere& outSphere, const SIMDMatrix& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingSphere& outSphere, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation)const noexcept;
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
    [[nodiscard]] Expected<f32> NWB_SIMD_CALL intersects(SIMDVectorSingle origin, SIMDVectorSingle direction)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)const noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BoundingDoubleSphere{
    Double4 centerRadius = Double4(0.0, 0.0, 0.0, 1.0);


public:
    [[nodiscard]] static SIMDVectorDouble NWB_SIMD_CALL TransformSphereValue(SIMDVectorDouble sphereValue, const SIMDMatrixDouble& matrix)noexcept;
    [[nodiscard]] static SIMDVectorDouble NWB_SIMD_CALL TransformSphereValue(SIMDVectorDouble sphereValue, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsPointValue(SIMDVectorDouble sphereValue, SIMDVectorDouble point)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsTriangleValue(SIMDVectorDouble sphereValue, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
    [[nodiscard]] static b8 NWB_SIMD_CALL IntersectsTriangleValue(SIMDVectorDouble sphereValue, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlaneValue(SIMDVectorDouble sphereValue, SIMDVectorDouble plane)noexcept;
    [[nodiscard]] static Expected<f64> NWB_SIMD_CALL IntersectsRayValue(SIMDVectorDouble sphereValue, SIMDVectorDouble origin, SIMDVectorDouble direction)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainedByValue(
        SIMDVectorDouble sphereValue, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5
    )noexcept;
    [[nodiscard]] static SIMDVectorDouble NWB_SIMD_CALL MergeSphereValues(SIMDVectorDouble sphereValue0, SIMDVectorDouble sphereValue1, b8& outDirectCopy, b8& outCopyFirst)noexcept;
    [[nodiscard]] static SIMDVectorDouble NWB_SIMD_CALL SphereFromCenterExtentsValue(SIMDVectorDouble centerValue, SIMDVectorDouble extentsValue)noexcept;
    static void CreateMerged(BoundingDoubleSphere& outSphere, const BoundingDoubleSphere& sphere0, const BoundingDoubleSphere& sphere1)noexcept;
    static void CreateFromBoundingBox(BoundingDoubleSphere& outSphere, const BoundingDoubleBox& box)noexcept;
    static void CreateFromBoundingBox(BoundingDoubleSphere& outSphere, const BoundingDoubleOrientedBox& box)noexcept;
    template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
    static void CreateFromPoints(BoundingDoubleSphere& outSphere, usize count, const PointT& points, usize stride)noexcept;
    static void CreateFromFrustum(BoundingDoubleSphere& outSphere, const BoundingDoubleFrustum& frustum)noexcept;


public:
    constexpr BoundingDoubleSphere()noexcept = default;
    template<typename CenterT> requires (IsSame_V<CenterT, Double3> || IsSame_V<CenterT, Double3U>)
    constexpr BoundingDoubleSphere(const CenterT& centerValue, const f64 radiusValue)noexcept
        : centerRadius(centerValue.x, centerValue.y, centerValue.z, radiusValue)
    {}
    constexpr explicit BoundingDoubleSphere(const Double4& centerRadiusValue)noexcept
        : centerRadius(centerRadiusValue)
    {}


public:
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsSphereValue(SIMDVectorDouble sphereValue, SIMDVectorDouble otherSphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsBoxValues(SIMDVectorDouble sphereValue, SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsOrientedBoxValues(SIMDVectorDouble sphereValue, SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsFrustumValues(
        SIMDVectorDouble sphereValue, SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue
    )const noexcept;
    [[nodiscard]] b8 NWB_SIMD_CALL intersectsSphereValue(SIMDVectorDouble sphereValue, SIMDVectorDouble otherSphereValue)const noexcept;
    [[nodiscard]] b8 NWB_SIMD_CALL intersectsBoxValues(SIMDVectorDouble sphereValue, SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents)const noexcept;
    [[nodiscard]] b8 NWB_SIMD_CALL intersectsOrientedBoxValues(SIMDVectorDouble sphereValue, SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation)const noexcept;
    void NWB_SIMD_CALL transform(BoundingDoubleSphere& outSphere, const SIMDMatrixDouble& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingDoubleSphere& outSphere, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation)const noexcept;
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
    [[nodiscard]] Expected<f64> NWB_SIMD_CALL intersects(SIMDVectorDouble origin, SIMDVectorDouble direction)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)const noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

