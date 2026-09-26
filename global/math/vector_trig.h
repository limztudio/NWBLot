// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_exp_log.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL VectorTrigCanonicalAngle(SIMDVector value, SIMDVector& outCosSignSelect)noexcept{
    SIMDVector x = VectorModAngles(value);

    const SIMDVector sign = VectorAndInt(x, s_SIMDNegativeZero);
    const SIMDVector c = VectorOrInt(s_SIMDPi, sign);
    const SIMDVector absX = VectorAbs(x);
    const SIMDVector rflX = VectorSubtract(c, x);
    outCosSignSelect = VectorLessOrEqual(absX, s_SIMDHalfPi);
    return VectorSelect(rflX, x, outCosSignSelect);
}

NWB_INLINE SIMDVector SIMDCALL VectorTrigCosSign(SIMDVector cosSignSelect)noexcept{
    return VectorSelect(s_SIMDNegativeOne, s_SIMDOne, cosSignSelect);
}

NWB_INLINE SIMDVector SIMDCALL VectorSinPolynomial(SIMDVector x2)noexcept{
    SIMDVector result = VectorMultiplyAdd(VectorSplatX(s_SIMDSinCoefficients1), x2, VectorSplatW(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatZ(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDSinCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDSinCoefficients0));
    return VectorMultiplyAdd(result, x2, s_SIMDOne);
}

NWB_INLINE SIMDVector SIMDCALL VectorCosPolynomial(SIMDVector x2)noexcept{
    SIMDVector result = VectorMultiplyAdd(VectorSplatX(s_SIMDCosCoefficients1), x2, VectorSplatW(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatZ(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDCosCoefficients0));
    result = VectorMultiplyAdd(result, x2, VectorSplatX(s_SIMDCosCoefficients0));
    return VectorMultiplyAdd(result, x2, s_SIMDOne);
}

NWB_INLINE SIMDVector SIMDCALL VectorSinEstPolynomial(SIMDVector x2)noexcept{
    SIMDVector result = VectorMultiplyAdd(VectorSplatW(s_SIMDSinCoefficients1), x2, VectorSplatZ(s_SIMDSinCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDSinCoefficients1));
    return VectorMultiplyAdd(result, x2, s_SIMDOne);
}

NWB_INLINE SIMDVector SIMDCALL VectorCosEstPolynomial(SIMDVector x2)noexcept{
    SIMDVector result = VectorMultiplyAdd(VectorSplatW(s_SIMDCosCoefficients1), x2, VectorSplatZ(s_SIMDCosCoefficients1));
    result = VectorMultiplyAdd(result, x2, VectorSplatY(s_SIMDCosCoefficients1));
    return VectorMultiplyAdd(result, x2, s_SIMDOne);
}

NWB_INLINE SIMDVector SIMDCALL VectorArcEstPolynomial(SIMDVector x, SIMDVector root)noexcept{
    SIMDVector result = VectorMultiplyAdd(VectorSplatW(s_SIMDArcEstCoefficients), x, VectorSplatZ(s_SIMDArcEstCoefficients));
    result = VectorMultiplyAdd(result, x, VectorSplatY(s_SIMDArcEstCoefficients));
    result = VectorMultiplyAdd(result, x, VectorSplatX(s_SIMDArcEstCoefficients));
    return VectorMultiply(result, root);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL VectorSin(SIMDVector value)noexcept{
    SIMDVector cosSignSelect;
    SIMDVector x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);

    SIMDVector x2 = VectorMultiply(x, x);
    return VectorMultiply(SIMDVectorDetail::VectorSinPolynomial(x2), x);
}

NWB_INLINE SIMDVector SIMDCALL VectorCos(SIMDVector value)noexcept{
    SIMDVector cosSignSelect;
    SIMDVector x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVector sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVector x2 = VectorMultiply(x, x);
    return VectorMultiply(SIMDVectorDetail::VectorCosPolynomial(x2), sign);
}

NWB_INLINE void SIMDCALL VectorSinCos(SIMDVector& outSin, SIMDVector& outCos, SIMDVector value)noexcept{
    SIMDVector cosSignSelect;
    SIMDVector x = SIMDVectorDetail::VectorTrigCanonicalAngle(value, cosSignSelect);
    const SIMDVector sign = SIMDVectorDetail::VectorTrigCosSign(cosSignSelect);

    SIMDVector x2 = VectorMultiply(x, x);

    outSin = VectorMultiply(SIMDVectorDetail::VectorSinPolynomial(x2), x);
    outCos = VectorMultiply(SIMDVectorDetail::VectorCosPolynomial(x2), sign);
}

NWB_INLINE SIMDVector SIMDCALL VectorTan(SIMDVector value)noexcept{
    static const SIMDVectorConstF tanCoefficients0 = { { { 1.0f, -4.667168334e-1f, 2.566383229e-2f, -3.118153191e-4f } } };
    static const SIMDVectorConstF tanCoefficients1 = { { { 4.981943399e-7f, -1.333835001e-1f, 3.424887824e-3f, -1.786170734e-5f } } };
    static const SIMDVectorConstF tanConstants = { { { 1.570796371f, 6.077100628e-11f, 0.000244140625f, 0.63661977228f } } };
    static const SIMDVectorConstU mask = { { { 0x1u, 0x1u, 0x1u, 0x1u } } };

    SIMDVector twoDivPi = VectorSplatW(tanConstants);
    SIMDVector zero = VectorZero();

    SIMDVector c0 = VectorSplatX(tanConstants);
    SIMDVector c1 = VectorSplatY(tanConstants);
    SIMDVector epsilon = VectorSplatZ(tanConstants);

    SIMDVector va = VectorMultiply(value, twoDivPi);
    va = VectorRound(va);

    SIMDVector vc = VectorNegativeMultiplySubtract(va, c0, value);
    SIMDVector vb = VectorAbs(va);
    vc = VectorNegativeMultiplySubtract(va, c1, vc);

#if defined(NWB_HAS_SSE4)
    vb = _mm_castsi128_ps(_mm_cvttps_epi32(vb));
#elif defined(NWB_HAS_NEON)
    vb = vreinterpretq_f32_u32(vcvtq_u32_f32(vb));
#else
    vb = VectorSetInt(
        static_cast<u32>(VectorGetX(vb)),
        static_cast<u32>(VectorGetY(vb)),
        static_cast<u32>(VectorGetZ(vb)),
        static_cast<u32>(VectorGetW(vb))
    );
#endif

    SIMDVector vc2 = VectorMultiply(vc, vc);

    SIMDVector t7 = VectorSplatW(tanCoefficients1);
    SIMDVector t6 = VectorSplatZ(tanCoefficients1);
    SIMDVector t4 = VectorSplatX(tanCoefficients1);
    SIMDVector t3 = VectorSplatW(tanCoefficients0);
    SIMDVector t5 = VectorSplatY(tanCoefficients1);
    SIMDVector t2 = VectorSplatZ(tanCoefficients0);
    SIMDVector t1 = VectorSplatY(tanCoefficients0);
    SIMDVector t0 = VectorSplatX(tanCoefficients0);

    SIMDVector vbIsEven = VectorAndInt(vb, mask);
    vbIsEven = VectorEqualInt(vbIsEven, zero);

    SIMDVector n = VectorMultiplyAdd(vc2, t7, t6);
    SIMDVector d = VectorMultiplyAdd(vc2, t4, t3);
    n = VectorMultiplyAdd(vc2, n, t5);
    d = VectorMultiplyAdd(vc2, d, t2);
    n = VectorMultiply(vc2, n);
    d = VectorMultiplyAdd(vc2, d, t1);
    n = VectorMultiplyAdd(vc, n, vc);
    SIMDVector vcNearZero = VectorInBounds(vc, epsilon);
    d = VectorMultiplyAdd(vc2, d, t0);

    n = VectorSelect(n, vc, vcNearZero);
    d = VectorSelect(d, s_SIMDOne, vcNearZero);

    SIMDVector r0 = VectorNegate(n);
    SIMDVector r1 = VectorDivide(n, d);
    r0 = VectorDivide(d, r0);

    SIMDVector valueIsZero = VectorEqual(value, zero);
    SIMDVector result = VectorSelect(r0, r1, vbIsEven);
    return VectorSelect(result, zero, valueIsZero);
}

NWB_INLINE SIMDVector SIMDCALL VectorSinH(SIMDVector value)noexcept{
    const SIMDVector e1 = VectorExp(value);
    const SIMDVector e2 = VectorExp(VectorNegate(value));
    return VectorMultiply(VectorSubtract(e1, e2), s_SIMDOneHalf);
}

NWB_INLINE SIMDVector SIMDCALL VectorCosH(SIMDVector value)noexcept{
    const SIMDVector e1 = VectorExp(value);
    const SIMDVector e2 = VectorExp(VectorNegate(value));
    return VectorMultiply(VectorAdd(e1, e2), s_SIMDOneHalf);
}

NWB_INLINE SIMDVector SIMDCALL VectorTanH(SIMDVector value)noexcept{
    const SIMDVector sign = VectorAndInt(value, s_SIMDNegativeZero);
    const SIMDVector absValue = VectorAbs(value);
    const SIMDVector e = VectorExp(VectorNegate(VectorAdd(absValue, absValue)));
    const SIMDVector magnitude = VectorDivide(VectorSubtract(s_SIMDOne, e), VectorAdd(s_SIMDOne, e));
    return VectorOrInt(magnitude, sign);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

