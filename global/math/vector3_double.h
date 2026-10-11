// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../expected.h"
#include "vector2_double.h"


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

