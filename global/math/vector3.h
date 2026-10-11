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


NWB_INLINE bool NWB_SIMD_CALL VectorDouble3Equal(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleEqual(v0, v1)) & VectorDoubleComponentMask::s_DoubleXYZ) == VectorDoubleComponentMask::s_DoubleXYZ; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble3EqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleEqualInt(v0, v1)) & VectorDoubleComponentMask::s_DoubleXYZ) == VectorDoubleComponentMask::s_DoubleXYZ; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble3NearEqual(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble epsilon)noexcept{ return (VectorDoubleMoveMask(VectorDoubleNearEqual(v0, v1, epsilon)) & VectorDoubleComponentMask::s_DoubleXYZ) == VectorDoubleComponentMask::s_DoubleXYZ; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble3NotEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return !VectorDouble3Equal(v0, v1); }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble3NotEqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return !VectorDouble3EqualInt(v0, v1); }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble3Greater(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleGreater(v0, v1)) & VectorDoubleComponentMask::s_DoubleXYZ) == VectorDoubleComponentMask::s_DoubleXYZ; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble3GreaterOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(v0, v1)) & VectorDoubleComponentMask::s_DoubleXYZ) == VectorDoubleComponentMask::s_DoubleXYZ; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble3Less(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDouble3Greater(v1, v0); }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble3LessOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDouble3GreaterOrEqual(v1, v0); }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble3InBounds(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{ return (VectorDoubleMoveMask(VectorDoubleInBounds(value, bounds)) & VectorDoubleComponentMask::s_DoubleXYZ) == VectorDoubleComponentMask::s_DoubleXYZ; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble3IsNaN(SIMDVectorDouble value)noexcept{ return (VectorDoubleMoveMask(VectorDoubleIsNaN(value)) & VectorDoubleComponentMask::s_DoubleXYZ) != 0; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble3IsInfinite(SIMDVectorDouble value)noexcept{ return (VectorDoubleMoveMask(VectorDoubleIsInfinite(value)) & VectorDoubleComponentMask::s_DoubleXYZ) != 0; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble3IsFinite(SIMDVectorDouble value)noexcept{ return !VectorDouble3IsNaN(value) && !VectorDouble3IsInfinite(value); }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble3EqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleEqual(v0, v1)), VectorDoubleComponentMask::s_DoubleXYZ); }
NWB_INLINE u32 NWB_SIMD_CALL VectorDouble3EqualIntR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleEqualInt(v0, v1)), VectorDoubleComponentMask::s_DoubleXYZ); }
NWB_INLINE u32 NWB_SIMD_CALL VectorDouble3GreaterR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleGreater(v0, v1)), VectorDoubleComponentMask::s_DoubleXYZ); }
NWB_INLINE u32 NWB_SIMD_CALL VectorDouble3GreaterOrEqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(v0, v1)), VectorDoubleComponentMask::s_DoubleXYZ); }
NWB_INLINE u32 NWB_SIMD_CALL VectorDouble3InBoundsR(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{ return SIMDVectorDoubleDetail::BoundsMaskR(VectorDoubleMoveMask(VectorDoubleInBounds(value, bounds)), VectorDoubleComponentMask::s_DoubleXYZ); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3Dot(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    v0 = VectorDoubleAndInt(v0, s_DoubleSIMDMask3);
    v1 = VectorDoubleAndInt(v1, s_DoubleSIMDMask3);
    const SIMDVectorDouble product = VectorDoubleMultiply(v0, v1);
    SIMDVectorDouble result = VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
    result = VectorDoubleAdd(result, VectorDoubleSplatZ(product));
    return result;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3Cross(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    v0 = VectorDoubleAndInt(v0, s_DoubleSIMDMask3);
    v1 = VectorDoubleAndInt(v1, s_DoubleSIMDMask3);
    SIMDVectorDouble temp0 = VectorDoubleSwizzle<1, 2, 0, 3>(v0);
    SIMDVectorDouble temp1 = VectorDoubleSwizzle<2, 0, 1, 3>(v1);
    SIMDVectorDouble result = VectorDoubleMultiply(temp0, temp1);
    temp0 = VectorDoubleSwizzle<2, 0, 1, 3>(v0);
    temp1 = VectorDoubleSwizzle<1, 2, 0, 3>(v1);
    return VectorDoubleAndInt(VectorDoubleNegativeMultiplySubtract(temp0, temp1, result), s_DoubleSIMDMask3);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3LengthSq(SIMDVectorDouble value)noexcept{
    return VectorDouble3Dot(value, value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3ReciprocalLength(SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocalSqrt(VectorDouble3LengthSq(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3ReciprocalLengthEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocalSqrtEst(VectorDouble3LengthSq(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3Length(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrt(VectorDouble3LengthSq(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3LengthEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrtEst(VectorDouble3LengthSq(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3Normalize(SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble lengthSq = VectorDouble3LengthSq(value);
    const SIMDVectorDouble length = VectorDoubleSqrt(lengthSq);
    SIMDVectorDouble result = VectorDoubleDivide(value, length);
    result = VectorDoubleAndInt(result, VectorDoubleNotEqual(length, VectorDoubleZero()));
    return VectorDoubleAndInt(VectorDoubleSelect(s_DoubleSIMDQNaN, result, VectorDoubleNotEqualInt(lengthSq, s_DoubleSIMDInfinity)), s_DoubleSIMDMask3);
}

[[nodiscard]] NWB_INLINE Expected<SIMDVectorDouble> NWB_SIMD_CALL VectorDouble3TryNormalize(SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble lengthSquared = VectorDouble3LengthSq(value);
    if(!VectorDoubleIsFinite(lengthSquared, VectorDoubleComponentMask::s_DoubleXYZW) || !VectorDouble3Greater(lengthSquared, VectorDoubleZero()))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble normalized = VectorDouble3Normalize(value);
    NWB_ASSERT(VectorDouble3IsFinite(normalized));
    return normalized;
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3NormalizeOr(SIMDVectorDouble value, SIMDVectorDouble fallback, const f64 minLengthSquared)noexcept{
    const SIMDVectorDouble lengthSquared = VectorDouble3LengthSq(value);
    return SIMDVectorDoubleDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, s_DoubleSIMDMask3);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3NormalizeEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleAndInt(VectorDoubleMultiply(value, VectorDouble3ReciprocalLengthEst(value)), s_DoubleSIMDMask3);
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3ClampLengthV(SIMDVectorDouble value, SIMDVectorDouble lengthMin, SIMDVectorDouble lengthMax)noexcept{
    NWB_ASSERT((VectorDoubleGetY(lengthMin) == VectorDoubleGetX(lengthMin)) && (VectorDoubleGetZ(lengthMin) == VectorDoubleGetX(lengthMin)));
    NWB_ASSERT((VectorDoubleGetY(lengthMax) == VectorDoubleGetX(lengthMax)) && (VectorDoubleGetZ(lengthMax) == VectorDoubleGetX(lengthMax)));
    NWB_ASSERT(VectorDouble3GreaterOrEqual(lengthMin, s_DoubleSIMDZero));
    NWB_ASSERT(VectorDouble3GreaterOrEqual(lengthMax, s_DoubleSIMDZero));
    NWB_ASSERT(VectorDouble3GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVectorDouble lengthSq = VectorDouble3LengthSq(value);
    return VectorDoubleAndInt(SIMDVectorDoubleDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax), s_DoubleSIMDMask3);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3ClampLength(SIMDVectorDouble value, f64 lengthMin, f64 lengthMax)noexcept{
    return VectorDouble3ClampLengthV(value, VectorDoubleReplicate(lengthMin), VectorDoubleReplicate(lengthMax));
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3Reflect(SIMDVectorDouble incident, SIMDVectorDouble normal)noexcept{
    SIMDVectorDouble result = VectorDouble3Dot(incident, normal);
    result = VectorDoubleAdd(result, result);
    return VectorDoubleNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3RefractV(SIMDVectorDouble incident, SIMDVectorDouble normal, SIMDVectorDouble refractionIndex)noexcept{
    return SIMDVectorDoubleDetail::RefractV(incident, normal, refractionIndex, VectorDouble3Dot(incident, normal));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3Refract(SIMDVectorDouble incident, SIMDVectorDouble normal, f64 refractionIndex)noexcept{ return VectorDouble3RefractV(incident, normal, VectorDoubleReplicate(refractionIndex)); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3Rotate(SIMDVectorDouble value, SIMDVectorDouble rotationQuaternion)noexcept{
    const SIMDVectorDouble q = VectorDoubleAndInt(rotationQuaternion, s_DoubleSIMDMask3);
    const SIMDVectorDouble t = VectorDoubleScale(VectorDouble3Cross(q, value), 2.0);
    return VectorDoubleAdd(VectorDoubleAdd(value, VectorDoubleMultiply(t, VectorDoubleSplatW(rotationQuaternion))), VectorDouble3Cross(q, t));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3InverseRotate(SIMDVectorDouble value, SIMDVectorDouble rotationQuaternion)noexcept{
    return VectorDouble3Rotate(value, VectorDoubleXorInt(rotationQuaternion, s_DoubleSIMDNegate3));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3Orthogonal(SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble zero = VectorDoubleZero();
    const SIMDVectorDouble z = VectorDoubleSplatZ(value);
    const SIMDVectorDouble yzyy = VectorDoubleSwizzle<1, 2, 1, 1>(value);
    const SIMDVectorDouble negativeValue = VectorDoubleSubtract(zero, value);
    const SIMDVectorDouble zIsNegative = VectorDoubleLess(z, zero);
    const SIMDVectorDouble yzyyIsNegative = VectorDoubleLess(yzyy, zero);
    const SIMDVectorDouble s = VectorDoubleAdd(yzyy, z);
    const SIMDVectorDouble d = VectorDoubleSubtract(yzyy, z);
    const SIMDVectorDouble select = VectorDoubleEqualInt(zIsNegative, yzyyIsNegative);
    const SIMDVectorDouble r0 = VectorDoublePermute<4, 0, 0, 0>(negativeValue, s);
    const SIMDVectorDouble r1 = VectorDoublePermute<4, 0, 0, 0>(value, d);
    return VectorDoubleSelect(r1, r0, select);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3AngleBetweenNormals(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{ return VectorDoubleACos(VectorDoubleClamp(VectorDouble3Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne)); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3AngleBetweenNormalsEst(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{ return VectorDoubleACosEst(VectorDoubleClamp(VectorDouble3Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne)); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3AngleBetweenVectors(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDouble reciprocalLength0 = VectorDouble3ReciprocalLength(v0);
    const SIMDVectorDouble reciprocalLength1 = VectorDouble3ReciprocalLength(v1);
    SIMDVectorDouble cosAngle = VectorDoubleMultiply(VectorDouble3Dot(v0, v1), VectorDoubleMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorDoubleClamp(cosAngle, s_DoubleSIMDNegativeOne, s_DoubleSIMDOne);
    return VectorDoubleACos(cosAngle);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3LinePointDistance(SIMDVectorDouble linePoint0, SIMDVectorDouble linePoint1, SIMDVectorDouble point)noexcept{
    const SIMDVectorDouble line = VectorDoubleSubtract(linePoint1, linePoint0);
    const SIMDVectorDouble pointVector = VectorDoubleSubtract(point, linePoint0);
    const SIMDVectorDouble projectionScale = VectorDoubleDivide(VectorDouble3Dot(pointVector, line), VectorDouble3LengthSq(line));
    return VectorDouble3Length(VectorDoubleSubtract(pointVector, VectorDoubleMultiply(line, projectionScale)));
}

NWB_INLINE void NWB_SIMD_CALL VectorDouble3ComponentsFromNormal(SIMDVectorDouble& outParallel, SIMDVectorDouble& outPerpendicular, SIMDVectorDouble value, SIMDVectorDouble normal)noexcept{
    const SIMDVectorDouble scale = VectorDouble3Dot(value, normal);
    outParallel = VectorDoubleMultiply(normal, scale);
    outPerpendicular = VectorDoubleSubtract(value, outParallel);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3Transform(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3TransformCoord(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3TransformNormal(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

