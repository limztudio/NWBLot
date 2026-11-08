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


struct BoundingBox{
    static constexpr usize s_CornerCount = 8u;


public:
    Float4 center;
    Float4 extents = Float4(1.0f, 1.0f, 1.0f, 0.0f);


public:
    static void NWB_SIMD_CALL TransformBoxValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, const SIMDMatrix& matrix, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents)noexcept;
    static void NWB_SIMD_CALL TransformBoxValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsPointValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle point)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsTriangleValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;
    [[nodiscard]] static b8 NWB_SIMD_CALL IntersectsTriangleValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlaneValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle plane)noexcept;
    [[nodiscard]] static Expected<f32> NWB_SIMD_CALL IntersectsRayValue(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle origin, SIMDVectorSingle direction)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainedByValue(
        SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5
    )noexcept;
    static void NWB_SIMD_CALL MergeBoxValues(SIMDVectorSingle boxCenter0, SIMDVectorSingle boxExtents0, SIMDVectorSingle boxCenter1, SIMDVectorSingle boxExtents1, SIMDVectorSingle& outCenter, SIMDVectorSingle& outExtents)noexcept;
    static void CreateMerged(BoundingBox& outBox, const BoundingBox& box0, const BoundingBox& box1)noexcept;
    static void CreateFromSphere(BoundingBox& outBox, const BoundingSphere& sphere)noexcept;
    static void NWB_SIMD_CALL CreateFromPoints(BoundingBox& outBox, SIMDVectorSingle point0, SIMDVectorSingle point1)noexcept;
    static void CreateFromPoints(BoundingBox& outBox, usize count, const Float3U& points, usize stride)noexcept;


public:
    constexpr BoundingBox()noexcept = default;
    constexpr BoundingBox(const Float3U& centerValue, const Float3U& extentsValue)noexcept
        : center(centerValue.x, centerValue.y, centerValue.z, 0.0f)
        , extents(extentsValue.x, extentsValue.y, extentsValue.z, 0.0f)
    {}
    constexpr BoundingBox(const Float4& centerValue, const Float4& extentsValue)noexcept
        : center(centerValue)
        , extents(extentsValue)
    {}


public:
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsSphereValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsBoxValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsOrientedBoxValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents, SIMDVectorSingle otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsFrustumValues(
        SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle frustumOrigin, SIMDVectorSingle frustumOrientation, f32 rightSlopeValue, f32 leftSlopeValue, f32 topSlopeValue, f32 bottomSlopeValue, f32 nearPlaneValue, f32 farPlaneValue
    )const noexcept;
    [[nodiscard]] b8 NWB_SIMD_CALL intersectsBoxValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents)const noexcept;
    [[nodiscard]] b8 NWB_SIMD_CALL intersectsOrientedBoxValues(SIMDVectorSingle boxCenter, SIMDVectorSingle boxExtents, SIMDVectorSingle otherBoxCenter, SIMDVectorSingle otherBoxExtents, SIMDVectorSingle otherBoxOrientation)const noexcept;
    void NWB_SIMD_CALL transform(BoundingBox& outBox, const SIMDMatrix& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingBox& outBox, f32 scale, SIMDVectorSingle rotation, SIMDVectorSingle translation)const noexcept;
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
    [[nodiscard]] Expected<f32> NWB_SIMD_CALL intersects(SIMDVectorSingle origin, SIMDVectorSingle direction)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(SIMDVectorSingle plane0, SIMDVectorSingle plane1, SIMDVectorSingle plane2, SIMDVectorSingle plane3, SIMDVectorSingle plane4, SIMDVectorSingle plane5)const noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BoundingDoubleBox{
    static constexpr usize s_DoubleCornerCount = 8u;


public:
    Double4 center;
    Double4 extents = Double4(1.0, 1.0, 1.0, 0.0);


public:
    static void NWB_SIMD_CALL TransformBoxValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, const SIMDMatrixDouble& matrix, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents)noexcept;
    static void NWB_SIMD_CALL TransformBoxValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsPointValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble point)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsTriangleValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
    [[nodiscard]] static b8 NWB_SIMD_CALL IntersectsTriangleValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlaneValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble plane)noexcept;
    [[nodiscard]] static Expected<f64> NWB_SIMD_CALL IntersectsRayValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble origin, SIMDVectorDouble direction)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainedByValue(
        SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5
    )noexcept;
    static void NWB_SIMD_CALL MergeBoxValues(SIMDVectorDouble boxCenter0, SIMDVectorDouble boxExtents0, SIMDVectorDouble boxCenter1, SIMDVectorDouble boxExtents1, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents)noexcept;
    static void CreateMerged(BoundingDoubleBox& outBox, const BoundingDoubleBox& box0, const BoundingDoubleBox& box1)noexcept;
    static void CreateFromSphere(BoundingDoubleBox& outBox, const BoundingDoubleSphere& sphere)noexcept;
    static void NWB_SIMD_CALL CreateFromPoints(BoundingDoubleBox& outBox, SIMDVectorDouble point0, SIMDVectorDouble point1)noexcept;
    template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
    static void CreateFromPoints(BoundingDoubleBox& outBox, usize count, const PointT& points, usize stride)noexcept;


public:
    constexpr BoundingDoubleBox()noexcept = default;
    template<typename CenterT, typename ExtentsT>
        requires ((IsSame_V<CenterT, Double3> || IsSame_V<CenterT, Double3U>) && (IsSame_V<ExtentsT, Double3> || IsSame_V<ExtentsT, Double3U>))
    constexpr BoundingDoubleBox(const CenterT& centerValue, const ExtentsT& extentsValue)noexcept
        : center(centerValue.x, centerValue.y, centerValue.z, 0.0)
        , extents(extentsValue.x, extentsValue.y, extentsValue.z, 0.0)
    {}
    constexpr BoundingDoubleBox(const Double4& centerValue, const Double4& extentsValue)noexcept
        : center(centerValue)
        , extents(extentsValue)
    {}


public:
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsSphereValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsBoxValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsOrientedBoxValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents, SIMDVectorDouble otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsFrustumValues(
        SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue
    )const noexcept;
    [[nodiscard]] b8 NWB_SIMD_CALL intersectsBoxValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents)const noexcept;
    [[nodiscard]] b8 NWB_SIMD_CALL intersectsOrientedBoxValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents, SIMDVectorDouble otherBoxOrientation)const noexcept;
    void NWB_SIMD_CALL transform(BoundingDoubleBox& outBox, const SIMDMatrixDouble& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingDoubleBox& outBox, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation)const noexcept;
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
    [[nodiscard]] Expected<f64> NWB_SIMD_CALL intersects(SIMDVectorDouble origin, SIMDVectorDouble direction)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)const noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

