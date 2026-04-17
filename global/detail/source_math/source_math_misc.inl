// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline bool MathCallConv QuaternionEqual
(
    VectorArg Q1,
    VectorArg Q2
)noexcept
{
    return Vector4Equal(Q1, Q2);
}

inline bool MathCallConv QuaternionNotEqual
(
    VectorArg Q1,
    VectorArg Q2
)noexcept
{
    return Vector4NotEqual(Q1, Q2);
}

inline bool MathCallConv QuaternionIsNaN(VectorArg Q)noexcept
{
    return Vector4IsNaN(Q);
}

inline bool MathCallConv QuaternionIsInfinite(VectorArg Q)noexcept
{
    return Vector4IsInfinite(Q);
}

inline bool MathCallConv QuaternionIsIdentity(VectorArg Q)noexcept
{
    return Vector4Equal(Q, g_IdentityC3.v);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Vector MathCallConv QuaternionDot
(
    VectorArg Q1,
    VectorArg Q2
)noexcept
{
    return Vector4Dot(Q1, Q2);
}

inline Vector MathCallConv QuaternionMultiply
(
    VectorArg Q1,
    VectorArg Q2
)noexcept
{
    // Returns Q2 * Q1. Under NWB's column-vector convention that means a caller
    // can compose rotations in application order: apply Q1 first, then Q2.

    // [ (Q2.w * Q1.x) + (Q2.x * Q1.w) + (Q2.y * Q1.z) - (Q2.z * Q1.y),
    //   (Q2.w * Q1.y) - (Q2.x * Q1.z) + (Q2.y * Q1.w) + (Q2.z * Q1.x),
    //   (Q2.w * Q1.z) + (Q2.x * Q1.y) - (Q2.y * Q1.x) + (Q2.z * Q1.w),
    //   (Q2.w * Q1.w) - (Q2.x * Q1.x) - (Q2.y * Q1.y) - (Q2.z * Q1.z) ]

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            (Q2.vector4_f32[3] * Q1.vector4_f32[0]) + (Q2.vector4_f32[0] * Q1.vector4_f32[3]) + (Q2.vector4_f32[1] * Q1.vector4_f32[2]) - (Q2.vector4_f32[2] * Q1.vector4_f32[1]),
            (Q2.vector4_f32[3] * Q1.vector4_f32[1]) - (Q2.vector4_f32[0] * Q1.vector4_f32[2]) + (Q2.vector4_f32[1] * Q1.vector4_f32[3]) + (Q2.vector4_f32[2] * Q1.vector4_f32[0]),
            (Q2.vector4_f32[3] * Q1.vector4_f32[2]) + (Q2.vector4_f32[0] * Q1.vector4_f32[1]) - (Q2.vector4_f32[1] * Q1.vector4_f32[0]) + (Q2.vector4_f32[2] * Q1.vector4_f32[3]),
            (Q2.vector4_f32[3] * Q1.vector4_f32[3]) - (Q2.vector4_f32[0] * Q1.vector4_f32[0]) - (Q2.vector4_f32[1] * Q1.vector4_f32[1]) - (Q2.vector4_f32[2] * Q1.vector4_f32[2])
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const VectorF32 ControlWZYX = { { { 1.0f, -1.0f, 1.0f, -1.0f } } };
    static const VectorF32 ControlZWXY = { { { 1.0f, 1.0f, -1.0f, -1.0f } } };
    static const VectorF32 ControlYXWZ = { { { -1.0f, 1.0f, 1.0f, -1.0f } } };

    float32x2_t Q2L = vget_low_f32(Q2);
    float32x2_t Q2H = vget_high_f32(Q2);

    float32x4_t Q2X = vdupq_lane_f32(Q2L, 0);
    float32x4_t Q2Y = vdupq_lane_f32(Q2L, 1);
    float32x4_t Q2Z = vdupq_lane_f32(Q2H, 0);
    Vector vResult = vmulq_lane_f32(Q1, Q2H, 1);

    // Mul by Q1WZYX
    float32x4_t vTemp = vrev64q_f32(Q1);
    vTemp = vcombine_f32(vget_high_f32(vTemp), vget_low_f32(vTemp));
    Q2X = vmulq_f32(Q2X, vTemp);
    vResult = vmlaq_f32(vResult, Q2X, ControlWZYX);

    // Mul by Q1ZWXY
    vTemp = vreinterpretq_f32_u32(vrev64q_u32(vreinterpretq_u32_f32(vTemp)));
    Q2Y = vmulq_f32(Q2Y, vTemp);
    vResult = vmlaq_f32(vResult, Q2Y, ControlZWXY);

    // Mul by Q1YXWZ
    vTemp = vreinterpretq_f32_u32(vrev64q_u32(vreinterpretq_u32_f32(vTemp)));
    vTemp = vcombine_f32(vget_high_f32(vTemp), vget_low_f32(vTemp));
    Q2Z = vmulq_f32(Q2Z, vTemp);
    vResult = vmlaq_f32(vResult, Q2Z, ControlYXWZ);
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    static const VectorF32 ControlWZYX = { { { 1.0f, -1.0f, 1.0f, -1.0f } } };
    static const VectorF32 ControlZWXY = { { { 1.0f, 1.0f, -1.0f, -1.0f } } };
    static const VectorF32 ControlYXWZ = { { { -1.0f, 1.0f, 1.0f, -1.0f } } };
    // Copy to SSE registers and use as few as possible for x86
    Vector Q2X = Q2;
    Vector Q2Y = Q2;
    Vector Q2Z = Q2;
    Vector vResult = Q2;
    // Splat with one instruction
    vResult = MATH_PERMUTE_PS(vResult, _MM_SHUFFLE(3, 3, 3, 3));
    Q2X = MATH_PERMUTE_PS(Q2X, _MM_SHUFFLE(0, 0, 0, 0));
    Q2Y = MATH_PERMUTE_PS(Q2Y, _MM_SHUFFLE(1, 1, 1, 1));
    Q2Z = MATH_PERMUTE_PS(Q2Z, _MM_SHUFFLE(2, 2, 2, 2));
    // Retire Q1 and perform Q1*Q2W
    vResult = _mm_mul_ps(vResult, Q1);
    Vector Q1Shuffle = Q1;
    // Shuffle the copies of Q1
    Q1Shuffle = MATH_PERMUTE_PS(Q1Shuffle, _MM_SHUFFLE(0, 1, 2, 3));
    // Mul by Q1WZYX
    Q2X = _mm_mul_ps(Q2X, Q1Shuffle);
    Q1Shuffle = MATH_PERMUTE_PS(Q1Shuffle, _MM_SHUFFLE(2, 3, 0, 1));
    // Flip the signs on y and z
    vResult = MATH_FMADD_PS(Q2X, ControlWZYX, vResult);
    // Mul by Q1ZWXY
    Q2Y = _mm_mul_ps(Q2Y, Q1Shuffle);
    Q1Shuffle = MATH_PERMUTE_PS(Q1Shuffle, _MM_SHUFFLE(0, 1, 2, 3));
    // Flip the signs on z and w
    Q2Y = _mm_mul_ps(Q2Y, ControlZWXY);
    // Mul by Q1YXWZ
    Q2Z = _mm_mul_ps(Q2Z, Q1Shuffle);
    // Flip the signs on x and w
    Q2Y = MATH_FMADD_PS(Q2Z, ControlYXWZ, Q2Y);
    vResult = _mm_add_ps(vResult, Q2Y);
    return vResult;
#endif
}

inline Vector MathCallConv QuaternionLengthSq(VectorArg Q)noexcept
{
    return Vector4LengthSq(Q);
}

inline Vector MathCallConv QuaternionReciprocalLength(VectorArg Q)noexcept
{
    return Vector4ReciprocalLength(Q);
}

inline Vector MathCallConv QuaternionLength(VectorArg Q)noexcept
{
    return Vector4Length(Q);
}

inline Vector MathCallConv QuaternionNormalizeEst(VectorArg Q)noexcept
{
    return Vector4NormalizeEst(Q);
}

inline Vector MathCallConv QuaternionNormalize(VectorArg Q)noexcept
{
    return Vector4Normalize(Q);
}

inline Vector MathCallConv QuaternionConjugate(VectorArg Q)noexcept
{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 Result = { { {
            -Q.vector4_f32[0],
            -Q.vector4_f32[1],
            -Q.vector4_f32[2],
            Q.vector4_f32[3]
        } } };
    return Result.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const VectorF32 NegativeOne3 = { { { -1.0f, -1.0f, -1.0f, 1.0f } } };
    return vmulq_f32(Q, NegativeOne3.v);
#elif defined(_MATH_SSE_INTRINSICS_)
    static const VectorF32 NegativeOne3 = { { { -1.0f, -1.0f, -1.0f, 1.0f } } };
    return _mm_mul_ps(Q, NegativeOne3);
#endif
}

inline Vector MathCallConv QuaternionInverse(VectorArg Q)noexcept
{
    Vector L = Vector4LengthSq(Q);
    Vector Conjugate = QuaternionConjugate(Q);

    Vector Control = VectorLessOrEqual(L, g_Epsilon.v);

    Vector Result = VectorDivide(Conjugate, L);

    Result = VectorSelect(Result, g_Zero, Control);

    return Result;
}

inline Vector MathCallConv QuaternionLn(VectorArg Q)noexcept
{
    static const VectorF32 OneMinusEpsilon = { { { 1.0f - 0.00001f, 1.0f - 0.00001f, 1.0f - 0.00001f, 1.0f - 0.00001f } } };

    Vector QW = VectorSplatW(Q);
    Vector Q0 = VectorSelect(g_Select1110.v, Q, g_Select1110.v);

    Vector ControlW = VectorInBounds(QW, OneMinusEpsilon.v);

    Vector Theta = VectorACos(QW);
    Vector SinTheta = VectorSin(Theta);

    Vector S = VectorDivide(Theta, SinTheta);

    Vector Result = VectorMultiply(Q0, S);
    Result = VectorSelect(Q0, Result, ControlW);

    return Result;
}

inline Vector MathCallConv QuaternionExp(VectorArg Q)noexcept
{
    Vector Theta = Vector3Length(Q);

    Vector SinTheta, CosTheta;
    VectorSinCos(&SinTheta, &CosTheta, Theta);

    Vector S = VectorDivide(SinTheta, Theta);

    Vector Result = VectorMultiply(Q, S);

    const Vector Zero = VectorZero();
    Vector Control = VectorNearEqual(Theta, Zero, g_Epsilon.v);
    Result = VectorSelect(Result, Q, Control);

    Result = VectorSelect(CosTheta, Result, g_Select1110.v);

    return Result;
}

inline Vector MathCallConv QuaternionSlerp
(
    VectorArg Q0,
    VectorArg Q1,
    float    t
)noexcept
{
    Vector T = VectorReplicate(t);
    return QuaternionSlerpV(Q0, Q1, T);
}

inline Vector MathCallConv QuaternionSlerpV
(
    VectorArg Q0,
    VectorArg Q1,
    VectorArg T
)noexcept
{
    assert((VectorGetY(T) == VectorGetX(T)) && (VectorGetZ(T) == VectorGetX(T)) && (VectorGetW(T) == VectorGetX(T)));

    // Result = Q0 * sin((1.0 - t) * Omega) / sin(Omega) + Q1 * sin(t * Omega) / sin(Omega)

#if defined(_MATH_NO_INTRINSICS_) || defined(_MATH_ARM_NEON_INTRINSICS_)

    const VectorF32 OneMinusEpsilon = { { { 1.0f - 0.00001f, 1.0f - 0.00001f, 1.0f - 0.00001f, 1.0f - 0.00001f } } };

    Vector CosOmega = QuaternionDot(Q0, Q1);

    const Vector Zero = VectorZero();
    Vector Control = VectorLess(CosOmega, Zero);
    Vector Sign = VectorSelect(g_One.v, g_NegativeOne.v, Control);

    CosOmega = VectorMultiply(CosOmega, Sign);

    Control = VectorLess(CosOmega, OneMinusEpsilon);

    Vector SinOmega = VectorNegativeMultiplySubtract(CosOmega, CosOmega, g_One.v);
    SinOmega = VectorSqrt(SinOmega);

    Vector Omega = VectorATan2(SinOmega, CosOmega);

    Vector SignMask = VectorSplatSignMask();
    Vector V01 = VectorShiftLeft(T, Zero, 2);
    SignMask = VectorShiftLeft(SignMask, Zero, 3);
    V01 = VectorXorInt(V01, SignMask);
    V01 = VectorAdd(g_IdentityC0.v, V01);

    Vector InvSinOmega = VectorReciprocal(SinOmega);

    Vector S0 = VectorMultiply(V01, Omega);
    S0 = VectorSin(S0);
    S0 = VectorMultiply(S0, InvSinOmega);

    S0 = VectorSelect(V01, S0, Control);

    Vector S1 = VectorSplatY(S0);
    S0 = VectorSplatX(S0);

    S1 = VectorMultiply(S1, Sign);

    Vector Result = VectorMultiply(Q0, S0);
    Result = VectorMultiplyAdd(Q1, S1, Result);

    return Result;

#elif defined(_MATH_SSE_INTRINSICS_)
    static const VectorF32 OneMinusEpsilon = { { { 1.0f - 0.00001f, 1.0f - 0.00001f, 1.0f - 0.00001f, 1.0f - 0.00001f } } };
    static const VectorU32 SignMask2 = { { { 0x80000000, 0x00000000, 0x00000000, 0x00000000 } } };

    Vector CosOmega = QuaternionDot(Q0, Q1);

    const Vector Zero = VectorZero();
    Vector Control = VectorLess(CosOmega, Zero);
    Vector Sign = VectorSelect(g_One, g_NegativeOne, Control);

    CosOmega = _mm_mul_ps(CosOmega, Sign);

    Control = VectorLess(CosOmega, OneMinusEpsilon);

    Vector SinOmega = _mm_mul_ps(CosOmega, CosOmega);
    SinOmega = _mm_sub_ps(g_One, SinOmega);
    SinOmega = _mm_sqrt_ps(SinOmega);

    Vector Omega = VectorATan2(SinOmega, CosOmega);

    Vector V01 = MATH_PERMUTE_PS(T, _MM_SHUFFLE(2, 3, 0, 1));
    V01 = _mm_and_ps(V01, g_MaskXY);
    V01 = _mm_xor_ps(V01, SignMask2);
    V01 = _mm_add_ps(g_IdentityC0, V01);

    Vector S0 = _mm_mul_ps(V01, Omega);
    S0 = VectorSin(S0);
    S0 = _mm_div_ps(S0, SinOmega);

    S0 = VectorSelect(V01, S0, Control);

    Vector S1 = VectorSplatY(S0);
    S0 = VectorSplatX(S0);

    S1 = _mm_mul_ps(S1, Sign);
    Vector Result = _mm_mul_ps(Q0, S0);
    S1 = _mm_mul_ps(S1, Q1);
    Result = _mm_add_ps(Result, S1);
    return Result;
#endif
}

inline Vector MathCallConv QuaternionSquad
(
    VectorArg Q0,
    VectorArg Q1,
    VectorArg Q2,
    VectorArg2 Q3,
    float    t
)noexcept
{
    Vector T = VectorReplicate(t);
    return QuaternionSquadV(Q0, Q1, Q2, Q3, T);
}

inline Vector MathCallConv QuaternionSquadV
(
    VectorArg Q0,
    VectorArg Q1,
    VectorArg Q2,
    VectorArg2 Q3,
    VectorArg3 T
)noexcept
{
    assert((VectorGetY(T) == VectorGetX(T)) && (VectorGetZ(T) == VectorGetX(T)) && (VectorGetW(T) == VectorGetX(T)));

    Vector TP = T;
    const Vector Two = VectorSplatConstant(2, 0);

    Vector Q03 = QuaternionSlerpV(Q0, Q3, T);
    Vector Q12 = QuaternionSlerpV(Q1, Q2, T);

    TP = VectorNegativeMultiplySubtract(TP, TP, TP);
    TP = VectorMultiply(TP, Two);

    Vector Result = QuaternionSlerpV(Q03, Q12, TP);

    return Result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline void MathCallConv QuaternionSquadSetup
(
    Vector* pA,
    Vector* pB,
    Vector* pC,
    VectorArg  Q0,
    VectorArg  Q1,
    VectorArg  Q2,
    VectorArg2  Q3
)noexcept
{
    assert(pA);
    assert(pB);
    assert(pC);

    Vector LS12 = QuaternionLengthSq(VectorAdd(Q1, Q2));
    Vector LD12 = QuaternionLengthSq(VectorSubtract(Q1, Q2));
    Vector SQ2 = VectorNegate(Q2);

    Vector Control1 = VectorLess(LS12, LD12);
    SQ2 = VectorSelect(Q2, SQ2, Control1);

    Vector LS01 = QuaternionLengthSq(VectorAdd(Q0, Q1));
    Vector LD01 = QuaternionLengthSq(VectorSubtract(Q0, Q1));
    Vector SQ0 = VectorNegate(Q0);

    Vector LS23 = QuaternionLengthSq(VectorAdd(SQ2, Q3));
    Vector LD23 = QuaternionLengthSq(VectorSubtract(SQ2, Q3));
    Vector SQ3 = VectorNegate(Q3);

    Vector Control0 = VectorLess(LS01, LD01);
    Vector Control2 = VectorLess(LS23, LD23);

    SQ0 = VectorSelect(Q0, SQ0, Control0);
    SQ3 = VectorSelect(Q3, SQ3, Control2);

    Vector InvQ1 = QuaternionInverse(Q1);
    Vector InvQ2 = QuaternionInverse(SQ2);

    Vector LnQ0 = QuaternionLn(QuaternionMultiply(InvQ1, SQ0));
    Vector LnQ2 = QuaternionLn(QuaternionMultiply(InvQ1, SQ2));
    Vector LnQ1 = QuaternionLn(QuaternionMultiply(InvQ2, Q1));
    Vector LnQ3 = QuaternionLn(QuaternionMultiply(InvQ2, SQ3));

    const Vector NegativeOneQuarter = VectorSplatConstant(-1, 2);

    Vector ExpQ02 = VectorMultiply(VectorAdd(LnQ0, LnQ2), NegativeOneQuarter);
    Vector ExpQ13 = VectorMultiply(VectorAdd(LnQ1, LnQ3), NegativeOneQuarter);
    ExpQ02 = QuaternionExp(ExpQ02);
    ExpQ13 = QuaternionExp(ExpQ13);

    *pA = QuaternionMultiply(Q1, ExpQ02);
    *pB = QuaternionMultiply(SQ2, ExpQ13);
    *pC = SQ2;
}

inline Vector MathCallConv QuaternionBaryCentric
(
    VectorArg Q0,
    VectorArg Q1,
    VectorArg Q2,
    float    f,
    float    g
)noexcept
{
    float s = f + g;

    Vector Result;
    if ((s < 0.00001f) && (s > -0.00001f))
    {
        Result = Q0;
    }
    else
    {
        Vector Q01 = QuaternionSlerp(Q0, Q1, s);
        Vector Q02 = QuaternionSlerp(Q0, Q2, s);

        Result = QuaternionSlerp(Q01, Q02, g / s);
    }

    return Result;
}

inline Vector MathCallConv QuaternionBaryCentricV
(
    VectorArg Q0,
    VectorArg Q1,
    VectorArg Q2,
    VectorArg2 F,
    VectorArg3 G
)noexcept
{
    assert((VectorGetY(F) == VectorGetX(F)) && (VectorGetZ(F) == VectorGetX(F)) && (VectorGetW(F) == VectorGetX(F)));
    assert((VectorGetY(G) == VectorGetX(G)) && (VectorGetZ(G) == VectorGetX(G)) && (VectorGetW(G) == VectorGetX(G)));

    const Vector Epsilon = VectorSplatConstant(1, 16);

    Vector S = VectorAdd(F, G);

    Vector Result;
    if (Vector4InBounds(S, Epsilon))
    {
        Result = Q0;
    }
    else
    {
        Vector Q01 = QuaternionSlerpV(Q0, Q1, S);
        Vector Q02 = QuaternionSlerpV(Q0, Q2, S);
        Vector GS = VectorReciprocal(S);
        GS = VectorMultiply(G, GS);

        Result = QuaternionSlerpV(Q01, Q02, GS);
    }

    return Result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Vector MathCallConv QuaternionIdentity()noexcept
{
    return g_IdentityC3.v;
}

inline Vector MathCallConv QuaternionRotationRollPitchYaw
(
    float Pitch,
    float Yaw,
    float Roll
)noexcept
{
    Vector QYaw = QuaternionRotationNormal(g_IdentityC1.v, Yaw);
    Vector QPitch = QuaternionRotationNormal(g_IdentityC0.v, Pitch);
    Vector QRoll = QuaternionRotationNormal(g_IdentityC2.v, Roll);
    return QuaternionMultiply(QuaternionMultiply(QYaw, QPitch), QRoll);
}

inline Vector MathCallConv QuaternionRotationRollPitchYawFromVector
(
    VectorArg Angles // <Pitch, Yaw, Roll, 0>
)noexcept
{
    return QuaternionRotationRollPitchYaw(
        VectorGetX(Angles),
        VectorGetY(Angles),
        VectorGetZ(Angles)
    );
}

inline Vector MathCallConv QuaternionRotationNormal
(
    VectorArg NormalAxis,
    float    Angle
)noexcept
{
#if defined(_MATH_NO_INTRINSICS_) || defined(_MATH_ARM_NEON_INTRINSICS_)

    Vector N = VectorSelect(g_One.v, NormalAxis, g_Select1110.v);

    float SinV, CosV;
    ScalarSinCos(&SinV, &CosV, 0.5f * Angle);

    Vector Scale = VectorSet(SinV, SinV, SinV, CosV);
    return VectorMultiply(N, Scale);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector N = _mm_and_ps(NormalAxis, g_Mask3);
    N = _mm_or_ps(N, g_IdentityC3);
    Vector Scale = _mm_set_ps1(0.5f * Angle);
    Vector vSine;
    Vector vCosine;
    VectorSinCos(&vSine, &vCosine, Scale);
    Scale = _mm_and_ps(vSine, g_Mask3);
    vCosine = _mm_and_ps(vCosine, g_MaskW);
    Scale = _mm_or_ps(Scale, vCosine);
    N = _mm_mul_ps(N, Scale);
    return N;
#endif
}

inline Vector MathCallConv QuaternionRotationAxis
(
    VectorArg Axis,
    float    Angle
)noexcept
{
    assert(!Vector3Equal(Axis, VectorZero()));
    assert(!Vector3IsInfinite(Axis));

    Vector Normal = Vector3Normalize(Axis);
    Vector Q = QuaternionRotationNormal(Normal, Angle);
    return Q;
}

inline Vector MathCallConv QuaternionRotationMatrix(FXMMATRIX M)noexcept
{
#if defined(_MATH_NO_INTRINSICS_)

    VectorF32 q;
    const float r22 = M(2, 2);
    if (r22 <= 0.f)  // x^2 + y^2 >= z^2 + w^2
    {
        const float dif10 = M(1, 1) - M(0, 0);
        const float omr22 = 1.f - r22;
        if (dif10 <= 0.f)  // x^2 >= y^2
        {
            const float fourXSqr = omr22 - dif10;
            const float inv4x = 0.5f / sqrtf(fourXSqr);
            q.f[0] = fourXSqr * inv4x;
            q.f[1] = (M(0, 1) + M(1, 0)) * inv4x;
            q.f[2] = (M(0, 2) + M(2, 0)) * inv4x;
            q.f[3] = (M(2, 1) - M(1, 2)) * inv4x;
        }
        else  // y^2 >= x^2
        {
            const float fourYSqr = omr22 + dif10;
            const float inv4y = 0.5f / sqrtf(fourYSqr);
            q.f[0] = (M(0, 1) + M(1, 0)) * inv4y;
            q.f[1] = fourYSqr * inv4y;
            q.f[2] = (M(1, 2) + M(2, 1)) * inv4y;
            q.f[3] = (M(0, 2) - M(2, 0)) * inv4y;
        }
    }
    else  // z^2 + w^2 >= x^2 + y^2
    {
        const float sum10 = M(1, 1) + M(0, 0);
        const float opr22 = 1.f + r22;
        if (sum10 <= 0.f)  // z^2 >= w^2
        {
            const float fourZSqr = opr22 - sum10;
            const float inv4z = 0.5f / sqrtf(fourZSqr);
            q.f[0] = (M(0, 2) + M(2, 0)) * inv4z;
            q.f[1] = (M(1, 2) + M(2, 1)) * inv4z;
            q.f[2] = fourZSqr * inv4z;
            q.f[3] = (M(1, 0) - M(0, 1)) * inv4z;
        }
        else  // w^2 >= z^2
        {
            const float fourWSqr = opr22 + sum10;
            const float inv4w = 0.5f / sqrtf(fourWSqr);
            q.f[0] = (M(2, 1) - M(1, 2)) * inv4w;
            q.f[1] = (M(0, 2) - M(2, 0)) * inv4w;
            q.f[2] = (M(1, 0) - M(0, 1)) * inv4w;
            q.f[3] = fourWSqr * inv4w;
        }
    }
    return q.v;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const VectorF32 MATH_PMMP = { { { +1.0f, -1.0f, -1.0f, +1.0f } } };
    static const VectorF32 MATH_MPMP = { { { -1.0f, +1.0f, -1.0f, +1.0f } } };
    static const VectorF32 MATH_MMPP = { { { -1.0f, -1.0f, +1.0f, +1.0f } } };
    static const VectorU32 Select0110 = { { { MATH_SELECT_0, MATH_SELECT_1, MATH_SELECT_1, MATH_SELECT_0 } } };
    static const VectorU32 Select0010 = { { { MATH_SELECT_0, MATH_SELECT_0, MATH_SELECT_1, MATH_SELECT_0 } } };

    float32x4_t r0 = M.r[0];
    float32x4_t r1 = M.r[1];
    float32x4_t r2 = M.r[2];

    float32x4_t r00 = vdupq_lane_f32(vget_low_f32(r0), 0);
    float32x4_t r11 = vdupq_lane_f32(vget_low_f32(r1), 1);
    float32x4_t r22 = vdupq_lane_f32(vget_high_f32(r2), 0);

    // x^2 >= y^2 equivalent to r11 - r00 <= 0
    float32x4_t r11mr00 = vsubq_f32(r11, r00);
    uint32x4_t x2gey2 = vcleq_f32(r11mr00, g_Zero);

    // z^2 >= w^2 equivalent to r11 + r00 <= 0
    float32x4_t r11pr00 = vaddq_f32(r11, r00);
    uint32x4_t z2gew2 = vcleq_f32(r11pr00, g_Zero);

    // x^2 + y^2 >= z^2 + w^2 equivalent to r22 <= 0
    uint32x4_t x2py2gez2pw2 = vcleq_f32(r22, g_Zero);

    // (4*x^2, 4*y^2, 4*z^2, 4*w^2)
    float32x4_t t0 = vmulq_f32(MATH_PMMP, r00);
    float32x4_t x2y2z2w2 = vmlaq_f32(t0, MATH_MPMP, r11);
    x2y2z2w2 = vmlaq_f32(x2y2z2w2, MATH_MMPP, r22);
    x2y2z2w2 = vaddq_f32(x2y2z2w2, g_One);

    // (r01, r02, r12, r11)
    t0 = vextq_f32(r0, r0, 1);
    float32x4_t t1 = vextq_f32(r1, r1, 1);
    t0 = vcombine_f32(vget_low_f32(t0), vrev64_f32(vget_low_f32(t1)));

    // (r10, r20, r21, r10)
    t1 = vextq_f32(r2, r2, 3);
    float32x4_t r10 = vdupq_lane_f32(vget_low_f32(r1), 0);
    t1 = vbslq_f32(Select0110, t1, r10);

    // (4*x*y, 4*x*z, 4*y*z, unused)
    float32x4_t xyxzyz = vaddq_f32(t0, t1);

    // (r21, r20, r10, r10)
    t0 = vcombine_f32(vrev64_f32(vget_low_f32(r2)), vget_low_f32(r10));

    // (r12, r02, r01, r12)
    float32x4_t t2 = vcombine_f32(vrev64_f32(vget_high_f32(r0)), vrev64_f32(vget_low_f32(r0)));
    float32x4_t t3 = vdupq_lane_f32(vget_high_f32(r1), 0);
    t1 = vbslq_f32(Select0110, t2, t3);

    // (4*x*w, 4*y*w, 4*z*w, unused)
    float32x4_t xwywzw = vsubq_f32(t0, t1);
    xwywzw = vmulq_f32(MATH_MPMP, xwywzw);

    // (4*x*x, 4*x*y, 4*x*z, 4*x*w)
    t0 = vextq_f32(xyxzyz, xyxzyz, 3);
    t1 = vbslq_f32(Select0110, t0, x2y2z2w2);
    t2 = vdupq_lane_f32(vget_low_f32(xwywzw), 0);
    float32x4_t tensor0 = vbslq_f32(g_Select1110, t1, t2);

    // (4*y*x, 4*y*y, 4*y*z, 4*y*w)
    t0 = vbslq_f32(g_Select1011, xyxzyz, x2y2z2w2);
    t1 = vdupq_lane_f32(vget_low_f32(xwywzw), 1);
    float32x4_t tensor1 = vbslq_f32(g_Select1110, t0, t1);

    // (4*z*x, 4*z*y, 4*z*z, 4*z*w)
    t0 = vextq_f32(xyxzyz, xyxzyz, 1);
    t1 = vcombine_f32(vget_low_f32(t0), vrev64_f32(vget_high_f32(xwywzw)));
    float32x4_t tensor2 = vbslq_f32(Select0010, x2y2z2w2, t1);

    // (4*w*x, 4*w*y, 4*w*z, 4*w*w)
    float32x4_t tensor3 = vbslq_f32(g_Select1110, xwywzw, x2y2z2w2);

    // Select the tensor-product candidate with the largest magnitude.
    t0 = vbslq_f32(x2gey2, tensor0, tensor1);
    t1 = vbslq_f32(z2gew2, tensor2, tensor3);
    t2 = vbslq_f32(x2py2gez2pw2, t0, t1);

    // Normalize the selected candidate. No division by zero is possible
    // because the quaternion is unit-length.
    t0 = Vector4Length(t2);
    return VectorDivide(t2, t0);
#elif defined(_MATH_SSE_INTRINSICS_)
    static const VectorF32 MATH_PMMP = { { { +1.0f, -1.0f, -1.0f, +1.0f } } };
    static const VectorF32 MATH_MPMP = { { { -1.0f, +1.0f, -1.0f, +1.0f } } };
    static const VectorF32 MATH_MMPP = { { { -1.0f, -1.0f, +1.0f, +1.0f } } };

    Vector r0 = M.r[0];  // (r00, r01, r02, 0)
    Vector r1 = M.r[1];  // (r10, r11, r12, 0)
    Vector r2 = M.r[2];  // (r20, r21, r22, 0)

    // (r00, r00, r00, r00)
    Vector r00 = MATH_PERMUTE_PS(r0, _MM_SHUFFLE(0, 0, 0, 0));
    // (r11, r11, r11, r11)
    Vector r11 = MATH_PERMUTE_PS(r1, _MM_SHUFFLE(1, 1, 1, 1));
    // (r22, r22, r22, r22)
    Vector r22 = MATH_PERMUTE_PS(r2, _MM_SHUFFLE(2, 2, 2, 2));

    // x^2 >= y^2 equivalent to r11 - r00 <= 0
    // (r11 - r00, r11 - r00, r11 - r00, r11 - r00)
    Vector r11mr00 = _mm_sub_ps(r11, r00);
    Vector x2gey2 = _mm_cmple_ps(r11mr00, g_Zero);

    // z^2 >= w^2 equivalent to r11 + r00 <= 0
    // (r11 + r00, r11 + r00, r11 + r00, r11 + r00)
    Vector r11pr00 = _mm_add_ps(r11, r00);
    Vector z2gew2 = _mm_cmple_ps(r11pr00, g_Zero);

    // x^2 + y^2 >= z^2 + w^2 equivalent to r22 <= 0
    Vector x2py2gez2pw2 = _mm_cmple_ps(r22, g_Zero);

    // (4*x^2, 4*y^2, 4*z^2, 4*w^2)
    Vector t0 = MATH_FMADD_PS(MATH_PMMP, r00, g_One);
    Vector t1 = _mm_mul_ps(MATH_MPMP, r11);
    Vector t2 = MATH_FMADD_PS(MATH_MMPP, r22, t0);
    Vector x2y2z2w2 = _mm_add_ps(t1, t2);

    // (r01, r02, r12, r11)
    t0 = _mm_shuffle_ps(r0, r1, _MM_SHUFFLE(1, 2, 2, 1));
    // (r10, r10, r20, r21)
    t1 = _mm_shuffle_ps(r1, r2, _MM_SHUFFLE(1, 0, 0, 0));
    // (r10, r20, r21, r10)
    t1 = MATH_PERMUTE_PS(t1, _MM_SHUFFLE(1, 3, 2, 0));
    // (4*x*y, 4*x*z, 4*y*z, unused)
    Vector xyxzyz = _mm_add_ps(t0, t1);

    // (r21, r20, r10, r10)
    t0 = _mm_shuffle_ps(r2, r1, _MM_SHUFFLE(0, 0, 0, 1));
    // (r12, r12, r02, r01)
    t1 = _mm_shuffle_ps(r1, r0, _MM_SHUFFLE(1, 2, 2, 2));
    // (r12, r02, r01, r12)
    t1 = MATH_PERMUTE_PS(t1, _MM_SHUFFLE(1, 3, 2, 0));
    // (4*x*w, 4*y*w, 4*z*w, unused)
    Vector xwywzw = _mm_sub_ps(t0, t1);
    xwywzw = _mm_mul_ps(MATH_MPMP, xwywzw);

    // (4*x^2, 4*y^2, 4*x*y, unused)
    t0 = _mm_shuffle_ps(x2y2z2w2, xyxzyz, _MM_SHUFFLE(0, 0, 1, 0));
    // (4*z^2, 4*w^2, 4*z*w, unused)
    t1 = _mm_shuffle_ps(x2y2z2w2, xwywzw, _MM_SHUFFLE(0, 2, 3, 2));
    // (4*x*z, 4*y*z, 4*x*w, 4*y*w)
    t2 = _mm_shuffle_ps(xyxzyz, xwywzw, _MM_SHUFFLE(1, 0, 2, 1));

    // (4*x*x, 4*x*y, 4*x*z, 4*x*w)
    Vector tensor0 = _mm_shuffle_ps(t0, t2, _MM_SHUFFLE(2, 0, 2, 0));
    // (4*y*x, 4*y*y, 4*y*z, 4*y*w)
    Vector tensor1 = _mm_shuffle_ps(t0, t2, _MM_SHUFFLE(3, 1, 1, 2));
    // (4*z*x, 4*z*y, 4*z*z, 4*z*w)
    Vector tensor2 = _mm_shuffle_ps(t2, t1, _MM_SHUFFLE(2, 0, 1, 0));
    // (4*w*x, 4*w*y, 4*w*z, 4*w*w)
    Vector tensor3 = _mm_shuffle_ps(t2, t1, _MM_SHUFFLE(1, 2, 3, 2));

    // Select the tensor-product candidate with the largest magnitude.
    t0 = _mm_and_ps(x2gey2, tensor0);
    t1 = _mm_andnot_ps(x2gey2, tensor1);
    t0 = _mm_or_ps(t0, t1);
    t1 = _mm_and_ps(z2gew2, tensor2);
    t2 = _mm_andnot_ps(z2gew2, tensor3);
    t1 = _mm_or_ps(t1, t2);
    t0 = _mm_and_ps(x2py2gez2pw2, t0);
    t1 = _mm_andnot_ps(x2py2gez2pw2, t1);
    t2 = _mm_or_ps(t0, t1);

    // Normalize the selected candidate. No division by zero is possible
    // because the quaternion is unit-length.
    t0 = Vector4Length(t2);
    return _mm_div_ps(t2, t0);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline void MathCallConv QuaternionToAxisAngle
(
    Vector* pAxis,
    float* pAngle,
    VectorArg  Q
)noexcept
{
    assert(pAxis);
    assert(pAngle);

    *pAxis = Q;

    *pAngle = 2.0f * ScalarACos(VectorGetW(Q));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline bool MathCallConv PlaneEqual
(
    VectorArg P1,
    VectorArg P2
)noexcept
{
    return Vector4Equal(P1, P2);
}

inline bool MathCallConv PlaneNearEqual
(
    VectorArg P1,
    VectorArg P2,
    VectorArg Epsilon
)noexcept
{
    Vector NP1 = PlaneNormalize(P1);
    Vector NP2 = PlaneNormalize(P2);
    return Vector4NearEqual(NP1, NP2, Epsilon);
}

inline bool MathCallConv PlaneNotEqual
(
    VectorArg P1,
    VectorArg P2
)noexcept
{
    return Vector4NotEqual(P1, P2);
}

inline bool MathCallConv PlaneIsNaN(VectorArg P)noexcept
{
    return Vector4IsNaN(P);
}

inline bool MathCallConv PlaneIsInfinite(VectorArg P)noexcept
{
    return Vector4IsInfinite(P);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Vector MathCallConv PlaneDot
(
    VectorArg P,
    VectorArg V
)noexcept
{
    return Vector4Dot(P, V);
}

inline Vector MathCallConv PlaneDotCoord
(
    VectorArg P,
    VectorArg V
)noexcept
{
    // Result = P[0] * V[0] + P[1] * V[1] + P[2] * V[2] + P[3]

    Vector V3 = VectorSelect(g_One.v, V, g_Select1110.v);
    Vector Result = Vector4Dot(P, V3);
    return Result;
}

inline Vector MathCallConv PlaneDotNormal
(
    VectorArg P,
    VectorArg V
)noexcept
{
    return Vector3Dot(P, V);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Vector MathCallConv PlaneNormalizeEst(VectorArg P)noexcept
{
#if defined(_MATH_NO_INTRINSICS_) || defined(_MATH_ARM_NEON_INTRINSICS_)

    Vector Result = Vector3ReciprocalLengthEst(P);
    return VectorMultiply(P, Result);

#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vTemp = _mm_dp_ps(P, P, 0x7f);
    Vector vResult = _mm_rsqrt_ps(vTemp);
    return _mm_mul_ps(vResult, P);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product
    Vector vDot = _mm_mul_ps(P, P);
    // x=Dot.y, y=Dot.z
    Vector vTemp = MATH_PERMUTE_PS(vDot, _MM_SHUFFLE(2, 1, 2, 1));
    // Result.x = x+y
    vDot = _mm_add_ss(vDot, vTemp);
    // x=Dot.z
    vTemp = MATH_PERMUTE_PS(vTemp, _MM_SHUFFLE(1, 1, 1, 1));
    // Result.x = (x+y)+z
    vDot = _mm_add_ss(vDot, vTemp);
    // Splat x
    vDot = MATH_PERMUTE_PS(vDot, _MM_SHUFFLE(0, 0, 0, 0));
    // Get the reciprocal
    vDot = _mm_rsqrt_ps(vDot);
    // Get the reciprocal
    vDot = _mm_mul_ps(vDot, P);
    return vDot;
#endif
}

inline Vector MathCallConv PlaneNormalize(VectorArg P)noexcept
{
#if defined(_MATH_NO_INTRINSICS_)
    float fLengthSq = sqrtf((P.vector4_f32[0] * P.vector4_f32[0]) + (P.vector4_f32[1] * P.vector4_f32[1]) + (P.vector4_f32[2] * P.vector4_f32[2]));
    // Prevent divide by zero
    if (fLengthSq > 0)
    {
        fLengthSq = 1.0f / fLengthSq;
    }
    VectorF32 vResult = { { {
            P.vector4_f32[0] * fLengthSq,
            P.vector4_f32[1] * fLengthSq,
            P.vector4_f32[2] * fLengthSq,
            P.vector4_f32[3] * fLengthSq
        } } };
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    Vector vLength = Vector3ReciprocalLength(P);
    return VectorMultiply(P, vLength);
#elif defined(_MATH_SSE4_INTRINSICS_)
    Vector vLengthSq = _mm_dp_ps(P, P, 0x7f);
    // Prepare for the division
    Vector vResult = _mm_sqrt_ps(vLengthSq);
    // Failsafe on zero (Or epsilon) length planes
    // If the length is infinity, set the elements to zero
    vLengthSq = _mm_cmpneq_ps(vLengthSq, g_Infinity);
    // Reciprocal mul to perform the normalization
    vResult = _mm_div_ps(P, vResult);
    // Any that are infinity, set to zero
    vResult = _mm_and_ps(vResult, vLengthSq);
    return vResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    // Perform the dot product on x,y and z only
    Vector vLengthSq = _mm_mul_ps(P, P);
    Vector vTemp = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(2, 1, 2, 1));
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    vTemp = MATH_PERMUTE_PS(vTemp, _MM_SHUFFLE(1, 1, 1, 1));
    vLengthSq = _mm_add_ss(vLengthSq, vTemp);
    vLengthSq = MATH_PERMUTE_PS(vLengthSq, _MM_SHUFFLE(0, 0, 0, 0));
    // Prepare for the division
    Vector vResult = _mm_sqrt_ps(vLengthSq);
    // Failsafe on zero (Or epsilon) length planes
    // If the length is infinity, set the elements to zero
    vLengthSq = _mm_cmpneq_ps(vLengthSq, g_Infinity);
    // Reciprocal mul to perform the normalization
    vResult = _mm_div_ps(P, vResult);
    // Any that are infinity, set to zero
    vResult = _mm_and_ps(vResult, vLengthSq);
    return vResult;
#endif
}

inline Vector MathCallConv PlaneIntersectLine
(
    VectorArg P,
    VectorArg LinePoint1,
    VectorArg LinePoint2
)noexcept
{
    Vector V1 = Vector3Dot(P, LinePoint1);
    Vector V2 = Vector3Dot(P, LinePoint2);
    Vector D = VectorSubtract(V1, V2);

    Vector VT = PlaneDotCoord(P, LinePoint1);
    VT = VectorDivide(VT, D);

    Vector Point = VectorSubtract(LinePoint2, LinePoint1);
    Point = VectorMultiplyAdd(Point, VT, LinePoint1);

    const Vector Zero = VectorZero();
    Vector Control = VectorNearEqual(D, Zero, g_Epsilon.v);

    return VectorSelect(Point, g_QNaN.v, Control);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline void MathCallConv PlaneIntersectPlane
(
    Vector* pLinePoint1,
    Vector* pLinePoint2,
    VectorArg  P1,
    VectorArg  P2
)noexcept
{
    assert(pLinePoint1);
    assert(pLinePoint2);

    Vector V1 = Vector3Cross(P2, P1);

    Vector LengthSq = Vector3LengthSq(V1);

    Vector V2 = Vector3Cross(P2, V1);

    Vector P1W = VectorSplatW(P1);
    Vector Point = VectorMultiply(V2, P1W);

    Vector V3 = Vector3Cross(V1, P1);

    Vector P2W = VectorSplatW(P2);
    Point = VectorMultiplyAdd(V3, P2W, Point);

    Vector LinePoint1 = VectorDivide(Point, LengthSq);

    Vector LinePoint2 = VectorAdd(LinePoint1, V1);

    Vector Control = VectorLessOrEqual(LengthSq, g_Epsilon.v);
    *pLinePoint1 = VectorSelect(LinePoint1, g_QNaN.v, Control);
    *pLinePoint2 = VectorSelect(LinePoint2, g_QNaN.v, Control);
}

inline Vector MathCallConv PlaneTransform
(
    VectorArg P,
    FXMMATRIX ITM
)noexcept
{
    // `ITM.r[0..3]` are the inverse-transpose matrix columns here: Result = ITM * P.
    Vector W = VectorSplatW(P);
    Vector Z = VectorSplatZ(P);
    Vector Y = VectorSplatY(P);
    Vector X = VectorSplatX(P);

    Vector Result = VectorMultiply(W, ITM.r[3]);
    Result = VectorMultiplyAdd(Z, ITM.r[2], Result);
    Result = VectorMultiplyAdd(Y, ITM.r[1], Result);
    Result = VectorMultiplyAdd(X, ITM.r[0], Result);
    return Result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float4* MathCallConv PlaneTransformStream
(
    Float4*       pOutputStream,
    size_t          OutputStride,
    const Float4* pInputStream,
    size_t          InputStride,
    size_t          PlaneCount,
    FXMMATRIX       ITM
)noexcept
{
    return Vector4TransformStream(pOutputStream,
        OutputStride,
        pInputStream,
        InputStride,
        PlaneCount,
        ITM);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Vector MathCallConv PlaneFromPointNormal
(
    VectorArg Point,
    VectorArg Normal
)noexcept
{
    Vector W = Vector3Dot(Point, Normal);
    W = VectorNegate(W);
    return VectorSelect(W, Normal, g_Select1110.v);
}

inline Vector MathCallConv PlaneFromPoints
(
    VectorArg Point1,
    VectorArg Point2,
    VectorArg Point3
)noexcept
{
    Vector V21 = VectorSubtract(Point1, Point2);
    Vector V31 = VectorSubtract(Point1, Point3);

    Vector N = Vector3Cross(V21, V31);
    N = Vector3Normalize(N);

    Vector D = PlaneDotNormal(N, Point1);
    D = VectorNegate(D);

    Vector Result = VectorSelect(D, N, g_Select1110.v);

    return Result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline bool MathCallConv ColorEqual
(
    VectorArg C1,
    VectorArg C2
)noexcept
{
    return Vector4Equal(C1, C2);
}

inline bool MathCallConv ColorNotEqual
(
    VectorArg C1,
    VectorArg C2
)noexcept
{
    return Vector4NotEqual(C1, C2);
}

inline bool MathCallConv ColorGreater
(
    VectorArg C1,
    VectorArg C2
)noexcept
{
    return Vector4Greater(C1, C2);
}

inline bool MathCallConv ColorGreaterOrEqual
(
    VectorArg C1,
    VectorArg C2
)noexcept
{
    return Vector4GreaterOrEqual(C1, C2);
}

inline bool MathCallConv ColorLess
(
    VectorArg C1,
    VectorArg C2
)noexcept
{
    return Vector4Less(C1, C2);
}

inline bool MathCallConv ColorLessOrEqual
(
    VectorArg C1,
    VectorArg C2
)noexcept
{
    return Vector4LessOrEqual(C1, C2);
}

inline bool MathCallConv ColorIsNaN(VectorArg C)noexcept
{
    return Vector4IsNaN(C);
}

inline bool MathCallConv ColorIsInfinite(VectorArg C)noexcept
{
    return Vector4IsInfinite(C);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Vector MathCallConv ColorNegative(VectorArg vColor)noexcept
{
#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult = { { {
            1.0f - vColor.vector4_f32[0],
            1.0f - vColor.vector4_f32[1],
            1.0f - vColor.vector4_f32[2],
            vColor.vector4_f32[3]
        } } };
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t vTemp = veorq_u32(vreinterpretq_u32_f32(vColor), g_Negate3);
    return vaddq_f32(vreinterpretq_f32_u32(vTemp), g_One3);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Negate only x,y and z.
    Vector vTemp = _mm_xor_ps(vColor, g_Negate3);
    // Add 1,1,1,0 to -x,-y,-z,w
    return _mm_add_ps(vTemp, g_One3);
#endif
}

inline Vector MathCallConv ColorModulate
(
    VectorArg C1,
    VectorArg C2
)noexcept
{
    return VectorMultiply(C1, C2);
}

inline Vector MathCallConv ColorAdjustSaturation
(
    VectorArg vColor,
    float    fSaturation
)noexcept
{
    // Luminance = 0.2125f * C[0] + 0.7154f * C[1] + 0.0721f * C[2];
    // Result = (C - Luminance) * Saturation + Luminance;

    const VectorF32 gvLuminance = { { { 0.2125f, 0.7154f, 0.0721f, 0.0f } } };
#if defined(_MATH_NO_INTRINSICS_)
    float fLuminance = (vColor.vector4_f32[0] * gvLuminance.f[0]) + (vColor.vector4_f32[1] * gvLuminance.f[1]) + (vColor.vector4_f32[2] * gvLuminance.f[2]);
    Vector vResult;
    vResult.vector4_f32[0] = ((vColor.vector4_f32[0] - fLuminance) * fSaturation) + fLuminance;
    vResult.vector4_f32[1] = ((vColor.vector4_f32[1] - fLuminance) * fSaturation) + fLuminance;
    vResult.vector4_f32[2] = ((vColor.vector4_f32[2] - fLuminance) * fSaturation) + fLuminance;
    vResult.vector4_f32[3] = vColor.vector4_f32[3];
    return vResult;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    Vector vLuminance = Vector3Dot(vColor, gvLuminance);
    Vector vResult = vsubq_f32(vColor, vLuminance);
    vResult = vmlaq_n_f32(vLuminance, vResult, fSaturation);
    return vbslq_f32(g_Select1110, vResult, vColor);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vLuminance = Vector3Dot(vColor, gvLuminance);
    // Splat fSaturation
    Vector vSaturation = _mm_set_ps1(fSaturation);
    // vResult = ((vColor-vLuminance)*vSaturation)+vLuminance;
    Vector vResult = _mm_sub_ps(vColor, vLuminance);
    vResult = MATH_FMADD_PS(vResult, vSaturation, vLuminance);
    // Retain w from the source color
    vLuminance = _mm_shuffle_ps(vResult, vColor, _MM_SHUFFLE(3, 2, 2, 2));   // x = vResult.z,y = vResult.z,z = vColor.z,w=vColor.w
    vResult = _mm_shuffle_ps(vResult, vLuminance, _MM_SHUFFLE(3, 0, 1, 0));  // x = vResult.x,y = vResult.y,z = vResult.z,w=vColor.w
    return vResult;
#endif
}

inline Vector MathCallConv ColorAdjustContrast
(
    VectorArg vColor,
    float    fContrast
)noexcept
{
    // Result = (vColor - 0.5f) * fContrast + 0.5f;

#if defined(_MATH_NO_INTRINSICS_)
    VectorF32 vResult = { { {
            ((vColor.vector4_f32[0] - 0.5f) * fContrast) + 0.5f,
            ((vColor.vector4_f32[1] - 0.5f) * fContrast) + 0.5f,
            ((vColor.vector4_f32[2] - 0.5f) * fContrast) + 0.5f,
            vColor.vector4_f32[3]        // Leave W untouched
        } } };
    return vResult.v;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    Vector vResult = vsubq_f32(vColor, g_OneHalf.v);
    vResult = vmlaq_n_f32(g_OneHalf.v, vResult, fContrast);
    return vbslq_f32(g_Select1110, vResult, vColor);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vScale = _mm_set_ps1(fContrast);           // Splat the scale
    Vector vResult = _mm_sub_ps(vColor, g_OneHalf);  // Subtract 0.5f from the source (Saving source)
    vResult = MATH_FMADD_PS(vResult, vScale, g_OneHalf);
// Retain w from the source color
    vScale = _mm_shuffle_ps(vResult, vColor, _MM_SHUFFLE(3, 2, 2, 2));   // x = vResult.z,y = vResult.z,z = vColor.z,w=vColor.w
    vResult = _mm_shuffle_ps(vResult, vScale, _MM_SHUFFLE(3, 0, 1, 0));  // x = vResult.x,y = vResult.y,z = vResult.z,w=vColor.w
    return vResult;
#endif
}

inline Vector MathCallConv ColorRGBToHSL(VectorArg rgb)noexcept
{
    Vector r = VectorSplatX(rgb);
    Vector g = VectorSplatY(rgb);
    Vector b = VectorSplatZ(rgb);

    Vector min = VectorMin(r, VectorMin(g, b));
    Vector max = VectorMax(r, VectorMax(g, b));

    Vector l = VectorMultiply(VectorAdd(min, max), g_OneHalf);

    Vector d = VectorSubtract(max, min);

    Vector la = VectorSelect(rgb, l, g_Select1110);

    if (Vector3Less(d, g_Epsilon))
    {
        // Achromatic, assume H and S of 0
        return VectorSelect(la, g_Zero, g_Select1100);
    }
    else
    {
        Vector s, h;

        Vector d2 = VectorAdd(min, max);

        if (Vector3Greater(l, g_OneHalf))
        {
            // d / (2-max-min)
            s = VectorDivide(d, VectorSubtract(g_Two, d2));
        }
        else
        {
            // d / (max+min)
            s = VectorDivide(d, d2);
        }

        if (Vector3Equal(r, max))
        {
            // Red is max
            h = VectorDivide(VectorSubtract(g, b), d);
        }
        else if (Vector3Equal(g, max))
        {
            // Green is max
            h = VectorDivide(VectorSubtract(b, r), d);
            h = VectorAdd(h, g_Two);
        }
        else
        {
            // Blue is max
            h = VectorDivide(VectorSubtract(r, g), d);
            h = VectorAdd(h, g_Four);
        }

        h = VectorDivide(h, g_Six);

        if (Vector3Less(h, g_Zero))
            h = VectorAdd(h, g_One);

        Vector lha = VectorSelect(la, h, g_Select1100);
        return VectorSelect(s, lha, g_Select1011);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MathInternal
{
    inline Vector MathCallConv ColorHue2Clr(VectorArg p, VectorArg q, VectorArg h)noexcept
    {
        static const VectorF32 oneSixth = { { { 1.0f / 6.0f, 1.0f / 6.0f, 1.0f / 6.0f, 1.0f / 6.0f } } };
        static const VectorF32 twoThirds = { { { 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f } } };

        Vector t = h;

        if (Vector3Less(t, g_Zero))
            t = VectorAdd(t, g_One);

        if (Vector3Greater(t, g_One))
            t = VectorSubtract(t, g_One);

        if (Vector3Less(t, oneSixth))
        {
            // p + (q - p) * 6 * t
            Vector t1 = VectorSubtract(q, p);
            Vector t2 = VectorMultiply(g_Six, t);
            return VectorMultiplyAdd(t1, t2, p);
        }

        if (Vector3Less(t, g_OneHalf))
            return q;

        if (Vector3Less(t, twoThirds))
        {
            // p + (q - p) * 6 * (2/3 - t)
            Vector t1 = VectorSubtract(q, p);
            Vector t2 = VectorMultiply(g_Six, VectorSubtract(twoThirds, t));
            return VectorMultiplyAdd(t1, t2, p);
        }

        return p;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Vector MathCallConv ColorHSLToRGB(VectorArg hsl)noexcept
{
    static const VectorF32 oneThird = { { { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f } } };

    Vector s = VectorSplatY(hsl);
    Vector l = VectorSplatZ(hsl);

    if (Vector3NearEqual(s, g_Zero, g_Epsilon))
    {
        // Achromatic
        return VectorSelect(hsl, l, g_Select1110);
    }
    else
    {
        Vector h = VectorSplatX(hsl);

        Vector q;
        if (Vector3Less(l, g_OneHalf))
        {
            q = VectorMultiply(l, VectorAdd(g_One, s));
        }
        else
        {
            q = VectorSubtract(VectorAdd(l, s), VectorMultiply(l, s));
        }

        Vector p = VectorSubtract(VectorMultiply(g_Two, l), q);

        Vector r = SourceMathInternal::MathInternal::ColorHue2Clr(p, q, VectorAdd(h, oneThird));
        Vector g = SourceMathInternal::MathInternal::ColorHue2Clr(p, q, h);
        Vector b = SourceMathInternal::MathInternal::ColorHue2Clr(p, q, VectorSubtract(h, oneThird));

        Vector rg = VectorSelect(g, r, g_Select1000);
        Vector ba = VectorSelect(hsl, b, g_Select1110);

        return VectorSelect(ba, rg, g_Select1100);
    }
}

inline Vector MathCallConv ColorRGBToHSV(VectorArg rgb)noexcept
{
    Vector r = VectorSplatX(rgb);
    Vector g = VectorSplatY(rgb);
    Vector b = VectorSplatZ(rgb);

    Vector min = VectorMin(r, VectorMin(g, b));
    Vector v = VectorMax(r, VectorMax(g, b));

    Vector d = VectorSubtract(v, min);

    Vector s = (Vector3NearEqual(v, g_Zero, g_Epsilon)) ? g_Zero : VectorDivide(d, v);

    if (Vector3Less(d, g_Epsilon))
    {
        // Achromatic, assume H of 0
        Vector hv = VectorSelect(v, g_Zero, g_Select1000);
        Vector hva = VectorSelect(rgb, hv, g_Select1110);
        return VectorSelect(s, hva, g_Select1011);
    }
    else
    {
        Vector h;

        if (Vector3Equal(r, v))
        {
            // Red is max
            h = VectorDivide(VectorSubtract(g, b), d);

            if (Vector3Less(g, b))
                h = VectorAdd(h, g_Six);
        }
        else if (Vector3Equal(g, v))
        {
            // Green is max
            h = VectorDivide(VectorSubtract(b, r), d);
            h = VectorAdd(h, g_Two);
        }
        else
        {
            // Blue is max
            h = VectorDivide(VectorSubtract(r, g), d);
            h = VectorAdd(h, g_Four);
        }

        h = VectorDivide(h, g_Six);

        Vector hv = VectorSelect(v, h, g_Select1000);
        Vector hva = VectorSelect(rgb, hv, g_Select1110);
        return VectorSelect(s, hva, g_Select1011);
    }
}

inline Vector MathCallConv ColorHSVToRGB(VectorArg hsv)noexcept
{
    Vector h = VectorSplatX(hsv);
    Vector s = VectorSplatY(hsv);
    Vector v = VectorSplatZ(hsv);

    Vector h6 = VectorMultiply(h, g_Six);

    Vector i = VectorFloor(h6);
    Vector f = VectorSubtract(h6, i);

    // p = v* (1-s)
    Vector p = VectorMultiply(v, VectorSubtract(g_One, s));

    // q = v*(1-f*s)
    Vector q = VectorMultiply(v, VectorSubtract(g_One, VectorMultiply(f, s)));

    // t = v*(1 - (1-f)*s)
    Vector t = VectorMultiply(v, VectorSubtract(g_One, VectorMultiply(VectorSubtract(g_One, f), s)));

    auto ii = static_cast<int>(VectorGetX(VectorMod(i, g_Six)));

    Vector _rgb;

    switch (ii)
    {
    case 0: // rgb = vtp
        {
            Vector vt = VectorSelect(t, v, g_Select1000);
            _rgb = VectorSelect(p, vt, g_Select1100);
        }
        break;
    case 1: // rgb = qvp
        {
            Vector qv = VectorSelect(v, q, g_Select1000);
            _rgb = VectorSelect(p, qv, g_Select1100);
        }
        break;
    case 2: // rgb = pvt
        {
            Vector pv = VectorSelect(v, p, g_Select1000);
            _rgb = VectorSelect(t, pv, g_Select1100);
        }
        break;
    case 3: // rgb = pqv
        {
            Vector pq = VectorSelect(q, p, g_Select1000);
            _rgb = VectorSelect(v, pq, g_Select1100);
        }
        break;
    case 4: // rgb = tpv
        {
            Vector tp = VectorSelect(p, t, g_Select1000);
            _rgb = VectorSelect(v, tp, g_Select1100);
        }
        break;
    default: // rgb = vpq
        {
            Vector vp = VectorSelect(p, v, g_Select1000);
            _rgb = VectorSelect(q, vp, g_Select1100);
        }
        break;
    }

    return VectorSelect(hsv, _rgb, g_Select1110);
}

// These coefficient vectors are stored as matrix columns so `Vector3Transform`
// evaluates `M * rgb` under NWB's column-vector convention.
inline Vector MathCallConv ColorRGBToYUV(VectorArg rgb)noexcept
{
    static const VectorF32 Scale0 = { { { 0.299f, -0.147f, 0.615f, 0.0f } } };
    static const VectorF32 Scale1 = { { { 0.587f, -0.289f, -0.515f, 0.0f } } };
    static const VectorF32 Scale2 = { { { 0.114f, 0.436f, -0.100f, 0.0f } } };

    Matrix M = MatrixSetColumns(Scale0, Scale1, Scale2, g_Zero);
    Vector clr = Vector3Transform(rgb, M);

    return VectorSelect(rgb, clr, g_Select1110);
}

inline Vector MathCallConv ColorYUVToRGB(VectorArg yuv)noexcept
{
    static const VectorF32 Scale1 = { { { 0.0f, -0.395f, 2.032f, 0.0f } } };
    static const VectorF32 Scale2 = { { { 1.140f, -0.581f, 0.0f, 0.0f } } };

    Matrix M = MatrixSetColumns(g_One, Scale1, Scale2, g_Zero);
    Vector clr = Vector3Transform(yuv, M);

    return VectorSelect(yuv, clr, g_Select1110);
}

inline Vector MathCallConv ColorRGBToYUV_HD(VectorArg rgb)noexcept
{
    static const VectorF32 Scale0 = { { { 0.2126f, -0.0997f, 0.6150f, 0.0f } } };
    static const VectorF32 Scale1 = { { { 0.7152f, -0.3354f, -0.5586f, 0.0f } } };
    static const VectorF32 Scale2 = { { { 0.0722f, 0.4351f, -0.0564f, 0.0f } } };

    Matrix M = MatrixSetColumns(Scale0, Scale1, Scale2, g_Zero);
    Vector clr = Vector3Transform(rgb, M);

    return VectorSelect(rgb, clr, g_Select1110);
}

inline Vector MathCallConv ColorYUVToRGB_HD(VectorArg yuv)noexcept
{
    static const VectorF32 Scale1 = { { { 0.0f, -0.2153f, 2.1324f, 0.0f } } };
    static const VectorF32 Scale2 = { { { 1.2803f, -0.3806f, 0.0f, 0.0f } } };

    Matrix M = MatrixSetColumns(g_One, Scale1, Scale2, g_Zero);
    Vector clr = Vector3Transform(yuv, M);

    return VectorSelect(yuv, clr, g_Select1110);
}

inline Vector MathCallConv ColorRGBToYUV_UHD(VectorArg rgb)noexcept
{
    static const VectorF32 Scale0 = { { { 0.2627f, -0.1215f,  0.6150f, 0.0f } } };
    static const VectorF32 Scale1 = { { { 0.6780f, -0.3136f, -0.5655f, 0.0f } } };
    static const VectorF32 Scale2 = { { { 0.0593f,  0.4351f, -0.0495f, 0.0f } } };

    Matrix M = MatrixSetColumns(Scale0, Scale1, Scale2, g_Zero);
    Vector clr = Vector3Transform(rgb, M);

    return VectorSelect(rgb, clr, g_Select1110);
}

inline Vector MathCallConv ColorYUVToRGB_UHD(VectorArg yuv)noexcept
{
    static const VectorF32 Scale1 = { { {    0.0f, -0.1891f, 2.1620f, 0.0f } } };
    static const VectorF32 Scale2 = { { { 1.1989f, -0.4645f,    0.0f, 0.0f } } };

    Matrix M = MatrixSetColumns(g_One, Scale1, Scale2, g_Zero);
    Vector clr = Vector3Transform(yuv, M);

    return VectorSelect(yuv, clr, g_Select1110);
}

inline Vector MathCallConv ColorRGBToXYZ(VectorArg rgb)noexcept
{
    static const VectorF32 Scale0 = { { { 0.4887180f, 0.1762044f, 0.0000000f, 0.0f } } };
    static const VectorF32 Scale1 = { { { 0.3106803f, 0.8129847f, 0.0102048f, 0.0f } } };
    static const VectorF32 Scale2 = { { { 0.2006017f, 0.0108109f, 0.9897952f, 0.0f } } };
    static const VectorF32 Scale = { { { 1.f / 0.17697f, 1.f / 0.17697f, 1.f / 0.17697f, 0.0f } } };

    Matrix M = MatrixSetColumns(Scale0, Scale1, Scale2, g_Zero);
    Vector clr = VectorMultiply(Vector3Transform(rgb, M), Scale);

    return VectorSelect(rgb, clr, g_Select1110);
}

inline Vector MathCallConv ColorXYZToRGB(VectorArg xyz)noexcept
{
    static const VectorF32 Scale0 = { { { 2.3706743f, -0.5138850f, 0.0052982f, 0.0f } } };
    static const VectorF32 Scale1 = { { { -0.9000405f, 1.4253036f, -0.0146949f, 0.0f } } };
    static const VectorF32 Scale2 = { { { -0.4706338f, 0.0885814f, 1.0093968f, 0.0f } } };
    static const VectorF32 Scale = { { { 0.17697f, 0.17697f, 0.17697f, 0.0f } } };

    Matrix M = MatrixSetColumns(Scale0, Scale1, Scale2, g_Zero);
    Vector clr = Vector3Transform(VectorMultiply(xyz, Scale), M);

    return VectorSelect(xyz, clr, g_Select1110);
}

inline Vector MathCallConv ColorXYZToSRGB(VectorArg xyz)noexcept
{
    static const VectorF32 Scale0 = { { { 3.2406f, -0.9689f, 0.0557f, 0.0f } } };
    static const VectorF32 Scale1 = { { { -1.5372f, 1.8758f, -0.2040f, 0.0f } } };
    static const VectorF32 Scale2 = { { { -0.4986f, 0.0415f, 1.0570f, 0.0f } } };
    static const VectorF32 Cutoff = { { { 0.0031308f, 0.0031308f, 0.0031308f, 0.0f } } };
    static const VectorF32 Exp = { { { 1.0f / 2.4f, 1.0f / 2.4f, 1.0f / 2.4f, 1.0f } } };

    Matrix M = MatrixSetColumns(Scale0, Scale1, Scale2, g_Zero);
    Vector lclr = Vector3Transform(xyz, M);

    Vector sel = VectorGreater(lclr, Cutoff);

    // clr = 12.92 * lclr for lclr <= 0.0031308f
    Vector smallC = VectorMultiply(lclr, g_srgbScale);

    // clr = (1+a)*pow(lclr, 1/2.4) - a for lclr > 0.0031308 (where a = 0.055)
    Vector largeC = VectorSubtract(VectorMultiply(g_srgbA1, VectorPow(lclr, Exp)), g_srgbA);

    Vector clr = VectorSelect(smallC, largeC, sel);

    return VectorSelect(xyz, clr, g_Select1110);
}

inline Vector MathCallConv ColorSRGBToXYZ(VectorArg srgb)noexcept
{
    static const VectorF32 Scale0 = { { { 0.4124f, 0.2126f, 0.0193f, 0.0f } } };
    static const VectorF32 Scale1 = { { { 0.3576f, 0.7152f, 0.1192f, 0.0f } } };
    static const VectorF32 Scale2 = { { { 0.1805f, 0.0722f, 0.9505f, 0.0f } } };
    static const VectorF32 Cutoff = { { { 0.04045f, 0.04045f, 0.04045f, 0.0f } } };
    static const VectorF32 Exp = { { { 2.4f, 2.4f, 2.4f, 1.0f } } };

    Vector sel = VectorGreater(srgb, Cutoff);

    // lclr = clr / 12.92
    Vector smallC = VectorDivide(srgb, g_srgbScale);

    // lclr = pow( (clr + a) / (1+a), 2.4 )
    Vector largeC = VectorPow(VectorDivide(VectorAdd(srgb, g_srgbA), g_srgbA1), Exp);

    Vector lclr = VectorSelect(smallC, largeC, sel);

    Matrix M = MatrixSetColumns(Scale0, Scale1, Scale2, g_Zero);
    Vector clr = Vector3Transform(lclr, M);

    return VectorSelect(srgb, clr, g_Select1110);
}

inline Vector MathCallConv ColorRGBToSRGB(VectorArg rgb)noexcept
{
    static const VectorF32 Cutoff = { { { 0.0031308f, 0.0031308f, 0.0031308f, 1.f } } };
    static const VectorF32 Linear = { { { 12.92f, 12.92f, 12.92f, 1.f } } };
    static const VectorF32 Scale = { { { 1.055f, 1.055f, 1.055f, 1.f } } };
    static const VectorF32 Bias = { { { 0.055f, 0.055f, 0.055f, 0.f } } };
    static const VectorF32 InvGamma = { { { 1.0f / 2.4f, 1.0f / 2.4f, 1.0f / 2.4f, 1.f } } };

    Vector V = VectorSaturate(rgb);
    Vector V0 = VectorMultiply(V, Linear);
    Vector V1 = VectorSubtract(VectorMultiply(Scale, VectorPow(V, InvGamma)), Bias);
    Vector select = VectorLess(V, Cutoff);
    V = VectorSelect(V1, V0, select);
    return VectorSelect(rgb, V, g_Select1110);
}

inline Vector MathCallConv ColorSRGBToRGB(VectorArg srgb)noexcept
{
    static const VectorF32 Cutoff = { { { 0.04045f, 0.04045f, 0.04045f, 1.f } } };
    static const VectorF32 ILinear = { { { 1.f / 12.92f, 1.f / 12.92f, 1.f / 12.92f, 1.f } } };
    static const VectorF32 Scale = { { { 1.f / 1.055f, 1.f / 1.055f, 1.f / 1.055f, 1.f } } };
    static const VectorF32 Bias = { { { 0.055f, 0.055f, 0.055f, 0.f } } };
    static const VectorF32 Gamma = { { { 2.4f, 2.4f, 2.4f, 1.f } } };

    Vector V = VectorSaturate(srgb);
    Vector V0 = VectorMultiply(V, ILinear);
    Vector V1 = VectorPow(VectorMultiply(VectorAdd(V, Bias), Scale), Gamma);
    Vector select = VectorGreater(V, Cutoff);
    V = VectorSelect(V0, V1, select);
    return VectorSelect(srgb, V, g_Select1110);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline bool VerifyCPUSupport()noexcept
{
#if defined(_MATH_SSE_INTRINSICS_) && !defined(__powerpc64__) && !defined(_MATH_NO_INTRINSICS_)
    int CPUInfo[4] = { -1 };
#if (defined(__clang__) || defined(__GNUC__)) && defined(__cpuid)
    __cpuid(0, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);
#else
    __cpuid(CPUInfo, 0);
#endif

#ifdef __AVX2__
    if (CPUInfo[0] < 7)
        return false;
#else
    if (CPUInfo[0] < 1)
        return false;
#endif

#if (defined(__clang__) || defined(__GNUC__)) && defined(__cpuid)
    __cpuid(1, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);
#else
    __cpuid(CPUInfo, 1);
#endif

#if defined(__AVX2__) || defined(_MATH_AVX2_INTRINSICS_)
    // The compiler can emit FMA3 instructions even without explicit intrinsics use
    if ((CPUInfo[2] & 0x38081001) != 0x38081001)
        return false; // No F16C/AVX/OSXSAVE/SSE4.1/FMA3/SSE3 support
#elif defined(_MATH_FMA3_INTRINSICS_) && defined(_MATH_F16C_INTRINSICS_)
    if ((CPUInfo[2] & 0x38081001) != 0x38081001)
        return false; // No F16C/AVX/OSXSAVE/SSE4.1/FMA3/SSE3 support
#elif defined(_MATH_FMA3_INTRINSICS_)
    if ((CPUInfo[2] & 0x18081001) != 0x18081001)
        return false; // No AVX/OSXSAVE/SSE4.1/FMA3/SSE3 support
#elif defined(_MATH_F16C_INTRINSICS_)
    if ((CPUInfo[2] & 0x38080001) != 0x38080001)
        return false; // No F16C/AVX/OSXSAVE/SSE4.1/SSE3 support
#elif defined(__AVX__) || defined(_MATH_AVX_INTRINSICS_)
    if ((CPUInfo[2] & 0x18080001) != 0x18080001)
        return false; // No AVX/OSXSAVE/SSE4.1/SSE3 support
#elif defined(_MATH_SSE4_INTRINSICS_)
    if ((CPUInfo[2] & 0x80001) != 0x80001)
        return false; // No SSE3/SSE4.1 support
#elif defined(_MATH_SSE3_INTRINSICS_)
    if (!(CPUInfo[2] & 0x1))
        return false; // No SSE3 support
#endif

    // The x64 processor model requires SSE2 support, but no harm in checking
    if ((CPUInfo[3] & 0x6000000) != 0x6000000)
        return false; // No SSE2/SSE support

#if defined(__AVX2__) || defined(_MATH_AVX2_INTRINSICS_)
#if defined(__clang__) || defined(__GNUC__)
    __cpuid_count(7, 0, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);
#else
    __cpuidex(CPUInfo, 7, 0);
#endif
    if (!(CPUInfo[1] & 0x20))
        return false; // No AVX2 support
#endif

    return true;
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    // ARM-NEON support is required for the Windows on ARM platform
    return true;
#else
    // No intrinsics path always supported
    return true;
#endif
}

inline Vector MathCallConv FresnelTerm
(
    VectorArg CosIncidentAngle,
    VectorArg RefractionIndex
)noexcept
{
    assert(!Vector4IsInfinite(CosIncidentAngle));

    // Result = 0.5f * (g - c)^2 / (g + c)^2 * ((c * (g + c) - 1)^2 / (c * (g - c) + 1)^2 + 1) where
    // c = CosIncidentAngle
    // g = sqrt(c^2 + RefractionIndex^2 - 1)

#if defined(_MATH_NO_INTRINSICS_) || defined(_MATH_ARM_NEON_INTRINSICS_)

    Vector G = VectorMultiplyAdd(RefractionIndex, RefractionIndex, g_NegativeOne.v);
    G = VectorMultiplyAdd(CosIncidentAngle, CosIncidentAngle, G);
    G = VectorAbs(G);
    G = VectorSqrt(G);

    Vector S = VectorAdd(G, CosIncidentAngle);
    Vector D = VectorSubtract(G, CosIncidentAngle);

    Vector V0 = VectorMultiply(D, D);
    Vector V1 = VectorMultiply(S, S);
    V1 = VectorReciprocal(V1);
    V0 = VectorMultiply(g_OneHalf.v, V0);
    V0 = VectorMultiply(V0, V1);

    Vector V2 = VectorMultiplyAdd(CosIncidentAngle, S, g_NegativeOne.v);
    Vector V3 = VectorMultiplyAdd(CosIncidentAngle, D, g_One.v);
    V2 = VectorMultiply(V2, V2);
    V3 = VectorMultiply(V3, V3);
    V3 = VectorReciprocal(V3);
    V2 = VectorMultiplyAdd(V2, V3, g_One.v);

    Vector Result = VectorMultiply(V0, V2);

    Result = VectorSaturate(Result);

    return Result;

#elif defined(_MATH_SSE_INTRINSICS_)
    // G = sqrt(abs((RefractionIndex^2-1) + CosIncidentAngle^2))
    Vector G = _mm_mul_ps(RefractionIndex, RefractionIndex);
    Vector vTemp = _mm_mul_ps(CosIncidentAngle, CosIncidentAngle);
    G = _mm_sub_ps(G, g_One);
    vTemp = _mm_add_ps(vTemp, G);
    // max((0-vTemp),vTemp) == abs(vTemp)
    // The abs is needed to deal with refraction and cosine being zero
    G = _mm_setzero_ps();
    G = _mm_sub_ps(G, vTemp);
    G = _mm_max_ps(G, vTemp);
    // Last operation, the sqrt()
    G = _mm_sqrt_ps(G);

    // Calc G-C and G+C
    Vector GAddC = _mm_add_ps(G, CosIncidentAngle);
    Vector GSubC = _mm_sub_ps(G, CosIncidentAngle);
    // Perform the term (0.5f *(g - c)^2) / (g + c)^2
    Vector vResult = _mm_mul_ps(GSubC, GSubC);
    vTemp = _mm_mul_ps(GAddC, GAddC);
    vResult = _mm_mul_ps(vResult, g_OneHalf);
    vResult = _mm_div_ps(vResult, vTemp);
    // Perform the term ((c * (g + c) - 1)^2 / (c * (g - c) + 1)^2 + 1)
    GAddC = _mm_mul_ps(GAddC, CosIncidentAngle);
    GSubC = _mm_mul_ps(GSubC, CosIncidentAngle);
    GAddC = _mm_sub_ps(GAddC, g_One);
    GSubC = _mm_add_ps(GSubC, g_One);
    GAddC = _mm_mul_ps(GAddC, GAddC);
    GSubC = _mm_mul_ps(GSubC, GSubC);
    GAddC = _mm_div_ps(GAddC, GSubC);
    GAddC = _mm_add_ps(GAddC, g_One);
    // Multiply the two term parts
    vResult = _mm_mul_ps(vResult, GAddC);
    // Clamp to 0.0 - 1.0f
    vResult = _mm_max_ps(vResult, g_Zero);
    vResult = _mm_min_ps(vResult, g_One);
    return vResult;
#endif
}

inline bool ScalarNearEqual
(
    float S1,
    float S2,
    float Epsilon
)noexcept
{
    float Delta = S1 - S2;
    return (fabsf(Delta) <= Epsilon);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Modulo the range of the given angle such that -MATH_PI <= Angle < MATH_PI
inline float ScalarModAngle(float Angle)noexcept
{
    // Note: The modulo is performed with unsigned math only to work
    // around a precision error on numbers that are close to PI

    // Normalize the range from 0.0f to MATH_2PI
    Angle = Angle + MATH_PI;
    // Perform the modulo, unsigned
    float fTemp = fabsf(Angle);
    fTemp = fTemp - (MATH_2PI * static_cast<float>(static_cast<int32_t>(fTemp / MATH_2PI)));
    // Restore the number to the range of -MATH_PI to MATH_PI-epsilon
    fTemp = fTemp - MATH_PI;
    // If the modulo'd value was negative, restore negation
    if (Angle < 0.0f)
    {
        fTemp = -fTemp;
    }
    return fTemp;
}

inline float ScalarSin(float Value)noexcept
{
    // Map Value to y in [-pi,pi], x = 2*pi*quotient + remainder.
    float quotient = MATH_1DIV2PI * Value;
    if (Value >= 0.0f)
    {
        quotient = static_cast<float>(static_cast<int>(quotient + 0.5f));
    }
    else
    {
        quotient = static_cast<float>(static_cast<int>(quotient - 0.5f));
    }
    float y = Value - MATH_2PI * quotient;

    // Map y to [-pi/2,pi/2] with sin(y) = sin(Value).
    if (y > MATH_PIDIV2)
    {
        y = MATH_PI - y;
    }
    else if (y < -MATH_PIDIV2)
    {
        y = -MATH_PI - y;
    }

    // 11-degree minimax approximation
    float y2 = y * y;
    return (((((-2.3889859e-08f * y2 + 2.7525562e-06f) * y2 - 0.00019840874f) * y2 + 0.0083333310f) * y2 - 0.16666667f) * y2 + 1.0f) * y;
}

inline float ScalarSinEst(float Value)noexcept
{
    // Map Value to y in [-pi,pi], x = 2*pi*quotient + remainder.
    float quotient = MATH_1DIV2PI * Value;
    if (Value >= 0.0f)
    {
        quotient = static_cast<float>(static_cast<int>(quotient + 0.5f));
    }
    else
    {
        quotient = static_cast<float>(static_cast<int>(quotient - 0.5f));
    }
    float y = Value - MATH_2PI * quotient;

    // Map y to [-pi/2,pi/2] with sin(y) = sin(Value).
    if (y > MATH_PIDIV2)
    {
        y = MATH_PI - y;
    }
    else if (y < -MATH_PIDIV2)
    {
        y = -MATH_PI - y;
    }

    // 7-degree minimax approximation
    float y2 = y * y;
    return (((-0.00018524670f * y2 + 0.0083139502f) * y2 - 0.16665852f) * y2 + 1.0f) * y;
}

inline float ScalarCos(float Value)noexcept
{
    // Map Value to y in [-pi,pi], x = 2*pi*quotient + remainder.
    float quotient = MATH_1DIV2PI * Value;
    if (Value >= 0.0f)
    {
        quotient = static_cast<float>(static_cast<int>(quotient + 0.5f));
    }
    else
    {
        quotient = static_cast<float>(static_cast<int>(quotient - 0.5f));
    }
    float y = Value - MATH_2PI * quotient;

    // Map y to [-pi/2,pi/2] with cos(y) = sign*cos(x).
    float sign;
    if (y > MATH_PIDIV2)
    {
        y = MATH_PI - y;
        sign = -1.0f;
    }
    else if (y < -MATH_PIDIV2)
    {
        y = -MATH_PI - y;
        sign = -1.0f;
    }
    else
    {
        sign = +1.0f;
    }

    // 10-degree minimax approximation
    float y2 = y * y;
    float p = ((((-2.6051615e-07f * y2 + 2.4760495e-05f) * y2 - 0.0013888378f) * y2 + 0.041666638f) * y2 - 0.5f) * y2 + 1.0f;
    return sign * p;
}

inline float ScalarCosEst(float Value)noexcept
{
    // Map Value to y in [-pi,pi], x = 2*pi*quotient + remainder.
    float quotient = MATH_1DIV2PI * Value;
    if (Value >= 0.0f)
    {
        quotient = static_cast<float>(static_cast<int>(quotient + 0.5f));
    }
    else
    {
        quotient = static_cast<float>(static_cast<int>(quotient - 0.5f));
    }
    float y = Value - MATH_2PI * quotient;

    // Map y to [-pi/2,pi/2] with cos(y) = sign*cos(x).
    float sign;
    if (y > MATH_PIDIV2)
    {
        y = MATH_PI - y;
        sign = -1.0f;
    }
    else if (y < -MATH_PIDIV2)
    {
        y = -MATH_PI - y;
        sign = -1.0f;
    }
    else
    {
        sign = +1.0f;
    }

    // 6-degree minimax approximation
    float y2 = y * y;
    float p = ((-0.0012712436f * y2 + 0.041493919f) * y2 - 0.49992746f) * y2 + 1.0f;
    return sign * p;
}

_Use_decl_annotations_
inline void ScalarSinCos
(
    float* pSin,
    float* pCos,
    float  Value
)noexcept
{
    assert(pSin);
    assert(pCos);

    // Map Value to y in [-pi,pi], x = 2*pi*quotient + remainder.
    float quotient = MATH_1DIV2PI * Value;
    if (Value >= 0.0f)
    {
        quotient = static_cast<float>(static_cast<int>(quotient + 0.5f));
    }
    else
    {
        quotient = static_cast<float>(static_cast<int>(quotient - 0.5f));
    }
    float y = Value - MATH_2PI * quotient;

    // Map y to [-pi/2,pi/2] with sin(y) = sin(Value).
    float sign;
    if (y > MATH_PIDIV2)
    {
        y = MATH_PI - y;
        sign = -1.0f;
    }
    else if (y < -MATH_PIDIV2)
    {
        y = -MATH_PI - y;
        sign = -1.0f;
    }
    else
    {
        sign = +1.0f;
    }

    float y2 = y * y;

    // 11-degree minimax approximation
    *pSin = (((((-2.3889859e-08f * y2 + 2.7525562e-06f) * y2 - 0.00019840874f) * y2 + 0.0083333310f) * y2 - 0.16666667f) * y2 + 1.0f) * y;

    // 10-degree minimax approximation
    float p = ((((-2.6051615e-07f * y2 + 2.4760495e-05f) * y2 - 0.0013888378f) * y2 + 0.041666638f) * y2 - 0.5f) * y2 + 1.0f;
    *pCos = sign * p;
}

_Use_decl_annotations_
inline void ScalarSinCosEst
(
    float* pSin,
    float* pCos,
    float  Value
)noexcept
{
    assert(pSin);
    assert(pCos);

    // Map Value to y in [-pi,pi], x = 2*pi*quotient + remainder.
    float quotient = MATH_1DIV2PI * Value;
    if (Value >= 0.0f)
    {
        quotient = static_cast<float>(static_cast<int>(quotient + 0.5f));
    }
    else
    {
        quotient = static_cast<float>(static_cast<int>(quotient - 0.5f));
    }
    float y = Value - MATH_2PI * quotient;

    // Map y to [-pi/2,pi/2] with sin(y) = sin(Value).
    float sign;
    if (y > MATH_PIDIV2)
    {
        y = MATH_PI - y;
        sign = -1.0f;
    }
    else if (y < -MATH_PIDIV2)
    {
        y = -MATH_PI - y;
        sign = -1.0f;
    }
    else
    {
        sign = +1.0f;
    }

    float y2 = y * y;

    // 7-degree minimax approximation
    *pSin = (((-0.00018524670f * y2 + 0.0083139502f) * y2 - 0.16665852f) * y2 + 1.0f) * y;

    // 6-degree minimax approximation
    float p = ((-0.0012712436f * y2 + 0.041493919f) * y2 - 0.49992746f) * y2 + 1.0f;
    *pCos = sign * p;
}

inline float ScalarASin(float Value)noexcept
{
    // Clamp input to [-1,1].
    bool nonnegative = (Value >= 0.0f);
    float x = fabsf(Value);
    float omx = 1.0f - x;
    if (omx < 0.0f)
    {
        omx = 0.0f;
    }
    float root = sqrtf(omx);

    // 7-degree minimax approximation
    float result = ((((((-0.0012624911f * x + 0.0066700901f) * x - 0.0170881256f) * x + 0.0308918810f) * x - 0.0501743046f) * x + 0.0889789874f) * x - 0.2145988016f) * x + 1.5707963050f;
    result *= root;  // acos(|x|)

    // acos(x) = pi - acos(-x) when x < 0, asin(x) = pi/2 - acos(x)
    return (nonnegative ? MATH_PIDIV2 - result : result - MATH_PIDIV2);
}

inline float ScalarASinEst(float Value)noexcept
{
    // Clamp input to [-1,1].
    bool nonnegative = (Value >= 0.0f);
    float x = fabsf(Value);
    float omx = 1.0f - x;
    if (omx < 0.0f)
    {
        omx = 0.0f;
    }
    float root = sqrtf(omx);

    // 3-degree minimax approximation
    float result = ((-0.0187293f * x + 0.0742610f) * x - 0.2121144f) * x + 1.5707288f;
    result *= root;  // acos(|x|)

    // acos(x) = pi - acos(-x) when x < 0, asin(x) = pi/2 - acos(x)
    return (nonnegative ? MATH_PIDIV2 - result : result - MATH_PIDIV2);
}

inline float ScalarACos(float Value)noexcept
{
    // Clamp input to [-1,1].
    bool nonnegative = (Value >= 0.0f);
    float x = fabsf(Value);
    float omx = 1.0f - x;
    if (omx < 0.0f)
    {
        omx = 0.0f;
    }
    float root = sqrtf(omx);

    // 7-degree minimax approximation
    float result = ((((((-0.0012624911f * x + 0.0066700901f) * x - 0.0170881256f) * x + 0.0308918810f) * x - 0.0501743046f) * x + 0.0889789874f) * x - 0.2145988016f) * x + 1.5707963050f;
    result *= root;

    // acos(x) = pi - acos(-x) when x < 0
    return (nonnegative ? result : MATH_PI - result);
}

inline float ScalarACosEst(float Value)noexcept
{
    // Clamp input to [-1,1].
    bool nonnegative = (Value >= 0.0f);
    float x = fabsf(Value);
    float omx = 1.0f - x;
    if (omx < 0.0f)
    {
        omx = 0.0f;
    }
    float root = sqrtf(omx);

    // 3-degree minimax approximation
    float result = ((-0.0187293f * x + 0.0742610f) * x - 0.2121144f) * x + 1.5707288f;
    result *= root;

    // acos(x) = pi - acos(-x) when x < 0
    return (nonnegative ? result : MATH_PI - result);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

