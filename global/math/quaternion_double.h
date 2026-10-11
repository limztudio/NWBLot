// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_double.h"


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

