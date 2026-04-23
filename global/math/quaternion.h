// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDQuaternionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void ScalarSinCos(f32* outSin, f32* outCos, f32 value)noexcept{
    NWB_ASSERT(outSin != nullptr);
    NWB_ASSERT(outCos != nullptr);

    f32 quotient = s_1DIV2PI * value;
    if(value >= 0.0f)
        quotient = static_cast<f32>(static_cast<i32>(quotient + 0.5f));
    else
        quotient = static_cast<f32>(static_cast<i32>(quotient - 0.5f));

    f32 y = value - (s_2PI * quotient);
    f32 sign{};
    if(y > s_PIDIV2){
        y = s_PI - y;
        sign = -1.0f;
    }
    else if(y < -s_PIDIV2){
        y = -s_PI - y;
        sign = -1.0f;
    }
    else{
        sign = 1.0f;
    }

    const f32 y2 = y * y;
    *outSin = (((((-2.3889859e-08f * y2 + 2.7525562e-06f) * y2 - 0.00019840874f) * y2 + 0.0083333310f) * y2 - 0.16666667f) * y2 + 1.0f) * y;
    *outCos = sign * (((((-2.6051615e-07f * y2 + 2.4760495e-05f) * y2 - 0.0013888378f) * y2 + 0.041666638f) * y2 - 0.5f) * y2 + 1.0f);
}

NWB_INLINE f32 ScalarACos(f32 value)noexcept{
    const bool nonnegative = (value >= 0.0f);
    const f32 x = std::fabs(value);
    f32 omx = 1.0f - x;
    if(omx < 0.0f)
        omx = 0.0f;

    const f32 root = std::sqrt(omx);
    f32 result = ((((((-0.0012624911f * x + 0.0066700901f) * x - 0.0170881256f) * x + 0.0308918810f) * x - 0.0501743046f) * x + 0.0889789874f) * x - 0.2145988016f) * x + 1.5707963050f;
    result *= root;
    return nonnegative ? result : (s_PI - result);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorConstF s_SIMDQuaternionControlWZYX = { { { 1.0f, -1.0f, 1.0f, -1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionControlZWXY = { { { 1.0f, 1.0f, -1.0f, -1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionControlYXWZ = { { { -1.0f, 1.0f, 1.0f, -1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionRollPitchYawSign = { { { 1.0f, -1.0f, -1.0f, 1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionXMPMMP = { { { 1.0f, -1.0f, -1.0f, 1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionXMMPMP = { { { -1.0f, 1.0f, -1.0f, 1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionXMMMPP = { { { -1.0f, -1.0f, 1.0f, 1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionColumnWSign = { { { 1.0f, -1.0f, 1.0f, 1.0f } } };
static const SIMDVectorConstU s_SIMDQuaternionSignMask2 = { { { 0x80000000, 0x00000000, 0x00000000, 0x00000000 } } };
static const SIMDVectorConstU s_SIMDQuaternionSelect0110 = { { { s_SELECT_0, s_SELECT_1, s_SELECT_1, s_SELECT_0 } } };
static const SIMDVectorConstU s_SIMDQuaternionSelect0010 = { { { s_SELECT_0, s_SELECT_0, s_SELECT_1, s_SELECT_0 } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// comparison operations


NWB_INLINE bool SIMDCALL QuaternionEqual(SIMDVector q0, SIMDVector q1)noexcept{ return Vector4Equal(q0, q1); }
NWB_INLINE bool SIMDCALL QuaternionNotEqual(SIMDVector q0, SIMDVector q1)noexcept{ return Vector4NotEqual(q0, q1); }
NWB_INLINE bool SIMDCALL QuaternionIsNaN(SIMDVector q)noexcept{ return Vector4IsNaN(q); }
NWB_INLINE bool SIMDCALL QuaternionIsInfinite(SIMDVector q)noexcept{ return Vector4IsInfinite(q); }
NWB_INLINE bool SIMDCALL QuaternionIsIdentity(SIMDVector q)noexcept{ return Vector4Equal(q, s_SIMDIdentityR3); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// computation operations


NWB_INLINE SIMDVector SIMDCALL QuaternionDot(SIMDVector q0, SIMDVector q1)noexcept{ return Vector4Dot(q0, q1); }

NWB_INLINE SIMDVector SIMDCALL QuaternionMultiply(SIMDVector lhs, SIMDVector rhs)noexcept{
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
    SIMDVector result = vmulq_lane_f32(rhs, lhsH, 1);

    SIMDVector rhsShuffle = vrev64q_f32(rhs);
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
    SIMDVector lhsX = lhs;
    SIMDVector lhsY = lhs;
    SIMDVector lhsZ = lhs;
    SIMDVector result = lhs;
#if defined(NWB_HAS_AVX2)
    result = _mm_permute_ps(result, _MM_SHUFFLE(3, 3, 3, 3));
#if defined(__AVX2__) || defined(_M_AVX2)
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
    SIMDVector rhsShuffle = rhs;
#if defined(NWB_HAS_AVX2)
    rhsShuffle = _mm_permute_ps(rhsShuffle, _MM_SHUFFLE(0, 1, 2, 3));
#else
    rhsShuffle = _mm_shuffle_ps(rhsShuffle, rhsShuffle, _MM_SHUFFLE(0, 1, 2, 3));
#endif

    lhsX = _mm_mul_ps(lhsX, rhsShuffle);
    result = VectorMultiplyAdd(lhsX, SIMDQuaternionDetail::s_SIMDQuaternionControlWZYX, result);

#if defined(NWB_HAS_AVX2)
    rhsShuffle = _mm_permute_ps(rhsShuffle, _MM_SHUFFLE(2, 3, 0, 1));
#else
    rhsShuffle = _mm_shuffle_ps(rhsShuffle, rhsShuffle, _MM_SHUFFLE(2, 3, 0, 1));
#endif
    lhsY = _mm_mul_ps(lhsY, rhsShuffle);
    lhsY = _mm_mul_ps(lhsY, SIMDQuaternionDetail::s_SIMDQuaternionControlZWXY);

#if defined(NWB_HAS_AVX2)
    rhsShuffle = _mm_permute_ps(rhsShuffle, _MM_SHUFFLE(0, 1, 2, 3));
#else
    rhsShuffle = _mm_shuffle_ps(rhsShuffle, rhsShuffle, _MM_SHUFFLE(0, 1, 2, 3));
#endif
    lhsZ = _mm_mul_ps(lhsZ, rhsShuffle);
    lhsY = VectorMultiplyAdd(lhsZ, SIMDQuaternionDetail::s_SIMDQuaternionControlYXWZ, lhsY);
    return _mm_add_ps(result, lhsY);
#endif
}

NWB_INLINE SIMDVector SIMDCALL QuaternionLengthSq(SIMDVector q)noexcept{ return Vector4LengthSq(q); }
NWB_INLINE SIMDVector SIMDCALL QuaternionReciprocalLength(SIMDVector q)noexcept{ return Vector4ReciprocalLength(q); }
NWB_INLINE SIMDVector SIMDCALL QuaternionLength(SIMDVector q)noexcept{ return Vector4Length(q); }
NWB_INLINE SIMDVector SIMDCALL QuaternionNormalizeEst(SIMDVector q)noexcept{ return Vector4NormalizeEst(q); }
NWB_INLINE SIMDVector SIMDCALL QuaternionNormalize(SIMDVector q)noexcept{ return Vector4Normalize(q); }
NWB_INLINE SIMDVector SIMDCALL QuaternionConjugate(SIMDVector q)noexcept{ return VectorXorInt(q, s_SIMDNegate3); }

NWB_INLINE SIMDVector SIMDCALL QuaternionInverse(SIMDVector q)noexcept{
    const SIMDVector lengthSq = QuaternionLengthSq(q);
    const SIMDVector conjugate = QuaternionConjugate(q);
    const SIMDVector control = VectorLessOrEqual(lengthSq, s_SIMDEpsilon);
    const SIMDVector result = VectorDivide(conjugate, lengthSq);
    return VectorSelect(result, VectorZero(), control);
}

NWB_INLINE SIMDVector SIMDCALL QuaternionLn(SIMDVector q)noexcept{
    const SIMDVector oneMinusEpsilon = VectorReplicate(1.0f - 0.00001f);
    const SIMDVector qw = VectorSplatW(q);
    const SIMDVector q0 = VectorAndInt(q, s_SIMDMask3);
    const SIMDVector controlW = VectorInBounds(qw, oneMinusEpsilon);
    const SIMDVector theta = VectorACos(qw);
    const SIMDVector sinTheta = VectorSin(theta);
    const SIMDVector scale = VectorDivide(theta, sinTheta);
    const SIMDVector result = VectorMultiply(q0, scale);
    return VectorSelect(q0, result, controlW);
}

NWB_INLINE SIMDVector SIMDCALL QuaternionExp(SIMDVector q)noexcept{
    const SIMDVector theta = Vector3Length(q);
    SIMDVector sinTheta{};
    SIMDVector cosTheta{};
    VectorSinCos(&sinTheta, &cosTheta, theta);

    const SIMDVector scale = VectorDivide(sinTheta, theta);
    SIMDVector result = VectorMultiply(q, scale);
    result = VectorSelect(result, q, VectorNearEqual(theta, VectorZero(), s_SIMDEpsilon));
    return VectorSelect(cosTheta, result, s_SIMDSelect1110);
}

NWB_INLINE SIMDVector SIMDCALL QuaternionSlerpV(SIMDVector q0, SIMDVector q1, SIMDVector t)noexcept{
    NWB_ASSERT((VectorGetY(t) == VectorGetX(t)) && (VectorGetZ(t) == VectorGetX(t)) && (VectorGetW(t) == VectorGetX(t)));

#if defined(NWB_HAS_SCALAR) || defined(NWB_HAS_NEON)
    const SIMDVector oneMinusEpsilon = VectorReplicate(1.0f - 0.00001f);
    SIMDVector cosOmega = QuaternionDot(q0, q1);
    SIMDVector control = VectorLess(cosOmega, VectorZero());
    SIMDVector sign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, control);

    cosOmega = VectorMultiply(cosOmega, sign);
    control = VectorLess(cosOmega, oneMinusEpsilon);

    SIMDVector sinOmega = VectorNegativeMultiplySubtract(cosOmega, cosOmega, s_SIMDOne);
    sinOmega = VectorSqrt(sinOmega);

    const SIMDVector omega = VectorATan2(sinOmega, cosOmega);
    SIMDVector signMask = VectorSplatSignMask();
    SIMDVector v01 = VectorShiftLeft(t, VectorZero(), 2);
    signMask = VectorShiftLeft(signMask, VectorZero(), 3);
    v01 = VectorXorInt(v01, signMask);
    v01 = VectorAdd(s_SIMDIdentityR0, v01);

    SIMDVector s0 = VectorMultiply(v01, omega);
    s0 = VectorSin(s0);
    s0 = VectorMultiply(s0, VectorReciprocal(sinOmega));
    s0 = VectorSelect(v01, s0, control);

    SIMDVector s1 = VectorSplatY(s0);
    s0 = VectorSplatX(s0);
    s1 = VectorMultiply(s1, sign);

    SIMDVector result = VectorMultiply(q0, s0);
    return VectorMultiplyAdd(q1, s1, result);
#else
    const SIMDVector oneMinusEpsilon = VectorReplicate(1.0f - 0.00001f);
    SIMDVector cosOmega = QuaternionDot(q0, q1);
    SIMDVector control = _mm_cmplt_ps(cosOmega, s_SIMDZero);
    SIMDVector sign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, control);

    cosOmega = _mm_mul_ps(cosOmega, sign);
    control = _mm_cmplt_ps(cosOmega, oneMinusEpsilon);

    SIMDVector sinOmega = _mm_mul_ps(cosOmega, cosOmega);
    sinOmega = _mm_sub_ps(s_SIMDOne, sinOmega);
    sinOmega = _mm_sqrt_ps(sinOmega);

    const SIMDVector omega = VectorATan2(sinOmega, cosOmega);
    SIMDVector v01{};
#if defined(NWB_HAS_AVX2)
    v01 = _mm_permute_ps(t, _MM_SHUFFLE(2, 3, 0, 1));
#else
    v01 = _mm_shuffle_ps(t, t, _MM_SHUFFLE(2, 3, 0, 1));
#endif
    v01 = _mm_and_ps(v01, s_SIMDMaskXY);
    v01 = _mm_xor_ps(v01, SIMDQuaternionDetail::s_SIMDQuaternionSignMask2);
    v01 = _mm_add_ps(s_SIMDIdentityR0, v01);

    SIMDVector s0 = _mm_mul_ps(v01, omega);
    s0 = VectorSin(s0);
    s0 = _mm_div_ps(s0, sinOmega);
    s0 = VectorSelect(v01, s0, control);

    SIMDVector s1{};
#if defined(NWB_HAS_AVX2)
    s1 = _mm_permute_ps(s0, _MM_SHUFFLE(1, 1, 1, 1));
#if defined(__AVX2__) || defined(_M_AVX2)
    s0 = _mm_broadcastss_ps(s0);
#else
    s0 = _mm_permute_ps(s0, _MM_SHUFFLE(0, 0, 0, 0));
#endif
#else
    s1 = _mm_shuffle_ps(s0, s0, _MM_SHUFFLE(1, 1, 1, 1));
    s0 = _mm_shuffle_ps(s0, s0, _MM_SHUFFLE(0, 0, 0, 0));
#endif

    s1 = _mm_mul_ps(s1, sign);
    SIMDVector result = _mm_mul_ps(q0, s0);
    return VectorMultiplyAdd(q1, s1, result);
#endif
}

NWB_INLINE SIMDVector SIMDCALL QuaternionSlerp(SIMDVector q0, SIMDVector q1, f32 t)noexcept{
    return QuaternionSlerpV(q0, q1, VectorReplicate(t));
}

NWB_INLINE SIMDVector SIMDCALL QuaternionSquadV(SIMDVector q0, SIMDVector q1, SIMDVector q2, SIMDVector q3, SIMDVector t)noexcept{
    NWB_ASSERT((VectorGetY(t) == VectorGetX(t)) && (VectorGetZ(t) == VectorGetX(t)) && (VectorGetW(t) == VectorGetX(t)));

    const SIMDVector q03 = QuaternionSlerpV(q0, q3, t);
    const SIMDVector q12 = QuaternionSlerpV(q1, q2, t);
    SIMDVector tp = VectorNegativeMultiplySubtract(t, t, t);
    tp = VectorAdd(tp, tp);
    return QuaternionSlerpV(q03, q12, tp);
}

NWB_INLINE SIMDVector SIMDCALL QuaternionSquad(SIMDVector q0, SIMDVector q1, SIMDVector q2, SIMDVector q3, f32 t)noexcept{
    return QuaternionSquadV(q0, q1, q2, q3, VectorReplicate(t));
}

NWB_INLINE void SIMDCALL QuaternionSquadSetup(SIMDVector* outA, SIMDVector* outB, SIMDVector* outC, SIMDVector q0, SIMDVector q1, SIMDVector q2, SIMDVector q3)noexcept{
    NWB_ASSERT(outA != nullptr);
    NWB_ASSERT(outB != nullptr);
    NWB_ASSERT(outC != nullptr);

    const SIMDVector lengthSq12 = QuaternionLengthSq(VectorAdd(q1, q2));
    const SIMDVector lengthDelta12 = QuaternionLengthSq(VectorSubtract(q1, q2));
    SIMDVector sq2 = VectorNegate(q2);
    sq2 = VectorSelect(q2, sq2, VectorLess(lengthSq12, lengthDelta12));

    const SIMDVector lengthSq01 = QuaternionLengthSq(VectorAdd(q0, q1));
    const SIMDVector lengthDelta01 = QuaternionLengthSq(VectorSubtract(q0, q1));
    SIMDVector sq0 = VectorNegate(q0);

    const SIMDVector lengthSq23 = QuaternionLengthSq(VectorAdd(sq2, q3));
    const SIMDVector lengthDelta23 = QuaternionLengthSq(VectorSubtract(sq2, q3));
    SIMDVector sq3 = VectorNegate(q3);

    sq0 = VectorSelect(q0, sq0, VectorLess(lengthSq01, lengthDelta01));
    sq3 = VectorSelect(q3, sq3, VectorLess(lengthSq23, lengthDelta23));

    const SIMDVector invQ1 = QuaternionInverse(q1);
    const SIMDVector invQ2 = QuaternionInverse(sq2);
    const SIMDVector lnQ0 = QuaternionLn(QuaternionMultiply(invQ1, sq0));
    const SIMDVector lnQ2 = QuaternionLn(QuaternionMultiply(invQ1, sq2));
    const SIMDVector lnQ1 = QuaternionLn(QuaternionMultiply(invQ2, q1));
    const SIMDVector lnQ3 = QuaternionLn(QuaternionMultiply(invQ2, sq3));

    SIMDVector expQ02 = VectorScale(VectorAdd(lnQ0, lnQ2), -0.25f);
    SIMDVector expQ13 = VectorScale(VectorAdd(lnQ1, lnQ3), -0.25f);
    expQ02 = QuaternionExp(expQ02);
    expQ13 = QuaternionExp(expQ13);

    *outA = QuaternionMultiply(q1, expQ02);
    *outB = QuaternionMultiply(sq2, expQ13);
    *outC = sq2;
}

NWB_INLINE SIMDVector SIMDCALL QuaternionBaryCentric(SIMDVector q0, SIMDVector q1, SIMDVector q2, f32 f, f32 g)noexcept{
    const f32 s = f + g;
    if((s < 0.00001f) && (s > -0.00001f))
        return q0;

    const SIMDVector q01 = QuaternionSlerp(q0, q1, s);
    const SIMDVector q02 = QuaternionSlerp(q0, q2, s);
    return QuaternionSlerp(q01, q02, g / s);
}

NWB_INLINE SIMDVector SIMDCALL QuaternionBaryCentricV(SIMDVector q0, SIMDVector q1, SIMDVector q2, SIMDVector f, SIMDVector g)noexcept{
    NWB_ASSERT((VectorGetY(f) == VectorGetX(f)) && (VectorGetZ(f) == VectorGetX(f)) && (VectorGetW(f) == VectorGetX(f)));
    NWB_ASSERT((VectorGetY(g) == VectorGetX(g)) && (VectorGetZ(g) == VectorGetX(g)) && (VectorGetW(g) == VectorGetX(g)));

    const SIMDVector s = VectorAdd(f, g);
    if(Vector4InBounds(s, VectorReplicate(1.0f / 65536.0f)))
        return q0;

    const SIMDVector q01 = QuaternionSlerpV(q0, q1, s);
    const SIMDVector q02 = QuaternionSlerpV(q0, q2, s);
    const SIMDVector gs = VectorMultiply(g, VectorReciprocal(s));
    return QuaternionSlerpV(q01, q02, gs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// transformation operations


NWB_INLINE SIMDVector SIMDCALL QuaternionIdentity()noexcept{ return s_SIMDIdentityR3; }

NWB_INLINE SIMDVector SIMDCALL QuaternionRotationNormal(SIMDVector normalAxis, f32 angle)noexcept{
#if defined(NWB_HAS_SCALAR) || defined(NWB_HAS_NEON)
    SIMDVector n = VectorSelect(s_SIMDOne, normalAxis, s_SIMDSelect1110);
    f32 sinAngle{};
    f32 cosAngle{};
    SIMDQuaternionDetail::ScalarSinCos(&sinAngle, &cosAngle, 0.5f * angle);
    const SIMDVector scale = VectorSet(sinAngle, sinAngle, sinAngle, cosAngle);
    return VectorMultiply(n, scale);
#else
    SIMDVector n = _mm_and_ps(normalAxis, s_SIMDMask3);
    n = _mm_or_ps(n, s_SIMDIdentityR3);

    SIMDVector sinAngle{};
    SIMDVector cosAngle{};
    VectorSinCos(&sinAngle, &cosAngle, _mm_set1_ps(0.5f * angle));

    sinAngle = _mm_and_ps(sinAngle, s_SIMDMask3);
    cosAngle = _mm_and_ps(cosAngle, s_SIMDMaskW);
    const SIMDVector scale = _mm_or_ps(sinAngle, cosAngle);
    return _mm_mul_ps(n, scale);
#endif
}

NWB_INLINE SIMDVector SIMDCALL QuaternionRotationAxis(SIMDVector axis, f32 angle)noexcept{
    NWB_ASSERT(!Vector3Equal(axis, VectorZero()));
    NWB_ASSERT(!Vector3IsInfinite(axis));

    return QuaternionRotationNormal(Vector3Normalize(axis), angle);
}

NWB_INLINE SIMDVector SIMDCALL QuaternionRotationRollPitchYawFromVector(SIMDVector angles)noexcept{
#if defined(NWB_HAS_SCALAR)
    const f32 halfPitch = 0.5f * angles.f[0];
    const f32 cp = static_cast<f32>(std::cos(halfPitch));
    const f32 sp = static_cast<f32>(std::sin(halfPitch));

    const f32 halfYaw = 0.5f * angles.f[1];
    const f32 cy = static_cast<f32>(std::cos(halfYaw));
    const f32 sy = static_cast<f32>(std::sin(halfYaw));

    const f32 halfRoll = 0.5f * angles.f[2];
    const f32 cr = static_cast<f32>(std::cos(halfRoll));
    const f32 sr = static_cast<f32>(std::sin(halfRoll));

    return SIMDConvertDetail::MakeF32(
        (cr * sp * cy) + (sr * cp * sy),
        (cr * cp * sy) - (sr * sp * cy),
        (sr * cp * cy) - (cr * sp * sy),
        (cr * cp * cy) + (sr * sp * sy)
    );
#else
    const SIMDVector halfAngles = VectorMultiply(angles, s_SIMDOneHalf);

    SIMDVector sinAngles{};
    SIMDVector cosAngles{};
    VectorSinCos(&sinAngles, &cosAngles, halfAngles);

    const SIMDVector p0 = VectorPermute<0, 4, 4, 4>(sinAngles, cosAngles);
    const SIMDVector y0 = VectorPermute<5, 1, 5, 5>(sinAngles, cosAngles);
    const SIMDVector r0 = VectorPermute<6, 6, 2, 6>(sinAngles, cosAngles);
    const SIMDVector p1 = VectorPermute<0, 4, 4, 4>(cosAngles, sinAngles);
    const SIMDVector y1 = VectorPermute<5, 1, 5, 5>(cosAngles, sinAngles);
    const SIMDVector r1 = VectorPermute<6, 6, 2, 6>(cosAngles, sinAngles);

    SIMDVector q1 = VectorMultiply(p1, SIMDQuaternionDetail::s_SIMDQuaternionRollPitchYawSign);
    SIMDVector q0 = VectorMultiply(p0, y0);
    q1 = VectorMultiply(q1, y1);
    q0 = VectorMultiply(q0, r0);
    return VectorMultiplyAdd(q1, r1, q0);
#endif
}

NWB_INLINE SIMDVector SIMDCALL QuaternionRotationRollPitchYaw(f32 pitch, f32 yaw, f32 roll)noexcept{
    return QuaternionRotationRollPitchYawFromVector(VectorSet(pitch, yaw, roll, 0.0f));
}

NWB_INLINE SIMDVector SIMDCALL QuaternionRotationMatrix(const SIMDMatrix& matrix)noexcept{
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
            const f32 inv4x = 0.5f / std::sqrt(fourXSqr);
            x = fourXSqr * inv4x;
            y = (matrix.m[0][1] + matrix.m[1][0]) * inv4x;
            z = (matrix.m[0][2] + matrix.m[2][0]) * inv4x;
            w = (matrix.m[2][1] - matrix.m[1][2]) * inv4x;
        }
        else{
            const f32 fourYSqr = omr22 + dif10;
            const f32 inv4y = 0.5f / std::sqrt(fourYSqr);
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
            const f32 inv4z = 0.5f / std::sqrt(fourZSqr);
            x = (matrix.m[0][2] + matrix.m[2][0]) * inv4z;
            y = (matrix.m[1][2] + matrix.m[2][1]) * inv4z;
            z = fourZSqr * inv4z;
            w = (matrix.m[1][0] - matrix.m[0][1]) * inv4z;
        }
        else{
            const f32 fourWSqr = opr22 + sum10;
            const f32 inv4w = 0.5f / std::sqrt(fourWSqr);
            x = (matrix.m[2][1] - matrix.m[1][2]) * inv4w;
            y = (matrix.m[0][2] - matrix.m[2][0]) * inv4w;
            z = (matrix.m[1][0] - matrix.m[0][1]) * inv4w;
            w = fourWSqr * inv4w;
        }
    }

    return SIMDConvertDetail::MakeF32(x, y, z, w);
#elif defined(NWB_HAS_NEON)
    const SIMDVector r0 = matrix.v[0];
    const SIMDVector r1 = matrix.v[1];
    const SIMDVector r2 = matrix.v[2];

    const SIMDVector r00 = vdupq_lane_f32(vget_low_f32(r0), 0);
    const SIMDVector r11 = vdupq_lane_f32(vget_low_f32(r1), 1);
    const SIMDVector r22 = vdupq_lane_f32(vget_high_f32(r2), 0);

    const uint32x4_t x2Gey2 = vcleq_f32(vsubq_f32(r11, r00), s_SIMDZero);
    const uint32x4_t z2Gew2 = vcleq_f32(vaddq_f32(r11, r00), s_SIMDZero);
    const uint32x4_t x2Py2Gez2Pw2 = vcleq_f32(r22, s_SIMDZero);

    SIMDVector t0 = vmulq_f32(SIMDQuaternionDetail::s_SIMDQuaternionXMPMMP, r00);
    SIMDVector x2y2z2w2 = vmlaq_f32(t0, SIMDQuaternionDetail::s_SIMDQuaternionXMMPMP, r11);
    x2y2z2w2 = vmlaq_f32(x2y2z2w2, SIMDQuaternionDetail::s_SIMDQuaternionXMMMPP, r22);
    x2y2z2w2 = vaddq_f32(x2y2z2w2, s_SIMDOne);

    t0 = vextq_f32(r0, r0, 1);
    SIMDVector t1 = vextq_f32(r1, r1, 1);
    t0 = vcombine_f32(vget_low_f32(t0), vrev64_f32(vget_low_f32(t1)));

    t1 = vextq_f32(r2, r2, 3);
    const SIMDVector r10 = vdupq_lane_f32(vget_low_f32(r1), 0);
    t1 = vbslq_f32(SIMDQuaternionDetail::s_SIMDQuaternionSelect0110, t1, r10);
    const SIMDVector xyxzyz = vaddq_f32(t0, t1);

    t0 = vcombine_f32(vrev64_f32(vget_low_f32(r2)), vget_low_f32(r10));
    const SIMDVector t2 = vcombine_f32(vrev64_f32(vget_high_f32(r0)), vrev64_f32(vget_low_f32(r0)));
    const SIMDVector t3 = vdupq_lane_f32(vget_high_f32(r1), 0);
    t1 = vbslq_f32(SIMDQuaternionDetail::s_SIMDQuaternionSelect0110, t2, t3);

    SIMDVector xwywzw = vsubq_f32(t0, t1);
    xwywzw = vmulq_f32(SIMDQuaternionDetail::s_SIMDQuaternionColumnWSign, xwywzw);

    t0 = vextq_f32(xyxzyz, xyxzyz, 3);
    t1 = vbslq_f32(SIMDQuaternionDetail::s_SIMDQuaternionSelect0110, t0, x2y2z2w2);
    t0 = vdupq_lane_f32(vget_low_f32(xwywzw), 0);
    const SIMDVector tensor0 = vbslq_f32(s_SIMDSelect1110, t1, t0);

    t0 = vbslq_f32(s_SIMDSelect1011, xyxzyz, x2y2z2w2);
    t1 = vdupq_lane_f32(vget_low_f32(xwywzw), 1);
    const SIMDVector tensor1 = vbslq_f32(s_SIMDSelect1110, t0, t1);

    t0 = vextq_f32(xyxzyz, xyxzyz, 1);
    t1 = vcombine_f32(vget_low_f32(t0), vrev64_f32(vget_high_f32(xwywzw)));
    const SIMDVector tensor2 = vbslq_f32(SIMDQuaternionDetail::s_SIMDQuaternionSelect0010, x2y2z2w2, t1);
    const SIMDVector tensor3 = vbslq_f32(s_SIMDSelect1110, xwywzw, x2y2z2w2);

    t0 = vbslq_f32(x2Gey2, tensor0, tensor1);
    t1 = vbslq_f32(z2Gew2, tensor2, tensor3);
    const SIMDVector result = vbslq_f32(x2Py2Gez2Pw2, t0, t1);
    return VectorDivide(result, Vector4Length(result));
#else
    const SIMDVector r0 = matrix.v[0];
    const SIMDVector r1 = matrix.v[1];
    const SIMDVector r2 = matrix.v[2];

    SIMDVector r00{};
    SIMDVector r11{};
    SIMDVector r22{};
#if defined(NWB_HAS_AVX2)
#if defined(__AVX2__) || defined(_M_AVX2)
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

    const SIMDVector x2Gey2 = _mm_cmple_ps(_mm_sub_ps(r11, r00), s_SIMDZero);
    const SIMDVector z2Gew2 = _mm_cmple_ps(_mm_add_ps(r11, r00), s_SIMDZero);
    const SIMDVector x2Py2Gez2Pw2 = _mm_cmple_ps(r22, s_SIMDZero);

    SIMDVector t0 = VectorMultiplyAdd(SIMDQuaternionDetail::s_SIMDQuaternionXMPMMP, r00, s_SIMDOne);
    SIMDVector t1 = _mm_mul_ps(SIMDQuaternionDetail::s_SIMDQuaternionXMMPMP, r11);
    SIMDVector t2 = VectorMultiplyAdd(SIMDQuaternionDetail::s_SIMDQuaternionXMMMPP, r22, t0);
    const SIMDVector x2y2z2w2 = _mm_add_ps(t1, t2);

    t0 = _mm_shuffle_ps(r0, r1, _MM_SHUFFLE(1, 2, 2, 1));
    t1 = _mm_shuffle_ps(r1, r2, _MM_SHUFFLE(1, 0, 0, 0));
#if defined(NWB_HAS_AVX2)
    t1 = _mm_permute_ps(t1, _MM_SHUFFLE(1, 3, 2, 0));
#else
    t1 = _mm_shuffle_ps(t1, t1, _MM_SHUFFLE(1, 3, 2, 0));
#endif
    const SIMDVector xyxzyz = _mm_add_ps(t0, t1);

    t0 = _mm_shuffle_ps(r2, r1, _MM_SHUFFLE(0, 0, 0, 1));
    t1 = _mm_shuffle_ps(r1, r0, _MM_SHUFFLE(1, 2, 2, 2));
#if defined(NWB_HAS_AVX2)
    t1 = _mm_permute_ps(t1, _MM_SHUFFLE(1, 3, 2, 0));
#else
    t1 = _mm_shuffle_ps(t1, t1, _MM_SHUFFLE(1, 3, 2, 0));
#endif
    SIMDVector xwywzw = _mm_sub_ps(t0, t1);
    xwywzw = _mm_mul_ps(SIMDQuaternionDetail::s_SIMDQuaternionColumnWSign, xwywzw);

    t0 = _mm_shuffle_ps(x2y2z2w2, xyxzyz, _MM_SHUFFLE(0, 0, 1, 0));
    t1 = _mm_shuffle_ps(x2y2z2w2, xwywzw, _MM_SHUFFLE(0, 2, 3, 2));
    t2 = _mm_shuffle_ps(xyxzyz, xwywzw, _MM_SHUFFLE(1, 0, 2, 1));

    const SIMDVector tensor0 = _mm_shuffle_ps(t0, t2, _MM_SHUFFLE(2, 0, 2, 0));
    const SIMDVector tensor1 = _mm_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 1, 1, 2));
    const SIMDVector tensor2 = _mm_shuffle_ps(t2, t1, _MM_SHUFFLE(2, 0, 1, 0));
    const SIMDVector tensor3 = _mm_shuffle_ps(t2, t1, _MM_SHUFFLE(1, 2, 3, 2));

    t0 = _mm_or_ps(_mm_and_ps(x2Gey2, tensor0), _mm_andnot_ps(x2Gey2, tensor1));
    t1 = _mm_or_ps(_mm_and_ps(z2Gew2, tensor2), _mm_andnot_ps(z2Gew2, tensor3));
    t2 = _mm_or_ps(_mm_and_ps(x2Py2Gez2Pw2, t0), _mm_andnot_ps(x2Py2Gez2Pw2, t1));

    return _mm_div_ps(t2, Vector4Length(t2));
#endif
}

NWB_INLINE void SIMDCALL QuaternionToAxisAngle(SIMDVector* outAxis, f32* outAngle, SIMDVector q)noexcept{
    NWB_ASSERT(outAxis != nullptr);
    NWB_ASSERT(outAngle != nullptr);

    *outAxis = q;
    *outAngle = 2.0f * SIMDQuaternionDetail::ScalarACos(VectorGetW(q));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

