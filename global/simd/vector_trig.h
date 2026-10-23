// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_exp_log.h"
#include "vector_reciprocal.h"
#include "vector_rounding.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorTrigCanonicalAngle(SIMDVectorSingle value, SIMDVectorSingle& outCosSignSelect)noexcept{
    SIMDVectorSingle x = VectorModAngles(value);

    const SIMDVectorSingle sign = VectorAndInt(x, s_SIMDNegativeZero);
    const SIMDVectorSingle c = VectorOrInt(s_SIMDPi, sign);
    const SIMDVectorSingle absX = VectorAbs(x);
    const SIMDVectorSingle rflX = VectorSubtract(c, x);
    outCosSignSelect = VectorLessOrEqual(absX, s_SIMDHalfPi);
    return VectorSelect(rflX, x, outCosSignSelect);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorTrigCosSign(SIMDVectorSingle cosSignSelect)noexcept{
    return VectorSelect(s_SIMDNegativeOne, s_SIMDOne, cosSignSelect);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSinPolynomial(SIMDVectorSingle x2)noexcept{
    SIMDVectorSingle result = VectorMultiplyAdd(VectorSplatX(s_SIMDSinCoefficients1), x2, VectorSplatW(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatZ(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDSinCoefficients0));
    return VectorMultiplyAdd(result, x2, s_SIMDOne);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorCosPolynomial(SIMDVectorSingle x2)noexcept{
    SIMDVectorSingle result = VectorMultiplyAdd(VectorSplatX(s_SIMDCosCoefficients1), x2, VectorSplatW(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatZ(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDCosCoefficients0));
    return VectorMultiplyAdd(result, x2, s_SIMDOne);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSinEstPolynomial(SIMDVectorSingle x2)noexcept{
    SIMDVectorSingle result = VectorMultiplyAdd(VectorSplatW(s_SIMDSinCoefficients1), x2, VectorSplatZ(s_SIMDSinCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDSinCoefficients1));
    return VectorMultiplyAdd(result, x2, s_SIMDOne);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorCosEstPolynomial(SIMDVectorSingle x2)noexcept{
    SIMDVectorSingle result = VectorMultiplyAdd(VectorSplatW(s_SIMDCosCoefficients1), x2, VectorSplatZ(s_SIMDCosCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDCosCoefficients1));
    return VectorMultiplyAdd(result, x2, s_SIMDOne);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorArcEstPolynomial(SIMDVectorSingle x, SIMDVectorSingle root)noexcept{
    SIMDVectorSingle result = VectorMultiplyAdd(VectorSplatW(s_SIMDArcEstCoefficients), x, VectorSplatZ(s_SIMDArcEstCoefficients));
    result = VectorMultiplyAdd(result, x, VectorSplatY(s_SIMDArcEstCoefficients));
    result = VectorMultiplyAdd(result, x, VectorSplatX(s_SIMDArcEstCoefficients));
    return VectorMultiply(result, root);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL SinReplicated(SIMDVectorDouble value)noexcept{
    return VectorDoubleReplicate(Sin(VectorDoubleGetX(value)));
}

// Uniform consumers evaluate each binary64 primitive once before broadcasting its result.
NWB_INLINE void NWB_SIMD_CALL SinCosReplicated(SIMDVectorDouble& outSin, SIMDVectorDouble& outCos, SIMDVectorDouble value)noexcept{
    outSin = VectorDoubleReplicate(Sin(VectorDoubleGetX(value)));
    outCos = VectorDoubleReplicate(Cos(VectorDoubleGetX(value)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSin(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle cosSignSelect;
    SIMDVectorSingle x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);

    SIMDVectorSingle x2 = VectorMultiply(x, x);
    return VectorMultiply(SIMDVectorDetail::VectorSinPolynomial(x2), x);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSin(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(Sin(value[0]), Sin(value[1]), Sin(value[2]), Sin(value[3]));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSinEst(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle cosSignSelect;
    SIMDVectorSingle x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);

    SIMDVectorSingle x2 = VectorMultiply(x, x);
    return VectorMultiply(SIMDVectorDetail::VectorSinEstPolynomial(x2), x);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSinEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleSin(value);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorCos(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle cosSignSelect;
    SIMDVectorSingle x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVectorSingle sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVectorSingle x2 = VectorMultiply(x, x);
    return VectorMultiply(SIMDVectorDetail::VectorCosPolynomial(x2), sign);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleCos(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(Cos(value[0]), Cos(value[1]), Cos(value[2]), Cos(value[3]));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorCosEst(SIMDVectorSingle value)noexcept{
    SIMDVectorSingle cosSignSelect;
    SIMDVectorSingle x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVectorSingle sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVectorSingle x2 = VectorMultiply(x, x);
    return VectorMultiply(SIMDVectorDetail::VectorCosEstPolynomial(x2), sign);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleCosEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleCos(value);
}

NWB_INLINE void NWB_SIMD_CALL VectorSinCos(SIMDVectorSingle& outSin, SIMDVectorSingle& outCos, SIMDVectorSingle value)noexcept{
    SIMDVectorSingle cosSignSelect;
    SIMDVectorSingle x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVectorSingle sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVectorSingle x2 = VectorMultiply(x, x);

    outSin = VectorMultiply(SIMDVectorDetail::VectorSinPolynomial(x2), x);
    outCos = VectorMultiply(SIMDVectorDetail::VectorCosPolynomial(x2), sign);
}

NWB_INLINE void NWB_SIMD_CALL VectorDoubleSinCos(SIMDVectorDouble& outSin, SIMDVectorDouble& outCos, SIMDVectorDouble value)noexcept{
    outSin = VectorDoubleSin(value);
    outCos = VectorDoubleCos(value);
}

NWB_INLINE void NWB_SIMD_CALL VectorSinCosEst(SIMDVectorSingle& outSin, SIMDVectorSingle& outCos, SIMDVectorSingle value)noexcept{
    SIMDVectorSingle cosSignSelect;
    SIMDVectorSingle x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVectorSingle sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVectorSingle x2 = VectorMultiply(x, x);

    outSin = VectorMultiply(SIMDVectorDetail::VectorSinEstPolynomial(x2), x);
    outCos = VectorMultiply(SIMDVectorDetail::VectorCosEstPolynomial(x2), sign);
}

NWB_INLINE void NWB_SIMD_CALL VectorDoubleSinCosEst(SIMDVectorDouble& outSin, SIMDVectorDouble& outCos, SIMDVectorDouble value)noexcept{
    outSin = VectorDoubleSin(value);
    outCos = VectorDoubleCos(value);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorTan(SIMDVectorSingle value)noexcept{
    static const SIMDVectorConstF s_TanCoefficients0 = { { { 1.0f, -4.667168334e-1f, 2.566383229e-2f, -3.118153191e-4f } } };
    static const SIMDVectorConstF s_TanCoefficients1 = { { { 4.981943399e-7f, -1.333835001e-1f, 3.424887824e-3f, -1.786170734e-5f } } };
    static const SIMDVectorConstF s_TanConstants = { { { 1.570796371f, 6.077100628e-11f, 0.000244140625f, 0.63661977228f } } };
    static const SIMDVectorConstU s_Mask = { { { 0x1u, 0x1u, 0x1u, 0x1u } } };

    SIMDVectorSingle twoDivPi = VectorSplatW(s_TanConstants);
    SIMDVectorSingle zero = VectorZero();

    SIMDVectorSingle c0 = VectorSplatX(s_TanConstants);
    SIMDVectorSingle c1 = VectorSplatY(s_TanConstants);
    SIMDVectorSingle epsilon = VectorSplatZ(s_TanConstants);

    SIMDVectorSingle va = VectorMultiply(value, twoDivPi);
    va = VectorRound(va);

    SIMDVectorSingle vc = VectorNegativeMultiplySubtract(va, c0, value);
    SIMDVectorSingle vb = VectorAbs(va);
    vc = VectorNegativeMultiplySubtract(va, c1, vc);

#if defined(NWB_HAS_SSE2)
    vb = _mm_castsi128_ps(_mm_cvttps_epi32(vb));
#elif defined(NWB_HAS_NEON)
    vb = vreinterpretq_f32_u32(vcvtq_u32_f32(vb));
#else
    vb = VectorSetInt(static_cast<u32>(VectorGetX(vb)), static_cast<u32>(VectorGetY(vb)), static_cast<u32>(VectorGetZ(vb)), static_cast<u32>(VectorGetW(vb)));
#endif

    SIMDVectorSingle vc2 = VectorMultiply(vc, vc);

    SIMDVectorSingle t7 = VectorSplatW(s_TanCoefficients1);
    SIMDVectorSingle t6 = VectorSplatZ(s_TanCoefficients1);
    SIMDVectorSingle t4 = VectorSplatX(s_TanCoefficients1);
    SIMDVectorSingle t3 = VectorSplatW(s_TanCoefficients0);
    SIMDVectorSingle t5 = VectorSplatY(s_TanCoefficients1);
    SIMDVectorSingle t2 = VectorSplatZ(s_TanCoefficients0);
    SIMDVectorSingle t1 = VectorSplatY(s_TanCoefficients0);
    SIMDVectorSingle t0 = VectorSplatX(s_TanCoefficients0);

    SIMDVectorSingle vbIsEven = VectorAndInt(vb, s_Mask);
    vbIsEven = VectorEqualInt(vbIsEven, zero);

    SIMDVectorSingle n = VectorMultiplyAdd(vc2, t7, t6);
    SIMDVectorSingle d = VectorMultiplyAdd(vc2, t4, t3);
    n = VectorMultiplyAdd(vc2, n, t5);
    d = VectorMultiplyAdd(vc2, d, t2);
    n = VectorMultiply(vc2, n);
    d = VectorMultiplyAdd(vc2, d, t1);
    n = VectorMultiplyAdd(vc, n, vc);
    SIMDVectorSingle vcNearZero = VectorInBounds(vc, epsilon);
    d = VectorMultiplyAdd(vc2, d, t0);

    n = VectorSelect(n, vc, vcNearZero);
    d = VectorSelect(d, s_SIMDOne, vcNearZero);

    SIMDVectorSingle r0 = VectorNegate(n);
    SIMDVectorSingle r1 = VectorDivide(n, d);
    r0 = VectorDivide(d, r0);

    SIMDVectorSingle valueIsZero = VectorEqual(value, zero);
    SIMDVectorSingle result = VectorSelect(r0, r1, vbIsEven);
    return VectorSelect(result, zero, valueIsZero);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleTan(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(Tan(value[0]), Tan(value[1]), Tan(value[2]), Tan(value[3]));
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

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleTanEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleTan(value);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSinH(SIMDVectorSingle value)noexcept{
    const SIMDVectorSingle e1 = VectorExp(value);
    const SIMDVectorSingle e2 = VectorExp(VectorNegate(value));
    return VectorMultiply(VectorSubtract(e1, e2), s_SIMDOneHalf);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSinH(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(SinH(value[0]), SinH(value[1]), SinH(value[2]), SinH(value[3]));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorCosH(SIMDVectorSingle value)noexcept{
    const SIMDVectorSingle e1 = VectorExp(value);
    const SIMDVectorSingle e2 = VectorExp(VectorNegate(value));
    return VectorMultiply(VectorAdd(e1, e2), s_SIMDOneHalf);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleCosH(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(CosH(value[0]), CosH(value[1]), CosH(value[2]), CosH(value[3]));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorTanH(SIMDVectorSingle value)noexcept{
    const SIMDVectorSingle sign = VectorAndInt(value, s_SIMDNegativeZero);
    const SIMDVectorSingle absValue = VectorAbs(value);
    const SIMDVectorSingle e = VectorExp(VectorNegate(VectorAdd(absValue, absValue)));
    const SIMDVectorSingle magnitude = VectorDivide(VectorSubtract(s_SIMDOne, e), VectorAdd(s_SIMDOne, e));
    return VectorOrInt(magnitude, sign);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleTanH(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(TanH(value[0]), TanH(value[1]), TanH(value[2]), TanH(value[3]));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

