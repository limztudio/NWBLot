// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector2.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool SIMDCALL Vector3Equal(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorEqual(v0, v1)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }
NWB_INLINE bool SIMDCALL Vector3EqualInt(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorEqualInt(v0, v1)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }
NWB_INLINE bool SIMDCALL Vector3NearEqual(SIMDVector v0, SIMDVector v1, SIMDVector epsilon)noexcept{ return (VectorMoveMask(VectorNearEqual(v0, v1, epsilon)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }
NWB_INLINE bool SIMDCALL Vector3NotEqual(SIMDVector v0, SIMDVector v1)noexcept{ return !Vector3Equal(v0, v1); }
NWB_INLINE bool SIMDCALL Vector3NotEqualInt(SIMDVector v0, SIMDVector v1)noexcept{ return !Vector3EqualInt(v0, v1); }
NWB_INLINE bool SIMDCALL Vector3Greater(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorGreater(v0, v1)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }
NWB_INLINE bool SIMDCALL Vector3GreaterOrEqual(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorGreaterOrEqual(v0, v1)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }
NWB_INLINE bool SIMDCALL Vector3Less(SIMDVector v0, SIMDVector v1)noexcept{ return Vector3Greater(v1, v0); }
NWB_INLINE bool SIMDCALL Vector3LessOrEqual(SIMDVector v0, SIMDVector v1)noexcept{ return Vector3GreaterOrEqual(v1, v0); }
NWB_INLINE bool SIMDCALL Vector3InBounds(SIMDVector value, SIMDVector bounds)noexcept{ return (VectorMoveMask(VectorInBounds(value, bounds)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }
NWB_INLINE bool SIMDCALL Vector3IsNaN(SIMDVector value)noexcept{ return (VectorMoveMask(VectorIsNaN(value)) & VectorComponentMask::s_XYZ) != 0; }
NWB_INLINE bool SIMDCALL Vector3IsInfinite(SIMDVector value)noexcept{ return (VectorMoveMask(VectorIsInfinite(value)) & VectorComponentMask::s_XYZ) != 0; }
NWB_INLINE bool SIMDCALL Vector3IsFinite(SIMDVector value)noexcept{ return !Vector3IsNaN(value) && !Vector3IsInfinite(value); }

NWB_INLINE u32 SIMDCALL Vector3EqualR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqual(v0, v1)), VectorComponentMask::s_XYZ); }
NWB_INLINE u32 SIMDCALL Vector3EqualIntR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqualInt(v0, v1)), VectorComponentMask::s_XYZ); }
NWB_INLINE u32 SIMDCALL Vector3GreaterR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreater(v0, v1)), VectorComponentMask::s_XYZ); }
NWB_INLINE u32 SIMDCALL Vector3GreaterOrEqualR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreaterOrEqual(v0, v1)), VectorComponentMask::s_XYZ); }
NWB_INLINE u32 SIMDCALL Vector3InBoundsR(SIMDVector value, SIMDVector bounds)noexcept{ return SIMDVectorDetail::BoundsMaskR(VectorMoveMask(VectorInBounds(value, bounds)), VectorComponentMask::s_XYZ); }

NWB_INLINE SIMDVector SIMDCALL Vector3Dot(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(VectorGetX(v0) * VectorGetX(v1) + VectorGetY(v0) * VectorGetY(v1) + VectorGetZ(v0) * VectorGetZ(v1));
#elif defined(NWB_HAS_NEON)
    float32x4_t product = vmulq_f32(v0, v1);
    float32x2_t lo = vget_low_f32(product);
    float32x2_t hi = vget_high_f32(product);
    lo = vpadd_f32(lo, lo);
    hi = vdup_lane_f32(hi, 0);
    lo = vadd_f32(lo, hi);
    return vcombine_f32(lo, lo);
#else
    return _mm_dp_ps(v0, v1, 0x7F);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3Cross(SIMDVector v0, SIMDVector v1)noexcept{
    SIMDVector temp0 = VectorSwizzle<1, 2, 0, 3>(v0);
    SIMDVector temp1 = VectorSwizzle<2, 0, 1, 3>(v1);
    SIMDVector result = VectorMultiply(temp0, temp1);
    temp0 = VectorSwizzle<2, 0, 1, 3>(v0);
    temp1 = VectorSwizzle<1, 2, 0, 3>(v1);
    return VectorAndInt(VectorNegativeMultiplySubtract(temp0, temp1, result), s_SIMDMask3);
}

NWB_INLINE SIMDVector SIMDCALL Vector3LengthSq(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_dp_ps(value, value, 0x7F);
#else
    return Vector3Dot(value, value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3ReciprocalLength(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector lengthSq = _mm_dp_ps(value, value, 0x7F);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    return _mm_div_ps(s_SIMDOne, length);
#else
    return VectorReciprocalSqrt(Vector3LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3ReciprocalLengthEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_rsqrt_ps(_mm_dp_ps(value, value, 0x7F));
#else
    return VectorReciprocalSqrtEst(Vector3LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3Length(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x7F));
#else
    return VectorSqrt(Vector3LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3LengthEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x7F));
#else
    return VectorSqrtEst(Vector3LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3Normalize(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    SIMDVector lengthSq = _mm_dp_ps(value, value, 0x7F);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    const SIMDVector zeroMask = _mm_cmpneq_ps(_mm_setzero_ps(), length);
    lengthSq = _mm_cmpneq_ps(lengthSq, s_SIMDInfinity);
    SIMDVector result = _mm_div_ps(value, length);
    result = _mm_and_ps(result, zeroMask);
    return _mm_and_ps(_mm_or_ps(_mm_andnot_ps(lengthSq, s_SIMDQNaN), _mm_and_ps(result, lengthSq)), s_SIMDMask3);
#else
    const SIMDVector lengthSq = Vector3LengthSq(value);
    const SIMDVector length = VectorSqrt(lengthSq);
    SIMDVector result = VectorDivide(value, length);
    result = VectorAndInt(result, VectorNotEqual(length, VectorZero()));
    return VectorAndInt(VectorSelect(s_SIMDQNaN, result, VectorNotEqualInt(lengthSq, s_SIMDInfinity)), s_SIMDMask3);
#endif
}

[[nodiscard]] NWB_INLINE bool SIMDCALL Vector3TryNormalize(SIMDVector value, SIMDVector& outValue)noexcept{
    const SIMDVector lengthSquared = Vector3LengthSq(value);
    if(!VectorIsFinite(lengthSquared, VectorComponentMask::s_XYZW) || !Vector3Greater(lengthSquared, VectorZero()))
        return false;

    const SIMDVector normalized = Vector3Normalize(value);
    NWB_ASSERT(Vector3IsFinite(normalized));
    outValue = normalized;
    return true;
}

[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL Vector3NormalizeOr(SIMDVector value, SIMDVector fallback, const f32 minLengthSquared)noexcept{
    const SIMDVector lengthSquared = Vector3LengthSq(value);
    return SIMDVectorDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, s_SIMDMask3);
}

NWB_INLINE SIMDVector SIMDCALL Vector3NormalizeEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector lengthSq = _mm_dp_ps(value, value, 0x7F);
    return _mm_and_ps(_mm_mul_ps(value, _mm_rsqrt_ps(lengthSq)), s_SIMDMask3);
#else
    return VectorAndInt(VectorMultiply(value, Vector3ReciprocalLengthEst(value)), s_SIMDMask3);
#endif
}
NWB_INLINE SIMDVector SIMDCALL Vector3ClampLengthV(SIMDVector value, SIMDVector lengthMin, SIMDVector lengthMax)noexcept{
    NWB_ASSERT((VectorGetY(lengthMin) == VectorGetX(lengthMin)) && (VectorGetZ(lengthMin) == VectorGetX(lengthMin)));
    NWB_ASSERT((VectorGetY(lengthMax) == VectorGetX(lengthMax)) && (VectorGetZ(lengthMax) == VectorGetX(lengthMax)));
    NWB_ASSERT(Vector3GreaterOrEqual(lengthMin, s_SIMDZero));
    NWB_ASSERT(Vector3GreaterOrEqual(lengthMax, s_SIMDZero));
    NWB_ASSERT(Vector3GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVector lengthSq = Vector3LengthSq(value);
    return VectorAndInt(SIMDVectorDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax), s_SIMDMask3);
}

NWB_INLINE SIMDVector SIMDCALL Vector3ClampLength(SIMDVector value, f32 lengthMin, f32 lengthMax)noexcept{
    return Vector3ClampLengthV(value, VectorReplicate(lengthMin), VectorReplicate(lengthMax));
}
NWB_INLINE SIMDVector SIMDCALL Vector3Reflect(SIMDVector incident, SIMDVector normal)noexcept{
    SIMDVector result = Vector3Dot(incident, normal);
    result = VectorAdd(result, result);
    return VectorNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVector SIMDCALL Vector3RefractV(SIMDVector incident, SIMDVector normal, SIMDVector refractionIndex)noexcept{
    return SIMDVectorDetail::RefractV(incident, normal, refractionIndex, Vector3Dot(incident, normal));
}

NWB_INLINE SIMDVector SIMDCALL Vector3Refract(SIMDVector incident, SIMDVector normal, f32 refractionIndex)noexcept{ return Vector3RefractV(incident, normal, VectorReplicate(refractionIndex)); }

NWB_INLINE SIMDVector SIMDCALL Vector3Rotate(SIMDVector value, SIMDVector rotationQuaternion)noexcept{
    const SIMDVector q = VectorAndInt(rotationQuaternion, s_SIMDMask3);
    const SIMDVector t = VectorScale(Vector3Cross(q, value), 2.0f);
    return VectorAdd(VectorAdd(value, VectorMultiply(t, VectorSplatW(rotationQuaternion))), Vector3Cross(q, t));
}

NWB_INLINE SIMDVector SIMDCALL Vector3InverseRotate(SIMDVector value, SIMDVector rotationQuaternion)noexcept{
    return Vector3Rotate(value, VectorXorInt(rotationQuaternion, s_SIMDNegate3));
}

NWB_INLINE SIMDVector SIMDCALL Vector3Orthogonal(SIMDVector value)noexcept{
    const SIMDVector zero = VectorZero();
    const SIMDVector z = VectorSplatZ(value);
    const SIMDVector yzyy = VectorSwizzle<1, 2, 1, 1>(value);
    const SIMDVector negativeValue = VectorSubtract(zero, value);
    const SIMDVector zIsNegative = VectorLess(z, zero);
    const SIMDVector yzyyIsNegative = VectorLess(yzyy, zero);
    const SIMDVector s = VectorAdd(yzyy, z);
    const SIMDVector d = VectorSubtract(yzyy, z);
    const SIMDVector select = VectorEqualInt(zIsNegative, yzyyIsNegative);
    const SIMDVector r0 = VectorPermute<4, 0, 0, 0>(negativeValue, s);
    const SIMDVector r1 = VectorPermute<4, 0, 0, 0>(value, d);
    return VectorSelect(r1, r0, select);
}

NWB_INLINE SIMDVector SIMDCALL Vector3AngleBetweenNormals(SIMDVector n0, SIMDVector n1)noexcept{ return VectorACos(VectorClamp(Vector3Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVector SIMDCALL Vector3AngleBetweenNormalsEst(SIMDVector n0, SIMDVector n1)noexcept{ return VectorACosEst(VectorClamp(Vector3Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVector SIMDCALL Vector3AngleBetweenVectors(SIMDVector v0, SIMDVector v1)noexcept{
    const SIMDVector reciprocalLength0 = Vector3ReciprocalLength(v0);
    const SIMDVector reciprocalLength1 = Vector3ReciprocalLength(v1);
    SIMDVector cosAngle = VectorMultiply(Vector3Dot(v0, v1), VectorMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorClamp(cosAngle, s_SIMDNegativeOne, s_SIMDOne);
    return VectorACos(cosAngle);
}

NWB_INLINE SIMDVector SIMDCALL Vector3LinePointDistance(SIMDVector linePoint0, SIMDVector linePoint1, SIMDVector point)noexcept{
    const SIMDVector line = VectorSubtract(linePoint1, linePoint0);
    const SIMDVector pointVector = VectorSubtract(point, linePoint0);
    const SIMDVector projectionScale = VectorDivide(Vector3Dot(pointVector, line), Vector3LengthSq(line));
    return Vector3Length(VectorSubtract(pointVector, VectorMultiply(line, projectionScale)));
}

NWB_INLINE void SIMDCALL Vector3ComponentsFromNormal(SIMDVector& outParallel, SIMDVector& outPerpendicular, SIMDVector value, SIMDVector normal)noexcept{
    const SIMDVector scale = Vector3Dot(value, normal);
    outParallel = VectorMultiply(normal, scale);
    outPerpendicular = VectorSubtract(value, outParallel);
}

NWB_INLINE SIMDVector SIMDCALL Vector3Transform(SIMDVector value, const SIMDMatrix& matrix)noexcept;
NWB_INLINE SIMDVector SIMDCALL Vector3TransformCoord(SIMDVector value, const SIMDMatrix& matrix)noexcept;
NWB_INLINE SIMDVector SIMDCALL Vector3TransformNormal(SIMDVector value, const SIMDMatrix& matrix)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

