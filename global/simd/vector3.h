// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "result.h"
#include "vector2.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVector3Detail{

inline constexpr f32 s_RotateCrossScale = 2.0f;
inline constexpr f64 s_DoubleRotateCrossScale = 2.0;

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL Vector3Equal(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorEqual(v0, v1)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL VectorDouble3Equal(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::AllComparisonMask<VectorDoubleComponentMask::s_DoubleXYZ>(VectorDoubleEqual(v0, v1)); }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL VectorDouble3EqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::AllComparisonMask<VectorDoubleComponentMask::s_DoubleXYZ>(VectorDoubleEqualInt(v0, v1)); }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL Vector3Greater(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorGreater(v0, v1)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL VectorDouble3Greater(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::AllComparisonMask<VectorDoubleComponentMask::s_DoubleXYZ>(VectorDoubleGreater(v0, v1)); }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL Vector3GreaterOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorGreaterOrEqual(v0, v1)) & VectorComponentMask::s_XYZ) == VectorComponentMask::s_XYZ; }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL VectorDouble3GreaterOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::AllComparisonMask<VectorDoubleComponentMask::s_DoubleXYZ>(VectorDoubleGreaterOrEqual(v0, v1)); }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL Vector3LessOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return Vector3GreaterOrEqual(v1, v0); }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL VectorDouble3LessOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDouble3GreaterOrEqual(v1, v0); }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL Vector3IsNaN(SIMDVectorSingle value)noexcept{ return (VectorMoveMask(VectorIsNaN(value)) & VectorComponentMask::s_XYZ) != 0; }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL VectorDouble3IsNaN(SIMDVectorDouble value)noexcept{ return (VectorDoubleMoveMask(VectorDoubleIsNaN(value)) & VectorDoubleComponentMask::s_DoubleXYZ) != 0; }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL Vector3IsInfinite(SIMDVectorSingle value)noexcept{ return (VectorMoveMask(VectorIsInfinite(value)) & VectorComponentMask::s_XYZ) != 0; }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL VectorDouble3IsInfinite(SIMDVectorDouble value)noexcept{ return (VectorDoubleMoveMask(VectorDoubleIsInfinite(value)) & VectorDoubleComponentMask::s_DoubleXYZ) != 0; }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL Vector3IsFinite(SIMDVectorSingle value)noexcept{ return !Vector3IsNaN(value) && !Vector3IsInfinite(value); }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL VectorDouble3IsFinite(SIMDVectorDouble value)noexcept{ return !VectorDouble3IsNaN(value) && !VectorDouble3IsInfinite(value); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3Dot(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(FOUNDATION_HAS_SCALAR)
    return VectorReplicate(VectorGetX(v0) * VectorGetX(v1) + VectorGetY(v0) * VectorGetY(v1) + VectorGetZ(v0) * VectorGetZ(v1));
#elif defined(FOUNDATION_HAS_NEON)
    v0 = VectorAndInt(v0, s_SIMDMask3);
    v1 = VectorAndInt(v1, s_SIMDMask3);
    float32x4_t product = vmulq_f32(v0, v1);
    float32x2_t lo = vget_low_f32(product);
    float32x2_t hi = vget_high_f32(product);
    lo = vpadd_f32(lo, lo);
    hi = vdup_lane_f32(hi, 0);
    lo = vadd_f32(lo, hi);
    return vcombine_f32(lo, lo);
#elif defined(FOUNDATION_HAS_SSE4)
    return _mm_dp_ps(v0, v1, s_SIMDSse4Dot3BroadcastMask);
#elif defined(FOUNDATION_HAS_SSE2)
    v0 = _mm_and_ps(v0, s_SIMDMask3);
    v1 = _mm_and_ps(v1, s_SIMDMask3);
    const SIMDVectorSingle product = _mm_mul_ps(v0, v1);
    SIMDVectorSingle result = _mm_add_ss(product, _mm_shuffle_ps(product, product, _MM_SHUFFLE(1, 1, 1, 1)));
    result = _mm_add_ss(result, _mm_shuffle_ps(product, product, _MM_SHUFFLE(2, 2, 2, 2)));
    return _mm_shuffle_ps(result, result, _MM_SHUFFLE(0, 0, 0, 0));
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3Dot(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(FOUNDATION_HAS_AVX2)
#pragma STDC FENV_ACCESS ON
    // Keep the original packed multiply so simultaneous active-lane exceptions retain their trap priority.
    v0 = VectorDoubleAndInt(v0, s_DoubleSIMDMask3);
    v1 = VectorDoubleAndInt(v1, s_DoubleSIMDMask3);
    const SIMDVectorDouble product = v0 * v1;
    return SIMDVectorDoubleDetail::Reduce3(product);
#else
    v0 = VectorDoubleAndInt(v0, s_DoubleSIMDMask3);
    v1 = VectorDoubleAndInt(v1, s_DoubleSIMDMask3);
    const SIMDVectorDouble product = VectorDoubleMultiply(v0, v1);
    SIMDVectorDouble result = VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
    result = VectorDoubleAdd(result, VectorDoubleSplatZ(product));
    return result;
#endif
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3Cross(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    v0 = VectorAndInt(v0, s_SIMDMask3);
    v1 = VectorAndInt(v1, s_SIMDMask3);
    SIMDVectorSingle temp0 = VectorSwizzle<1, 2, 0, 3>(v0);
    SIMDVectorSingle temp1 = VectorSwizzle<2, 0, 1, 3>(v1);
    SIMDVectorSingle result = VectorMultiply(temp0, temp1);
    temp0 = VectorSwizzle<2, 0, 1, 3>(v0);
    temp1 = VectorSwizzle<1, 2, 0, 3>(v1);
    return VectorAndInt(VectorNegativeMultiplySubtract(temp0, temp1, result), s_SIMDMask3);
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3Cross(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    v0 = VectorDoubleAndInt(v0, s_DoubleSIMDMask3);
    v1 = VectorDoubleAndInt(v1, s_DoubleSIMDMask3);
    SIMDVectorDouble temp0 = VectorDoubleSwizzle<1, 2, 0, 3>(v0);
    SIMDVectorDouble temp1 = VectorDoubleSwizzle<2, 0, 1, 3>(v1);
    SIMDVectorDouble result = VectorDoubleMultiply(temp0, temp1);
    temp0 = VectorDoubleSwizzle<2, 0, 1, 3>(v0);
    temp1 = VectorDoubleSwizzle<1, 2, 0, 3>(v1);
    return VectorDoubleAndInt(VectorDoubleNegativeMultiplySubtract(temp0, temp1, result), s_DoubleSIMDMask3);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3LengthSq(SIMDVectorSingle value)noexcept{
#if defined(FOUNDATION_HAS_SSE4)
    return _mm_dp_ps(value, value, s_SIMDSse4Dot3BroadcastMask);
#else
    return Vector3Dot(value, value);
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3LengthSq(SIMDVectorDouble value)noexcept{
    return VectorDouble3Dot(value, value);
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3ReciprocalLength(SIMDVectorDouble value)noexcept{
#if defined(FOUNDATION_HAS_SSE2)
    return SIMDVectorDoubleDetail::ReciprocalSqrtReplicated(VectorDouble3LengthSq(value));
#else
    return VectorDoubleReciprocalSqrt(VectorDouble3LengthSq(value));
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3ReciprocalLengthEst(SIMDVectorDouble value)noexcept{
#if defined(FOUNDATION_HAS_SSE2)
    return SIMDVectorDoubleDetail::ReciprocalSqrtReplicated(VectorDouble3LengthSq(value));
#else
    return VectorDoubleReciprocalSqrtEst(VectorDouble3LengthSq(value));
#endif
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3Length(SIMDVectorSingle value)noexcept{
#if defined(FOUNDATION_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, s_SIMDSse4Dot3BroadcastMask));
#else
    return VectorSqrt(Vector3LengthSq(value));
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3Length(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrt(VectorDouble3LengthSq(value));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3Normalize(SIMDVectorSingle value)noexcept{
#if defined(FOUNDATION_HAS_SSE4)
    SIMDVectorSingle lengthSq = _mm_dp_ps(value, value, s_SIMDSse4Dot3BroadcastMask);
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

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3Normalize(SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble lengthSq = VectorDouble3LengthSq(value);
    const SIMDVectorDouble length = VectorDoubleSqrt(lengthSq);
    SIMDVectorDouble result = VectorDoubleDivide(value, length);
    result = VectorDoubleAndInt(result, VectorDoubleNotEqual(length, VectorDoubleZero()));
    return VectorDoubleAndInt(VectorDoubleSelect(s_DoubleSIMDQNaN, result, VectorDoubleNotEqualInt(lengthSq, s_DoubleSIMDInfinity)), s_DoubleSIMDMask3);
}

[[nodiscard]] FOUNDATION_INLINE Expected<SIMDVectorSingleResult> FOUNDATION_SIMD_CALL Vector3TryNormalize(SIMDVectorSingle value)noexcept{
    const SIMDVectorSingle lengthSquared = Vector3LengthSq(value);
    if(!VectorIsFinite(lengthSquared, VectorComponentMask::s_XYZW) || !Vector3Greater(lengthSquared, VectorZero()))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle normalized = Vector3Normalize(value);
    FOUNDATION_ASSERT(Vector3IsFinite(normalized));
    return SIMDVectorSingleResult{ normalized };
}

[[nodiscard]] FOUNDATION_INLINE Expected<SIMDVectorDoubleResult> FOUNDATION_SIMD_CALL VectorDouble3TryNormalize(SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble lengthSquared = VectorDouble3LengthSq(value);
    if(!VectorDoubleIsFinite(lengthSquared, VectorDoubleComponentMask::s_DoubleXYZW) || !VectorDouble3Greater(lengthSquared, VectorDoubleZero()))
        return MakeUnexpected(Failure{});

    const SIMDVectorDouble normalized = VectorDouble3Normalize(value);
    FOUNDATION_ASSERT(VectorDouble3IsFinite(normalized));
    return SIMDVectorDoubleResult{ normalized };
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3NormalizeOr(SIMDVectorSingle value, SIMDVectorSingle fallback, const f32 minLengthSquared)noexcept{
    const SIMDVectorSingle lengthSquared = Vector3LengthSq(value);
    return SIMDVectorDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, s_SIMDMask3);
}

[[nodiscard]] FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3NormalizeOr(SIMDVectorDouble value, SIMDVectorDouble fallback, const f64 minLengthSquared)noexcept{
    const SIMDVectorDouble lengthSquared = VectorDouble3LengthSq(value);
    return SIMDVectorDoubleDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, s_DoubleSIMDMask3);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3ClampLengthV(SIMDVectorDouble value, SIMDVectorDouble lengthMin, SIMDVectorDouble lengthMax)noexcept{
    FOUNDATION_ASSERT((VectorDoubleGetY(lengthMin) == VectorDoubleGetX(lengthMin)) && (VectorDoubleGetZ(lengthMin) == VectorDoubleGetX(lengthMin)));
    FOUNDATION_ASSERT((VectorDoubleGetY(lengthMax) == VectorDoubleGetX(lengthMax)) && (VectorDoubleGetZ(lengthMax) == VectorDoubleGetX(lengthMax)));
    FOUNDATION_ASSERT(VectorDouble3GreaterOrEqual(lengthMin, s_DoubleSIMDZero));
    FOUNDATION_ASSERT(VectorDouble3GreaterOrEqual(lengthMax, s_DoubleSIMDZero));
    FOUNDATION_ASSERT(VectorDouble3GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVectorDouble lengthSq = VectorDouble3LengthSq(value);
    return VectorDoubleAndInt(SIMDVectorDoubleDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax), s_DoubleSIMDMask3);
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3ClampLength(SIMDVectorDouble value, f64 lengthMin, f64 lengthMax)noexcept{
    return VectorDouble3ClampLengthV(value, VectorDoubleReplicate(lengthMin), VectorDoubleReplicate(lengthMax));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3RefractV(SIMDVectorSingle incident, SIMDVectorSingle normal, SIMDVectorSingle refractionIndex)noexcept{
    return SIMDVectorDetail::RefractV(incident, normal, refractionIndex, Vector3Dot(incident, normal));
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3RefractV(SIMDVectorDouble incident, SIMDVectorDouble normal, SIMDVectorDouble refractionIndex)noexcept{
    return SIMDVectorDoubleDetail::RefractV(incident, normal, refractionIndex, VectorDouble3Dot(incident, normal));
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3Refract(SIMDVectorSingle incident, SIMDVectorSingle normal, f32 refractionIndex)noexcept{ return Vector3RefractV(incident, normal, VectorReplicate(refractionIndex)); }

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3Refract(SIMDVectorDouble incident, SIMDVectorDouble normal, f64 refractionIndex)noexcept{ return VectorDouble3RefractV(incident, normal, VectorDoubleReplicate(refractionIndex)); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3Rotate(SIMDVectorSingle value, SIMDVectorSingle rotationQuaternion)noexcept{
    const SIMDVectorSingle q = VectorAndInt(rotationQuaternion, s_SIMDMask3);
    const SIMDVectorSingle t = VectorScale(Vector3Cross(q, value), SIMDVector3Detail::s_RotateCrossScale);
    return VectorAdd(VectorAdd(value, VectorMultiply(t, VectorSplatW(rotationQuaternion))), Vector3Cross(q, t));
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3Rotate(SIMDVectorDouble value, SIMDVectorDouble rotationQuaternion)noexcept{
    const SIMDVectorDouble q = VectorDoubleAndInt(rotationQuaternion, s_DoubleSIMDMask3);
    const SIMDVectorDouble t = VectorDoubleScale(VectorDouble3Cross(q, value), SIMDVector3Detail::s_DoubleRotateCrossScale);
    return VectorDoubleAdd(VectorDoubleAdd(value, VectorDoubleMultiply(t, VectorDoubleSplatW(rotationQuaternion))), VectorDouble3Cross(q, t));
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3InverseRotate(SIMDVectorSingle value, SIMDVectorSingle rotationQuaternion)noexcept{
    return Vector3Rotate(value, VectorXorInt(rotationQuaternion, s_SIMDNegate3));
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3InverseRotate(SIMDVectorDouble value, SIMDVectorDouble rotationQuaternion)noexcept{
    return VectorDouble3Rotate(value, VectorDoubleXorInt(rotationQuaternion, s_DoubleSIMDNegate3));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3AngleBetweenNormals(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{
    return SIMDVectorDoubleDetail::ACosReplicated(VectorDoubleClamp(VectorDouble3Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne));
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3AngleBetweenNormalsEst(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{
    return SIMDVectorDoubleDetail::ACosReplicated(VectorDoubleClamp(VectorDouble3Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne));
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3AngleBetweenVectors(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDouble reciprocalLength0 = VectorDouble3ReciprocalLength(v0);
    const SIMDVectorDouble reciprocalLength1 = VectorDouble3ReciprocalLength(v1);
    SIMDVectorDouble cosAngle = VectorDoubleMultiply(VectorDouble3Dot(v0, v1), VectorDoubleMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorDoubleClamp(cosAngle, s_DoubleSIMDNegativeOne, s_DoubleSIMDOne);
    return SIMDVectorDoubleDetail::ACosReplicated(cosAngle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3Transform(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept;

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3Transform(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3TransformCoord(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3TransformNormal(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept;

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3TransformNormal(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

