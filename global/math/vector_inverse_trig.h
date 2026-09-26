// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_trig.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL VectorArcCoefficientApproximation(SIMDVector value)noexcept{
    SIMDVector x = VectorAbs(value);
    SIMDVector root = VectorSqrt(VectorMax(s_SIMDZero, VectorSubtract(s_SIMDOne, x)));

    SIMDVector result = VectorMultiplyAdd(VectorSplatW(s_SIMDArcCoefficients1), x, VectorSplatZ(s_SIMDArcCoefficients1));
    result = VectorMultiplyAdd(result, x, VectorSplatY(s_SIMDArcCoefficients1));
    result = VectorMultiplyAdd(result, x, VectorSplatX(s_SIMDArcCoefficients1));
    result = VectorMultiplyAdd(result, x, VectorSplatW(s_SIMDArcCoefficients0));
    result = VectorMultiplyAdd(result, x, VectorSplatZ(s_SIMDArcCoefficients0));
    result = VectorMultiplyAdd(result, x, VectorSplatY(s_SIMDArcCoefficients0));
    result = VectorMultiplyAdd(result, x, VectorSplatX(s_SIMDArcCoefficients0));
    return VectorMultiply(result, root);
}

NWB_INLINE SIMDVector SIMDCALL VectorATan2SelectResult(SIMDVector y, SIMDVector x, SIMDVector atanResult, SIMDVector constants)noexcept{
    const SIMDVector zero = VectorZero();
    SIMDVector atanResultValid = VectorTrueInt();

    SIMDVector pi = VectorSplatX(constants);
    SIMDVector piOverTwo = VectorSplatY(constants);
    SIMDVector piOverFour = VectorSplatZ(constants);
    SIMDVector threePiOverFour = VectorSplatW(constants);

    SIMDVector yEqualsZero = VectorEqual(y, zero);
    SIMDVector xEqualsZero = VectorEqual(x, zero);
    SIMDVector xIsPositive = VectorEqualInt(VectorAndInt(x, s_SIMDNegativeZero), zero);
    SIMDVector yEqualsInfinity = VectorIsInfinite(y);
    SIMDVector xEqualsInfinity = VectorIsInfinite(x);

    SIMDVector ySign = VectorAndInt(y, s_SIMDNegativeZero);
    pi = VectorOrInt(pi, ySign);
    piOverTwo = VectorOrInt(piOverTwo, ySign);
    piOverFour = VectorOrInt(piOverFour, ySign);
    threePiOverFour = VectorOrInt(threePiOverFour, ySign);

    SIMDVector r1 = VectorSelect(pi, ySign, xIsPositive);
    SIMDVector r2 = VectorSelect(atanResultValid, piOverTwo, xEqualsZero);
    SIMDVector r3 = VectorSelect(r2, r1, yEqualsZero);
    SIMDVector r4 = VectorSelect(threePiOverFour, piOverFour, xIsPositive);
    SIMDVector r5 = VectorSelect(piOverTwo, r4, xEqualsInfinity);
    SIMDVector result = VectorSelect(r3, r5, yEqualsInfinity);
    atanResultValid = VectorEqualInt(result, atanResultValid);

    r1 = VectorSelect(pi, s_SIMDNegativeZero, xIsPositive);
    r2 = VectorAdd(atanResult, r1);
    return VectorSelect(result, r2, atanResultValid);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL VectorASin(SIMDVector value)noexcept{
    const SIMDVector nonnegative = VectorGreaterOrEqual(value, s_SIMDZero);
    SIMDVector t0 = SIMDVectorDetail::VectorArcCoefficientApproximation(value);
    SIMDVector t1 = VectorSubtract(s_SIMDPi, t0);
    t0 = VectorSelect(t1, t0, nonnegative);
    return VectorSubtract(s_SIMDHalfPi, t0);
}

NWB_INLINE SIMDVector SIMDCALL VectorACos(SIMDVector value)noexcept{
    const SIMDVector nonnegative = VectorGreaterOrEqual(value, s_SIMDZero);
    const SIMDVector t0 = SIMDVectorDetail::VectorArcCoefficientApproximation(value);
    SIMDVector t1 = VectorSubtract(s_SIMDPi, t0);
    return VectorSelect(t1, t0, nonnegative);
}

NWB_INLINE SIMDVector SIMDCALL VectorATan(SIMDVector value)noexcept{
    SIMDVector absV = VectorAbs(value);
    SIMDVector invV = VectorReciprocal(value);
    SIMDVector comp = VectorGreater(value, s_SIMDOne);
    SIMDVector sign = VectorSelect(s_SIMDNegativeOne, s_SIMDOne, comp);
    comp = VectorLessOrEqual(absV, s_SIMDOne);
    sign = VectorSelect(sign, s_SIMDZero, comp);
    SIMDVector x = VectorSelect(invV, value, comp);

    SIMDVector x2 = VectorMultiply(x, x);
    SIMDVector result = VectorMultiplyAdd(VectorSplatW(s_SIMDATanCoefficients1), x2, VectorSplatZ(s_SIMDATanCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDATanCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDATanCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatW(s_SIMDATanCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatZ(s_SIMDATanCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDATanCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDATanCoefficients0));
    result = VectorMultiplyAdd(result, x2, s_SIMDOne);
    result = VectorMultiply(result, x);

    SIMDVector result1 = VectorSubtract(VectorMultiply(sign, s_SIMDHalfPi), result);
    comp = VectorEqual(sign, s_SIMDZero);
    return VectorSelect(result1, result, comp);
}

NWB_INLINE SIMDVector SIMDCALL VectorATan2(SIMDVector y, SIMDVector x)noexcept{
    const SIMDVector constants = VectorSet(s_PI, s_PIDIV2, s_PIDIV4, s_PI * 0.75f);
    SIMDVector v = VectorDivide(y, x);
    return SIMDVectorDetail::VectorATan2SelectResult(y, x, VectorATan(v), constants);
}

NWB_INLINE SIMDVector SIMDCALL VectorSinEst(SIMDVector value)noexcept{
    SIMDVector cosSignSelect;
    SIMDVector x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);

    SIMDVector x2 = VectorMultiply(x, x);
    return VectorMultiply(SIMDVectorDetail::VectorSinEstPolynomial(x2), x);
}

NWB_INLINE SIMDVector SIMDCALL VectorCosEst(SIMDVector value)noexcept{
    SIMDVector cosSignSelect;
    SIMDVector x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVector sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVector x2 = VectorMultiply(x, x);
    return VectorMultiply(SIMDVectorDetail::VectorCosEstPolynomial(x2), sign);
}

NWB_INLINE void SIMDCALL VectorSinCosEst(SIMDVector& outSin, SIMDVector& outCos, SIMDVector value)noexcept{
    SIMDVector cosSignSelect;
    SIMDVector x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVector sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVector x2 = VectorMultiply(x, x);

    outSin = VectorMultiply(SIMDVectorDetail::VectorSinEstPolynomial(x2), x);
    outCos = VectorMultiply(SIMDVectorDetail::VectorCosEstPolynomial(x2), sign);
}

NWB_INLINE SIMDVector SIMDCALL VectorTanEst(SIMDVector value)noexcept{
    SIMDVector v1 = VectorMultiply(value, VectorSplatW(s_SIMDTanEstCoefficients));
    v1 = VectorRound(v1);
    v1 = VectorNegativeMultiplySubtract(s_SIMDPi, v1, value);

    SIMDVector t0 = VectorSplatX(s_SIMDTanEstCoefficients);
    SIMDVector t1 = VectorSplatY(s_SIMDTanEstCoefficients);
    SIMDVector t2 = VectorSplatZ(s_SIMDTanEstCoefficients);

    SIMDVector v2t2 = VectorNegativeMultiplySubtract(v1, v1, t2);
    SIMDVector v2 = VectorMultiply(v1, v1);
    SIMDVector v1t0 = VectorMultiply(v1, t0);
    SIMDVector v1t1 = VectorMultiply(v1, t1);

    SIMDVector d = VectorReciprocalEst(v2t2);
    SIMDVector n = VectorMultiplyAdd(v2, v1t1, v1t0);
    return VectorMultiply(n, d);
}

NWB_INLINE SIMDVector SIMDCALL VectorASinEst(SIMDVector value)noexcept{
    SIMDVector nonnegative = VectorGreaterOrEqual(value, s_SIMDZero);
    SIMDVector x = VectorAbs(value);
    SIMDVector root = VectorSqrt(VectorMax(s_SIMDZero, VectorSubtract(s_SIMDOne, x)));

    SIMDVector t0 = SIMDVectorDetail::VectorArcEstPolynomial(x, root);
    SIMDVector t1 = VectorSubtract(s_SIMDPi, t0);
    t0 = VectorSelect(t1, t0, nonnegative);
    return VectorSubtract(s_SIMDHalfPi, t0);
}

NWB_INLINE SIMDVector SIMDCALL VectorACosEst(SIMDVector value)noexcept{
    SIMDVector nonnegative = VectorGreaterOrEqual(value, s_SIMDZero);
    SIMDVector x = VectorAbs(value);
    SIMDVector root = VectorSqrt(VectorMax(s_SIMDZero, VectorSubtract(s_SIMDOne, x)));

    SIMDVector t0 = SIMDVectorDetail::VectorArcEstPolynomial(x, root);
    SIMDVector t1 = VectorSubtract(s_SIMDPi, t0);
    return VectorSelect(t1, t0, nonnegative);
}

NWB_INLINE SIMDVector SIMDCALL VectorATanEst(SIMDVector value)noexcept{
    SIMDVector absV = VectorAbs(value);
    SIMDVector invV = VectorReciprocalEst(value);
    SIMDVector comp = VectorGreater(value, s_SIMDOne);
    SIMDVector sign = VectorSelect(s_SIMDNegativeOne, s_SIMDOne, comp);
    comp = VectorLessOrEqual(absV, s_SIMDOne);
    sign = VectorSelect(sign, s_SIMDZero, comp);
    SIMDVector x = VectorSelect(invV, value, comp);

    SIMDVector x2 = VectorMultiply(x, x);
    SIMDVector result = VectorMultiplyAdd(VectorSplatW(s_SIMDATanEstCoefficients1), x2, VectorSplatZ(s_SIMDATanEstCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDATanEstCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDATanEstCoefficients1));
    result = VectorMultiplyAdd(result, x2, s_SIMDATanEstCoefficients0);
    result = VectorMultiply(result, x);

    SIMDVector result1 = VectorSubtract(VectorMultiply(sign, s_SIMDHalfPi), result);
    comp = VectorEqual(sign, s_SIMDZero);
    return VectorSelect(result1, result, comp);
}

NWB_INLINE SIMDVector SIMDCALL VectorATan2Est(SIMDVector y, SIMDVector x)noexcept{
    const SIMDVector constants = VectorSet(s_PI, s_PIDIV2, s_PIDIV4, 2.3561944905f);
    SIMDVector v = VectorMultiply(y, VectorReciprocalEst(x));
    return SIMDVectorDetail::VectorATan2SelectResult(y, x, VectorATanEst(v), constants);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

