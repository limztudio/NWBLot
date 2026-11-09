// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "quaternion_arithmetic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL QuaternionDoubleSlerpV(SIMDVectorDouble q0, SIMDVectorDouble q1, SIMDVectorDouble t)noexcept{
    FOUNDATION_ASSERT((VectorDoubleGetY(t) == VectorDoubleGetX(t)) && (VectorDoubleGetZ(t) == VectorDoubleGetX(t)) && (VectorDoubleGetW(t) == VectorDoubleGetX(t)));

    const SIMDVectorDouble oneMinusEpsilon = VectorDoubleReplicate(1.0 - SIMDQuaternionDoubleDetail::s_DoubleQuaternionInterpolationEpsilon);
    SIMDVectorDouble cosOmega = QuaternionDoubleDot(q0, q1);
    SIMDVectorDouble control = VectorDoubleLess(cosOmega, VectorDoubleZero());
    SIMDVectorDouble sign = VectorDoubleSelect(s_DoubleSIMDOne, s_DoubleSIMDNegativeOne, control);

    cosOmega = VectorDoubleMultiply(cosOmega, sign);
    control = VectorDoubleLess(cosOmega, oneMinusEpsilon);

    SIMDVectorDouble sinOmega = VectorDoubleNegativeMultiplySubtract(cosOmega, cosOmega, s_DoubleSIMDOne);
    sinOmega = VectorDoubleSqrt(sinOmega);

    const SIMDVectorDouble omega = SIMDVectorDoubleDetail::ATan2Replicated(sinOmega, cosOmega);
    SIMDVectorDouble signMask = VectorDoubleSplatSignMask();
    SIMDVectorDouble v01 = VectorDoubleShiftLeft(t, VectorDoubleZero(), 2);
    signMask = VectorDoubleShiftLeft(signMask, VectorDoubleZero(), 3);
    v01 = VectorDoubleXorInt(v01, signMask);
    v01 = VectorDoubleAdd(s_DoubleSIMDIdentityR0, v01);

    SIMDVectorDouble s0 = VectorDoubleMultiply(v01, omega);
    s0 = VectorDoubleSin(s0);
    s0 = VectorDoubleMultiply(s0, VectorDoubleReciprocal(sinOmega));
    s0 = VectorDoubleSelect(v01, s0, control);

    SIMDVectorDouble s1 = VectorDoubleSplatY(s0);
    s0 = VectorDoubleSplatX(s0);
    s1 = VectorDoubleMultiply(s1, sign);

    SIMDVectorDouble result = VectorDoubleMultiply(q0, s0);
    return VectorDoubleMultiplyAdd(q1, s1, result);
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL QuaternionDoubleSlerp(SIMDVectorDouble q0, SIMDVectorDouble q1, f64 t)noexcept{
    return QuaternionDoubleSlerpV(q0, q1, VectorDoubleReplicate(t));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

