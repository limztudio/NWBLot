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


inline void FrustumCorners(const SIMDVectorSingle origin, const SIMDVectorSingle orientation, const f32 rightSlope, const f32 leftSlope, const f32 topSlope, const f32 bottomSlope, const f32 nearPlane, const f32 farPlane, SIMDVectorSingle* outCorners)noexcept{
    const SIMDVectorSingle slopes = VectorSet(rightSlope, leftSlope, bottomSlope, topSlope);
    const SIMDVectorSingle depths[2] = { VectorReplicate(nearPlane), VectorReplicate(farPlane), };
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


inline void FrustumCorners(const SIMDVectorDouble origin, const SIMDVectorDouble orientation, const f64 rightSlope, const f64 leftSlope, const f64 topSlope, const f64 bottomSlope, const f64 nearPlane, const f64 farPlane, SIMDVectorDouble* outCorners)noexcept{
    const SIMDVectorDouble slopes = VectorDoubleSet(rightSlope, leftSlope, bottomSlope, topSlope);
    const SIMDVectorDouble depths[2] = { VectorDoubleReplicate(nearPlane), VectorDoubleReplicate(farPlane), };
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


[[nodiscard]] inline SIMDVectorSingle NWB_SIMD_CALL BoundingSphere::TransformSphereValue(SIMDVectorSingle sphereValue, const f32 scale, const SIMDVectorSingle rotation, const SIMDVectorSingle translation)noexcept{
    const SIMDVectorSingle transformedCenter = VectorAdd(Vector3Rotate(VectorScale(CollisionDetail::SphereCenter(sphereValue), scale), rotation), translation);
    return CollisionDetail::SphereCenterRadius(transformedCenter, VectorScale(VectorSplatW(sphereValue), Abs(scale)));
}

[[nodiscard]] inline SIMDVectorDouble NWB_SIMD_CALL BoundingDoubleSphere::TransformSphereValue(SIMDVectorDouble sphereValue, const f64 scale, const SIMDVectorDouble rotation, const SIMDVectorDouble translation)noexcept{
    const SIMDVectorDouble transformedCenter = VectorDoubleAdd(VectorDouble3Rotate(VectorDoubleScale(CollisionDoubleDetail::SphereCenter(sphereValue), scale), rotation), translation);
    return CollisionDoubleDetail::SphereCenterRadius(transformedCenter, VectorDoubleScale(VectorDoubleSplatW(sphereValue), Abs(scale)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

