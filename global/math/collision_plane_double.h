// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix_double.h"
#include "collision_detail_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PlaneDoubleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL Distance(SIMDVectorDouble plane, SIMDVectorDouble point)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL FromPointNormal(SIMDVectorDouble normal, SIMDVectorDouble point, SIMDVectorDouble fallbackNormal)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL PlaneDoubleTests::Distance(const SIMDVectorDouble plane, const SIMDVectorDouble point)noexcept{
    return CollisionDoubleDetail::PlaneDoubleDistance(plane, point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL PlaneDoubleTests::FromPointNormal(
    const SIMDVectorDouble normal,
    const SIMDVectorDouble point,
    const SIMDVectorDouble fallbackNormal
)noexcept{
    const SIMDVectorDouble lengthSquared = VectorDouble3LengthSq(normal);
    SIMDVectorDouble unitNormal = VectorDoubleSetW(fallbackNormal, 0.0);
    if(
        VectorDoubleIsFinite(lengthSquared, VectorDoubleComponentMask::s_DoubleXYZW)
        && VectorDouble4Greater(lengthSquared, VectorDoubleZero())
    )
        unitNormal = VectorDoubleSetW(VectorDoubleMultiply(normal, VectorDoubleReciprocalSqrt(lengthSquared)), 0.0);
    return VectorDoubleSelect(unitNormal, VectorDoubleNegate(VectorDouble3Dot(unitNormal, point)), s_DoubleSIMDMaskW);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

