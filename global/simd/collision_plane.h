// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "matrix.h"
#include "collision_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PlaneTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL Distance(SIMDVectorSingle plane, SIMDVectorSingle point)noexcept;
[[nodiscard]] SIMDVectorSingle NWB_SIMD_CALL FromPointNormal(SIMDVectorSingle normal, SIMDVectorSingle point, SIMDVectorSingle fallbackNormal)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PlaneDoubleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL Distance(SIMDVectorDouble plane, SIMDVectorDouble point)noexcept;
[[nodiscard]] SIMDVectorDouble NWB_SIMD_CALL FromPointNormal(SIMDVectorDouble normal, SIMDVectorDouble point, SIMDVectorDouble fallbackNormal)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL PlaneTests::Distance(const SIMDVectorSingle plane, const SIMDVectorSingle point)noexcept{
    return CollisionDetail::PlaneDistance(plane, point);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL PlaneDoubleTests::Distance(const SIMDVectorDouble plane, const SIMDVectorDouble point)noexcept{
    return CollisionDoubleDetail::PlaneDoubleDistance(plane, point);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL PlaneTests::FromPointNormal(const SIMDVectorSingle normal, const SIMDVectorSingle point, const SIMDVectorSingle fallbackNormal)noexcept{
    const SIMDVectorSingle lengthSquared = Vector3LengthSq(normal);
    SIMDVectorSingle unitNormal = VectorSetW(fallbackNormal, 0.0f);
    if(VectorIsFinite(lengthSquared, VectorComponentMask::s_XYZW) && Vector4Greater(lengthSquared, VectorZero()))
        unitNormal = VectorSetW(VectorMultiply(normal, VectorReciprocalSqrt(lengthSquared)), 0.0f);
    return VectorSelect(unitNormal, VectorNegate(Vector3Dot(unitNormal, point)), s_SIMDMaskW);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL PlaneDoubleTests::FromPointNormal(const SIMDVectorDouble normal, const SIMDVectorDouble point, const SIMDVectorDouble fallbackNormal)noexcept{
    const SIMDVectorDouble lengthSquared = VectorDouble3LengthSq(normal);
    SIMDVectorDouble unitNormal = VectorDoubleSetW(fallbackNormal, 0.0);
    if(VectorDoubleIsFinite(lengthSquared, VectorDoubleComponentMask::s_DoubleXYZW) && VectorDouble4Greater(lengthSquared, VectorDoubleZero()))
        unitNormal = VectorDoubleSetW(VectorDoubleMultiply(normal, VectorDoubleReciprocalSqrt(lengthSquared)), 0.0);
    return VectorDoubleSelect(unitNormal, VectorDoubleNegate(VectorDouble3Dot(unitNormal, point)), s_DoubleSIMDMaskW);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

