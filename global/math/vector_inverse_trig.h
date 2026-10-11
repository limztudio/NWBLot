// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_trig.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorArcCoefficientApproximation(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle x = VectorAbs(value);
    SIMDVectorSingle root = VectorSqrt(VectorMax(s_SIMDZero, VectorSubtract(s_SIMDOne, x)));

    SIMDVectorSingle result = VectorMultiplyAdd(VectorSplatW(s_SIMDArcCoefficients1), x, VectorSplatZ(s_SIMDArcCoefficients1));
    result = VectorMultiplyAdd(result, x, VectorSplatY(s_SIMDArcCoefficients1));
    result = VectorMultiplyAdd(result, x, VectorSplatX(s_SIMDArcCoefficients1));
    result = VectorMultiplyAdd(result, x, VectorSplatW(s_SIMDArcCoefficients0));
    result = VectorMultiplyAdd(result, x, VectorSplatZ(s_SIMDArcCoefficients0));
    result = VectorMultiplyAdd(result, x, VectorSplatY(s_SIMDArcCoefficients0));
    result = VectorMultiplyAdd(result, x, VectorSplatX(s_SIMDArcCoefficients0));
    return VectorMultiply(result, root);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorATan2SelectResult(SIMDVectorSingle y, SIMDVectorSingle x, SIMDVectorSingle atanResult, SIMDVectorSingle constants)noexcept{
    const SIMDVectorSingle zero = VectorZero();
    SIMDVectorSingle atanResultValid = VectorTrueInt();

    SIMDVectorSingle pi = VectorSplatX(constants);
    SIMDVectorSingle piOverTwo = VectorSplatY(constants);
    SIMDVectorSingle piOverFour = VectorSplatZ(constants);
    SIMDVectorSingle threePiOverFour = VectorSplatW(constants);

    SIMDVectorSingle yEqualsZero = VectorEqual(y, zero);
    SIMDVectorSingle xEqualsZero = VectorEqual(x, zero);
    SIMDVectorSingle xIsPositive = VectorEqualInt(VectorAndInt(x, s_SIMDNegativeZero), zero);
    SIMDVectorSingle yEqualsInfinity = VectorIsInfinite(y);
    SIMDVectorSingle xEqualsInfinity = VectorIsInfinite(x);

    SIMDVectorSingle ySign = VectorAndInt(y, s_SIMDNegativeZero);
    pi = VectorOrInt(pi, ySign);
    piOverTwo = VectorOrInt(piOverTwo, ySign);
    piOverFour = VectorOrInt(piOverFour, ySign);
    threePiOverFour = VectorOrInt(threePiOverFour, ySign);

    SIMDVectorSingle r1 = VectorSelect(pi, ySign, xIsPositive);
    SIMDVectorSingle r2 = VectorSelect(atanResultValid, piOverTwo, xEqualsZero);
    SIMDVectorSingle r3 = VectorSelect(r2, r1, yEqualsZero);
    SIMDVectorSingle r4 = VectorSelect(threePiOverFour, piOverFour, xIsPositive);
    SIMDVectorSingle r5 = VectorSelect(piOverTwo, r4, xEqualsInfinity);
    SIMDVectorSingle result = VectorSelect(r3, r5, yEqualsInfinity);
    atanResultValid = VectorEqualInt(result, atanResultValid);

    r1 = VectorSelect(pi, s_SIMDNegativeZero, xIsPositive);
    r2 = VectorAdd(atanResult, r1);
    return VectorSelect(result, r2, atanResultValid);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorASin(SIMDVectorSingle value)noexcept{
    const SIMDVectorSingle nonnegative = VectorGreaterOrEqual(value, s_SIMDZero);
    SIMDVectorSingle t0 = SIMDVectorDetail::VectorArcCoefficientApproximation(value);
    SIMDVectorSingle t1 = VectorSubtract(s_SIMDPi, t0);
    t0 = VectorSelect(t1, t0, nonnegative);
    return VectorSubtract(s_SIMDHalfPi, t0);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleASin(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(ASin(value[0]), ASin(value[1]), ASin(value[2]), ASin(value[3]));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorACos(SIMDVectorSingle value)noexcept{
    const SIMDVectorSingle nonnegative = VectorGreaterOrEqual(value, s_SIMDZero);
    const SIMDVectorSingle t0 = SIMDVectorDetail::VectorArcCoefficientApproximation(value);
    SIMDVectorSingle t1 = VectorSubtract(s_SIMDPi, t0);
    return VectorSelect(t1, t0, nonnegative);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleACos(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(ACos(value[0]), ACos(value[1]), ACos(value[2]), ACos(value[3]));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorATan(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle absV = VectorAbs(value);
    SIMDVectorSingle invV = VectorReciprocal(value);
    SIMDVectorSingle comp = VectorGreater(value, s_SIMDOne);
    SIMDVectorSingle sign = VectorSelect(s_SIMDNegativeOne, s_SIMDOne, comp);
    comp = VectorLessOrEqual(absV, s_SIMDOne);
    sign = VectorSelect(sign, s_SIMDZero, comp);
    SIMDVectorSingle x = VectorSelect(invV, value, comp);

    SIMDVectorSingle x2 = VectorMultiply(x, x);
    SIMDVectorSingle result = VectorMultiplyAdd(VectorSplatW(s_SIMDATanCoefficients1), x2, VectorSplatZ(s_SIMDATanCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDATanCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDATanCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatW(s_SIMDATanCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatZ(s_SIMDATanCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDATanCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDATanCoefficients0));
    result = VectorMultiplyAdd(result, x2, s_SIMDOne);
    result = VectorMultiply(result, x);

    SIMDVectorSingle result1 = VectorSubtract(VectorMultiply(sign, s_SIMDHalfPi), result);
    comp = VectorEqual(sign, s_SIMDZero);
    return VectorSelect(result1, result, comp);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleATan(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(ATan(value[0]), ATan(value[1]), ATan(value[2]), ATan(value[3]));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorATan2(SIMDVectorSingle y, SIMDVectorSingle x)noexcept{
    const SIMDVectorSingle constants = VectorSet(s_PI, s_PIDIV2, s_PIDIV4, s_PI * 0.75f);
    SIMDVectorSingle v = VectorDivide(y, x);
    return SIMDVectorDetail::VectorATan2SelectResult(y, x, VectorATan(v), constants);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleATan2(SIMDVectorDouble y, SIMDVectorDouble x)noexcept{
    return VectorDoubleSet(ATan2(y[0], x[0]), ATan2(y[1], x[1]), ATan2(y[2], x[2]), ATan2(y[3], x[3]));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSinEst(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle cosSignSelect;
    SIMDVectorSingle x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);

    SIMDVectorSingle x2 = VectorMultiply(x, x);
    return VectorMultiply(SIMDVectorDetail::VectorSinEstPolynomial(x2), x);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorCosEst(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle cosSignSelect;
    SIMDVectorSingle x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVectorSingle sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVectorSingle x2 = VectorMultiply(x, x);
    return VectorMultiply(SIMDVectorDetail::VectorCosEstPolynomial(x2), sign);
}

NWB_INLINE void NWB_SIMD_CALL VectorSinCosEst(SIMDVectorSingle& outSin, SIMDVectorSingle& outCos, SIMDVectorSingle value)noexcept{
    SIMDVectorSingle cosSignSelect;
    SIMDVectorSingle x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVectorSingle sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVectorSingle x2 = VectorMultiply(x, x);

    outSin = VectorMultiply(SIMDVectorDetail::VectorSinEstPolynomial(x2), x);
    outCos = VectorMultiply(SIMDVectorDetail::VectorCosEstPolynomial(x2), sign);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorTanEst(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle v1 = VectorMultiply(value, VectorSplatW(s_SIMDTanEstCoefficients));
    v1 = VectorRound(v1);
    v1 = VectorNegativeMultiplySubtract(s_SIMDPi, v1, value);

    SIMDVectorSingle t0 = VectorSplatX(s_SIMDTanEstCoefficients);
    SIMDVectorSingle t1 = VectorSplatY(s_SIMDTanEstCoefficients);
    SIMDVectorSingle t2 = VectorSplatZ(s_SIMDTanEstCoefficients);

    SIMDVectorSingle v2t2 = VectorNegativeMultiplySubtract(v1, v1, t2);
    SIMDVectorSingle v2 = VectorMultiply(v1, v1);
    SIMDVectorSingle v1t0 = VectorMultiply(v1, t0);
    SIMDVectorSingle v1t1 = VectorMultiply(v1, t1);

    SIMDVectorSingle d = VectorReciprocalEst(v2t2);
    SIMDVectorSingle n = VectorMultiplyAdd(v2, v1t1, v1t0);
    return VectorMultiply(n, d);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorASinEst(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle nonnegative = VectorGreaterOrEqual(value, s_SIMDZero);
    SIMDVectorSingle x = VectorAbs(value);
    SIMDVectorSingle root = VectorSqrt(VectorMax(s_SIMDZero, VectorSubtract(s_SIMDOne, x)));

    SIMDVectorSingle t0 = SIMDVectorDetail::VectorArcEstPolynomial(x, root);
    SIMDVectorSingle t1 = VectorSubtract(s_SIMDPi, t0);
    t0 = VectorSelect(t1, t0, nonnegative);
    return VectorSubtract(s_SIMDHalfPi, t0);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleASinEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleASin(value);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorACosEst(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle nonnegative = VectorGreaterOrEqual(value, s_SIMDZero);
    SIMDVectorSingle x = VectorAbs(value);
    SIMDVectorSingle root = VectorSqrt(VectorMax(s_SIMDZero, VectorSubtract(s_SIMDOne, x)));

    SIMDVectorSingle t0 = SIMDVectorDetail::VectorArcEstPolynomial(x, root);
    SIMDVectorSingle t1 = VectorSubtract(s_SIMDPi, t0);
    return VectorSelect(t1, t0, nonnegative);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleACosEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleACos(value);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorATanEst(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle absV = VectorAbs(value);
    SIMDVectorSingle invV = VectorReciprocalEst(value);
    SIMDVectorSingle comp = VectorGreater(value, s_SIMDOne);
    SIMDVectorSingle sign = VectorSelect(s_SIMDNegativeOne, s_SIMDOne, comp);
    comp = VectorLessOrEqual(absV, s_SIMDOne);
    sign = VectorSelect(sign, s_SIMDZero, comp);
    SIMDVectorSingle x = VectorSelect(invV, value, comp);

    SIMDVectorSingle x2 = VectorMultiply(x, x);
    SIMDVectorSingle result = VectorMultiplyAdd(VectorSplatW(s_SIMDATanEstCoefficients1), x2, VectorSplatZ(s_SIMDATanEstCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDATanEstCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDATanEstCoefficients1));
    result = VectorMultiplyAdd(result, x2, s_SIMDATanEstCoefficients0);
    result = VectorMultiply(result, x);

    SIMDVectorSingle result1 = VectorSubtract(VectorMultiply(sign, s_SIMDHalfPi), result);
    comp = VectorEqual(sign, s_SIMDZero);
    return VectorSelect(result1, result, comp);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleATanEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleATan(value);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorATan2Est(SIMDVectorSingle y, SIMDVectorSingle x)noexcept{
    const SIMDVectorSingle constants = VectorSet(s_PI, s_PIDIV2, s_PIDIV4, 2.3561944905f);
    SIMDVectorSingle v = VectorMultiply(y, VectorReciprocalEst(x));
    return SIMDVectorDetail::VectorATan2SelectResult(y, x, VectorATanEst(v), constants);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleATan2Est(SIMDVectorDouble y, SIMDVectorDouble x)noexcept{
    return VectorDoubleSet(ATan2(y[0], x[0]), ATan2(y[1], x[1]), ATan2(y[2], x[2]), ATan2(y[3], x[3]));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

