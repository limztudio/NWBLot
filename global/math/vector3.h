// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "vector2.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool NWB_SIMD_CALL Vector3Equal(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorEqual(v0, v1)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }
NWB_INLINE bool NWB_SIMD_CALL Vector3EqualInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorEqualInt(v0, v1)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }
NWB_INLINE bool NWB_SIMD_CALL Vector3NearEqual(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle epsilon)noexcept{ return (VectorMoveMask(VectorNearEqual(v0, v1, epsilon)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }
NWB_INLINE bool NWB_SIMD_CALL Vector3NotEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return !Vector3Equal(v0, v1); }
NWB_INLINE bool NWB_SIMD_CALL Vector3NotEqualInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return !Vector3EqualInt(v0, v1); }
NWB_INLINE bool NWB_SIMD_CALL Vector3Greater(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorGreater(v0, v1)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }
NWB_INLINE bool NWB_SIMD_CALL Vector3GreaterOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorGreaterOrEqual(v0, v1)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }
NWB_INLINE bool NWB_SIMD_CALL Vector3Less(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return Vector3Greater(v1, v0); }
NWB_INLINE bool NWB_SIMD_CALL Vector3LessOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return Vector3GreaterOrEqual(v1, v0); }
NWB_INLINE bool NWB_SIMD_CALL Vector3InBounds(SIMDVectorSingle value, SIMDVectorSingle bounds)noexcept{ return (VectorMoveMask(VectorInBounds(value, bounds)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }
NWB_INLINE bool NWB_SIMD_CALL Vector3IsNaN(SIMDVectorSingle value)noexcept{ return (VectorMoveMask(VectorIsNaN(value)) & VectorComponentMask::s_XYZ) != 0; }
NWB_INLINE bool NWB_SIMD_CALL Vector3IsInfinite(SIMDVectorSingle value)noexcept{ return (VectorMoveMask(VectorIsInfinite(value)) & VectorComponentMask::s_XYZ) != 0; }
NWB_INLINE bool NWB_SIMD_CALL Vector3IsFinite(SIMDVectorSingle value)noexcept{ return !Vector3IsNaN(value) && !Vector3IsInfinite(value); }

NWB_INLINE u32 NWB_SIMD_CALL Vector3EqualR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqual(v0, v1)), VectorComponentMask::s_XYZ); }
NWB_INLINE u32 NWB_SIMD_CALL Vector3EqualIntR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqualInt(v0, v1)), VectorComponentMask::s_XYZ); }
NWB_INLINE u32 NWB_SIMD_CALL Vector3GreaterR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreater(v0, v1)), VectorComponentMask::s_XYZ); }
NWB_INLINE u32 NWB_SIMD_CALL Vector3GreaterOrEqualR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreaterOrEqual(v0, v1)), VectorComponentMask::s_XYZ); }
NWB_INLINE u32 NWB_SIMD_CALL Vector3InBoundsR(SIMDVectorSingle value, SIMDVectorSingle bounds)noexcept{ return SIMDVectorDetail::BoundsMaskR(VectorMoveMask(VectorInBounds(value, bounds)), VectorComponentMask::s_XYZ); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3Dot(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
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

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3Cross(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    SIMDVectorSingle temp0 = VectorSwizzle<1, 2, 0, 3>(v0);
    SIMDVectorSingle temp1 = VectorSwizzle<2, 0, 1, 3>(v1);
    SIMDVectorSingle result = VectorMultiply(temp0, temp1);
    temp0 = VectorSwizzle<2, 0, 1, 3>(v0);
    temp1 = VectorSwizzle<1, 2, 0, 3>(v1);
    return VectorAndInt(VectorNegativeMultiplySubtract(temp0, temp1, result), s_SIMDMask3);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3LengthSq(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_dp_ps(value, value, 0x7F);
#else
    return Vector3Dot(value, value);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3ReciprocalLength(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVectorSingle lengthSq = _mm_dp_ps(value, value, 0x7F);
    const SIMDVectorSingle length = _mm_sqrt_ps(lengthSq);
    return _mm_div_ps(s_SIMDOne, length);
#else
    return VectorReciprocalSqrt(Vector3LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3ReciprocalLengthEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_rsqrt_ps(_mm_dp_ps(value, value, 0x7F));
#else
    return VectorReciprocalSqrtEst(Vector3LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3Length(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x7F));
#else
    return VectorSqrt(Vector3LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3LengthEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x7F));
#else
    return VectorSqrtEst(Vector3LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3Normalize(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    SIMDVectorSingle lengthSq = _mm_dp_ps(value, value, 0x7F);
    const SIMDVectorSingle length = _mm_sqrt_ps(lengthSq);
    const SIMDVectorSingle zeroMask = _mm_cmpneq_ps(_mm_setzero_ps(), length);
    lengthSq = _mm_cmpneq_ps(lengthSq, s_SIMDInfinity);
    SIMDVectorSingle result = _mm_div_ps(value, length);
    result = _mm_and_ps(result, zeroMask);
    return _mm_and_ps(_mm_or_ps(_mm_andnot_ps(lengthSq, s_SIMDQNaN), _mm_and_ps(result, lengthSq)), s_SIMDMask3);
#else
    const SIMDVectorSingle lengthSq = Vector3LengthSq(value);
    const SIMDVectorSingle length = VectorSqrt(lengthSq);
    SIMDVectorSingle result = VectorDivide(value, length);
    result = VectorAndInt(result, VectorNotEqual(length, VectorZero()));
    return VectorAndInt(VectorSelect(s_SIMDQNaN, result, VectorNotEqualInt(lengthSq, s_SIMDInfinity)), s_SIMDMask3);
#endif
}

[[nodiscard]] NWB_INLINE Expected<SIMDVectorSingle> NWB_SIMD_CALL Vector3TryNormalize(SIMDVectorSingle value)noexcept{
    const SIMDVectorSingle lengthSquared = Vector3LengthSq(value);
    if(!VectorIsFinite(lengthSquared, VectorComponentMask::s_XYZW) || !Vector3Greater(lengthSquared, VectorZero()))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle normalized = Vector3Normalize(value);
    NWB_ASSERT(Vector3IsFinite(normalized));
    return normalized;
}

[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3NormalizeOr(SIMDVectorSingle value, SIMDVectorSingle fallback, const f32 minLengthSquared)noexcept{
    const SIMDVectorSingle lengthSquared = Vector3LengthSq(value);
    return SIMDVectorDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, s_SIMDMask3);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3NormalizeEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVectorSingle lengthSq = _mm_dp_ps(value, value, 0x7F);
    return _mm_and_ps(_mm_mul_ps(value, _mm_rsqrt_ps(lengthSq)), s_SIMDMask3);
#else
    return VectorAndInt(VectorMultiply(value, Vector3ReciprocalLengthEst(value)), s_SIMDMask3);
#endif
}
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3ClampLengthV(SIMDVectorSingle value, SIMDVectorSingle lengthMin, SIMDVectorSingle lengthMax)noexcept{
    NWB_ASSERT((VectorGetY(lengthMin) == VectorGetX(lengthMin)) && (VectorGetZ(lengthMin) == VectorGetX(lengthMin)));
    NWB_ASSERT((VectorGetY(lengthMax) == VectorGetX(lengthMax)) && (VectorGetZ(lengthMax) == VectorGetX(lengthMax)));
    NWB_ASSERT(Vector3GreaterOrEqual(lengthMin, s_SIMDZero));
    NWB_ASSERT(Vector3GreaterOrEqual(lengthMax, s_SIMDZero));
    NWB_ASSERT(Vector3GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVectorSingle lengthSq = Vector3LengthSq(value);
    return VectorAndInt(SIMDVectorDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax), s_SIMDMask3);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3ClampLength(SIMDVectorSingle value, f32 lengthMin, f32 lengthMax)noexcept{
    return Vector3ClampLengthV(value, VectorReplicate(lengthMin), VectorReplicate(lengthMax));
}
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3Reflect(SIMDVectorSingle incident, SIMDVectorSingle normal)noexcept{
    SIMDVectorSingle result = Vector3Dot(incident, normal);
    result = VectorAdd(result, result);
    return VectorNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3RefractV(SIMDVectorSingle incident, SIMDVectorSingle normal, SIMDVectorSingle refractionIndex)noexcept{
    return SIMDVectorDetail::RefractV(incident, normal, refractionIndex, Vector3Dot(incident, normal));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3Refract(SIMDVectorSingle incident, SIMDVectorSingle normal, f32 refractionIndex)noexcept{ return Vector3RefractV(incident, normal, VectorReplicate(refractionIndex)); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3Rotate(SIMDVectorSingle value, SIMDVectorSingle rotationQuaternion)noexcept{
    const SIMDVectorSingle q = VectorAndInt(rotationQuaternion, s_SIMDMask3);
    const SIMDVectorSingle t = VectorScale(Vector3Cross(q, value), 2.0f);
    return VectorAdd(VectorAdd(value, VectorMultiply(t, VectorSplatW(rotationQuaternion))), Vector3Cross(q, t));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3InverseRotate(SIMDVectorSingle value, SIMDVectorSingle rotationQuaternion)noexcept{
    return Vector3Rotate(value, VectorXorInt(rotationQuaternion, s_SIMDNegate3));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3Orthogonal(SIMDVectorSingle value)noexcept{
    const SIMDVectorSingle zero = VectorZero();
    const SIMDVectorSingle z = VectorSplatZ(value);
    const SIMDVectorSingle yzyy = VectorSwizzle<1, 2, 1, 1>(value);
    const SIMDVectorSingle negativeValue = VectorSubtract(zero, value);
    const SIMDVectorSingle zIsNegative = VectorLess(z, zero);
    const SIMDVectorSingle yzyyIsNegative = VectorLess(yzyy, zero);
    const SIMDVectorSingle s = VectorAdd(yzyy, z);
    const SIMDVectorSingle d = VectorSubtract(yzyy, z);
    const SIMDVectorSingle select = VectorEqualInt(zIsNegative, yzyyIsNegative);
    const SIMDVectorSingle r0 = VectorPermute<4, 0, 0, 0>(negativeValue, s);
    const SIMDVectorSingle r1 = VectorPermute<4, 0, 0, 0>(value, d);
    return VectorSelect(r1, r0, select);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3AngleBetweenNormals(SIMDVectorSingle n0, SIMDVectorSingle n1)noexcept{ return VectorACos(VectorClamp(Vector3Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3AngleBetweenNormalsEst(SIMDVectorSingle n0, SIMDVectorSingle n1)noexcept{ return VectorACosEst(VectorClamp(Vector3Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3AngleBetweenVectors(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    const SIMDVectorSingle reciprocalLength0 = Vector3ReciprocalLength(v0);
    const SIMDVectorSingle reciprocalLength1 = Vector3ReciprocalLength(v1);
    SIMDVectorSingle cosAngle = VectorMultiply(Vector3Dot(v0, v1), VectorMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorClamp(cosAngle, s_SIMDNegativeOne, s_SIMDOne);
    return VectorACos(cosAngle);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3LinePointDistance(SIMDVectorSingle linePoint0, SIMDVectorSingle linePoint1, SIMDVectorSingle point)noexcept{
    const SIMDVectorSingle line = VectorSubtract(linePoint1, linePoint0);
    const SIMDVectorSingle pointVector = VectorSubtract(point, linePoint0);
    const SIMDVectorSingle projectionScale = VectorDivide(Vector3Dot(pointVector, line), Vector3LengthSq(line));
    return Vector3Length(VectorSubtract(pointVector, VectorMultiply(line, projectionScale)));
}

NWB_INLINE void NWB_SIMD_CALL Vector3ComponentsFromNormal(SIMDVectorSingle& outParallel, SIMDVectorSingle& outPerpendicular, SIMDVectorSingle value, SIMDVectorSingle normal)noexcept{
    const SIMDVectorSingle scale = Vector3Dot(value, normal);
    outParallel = VectorMultiply(normal, scale);
    outPerpendicular = VectorSubtract(value, outParallel);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3Transform(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept;
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3TransformCoord(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept;
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector3TransformNormal(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

