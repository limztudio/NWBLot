// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix.h"
#include "collision_detail.h"
#include "collision_plane.h"
#include "collision_sdf.h"
#include "collision_aabb.h"
#include "collision_triangle.h"
#include "bounding_shape_decls.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CollisionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void AabbCorners(const SIMDVectorSingle center, const SIMDVectorSingle extents, SIMDVectorSingle* outCorners)noexcept{
    for(u32 i = 0u; i < BoundingBox::s_CornerCount; ++i)
        outCorners[i] = VectorMultiplyAdd(extents, BoxCornerOffset(i), center);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void ObbCorners(const SIMDVectorSingle center, const SIMDVectorSingle extents, const SIMDVectorSingle orientation, SIMDVectorSingle* outCorners)noexcept{
    SIMDVectorSingle axis0{};
    SIMDVectorSingle axis1{};
    SIMDVectorSingle axis2{};
    ObbAxes(orientation, axis0, axis1, axis2);
    axis0 = VectorMultiply(axis0, VectorSplatX(extents));
    axis1 = VectorMultiply(axis1, VectorSplatY(extents));
    axis2 = VectorMultiply(axis2, VectorSplatZ(extents));
    for(u32 i = 0u; i < BoundingOrientedBox::s_CornerCount; ++i){
        const SIMDVectorSingle offset = BoxCornerOffset(i);
        SIMDVectorSingle corner = VectorMultiply(axis0, VectorSplatX(offset));
        corner = VectorMultiplyAdd(axis1, VectorSplatY(offset), corner);
        corner = VectorMultiplyAdd(axis2, VectorSplatZ(offset), corner);
        outCorners[i] = VectorAdd(corner, center);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void FrustumCorners(
    const SIMDVectorSingle origin,
    const SIMDVectorSingle orientation,
    const f32 rightSlope,
    const f32 leftSlope,
    const f32 topSlope,
    const f32 bottomSlope,
    const f32 nearPlane,
    const f32 farPlane,
    SIMDVectorSingle* outCorners
)noexcept{
    const SIMDVectorSingle slopes = VectorSet(rightSlope, leftSlope, bottomSlope, topSlope);
    const SIMDVectorSingle depths[2] = {
        VectorReplicate(nearPlane),
        VectorReplicate(farPlane),
    };
    const SIMDVectorSingle zero = VectorZero();
    u32 corner = 0u;
    for(const SIMDVectorSingle depth : depths){
        const SIMDVectorSingle scaledSlopes = VectorMultiply(slopes, depth);
        const SIMDVectorSingle right = VectorSplatX(scaledSlopes);
        const SIMDVectorSingle left = VectorSplatY(scaledSlopes);
        const SIMDVectorSingle bottom = VectorSplatZ(scaledSlopes);
        const SIMDVectorSingle top = VectorSplatW(scaledSlopes);
        const SIMDVectorSingle localCorners[4] = {
            VectorMergeX(right, top, depth, zero),
            VectorMergeX(left, top, depth, zero),
            VectorMergeX(left, bottom, depth, zero),
            VectorMergeX(right, bottom, depth, zero),
        };
        for(const SIMDVectorSingle localCorner : localCorners){
            outCorners[corner] = VectorAdd(Vector3Rotate(localCorner, orientation), origin);
            ++corner;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorSingle NWB_SIMD_CALL BoundingSphere::TransformSphereValue(SIMDVectorSingle sphereValue, const SIMDMatrix& matrix)noexcept{
    const SIMDVectorSingle centerVector = CollisionDetail::SphereCenter(sphereValue);
    const SIMDVectorSingle sphereRadius = VectorSplatW(sphereValue);
    const auto decomposition = MatrixDecompose(matrix);
    if(decomposition){
        const SIMDVectorSingle absScale = VectorAbs(decomposition->scale);
        const SIMDVectorSingle maxScale = CollisionDetail::Vector3MaxComponent(absScale);
        return CollisionDetail::SphereCenterRadius(Vector3Transform(centerVector, matrix), VectorMultiply(sphereRadius, maxScale));
    }

    const SIMDVectorSingle maxScaleVector = VectorMax(Vector3Length(matrix.v[0]), VectorMax(Vector3Length(matrix.v[1]), Vector3Length(matrix.v[2])));
    return CollisionDetail::SphereCenterRadius(Vector3Transform(centerVector, matrix), VectorMultiply(sphereRadius, maxScaleVector));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorSingle NWB_SIMD_CALL BoundingSphere::TransformSphereValue(
    SIMDVectorSingle sphereValue,
    const f32 scale,
    const SIMDVectorSingle rotation,
    const SIMDVectorSingle translation
)noexcept{
    const SIMDVectorSingle transformedCenter = VectorAdd(Vector3Rotate(VectorScale(CollisionDetail::SphereCenter(sphereValue), scale), rotation), translation);
    return CollisionDetail::SphereCenterRadius(transformedCenter, VectorScale(VectorSplatW(sphereValue), Abs(scale)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

