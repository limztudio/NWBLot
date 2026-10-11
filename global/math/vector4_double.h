// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector3_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool NWB_SIMD_CALL VectorDouble4Equal(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleEqual(v0, v1)) & VectorDoubleComponentMask::s_DoubleXYZW) == VectorDoubleComponentMask::s_DoubleXYZW; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble4EqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleEqualInt(v0, v1)) & VectorDoubleComponentMask::s_DoubleXYZW) == VectorDoubleComponentMask::s_DoubleXYZW; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble4NearEqual(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble epsilon)noexcept{ return (VectorDoubleMoveMask(VectorDoubleNearEqual(v0, v1, epsilon)) & VectorDoubleComponentMask::s_DoubleXYZW) == VectorDoubleComponentMask::s_DoubleXYZW; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble4NotEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return !VectorDouble4Equal(v0, v1); }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble4NotEqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return !VectorDouble4EqualInt(v0, v1); }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble4Greater(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleGreater(v0, v1)) & VectorDoubleComponentMask::s_DoubleXYZW) == VectorDoubleComponentMask::s_DoubleXYZW; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble4GreaterOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(v0, v1)) & VectorDoubleComponentMask::s_DoubleXYZW) == VectorDoubleComponentMask::s_DoubleXYZW; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble4Less(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDouble4Greater(v1, v0); }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble4LessOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDouble4GreaterOrEqual(v1, v0); }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble4InBounds(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{ return (VectorDoubleMoveMask(VectorDoubleInBounds(value, bounds)) & VectorDoubleComponentMask::s_DoubleXYZW) == VectorDoubleComponentMask::s_DoubleXYZW; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble4IsNaN(SIMDVectorDouble value)noexcept{ return (VectorDoubleMoveMask(VectorDoubleIsNaN(value)) & VectorDoubleComponentMask::s_DoubleXYZW) != 0; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble4IsInfinite(SIMDVectorDouble value)noexcept{ return (VectorDoubleMoveMask(VectorDoubleIsInfinite(value)) & VectorDoubleComponentMask::s_DoubleXYZW) != 0; }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble4EqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDoubleEqualR(v0, v1); }
NWB_INLINE u32 NWB_SIMD_CALL VectorDouble4EqualIntR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDoubleEqualIntR(v0, v1); }
NWB_INLINE u32 NWB_SIMD_CALL VectorDouble4GreaterR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDoubleGreaterR(v0, v1); }
NWB_INLINE u32 NWB_SIMD_CALL VectorDouble4GreaterOrEqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDoubleGreaterOrEqualR(v0, v1); }
NWB_INLINE u32 NWB_SIMD_CALL VectorDouble4InBoundsR(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{ return VectorDoubleInBoundsR(value, bounds); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Dot(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDouble product = VectorDoubleMultiply(v0, v1);
    SIMDVectorDouble result = VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
    result = VectorDoubleAdd(result, VectorDoubleSplatZ(product));
    result = VectorDoubleAdd(result, VectorDoubleSplatW(product));
    return result;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4LengthSq(SIMDVectorDouble value)noexcept{
    return VectorDouble4Dot(value, value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4ReciprocalLength(SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocalSqrt(VectorDouble4LengthSq(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4ReciprocalLengthEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocalSqrtEst(VectorDouble4LengthSq(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Length(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrt(VectorDouble4LengthSq(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4LengthEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrtEst(VectorDouble4LengthSq(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Normalize(SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble lengthSq = VectorDouble4LengthSq(value);
    const SIMDVectorDouble length = VectorDoubleSqrt(lengthSq);
    SIMDVectorDouble result = VectorDoubleDivide(value, length);
    result = VectorDoubleAndInt(result, VectorDoubleNotEqual(length, VectorDoubleZero()));
    return VectorDoubleSelect(s_DoubleSIMDQNaN, result, VectorDoubleNotEqualInt(lengthSq, s_DoubleSIMDInfinity));
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4NormalizeOr(SIMDVectorDouble value, SIMDVectorDouble fallback, const f64 minLengthSquared)noexcept{
    const SIMDVectorDouble lengthSquared = VectorDouble4LengthSq(value);
    return SIMDVectorDoubleDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, VectorDoubleTrueInt());
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4NormalizeEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleMultiply(value, VectorDouble4ReciprocalLengthEst(value));
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4ClampLengthV(SIMDVectorDouble value, SIMDVectorDouble lengthMin, SIMDVectorDouble lengthMax)noexcept{
    NWB_ASSERT((VectorDoubleGetY(lengthMin) == VectorDoubleGetX(lengthMin)) && (VectorDoubleGetZ(lengthMin) == VectorDoubleGetX(lengthMin)) && (VectorDoubleGetW(lengthMin) == VectorDoubleGetX(lengthMin)));
    NWB_ASSERT((VectorDoubleGetY(lengthMax) == VectorDoubleGetX(lengthMax)) && (VectorDoubleGetZ(lengthMax) == VectorDoubleGetX(lengthMax)) && (VectorDoubleGetW(lengthMax) == VectorDoubleGetX(lengthMax)));
    NWB_ASSERT(VectorDouble4GreaterOrEqual(lengthMin, s_DoubleSIMDZero));
    NWB_ASSERT(VectorDouble4GreaterOrEqual(lengthMax, s_DoubleSIMDZero));
    NWB_ASSERT(VectorDouble4GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVectorDouble lengthSq = VectorDouble4LengthSq(value);
    return SIMDVectorDoubleDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4ClampLength(SIMDVectorDouble value, f64 lengthMin, f64 lengthMax)noexcept{
    return VectorDouble4ClampLengthV(value, VectorDoubleReplicate(lengthMin), VectorDoubleReplicate(lengthMax));
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Reflect(SIMDVectorDouble incident, SIMDVectorDouble normal)noexcept{
    SIMDVectorDouble result = VectorDouble4Dot(incident, normal);
    result = VectorDoubleAdd(result, result);
    return VectorDoubleNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4RefractV(SIMDVectorDouble incident, SIMDVectorDouble normal, SIMDVectorDouble refractionIndex)noexcept{
    return SIMDVectorDoubleDetail::RefractV(incident, normal, refractionIndex, VectorDouble4Dot(incident, normal));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Refract(SIMDVectorDouble incident, SIMDVectorDouble normal, f64 refractionIndex)noexcept{ return VectorDouble4RefractV(incident, normal, VectorDoubleReplicate(refractionIndex)); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Orthogonal(SIMDVectorDouble value)noexcept{ return VectorDoubleMultiply(VectorDoubleSwizzle<2, 3, 0, 1>(value), VectorDoubleSet(1.0, 1.0, -1.0, -1.0)); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4AngleBetweenNormals(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{ return VectorDoubleACos(VectorDoubleClamp(VectorDouble4Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne)); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4AngleBetweenNormalsEst(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{ return VectorDoubleACosEst(VectorDoubleClamp(VectorDouble4Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne)); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4AngleBetweenVectors(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDouble reciprocalLength0 = VectorDouble4ReciprocalLength(v0);
    const SIMDVectorDouble reciprocalLength1 = VectorDouble4ReciprocalLength(v1);
    SIMDVectorDouble cosAngle = VectorDoubleMultiply(VectorDouble4Dot(v0, v1), VectorDoubleMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorDoubleClamp(cosAngle, s_DoubleSIMDNegativeOne, s_DoubleSIMDOne);
    return VectorDoubleACos(cosAngle);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Cross(SIMDVectorDouble v1, SIMDVectorDouble v2, SIMDVectorDouble v3)noexcept{
    SIMDVectorDouble result = VectorDoubleMultiply(VectorDoubleSwizzle<2, 3, 1, 2>(v2), VectorDoubleSwizzle<3, 2, 3, 1>(v3));
    SIMDVectorDouble temp = VectorDoubleSwizzle<3, 2, 3, 1>(v2);
    SIMDVectorDouble temp3 = VectorDoubleSwizzle<2, 3, 1, 2>(v3);
    result = VectorDoubleNegativeMultiplySubtract(temp, temp3, result);
    result = VectorDoubleMultiply(result, VectorDoubleSwizzle<1, 0, 0, 0>(v1));

    temp = VectorDoubleSwizzle<1, 3, 0, 2>(v2);
    temp3 = VectorDoubleSwizzle<3, 0, 3, 0>(v3);
    temp3 = VectorDoubleMultiply(temp3, temp);
    temp = VectorDoubleSwizzle<3, 0, 3, 0>(temp);
    SIMDVectorDouble temp1 = VectorDoubleSwizzle<1, 3, 0, 2>(v3);
    temp3 = VectorDoubleNegativeMultiplySubtract(temp, temp1, temp3);
    temp1 = VectorDoubleSwizzle<2, 2, 1, 1>(v1);
    result = VectorDoubleNegativeMultiplySubtract(temp1, temp3, result);

    temp = VectorDoubleSwizzle<1, 2, 0, 1>(v2);
    temp3 = VectorDoubleSwizzle<2, 0, 1, 0>(v3);
    temp3 = VectorDoubleMultiply(temp3, temp);
    temp = VectorDoubleSwizzle<1, 2, 0, 2>(temp);
    temp1 = VectorDoubleSwizzle<1, 2, 0, 1>(v3);
    temp3 = VectorDoubleNegativeMultiplySubtract(temp1, temp, temp3);
    temp1 = VectorDoubleSwizzle<3, 3, 3, 2>(v1);
    return VectorDoubleNegate(VectorDoubleMultiplyAdd(temp3, temp1, result));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

