// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "quaternion_arithmetic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDQuaternionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorConstU s_SIMDQuaternionSignMask2 = { { { 0x80000000, 0x00000000, 0x00000000, 0x00000000 } } };
inline constexpr f32 s_QuaternionBarycentricVectorEpsilon = 1.0f / 65536.0f;
inline constexpr f32 s_QuaternionSquadControlScale = -0.25f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDQuaternionDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f64 s_DoubleQuaternionBarycentricVectorEpsilon = 1.0e-12;
inline constexpr f64 s_DoubleQuaternionSquadControlScale = -0.25;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionSlerpV(SIMDVectorSingle q0, SIMDVectorSingle q1, SIMDVectorSingle t)noexcept{
    NWB_ASSERT((VectorGetY(t) == VectorGetX(t)) && (VectorGetZ(t) == VectorGetX(t)) && (VectorGetW(t) == VectorGetX(t)));

#if defined(NWB_HAS_SCALAR) || defined(NWB_HAS_NEON)
    const SIMDVectorSingle oneMinusEpsilon = VectorReplicate(1.0f - SIMDQuaternionDetail::s_QuaternionInterpolationEpsilon);
    SIMDVectorSingle cosOmega = QuaternionDot(q0, q1);
    SIMDVectorSingle control = VectorLess(cosOmega, VectorZero());
    SIMDVectorSingle sign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, control);

    cosOmega = VectorMultiply(cosOmega, sign);
    control = VectorLess(cosOmega, oneMinusEpsilon);

    SIMDVectorSingle sinOmega = VectorNegativeMultiplySubtract(cosOmega, cosOmega, s_SIMDOne);
    sinOmega = VectorSqrt(sinOmega);

    const SIMDVectorSingle omega = VectorATan2(sinOmega, cosOmega);
    SIMDVectorSingle signMask = VectorSplatSignMask();
    SIMDVectorSingle v01 = VectorShiftLeft(t, VectorZero(), 2);
    signMask = VectorShiftLeft(signMask, VectorZero(), 3);
    v01 = VectorXorInt(v01, signMask);
    v01 = VectorAdd(s_SIMDIdentityR0, v01);

    SIMDVectorSingle s0 = VectorMultiply(v01, omega);
    s0 = VectorSin(s0);
    s0 = VectorMultiply(s0, VectorReciprocal(sinOmega));
    s0 = VectorSelect(v01, s0, control);

    SIMDVectorSingle s1 = VectorSplatY(s0);
    s0 = VectorSplatX(s0);
    s1 = VectorMultiply(s1, sign);

    SIMDVectorSingle result = VectorMultiply(q0, s0);
    return VectorMultiplyAdd(q1, s1, result);
#else
    const SIMDVectorSingle oneMinusEpsilon = VectorReplicate(1.0f - SIMDQuaternionDetail::s_QuaternionInterpolationEpsilon);
    SIMDVectorSingle cosOmega = QuaternionDot(q0, q1);
    SIMDVectorSingle control = _mm_cmplt_ps(cosOmega, s_SIMDZero);
    SIMDVectorSingle sign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, control);

    cosOmega = _mm_mul_ps(cosOmega, sign);
    control = _mm_cmplt_ps(cosOmega, oneMinusEpsilon);

    SIMDVectorSingle sinOmega = _mm_mul_ps(cosOmega, cosOmega);
    sinOmega = _mm_sub_ps(s_SIMDOne, sinOmega);
    sinOmega = _mm_sqrt_ps(sinOmega);

    const SIMDVectorSingle omega = VectorATan2(sinOmega, cosOmega);
    SIMDVectorSingle v01{};
#if defined(NWB_HAS_AVX)
    v01 = _mm_permute_ps(t, _MM_SHUFFLE(2, 3, 0, 1));
#else
    v01 = _mm_shuffle_ps(t, t, _MM_SHUFFLE(2, 3, 0, 1));
#endif
    v01 = _mm_and_ps(v01, s_SIMDMaskXY);
    v01 = _mm_xor_ps(v01, SIMDQuaternionDetail::s_SIMDQuaternionSignMask2);
    v01 = _mm_add_ps(s_SIMDIdentityR0, v01);

    SIMDVectorSingle s0 = _mm_mul_ps(v01, omega);
    s0 = VectorSin(s0);
    s0 = _mm_div_ps(s0, sinOmega);
    s0 = VectorSelect(v01, s0, control);

    SIMDVectorSingle s1{};
#if defined(NWB_HAS_AVX)
    s1 = _mm_permute_ps(s0, _MM_SHUFFLE(1, 1, 1, 1));
#if defined(NWB_HAS_AVX2)
    s0 = _mm_broadcastss_ps(s0);
#else
    s0 = _mm_permute_ps(s0, _MM_SHUFFLE(0, 0, 0, 0));
#endif
#else
    s1 = _mm_shuffle_ps(s0, s0, _MM_SHUFFLE(1, 1, 1, 1));
    s0 = _mm_shuffle_ps(s0, s0, _MM_SHUFFLE(0, 0, 0, 0));
#endif

    s1 = _mm_mul_ps(s1, sign);
    SIMDVectorSingle result = _mm_mul_ps(q0, s0);
    return VectorMultiplyAdd(q1, s1, result);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleSlerpV(SIMDVectorDouble q0, SIMDVectorDouble q1, SIMDVectorDouble t)noexcept{
    NWB_ASSERT((VectorDoubleGetY(t) == VectorDoubleGetX(t)) && (VectorDoubleGetZ(t) == VectorDoubleGetX(t)) && (VectorDoubleGetW(t) == VectorDoubleGetX(t)));

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

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionSlerp(SIMDVectorSingle q0, SIMDVectorSingle q1, f32 t)noexcept{
    return QuaternionSlerpV(q0, q1, VectorReplicate(t));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleSlerp(SIMDVectorDouble q0, SIMDVectorDouble q1, f64 t)noexcept{
    return QuaternionDoubleSlerpV(q0, q1, VectorDoubleReplicate(t));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionSquadV(SIMDVectorSingle q0, SIMDVectorSingle q1, SIMDVectorSingle q2, SIMDVectorSingle q3, SIMDVectorSingle t)noexcept{
    NWB_ASSERT((VectorGetY(t) == VectorGetX(t)) && (VectorGetZ(t) == VectorGetX(t)) && (VectorGetW(t) == VectorGetX(t)));

    const SIMDVectorSingle q03 = QuaternionSlerpV(q0, q3, t);
    const SIMDVectorSingle q12 = QuaternionSlerpV(q1, q2, t);
    SIMDVectorSingle tp = VectorNegativeMultiplySubtract(t, t, t);
    tp = VectorAdd(tp, tp);
    return QuaternionSlerpV(q03, q12, tp);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleSquadV(SIMDVectorDouble q0, SIMDVectorDouble q1, SIMDVectorDouble q2, SIMDVectorDouble q3, SIMDVectorDouble t)noexcept{
    NWB_ASSERT((VectorDoubleGetY(t) == VectorDoubleGetX(t)) && (VectorDoubleGetZ(t) == VectorDoubleGetX(t)) && (VectorDoubleGetW(t) == VectorDoubleGetX(t)));

    const SIMDVectorDouble q03 = QuaternionDoubleSlerpV(q0, q3, t);
    const SIMDVectorDouble q12 = QuaternionDoubleSlerpV(q1, q2, t);
    SIMDVectorDouble tp = VectorDoubleNegativeMultiplySubtract(t, t, t);
    tp = VectorDoubleAdd(tp, tp);
    return QuaternionDoubleSlerpV(q03, q12, tp);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionSquad(SIMDVectorSingle q0, SIMDVectorSingle q1, SIMDVectorSingle q2, SIMDVectorSingle q3, f32 t)noexcept{
    return QuaternionSquadV(q0, q1, q2, q3, VectorReplicate(t));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleSquad(SIMDVectorDouble q0, SIMDVectorDouble q1, SIMDVectorDouble q2, SIMDVectorDouble q3, f64 t)noexcept{
    return QuaternionDoubleSquadV(q0, q1, q2, q3, VectorDoubleReplicate(t));
}

NWB_INLINE void NWB_SIMD_CALL QuaternionSquadSetup(SIMDVectorSingle& outA, SIMDVectorSingle& outB, SIMDVectorSingle& outC, SIMDVectorSingle q0, SIMDVectorSingle q1, SIMDVectorSingle q2, SIMDVectorSingle q3)noexcept{

    const SIMDVectorSingle lengthSq12 = QuaternionLengthSq(VectorAdd(q1, q2));
    const SIMDVectorSingle lengthDelta12 = QuaternionLengthSq(VectorSubtract(q1, q2));
    SIMDVectorSingle sq2 = VectorNegate(q2);
    sq2 = VectorSelect(q2, sq2, VectorLess(lengthSq12, lengthDelta12));

    const SIMDVectorSingle lengthSq01 = QuaternionLengthSq(VectorAdd(q0, q1));
    const SIMDVectorSingle lengthDelta01 = QuaternionLengthSq(VectorSubtract(q0, q1));
    SIMDVectorSingle sq0 = VectorNegate(q0);

    const SIMDVectorSingle lengthSq23 = QuaternionLengthSq(VectorAdd(sq2, q3));
    const SIMDVectorSingle lengthDelta23 = QuaternionLengthSq(VectorSubtract(sq2, q3));
    SIMDVectorSingle sq3 = VectorNegate(q3);

    sq0 = VectorSelect(q0, sq0, VectorLess(lengthSq01, lengthDelta01));
    sq3 = VectorSelect(q3, sq3, VectorLess(lengthSq23, lengthDelta23));

    const SIMDVectorSingle invQ1 = QuaternionInverse(q1);
    const SIMDVectorSingle invQ2 = QuaternionInverse(sq2);
    const SIMDVectorSingle lnQ0 = QuaternionLn(QuaternionMultiply(invQ1, sq0));
    const SIMDVectorSingle lnQ2 = QuaternionLn(QuaternionMultiply(invQ1, sq2));
    const SIMDVectorSingle lnQ1 = QuaternionLn(QuaternionMultiply(invQ2, q1));
    const SIMDVectorSingle lnQ3 = QuaternionLn(QuaternionMultiply(invQ2, sq3));

    SIMDVectorSingle expQ02 = VectorScale(VectorAdd(lnQ0, lnQ2), SIMDQuaternionDetail::s_QuaternionSquadControlScale);
    SIMDVectorSingle expQ13 = VectorScale(VectorAdd(lnQ1, lnQ3), SIMDQuaternionDetail::s_QuaternionSquadControlScale);
    expQ02 = QuaternionExp(expQ02);
    expQ13 = QuaternionExp(expQ13);

    outA = QuaternionMultiply(q1, expQ02);
    outB = QuaternionMultiply(sq2, expQ13);
    outC = sq2;
}

NWB_INLINE void NWB_SIMD_CALL QuaternionDoubleSquadSetup(SIMDVectorDouble& outA, SIMDVectorDouble& outB, SIMDVectorDouble& outC, SIMDVectorDouble q0, SIMDVectorDouble q1, SIMDVectorDouble q2, SIMDVectorDouble q3)noexcept{

    const SIMDVectorDouble lengthSq12 = QuaternionDoubleLengthSq(VectorDoubleAdd(q1, q2));
    const SIMDVectorDouble lengthDelta12 = QuaternionDoubleLengthSq(VectorDoubleSubtract(q1, q2));
    SIMDVectorDouble sq2 = VectorDoubleNegate(q2);
    sq2 = VectorDoubleSelect(q2, sq2, VectorDoubleLess(lengthSq12, lengthDelta12));

    const SIMDVectorDouble lengthSq01 = QuaternionDoubleLengthSq(VectorDoubleAdd(q0, q1));
    const SIMDVectorDouble lengthDelta01 = QuaternionDoubleLengthSq(VectorDoubleSubtract(q0, q1));
    SIMDVectorDouble sq0 = VectorDoubleNegate(q0);

    const SIMDVectorDouble lengthSq23 = QuaternionDoubleLengthSq(VectorDoubleAdd(sq2, q3));
    const SIMDVectorDouble lengthDelta23 = QuaternionDoubleLengthSq(VectorDoubleSubtract(sq2, q3));
    SIMDVectorDouble sq3 = VectorDoubleNegate(q3);

    sq0 = VectorDoubleSelect(q0, sq0, VectorDoubleLess(lengthSq01, lengthDelta01));
    sq3 = VectorDoubleSelect(q3, sq3, VectorDoubleLess(lengthSq23, lengthDelta23));

    const SIMDVectorDouble invQ1 = QuaternionDoubleInverse(q1);
    const SIMDVectorDouble invQ2 = QuaternionDoubleInverse(sq2);
    const SIMDVectorDouble lnQ0 = QuaternionDoubleLn(QuaternionDoubleMultiply(invQ1, sq0));
    const SIMDVectorDouble lnQ2 = QuaternionDoubleLn(QuaternionDoubleMultiply(invQ1, sq2));
    const SIMDVectorDouble lnQ1 = QuaternionDoubleLn(QuaternionDoubleMultiply(invQ2, q1));
    const SIMDVectorDouble lnQ3 = QuaternionDoubleLn(QuaternionDoubleMultiply(invQ2, sq3));

    SIMDVectorDouble expQ02 = VectorDoubleScale(VectorDoubleAdd(lnQ0, lnQ2), SIMDQuaternionDoubleDetail::s_DoubleQuaternionSquadControlScale);
    SIMDVectorDouble expQ13 = VectorDoubleScale(VectorDoubleAdd(lnQ1, lnQ3), SIMDQuaternionDoubleDetail::s_DoubleQuaternionSquadControlScale);
    expQ02 = QuaternionDoubleExp(expQ02);
    expQ13 = QuaternionDoubleExp(expQ13);

    outA = QuaternionDoubleMultiply(q1, expQ02);
    outB = QuaternionDoubleMultiply(sq2, expQ13);
    outC = sq2;
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionBaryCentric(SIMDVectorSingle q0, SIMDVectorSingle q1, SIMDVectorSingle q2, f32 f, f32 g)noexcept{
    const SIMDVectorSingle fVector = VectorReplicate(f);
    const SIMDVectorSingle gVector = VectorReplicate(g);
    const SIMDVectorSingle s = VectorAdd(fVector, gVector);
    if(Vector4InBounds(s, VectorReplicate(SIMDQuaternionDetail::s_QuaternionInterpolationEpsilon)))
        return q0;

    const SIMDVectorSingle q01 = QuaternionSlerpV(q0, q1, s);
    const SIMDVectorSingle q02 = QuaternionSlerpV(q0, q2, s);
    return QuaternionSlerpV(q01, q02, VectorDivide(gVector, s));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleBaryCentric(SIMDVectorDouble q0, SIMDVectorDouble q1, SIMDVectorDouble q2, f64 f, f64 g)noexcept{
    const SIMDVectorDouble fVector = VectorDoubleReplicate(f);
    const SIMDVectorDouble gVector = VectorDoubleReplicate(g);
    const SIMDVectorDouble s = VectorDoubleAdd(fVector, gVector);
    if(VectorDouble4InBounds(s, VectorDoubleReplicate(SIMDQuaternionDoubleDetail::s_DoubleQuaternionInterpolationEpsilon)))
        return q0;

    const SIMDVectorDouble q01 = QuaternionDoubleSlerpV(q0, q1, s);
    const SIMDVectorDouble q02 = QuaternionDoubleSlerpV(q0, q2, s);
    return QuaternionDoubleSlerpV(q01, q02, VectorDoubleDivide(gVector, s));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionBaryCentricV(SIMDVectorSingle q0, SIMDVectorSingle q1, SIMDVectorSingle q2, SIMDVectorSingle f, SIMDVectorSingle g)noexcept{
    NWB_ASSERT((VectorGetY(f) == VectorGetX(f)) && (VectorGetZ(f) == VectorGetX(f)) && (VectorGetW(f) == VectorGetX(f)));
    NWB_ASSERT((VectorGetY(g) == VectorGetX(g)) && (VectorGetZ(g) == VectorGetX(g)) && (VectorGetW(g) == VectorGetX(g)));

    const SIMDVectorSingle s = VectorAdd(f, g);
    if(Vector4InBounds(s, VectorReplicate(SIMDQuaternionDetail::s_QuaternionBarycentricVectorEpsilon)))
        return q0;

    const SIMDVectorSingle q01 = QuaternionSlerpV(q0, q1, s);
    const SIMDVectorSingle q02 = QuaternionSlerpV(q0, q2, s);
    const SIMDVectorSingle gs = VectorMultiply(g, VectorReciprocal(s));
    return QuaternionSlerpV(q01, q02, gs);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleBaryCentricV(SIMDVectorDouble q0, SIMDVectorDouble q1, SIMDVectorDouble q2, SIMDVectorDouble f, SIMDVectorDouble g)noexcept{
    NWB_ASSERT((VectorDoubleGetY(f) == VectorDoubleGetX(f)) && (VectorDoubleGetZ(f) == VectorDoubleGetX(f)) && (VectorDoubleGetW(f) == VectorDoubleGetX(f)));
    NWB_ASSERT((VectorDoubleGetY(g) == VectorDoubleGetX(g)) && (VectorDoubleGetZ(g) == VectorDoubleGetX(g)) && (VectorDoubleGetW(g) == VectorDoubleGetX(g)));

    const SIMDVectorDouble s = VectorDoubleAdd(f, g);
    if(VectorDouble4InBounds(s, VectorDoubleReplicate(SIMDQuaternionDoubleDetail::s_DoubleQuaternionBarycentricVectorEpsilon)))
        return q0;

    const SIMDVectorDouble q01 = QuaternionDoubleSlerpV(q0, q1, s);
    const SIMDVectorDouble q02 = QuaternionDoubleSlerpV(q0, q2, s);
    const SIMDVectorDouble gs = VectorDoubleMultiply(g, VectorDoubleReciprocal(s));
    return QuaternionDoubleSlerpV(q01, q02, gs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

