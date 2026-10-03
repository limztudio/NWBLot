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

