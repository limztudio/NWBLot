// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix_double.h"
#include "collision_detail_double.h"
#include "collision_plane_double.h"
#include "collision_sdf_double.h"
#include "collision_aabb_double.h"
#include "collision_triangle_double.h"
#include "bounding_shape_decls_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CollisionDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void AabbCorners(const SIMDVectorDouble center, const SIMDVectorDouble extents, SIMDVectorDouble* outCorners)noexcept{
    for(u64 i = 0u; i < BoundingDoubleBox::s_DoubleCornerCount; ++i)
        outCorners[i] = VectorDoubleMultiplyAdd(extents, BoxCornerOffset(i), center);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void ObbCorners(const SIMDVectorDouble center, const SIMDVectorDouble extents, const SIMDVectorDouble orientation, SIMDVectorDouble* outCorners)noexcept{
    SIMDVectorDouble axis0{};
    SIMDVectorDouble axis1{};
    SIMDVectorDouble axis2{};
    ObbAxes(orientation, axis0, axis1, axis2);
    axis0 = VectorDoubleMultiply(axis0, VectorDoubleSplatX(extents));
    axis1 = VectorDoubleMultiply(axis1, VectorDoubleSplatY(extents));
    axis2 = VectorDoubleMultiply(axis2, VectorDoubleSplatZ(extents));
    for(u64 i = 0u; i < BoundingDoubleOrientedBox::s_DoubleCornerCount; ++i){
        const SIMDVectorDouble offset = BoxCornerOffset(i);
        SIMDVectorDouble corner = VectorDoubleMultiply(axis0, VectorDoubleSplatX(offset));
        corner = VectorDoubleMultiplyAdd(axis1, VectorDoubleSplatY(offset), corner);
        corner = VectorDoubleMultiplyAdd(axis2, VectorDoubleSplatZ(offset), corner);
        outCorners[i] = VectorDoubleAdd(corner, center);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void FrustumCorners(
    const SIMDVectorDouble origin,
    const SIMDVectorDouble orientation,
    const f64 rightSlope,
    const f64 leftSlope,
    const f64 topSlope,
    const f64 bottomSlope,
    const f64 nearPlane,
    const f64 farPlane,
    SIMDVectorDouble* outCorners
)noexcept{
    const SIMDVectorDouble slopes = VectorDoubleSet(rightSlope, leftSlope, bottomSlope, topSlope);
    const SIMDVectorDouble depths[2] = {
        VectorDoubleReplicate(nearPlane),
        VectorDoubleReplicate(farPlane),
    };
    const SIMDVectorDouble zero = VectorDoubleZero();
    u64 corner = 0u;
    for(const SIMDVectorDouble depth : depths){
        const SIMDVectorDouble scaledSlopes = VectorDoubleMultiply(slopes, depth);
        const SIMDVectorDouble right = VectorDoubleSplatX(scaledSlopes);
        const SIMDVectorDouble left = VectorDoubleSplatY(scaledSlopes);
        const SIMDVectorDouble bottom = VectorDoubleSplatZ(scaledSlopes);
        const SIMDVectorDouble top = VectorDoubleSplatW(scaledSlopes);
        const SIMDVectorDouble localCorners[4] = {
            VectorDoubleMergeX(right, top, depth, zero),
            VectorDoubleMergeX(left, top, depth, zero),
            VectorDoubleMergeX(left, bottom, depth, zero),
            VectorDoubleMergeX(right, bottom, depth, zero),
        };
        for(const SIMDVectorDouble localCorner : localCorners){
            outCorners[corner] = VectorDoubleAdd(VectorDouble3Rotate(localCorner, orientation), origin);
            ++corner;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorDouble NWB_SIMD_CALL BoundingDoubleSphere::TransformSphereValue(SIMDVectorDouble sphereValue, const SIMDMatrixDouble& matrix)noexcept{
    const SIMDVectorDouble centerVector = CollisionDoubleDetail::SphereCenter(sphereValue);
    const SIMDVectorDouble sphereRadius = VectorDoubleSplatW(sphereValue);
    const auto decomposition = MatrixDoubleDecompose(matrix);
    if(decomposition){
        const SIMDVectorDouble absScale = VectorDoubleAbs(decomposition->scale);
        const SIMDVectorDouble maxScale = CollisionDoubleDetail::VectorDouble3MaxComponent(absScale);
        return CollisionDoubleDetail::SphereCenterRadius(VectorDouble3Transform(centerVector, matrix), VectorDoubleMultiply(sphereRadius, maxScale));
    }

    const SIMDVectorDouble maxScaleVector = VectorDoubleMax(VectorDouble3Length(matrix.v[0]), VectorDoubleMax(VectorDouble3Length(matrix.v[1]), VectorDouble3Length(matrix.v[2])));
    return CollisionDoubleDetail::SphereCenterRadius(VectorDouble3Transform(centerVector, matrix), VectorDoubleMultiply(sphereRadius, maxScaleVector));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline SIMDVectorDouble NWB_SIMD_CALL BoundingDoubleSphere::TransformSphereValue(
    SIMDVectorDouble sphereValue,
    const f64 scale,
    const SIMDVectorDouble rotation,
    const SIMDVectorDouble translation
)noexcept{
    const SIMDVectorDouble transformedCenter = VectorDoubleAdd(VectorDouble3Rotate(VectorDoubleScale(CollisionDoubleDetail::SphereCenter(sphereValue), scale), rotation), translation);
    return CollisionDoubleDetail::SphereCenterRadius(transformedCenter, VectorDoubleScale(VectorDoubleSplatW(sphereValue), Abs(scale)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

