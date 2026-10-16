// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_interp.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL NormalizeOrV(SIMDVectorSingle value, SIMDVectorSingle fallback, SIMDVectorSingle lengthSquared, const f32 minLengthSquared, SIMDVectorSingle componentMask)noexcept{
    const SIMDVectorSingle validLength = VectorAndInt(VectorGreater(lengthSquared, VectorMax(VectorZero(), VectorReplicate(minLengthSquared))), VectorEqualInt(VectorOrInt(VectorIsNaN(lengthSquared), VectorIsInfinite(lengthSquared)), VectorFalseInt()));
    SIMDVectorSingle normalized = VectorAndInt(VectorMultiply(value, VectorReciprocalSqrt(lengthSquared)), componentMask);
    fallback = VectorAndInt(fallback, componentMask);
    const SIMDVectorSingle validNormalized = VectorEqualInt(VectorOrInt(VectorIsNaN(normalized), VectorIsInfinite(normalized)), VectorFalseInt());
    return VectorSelect(fallback, normalized, VectorAndInt(validLength, validNormalized));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL ClampLengthV(SIMDVectorSingle value, SIMDVectorSingle lengthSq, SIMDVectorSingle lengthMin, SIMDVectorSingle lengthMax)noexcept{
    const SIMDVectorSingle reciprocalLength = VectorReciprocalSqrt(lengthSq);
    const SIMDVectorSingle infiniteLength = VectorEqualInt(lengthSq, s_SIMDInfinity);
    const SIMDVectorSingle zeroLength = VectorEqual(lengthSq, s_SIMDZero);
    const SIMDVectorSingle select = VectorEqualInt(infiniteLength, zeroLength);
    SIMDVectorSingle normal = VectorMultiply(value, reciprocalLength);
    SIMDVectorSingle length = VectorMultiply(lengthSq, reciprocalLength);
    length = VectorSelect(lengthSq, length, select);
    normal = VectorSelect(lengthSq, normal, select);
    const SIMDVectorSingle controlMax = VectorGreater(length, lengthMax);
    const SIMDVectorSingle controlMin = VectorLess(length, lengthMin);
    SIMDVectorSingle clampedLength = VectorSelect(length, lengthMax, controlMax);
    clampedLength = VectorSelect(clampedLength, lengthMin, controlMin);
    SIMDVectorSingle result = VectorMultiply(normal, clampedLength);
    return VectorSelect(result, value, VectorEqualInt(controlMax, controlMin));
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


NWB_INLINE bool NWB_SIMD_CALL Vector2Equal(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorEqual(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble2Equal(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleEqual(v0, v1)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY; }

NWB_INLINE bool NWB_SIMD_CALL Vector2EqualInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorEqualInt(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble2EqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleEqualInt(v0, v1)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY; }

NWB_INLINE bool NWB_SIMD_CALL Vector2NearEqual(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle epsilon)noexcept{ return (VectorMoveMask(VectorNearEqual(v0, v1, epsilon)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble2NearEqual(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble epsilon)noexcept{
    return (VectorDoubleMoveMask(VectorDoubleNearEqual(v0, v1, epsilon)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY;
}

NWB_INLINE bool NWB_SIMD_CALL Vector2NotEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return !Vector2Equal(v0, v1); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble2NotEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return !VectorDouble2Equal(v0, v1); }

NWB_INLINE bool NWB_SIMD_CALL Vector2NotEqualInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return !Vector2EqualInt(v0, v1); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble2NotEqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return !VectorDouble2EqualInt(v0, v1); }

NWB_INLINE bool NWB_SIMD_CALL Vector2Greater(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorGreater(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble2Greater(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleGreater(v0, v1)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY; }

NWB_INLINE bool NWB_SIMD_CALL Vector2GreaterOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorGreaterOrEqual(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble2GreaterOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(v0, v1)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY; }

NWB_INLINE bool NWB_SIMD_CALL Vector2Less(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return Vector2Greater(v1, v0); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble2Less(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDouble2Greater(v1, v0); }

NWB_INLINE bool NWB_SIMD_CALL Vector2LessOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return Vector2GreaterOrEqual(v1, v0); }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble2LessOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDouble2GreaterOrEqual(v1, v0); }

NWB_INLINE bool NWB_SIMD_CALL Vector2InBounds(SIMDVectorSingle value, SIMDVectorSingle bounds)noexcept{ return (VectorMoveMask(VectorInBounds(value, bounds)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble2InBounds(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{ return (VectorDoubleMoveMask(VectorDoubleInBounds(value, bounds)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY; }

NWB_INLINE bool NWB_SIMD_CALL Vector2IsNaN(SIMDVectorSingle value)noexcept{ return (VectorMoveMask(VectorIsNaN(value)) & VectorComponentMask::s_XY) != 0; }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble2IsNaN(SIMDVectorDouble value)noexcept{ return (VectorDoubleMoveMask(VectorDoubleIsNaN(value)) & VectorDoubleComponentMask::s_DoubleXY) != 0; }

NWB_INLINE bool NWB_SIMD_CALL Vector2IsInfinite(SIMDVectorSingle value)noexcept{ return (VectorMoveMask(VectorIsInfinite(value)) & VectorComponentMask::s_XY) != 0; }

NWB_INLINE bool NWB_SIMD_CALL VectorDouble2IsInfinite(SIMDVectorDouble value)noexcept{ return (VectorDoubleMoveMask(VectorDoubleIsInfinite(value)) & VectorDoubleComponentMask::s_DoubleXY) != 0; }

NWB_INLINE u32 NWB_SIMD_CALL Vector2EqualR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return VectorScalarDetail::ComparisonMaskR(VectorMoveMask(VectorEqual(v0, v1)), VectorComponentMask::s_XY); }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble2EqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorScalarDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleEqual(v0, v1)), VectorDoubleComponentMask::s_DoubleXY); }

NWB_INLINE u32 NWB_SIMD_CALL Vector2EqualIntR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return VectorScalarDetail::ComparisonMaskR(VectorMoveMask(VectorEqualInt(v0, v1)), VectorComponentMask::s_XY); }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble2EqualIntR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorScalarDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleEqualInt(v0, v1)), VectorDoubleComponentMask::s_DoubleXY); }

NWB_INLINE u32 NWB_SIMD_CALL Vector2GreaterR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return VectorScalarDetail::ComparisonMaskR(VectorMoveMask(VectorGreater(v0, v1)), VectorComponentMask::s_XY); }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble2GreaterR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorScalarDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleGreater(v0, v1)), VectorDoubleComponentMask::s_DoubleXY); }

NWB_INLINE u32 NWB_SIMD_CALL Vector2GreaterOrEqualR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return VectorScalarDetail::ComparisonMaskR(VectorMoveMask(VectorGreaterOrEqual(v0, v1)), VectorComponentMask::s_XY); }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble2GreaterOrEqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorScalarDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(v0, v1)), VectorDoubleComponentMask::s_DoubleXY); }

NWB_INLINE u32 NWB_SIMD_CALL Vector2InBoundsR(SIMDVectorSingle value, SIMDVectorSingle bounds)noexcept{ return VectorScalarDetail::BoundsMaskR(VectorMoveMask(VectorInBounds(value, bounds)), VectorComponentMask::s_XY); }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble2InBoundsR(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{ return VectorScalarDetail::BoundsMaskR(VectorDoubleMoveMask(VectorDoubleInBounds(value, bounds)), VectorDoubleComponentMask::s_DoubleXY); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Dot(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(VectorGetX(v0) * VectorGetX(v1) + VectorGetY(v0) * VectorGetY(v1));
#elif defined(NWB_HAS_NEON)
    const float32x2_t product = vmul_f32(vget_low_f32(v0), vget_low_f32(v1));
    const float32x2_t result = vpadd_f32(product, product);
    return vcombine_f32(result, result);
#elif defined(NWB_HAS_SSE4)
    return _mm_dp_ps(v0, v1, 0x3F);
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
    const float32x2_t negate = vcreate_f32(0xBF8000003F800000ull);
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
    return _mm_dp_ps(value, value, 0x3F);
#else
    return Vector2Dot(value, value);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2LengthSq(SIMDVectorDouble value)noexcept{
    return VectorDouble2Dot(value, value);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2ReciprocalLength(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVectorSingle lengthSq = _mm_dp_ps(value, value, 0x3F);
    const SIMDVectorSingle length = _mm_sqrt_ps(lengthSq);
    return _mm_div_ps(s_SIMDOne, length);
#else
    return VectorReciprocalSqrt(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2ReciprocalLength(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::ReciprocalSqrtReplicated(VectorDouble2LengthSq(value));
#else
    return VectorDoubleReciprocalSqrt(VectorDouble2LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2ReciprocalLengthEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_rsqrt_ps(_mm_dp_ps(value, value, 0x3F));
#else
    return VectorReciprocalSqrtEst(Vector2LengthSq(value));
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
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x3F));
#else
    return VectorSqrt(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Length(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrt(VectorDouble2LengthSq(value));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2LengthEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x3F));
#else
    return VectorSqrtEst(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2LengthEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrtEst(VectorDouble2LengthSq(value));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Normalize(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    SIMDVectorSingle lengthSq = _mm_dp_ps(value, value, 0x3F);
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

[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2NormalizeOr(SIMDVectorSingle value, SIMDVectorSingle fallback, const f32 minLengthSquared)noexcept{
    const SIMDVectorSingle lengthSquared = Vector2LengthSq(value);
    return SIMDVectorDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, s_SIMDMaskXY);
}

[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2NormalizeOr(SIMDVectorDouble value, SIMDVectorDouble fallback, const f64 minLengthSquared)noexcept{
    const SIMDVectorDouble lengthSquared = VectorDouble2LengthSq(value);
    return SIMDVectorDoubleDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, s_DoubleSIMDMaskXY);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2NormalizeEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVectorSingle lengthSq = _mm_dp_ps(value, value, 0x3F);
    return _mm_and_ps(_mm_mul_ps(value, _mm_rsqrt_ps(lengthSq)), s_SIMDMaskXY);
#else
    return VectorAndInt(VectorMultiply(value, Vector2ReciprocalLengthEst(value)), s_SIMDMaskXY);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2NormalizeEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleAndInt(VectorDoubleMultiply(value, VectorDouble2ReciprocalLengthEst(value)), s_DoubleSIMDMaskXY);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2ClampLengthV(SIMDVectorSingle value, SIMDVectorSingle lengthMin, SIMDVectorSingle lengthMax)noexcept{
    NWB_ASSERT(VectorGetY(lengthMin) == VectorGetX(lengthMin));
    NWB_ASSERT(VectorGetY(lengthMax) == VectorGetX(lengthMax));
    NWB_ASSERT(Vector2GreaterOrEqual(lengthMin, s_SIMDZero));
    NWB_ASSERT(Vector2GreaterOrEqual(lengthMax, s_SIMDZero));
    NWB_ASSERT(Vector2GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVectorSingle lengthSq = Vector2LengthSq(value);
    return VectorAndInt(SIMDVectorDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax), s_SIMDMaskXY);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2ClampLengthV(SIMDVectorDouble value, SIMDVectorDouble lengthMin, SIMDVectorDouble lengthMax)noexcept{
    NWB_ASSERT(VectorDoubleGetY(lengthMin) == VectorDoubleGetX(lengthMin));
    NWB_ASSERT(VectorDoubleGetY(lengthMax) == VectorDoubleGetX(lengthMax));
    NWB_ASSERT(VectorDouble2GreaterOrEqual(lengthMin, s_DoubleSIMDZero));
    NWB_ASSERT(VectorDouble2GreaterOrEqual(lengthMax, s_DoubleSIMDZero));
    NWB_ASSERT(VectorDouble2GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVectorDouble lengthSq = VectorDouble2LengthSq(value);
    return VectorDoubleAndInt(SIMDVectorDoubleDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax), s_DoubleSIMDMaskXY);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2ClampLength(SIMDVectorSingle value, f32 lengthMin, f32 lengthMax)noexcept{
    return Vector2ClampLengthV(value, VectorReplicate(lengthMin), VectorReplicate(lengthMax));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2ClampLength(SIMDVectorDouble value, f64 lengthMin, f64 lengthMax)noexcept{
    return VectorDouble2ClampLengthV(value, VectorDoubleReplicate(lengthMin), VectorDoubleReplicate(lengthMax));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Reflect(SIMDVectorSingle incident, SIMDVectorSingle normal)noexcept{
    SIMDVectorSingle result = Vector2Dot(incident, normal);
    result = VectorAdd(result, result);
    return VectorNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Reflect(SIMDVectorDouble incident, SIMDVectorDouble normal)noexcept{
    SIMDVectorDouble result = VectorDouble2Dot(incident, normal);
    result = VectorDoubleAdd(result, result);
    return VectorDoubleNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2RefractV(SIMDVectorSingle incident, SIMDVectorSingle normal, SIMDVectorSingle refractionIndex)noexcept{
    return SIMDVectorDetail::RefractV(incident, normal, refractionIndex, Vector2Dot(incident, normal));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2RefractV(SIMDVectorDouble incident, SIMDVectorDouble normal, SIMDVectorDouble refractionIndex)noexcept{
    return SIMDVectorDoubleDetail::RefractV(incident, normal, refractionIndex, VectorDouble2Dot(incident, normal));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Refract(SIMDVectorSingle incident, SIMDVectorSingle normal, f32 refractionIndex)noexcept{
    return Vector2RefractV(incident, normal, VectorReplicate(refractionIndex));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Refract(SIMDVectorDouble incident, SIMDVectorDouble normal, f64 refractionIndex)noexcept{
    return VectorDouble2RefractV(incident, normal, VectorDoubleReplicate(refractionIndex));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Orthogonal(SIMDVectorSingle value)noexcept{
    return VectorMultiply(VectorSwizzle<1, 0, 2, 3>(value), VectorSet(-1.0f, 1.0f, 0.0f, 0.0f));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Orthogonal(SIMDVectorDouble value)noexcept{
    return VectorDoubleMultiply(VectorDoubleSwizzle<1, 0, 2, 3>(value), VectorDoubleSet(-1.0, 1.0, 0.0, 0.0));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2AngleBetweenNormals(SIMDVectorSingle n0, SIMDVectorSingle n1)noexcept{
    return VectorACos(VectorClamp(Vector2Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2AngleBetweenNormals(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{
    return SIMDVectorDoubleDetail::ACosReplicated(VectorDoubleClamp(VectorDouble2Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2AngleBetweenNormalsEst(SIMDVectorSingle n0, SIMDVectorSingle n1)noexcept{ return VectorACosEst(VectorClamp(Vector2Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2AngleBetweenNormalsEst(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{ return SIMDVectorDoubleDetail::ACosReplicated(VectorDoubleClamp(VectorDouble2Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne)); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2AngleBetweenVectors(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    const SIMDVectorSingle reciprocalLength0 = Vector2ReciprocalLength(v0);
    const SIMDVectorSingle reciprocalLength1 = Vector2ReciprocalLength(v1);
    SIMDVectorSingle cosAngle = VectorMultiply(Vector2Dot(v0, v1), VectorMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorClamp(cosAngle, s_SIMDNegativeOne, s_SIMDOne);
    return VectorACos(cosAngle);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2AngleBetweenVectors(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDouble reciprocalLength0 = VectorDouble2ReciprocalLength(v0);
    const SIMDVectorDouble reciprocalLength1 = VectorDouble2ReciprocalLength(v1);
    SIMDVectorDouble cosAngle = VectorDoubleMultiply(VectorDouble2Dot(v0, v1), VectorDoubleMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorDoubleClamp(cosAngle, s_DoubleSIMDNegativeOne, s_DoubleSIMDOne);
    return SIMDVectorDoubleDetail::ACosReplicated(cosAngle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2LinePointDistance(SIMDVectorSingle linePoint0, SIMDVectorSingle linePoint1, SIMDVectorSingle point)noexcept{
    const SIMDVectorSingle line = VectorSubtract(linePoint1, linePoint0);
    const SIMDVectorSingle pointVector = VectorSubtract(point, linePoint0);
    const SIMDVectorSingle projectionScale = VectorDivide(Vector2Dot(pointVector, line), Vector2LengthSq(line));
    return Vector2Length(VectorSubtract(pointVector, VectorMultiply(line, projectionScale)));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2LinePointDistance(SIMDVectorDouble linePoint0, SIMDVectorDouble linePoint1, SIMDVectorDouble point)noexcept{
    const SIMDVectorDouble line = VectorDoubleSubtract(linePoint1, linePoint0);
    const SIMDVectorDouble pointVector = VectorDoubleSubtract(point, linePoint0);
    const SIMDVectorDouble projectionScale = VectorDoubleDivide(VectorDouble2Dot(pointVector, line), VectorDouble2LengthSq(line));
    return VectorDouble2Length(VectorDoubleSubtract(pointVector, VectorDoubleMultiply(line, projectionScale)));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2IntersectLine(SIMDVectorSingle line1Point1, SIMDVectorSingle line1Point2, SIMDVectorSingle line2Point1, SIMDVectorSingle line2Point2)noexcept{
    const SIMDVectorSingle v1 = VectorSubtract(line1Point2, line1Point1);
    const SIMDVectorSingle v2 = VectorSubtract(line2Point2, line2Point1);
    const SIMDVectorSingle v3 = VectorSubtract(line1Point1, line2Point1);
    const SIMDVectorSingle c1 = Vector2Cross(v1, v2);
    const SIMDVectorSingle c2 = Vector2Cross(v2, v3);

    const SIMDVectorSingle resultMask = VectorGreater(VectorAbs(c1), s_SIMDEpsilon);
    const SIMDVectorSingle failMask = VectorLessOrEqual(VectorAbs(c2), s_SIMDEpsilon);
    const SIMDVectorSingle fail = VectorOrInt(VectorAndInt(failMask, s_SIMDInfinity), VectorAndCInt(s_SIMDQNaN, failMask));
    const SIMDVectorSingle intersection = VectorMultiplyAdd(v1, VectorDivide(c2, c1), line1Point1);
    return VectorSelect(fail, intersection, resultMask);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2IntersectLine(SIMDVectorDouble line1Point1, SIMDVectorDouble line1Point2, SIMDVectorDouble line2Point1, SIMDVectorDouble line2Point2)noexcept{
    const SIMDVectorDouble v1 = VectorDoubleSubtract(line1Point2, line1Point1);
    const SIMDVectorDouble v2 = VectorDoubleSubtract(line2Point2, line2Point1);
    const SIMDVectorDouble v3 = VectorDoubleSubtract(line1Point1, line2Point1);
    const SIMDVectorDouble c1 = VectorDouble2Cross(v1, v2);
    const SIMDVectorDouble c2 = VectorDouble2Cross(v2, v3);

    const SIMDVectorDouble resultMask = VectorDoubleGreater(VectorDoubleAbs(c1), s_DoubleSIMDEpsilon);
    const SIMDVectorDouble failMask = VectorDoubleLessOrEqual(VectorDoubleAbs(c2), s_DoubleSIMDEpsilon);
    const SIMDVectorDouble fail = VectorDoubleOrInt(VectorDoubleAndInt(failMask, s_DoubleSIMDInfinity), VectorDoubleAndCInt(s_DoubleSIMDQNaN, failMask));
    const SIMDVectorDouble intersection = VectorDoubleMultiplyAdd(v1, VectorDoubleDivide(c2, c1), line1Point1);
    return VectorDoubleSelect(fail, intersection, resultMask);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Transform(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept;

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Transform(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2TransformCoord(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept;

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2TransformCoord(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2TransformNormal(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept;

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2TransformNormal(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

