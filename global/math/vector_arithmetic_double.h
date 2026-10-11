// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_compare_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNegate(SIMDVectorDouble value)noexcept{
    return -value;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAdd(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return v0 + v1;
}

// Adds raw unsigned 64-bit lane representations with modulo arithmetic.
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAddInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits + v1Bits);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSubtract(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return v0 - v1;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMultiply(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return v0 * v1;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleDivide(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return v0 / v1;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMultiplyAdd(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
#if defined(NWB_HAS_FMA3) || defined(NWB_HAS_NEON)
    return __builtin_elementwise_fma(v0, v1, v2);
#else
    return VectorDoubleAdd(VectorDoubleMultiply(v0, v1), v2);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNegativeMultiplySubtract(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
#if defined(NWB_HAS_FMA3) || defined(NWB_HAS_NEON)
    return __builtin_elementwise_fma(-v0, v1, v2);
#else
    return VectorDoubleSubtract(v2, VectorDoubleMultiply(v0, v1));
#endif
}

// Preserve the compiler's contraction policy for a source multiply-add expression instead of requiring an explicit FMA.
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMultiplyAddExpression(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
    return v0 * v1 + v2;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNegativeMultiplySubtractExpression(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
    return v2 - v0 * v1;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleScale(SIMDVectorDouble value, f64 scale)noexcept{
    return value * scale;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMin(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return (v0 < v1) ? v0 : v1;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMax(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return (v0 > v1) ? v0 : v1;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleClamp(SIMDVectorDouble value, SIMDVectorDouble minValue, SIMDVectorDouble maxValue)noexcept{
    NWB_ASSERT((VectorDoubleMoveMask(VectorDoubleLessOrEqual(minValue, maxValue)) & VectorDoubleComponentMask::s_DoubleXYZW) == VectorDoubleComponentMask::s_DoubleXYZW);
    return VectorDoubleMin(maxValue, VectorDoubleMax(minValue, value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSaturate(SIMDVectorDouble value)noexcept{
    return VectorDoubleMin(VectorDoubleSplatOne(), VectorDoubleMax(VectorDoubleZero(), value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAbs(SIMDVectorDouble value)noexcept{
    return VectorDoubleAndInt(value, s_DoubleSIMDAbsMask);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSum(SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble xy = VectorDoubleAdd(VectorDoubleSplatX(value), VectorDoubleSplatY(value));
    const SIMDVectorDouble xyz = VectorDoubleAdd(xy, VectorDoubleSplatZ(value));
    return VectorDoubleAdd(xyz, VectorDoubleSplatW(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMergeX(SIMDVectorDouble x, SIMDVectorDouble y, SIMDVectorDouble z, SIMDVectorDouble w)noexcept{
    const SIMDVectorDouble xy = VectorDoublePermute<0, 4, 1, 5>(x, y);
    const SIMDVectorDouble zw = VectorDoublePermute<0, 4, 1, 5>(z, w);
    return VectorDoublePermute<0, 1, 4, 5>(xy, zw);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4MinComponent(SIMDVectorDouble value)noexcept{
    SIMDVectorDouble result = VectorDoubleMin(value, VectorDoubleSwizzle<2, 3, 0, 1>(value));
    result = VectorDoubleMin(result, VectorDoubleSwizzle<1, 0, 3, 2>(result));
    return result;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4MaxComponent(SIMDVectorDouble value)noexcept{
    SIMDVectorDouble result = VectorDoubleMax(value, VectorDoubleSwizzle<2, 3, 0, 1>(value));
    result = VectorDoubleMax(result, VectorDoubleSwizzle<1, 0, 3, 2>(result));
    return result;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3MinComponent(SIMDVectorDouble value)noexcept{
    return VectorDouble4MinComponent(VectorDoubleSwizzle<0, 1, 2, 0>(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3MaxComponent(SIMDVectorDouble value)noexcept{
    return VectorDouble4MaxComponent(VectorDoubleSwizzle<0, 1, 2, 0>(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReciprocal(SIMDVectorDouble value)noexcept{
    return VectorDoubleDivide(VectorDoubleSplatOne(), value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReciprocalEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocal(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSqrt(SIMDVectorDouble value)noexcept{
    return __builtin_elementwise_sqrt(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSqrtEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleSqrt(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReciprocalSqrt(SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocal(VectorDoubleSqrt(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReciprocalSqrtEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleReciprocalSqrt(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleRound(SIMDVectorDouble value)noexcept{
    return __builtin_elementwise_roundeven(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleTruncate(SIMDVectorDouble value)noexcept{
    return __builtin_elementwise_trunc(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleFloor(SIMDVectorDouble value)noexcept{
    return __builtin_elementwise_floor(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleCeiling(SIMDVectorDouble value)noexcept{
    return __builtin_elementwise_ceil(value);
}

namespace SIMDVectorDoubleDetail{


[[nodiscard]] NWB_INLINE f64 FloorRemainder(const f64 numerator, const f64 denominator)noexcept{
    const f64 remainder = FMod(numerator, denominator);
    if(remainder == 0.0)
        return CopySign(0.0, denominator);
    if(SignBit(remainder) != SignBit(denominator))
        return remainder + denominator;
    return remainder;
}


};


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMod(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return VectorDoubleSet(
        SIMDVectorDoubleDetail::FloorRemainder(v0[0], v1[0]),
        SIMDVectorDoubleDetail::FloorRemainder(v0[1], v1[1]),
        SIMDVectorDoubleDetail::FloorRemainder(v0[2], v1[2]),
        SIMDVectorDoubleDetail::FloorRemainder(v0[3], v1[3])
    );
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleModAngles(SIMDVectorDouble angles)noexcept{
    return VectorDoubleSet(
        Remainder(angles[0], s_Double2PI),
        Remainder(angles[1], s_Double2PI),
        Remainder(angles[2], s_Double2PI),
        Remainder(angles[3], s_Double2PI)
    );
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAddAngles(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    SIMDVectorDouble result = VectorDoubleAdd(v0, v1);
    SIMDVectorDouble mask = VectorDoubleLess(result, s_DoubleSIMDNegativePi);
    SIMDVectorDouble offset = VectorDoubleSelect(VectorDoubleZero(), s_DoubleSIMDTwoPi, mask);
    mask = VectorDoubleGreaterOrEqual(result, s_DoubleSIMDPi);
    offset = VectorDoubleSelect(offset, s_DoubleSIMDNegativeTwoPi, mask);
    return VectorDoubleAdd(result, offset);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSubtractAngles(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    SIMDVectorDouble result = VectorDoubleSubtract(v0, v1);
    SIMDVectorDouble mask = VectorDoubleLess(result, s_DoubleSIMDNegativePi);
    SIMDVectorDouble offset = VectorDoubleSelect(VectorDoubleZero(), s_DoubleSIMDTwoPi, mask);
    mask = VectorDoubleGreaterOrEqual(result, s_DoubleSIMDPi);
    offset = VectorDoubleSelect(offset, s_DoubleSIMDNegativeTwoPi, mask);
    return VectorDoubleAdd(result, offset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

