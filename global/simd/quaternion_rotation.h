// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDQuaternionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorConstF s_SIMDQuaternionRollPitchYawSign = { { { 1.0f, -1.0f, -1.0f, 1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionSignPNNP = { { { 1.0f, -1.0f, -1.0f, 1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionSignNPNP = { { { -1.0f, 1.0f, -1.0f, 1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionSignNNPP = { { { -1.0f, -1.0f, 1.0f, 1.0f } } };
static const SIMDVectorConstF s_SIMDQuaternionColumnWSign = { { { 1.0f, -1.0f, 1.0f, 1.0f } } };
static const SIMDVectorConstU s_SIMDQuaternionSelect0110 = { { { s_SELECT_0, s_SELECT_1, s_SELECT_1, s_SELECT_0 } } };
static const SIMDVectorConstU s_SIMDQuaternionSelect0010 = { { { s_SELECT_0, s_SELECT_0, s_SELECT_1, s_SELECT_0 } } };
inline constexpr f32 s_QuaternionHalfAngleScale = 0.5f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDQuaternionDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorDoubleConstF s_DoubleSIMDQuaternionRollPitchYawSign = { { { 1.0, -1.0, -1.0, 1.0 } } };
inline constexpr f64 s_DoubleQuaternionHalfAngleScale = 0.5;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionIdentity()noexcept{ return s_SIMDIdentityR3; }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleIdentity()noexcept{ return s_DoubleSIMDIdentityR3; }

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

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleRotationNormal(SIMDVectorDouble normalAxis, f64 angle)noexcept{
    const SIMDVectorDouble n = VectorDoubleSelect(s_DoubleSIMDIdentityR3, normalAxis, s_DoubleSIMDSelect1110);
    SIMDVectorDouble sinAngle{};
    SIMDVectorDouble cosAngle{};
#if defined(NWB_HAS_AVX2) || defined(NWB_HAS_NEON64)
    SIMDVectorDoubleDetail::SinCosReplicated(sinAngle, cosAngle, VectorDoubleScale(VectorDoubleReplicate(angle), SIMDQuaternionDoubleDetail::s_DoubleQuaternionHalfAngleScale));
#else
    VectorDoubleSinCos(sinAngle, cosAngle, VectorDoubleScale(VectorDoubleReplicate(angle), SIMDQuaternionDoubleDetail::s_DoubleQuaternionHalfAngleScale));
#endif
    const SIMDVectorDouble scale = VectorDoubleSelect(cosAngle, sinAngle, s_DoubleSIMDSelect1110);
    return VectorDoubleMultiply(n, scale);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionRotationAxis(SIMDVectorSingle axis, f32 angle)noexcept{
    NWB_ASSERT(!Vector3Equal(axis, VectorZero()));
    NWB_ASSERT(!Vector3IsInfinite(axis));

    return QuaternionRotationNormal(Vector3Normalize(axis), angle);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleRotationAxis(SIMDVectorDouble axis, f64 angle)noexcept{
    NWB_ASSERT(!VectorDouble3Equal(axis, VectorDoubleZero()));
    NWB_ASSERT(!VectorDouble3IsInfinite(axis));

    return QuaternionDoubleRotationNormal(VectorDouble3Normalize(axis), angle);
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

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL QuaternionRotationRollPitchYaw(f32 pitch, f32 yaw, f32 roll)noexcept{
    return QuaternionRotationRollPitchYawFromVector(VectorSet(pitch, yaw, roll, 0.0f));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleRotationRollPitchYaw(f64 pitch, f64 yaw, f64 roll)noexcept{
    return QuaternionDoubleRotationRollPitchYawFromVector(VectorDoubleSet(pitch, yaw, roll, 0.0));
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

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL QuaternionDoubleRotationMatrix(const SIMDMatrixDouble& matrix)noexcept{
    const SIMDVectorDouble r22 = VectorDoubleSplatZ(matrix.v[2]);
    if(VectorDoubleGetX(r22) <= 0.0){
        const SIMDVectorDouble dif10 = VectorDoubleSubtract(VectorDoubleSplatY(matrix.v[1]), VectorDoubleSplatX(matrix.v[0]));
        const SIMDVectorDouble omr22 = VectorDoubleSubtract(s_DoubleSIMDOne, r22);
        if(VectorDoubleGetX(dif10) <= 0.0){
            const SIMDVectorDouble fourXSqr = VectorDoubleSubtract(omr22, dif10);
            const SIMDVectorDouble inv4x = VectorDoubleDivide(s_DoubleSIMDOneHalf, VectorDoubleSqrt(fourXSqr));
            SIMDVectorDouble numerator = VectorDoubleAdd(VectorDoubleSet(0.0, matrix.v[0][1], matrix.v[0][2], 0.0), VectorDoubleSet(0.0, matrix.v[1][0], matrix.v[2][0], 0.0));
            numerator = VectorDoubleSelect(numerator, fourXSqr, s_DoubleSIMDMaskX);
            numerator = VectorDoubleSelect(numerator, VectorDoubleSubtract(VectorDoubleSplatY(matrix.v[2]), VectorDoubleSplatZ(matrix.v[1])), s_DoubleSIMDMaskW);
            return VectorDoubleMultiply(numerator, inv4x);
        }
        const SIMDVectorDouble fourYSqr = VectorDoubleAdd(omr22, dif10);
        const SIMDVectorDouble inv4y = VectorDoubleDivide(s_DoubleSIMDOneHalf, VectorDoubleSqrt(fourYSqr));
        SIMDVectorDouble numerator = VectorDoubleAdd(VectorDoubleSet(matrix.v[0][1], 0.0, matrix.v[1][2], 0.0), VectorDoubleSet(matrix.v[1][0], 0.0, matrix.v[2][1], 0.0));
        numerator = VectorDoubleSelect(numerator, fourYSqr, s_DoubleSIMDMaskY);
        numerator = VectorDoubleSelect(numerator, VectorDoubleSubtract(VectorDoubleSplatZ(matrix.v[0]), VectorDoubleSplatX(matrix.v[2])), s_DoubleSIMDMaskW);
        return VectorDoubleMultiply(numerator, inv4y);
    }
    const SIMDVectorDouble sum10 = VectorDoubleAdd(VectorDoubleSplatY(matrix.v[1]), VectorDoubleSplatX(matrix.v[0]));
    const SIMDVectorDouble opr22 = VectorDoubleAdd(s_DoubleSIMDOne, r22);
    if(VectorDoubleGetX(sum10) <= 0.0){
        const SIMDVectorDouble fourZSqr = VectorDoubleSubtract(opr22, sum10);
        const SIMDVectorDouble inv4z = VectorDoubleDivide(s_DoubleSIMDOneHalf, VectorDoubleSqrt(fourZSqr));
        SIMDVectorDouble numerator = VectorDoubleAdd(VectorDoubleSet(matrix.v[0][2], matrix.v[1][2], 0.0, 0.0), VectorDoubleSet(matrix.v[2][0], matrix.v[2][1], 0.0, 0.0));
        numerator = VectorDoubleSelect(numerator, fourZSqr, s_DoubleSIMDMaskZ);
        numerator = VectorDoubleSelect(numerator, VectorDoubleSubtract(VectorDoubleSplatX(matrix.v[1]), VectorDoubleSplatY(matrix.v[0])), s_DoubleSIMDMaskW);
        return VectorDoubleMultiply(numerator, inv4z);
    }
    const SIMDVectorDouble fourWSqr = VectorDoubleAdd(opr22, sum10);
    const SIMDVectorDouble inv4w = VectorDoubleDivide(s_DoubleSIMDOneHalf, VectorDoubleSqrt(fourWSqr));
    SIMDVectorDouble numerator = VectorDoubleSubtract(VectorDoubleSet(matrix.v[2][1], matrix.v[0][2], matrix.v[1][0], 0.0), VectorDoubleSet(matrix.v[1][2], matrix.v[2][0], matrix.v[0][1], 0.0));
    numerator = VectorDoubleSelect(numerator, fourWSqr, s_DoubleSIMDMaskW);
    return VectorDoubleMultiply(numerator, inv4w);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void NWB_SIMD_CALL QuaternionToAxisAngle(SIMDVectorSingle& outAxis, f32& outAngle, SIMDVectorSingle q)noexcept{
    outAxis = q;
    const SIMDVectorSingle angle = VectorScale(VectorACos(VectorSplatW(q)), 2.0f);
    outAngle = VectorGetX(angle);
}

NWB_INLINE void NWB_SIMD_CALL QuaternionDoubleToAxisAngle(SIMDVectorDouble& outAxis, f64& outAngle, SIMDVectorDouble q)noexcept{
    outAxis = q;
    const SIMDVectorDouble angle = VectorDoubleScale(SIMDVectorDoubleDetail::ACosReplicated(VectorDoubleSplatW(q)), 2.0);
    outAngle = VectorDoubleGetX(angle);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

