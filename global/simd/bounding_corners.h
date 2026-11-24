// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix.h"
#include "collision_detail.h"
#include "collision_plane.h"
#include "collision_sdf.h"
#include "collision_aabb.h"
#include "collision_triangle.h"
#include "bounding_sphere_types.h"
#include "bounding_box_types.h"
#include "bounding_oriented_box_types.h"
#include "bounding_frustum_types.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CollisionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle AabbCorner(const SIMDVectorSingle center, const SIMDVectorSingle extents, const u32 index)noexcept{
    return VectorMultiplyAdd(extents, BoxCornerOffset(index), center);
}

FOUNDATION_INLINE void ObbCornerAxes(const SIMDVectorSingle extents, const SIMDVectorSingle orientation, SIMDVectorSingle& outAxis0, SIMDVectorSingle& outAxis1, SIMDVectorSingle& outAxis2)noexcept{
    ObbAxes(orientation, outAxis0, outAxis1, outAxis2);
    outAxis0 = VectorMultiply(outAxis0, VectorSplatX(extents));
    outAxis1 = VectorMultiply(outAxis1, VectorSplatY(extents));
    outAxis2 = VectorMultiply(outAxis2, VectorSplatZ(extents));
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle ObbCornerFromAxes(const SIMDVectorSingle center, const SIMDVectorSingle axis0, const SIMDVectorSingle axis1, const SIMDVectorSingle axis2, const u32 index)noexcept{
    const SIMDVectorSingle offset = BoxCornerOffset(index);
    SIMDVectorSingle corner = VectorMultiply(axis0, VectorSplatX(offset));
    corner = VectorMultiplyAdd(axis1, VectorSplatY(offset), corner);
    corner = VectorMultiplyAdd(axis2, VectorSplatZ(offset), corner);
    return VectorAdd(corner, center);
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FrustumCornerSlopes(const f32 rightSlope, const f32 leftSlope, const f32 topSlope, const f32 bottomSlope)noexcept{
    return VectorSet(rightSlope, leftSlope, bottomSlope, topSlope);
}

[[nodiscard]] inline SIMDVectorSingle FrustumCornerFromSlopes(
    const SIMDVectorSingle origin,
    const SIMDVectorSingle orientation,
    const SIMDVectorSingle slopes,
    const SIMDVectorSingle nearDepth,
    const SIMDVectorSingle farDepth,
    const u32 index
)noexcept{
    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle depth = (index < 4u) ? nearDepth : farDepth;
    const SIMDVectorSingle scaledSlopes = VectorMultiply(slopes, depth);
    const SIMDVectorSingle right = VectorSplatX(scaledSlopes);
    const SIMDVectorSingle left = VectorSplatY(scaledSlopes);
    const SIMDVectorSingle bottom = VectorSplatZ(scaledSlopes);
    const SIMDVectorSingle top = VectorSplatW(scaledSlopes);
    SIMDVectorSingle localCorner{};
    switch(index & 3u){
        case 0u:
            localCorner = VectorMergeX(right, top, depth, zero);
            break;
        case 1u:
            localCorner = VectorMergeX(left, top, depth, zero);
            break;
        case 2u:
            localCorner = VectorMergeX(left, bottom, depth, zero);
            break;
        default:
            localCorner = VectorMergeX(right, bottom, depth, zero);
            break;
    }
    return VectorAdd(Vector3Rotate(localCorner, orientation), origin);
}

[[nodiscard]] inline SIMDVectorSingle FrustumCorner(
    const SIMDVectorSingle origin,
    const SIMDVectorSingle orientation,
    const f32 rightSlope,
    const f32 leftSlope,
    const f32 topSlope,
    const f32 bottomSlope,
    const f32 nearPlane,
    const f32 farPlane,
    const u32 index
)noexcept{
    return FrustumCornerFromSlopes(
        origin,
        orientation,
        FrustumCornerSlopes(rightSlope, leftSlope, topSlope, bottomSlope),
        VectorReplicate(nearPlane),
        VectorReplicate(farPlane),
        index
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CollisionDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] FOUNDATION_INLINE SIMDVectorDouble AabbCorner(const SIMDVectorDouble center, const SIMDVectorDouble extents, const u64 index)noexcept{
    return VectorDoubleMultiplyAdd(extents, BoxCornerOffset(index), center);
}

FOUNDATION_INLINE void ObbCornerAxes(const SIMDVectorDouble extents, const SIMDVectorDouble orientation, SIMDVectorDouble& outAxis0, SIMDVectorDouble& outAxis1, SIMDVectorDouble& outAxis2)noexcept{
    ObbAxes(orientation, outAxis0, outAxis1, outAxis2);
    outAxis0 = VectorDoubleMultiply(outAxis0, VectorDoubleSplatX(extents));
    outAxis1 = VectorDoubleMultiply(outAxis1, VectorDoubleSplatY(extents));
    outAxis2 = VectorDoubleMultiply(outAxis2, VectorDoubleSplatZ(extents));
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorDouble ObbCornerFromAxes(const SIMDVectorDouble center, const SIMDVectorDouble axis0, const SIMDVectorDouble axis1, const SIMDVectorDouble axis2, const u64 index)noexcept{
    const SIMDVectorDouble offset = BoxCornerOffset(index);
    SIMDVectorDouble corner = VectorDoubleMultiply(axis0, VectorDoubleSplatX(offset));
    corner = VectorDoubleMultiplyAdd(axis1, VectorDoubleSplatY(offset), corner);
    corner = VectorDoubleMultiplyAdd(axis2, VectorDoubleSplatZ(offset), corner);
    return VectorDoubleAdd(corner, center);
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorDouble FrustumCornerSlopes(const f64 rightSlope, const f64 leftSlope, const f64 topSlope, const f64 bottomSlope)noexcept{
    return VectorDoubleSet(rightSlope, leftSlope, bottomSlope, topSlope);
}

[[nodiscard]] inline SIMDVectorDouble FrustumCornerFromSlopes(
    const SIMDVectorDouble origin,
    const SIMDVectorDouble orientation,
    const SIMDVectorDouble slopes,
    const SIMDVectorDouble nearDepth,
    const SIMDVectorDouble farDepth,
    const u64 index
)noexcept{
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble depth = (index < 4u) ? nearDepth : farDepth;
    const SIMDVectorDouble scaledSlopes = VectorDoubleMultiply(slopes, depth);
    const SIMDVectorDouble right = VectorDoubleSplatX(scaledSlopes);
    const SIMDVectorDouble left = VectorDoubleSplatY(scaledSlopes);
    const SIMDVectorDouble bottom = VectorDoubleSplatZ(scaledSlopes);
    const SIMDVectorDouble top = VectorDoubleSplatW(scaledSlopes);
    SIMDVectorDouble localCorner{};
    switch(index & 3u){
        case 0u:
            localCorner = VectorDoubleMergeX(right, top, depth, zero);
            break;
        case 1u:
            localCorner = VectorDoubleMergeX(left, top, depth, zero);
            break;
        case 2u:
            localCorner = VectorDoubleMergeX(left, bottom, depth, zero);
            break;
        default:
            localCorner = VectorDoubleMergeX(right, bottom, depth, zero);
            break;
    }
    return VectorDoubleAdd(VectorDouble3Rotate(localCorner, orientation), origin);
}

[[nodiscard]] inline SIMDVectorDouble FrustumCorner(
    const SIMDVectorDouble origin,
    const SIMDVectorDouble orientation,
    const f64 rightSlope,
    const f64 leftSlope,
    const f64 topSlope,
    const f64 bottomSlope,
    const f64 nearPlane,
    const f64 farPlane,
    const u64 index
)noexcept{
    return FrustumCornerFromSlopes(
        origin,
        orientation,
        FrustumCornerSlopes(rightSlope, leftSlope, topSlope, bottomSlope),
        VectorDoubleReplicate(nearPlane),
        VectorDoubleReplicate(farPlane),
        index
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

