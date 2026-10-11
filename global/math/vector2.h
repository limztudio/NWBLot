// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_interp.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool NWB_SIMD_CALL Vector2Equal(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorEqual(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }
NWB_INLINE bool NWB_SIMD_CALL Vector2EqualInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorEqualInt(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }
NWB_INLINE bool NWB_SIMD_CALL Vector2NearEqual(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle epsilon)noexcept{ return (VectorMoveMask(VectorNearEqual(v0, v1, epsilon)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }
NWB_INLINE bool NWB_SIMD_CALL Vector2NotEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return !Vector2Equal(v0, v1); }
NWB_INLINE bool NWB_SIMD_CALL Vector2NotEqualInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return !Vector2EqualInt(v0, v1); }
NWB_INLINE bool NWB_SIMD_CALL Vector2Greater(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorGreater(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }
NWB_INLINE bool NWB_SIMD_CALL Vector2GreaterOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return (VectorMoveMask(VectorGreaterOrEqual(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }
NWB_INLINE bool NWB_SIMD_CALL Vector2Less(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return Vector2Greater(v1, v0); }
NWB_INLINE bool NWB_SIMD_CALL Vector2LessOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return Vector2GreaterOrEqual(v1, v0); }
NWB_INLINE bool NWB_SIMD_CALL Vector2InBounds(SIMDVectorSingle value, SIMDVectorSingle bounds)noexcept{ return (VectorMoveMask(VectorInBounds(value, bounds)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }
NWB_INLINE bool NWB_SIMD_CALL Vector2IsNaN(SIMDVectorSingle value)noexcept{ return (VectorMoveMask(VectorIsNaN(value)) & VectorComponentMask::s_XY) != 0; }
NWB_INLINE bool NWB_SIMD_CALL Vector2IsInfinite(SIMDVectorSingle value)noexcept{ return (VectorMoveMask(VectorIsInfinite(value)) & VectorComponentMask::s_XY) != 0; }

NWB_INLINE u32 NWB_SIMD_CALL Vector2EqualR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqual(v0, v1)), VectorComponentMask::s_XY); }
NWB_INLINE u32 NWB_SIMD_CALL Vector2EqualIntR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqualInt(v0, v1)), VectorComponentMask::s_XY); }
NWB_INLINE u32 NWB_SIMD_CALL Vector2GreaterR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreater(v0, v1)), VectorComponentMask::s_XY); }
NWB_INLINE u32 NWB_SIMD_CALL Vector2GreaterOrEqualR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreaterOrEqual(v0, v1)), VectorComponentMask::s_XY); }
NWB_INLINE u32 NWB_SIMD_CALL Vector2InBoundsR(SIMDVectorSingle value, SIMDVectorSingle bounds)noexcept{ return SIMDVectorDetail::BoundsMaskR(VectorMoveMask(VectorInBounds(value, bounds)), VectorComponentMask::s_XY); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Dot(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(VectorGetX(v0) * VectorGetX(v1) + VectorGetY(v0) * VectorGetY(v1));
#elif defined(NWB_HAS_NEON)
    const float32x2_t product = vmul_f32(vget_low_f32(v0), vget_low_f32(v1));
    const float32x2_t result = vpadd_f32(product, product);
    return vcombine_f32(result, result);
#else
    return _mm_dp_ps(v0, v1, 0x3F);
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
    SIMDVectorSingle result = VectorSwizzle<1, 0, 1, 0>(v1);
    result = VectorMultiply(result, v0);
    const SIMDVectorSingle temp = VectorSplatY(result);
    result = _mm_sub_ss(result, temp);
    return VectorSplatX(result);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2LengthSq(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_dp_ps(value, value, 0x3F);
#else
    return Vector2Dot(value, value);
#endif
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

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2ReciprocalLengthEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_rsqrt_ps(_mm_dp_ps(value, value, 0x3F));
#else
    return VectorReciprocalSqrtEst(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Length(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x3F));
#else
    return VectorSqrt(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2LengthEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x3F));
#else
    return VectorSqrtEst(Vector2LengthSq(value));
#endif
}

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL NormalizeOrV(
    SIMDVectorSingle value,
    SIMDVectorSingle fallback,
    SIMDVectorSingle lengthSquared,
    const f32 minLengthSquared,
    SIMDVectorSingle componentMask
)noexcept{
    const SIMDVectorSingle validLength = VectorAndInt(
        VectorGreater(lengthSquared, VectorReplicate(Max(0.0f, minLengthSquared))),
        VectorEqualInt(VectorOrInt(VectorIsNaN(lengthSquared), VectorIsInfinite(lengthSquared)), VectorFalseInt())
    );
    SIMDVectorSingle normalized = VectorAndInt(VectorMultiply(value, VectorReciprocalSqrt(lengthSquared)), componentMask);
    fallback = VectorAndInt(fallback, componentMask);
    const SIMDVectorSingle validNormalized = VectorEqualInt(
        VectorOrInt(VectorIsNaN(normalized), VectorIsInfinite(normalized)),
        VectorFalseInt()
    );
    return VectorSelect(fallback, normalized, VectorAndInt(validLength, validNormalized));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2NormalizeOr(SIMDVectorSingle value, SIMDVectorSingle fallback, const f32 minLengthSquared)noexcept{
    const SIMDVectorSingle lengthSquared = Vector2LengthSq(value);
    return SIMDVectorDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, s_SIMDMaskXY);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2NormalizeEst(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVectorSingle lengthSq = _mm_dp_ps(value, value, 0x3F);
    return _mm_and_ps(_mm_mul_ps(value, _mm_rsqrt_ps(lengthSq)), s_SIMDMaskXY);
#else
    return VectorAndInt(VectorMultiply(value, Vector2ReciprocalLengthEst(value)), s_SIMDMaskXY);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL RefractV(
    SIMDVectorSingle incident,
    SIMDVectorSingle normal,
    SIMDVectorSingle refractionIndex,
    SIMDVectorSingle dot
)noexcept{
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


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2ClampLengthV(SIMDVectorSingle value, SIMDVectorSingle lengthMin, SIMDVectorSingle lengthMax)noexcept{
    NWB_ASSERT(VectorGetY(lengthMin) == VectorGetX(lengthMin));
    NWB_ASSERT(VectorGetY(lengthMax) == VectorGetX(lengthMax));
    NWB_ASSERT(Vector2GreaterOrEqual(lengthMin, s_SIMDZero));
    NWB_ASSERT(Vector2GreaterOrEqual(lengthMax, s_SIMDZero));
    NWB_ASSERT(Vector2GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVectorSingle lengthSq = Vector2LengthSq(value);
    return VectorAndInt(SIMDVectorDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax), s_SIMDMaskXY);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2ClampLength(SIMDVectorSingle value, f32 lengthMin, f32 lengthMax)noexcept{
    return Vector2ClampLengthV(value, VectorReplicate(lengthMin), VectorReplicate(lengthMax));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Reflect(SIMDVectorSingle incident, SIMDVectorSingle normal)noexcept{
    SIMDVectorSingle result = Vector2Dot(incident, normal);
    result = VectorAdd(result, result);
    return VectorNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2RefractV(SIMDVectorSingle incident, SIMDVectorSingle normal, SIMDVectorSingle refractionIndex)noexcept{
    return SIMDVectorDetail::RefractV(incident, normal, refractionIndex, Vector2Dot(incident, normal));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Refract(SIMDVectorSingle incident, SIMDVectorSingle normal, f32 refractionIndex)noexcept{
    return Vector2RefractV(incident, normal, VectorReplicate(refractionIndex));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Orthogonal(SIMDVectorSingle value)noexcept{
    return VectorMultiply(VectorSwizzle<1, 0, 2, 3>(value), VectorSet(-1.0f, 1.0f, 0.0f, 0.0f));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2AngleBetweenNormals(SIMDVectorSingle n0, SIMDVectorSingle n1)noexcept{
    return VectorACos(VectorClamp(Vector2Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2AngleBetweenNormalsEst(SIMDVectorSingle n0, SIMDVectorSingle n1)noexcept{ return VectorACosEst(VectorClamp(Vector2Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2AngleBetweenVectors(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    const SIMDVectorSingle reciprocalLength0 = Vector2ReciprocalLength(v0);
    const SIMDVectorSingle reciprocalLength1 = Vector2ReciprocalLength(v1);
    SIMDVectorSingle cosAngle = VectorMultiply(Vector2Dot(v0, v1), VectorMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorClamp(cosAngle, s_SIMDNegativeOne, s_SIMDOne);
    return VectorACos(cosAngle);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2LinePointDistance(SIMDVectorSingle linePoint0, SIMDVectorSingle linePoint1, SIMDVectorSingle point)noexcept{
    const SIMDVectorSingle line = VectorSubtract(linePoint1, linePoint0);
    const SIMDVectorSingle pointVector = VectorSubtract(point, linePoint0);
    const SIMDVectorSingle projectionScale = VectorDivide(Vector2Dot(pointVector, line), Vector2LengthSq(line));
    return Vector2Length(VectorSubtract(pointVector, VectorMultiply(line, projectionScale)));
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

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2Transform(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept;
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2TransformCoord(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept;
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL Vector2TransformNormal(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

