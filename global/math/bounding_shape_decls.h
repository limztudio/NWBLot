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
struct BoundingDoubleBox;
struct BoundingDoubleOrientedBox;
struct BoundingDoubleFrustum;


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


struct BoundingDoubleSphere{
    Double4 centerRadius = Double4(0.0, 0.0, 0.0, 1.0);

    constexpr BoundingDoubleSphere()noexcept = default;
    template<typename CenterT> requires (IsSame_V<CenterT, Double3> || IsSame_V<CenterT, Double3U>)
    constexpr BoundingDoubleSphere(const CenterT& centerValue, const f64 radiusValue)noexcept
        : centerRadius(centerValue.x, centerValue.y, centerValue.z, radiusValue)
    {}
    constexpr explicit BoundingDoubleSphere(const Double4& centerRadiusValue)noexcept
        : centerRadius(centerRadiusValue)
    {}

    [[nodiscard]] static SIMDVectorDouble NWB_SIMD_CALL TransformSphereValue(SIMDVectorDouble sphereValue, const SIMDMatrixDouble& matrix)noexcept;
    [[nodiscard]] static SIMDVectorDouble NWB_SIMD_CALL TransformSphereValue(SIMDVectorDouble sphereValue, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsPointValue(SIMDVectorDouble sphereValue, SIMDVectorDouble point)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsTriangleValue(SIMDVectorDouble sphereValue, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
    [[nodiscard]] static bool NWB_SIMD_CALL IntersectsTriangleValue(SIMDVectorDouble sphereValue, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlaneValue(SIMDVectorDouble sphereValue, SIMDVectorDouble plane)noexcept;
    [[nodiscard]] static Expected<f64> NWB_SIMD_CALL IntersectsRayValue(SIMDVectorDouble sphereValue, SIMDVectorDouble origin, SIMDVectorDouble direction)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainedByValue(SIMDVectorDouble sphereValue, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)noexcept;
    [[nodiscard]] static SIMDVectorDouble NWB_SIMD_CALL MergeSphereValues(SIMDVectorDouble sphereValue0, SIMDVectorDouble sphereValue1, bool& outDirectCopy, bool& outCopyFirst)noexcept;
    [[nodiscard]] static SIMDVectorDouble NWB_SIMD_CALL SphereFromCenterExtentsValue(SIMDVectorDouble centerValue, SIMDVectorDouble extentsValue)noexcept;
    [[nodiscard]] static SIMDVectorDouble NWB_SIMD_CALL SphereFromCornersValue(const SIMDVectorDouble* corners, usize cornerCount)noexcept;

    void NWB_SIMD_CALL transform(BoundingDoubleSphere& outSphere, const SIMDMatrixDouble& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingDoubleSphere& outSphere, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorDouble point)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsSphereValue(SIMDVectorDouble sphereValue, SIMDVectorDouble otherSphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsBoxValues(SIMDVectorDouble sphereValue, SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsOrientedBoxValues(SIMDVectorDouble sphereValue, SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsFrustumValues(SIMDVectorDouble sphereValue, SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue)const noexcept;

    [[nodiscard]] bool intersects(const BoundingDoubleSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingDoubleBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingDoubleOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingDoubleFrustum& frustum)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsSphereValue(SIMDVectorDouble sphereValue, SIMDVectorDouble otherSphereValue)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsBoxValues(SIMDVectorDouble sphereValue, SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsOrientedBoxValues(SIMDVectorDouble sphereValue, SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersects(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL intersects(SIMDVectorDouble plane)const noexcept;
    [[nodiscard]] Expected<f64> NWB_SIMD_CALL intersects(SIMDVectorDouble origin, SIMDVectorDouble direction)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(
        SIMDVectorDouble plane0,
        SIMDVectorDouble plane1,
        SIMDVectorDouble plane2,
        SIMDVectorDouble plane3,
        SIMDVectorDouble plane4,
        SIMDVectorDouble plane5
    )const noexcept;

    static void CreateMerged(BoundingDoubleSphere& outSphere, const BoundingDoubleSphere& sphere0, const BoundingDoubleSphere& sphere1)noexcept;
    static void CreateFromBoundingBox(BoundingDoubleSphere& outSphere, const BoundingDoubleBox& box)noexcept;
    static void CreateFromBoundingBox(BoundingDoubleSphere& outSphere, const BoundingDoubleOrientedBox& box)noexcept;
    template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
    static void CreateFromPoints(BoundingDoubleSphere& outSphere, usize count, const PointT* points, usize stride)noexcept;
    static void CreateFromFrustum(BoundingDoubleSphere& outSphere, const BoundingDoubleFrustum& frustum)noexcept;
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


struct BoundingDoubleBox{
    static constexpr usize s_DoubleCornerCount = 8u;

    Double4 center;
    Double4 extents = Double4(1.0, 1.0, 1.0, 0.0);

    constexpr BoundingDoubleBox()noexcept = default;
    template<typename CenterT, typename ExtentsT>
        requires ((IsSame_V<CenterT, Double3> || IsSame_V<CenterT, Double3U>)
            && (IsSame_V<ExtentsT, Double3> || IsSame_V<ExtentsT, Double3U>))
    constexpr BoundingDoubleBox(const CenterT& centerValue, const ExtentsT& extentsValue)noexcept
        : center(centerValue.x, centerValue.y, centerValue.z, 0.0)
        , extents(extentsValue.x, extentsValue.y, extentsValue.z, 0.0)
    {}
    constexpr BoundingDoubleBox(const Double4& centerValue, const Double4& extentsValue)noexcept
        : center(centerValue)
        , extents(extentsValue)
    {}

    static void NWB_SIMD_CALL TransformBoxValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, const SIMDMatrixDouble& matrix, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents)noexcept;
    static void NWB_SIMD_CALL TransformBoxValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsPointValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble point)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsTriangleValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
    [[nodiscard]] static bool NWB_SIMD_CALL IntersectsTriangleValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlaneValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble plane)noexcept;
    [[nodiscard]] static Expected<f64> NWB_SIMD_CALL IntersectsRayValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble origin, SIMDVectorDouble direction)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainedByValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)noexcept;
    static void NWB_SIMD_CALL MergeBoxValues(SIMDVectorDouble boxCenter0, SIMDVectorDouble boxExtents0, SIMDVectorDouble boxCenter1, SIMDVectorDouble boxExtents1, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents)noexcept;
    static void NWB_SIMD_CALL CornersValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble* outCorners)noexcept;

    void NWB_SIMD_CALL transform(BoundingDoubleBox& outBox, const SIMDMatrixDouble& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingDoubleBox& outBox, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation)const noexcept;
    template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
    void getCorners(PointT* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorDouble point)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsSphereValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsBoxValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsOrientedBoxValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents, SIMDVectorDouble otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsFrustumValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue)const noexcept;

    [[nodiscard]] bool intersects(const BoundingDoubleSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingDoubleBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingDoubleOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingDoubleFrustum& frustum)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsBoxValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsOrientedBoxValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents, SIMDVectorDouble otherBoxOrientation)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersects(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL intersects(SIMDVectorDouble plane)const noexcept;
    [[nodiscard]] Expected<f64> NWB_SIMD_CALL intersects(SIMDVectorDouble origin, SIMDVectorDouble direction)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(
        SIMDVectorDouble plane0,
        SIMDVectorDouble plane1,
        SIMDVectorDouble plane2,
        SIMDVectorDouble plane3,
        SIMDVectorDouble plane4,
        SIMDVectorDouble plane5
    )const noexcept;

    static void CreateMerged(BoundingDoubleBox& outBox, const BoundingDoubleBox& box0, const BoundingDoubleBox& box1)noexcept;
    static void CreateFromSphere(BoundingDoubleBox& outBox, const BoundingDoubleSphere& sphere)noexcept;
    static void NWB_SIMD_CALL CreateFromPoints(BoundingDoubleBox& outBox, SIMDVectorDouble point0, SIMDVectorDouble point1)noexcept;
    template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
    static void CreateFromPoints(BoundingDoubleBox& outBox, usize count, const PointT* points, usize stride)noexcept;
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

    static void CreateFromPoints(BoundingOrientedBox& outBox, usize count, const Float3U* points, usize stride)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct BoundingDoubleOrientedBox{
    static constexpr usize s_DoubleCornerCount = 8u;

    Double4 center;
    Double4 extents = Double4(1.0, 1.0, 1.0, 0.0);
    Double4 orientation = Double4(0.0, 0.0, 0.0, 1.0);

    constexpr BoundingDoubleOrientedBox()noexcept = default;
    template<typename CenterT, typename ExtentsT>
        requires ((IsSame_V<CenterT, Double3> || IsSame_V<CenterT, Double3U>)
            && (IsSame_V<ExtentsT, Double3> || IsSame_V<ExtentsT, Double3U>))
    constexpr BoundingDoubleOrientedBox(
        const CenterT& centerValue,
        const ExtentsT& extentsValue,
        const Double4& orientationValue
    )noexcept
        : center(centerValue.x, centerValue.y, centerValue.z, 0.0)
        , extents(extentsValue.x, extentsValue.y, extentsValue.z, 0.0)
        , orientation(orientationValue)
    {}
    constexpr BoundingDoubleOrientedBox(
        const Double4& centerValue,
        const Double4& extentsValue,
        const Double4& orientationValue
    )noexcept
        : center(centerValue)
        , extents(extentsValue)
        , orientation(orientationValue)
    {}

    static void NWB_SIMD_CALL TransformOrientedBoxValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, const SIMDMatrixDouble& matrix, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents, SIMDVectorDouble& outOrientation, bool& outCollapsedToAxisAligned)noexcept;
    static void NWB_SIMD_CALL TransformOrientedBoxValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation, SIMDVectorDouble& outCenter, SIMDVectorDouble& outExtents, SIMDVectorDouble& outOrientation)noexcept;
    static void NWB_SIMD_CALL CornersValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble* outCorners)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsPointValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble point)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsTriangleValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
    [[nodiscard]] static bool NWB_SIMD_CALL IntersectsTriangleValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlaneValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble plane)noexcept;
    [[nodiscard]] static Expected<f64> NWB_SIMD_CALL IntersectsRayValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble origin, SIMDVectorDouble direction)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainedByValue(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)noexcept;

    void NWB_SIMD_CALL transform(BoundingDoubleOrientedBox& outBox, const SIMDMatrixDouble& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingDoubleOrientedBox& outBox, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation)const noexcept;
    template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
    void getCorners(PointT* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorDouble point)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsSphereValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsBoxValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsOrientedBoxValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents, SIMDVectorDouble otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsFrustumValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue)const noexcept;

    [[nodiscard]] bool intersects(const BoundingDoubleSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingDoubleBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingDoubleOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingDoubleFrustum& frustum)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsBoxValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsOrientedBoxValues(SIMDVectorDouble boxCenter, SIMDVectorDouble boxExtents, SIMDVectorDouble boxOrientation, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents, SIMDVectorDouble otherBoxOrientation)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersects(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL intersects(SIMDVectorDouble plane)const noexcept;
    [[nodiscard]] Expected<f64> NWB_SIMD_CALL intersects(SIMDVectorDouble origin, SIMDVectorDouble direction)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(
        SIMDVectorDouble plane0,
        SIMDVectorDouble plane1,
        SIMDVectorDouble plane2,
        SIMDVectorDouble plane3,
        SIMDVectorDouble plane4,
        SIMDVectorDouble plane5
    )const noexcept;

    template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
    static void CreateFromPoints(BoundingDoubleOrientedBox& outBox, usize count, const PointT* points, usize stride)noexcept;
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


struct BoundingDoubleFrustum{
    static constexpr usize s_DoubleCornerCount = 8u;

    Double4 origin;
    Double4 orientation = Double4(0.0, 0.0, 0.0, 1.0);
    f64 rightSlope = 1.0;
    f64 leftSlope = -1.0;
    f64 topSlope = 1.0;
    f64 bottomSlope = -1.0;
    f64 nearPlane = 0.0;
    f64 farPlane = 1.0;

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
    explicit BoundingDoubleFrustum(const SIMDMatrixDouble& projection, bool rightHandedCoordinates = false)noexcept;

    static void NWB_SIMD_CALL TransformFrustumValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, const SIMDMatrixDouble& matrix, SIMDVectorDouble& outOrigin, SIMDVectorDouble& outOrientation, f64& outRightSlope, f64& outLeftSlope, f64& outTopSlope, f64& outBottomSlope, f64& outNearPlane, f64& outFarPlane)noexcept;
    static void NWB_SIMD_CALL TransformFrustumValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation, SIMDVectorDouble& outOrigin, SIMDVectorDouble& outOrientation, f64& outRightSlope, f64& outLeftSlope, f64& outTopSlope, f64& outBottomSlope, f64& outNearPlane, f64& outFarPlane)noexcept;
    static void NWB_SIMD_CALL CornersValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble* outCorners)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsPointValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble point)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainsTriangleValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept;
    [[nodiscard]] static PlaneIntersectionType::Enum NWB_SIMD_CALL IntersectsPlaneValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble plane)noexcept;
    [[nodiscard]] static Expected<f64> NWB_SIMD_CALL IntersectsRayValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble rayOrigin, SIMDVectorDouble direction)noexcept;
    [[nodiscard]] static ContainmentType::Enum NWB_SIMD_CALL ContainedByValue(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble plane0, SIMDVectorDouble plane1, SIMDVectorDouble plane2, SIMDVectorDouble plane3, SIMDVectorDouble plane4, SIMDVectorDouble plane5)noexcept;

    void NWB_SIMD_CALL transform(BoundingDoubleFrustum& outFrustum, const SIMDMatrixDouble& matrix)const noexcept;
    void NWB_SIMD_CALL transform(BoundingDoubleFrustum& outFrustum, f64 scale, SIMDVectorDouble rotation, SIMDVectorDouble translation)const noexcept;
    template<typename PointT> requires (IsSame_V<PointT, Double3> || IsSame_V<PointT, Double3U>)
    void getCorners(PointT* corners)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorDouble point)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL contains(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleSphere& sphere)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleOrientedBox& box)const noexcept;
    [[nodiscard]] ContainmentType::Enum contains(const BoundingDoubleFrustum& frustum)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsSphereValues(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble sphereValue)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsBoxValues(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsOrientedBoxValues(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents, SIMDVectorDouble otherBoxOrientation)const noexcept;
    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containsFrustumValues(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble otherFrustumOrigin, SIMDVectorDouble otherFrustumOrientation, f64 otherRightSlope, f64 otherLeftSlope, f64 otherTopSlope, f64 otherBottomSlope, f64 otherNearPlane, f64 otherFarPlane)const noexcept;

    [[nodiscard]] bool intersects(const BoundingDoubleSphere& sphere)const noexcept;
    [[nodiscard]] bool intersects(const BoundingDoubleBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingDoubleOrientedBox& box)const noexcept;
    [[nodiscard]] bool intersects(const BoundingDoubleFrustum& frustum)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsSphereValues(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble sphereValue)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsBoxValues(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsOrientedBoxValues(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble otherBoxCenter, SIMDVectorDouble otherBoxExtents, SIMDVectorDouble otherBoxOrientation)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersectsFrustumValues(SIMDVectorDouble frustumOrigin, SIMDVectorDouble frustumOrientation, f64 rightSlopeValue, f64 leftSlopeValue, f64 topSlopeValue, f64 bottomSlopeValue, f64 nearPlaneValue, f64 farPlaneValue, SIMDVectorDouble otherFrustumOrigin, SIMDVectorDouble otherFrustumOrientation, f64 otherRightSlope, f64 otherLeftSlope, f64 otherTopSlope, f64 otherBottomSlope, f64 otherNearPlane, f64 otherFarPlane)const noexcept;
    [[nodiscard]] bool NWB_SIMD_CALL intersects(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)const noexcept;
    [[nodiscard]] PlaneIntersectionType::Enum NWB_SIMD_CALL intersects(SIMDVectorDouble plane)const noexcept;
    [[nodiscard]] Expected<f64> NWB_SIMD_CALL intersects(SIMDVectorDouble rayOrigin, SIMDVectorDouble direction)const noexcept;

    [[nodiscard]] ContainmentType::Enum NWB_SIMD_CALL containedBy(
        SIMDVectorDouble plane0,
        SIMDVectorDouble plane1,
        SIMDVectorDouble plane2,
        SIMDVectorDouble plane3,
        SIMDVectorDouble plane4,
        SIMDVectorDouble plane5
    )const noexcept;

    void getPlanes(
        SIMDVectorDouble* nearPlaneOut,
        SIMDVectorDouble* farPlaneOut,
        SIMDVectorDouble* rightPlaneOut,
        SIMDVectorDouble* leftPlaneOut,
        SIMDVectorDouble* topPlaneOut,
        SIMDVectorDouble* bottomPlaneOut
    )const noexcept;

    static void NWB_SIMD_CALL CreateFromMatrix(
        BoundingDoubleFrustum& outFrustum,
        const SIMDMatrixDouble& projection,
        bool rightHandedCoordinates = false
    )noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

