// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDQuaternionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorConstF s_SIMDQuaternionControlWZYX = { { { 1.0f, -1.0f, 1.0f, -1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionControlZWXY = { { { 1.0f, 1.0f, -1.0f, -1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionControlYXWZ = { { { -1.0f, 1.0f, 1.0f, -1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionRollPitchYawSign = { { { 1.0f, -1.0f, -1.0f, 1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionSignPNNP = { { { 1.0f, -1.0f, -1.0f, 1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionSignNPNP = { { { -1.0f, 1.0f, -1.0f, 1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionSignNNPP = { { { -1.0f, -1.0f, 1.0f, 1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionColumnWSign = { { { 1.0f, -1.0f, 1.0f, 1.0f } } };
static const SIMDVectorConstU s_SIMDQuaternionSignMask2 = { { { 0x80000000, 0x00000000, 0x00000000, 0x00000000 } } };
static const SIMDVectorConstU s_SIMDQuaternionSelect0110 = { { { s_SELECT_0, s_SELECT_1, s_SELECT_1, s_SELECT_0 } } };
static const SIMDVectorConstU s_SIMDQuaternionSelect0010 = { { { s_SELECT_0, s_SELECT_0, s_SELECT_1, s_SELECT_0 } } };
inline constexpr f32 s_QuaternionInterpolationEpsilon = 0.00001f;
inline constexpr f32 s_QuaternionBarycentricVectorEpsilon = 1.0f / 65536.0f;
inline constexpr f32 s_QuaternionSquadControlScale = -0.25f;
inline constexpr f32 s_QuaternionHalfAngleScale = 0.5f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool NWB_SIMD_CALL QuaternionEqual(SIMDVectorSingle q0, SIMDVectorSingle q1)noexcept{ return Vector4Equal(q0, q1); }
NWB_INLINE bool NWB_SIMD_CALL QuaternionNotEqual(SIMDVectorSingle q0, SIMDVectorSingle q1)noexcept{ return Vector4NotEqual(q0, q1); }
NWB_INLINE bool NWB_SIMD_CALL QuaternionIsNaN(SIMDVectorSingle q)noexcept{ return Vector4IsNaN(q); }
NWB_INLINE bool NWB_SIMD_CALL QuaternionIsInfinite(SIMDVectorSingle q)noexcept{ return Vector4IsInfinite(q); }
NWB_INLINE bool NWB_SIMD_CALL QuaternionIsIdentity(SIMDVectorSingle q)noexcept{ return Vector4Equal(q, s_SIMDIdentityR3); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionDot(SIMDVectorSingle q0, SIMDVectorSingle q1)noexcept{ return Vector4Dot(q0, q1); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionMultiply(SIMDVectorSingle lhs, SIMDVectorSingle rhs)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(
        (lhs.f[3] * rhs.f[0]) + (lhs.f[0] * rhs.f[3]) + (lhs.f[1] * rhs.f[2]) - (lhs.f[2] * rhs.f[1]),
        (lhs.f[3] * rhs.f[1]) - (lhs.f[0] * rhs.f[2]) + (lhs.f[1] * rhs.f[3]) + (lhs.f[2] * rhs.f[0]),
        (lhs.f[3] * rhs.f[2]) + (lhs.f[0] * rhs.f[1]) - (lhs.f[1] * rhs.f[0]) + (lhs.f[2] * rhs.f[3]),
        (lhs.f[3] * rhs.f[3]) - (lhs.f[0] * rhs.f[0]) - (lhs.f[1] * rhs.f[1]) - (lhs.f[2] * rhs.f[2])
    );
#elif defined(NWB_HAS_NEON)
    const float32x2_t lhsL = vget_low_f32(lhs);
    const float32x2_t lhsH = vget_high_f32(lhs);

    float32x4_t lhsX = vdupq_lane_f32(lhsL, 0);
    float32x4_t lhsY = vdupq_lane_f32(lhsL, 1);
    float32x4_t lhsZ = vdupq_lane_f32(lhsH, 0);
    SIMDVectorSingle result = vmulq_lane_f32(rhs, lhsH, 1);

    SIMDVectorSingle rhsShuffle = vrev64q_f32(rhs);
    rhsShuffle = vcombine_f32(vget_high_f32(rhsShuffle), vget_low_f32(rhsShuffle));
    lhsX = vmulq_f32(lhsX, rhsShuffle);
    result = vmlaq_f32(result, lhsX, SIMDQuaternionDetail::s_SIMDQuaternionControlWZYX);

    rhsShuffle = vreinterpretq_f32_u32(vrev64q_u32(vreinterpretq_u32_f32(rhsShuffle)));
    lhsY = vmulq_f32(lhsY, rhsShuffle);
    result = vmlaq_f32(result, lhsY, SIMDQuaternionDetail::s_SIMDQuaternionControlZWXY);

    rhsShuffle = vreinterpretq_f32_u32(vrev64q_u32(vreinterpretq_u32_f32(rhsShuffle)));
    rhsShuffle = vcombine_f32(vget_high_f32(rhsShuffle), vget_low_f32(rhsShuffle));
    lhsZ = vmulq_f32(lhsZ, rhsShuffle);
    return vmlaq_f32(result, lhsZ, SIMDQuaternionDetail::s_SIMDQuaternionControlYXWZ);
#else
    SIMDVectorSingle lhsX = lhs;
    SIMDVectorSingle lhsY = lhs;
    SIMDVectorSingle lhsZ = lhs;
    SIMDVectorSingle result = lhs;
#if defined(NWB_HAS_AVX)
    result = _mm_permute_ps(result, _MM_SHUFFLE(3, 3, 3, 3));
#if defined(NWB_HAS_AVX2)
    lhsX = _mm_broadcastss_ps(lhsX);
#else
    lhsX = _mm_permute_ps(lhsX, _MM_SHUFFLE(0, 0, 0, 0));
#endif
    lhsY = _mm_permute_ps(lhsY, _MM_SHUFFLE(1, 1, 1, 1));
    lhsZ = _mm_permute_ps(lhsZ, _MM_SHUFFLE(2, 2, 2, 2));
#else
    result = _mm_shuffle_ps(result, result, _MM_SHUFFLE(3, 3, 3, 3));
    lhsX = _mm_shuffle_ps(lhsX, lhsX, _MM_SHUFFLE(0, 0, 0, 0));
    lhsY = _mm_shuffle_ps(lhsY, lhsY, _MM_SHUFFLE(1, 1, 1, 1));
    lhsZ = _mm_shuffle_ps(lhsZ, lhsZ, _MM_SHUFFLE(2, 2, 2, 2));
#endif

    result = _mm_mul_ps(result, rhs);
    SIMDVectorSingle rhsShuffle = rhs;
#if defined(NWB_HAS_AVX)
    rhsShuffle = _mm_permute_ps(rhsShuffle, _MM_SHUFFLE(0, 1, 2, 3));
#else
    rhsShuffle = _mm_shuffle_ps(rhsShuffle, rhsShuffle, _MM_SHUFFLE(0, 1, 2, 3));
#endif

    lhsX = _mm_mul_ps(lhsX, rhsShuffle);
    result = VectorMultiplyAdd(lhsX, SIMDQuaternionDetail::s_SIMDQuaternionControlWZYX, result);

#if defined(NWB_HAS_AVX)
    rhsShuffle = _mm_permute_ps(rhsShuffle, _MM_SHUFFLE(2, 3, 0, 1));
#else
    rhsShuffle = _mm_shuffle_ps(rhsShuffle, rhsShuffle, _MM_SHUFFLE(2, 3, 0, 1));
#endif
    lhsY = _mm_mul_ps(lhsY, rhsShuffle);
    lhsY = _mm_mul_ps(lhsY, SIMDQuaternionDetail::s_SIMDQuaternionControlZWXY);

#if defined(NWB_HAS_AVX)
    rhsShuffle = _mm_permute_ps(rhsShuffle, _MM_SHUFFLE(0, 1, 2, 3));
#else
    rhsShuffle = _mm_shuffle_ps(rhsShuffle, rhsShuffle, _MM_SHUFFLE(0, 1, 2, 3));
#endif
    lhsZ = _mm_mul_ps(lhsZ, rhsShuffle);
    lhsY = VectorMultiplyAdd(lhsZ, SIMDQuaternionDetail::s_SIMDQuaternionControlYXWZ, lhsY);
    return _mm_add_ps(result, lhsY);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionLengthSq(SIMDVectorSingle q)noexcept{ return Vector4LengthSq(q); }
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionReciprocalLength(SIMDVectorSingle q)noexcept{ return Vector4ReciprocalLength(q); }
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionLength(SIMDVectorSingle q)noexcept{ return Vector4Length(q); }
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionNormalizeEst(SIMDVectorSingle q)noexcept{ return Vector4NormalizeEst(q); }
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionNormalize(SIMDVectorSingle q)noexcept{ return Vector4Normalize(q); }
NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionConjugate(SIMDVectorSingle q)noexcept{ return VectorXorInt(q, s_SIMDNegate3); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionInverse(SIMDVectorSingle q)noexcept{
    const SIMDVectorSingle lengthSq = QuaternionLengthSq(q);
    const SIMDVectorSingle conjugate = QuaternionConjugate(q);
    const SIMDVectorSingle control = VectorLessOrEqual(lengthSq, s_SIMDEpsilon);
    const SIMDVectorSingle result = VectorDivide(conjugate, lengthSq);
    return VectorSelect(result, VectorZero(), control);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionLn(SIMDVectorSingle q)noexcept{
    const SIMDVectorSingle oneMinusEpsilon = VectorReplicate(1.0f - SIMDQuaternionDetail::s_QuaternionInterpolationEpsilon);
    const SIMDVectorSingle qw = VectorSplatW(q);
    const SIMDVectorSingle q0 = VectorAndInt(q, s_SIMDMask3);
    const SIMDVectorSingle controlW = VectorInBounds(qw, oneMinusEpsilon);
    const SIMDVectorSingle theta = VectorACos(qw);
    const SIMDVectorSingle sinTheta = VectorSin(theta);
    const SIMDVectorSingle scale = VectorDivide(theta, sinTheta);
    const SIMDVectorSingle result = VectorMultiply(q0, scale);
    return VectorSelect(q0, result, controlW);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionExp(SIMDVectorSingle q)noexcept{
    const SIMDVectorSingle theta = Vector3Length(q);
    SIMDVectorSingle sinTheta{};
    SIMDVectorSingle cosTheta{};
    VectorSinCos(sinTheta, cosTheta, theta);

    const SIMDVectorSingle scale = VectorDivide(sinTheta, theta);
    SIMDVectorSingle result = VectorMultiply(q, scale);
    result = VectorSelect(result, q, VectorNearEqual(theta, VectorZero(), s_SIMDEpsilon));
    return VectorSelect(cosTheta, result, s_SIMDSelect1110);
}

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

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionSlerp(SIMDVectorSingle q0, SIMDVectorSingle q1, f32 t)noexcept{
    return QuaternionSlerpV(q0, q1, VectorReplicate(t));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionSquadV(SIMDVectorSingle q0, SIMDVectorSingle q1, SIMDVectorSingle q2, SIMDVectorSingle q3, SIMDVectorSingle t)noexcept{
    NWB_ASSERT((VectorGetY(t) == VectorGetX(t)) && (VectorGetZ(t) == VectorGetX(t)) && (VectorGetW(t) == VectorGetX(t)));

    const SIMDVectorSingle q03 = QuaternionSlerpV(q0, q3, t);
    const SIMDVectorSingle q12 = QuaternionSlerpV(q1, q2, t);
    SIMDVectorSingle tp = VectorNegativeMultiplySubtract(t, t, t);
    tp = VectorAdd(tp, tp);
    return QuaternionSlerpV(q03, q12, tp);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionSquad(SIMDVectorSingle q0, SIMDVectorSingle q1, SIMDVectorSingle q2, SIMDVectorSingle q3, f32 t)noexcept{
    return QuaternionSquadV(q0, q1, q2, q3, VectorReplicate(t));
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionIdentity()noexcept{ return s_SIMDIdentityR3; }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionRotationNormal(SIMDVectorSingle normalAxis, f32 angle)noexcept{
#if defined(NWB_HAS_SCALAR) || defined(NWB_HAS_NEON)
    const SIMDVectorSingle n = VectorSelect(s_SIMDIdentityR3, normalAxis, s_SIMDSelect1110);
    SIMDVectorSingle sinAngle{};
    SIMDVectorSingle cosAngle{};
    VectorSinCos(sinAngle, cosAngle, VectorScale(VectorReplicate(angle), SIMDQuaternionDetail::s_QuaternionHalfAngleScale));
    const SIMDVectorSingle scale = VectorSelect(cosAngle, sinAngle, s_SIMDSelect1110);
    return VectorMultiply(n, scale);
#else
    SIMDVectorSingle n = _mm_and_ps(normalAxis, s_SIMDMask3);
    n = _mm_or_ps(n, s_SIMDIdentityR3);

    SIMDVectorSingle sinAngle{};
    SIMDVectorSingle cosAngle{};
    VectorSinCos(sinAngle, cosAngle, VectorScale(VectorReplicate(angle), SIMDQuaternionDetail::s_QuaternionHalfAngleScale));

    sinAngle = _mm_and_ps(sinAngle, s_SIMDMask3);
    cosAngle = _mm_and_ps(cosAngle, s_SIMDMaskW);
    const SIMDVectorSingle scale = _mm_or_ps(sinAngle, cosAngle);
    return _mm_mul_ps(n, scale);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionRotationAxis(SIMDVectorSingle axis, f32 angle)noexcept{
    NWB_ASSERT(!Vector3Equal(axis, VectorZero()));
    NWB_ASSERT(!Vector3IsInfinite(axis));

    return QuaternionRotationNormal(Vector3Normalize(axis), angle);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionRotationRollPitchYawFromVector(SIMDVectorSingle angles)noexcept{
    const SIMDVectorSingle halfAngles = VectorMultiply(angles, s_SIMDOneHalf);
    SIMDVectorSingle sinAngles{};
    SIMDVectorSingle cosAngles{};
    VectorSinCos(sinAngles, cosAngles, halfAngles);

    const SIMDVectorSingle p0 = VectorPermute<0, 4, 4, 4>(sinAngles, cosAngles);
    const SIMDVectorSingle y0 = VectorPermute<5, 1, 5, 5>(sinAngles, cosAngles);
    const SIMDVectorSingle r0 = VectorPermute<6, 6, 2, 6>(sinAngles, cosAngles);
    const SIMDVectorSingle p1 = VectorPermute<0, 4, 4, 4>(cosAngles, sinAngles);
    const SIMDVectorSingle y1 = VectorPermute<5, 1, 5, 5>(cosAngles, sinAngles);
    const SIMDVectorSingle r1 = VectorPermute<6, 6, 2, 6>(cosAngles, sinAngles);

    SIMDVectorSingle q1 = VectorMultiply(p1, SIMDQuaternionDetail::s_SIMDQuaternionRollPitchYawSign);
    SIMDVectorSingle q0 = VectorMultiply(p0, y0);
    q1 = VectorMultiply(q1, y1);
    q0 = VectorMultiply(q0, r0);
    return VectorMultiplyAdd(q1, r1, q0);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionRotationRollPitchYaw(f32 pitch, f32 yaw, f32 roll)noexcept{
    return QuaternionRotationRollPitchYawFromVector(VectorSet(pitch, yaw, roll, 0.0f));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionRotationMatrix(const SIMDMatrix& matrix)noexcept{
#if defined(NWB_HAS_SCALAR)
    const f32 r22 = matrix.m[2][2];
    f32 x{};
    f32 y{};
    f32 z{};
    f32 w{};

    if(r22 <= 0.0f){
        const f32 dif10 = matrix.m[1][1] - matrix.m[0][0];
        const f32 omr22 = 1.0f - r22;
        if(dif10 <= 0.0f){
            const f32 fourXSqr = omr22 - dif10;
            const f32 inv4x = SIMDQuaternionDetail::s_QuaternionHalfAngleScale / Sqrt(fourXSqr);
            x = fourXSqr * inv4x;
            y = (matrix.m[0][1] + matrix.m[1][0]) * inv4x;
            z = (matrix.m[0][2] + matrix.m[2][0]) * inv4x;
            w = (matrix.m[2][1] - matrix.m[1][2]) * inv4x;
        }
        else{
            const f32 fourYSqr = omr22 + dif10;
            const f32 inv4y = SIMDQuaternionDetail::s_QuaternionHalfAngleScale / Sqrt(fourYSqr);
            x = (matrix.m[0][1] + matrix.m[1][0]) * inv4y;
            y = fourYSqr * inv4y;
            z = (matrix.m[1][2] + matrix.m[2][1]) * inv4y;
            w = (matrix.m[0][2] - matrix.m[2][0]) * inv4y;
        }
    }
    else{
        const f32 sum10 = matrix.m[1][1] + matrix.m[0][0];
        const f32 opr22 = 1.0f + r22;
        if(sum10 <= 0.0f){
            const f32 fourZSqr = opr22 - sum10;
            const f32 inv4z = SIMDQuaternionDetail::s_QuaternionHalfAngleScale / Sqrt(fourZSqr);
            x = (matrix.m[0][2] + matrix.m[2][0]) * inv4z;
            y = (matrix.m[1][2] + matrix.m[2][1]) * inv4z;
            z = fourZSqr * inv4z;
            w = (matrix.m[1][0] - matrix.m[0][1]) * inv4z;
        }
        else{
            const f32 fourWSqr = opr22 + sum10;
            const f32 inv4w = SIMDQuaternionDetail::s_QuaternionHalfAngleScale / Sqrt(fourWSqr);
            x = (matrix.m[2][1] - matrix.m[1][2]) * inv4w;
            y = (matrix.m[0][2] - matrix.m[2][0]) * inv4w;
            z = (matrix.m[1][0] - matrix.m[0][1]) * inv4w;
            w = fourWSqr * inv4w;
        }
    }

    return SIMDConvertDetail::MakeF32(x, y, z, w);
#elif defined(NWB_HAS_NEON)
    const SIMDVectorSingle r0 = matrix.v[0];
    const SIMDVectorSingle r1 = matrix.v[1];
    const SIMDVectorSingle r2 = matrix.v[2];

    const SIMDVectorSingle r00 = vdupq_lane_f32(vget_low_f32(r0), 0);
    const SIMDVectorSingle r11 = vdupq_lane_f32(vget_low_f32(r1), 1);
    const SIMDVectorSingle r22 = vdupq_lane_f32(vget_high_f32(r2), 0);

    const uint32x4_t x2Gey2 = vcleq_f32(vsubq_f32(r11, r00), s_SIMDZero);
    const uint32x4_t z2Gew2 = vcleq_f32(vaddq_f32(r11, r00), s_SIMDZero);
    const uint32x4_t x2Py2Gez2Pw2 = vcleq_f32(r22, s_SIMDZero);

    SIMDVectorSingle t0 = vmulq_f32(SIMDQuaternionDetail::s_SIMDQuaternionSignPNNP, r00);
    SIMDVectorSingle x2y2z2w2 = vmlaq_f32(t0, SIMDQuaternionDetail::s_SIMDQuaternionSignNPNP, r11);
    x2y2z2w2 = vmlaq_f32(x2y2z2w2, SIMDQuaternionDetail::s_SIMDQuaternionSignNNPP, r22);
    x2y2z2w2 = vaddq_f32(x2y2z2w2, s_SIMDOne);

    t0 = vextq_f32(r0, r0, 1);
    SIMDVectorSingle t1 = vextq_f32(r1, r1, 1);
    t0 = vcombine_f32(vget_low_f32(t0), vrev64_f32(vget_low_f32(t1)));

    t1 = vextq_f32(r2, r2, 3);
    const SIMDVectorSingle r10 = vdupq_lane_f32(vget_low_f32(r1), 0);
    t1 = vbslq_f32(SIMDQuaternionDetail::s_SIMDQuaternionSelect0110, t1, r10);
    const SIMDVectorSingle xyxzyz = vaddq_f32(t0, t1);

    t0 = vcombine_f32(vrev64_f32(vget_low_f32(r2)), vget_low_f32(r10));
    const SIMDVectorSingle t2 = vcombine_f32(vrev64_f32(vget_high_f32(r0)), vrev64_f32(vget_low_f32(r0)));
    const SIMDVectorSingle t3 = vdupq_lane_f32(vget_high_f32(r1), 0);
    t1 = vbslq_f32(SIMDQuaternionDetail::s_SIMDQuaternionSelect0110, t2, t3);

    SIMDVectorSingle xwywzw = vsubq_f32(t0, t1);
    xwywzw = vmulq_f32(SIMDQuaternionDetail::s_SIMDQuaternionColumnWSign, xwywzw);

    t0 = vextq_f32(xyxzyz, xyxzyz, 3);
    t1 = vbslq_f32(SIMDQuaternionDetail::s_SIMDQuaternionSelect0110, t0, x2y2z2w2);
    t0 = vdupq_lane_f32(vget_low_f32(xwywzw), 0);
    const SIMDVectorSingle tensor0 = vbslq_f32(s_SIMDSelect1110, t1, t0);

    t0 = vbslq_f32(s_SIMDSelect1011, xyxzyz, x2y2z2w2);
    t1 = vdupq_lane_f32(vget_low_f32(xwywzw), 1);
    const SIMDVectorSingle tensor1 = vbslq_f32(s_SIMDSelect1110, t0, t1);

    t0 = vextq_f32(xyxzyz, xyxzyz, 1);
    t1 = vcombine_f32(vget_low_f32(t0), vrev64_f32(vget_high_f32(xwywzw)));
    const SIMDVectorSingle tensor2 = vbslq_f32(SIMDQuaternionDetail::s_SIMDQuaternionSelect0010, x2y2z2w2, t1);
    const SIMDVectorSingle tensor3 = vbslq_f32(s_SIMDSelect1110, xwywzw, x2y2z2w2);

    t0 = vbslq_f32(x2Gey2, tensor0, tensor1);
    t1 = vbslq_f32(z2Gew2, tensor2, tensor3);
    const SIMDVectorSingle result = vbslq_f32(x2Py2Gez2Pw2, t0, t1);
    return VectorDivide(result, Vector4Length(result));
#else
    const SIMDVectorSingle r0 = matrix.v[0];
    const SIMDVectorSingle r1 = matrix.v[1];
    const SIMDVectorSingle r2 = matrix.v[2];

    SIMDVectorSingle r00{};
    SIMDVectorSingle r11{};
    SIMDVectorSingle r22{};
#if defined(NWB_HAS_AVX)
#if defined(NWB_HAS_AVX2)
    r00 = _mm_broadcastss_ps(r0);
#else
    r00 = _mm_permute_ps(r0, _MM_SHUFFLE(0, 0, 0, 0));
#endif
    r11 = _mm_permute_ps(r1, _MM_SHUFFLE(1, 1, 1, 1));
    r22 = _mm_permute_ps(r2, _MM_SHUFFLE(2, 2, 2, 2));
#else
    r00 = _mm_shuffle_ps(r0, r0, _MM_SHUFFLE(0, 0, 0, 0));
    r11 = _mm_shuffle_ps(r1, r1, _MM_SHUFFLE(1, 1, 1, 1));
    r22 = _mm_shuffle_ps(r2, r2, _MM_SHUFFLE(2, 2, 2, 2));
#endif

    const SIMDVectorSingle x2Gey2 = _mm_cmple_ps(_mm_sub_ps(r11, r00), s_SIMDZero);
    const SIMDVectorSingle z2Gew2 = _mm_cmple_ps(_mm_add_ps(r11, r00), s_SIMDZero);
    const SIMDVectorSingle x2Py2Gez2Pw2 = _mm_cmple_ps(r22, s_SIMDZero);

    SIMDVectorSingle t0 = VectorMultiplyAdd(SIMDQuaternionDetail::s_SIMDQuaternionSignPNNP, r00, s_SIMDOne);
    SIMDVectorSingle t1 = _mm_mul_ps(SIMDQuaternionDetail::s_SIMDQuaternionSignNPNP, r11);
    SIMDVectorSingle t2 = VectorMultiplyAdd(SIMDQuaternionDetail::s_SIMDQuaternionSignNNPP, r22, t0);
    const SIMDVectorSingle x2y2z2w2 = _mm_add_ps(t1, t2);

    t0 = _mm_shuffle_ps(r0, r1, _MM_SHUFFLE(1, 2, 2, 1));
    t1 = _mm_shuffle_ps(r1, r2, _MM_SHUFFLE(1, 0, 0, 0));
#if defined(NWB_HAS_AVX)
    t1 = _mm_permute_ps(t1, _MM_SHUFFLE(1, 3, 2, 0));
#else
    t1 = _mm_shuffle_ps(t1, t1, _MM_SHUFFLE(1, 3, 2, 0));
#endif
    const SIMDVectorSingle xyxzyz = _mm_add_ps(t0, t1);

    t0 = _mm_shuffle_ps(r2, r1, _MM_SHUFFLE(0, 0, 0, 1));
    t1 = _mm_shuffle_ps(r1, r0, _MM_SHUFFLE(1, 2, 2, 2));
#if defined(NWB_HAS_AVX)
    t1 = _mm_permute_ps(t1, _MM_SHUFFLE(1, 3, 2, 0));
#else
    t1 = _mm_shuffle_ps(t1, t1, _MM_SHUFFLE(1, 3, 2, 0));
#endif
    SIMDVectorSingle xwywzw = _mm_sub_ps(t0, t1);
    xwywzw = _mm_mul_ps(SIMDQuaternionDetail::s_SIMDQuaternionColumnWSign, xwywzw);

    t0 = _mm_shuffle_ps(x2y2z2w2, xyxzyz, _MM_SHUFFLE(0, 0, 1, 0));
    t1 = _mm_shuffle_ps(x2y2z2w2, xwywzw, _MM_SHUFFLE(0, 2, 3, 2));
    t2 = _mm_shuffle_ps(xyxzyz, xwywzw, _MM_SHUFFLE(1, 0, 2, 1));

    const SIMDVectorSingle tensor0 = _mm_shuffle_ps(t0, t2, _MM_SHUFFLE(2, 0, 2, 0));
    const SIMDVectorSingle tensor1 = _mm_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 1, 1, 2));
    const SIMDVectorSingle tensor2 = _mm_shuffle_ps(t2, t1, _MM_SHUFFLE(2, 0, 1, 0));
    const SIMDVectorSingle tensor3 = _mm_shuffle_ps(t2, t1, _MM_SHUFFLE(1, 2, 3, 2));

    t0 = _mm_or_ps(_mm_and_ps(x2Gey2, tensor0), _mm_andnot_ps(x2Gey2, tensor1));
    t1 = _mm_or_ps(_mm_and_ps(z2Gew2, tensor2), _mm_andnot_ps(z2Gew2, tensor3));
    t2 = _mm_or_ps(_mm_and_ps(x2Py2Gez2Pw2, t0), _mm_andnot_ps(x2Py2Gez2Pw2, t1));

    return _mm_div_ps(t2, Vector4Length(t2));
#endif
}

NWB_INLINE void NWB_SIMD_CALL QuaternionToAxisAngle(SIMDVectorSingle& outAxis, f32& outAngle, SIMDVectorSingle q)noexcept{
    outAxis = q;
    const SIMDVectorSingle angle = VectorScale(VectorACos(VectorSplatW(q)), 2.0f);
    outAngle = VectorGetX(angle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDQuaternionDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorDoubleConstF s_DoubleSIMDQuaternionControlWZYX = { { { 1.0, -1.0, 1.0, -1.0 } } };
static const SIMDVectorDoubleConstF s_DoubleSIMDQuaternionControlZWXY = { { { 1.0, 1.0, -1.0, -1.0 } } };
static const SIMDVectorDoubleConstF s_DoubleSIMDQuaternionControlYXWZ = { { { -1.0, 1.0, 1.0, -1.0 } } };
static const SIMDVectorDoubleConstF s_DoubleSIMDQuaternionRollPitchYawSign = { { { 1.0, -1.0, -1.0, 1.0 } } };
inline constexpr f64 s_DoubleQuaternionInterpolationEpsilon = 1.0e-12;
inline constexpr f64 s_DoubleQuaternionBarycentricVectorEpsilon = 1.0e-12;
inline constexpr f64 s_DoubleQuaternionSquadControlScale = -0.25;
inline constexpr f64 s_DoubleQuaternionHalfAngleScale = 0.5;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE bool NWB_SIMD_CALL QuaternionDoubleEqual(SIMDVectorDouble q0, SIMDVectorDouble q1)noexcept{ return VectorDouble4Equal(q0, q1); }
NWB_INLINE bool NWB_SIMD_CALL QuaternionDoubleNotEqual(SIMDVectorDouble q0, SIMDVectorDouble q1)noexcept{ return VectorDouble4NotEqual(q0, q1); }
NWB_INLINE bool NWB_SIMD_CALL QuaternionDoubleIsNaN(SIMDVectorDouble q)noexcept{ return VectorDouble4IsNaN(q); }
NWB_INLINE bool NWB_SIMD_CALL QuaternionDoubleIsInfinite(SIMDVectorDouble q)noexcept{ return VectorDouble4IsInfinite(q); }
NWB_INLINE bool NWB_SIMD_CALL QuaternionDoubleIsIdentity(SIMDVectorDouble q)noexcept{ return VectorDouble4Equal(q, s_DoubleSIMDIdentityR3); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleDot(SIMDVectorDouble q0, SIMDVectorDouble q1)noexcept{ return VectorDouble4Dot(q0, q1); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleMultiply(SIMDVectorDouble lhs, SIMDVectorDouble rhs)noexcept{
    SIMDVectorDouble result = VectorDoubleMultiply(VectorDoubleSplatW(lhs), rhs);
    SIMDVectorDouble component = VectorDoubleMultiply(VectorDoubleSwizzle<3, 2, 1, 0>(rhs), SIMDQuaternionDoubleDetail::s_DoubleSIMDQuaternionControlWZYX);
    result = VectorDoubleMultiplyAdd(VectorDoubleSplatX(lhs), component, result);
    component = VectorDoubleMultiply(VectorDoubleSwizzle<2, 3, 0, 1>(rhs), SIMDQuaternionDoubleDetail::s_DoubleSIMDQuaternionControlZWXY);
    result = VectorDoubleMultiplyAdd(VectorDoubleSplatY(lhs), component, result);
    component = VectorDoubleMultiply(VectorDoubleSwizzle<1, 0, 3, 2>(rhs), SIMDQuaternionDoubleDetail::s_DoubleSIMDQuaternionControlYXWZ);
    return VectorDoubleMultiplyAdd(VectorDoubleSplatZ(lhs), component, result);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleLengthSq(SIMDVectorDouble q)noexcept{ return VectorDouble4LengthSq(q); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleReciprocalLength(SIMDVectorDouble q)noexcept{ return VectorDouble4ReciprocalLength(q); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleLength(SIMDVectorDouble q)noexcept{ return VectorDouble4Length(q); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleNormalizeEst(SIMDVectorDouble q)noexcept{ return VectorDouble4NormalizeEst(q); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleNormalize(SIMDVectorDouble q)noexcept{ return VectorDouble4Normalize(q); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleConjugate(SIMDVectorDouble q)noexcept{ return VectorDoubleXorInt(q, s_DoubleSIMDNegate3); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleInverse(SIMDVectorDouble q)noexcept{
    const SIMDVectorDouble lengthSq = QuaternionDoubleLengthSq(q);
    const SIMDVectorDouble conjugate = QuaternionDoubleConjugate(q);
    const SIMDVectorDouble control = VectorDoubleLessOrEqual(lengthSq, s_DoubleSIMDEpsilon);
    const SIMDVectorDouble result = VectorDoubleDivide(conjugate, lengthSq);
    return VectorDoubleSelect(result, VectorDoubleZero(), control);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleLn(SIMDVectorDouble q)noexcept{
    const SIMDVectorDouble oneMinusEpsilon = VectorDoubleReplicate(1.0 - SIMDQuaternionDoubleDetail::s_DoubleQuaternionInterpolationEpsilon);
    const SIMDVectorDouble qw = VectorDoubleSplatW(q);
    const SIMDVectorDouble q0 = VectorDoubleAndInt(q, s_DoubleSIMDMask3);
    const SIMDVectorDouble controlW = VectorDoubleInBounds(qw, oneMinusEpsilon);
    const SIMDVectorDouble theta = VectorDoubleACos(qw);
    const SIMDVectorDouble sinTheta = VectorDoubleSin(theta);
    const SIMDVectorDouble scale = VectorDoubleDivide(theta, sinTheta);
    const SIMDVectorDouble result = VectorDoubleMultiply(q0, scale);
    return VectorDoubleSelect(q0, result, controlW);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleExp(SIMDVectorDouble q)noexcept{
    const SIMDVectorDouble theta = VectorDouble3Length(q);
    SIMDVectorDouble sinTheta{};
    SIMDVectorDouble cosTheta{};
    VectorDoubleSinCos(sinTheta, cosTheta, theta);

    const SIMDVectorDouble scale = VectorDoubleDivide(sinTheta, theta);
    SIMDVectorDouble result = VectorDoubleMultiply(q, scale);
    result = VectorDoubleSelect(result, q, VectorDoubleNearEqual(theta, VectorDoubleZero(), s_DoubleSIMDEpsilon));
    return VectorDoubleSelect(cosTheta, result, s_DoubleSIMDSelect1110);
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

    const SIMDVectorDouble omega = VectorDoubleATan2(sinOmega, cosOmega);
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

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleSlerp(SIMDVectorDouble q0, SIMDVectorDouble q1, f64 t)noexcept{
    return QuaternionDoubleSlerpV(q0, q1, VectorDoubleReplicate(t));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleSquadV(SIMDVectorDouble q0, SIMDVectorDouble q1, SIMDVectorDouble q2, SIMDVectorDouble q3, SIMDVectorDouble t)noexcept{
    NWB_ASSERT((VectorDoubleGetY(t) == VectorDoubleGetX(t)) && (VectorDoubleGetZ(t) == VectorDoubleGetX(t)) && (VectorDoubleGetW(t) == VectorDoubleGetX(t)));

    const SIMDVectorDouble q03 = QuaternionDoubleSlerpV(q0, q3, t);
    const SIMDVectorDouble q12 = QuaternionDoubleSlerpV(q1, q2, t);
    SIMDVectorDouble tp = VectorDoubleNegativeMultiplySubtract(t, t, t);
    tp = VectorDoubleAdd(tp, tp);
    return QuaternionDoubleSlerpV(q03, q12, tp);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleSquad(SIMDVectorDouble q0, SIMDVectorDouble q1, SIMDVectorDouble q2, SIMDVectorDouble q3, f64 t)noexcept{
    return QuaternionDoubleSquadV(q0, q1, q2, q3, VectorDoubleReplicate(t));
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


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleIdentity()noexcept{ return s_DoubleSIMDIdentityR3; }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleRotationNormal(SIMDVectorDouble normalAxis, f64 angle)noexcept{
    const SIMDVectorDouble n = VectorDoubleSelect(s_DoubleSIMDIdentityR3, normalAxis, s_DoubleSIMDSelect1110);
    SIMDVectorDouble sinAngle{};
    SIMDVectorDouble cosAngle{};
    VectorDoubleSinCos(sinAngle, cosAngle, VectorDoubleScale(VectorDoubleReplicate(angle), SIMDQuaternionDoubleDetail::s_DoubleQuaternionHalfAngleScale));
    const SIMDVectorDouble scale = VectorDoubleSelect(cosAngle, sinAngle, s_DoubleSIMDSelect1110);
    return VectorDoubleMultiply(n, scale);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleRotationAxis(SIMDVectorDouble axis, f64 angle)noexcept{
    NWB_ASSERT(!VectorDouble3Equal(axis, VectorDoubleZero()));
    NWB_ASSERT(!VectorDouble3IsInfinite(axis));

    return QuaternionDoubleRotationNormal(VectorDouble3Normalize(axis), angle);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleRotationRollPitchYawFromVector(SIMDVectorDouble angles)noexcept{
    const SIMDVectorDouble halfAngles = VectorDoubleMultiply(angles, s_DoubleSIMDOneHalf);
    SIMDVectorDouble sinAngles{};
    SIMDVectorDouble cosAngles{};
    VectorDoubleSinCos(sinAngles, cosAngles, halfAngles);

    const SIMDVectorDouble p0 = VectorDoublePermute<0, 4, 4, 4>(sinAngles, cosAngles);
    const SIMDVectorDouble y0 = VectorDoublePermute<5, 1, 5, 5>(sinAngles, cosAngles);
    const SIMDVectorDouble r0 = VectorDoublePermute<6, 6, 2, 6>(sinAngles, cosAngles);
    const SIMDVectorDouble p1 = VectorDoublePermute<0, 4, 4, 4>(cosAngles, sinAngles);
    const SIMDVectorDouble y1 = VectorDoublePermute<5, 1, 5, 5>(cosAngles, sinAngles);
    const SIMDVectorDouble r1 = VectorDoublePermute<6, 6, 2, 6>(cosAngles, sinAngles);

    SIMDVectorDouble q1 = VectorDoubleMultiply(p1, SIMDQuaternionDoubleDetail::s_DoubleSIMDQuaternionRollPitchYawSign);
    SIMDVectorDouble q0 = VectorDoubleMultiply(p0, y0);
    q1 = VectorDoubleMultiply(q1, y1);
    q0 = VectorDoubleMultiply(q0, r0);
    return VectorDoubleMultiplyAdd(q1, r1, q0);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleRotationRollPitchYaw(f64 pitch, f64 yaw, f64 roll)noexcept{
    return QuaternionDoubleRotationRollPitchYawFromVector(VectorDoubleSet(pitch, yaw, roll, 0.0));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleRotationMatrix(const SIMDMatrixDouble& matrix)noexcept{
    const f64 r22 = matrix.v[2][2];
    f64 x{};
    f64 y{};
    f64 z{};
    f64 w{};

    if(r22 <= 0.0){
        const f64 dif10 = matrix.v[1][1] - matrix.v[0][0];
        const f64 omr22 = 1.0 - r22;
        if(dif10 <= 0.0){
            const f64 fourXSqr = omr22 - dif10;
            const f64 inv4x = SIMDQuaternionDoubleDetail::s_DoubleQuaternionHalfAngleScale / Sqrt(fourXSqr);
            x = fourXSqr * inv4x;
            y = (matrix.v[0][1] + matrix.v[1][0]) * inv4x;
            z = (matrix.v[0][2] + matrix.v[2][0]) * inv4x;
            w = (matrix.v[2][1] - matrix.v[1][2]) * inv4x;
        }
        else{
            const f64 fourYSqr = omr22 + dif10;
            const f64 inv4y = SIMDQuaternionDoubleDetail::s_DoubleQuaternionHalfAngleScale / Sqrt(fourYSqr);
            x = (matrix.v[0][1] + matrix.v[1][0]) * inv4y;
            y = fourYSqr * inv4y;
            z = (matrix.v[1][2] + matrix.v[2][1]) * inv4y;
            w = (matrix.v[0][2] - matrix.v[2][0]) * inv4y;
        }
    }
    else{
        const f64 sum10 = matrix.v[1][1] + matrix.v[0][0];
        const f64 opr22 = 1.0 + r22;
        if(sum10 <= 0.0){
            const f64 fourZSqr = opr22 - sum10;
            const f64 inv4z = SIMDQuaternionDoubleDetail::s_DoubleQuaternionHalfAngleScale / Sqrt(fourZSqr);
            x = (matrix.v[0][2] + matrix.v[2][0]) * inv4z;
            y = (matrix.v[1][2] + matrix.v[2][1]) * inv4z;
            z = fourZSqr * inv4z;
            w = (matrix.v[1][0] - matrix.v[0][1]) * inv4z;
        }
        else{
            const f64 fourWSqr = opr22 + sum10;
            const f64 inv4w = SIMDQuaternionDoubleDetail::s_DoubleQuaternionHalfAngleScale / Sqrt(fourWSqr);
            x = (matrix.v[2][1] - matrix.v[1][2]) * inv4w;
            y = (matrix.v[0][2] - matrix.v[2][0]) * inv4w;
            z = (matrix.v[1][0] - matrix.v[0][1]) * inv4w;
            w = fourWSqr * inv4w;
        }
    }

    return SIMDConvertDoubleDetail::MakeF64(x, y, z, w);
}

NWB_INLINE void NWB_SIMD_CALL QuaternionDoubleToAxisAngle(SIMDVectorDouble& outAxis, f64& outAngle, SIMDVectorDouble q)noexcept{
    outAxis = q;
    const SIMDVectorDouble angle = VectorDoubleScale(VectorDoubleACos(VectorDoubleSplatW(q)), 2.0);
    outAngle = VectorDoubleGetX(angle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

