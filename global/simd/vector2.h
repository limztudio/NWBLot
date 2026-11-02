// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_interp.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Packed binary32 lane multipliers { +1, -1 } for the NEON 2D cross product.
inline constexpr u64 s_Vector2CrossLaneSignBits = 0xBF8000003F800000ull;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL NormalizeOrV(SIMDVectorSingle value, SIMDVectorSingle fallback, SIMDVectorSingle lengthSquared, const f32 minLengthSquared, SIMDVectorSingle componentMask)noexcept{
    const SIMDVectorSingle validLength = VectorAndInt(VectorGreater(lengthSquared, VectorMax(VectorZero(), VectorReplicate(minLengthSquared))), VectorEqualInt(VectorOrInt(VectorIsNaN(lengthSquared), VectorIsInfinite(lengthSquared)), VectorFalseInt()));
    SIMDVectorSingle normalized = VectorAndInt(VectorMultiply(value, VectorReciprocalSqrt(lengthSquared)), componentMask);
    fallback = VectorAndInt(fallback, componentMask);
    const SIMDVectorSingle validNormalized = VectorEqualInt(VectorOrInt(VectorIsNaN(normalized), VectorIsInfinite(normalized)), VectorFalseInt());
    return VectorSelect(fallback, normalized, VectorAndInt(validLength, validNormalized));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL RefractV(SIMDVectorSingle incident, SIMDVectorSingle normal, SIMDVectorSingle refractionIndex, SIMDVectorSingle dot)noexcept{
    SIMDVectorSingle r = VectorNegativeMultiplySubtract(dot, dot, s_SIMDOne);
    const SIMDVectorSingle refractionIndexSq = VectorMultiply(refractionIndex, refractionIndex);
    r = VectorNegativeMultiplySubtract(r, refractionIndexSq, s_SIMDOne);
    const SIMDVectorSingle totalInternalReflection = VectorLess(r, s_SIMDZero);
    r = VectorMultiplyAdd(refractionIndex, dot, VectorSqrt(VectorMax(r, s_SIMDZero)));
    SIMDVectorSingle result = VectorMultiply(refractionIndex, incident);
    result = VectorNegativeMultiplySubtract(r, normal, result);
    return VectorSelect(result, VectorZero(), totalInternalReflection);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL NormalizeOrV(SIMDVectorDouble value, SIMDVectorDouble fallback, SIMDVectorDouble lengthSquared, const f64 minLengthSquared, SIMDVectorDouble componentMask)noexcept{
    const SIMDVectorDouble validLength = VectorDoubleAndInt(
        VectorDoubleGreater(lengthSquared, VectorDoubleMax(VectorDoubleZero(), VectorDoubleReplicate(minLengthSquared))),
        VectorDoubleEqualInt(VectorDoubleOrInt(VectorDoubleIsNaN(lengthSquared), VectorDoubleIsInfinite(lengthSquared)), VectorDoubleFalseInt())
    );
    SIMDVectorDouble normalized = VectorDoubleAndInt(VectorDoubleMultiply(value, VectorDoubleReciprocalSqrt(lengthSquared)), componentMask);
    fallback = VectorDoubleAndInt(fallback, componentMask);
    const SIMDVectorDouble validNormalized = VectorDoubleEqualInt(VectorDoubleOrInt(VectorDoubleIsNaN(normalized), VectorDoubleIsInfinite(normalized)), VectorDoubleFalseInt());
    return VectorDoubleSelect(fallback, normalized, VectorDoubleAndInt(validLength, validNormalized));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL ClampLengthV(SIMDVectorDouble value, SIMDVectorDouble lengthSq, SIMDVectorDouble lengthMin, SIMDVectorDouble lengthMax)noexcept{
    const SIMDVectorDouble reciprocalLength = VectorDoubleReciprocalSqrt(lengthSq);
    const SIMDVectorDouble infiniteLength = VectorDoubleEqualInt(lengthSq, s_DoubleSIMDInfinity);
    const SIMDVectorDouble zeroLength = VectorDoubleEqual(lengthSq, s_DoubleSIMDZero);
    const SIMDVectorDouble select = VectorDoubleEqualInt(infiniteLength, zeroLength);
    SIMDVectorDouble normal = VectorDoubleMultiply(value, reciprocalLength);
    SIMDVectorDouble length = VectorDoubleMultiply(lengthSq, reciprocalLength);
    length = VectorDoubleSelect(lengthSq, length, select);
    normal = VectorDoubleSelect(lengthSq, normal, select);
    const SIMDVectorDouble controlMax = VectorDoubleGreater(length, lengthMax);
    const SIMDVectorDouble controlMin = VectorDoubleLess(length, lengthMin);
    SIMDVectorDouble clampedLength = VectorDoubleSelect(length, lengthMax, controlMax);
    clampedLength = VectorDoubleSelect(clampedLength, lengthMin, controlMin);
    SIMDVectorDouble result = VectorDoubleMultiply(normal, clampedLength);
    return VectorDoubleSelect(result, value, VectorDoubleEqualInt(controlMax, controlMin));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL RefractV(SIMDVectorDouble incident, SIMDVectorDouble normal, SIMDVectorDouble refractionIndex, SIMDVectorDouble dot)noexcept{
    SIMDVectorDouble r = VectorDoubleNegativeMultiplySubtract(dot, dot, s_DoubleSIMDOne);
    const SIMDVectorDouble refractionIndexSq = VectorDoubleMultiply(refractionIndex, refractionIndex);
    r = VectorDoubleNegativeMultiplySubtract(r, refractionIndexSq, s_DoubleSIMDOne);
    const SIMDVectorDouble totalInternalReflection = VectorDoubleLess(r, s_DoubleSIMDZero);
    r = VectorDoubleMultiplyAdd(refractionIndex, dot, VectorDoubleSqrt(VectorDoubleMax(r, s_DoubleSIMDZero)));
    SIMDVectorDouble result = VectorDoubleMultiply(refractionIndex, incident);
    result = VectorDoubleNegativeMultiplySubtract(r, normal, result);
    return VectorDoubleSelect(result, VectorDoubleZero(), totalInternalReflection);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE b8 NWB_SIMD_CALL Vector2GreaterOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorGreaterOrEqual(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }

NWB_INLINE b8 NWB_SIMD_CALL VectorDouble2GreaterOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(v0, v1)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY; }

NWB_INLINE b8 NWB_SIMD_CALL Vector2LessOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return Vector2GreaterOrEqual(v1, v0); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Dot(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(VectorGetX(v0) * VectorGetX(v1) + VectorGetY(v0) * VectorGetY(v1));
#elif defined(NWB_HAS_NEON)
    const float32x2_t product = vmul_f32(vget_low_f32(v0), vget_low_f32(v1));
    const float32x2_t result = vpadd_f32(product, product);
    return vcombine_f32(result, result);
#elif defined(NWB_HAS_SSE4)
    return _mm_dp_ps(v0, v1, s_SIMDSse4Dot2BroadcastMask);
#elif defined(NWB_HAS_SSE2)
    v0 = _mm_and_ps(v0, s_SIMDMaskXY);
    v1 = _mm_and_ps(v1, s_SIMDMaskXY);
    const SIMDVectorSingle product = _mm_mul_ps(v0, v1);
    SIMDVectorSingle result = _mm_add_ss(product, _mm_shuffle_ps(product, product, _MM_SHUFFLE(1, 1, 1, 1)));
    return _mm_shuffle_ps(result, result, _MM_SHUFFLE(0, 0, 0, 0));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Dot(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_AVX2)
    const __m128d product = _mm_mul_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1));
    const __m128d sum = _mm_add_sd(product, _mm_unpackhi_pd(product, product));
    const __m128d replicated = _mm_unpacklo_pd(sum, sum);
    return SIMDVectorDoubleDetail::FromPairs(replicated, replicated);
#else
    v0 = VectorDoubleAndInt(v0, s_DoubleSIMDMaskXY);
    v1 = VectorDoubleAndInt(v1, s_DoubleSIMDMaskXY);
    const SIMDVectorDouble product = VectorDoubleMultiply(v0, v1);
    SIMDVectorDouble result = VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
    return result;
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Cross(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(v0.f[0] * v1.f[1] - v0.f[1] * v1.f[0]);
#elif defined(NWB_HAS_NEON)
    const float32x2_t negate = vcreate_f32(SIMDVectorDetail::s_Vector2CrossLaneSignBits);
    float32x2_t result = vmul_f32(vget_low_f32(v0), vrev64_f32(vget_low_f32(v1)));
    result = vmul_f32(result, negate);
    result = vpadd_f32(result, result);
    return vcombine_f32(result, result);
#else
    v0 = VectorAndInt(v0, s_SIMDMaskXY);
    v1 = VectorAndInt(v1, s_SIMDMaskXY);
    SIMDVectorSingle result = VectorSwizzle<1, 0, 2, 3>(v1);
    result = VectorMultiply(result, v0);
    const SIMDVectorSingle temp = VectorSplatY(result);
    result = _mm_sub_ss(result, temp);
    return VectorSplatX(result);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Cross(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    v0 = VectorDoubleAndInt(v0, s_DoubleSIMDMaskXY);
    v1 = VectorDoubleAndInt(v1, s_DoubleSIMDMaskXY);
    const SIMDVectorDouble product = VectorDoubleMultiply(v0, VectorDoubleSwizzle<1, 0, 3, 2>(v1));
    return VectorDoubleSubtract(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2LengthSq(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_dp_ps(value, value, s_SIMDSse4Dot2BroadcastMask);
#else
    return Vector2Dot(value, value);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2LengthSq(SIMDVectorDouble value)noexcept{
    return VectorDouble2Dot(value, value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2ReciprocalLength(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::ReciprocalSqrtReplicated(VectorDouble2LengthSq(value));
#else
    return VectorDoubleReciprocalSqrt(VectorDouble2LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2ReciprocalLengthEst(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::ReciprocalSqrtReplicated(VectorDouble2LengthSq(value));
#else
    return VectorDoubleReciprocalSqrtEst(VectorDouble2LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Length(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, s_SIMDSse4Dot2BroadcastMask));
#else
    return VectorSqrt(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Length(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrt(VectorDouble2LengthSq(value));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Normalize(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    SIMDVectorSingle lengthSq = _mm_dp_ps(value, value, s_SIMDSse4Dot2BroadcastMask);
    const SIMDVectorSingle length = _mm_sqrt_ps(lengthSq);
    const SIMDVectorSingle zeroMask = _mm_cmpneq_ps(_mm_setzero_ps(), length);
    lengthSq = _mm_cmpneq_ps(lengthSq, s_SIMDInfinity);
    SIMDVectorSingle result = _mm_div_ps(value, length);
    result = _mm_and_ps(result, zeroMask);
    return _mm_and_ps(_mm_or_ps(_mm_andnot_ps(lengthSq, s_SIMDQNaN), _mm_and_ps(result, lengthSq)), s_SIMDMaskXY);
#else
    const SIMDVectorSingle lengthSq = Vector2LengthSq(value);
    const SIMDVectorSingle length = VectorSqrt(lengthSq);
    SIMDVectorSingle result = VectorDivide(value, length);
    result = VectorAndInt(result, VectorNotEqual(length, VectorZero()));
    return VectorAndInt(VectorSelect(s_SIMDQNaN, result, VectorNotEqualInt(lengthSq, s_SIMDInfinity)), s_SIMDMaskXY);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Normalize(SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble lengthSq = VectorDouble2LengthSq(value);
    const SIMDVectorDouble length = VectorDoubleSqrt(lengthSq);
    SIMDVectorDouble result = VectorDoubleDivide(value, length);
    result = VectorDoubleAndInt(result, VectorDoubleNotEqual(length, VectorDoubleZero()));
    return VectorDoubleAndInt(VectorDoubleSelect(s_DoubleSIMDQNaN, result, VectorDoubleNotEqualInt(lengthSq, s_DoubleSIMDInfinity)), s_DoubleSIMDMaskXY);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2NormalizeOr(SIMDVectorDouble value, SIMDVectorDouble fallback, const f64 minLengthSquared)noexcept{
    const SIMDVectorDouble lengthSquared = VectorDouble2LengthSq(value);
    return SIMDVectorDoubleDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, s_DoubleSIMDMaskXY);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2ClampLengthV(SIMDVectorDouble value, SIMDVectorDouble lengthMin, SIMDVectorDouble lengthMax)noexcept{
    NWB_ASSERT(VectorDoubleGetY(lengthMin) == VectorDoubleGetX(lengthMin));
    NWB_ASSERT(VectorDoubleGetY(lengthMax) == VectorDoubleGetX(lengthMax));
    NWB_ASSERT(VectorDouble2GreaterOrEqual(lengthMin, s_DoubleSIMDZero));
    NWB_ASSERT(VectorDouble2GreaterOrEqual(lengthMax, s_DoubleSIMDZero));
    NWB_ASSERT(VectorDouble2GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVectorDouble lengthSq = VectorDouble2LengthSq(value);
    return VectorDoubleAndInt(SIMDVectorDoubleDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax), s_DoubleSIMDMaskXY);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2ClampLength(SIMDVectorDouble value, f64 lengthMin, f64 lengthMax)noexcept{
    return VectorDouble2ClampLengthV(value, VectorDoubleReplicate(lengthMin), VectorDoubleReplicate(lengthMax));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2RefractV(SIMDVectorSingle incident, SIMDVectorSingle normal, SIMDVectorSingle refractionIndex)noexcept{
    return SIMDVectorDetail::RefractV(incident, normal, refractionIndex, Vector2Dot(incident, normal));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Refract(SIMDVectorSingle incident, SIMDVectorSingle normal, f32 refractionIndex)noexcept{
    return Vector2RefractV(incident, normal, VectorReplicate(refractionIndex));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2AngleBetweenNormals(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{
    return SIMDVectorDoubleDetail::ACosReplicated(VectorDoubleClamp(VectorDouble2Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2AngleBetweenNormalsEst(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{
    return SIMDVectorDoubleDetail::ACosReplicated(VectorDoubleClamp(VectorDouble2Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2AngleBetweenVectors(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDouble reciprocalLength0 = VectorDouble2ReciprocalLength(v0);
    const SIMDVectorDouble reciprocalLength1 = VectorDouble2ReciprocalLength(v1);
    SIMDVectorDouble cosAngle = VectorDoubleMultiply(VectorDouble2Dot(v0, v1), VectorDoubleMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorDoubleClamp(cosAngle, s_DoubleSIMDNegativeOne, s_DoubleSIMDOne);
    return SIMDVectorDoubleDetail::ACosReplicated(cosAngle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Transform(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept;

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Transform(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

