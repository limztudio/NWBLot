// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_interp_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool NWB_SIMD_CALL VectorDouble2Equal(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleEqual(v0, v1)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble2EqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleEqualInt(v0, v1)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble2NearEqual(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble epsilon)noexcept{ return (VectorDoubleMoveMask(VectorDoubleNearEqual(v0, v1, epsilon)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble2NotEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return !VectorDouble2Equal(v0, v1); }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble2NotEqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return !VectorDouble2EqualInt(v0, v1); }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble2Greater(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleGreater(v0, v1)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble2GreaterOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return (VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(v0, v1)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble2Less(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDouble2Greater(v1, v0); }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble2LessOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return VectorDouble2GreaterOrEqual(v1, v0); }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble2InBounds(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{ return (VectorDoubleMoveMask(VectorDoubleInBounds(value, bounds)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble2IsNaN(SIMDVectorDouble value)noexcept{ return (VectorDoubleMoveMask(VectorDoubleIsNaN(value)) & VectorDoubleComponentMask::s_DoubleXY) != 0; }
NWB_INLINE bool NWB_SIMD_CALL VectorDouble2IsInfinite(SIMDVectorDouble value)noexcept{ return (VectorDoubleMoveMask(VectorDoubleIsInfinite(value)) & VectorDoubleComponentMask::s_DoubleXY) != 0; }

NWB_INLINE u32 NWB_SIMD_CALL VectorDouble2EqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleEqual(v0, v1)), VectorDoubleComponentMask::s_DoubleXY); }
NWB_INLINE u32 NWB_SIMD_CALL VectorDouble2EqualIntR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleEqualInt(v0, v1)), VectorDoubleComponentMask::s_DoubleXY); }
NWB_INLINE u32 NWB_SIMD_CALL VectorDouble2GreaterR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleGreater(v0, v1)), VectorDoubleComponentMask::s_DoubleXY); }
NWB_INLINE u32 NWB_SIMD_CALL VectorDouble2GreaterOrEqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{ return SIMDVectorDoubleDetail::ComparisonMaskR(VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(v0, v1)), VectorDoubleComponentMask::s_DoubleXY); }
NWB_INLINE u32 NWB_SIMD_CALL VectorDouble2InBoundsR(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{ return SIMDVectorDoubleDetail::BoundsMaskR(VectorDoubleMoveMask(VectorDoubleInBounds(value, bounds)), VectorDoubleComponentMask::s_DoubleXY); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Dot(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    v0 = VectorDoubleAndInt(v0, s_DoubleSIMDMaskXY);
    v1 = VectorDoubleAndInt(v1, s_DoubleSIMDMaskXY);
    const SIMDVectorDouble product = VectorDoubleMultiply(v0, v1);
    SIMDVectorDouble result = VectorDoubleAdd(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
    return result;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Cross(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    v0 = VectorDoubleAndInt(v0, s_DoubleSIMDMaskXY);
    v1 = VectorDoubleAndInt(v1, s_DoubleSIMDMaskXY);
    const SIMDVectorDouble product = VectorDoubleMultiply(v0, VectorDoubleSwizzle<1, 0, 3, 2>(v1));
    return VectorDoubleSubtract(VectorDoubleSplatX(product), VectorDoubleSplatY(product));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2LengthSq(SIMDVectorDouble value)noexcept{
    return VectorDouble2Dot(value, value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2ReciprocalLength(SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocalSqrt(VectorDouble2LengthSq(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2ReciprocalLengthEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocalSqrtEst(VectorDouble2LengthSq(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Length(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrt(VectorDouble2LengthSq(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2LengthEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrtEst(VectorDouble2LengthSq(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Normalize(SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble lengthSq = VectorDouble2LengthSq(value);
    const SIMDVectorDouble length = VectorDoubleSqrt(lengthSq);
    SIMDVectorDouble result = VectorDoubleDivide(value, length);
    result = VectorDoubleAndInt(result, VectorDoubleNotEqual(length, VectorDoubleZero()));
    return VectorDoubleAndInt(VectorDoubleSelect(s_DoubleSIMDQNaN, result, VectorDoubleNotEqualInt(lengthSq, s_DoubleSIMDInfinity)), s_DoubleSIMDMaskXY);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL NormalizeOrV(
    SIMDVectorDouble value,
    SIMDVectorDouble fallback,
    SIMDVectorDouble lengthSquared,
    const f64 minLengthSquared,
    SIMDVectorDouble componentMask
)noexcept{
    const SIMDVectorDouble validLength = VectorDoubleAndInt(
        VectorDoubleGreater(lengthSquared, VectorDoubleReplicate(Max(0.0, minLengthSquared))),
        VectorDoubleEqualInt(VectorDoubleOrInt(VectorDoubleIsNaN(lengthSquared), VectorDoubleIsInfinite(lengthSquared)), VectorDoubleFalseInt())
    );
    SIMDVectorDouble normalized = VectorDoubleAndInt(VectorDoubleMultiply(value, VectorDoubleReciprocalSqrt(lengthSquared)), componentMask);
    fallback = VectorDoubleAndInt(fallback, componentMask);
    const SIMDVectorDouble validNormalized = VectorDoubleEqualInt(
        VectorDoubleOrInt(VectorDoubleIsNaN(normalized), VectorDoubleIsInfinite(normalized)),
        VectorDoubleFalseInt()
    );
    return VectorDoubleSelect(fallback, normalized, VectorDoubleAndInt(validLength, validNormalized));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2NormalizeOr(SIMDVectorDouble value, SIMDVectorDouble fallback, const f64 minLengthSquared)noexcept{
    const SIMDVectorDouble lengthSquared = VectorDouble2LengthSq(value);
    return SIMDVectorDoubleDetail::NormalizeOrV(value, fallback, lengthSquared, minLengthSquared, s_DoubleSIMDMaskXY);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2NormalizeEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleAndInt(VectorDoubleMultiply(value, VectorDouble2ReciprocalLengthEst(value)), s_DoubleSIMDMaskXY);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL RefractV(
    SIMDVectorDouble incident,
    SIMDVectorDouble normal,
    SIMDVectorDouble refractionIndex,
    SIMDVectorDouble dot
)noexcept{
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

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Reflect(SIMDVectorDouble incident, SIMDVectorDouble normal)noexcept{
    SIMDVectorDouble result = VectorDouble2Dot(incident, normal);
    result = VectorDoubleAdd(result, result);
    return VectorDoubleNegativeMultiplySubtract(result, normal, incident);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2RefractV(SIMDVectorDouble incident, SIMDVectorDouble normal, SIMDVectorDouble refractionIndex)noexcept{
    return SIMDVectorDoubleDetail::RefractV(incident, normal, refractionIndex, VectorDouble2Dot(incident, normal));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Refract(SIMDVectorDouble incident, SIMDVectorDouble normal, f64 refractionIndex)noexcept{
    return VectorDouble2RefractV(incident, normal, VectorDoubleReplicate(refractionIndex));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Orthogonal(SIMDVectorDouble value)noexcept{
    return VectorDoubleMultiply(VectorDoubleSwizzle<1, 0, 2, 3>(value), VectorDoubleSet(-1.0, 1.0, 0.0, 0.0));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2AngleBetweenNormals(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{
    return VectorDoubleACos(VectorDoubleClamp(VectorDouble2Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2AngleBetweenNormalsEst(SIMDVectorDouble n0, SIMDVectorDouble n1)noexcept{ return VectorDoubleACosEst(VectorDoubleClamp(VectorDouble2Dot(n0, n1), s_DoubleSIMDNegativeOne, s_DoubleSIMDOne)); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2AngleBetweenVectors(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDouble reciprocalLength0 = VectorDouble2ReciprocalLength(v0);
    const SIMDVectorDouble reciprocalLength1 = VectorDouble2ReciprocalLength(v1);
    SIMDVectorDouble cosAngle = VectorDoubleMultiply(VectorDouble2Dot(v0, v1), VectorDoubleMultiply(reciprocalLength0, reciprocalLength1));
    cosAngle = VectorDoubleClamp(cosAngle, s_DoubleSIMDNegativeOne, s_DoubleSIMDOne);
    return VectorDoubleACos(cosAngle);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2LinePointDistance(SIMDVectorDouble linePoint0, SIMDVectorDouble linePoint1, SIMDVectorDouble point)noexcept{
    const SIMDVectorDouble line = VectorDoubleSubtract(linePoint1, linePoint0);
    const SIMDVectorDouble pointVector = VectorDoubleSubtract(point, linePoint0);
    const SIMDVectorDouble projectionScale = VectorDoubleDivide(VectorDouble2Dot(pointVector, line), VectorDouble2LengthSq(line));
    return VectorDouble2Length(VectorDoubleSubtract(pointVector, VectorDoubleMultiply(line, projectionScale)));
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

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Transform(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2TransformCoord(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2TransformNormal(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

