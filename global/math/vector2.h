// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_interp.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool SIMDCALL Vector2Equal(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorEqual(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }
NWB_INLINE bool SIMDCALL Vector2EqualInt(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorEqualInt(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }
NWB_INLINE bool SIMDCALL Vector2NearEqual(SIMDVector v0, SIMDVector v1, SIMDVector epsilon)noexcept{ return (VectorMoveMask(VectorNearEqual(v0, v1, epsilon)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }
NWB_INLINE bool SIMDCALL Vector2NotEqual(SIMDVector v0, SIMDVector v1)noexcept{ return !Vector2Equal(v0, v1); }
NWB_INLINE bool SIMDCALL Vector2NotEqualInt(SIMDVector v0, SIMDVector v1)noexcept{ return !Vector2EqualInt(v0, v1); }
NWB_INLINE bool SIMDCALL Vector2Greater(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorGreater(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }
NWB_INLINE bool SIMDCALL Vector2GreaterOrEqual(SIMDVector v0, SIMDVector v1)noexcept{ return (VectorMoveMask(VectorGreaterOrEqual(v0, v1)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }
NWB_INLINE bool SIMDCALL Vector2Less(SIMDVector v0, SIMDVector v1)noexcept{ return Vector2Greater(v1, v0); }
NWB_INLINE bool SIMDCALL Vector2LessOrEqual(SIMDVector v0, SIMDVector v1)noexcept{ return Vector2GreaterOrEqual(v1, v0); }
NWB_INLINE bool SIMDCALL Vector2InBounds(SIMDVector value, SIMDVector bounds)noexcept{ return (VectorMoveMask(VectorInBounds(value, bounds)) & VectorComponentMask::s_XY) == VectorComponentMask::s_XY; }
NWB_INLINE bool SIMDCALL Vector2IsNaN(SIMDVector value)noexcept{ return (VectorMoveMask(VectorIsNaN(value)) & VectorComponentMask::s_XY) != 0; }
NWB_INLINE bool SIMDCALL Vector2IsInfinite(SIMDVector value)noexcept{ return (VectorMoveMask(VectorIsInfinite(value)) & VectorComponentMask::s_XY) != 0; }

NWB_INLINE u32 SIMDCALL Vector2EqualR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqual(v0, v1)), VectorComponentMask::s_XY); }
NWB_INLINE u32 SIMDCALL Vector2EqualIntR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqualInt(v0, v1)), VectorComponentMask::s_XY); }
NWB_INLINE u32 SIMDCALL Vector2GreaterR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreater(v0, v1)), VectorComponentMask::s_XY); }
NWB_INLINE u32 SIMDCALL Vector2GreaterOrEqualR(SIMDVector v0, SIMDVector v1)noexcept{ return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreaterOrEqual(v0, v1)), VectorComponentMask::s_XY); }
NWB_INLINE u32 SIMDCALL Vector2InBoundsR(SIMDVector value, SIMDVector bounds)noexcept{ return SIMDVectorDetail::BoundsMaskR(VectorMoveMask(VectorInBounds(value, bounds)), VectorComponentMask::s_XY); }

NWB_INLINE SIMDVector SIMDCALL Vector2Dot(SIMDVector v0, SIMDVector v1)noexcept{
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

NWB_INLINE SIMDVector SIMDCALL Vector2Cross(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(v0.f[0] * v1.f[1] - v0.f[1] * v1.f[0]);
#elif defined(NWB_HAS_NEON)
    const float32x2_t negate = vcreate_f32(0xBF8000003F800000ull);
    float32x2_t result = vmul_f32(vget_low_f32(v0), vrev64_f32(vget_low_f32(v1)));
    result = vmul_f32(result, negate);
    result = vpadd_f32(result, result);
    return vcombine_f32(result, result);
#else
    SIMDVector result = VectorSwizzle<1, 0, 1, 0>(v1);
    result = VectorMultiply(result, v0);
    const SIMDVector temp = VectorSplatY(result);
    result = _mm_sub_ss(result, temp);
    return VectorSplatX(result);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2LengthSq(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_dp_ps(value, value, 0x3F);
#else
    return Vector2Dot(value, value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2ReciprocalLength(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector lengthSq = _mm_dp_ps(value, value, 0x3F);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    return _mm_div_ps(s_SIMDOne, length);
#else
    return VectorReciprocalSqrt(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2ReciprocalLengthEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_rsqrt_ps(_mm_dp_ps(value, value, 0x3F));
#else
    return VectorReciprocalSqrtEst(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2Length(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x3F));
#else
    return VectorSqrt(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2LengthEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_sqrt_ps(_mm_dp_ps(value, value, 0x3F));
#else
    return VectorSqrtEst(Vector2LengthSq(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2Normalize(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    SIMDVector lengthSq = _mm_dp_ps(value, value, 0x3F);
    const SIMDVector length = _mm_sqrt_ps(lengthSq);
    const SIMDVector zeroMask = _mm_cmpneq_ps(_mm_setzero_ps(), length);
    lengthSq = _mm_cmpneq_ps(lengthSq, s_SIMDInfinity);
    SIMDVector result = _mm_div_ps(value, length);
    result = _mm_and_ps(result, zeroMask);
    return _mm_and_ps(_mm_or_ps(_mm_andnot_ps(lengthSq, s_SIMDQNaN), _mm_and_ps(result, lengthSq)), s_SIMDMaskXY);
#else
    const SIMDVector lengthSq = Vector2LengthSq(value);
    const SIMDVector length = VectorSqrt(lengthSq);
    SIMDVector result = VectorDivide(value, length);
    result = VectorAndInt(result, VectorNotEqual(length, VectorZero()));
    return VectorAndInt(VectorSelect(s_SIMDQNaN, result, VectorNotEqualInt(lengthSq, s_SIMDInfinity)), s_SIMDMaskXY);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL NormalizeOrV(
    SIMDVector value,
    SIMDVector fallback,
    SIMDVector lengthSquared,
    const f32 minLengthSquared,
    SIMDVector componentMask
)noexcept{
    const SIMDVector validLength = VectorAndInt(
        VectorGreater(lengthSquared, VectorReplicate(Max(0.0f, minLengthSquared))),
        VectorEqualInt(VectorOrInt(VectorIsNaN(lengthSquared), VectorIsInfinite(lengthSquared)), VectorFalseInt())
    );
    SIMDVector normalized = VectorAndInt(VectorMultiply(value, VectorReciprocalSqrt(lengthSquared)), componentMask);
    fallback = VectorAndInt(fallback, componentMask);
    const SIMDVector validNormalized = VectorEqualInt(
        VectorOrInt(VectorIsNaN(normalized), VectorIsInfinite(normalized)),
        VectorFalseInt()
    );
    return VectorSelect(fallback, normalized, VectorAndInt(validLength, validNormalized));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVector SIMDCALL Vector2NormalizeOr(SIMDVector value, SIMDVector fallback, const f32 minLengthSquared)noexcept{
    const SIMDVector lengthSquared = Vector2LengthSq(value);
    return SIMDVectorDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, s_SIMDMaskXY);
}

NWB_INLINE SIMDVector SIMDCALL Vector2NormalizeEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector lengthSq = _mm_dp_ps(value, value, 0x3F);
    return _mm_and_ps(_mm_mul_ps(value, _mm_rsqrt_ps(lengthSq)), s_SIMDMaskXY);
#else
    return VectorAndInt(VectorMultiply(value, Vector2ReciprocalLengthEst(value)), s_SIMDMaskXY);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL ClampLengthV(SIMDVector value, SIMDVector lengthSq, SIMDVector lengthMin, SIMDVector lengthMax)noexcept{
    const SIMDVector reciprocalLength = VectorReciprocalSqrt(lengthSq);
    const SIMDVector infiniteLength = VectorEqualInt(lengthSq, s_SIMDInfinity);
    const SIMDVector zeroLength = VectorEqual(lengthSq, s_SIMDZero);
    const SIMDVector select = VectorEqualInt(infiniteLength, zeroLength);
    SIMDVector normal = VectorMultiply(value, reciprocalLength);
    SIMDVector length = VectorMultiply(lengthSq, reciprocalLength);
    length = VectorSelect(lengthSq, length, select);
    normal = VectorSelect(lengthSq, normal, select);
    const SIMDVector controlMax = VectorGreater(length, lengthMax);
    const SIMDVector controlMin = VectorLess(length, lengthMin);
    SIMDVector clampedLength = VectorSelect(length, lengthMax, controlMax);
    clampedLength = VectorSelect(clampedLength, lengthMin, controlMin);
    SIMDVector result = VectorMultiply(normal, clampedLength);
    return VectorSelect(result, value, VectorEqualInt(controlMax, controlMin));
}

NWB_INLINE SIMDVector SIMDCALL RefractV(
    SIMDVector incident,
    SIMDVector normal,
    SIMDVector refractionIndex,
    SIMDVector dot)noexcept
{
    SIMDVector r = VectorNegativeMultiplySubtract(dot, dot, s_SIMDOne);
    const SIMDVector refractionIndexSq = VectorMultiply(refractionIndex, refractionIndex);
    r = VectorNegativeMultiplySubtract(r, refractionIndexSq, s_SIMDOne);
    const SIMDVector totalInternalReflection = VectorLess(r, s_SIMDZero);
    r = VectorMultiplyAdd(refractionIndex, dot, VectorSqrt(VectorMax(r, s_SIMDZero)));
    SIMDVector result = VectorMultiply(refractionIndex, incident);
    result = VectorNegativeMultiplySubtract(r, normal, result);
    return VectorSelect(result, VectorZero(), totalInternalReflection);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL Vector2ClampLengthV(SIMDVector value, SIMDVector lengthMin, SIMDVector lengthMax)noexcept{
    NWB_ASSERT(VectorGetY(lengthMin) == VectorGetX(lengthMin));
    NWB_ASSERT(VectorGetY(lengthMax) == VectorGetX(lengthMax));
    NWB_ASSERT(Vector2GreaterOrEqual(lengthMin, s_SIMDZero));
    NWB_ASSERT(Vector2GreaterOrEqual(lengthMax, s_SIMDZero));
    NWB_ASSERT(Vector2GreaterOrEqual(lengthMax, lengthMin));

    const SIMDVector lengthSq = Vector2LengthSq(value);
    return VectorAndInt(SIMDVectorDetail::ClampLengthV(value, lengthSq, lengthMin, lengthMax), s_SIMDMaskXY);
}

NWB_INLINE SIMDVector SIMDCALL Vector2ClampLength(SIMDVector value, f32 lengthMin, f32 lengthMax)noexcept{
    return Vector2ClampLengthV(value, VectorReplicate(lengthMin), VectorReplicate(lengthMax));
}

NWB_INLINE SIMDVector SIMDCALL Vector2Reflect(SIMDVector incident, SIMDVector normal)noexcept{
    SIMDVector result = Vector2Dot(incident, normal);
    result = VectorAdd(result, result);
    return VectorNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVector SIMDCALL Vector2RefractV(SIMDVector incident, SIMDVector normal, SIMDVector refractionIndex)noexcept{
    return SIMDVectorDetail::RefractV(incident, normal, refractionIndex, Vector2Dot(incident, normal));
}

NWB_INLINE SIMDVector SIMDCALL Vector2Refract(SIMDVector incident, SIMDVector normal, f32 refractionIndex)noexcept{
    return Vector2RefractV(incident, normal, VectorReplicate(refractionIndex));
}

NWB_INLINE SIMDVector SIMDCALL Vector2Orthogonal(SIMDVector value)noexcept{
    return VectorMultiply(VectorSwizzle<1, 0, 2, 3>(value), VectorSet(-1.0f, 1.0f, 0.0f, 0.0f));
}

NWB_INLINE SIMDVector SIMDCALL Vector2AngleBetweenNormals(SIMDVector n0, SIMDVector n1)noexcept{
    return VectorACos(VectorClamp(Vector2Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne));
}

NWB_INLINE SIMDVector SIMDCALL Vector2AngleBetweenNormalsEst(SIMDVector n0, SIMDVector n1)noexcept{ return VectorACosEst(VectorClamp(Vector2Dot(n0, n1), s_SIMDNegativeOne, s_SIMDOne)); }
NWB_INLINE SIMDVector SIMDCALL Vector2AngleBetweenVectors(SIMDVector v0, SIMDVector v1)noexcept{
    const SIMDVector reciprocalLength0 = Vector2ReciprocalLength(v0);
    const SIMDVector reciprocalLength1 = Vector2ReciprocalLength(v1);
    SIMDVector cosAngle = VectorMultiply(Vector2Dot(v0, v1), VectorMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorClamp(cosAngle, s_SIMDNegativeOne, s_SIMDOne);
    return VectorACos(cosAngle);
}

NWB_INLINE SIMDVector SIMDCALL Vector2LinePointDistance(SIMDVector linePoint0, SIMDVector linePoint1, SIMDVector point)noexcept{
    const SIMDVector line = VectorSubtract(linePoint1, linePoint0);
    const SIMDVector pointVector = VectorSubtract(point, linePoint0);
    const SIMDVector projectionScale = VectorDivide(Vector2Dot(pointVector, line), Vector2LengthSq(line));
    return Vector2Length(VectorSubtract(pointVector, VectorMultiply(line, projectionScale)));
}

NWB_INLINE SIMDVector SIMDCALL Vector2IntersectLine(SIMDVector line1Point1, SIMDVector line1Point2, SIMDVector line2Point1, SIMDVector line2Point2)noexcept{
    const SIMDVector v1 = VectorSubtract(line1Point2, line1Point1);
    const SIMDVector v2 = VectorSubtract(line2Point2, line2Point1);
    const SIMDVector v3 = VectorSubtract(line1Point1, line2Point1);
    const SIMDVector c1 = Vector2Cross(v1, v2);
    const SIMDVector c2 = Vector2Cross(v2, v3);

    const SIMDVector resultMask = VectorGreater(VectorAbs(c1), s_SIMDEpsilon);
    const SIMDVector failMask = VectorLessOrEqual(VectorAbs(c2), s_SIMDEpsilon);
    const SIMDVector fail = VectorOrInt(VectorAndInt(failMask, s_SIMDInfinity), VectorAndCInt(s_SIMDQNaN, failMask));
    const SIMDVector intersection = VectorMultiplyAdd(v1, VectorDivide(c2, c1), line1Point1);
    return VectorSelect(fail, intersection, resultMask);
}

NWB_INLINE SIMDVector SIMDCALL Vector2Transform(SIMDVector value, const SIMDMatrix& matrix)noexcept;
NWB_INLINE SIMDVector SIMDCALL Vector2TransformCoord(SIMDVector value, const SIMDMatrix& matrix)noexcept;
NWB_INLINE SIMDVector SIMDCALL Vector2TransformNormal(SIMDVector value, const SIMDMatrix& matrix)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

