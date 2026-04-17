// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(_MATH_NO_INTRINSICS_) && defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma float_control(push)
#pragma float_control(precise, on)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Return true if any entry in the matrix is NaN
inline bool MathCallConv MatrixIsNaN(FXMMATRIX M)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    size_t i = 16;
    const uint32_t* pWork = reinterpret_cast<const uint32_t*>(&M.m[0][0]);
    do{
    // Fetch value into integer unit
        uint32_t uTest = pWork[0];
        // Remove sign
        uTest &= 0x7FFFFFFFU;
        // NaN is 0x7F800001 through 0x7FFFFFFF inclusive
        uTest -= 0x7F800001U;
        if(uTest < 0x007FFFFFU){
            break;      // NaN found
        }
        ++pWork;        // Next entry
    }
    while(--i);
    return (i != 0);      // i == 0 if nothing matched
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Load in registers
    float32x4_t vX = M.r[0];
    float32x4_t vY = M.r[1];
    float32x4_t vZ = M.r[2];
    float32x4_t vW = M.r[3];
    // Test themselves to check for NaN
    uint32x4_t xmask = vmvnq_u32(vceqq_f32(vX, vX));
    uint32x4_t ymask = vmvnq_u32(vceqq_f32(vY, vY));
    uint32x4_t zmask = vmvnq_u32(vceqq_f32(vZ, vZ));
    uint32x4_t wmask = vmvnq_u32(vceqq_f32(vW, vW));
    // Or all the results
    xmask = vorrq_u32(xmask, zmask);
    ymask = vorrq_u32(ymask, wmask);
    xmask = vorrq_u32(xmask, ymask);
    // If any tested true, return true
    uint8x8x2_t vTemp = vzip_u8(
        vget_low_u8(vreinterpretq_u8_u32(xmask)),
        vget_high_u8(vreinterpretq_u8_u32(xmask)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1);
    return (r != 0);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Load in registers
    Vector vX = M.r[0];
    Vector vY = M.r[1];
    Vector vZ = M.r[2];
    Vector vW = M.r[3];
    // Test themselves to check for NaN
    vX = _mm_cmpneq_ps(vX, vX);
    vY = _mm_cmpneq_ps(vY, vY);
    vZ = _mm_cmpneq_ps(vZ, vZ);
    vW = _mm_cmpneq_ps(vW, vW);
    // Or all the results
    vX = _mm_or_ps(vX, vZ);
    vY = _mm_or_ps(vY, vW);
    vX = _mm_or_ps(vX, vY);
    // If any tested true, return true
    return (_mm_movemask_ps(vX) != 0);
#else
#endif
}

#if !defined(_MATH_NO_INTRINSICS_) && defined(_MSC_VER) && !defined(__INTEL_COMPILER)
#pragma float_control(pop)
#endif

// Return true if any entry in the matrix is +/-INF
inline bool MathCallConv MatrixIsInfinite(FXMMATRIX M)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    size_t i = 16;
    const uint32_t* pWork = reinterpret_cast<const uint32_t*>(&M.m[0][0]);
    do{
    // Fetch value into integer unit
        uint32_t uTest = pWork[0];
        // Remove sign
        uTest &= 0x7FFFFFFFU;
        // INF is 0x7F800000
        if(uTest == 0x7F800000U){
            break;      // INF found
        }
        ++pWork;        // Next entry
    }
    while(--i);
    return (i != 0);      // i == 0 if nothing matched
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    // Load in registers
    float32x4_t vX = M.r[0];
    float32x4_t vY = M.r[1];
    float32x4_t vZ = M.r[2];
    float32x4_t vW = M.r[3];
    // Mask off the sign bits
    vX = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(vX), g_AbsMask));
    vY = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(vY), g_AbsMask));
    vZ = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(vZ), g_AbsMask));
    vW = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(vW), g_AbsMask));
    // Compare to infinity
    uint32x4_t xmask = vceqq_f32(vX, g_Infinity);
    uint32x4_t ymask = vceqq_f32(vY, g_Infinity);
    uint32x4_t zmask = vceqq_f32(vZ, g_Infinity);
    uint32x4_t wmask = vceqq_f32(vW, g_Infinity);
    // Or the answers together
    xmask = vorrq_u32(xmask, zmask);
    ymask = vorrq_u32(ymask, wmask);
    xmask = vorrq_u32(xmask, ymask);
    // If any tested true, return true
    uint8x8x2_t vTemp = vzip_u8(
        vget_low_u8(vreinterpretq_u8_u32(xmask)),
        vget_high_u8(vreinterpretq_u8_u32(xmask)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1);
    return (r != 0);
#elif defined(_MATH_SSE_INTRINSICS_)
    // Mask off the sign bits
    Vector vTemp1 = _mm_and_ps(M.r[0], g_AbsMask);
    Vector vTemp2 = _mm_and_ps(M.r[1], g_AbsMask);
    Vector vTemp3 = _mm_and_ps(M.r[2], g_AbsMask);
    Vector vTemp4 = _mm_and_ps(M.r[3], g_AbsMask);
    // Compare to infinity
    vTemp1 = _mm_cmpeq_ps(vTemp1, g_Infinity);
    vTemp2 = _mm_cmpeq_ps(vTemp2, g_Infinity);
    vTemp3 = _mm_cmpeq_ps(vTemp3, g_Infinity);
    vTemp4 = _mm_cmpeq_ps(vTemp4, g_Infinity);
    // Or the answers together
    vTemp1 = _mm_or_ps(vTemp1, vTemp2);
    vTemp3 = _mm_or_ps(vTemp3, vTemp4);
    vTemp1 = _mm_or_ps(vTemp1, vTemp3);
    // If any are infinity, the signs are true.
    return (_mm_movemask_ps(vTemp1) != 0);
#endif
}

// Return true if the Matrix is equal to identity
inline bool MathCallConv MatrixIsIdentity(FXMMATRIX M)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    // Use the integer pipeline to reduce branching to a minimum
    const uint32_t* pWork = reinterpret_cast<const uint32_t*>(&M.m[0][0]);
    // Convert 1.0f to zero and or them together
    uint32_t uOne = pWork[0] ^ 0x3F800000U;
    // Or all the 0.0f entries together
    uint32_t uZero = pWork[1];
    uZero |= pWork[2];
    uZero |= pWork[3];
    // 2nd row
    uZero |= pWork[4];
    uOne |= pWork[5] ^ 0x3F800000U;
    uZero |= pWork[6];
    uZero |= pWork[7];
    // 3rd row
    uZero |= pWork[8];
    uZero |= pWork[9];
    uOne |= pWork[10] ^ 0x3F800000U;
    uZero |= pWork[11];
    // 4th row
    uZero |= pWork[12];
    uZero |= pWork[13];
    uZero |= pWork[14];
    uOne |= pWork[15] ^ 0x3F800000U;
    // If all zero entries are zero, the uZero==0
    uZero &= 0x7FFFFFFF;    // Allow -0.0f
    // If all 1.0f entries are 1.0f, then uOne==0
    uOne |= uZero;
    return (uOne == 0);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    uint32x4_t xmask = vceqq_f32(M.r[0], g_IdentityC0);
    uint32x4_t ymask = vceqq_f32(M.r[1], g_IdentityC1);
    uint32x4_t zmask = vceqq_f32(M.r[2], g_IdentityC2);
    uint32x4_t wmask = vceqq_f32(M.r[3], g_IdentityC3);
    xmask = vandq_u32(xmask, zmask);
    ymask = vandq_u32(ymask, wmask);
    xmask = vandq_u32(xmask, ymask);
    uint8x8x2_t vTemp = vzip_u8(vget_low_u8(vreinterpretq_u8_u32(xmask)), vget_high_u8(vreinterpretq_u8_u32(xmask)));
    uint16x4x2_t vTemp2 = vzip_u16(vreinterpret_u16_u8(vTemp.val[0]), vreinterpret_u16_u8(vTemp.val[1]));
    uint32_t r = vget_lane_u32(vreinterpret_u32_u16(vTemp2.val[1]), 1);
    return (r == 0xFFFFFFFFU);
#elif defined(_MATH_SSE_INTRINSICS_)
    Vector vTemp1 = _mm_cmpeq_ps(M.r[0], g_IdentityC0);
    Vector vTemp2 = _mm_cmpeq_ps(M.r[1], g_IdentityC1);
    Vector vTemp3 = _mm_cmpeq_ps(M.r[2], g_IdentityC2);
    Vector vTemp4 = _mm_cmpeq_ps(M.r[3], g_IdentityC3);
    vTemp1 = _mm_and_ps(vTemp1, vTemp2);
    vTemp3 = _mm_and_ps(vTemp3, vTemp4);
    vTemp1 = _mm_and_ps(vTemp1, vTemp3);
    return (_mm_movemask_ps(vTemp1) == 0x0f);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Matrix MathCallConv MatrixMultiply
(
    FXMMATRIX M1,
    CXMMATRIX M2
)noexcept{
    Matrix mResult;
    mResult.r[0] = Vector4Transform(M2.r[0], M1);
    mResult.r[1] = Vector4Transform(M2.r[1], M1);
    mResult.r[2] = Vector4Transform(M2.r[2], M1);
    mResult.r[3] = Vector4Transform(M2.r[3], M1);
    return mResult;
}

inline Matrix MathCallConv MatrixMultiplyTranspose
(
    FXMMATRIX M1,
    CXMMATRIX M2
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    Matrix mResult;

    float x = M1(0, 0);
    float y = M1(0, 1);
    float z = M1(0, 2);
    float w = M1(0, 3);
    mResult.r[0] = VectorSet(
        M2(0, 0) * x + M2(1, 0) * y + M2(2, 0) * z + M2(3, 0) * w,
        M2(0, 1) * x + M2(1, 1) * y + M2(2, 1) * z + M2(3, 1) * w,
        M2(0, 2) * x + M2(1, 2) * y + M2(2, 2) * z + M2(3, 2) * w,
        M2(0, 3) * x + M2(1, 3) * y + M2(2, 3) * z + M2(3, 3) * w
    );

    x = M1(1, 0);
    y = M1(1, 1);
    z = M1(1, 2);
    w = M1(1, 3);
    mResult.r[1] = VectorSet(
        M2(0, 0) * x + M2(1, 0) * y + M2(2, 0) * z + M2(3, 0) * w,
        M2(0, 1) * x + M2(1, 1) * y + M2(2, 1) * z + M2(3, 1) * w,
        M2(0, 2) * x + M2(1, 2) * y + M2(2, 2) * z + M2(3, 2) * w,
        M2(0, 3) * x + M2(1, 3) * y + M2(2, 3) * z + M2(3, 3) * w
    );

    x = M1(2, 0);
    y = M1(2, 1);
    z = M1(2, 2);
    w = M1(2, 3);
    mResult.r[2] = VectorSet(
        M2(0, 0) * x + M2(1, 0) * y + M2(2, 0) * z + M2(3, 0) * w,
        M2(0, 1) * x + M2(1, 1) * y + M2(2, 1) * z + M2(3, 1) * w,
        M2(0, 2) * x + M2(1, 2) * y + M2(2, 2) * z + M2(3, 2) * w,
        M2(0, 3) * x + M2(1, 3) * y + M2(2, 3) * z + M2(3, 3) * w
    );

    x = M1(3, 0);
    y = M1(3, 1);
    z = M1(3, 2);
    w = M1(3, 3);
    mResult.r[3] = VectorSet(
        M2(0, 0) * x + M2(1, 0) * y + M2(2, 0) * z + M2(3, 0) * w,
        M2(0, 1) * x + M2(1, 1) * y + M2(2, 1) * z + M2(3, 1) * w,
        M2(0, 2) * x + M2(1, 2) * y + M2(2, 2) * z + M2(3, 2) * w,
        M2(0, 3) * x + M2(1, 3) * y + M2(2, 3) * z + M2(3, 3) * w
    );
    return mResult;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x2_t VL = vget_low_f32(M2.r[0]);
    float32x2_t VH = vget_high_f32(M2.r[0]);

    float32x4_t vX = vmulq_lane_f32(M1.r[0], VL, 0);
    float32x4_t vY = vmulq_lane_f32(M1.r[1], VL, 1);
    float32x4_t vZ = vmlaq_lane_f32(vX, M1.r[2], VH, 0);
    float32x4_t vW = vmlaq_lane_f32(vY, M1.r[3], VH, 1);
    float32x4_t r0 = vaddq_f32(vZ, vW);

    VL = vget_low_f32(M2.r[1]);
    VH = vget_high_f32(M2.r[1]);
    vX = vmulq_lane_f32(M1.r[0], VL, 0);
    vY = vmulq_lane_f32(M1.r[1], VL, 1);
    vZ = vmlaq_lane_f32(vX, M1.r[2], VH, 0);
    vW = vmlaq_lane_f32(vY, M1.r[3], VH, 1);
    float32x4_t r1 = vaddq_f32(vZ, vW);

    VL = vget_low_f32(M2.r[2]);
    VH = vget_high_f32(M2.r[2]);
    vX = vmulq_lane_f32(M1.r[0], VL, 0);
    vY = vmulq_lane_f32(M1.r[1], VL, 1);
    vZ = vmlaq_lane_f32(vX, M1.r[2], VH, 0);
    vW = vmlaq_lane_f32(vY, M1.r[3], VH, 1);
    float32x4_t r2 = vaddq_f32(vZ, vW);

    VL = vget_low_f32(M2.r[3]);
    VH = vget_high_f32(M2.r[3]);
    vX = vmulq_lane_f32(M1.r[0], VL, 0);
    vY = vmulq_lane_f32(M1.r[1], VL, 1);
    vZ = vmlaq_lane_f32(vX, M1.r[2], VH, 0);
    vW = vmlaq_lane_f32(vY, M1.r[3], VH, 1);
    float32x4_t r3 = vaddq_f32(vZ, vW);

    float32x4x2_t P0 = vzipq_f32(r0, r2);
    float32x4x2_t P1 = vzipq_f32(r1, r3);

    float32x4x2_t T0 = vzipq_f32(P0.val[0], P1.val[0]);
    float32x4x2_t T1 = vzipq_f32(P0.val[1], P1.val[1]);

    Matrix mResult;
    mResult.r[0] = T0.val[0];
    mResult.r[1] = T0.val[1];
    mResult.r[2] = T1.val[0];
    mResult.r[3] = T1.val[1];
    return mResult;

#elif defined(_MATH_AVX2_INTRINSICS_)
    __m256 t0 = _mm256_castps128_ps256(M2.r[0]);
    t0 = _mm256_insertf128_ps(t0, M2.r[1], 1);
    __m256 t1 = _mm256_castps128_ps256(M2.r[2]);
    t1 = _mm256_insertf128_ps(t1, M2.r[3], 1);

    __m256 u0 = _mm256_castps128_ps256(M1.r[0]);
    u0 = _mm256_insertf128_ps(u0, M1.r[1], 1);
    __m256 u1 = _mm256_castps128_ps256(M1.r[2]);
    u1 = _mm256_insertf128_ps(u1, M1.r[3], 1);

    __m256 a0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(0, 0, 0, 0));
    __m256 a1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(0, 0, 0, 0));
    __m256 b0 = _mm256_permute2f128_ps(u0, u0, 0x00);
    __m256 c0 = _mm256_mul_ps(a0, b0);
    __m256 c1 = _mm256_mul_ps(a1, b0);

    a0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(1, 1, 1, 1));
    a1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(1, 1, 1, 1));
    b0 = _mm256_permute2f128_ps(u0, u0, 0x11);
    __m256 c2 = _mm256_fmadd_ps(a0, b0, c0);
    __m256 c3 = _mm256_fmadd_ps(a1, b0, c1);

    a0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(2, 2, 2, 2));
    a1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(2, 2, 2, 2));
    __m256 b1 = _mm256_permute2f128_ps(u1, u1, 0x00);
    __m256 c4 = _mm256_mul_ps(a0, b1);
    __m256 c5 = _mm256_mul_ps(a1, b1);

    a0 = _mm256_shuffle_ps(t0, t0, _MM_SHUFFLE(3, 3, 3, 3));
    a1 = _mm256_shuffle_ps(t1, t1, _MM_SHUFFLE(3, 3, 3, 3));
    b1 = _mm256_permute2f128_ps(u1, u1, 0x11);
    __m256 c6 = _mm256_fmadd_ps(a0, b1, c4);
    __m256 c7 = _mm256_fmadd_ps(a1, b1, c5);

    t0 = _mm256_add_ps(c2, c6);
    t1 = _mm256_add_ps(c3, c7);

    __m256 vTemp = _mm256_unpacklo_ps(t0, t1);
    __m256 vTemp2 = _mm256_unpackhi_ps(t0, t1);
    __m256 vTemp3 = _mm256_permute2f128_ps(vTemp, vTemp2, 0x20);
    __m256 vTemp4 = _mm256_permute2f128_ps(vTemp, vTemp2, 0x31);
    vTemp = _mm256_unpacklo_ps(vTemp3, vTemp4);
    vTemp2 = _mm256_unpackhi_ps(vTemp3, vTemp4);
    t0 = _mm256_permute2f128_ps(vTemp, vTemp2, 0x20);
    t1 = _mm256_permute2f128_ps(vTemp, vTemp2, 0x31);

    Matrix mResult;
    mResult.r[0] = _mm256_castps256_ps128(t0);
    mResult.r[1] = _mm256_extractf128_ps(t0, 1);
    mResult.r[2] = _mm256_castps256_ps128(t1);
    mResult.r[3] = _mm256_extractf128_ps(t1, 1);
    return mResult;

#elif defined(_MATH_SSE_INTRINSICS_)
    return MatrixTranspose(MatrixMultiply(M1, M2));
#endif
}

inline Matrix MathCallConv MatrixTranspose(FXMMATRIX M)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    // Original matrix:
    //
    //     m00m01m02m03
    //     m10m11m12m13
    //     m20m21m22m23
    //     m30m31m32m33

    Matrix P;
    P.r[0] = VectorMergeXY(M.r[0], M.r[2]); // m00m20m01m21
    P.r[1] = VectorMergeXY(M.r[1], M.r[3]); // m10m30m11m31
    P.r[2] = VectorMergeZW(M.r[0], M.r[2]); // m02m22m03m23
    P.r[3] = VectorMergeZW(M.r[1], M.r[3]); // m12m32m13m33

    Matrix MT;
    MT.r[0] = VectorMergeXY(P.r[0], P.r[1]); // m00m10m20m30
    MT.r[1] = VectorMergeZW(P.r[0], P.r[1]); // m01m11m21m31
    MT.r[2] = VectorMergeXY(P.r[2], P.r[3]); // m02m12m22m32
    MT.r[3] = VectorMergeZW(P.r[2], P.r[3]); // m03m13m23m33
    return MT;

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float32x4x2_t P0 = vzipq_f32(M.r[0], M.r[2]);
    float32x4x2_t P1 = vzipq_f32(M.r[1], M.r[3]);

    float32x4x2_t T0 = vzipq_f32(P0.val[0], P1.val[0]);
    float32x4x2_t T1 = vzipq_f32(P0.val[1], P1.val[1]);

    Matrix mResult;
    mResult.r[0] = T0.val[0];
    mResult.r[1] = T0.val[1];
    mResult.r[2] = T1.val[0];
    mResult.r[3] = T1.val[1];
    return mResult;
#elif defined(_MATH_AVX2_INTRINSICS_)
    __m256 t0 = _mm256_castps128_ps256(M.r[0]);
    t0 = _mm256_insertf128_ps(t0, M.r[1], 1);
    __m256 t1 = _mm256_castps128_ps256(M.r[2]);
    t1 = _mm256_insertf128_ps(t1, M.r[3], 1);

    __m256 vTemp = _mm256_unpacklo_ps(t0, t1);
    __m256 vTemp2 = _mm256_unpackhi_ps(t0, t1);
    __m256 vTemp3 = _mm256_permute2f128_ps(vTemp, vTemp2, 0x20);
    __m256 vTemp4 = _mm256_permute2f128_ps(vTemp, vTemp2, 0x31);
    vTemp = _mm256_unpacklo_ps(vTemp3, vTemp4);
    vTemp2 = _mm256_unpackhi_ps(vTemp3, vTemp4);
    t0 = _mm256_permute2f128_ps(vTemp, vTemp2, 0x20);
    t1 = _mm256_permute2f128_ps(vTemp, vTemp2, 0x31);

    Matrix mResult;
    mResult.r[0] = _mm256_castps256_ps128(t0);
    mResult.r[1] = _mm256_extractf128_ps(t0, 1);
    mResult.r[2] = _mm256_castps256_ps128(t1);
    mResult.r[3] = _mm256_extractf128_ps(t1, 1);
    return mResult;
#elif defined(_MATH_SSE_INTRINSICS_)
    // x.x,x.y,y.x,y.y
    Vector vTemp1 = _mm_shuffle_ps(M.r[0], M.r[1], _MM_SHUFFLE(1, 0, 1, 0));
    // x.z,x.w,y.z,y.w
    Vector vTemp3 = _mm_shuffle_ps(M.r[0], M.r[1], _MM_SHUFFLE(3, 2, 3, 2));
    // z.x,z.y,w.x,w.y
    Vector vTemp2 = _mm_shuffle_ps(M.r[2], M.r[3], _MM_SHUFFLE(1, 0, 1, 0));
    // z.z,z.w,w.z,w.w
    Vector vTemp4 = _mm_shuffle_ps(M.r[2], M.r[3], _MM_SHUFFLE(3, 2, 3, 2));

    Matrix mResult;
    // x.x,y.x,z.x,w.x
    mResult.r[0] = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(2, 0, 2, 0));
    // x.y,y.y,z.y,w.y
    mResult.r[1] = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(3, 1, 3, 1));
    // x.z,y.z,z.z,w.z
    mResult.r[2] = _mm_shuffle_ps(vTemp3, vTemp4, _MM_SHUFFLE(2, 0, 2, 0));
    // x.w,y.w,z.w,w.w
    mResult.r[3] = _mm_shuffle_ps(vTemp3, vTemp4, _MM_SHUFFLE(3, 1, 3, 1));
    return mResult;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Matrix MathCallConv MatrixInverse
(
    Vector* pDeterminant,
    FXMMATRIX  M
)noexcept{
#if defined(_MATH_NO_INTRINSICS_) || defined(_MATH_ARM_NEON_INTRINSICS_)

    Matrix MT = MatrixTranspose(M);

    Vector V0[4], V1[4];
    V0[0] = VectorSwizzle<MATH_SWIZZLE_X, MATH_SWIZZLE_X, MATH_SWIZZLE_Y, MATH_SWIZZLE_Y>(MT.r[2]);
    V1[0] = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_W, MATH_SWIZZLE_Z, MATH_SWIZZLE_W>(MT.r[3]);
    V0[1] = VectorSwizzle<MATH_SWIZZLE_X, MATH_SWIZZLE_X, MATH_SWIZZLE_Y, MATH_SWIZZLE_Y>(MT.r[0]);
    V1[1] = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_W, MATH_SWIZZLE_Z, MATH_SWIZZLE_W>(MT.r[1]);
    V0[2] = VectorPermute<MATH_PERMUTE_0X, MATH_PERMUTE_0Z, MATH_PERMUTE_1X, MATH_PERMUTE_1Z>(MT.r[2], MT.r[0]);
    V1[2] = VectorPermute<MATH_PERMUTE_0Y, MATH_PERMUTE_0W, MATH_PERMUTE_1Y, MATH_PERMUTE_1W>(MT.r[3], MT.r[1]);

    Vector D0 = VectorMultiply(V0[0], V1[0]);
    Vector D1 = VectorMultiply(V0[1], V1[1]);
    Vector D2 = VectorMultiply(V0[2], V1[2]);

    V0[0] = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_W, MATH_SWIZZLE_Z, MATH_SWIZZLE_W>(MT.r[2]);
    V1[0] = VectorSwizzle<MATH_SWIZZLE_X, MATH_SWIZZLE_X, MATH_SWIZZLE_Y, MATH_SWIZZLE_Y>(MT.r[3]);
    V0[1] = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_W, MATH_SWIZZLE_Z, MATH_SWIZZLE_W>(MT.r[0]);
    V1[1] = VectorSwizzle<MATH_SWIZZLE_X, MATH_SWIZZLE_X, MATH_SWIZZLE_Y, MATH_SWIZZLE_Y>(MT.r[1]);
    V0[2] = VectorPermute<MATH_PERMUTE_0Y, MATH_PERMUTE_0W, MATH_PERMUTE_1Y, MATH_PERMUTE_1W>(MT.r[2], MT.r[0]);
    V1[2] = VectorPermute<MATH_PERMUTE_0X, MATH_PERMUTE_0Z, MATH_PERMUTE_1X, MATH_PERMUTE_1Z>(MT.r[3], MT.r[1]);

    D0 = VectorNegativeMultiplySubtract(V0[0], V1[0], D0);
    D1 = VectorNegativeMultiplySubtract(V0[1], V1[1], D1);
    D2 = VectorNegativeMultiplySubtract(V0[2], V1[2], D2);

    V0[0] = VectorSwizzle<MATH_SWIZZLE_Y, MATH_SWIZZLE_Z, MATH_SWIZZLE_X, MATH_SWIZZLE_Y>(MT.r[1]);
    V1[0] = VectorPermute<MATH_PERMUTE_1Y, MATH_PERMUTE_0Y, MATH_PERMUTE_0W, MATH_PERMUTE_0X>(D0, D2);
    V0[1] = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_X, MATH_SWIZZLE_Y, MATH_SWIZZLE_X>(MT.r[0]);
    V1[1] = VectorPermute<MATH_PERMUTE_0W, MATH_PERMUTE_1Y, MATH_PERMUTE_0Y, MATH_PERMUTE_0Z>(D0, D2);
    V0[2] = VectorSwizzle<MATH_SWIZZLE_Y, MATH_SWIZZLE_Z, MATH_SWIZZLE_X, MATH_SWIZZLE_Y>(MT.r[3]);
    V1[2] = VectorPermute<MATH_PERMUTE_1W, MATH_PERMUTE_0Y, MATH_PERMUTE_0W, MATH_PERMUTE_0X>(D1, D2);
    V0[3] = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_X, MATH_SWIZZLE_Y, MATH_SWIZZLE_X>(MT.r[2]);
    V1[3] = VectorPermute<MATH_PERMUTE_0W, MATH_PERMUTE_1W, MATH_PERMUTE_0Y, MATH_PERMUTE_0Z>(D1, D2);

    Vector C0 = VectorMultiply(V0[0], V1[0]);
    Vector C2 = VectorMultiply(V0[1], V1[1]);
    Vector C4 = VectorMultiply(V0[2], V1[2]);
    Vector C6 = VectorMultiply(V0[3], V1[3]);

    V0[0] = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_W, MATH_SWIZZLE_Y, MATH_SWIZZLE_Z>(MT.r[1]);
    V1[0] = VectorPermute<MATH_PERMUTE_0W, MATH_PERMUTE_0X, MATH_PERMUTE_0Y, MATH_PERMUTE_1X>(D0, D2);
    V0[1] = VectorSwizzle<MATH_SWIZZLE_W, MATH_SWIZZLE_Z, MATH_SWIZZLE_W, MATH_SWIZZLE_Y>(MT.r[0]);
    V1[1] = VectorPermute<MATH_PERMUTE_0Z, MATH_PERMUTE_0Y, MATH_PERMUTE_1X, MATH_PERMUTE_0X>(D0, D2);
    V0[2] = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_W, MATH_SWIZZLE_Y, MATH_SWIZZLE_Z>(MT.r[3]);
    V1[2] = VectorPermute<MATH_PERMUTE_0W, MATH_PERMUTE_0X, MATH_PERMUTE_0Y, MATH_PERMUTE_1Z>(D1, D2);
    V0[3] = VectorSwizzle<MATH_SWIZZLE_W, MATH_SWIZZLE_Z, MATH_SWIZZLE_W, MATH_SWIZZLE_Y>(MT.r[2]);
    V1[3] = VectorPermute<MATH_PERMUTE_0Z, MATH_PERMUTE_0Y, MATH_PERMUTE_1Z, MATH_PERMUTE_0X>(D1, D2);

    C0 = VectorNegativeMultiplySubtract(V0[0], V1[0], C0);
    C2 = VectorNegativeMultiplySubtract(V0[1], V1[1], C2);
    C4 = VectorNegativeMultiplySubtract(V0[2], V1[2], C4);
    C6 = VectorNegativeMultiplySubtract(V0[3], V1[3], C6);

    V0[0] = VectorSwizzle<MATH_SWIZZLE_W, MATH_SWIZZLE_X, MATH_SWIZZLE_W, MATH_SWIZZLE_X>(MT.r[1]);
    V1[0] = VectorPermute<MATH_PERMUTE_0Z, MATH_PERMUTE_1Y, MATH_PERMUTE_1X, MATH_PERMUTE_0Z>(D0, D2);
    V0[1] = VectorSwizzle<MATH_SWIZZLE_Y, MATH_SWIZZLE_W, MATH_SWIZZLE_X, MATH_SWIZZLE_Z>(MT.r[0]);
    V1[1] = VectorPermute<MATH_PERMUTE_1Y, MATH_PERMUTE_0X, MATH_PERMUTE_0W, MATH_PERMUTE_1X>(D0, D2);
    V0[2] = VectorSwizzle<MATH_SWIZZLE_W, MATH_SWIZZLE_X, MATH_SWIZZLE_W, MATH_SWIZZLE_X>(MT.r[3]);
    V1[2] = VectorPermute<MATH_PERMUTE_0Z, MATH_PERMUTE_1W, MATH_PERMUTE_1Z, MATH_PERMUTE_0Z>(D1, D2);
    V0[3] = VectorSwizzle<MATH_SWIZZLE_Y, MATH_SWIZZLE_W, MATH_SWIZZLE_X, MATH_SWIZZLE_Z>(MT.r[2]);
    V1[3] = VectorPermute<MATH_PERMUTE_1W, MATH_PERMUTE_0X, MATH_PERMUTE_0W, MATH_PERMUTE_1Z>(D1, D2);

    Vector C1 = VectorNegativeMultiplySubtract(V0[0], V1[0], C0);
    C0 = VectorMultiplyAdd(V0[0], V1[0], C0);
    Vector C3 = VectorMultiplyAdd(V0[1], V1[1], C2);
    C2 = VectorNegativeMultiplySubtract(V0[1], V1[1], C2);
    Vector C5 = VectorNegativeMultiplySubtract(V0[2], V1[2], C4);
    C4 = VectorMultiplyAdd(V0[2], V1[2], C4);
    Vector C7 = VectorMultiplyAdd(V0[3], V1[3], C6);
    C6 = VectorNegativeMultiplySubtract(V0[3], V1[3], C6);

    Matrix R;
    R.r[0] = VectorSelect(C0, C1, g_Select0101.v);
    R.r[1] = VectorSelect(C2, C3, g_Select0101.v);
    R.r[2] = VectorSelect(C4, C5, g_Select0101.v);
    R.r[3] = VectorSelect(C6, C7, g_Select0101.v);

    Vector Determinant = Vector4Dot(R.r[0], MT.r[0]);

    if(pDeterminant != nullptr)
        *pDeterminant = Determinant;

    Vector Reciprocal = VectorReciprocal(Determinant);

    Matrix Result;
    Result.r[0] = VectorMultiply(R.r[0], Reciprocal);
    Result.r[1] = VectorMultiply(R.r[1], Reciprocal);
    Result.r[2] = VectorMultiply(R.r[2], Reciprocal);
    Result.r[3] = VectorMultiply(R.r[3], Reciprocal);
    return Result;

#elif defined(_MATH_SSE_INTRINSICS_)
    // Transpose matrix
    Vector vTemp1 = _mm_shuffle_ps(M.r[0], M.r[1], _MM_SHUFFLE(1, 0, 1, 0));
    Vector vTemp3 = _mm_shuffle_ps(M.r[0], M.r[1], _MM_SHUFFLE(3, 2, 3, 2));
    Vector vTemp2 = _mm_shuffle_ps(M.r[2], M.r[3], _MM_SHUFFLE(1, 0, 1, 0));
    Vector vTemp4 = _mm_shuffle_ps(M.r[2], M.r[3], _MM_SHUFFLE(3, 2, 3, 2));

    Matrix MT;
    MT.r[0] = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(2, 0, 2, 0));
    MT.r[1] = _mm_shuffle_ps(vTemp1, vTemp2, _MM_SHUFFLE(3, 1, 3, 1));
    MT.r[2] = _mm_shuffle_ps(vTemp3, vTemp4, _MM_SHUFFLE(2, 0, 2, 0));
    MT.r[3] = _mm_shuffle_ps(vTemp3, vTemp4, _MM_SHUFFLE(3, 1, 3, 1));

    Vector V00 = MATH_PERMUTE_PS(MT.r[2], _MM_SHUFFLE(1, 1, 0, 0));
    Vector V10 = MATH_PERMUTE_PS(MT.r[3], _MM_SHUFFLE(3, 2, 3, 2));
    Vector V01 = MATH_PERMUTE_PS(MT.r[0], _MM_SHUFFLE(1, 1, 0, 0));
    Vector V11 = MATH_PERMUTE_PS(MT.r[1], _MM_SHUFFLE(3, 2, 3, 2));
    Vector V02 = _mm_shuffle_ps(MT.r[2], MT.r[0], _MM_SHUFFLE(2, 0, 2, 0));
    Vector V12 = _mm_shuffle_ps(MT.r[3], MT.r[1], _MM_SHUFFLE(3, 1, 3, 1));

    Vector D0 = _mm_mul_ps(V00, V10);
    Vector D1 = _mm_mul_ps(V01, V11);
    Vector D2 = _mm_mul_ps(V02, V12);

    V00 = MATH_PERMUTE_PS(MT.r[2], _MM_SHUFFLE(3, 2, 3, 2));
    V10 = MATH_PERMUTE_PS(MT.r[3], _MM_SHUFFLE(1, 1, 0, 0));
    V01 = MATH_PERMUTE_PS(MT.r[0], _MM_SHUFFLE(3, 2, 3, 2));
    V11 = MATH_PERMUTE_PS(MT.r[1], _MM_SHUFFLE(1, 1, 0, 0));
    V02 = _mm_shuffle_ps(MT.r[2], MT.r[0], _MM_SHUFFLE(3, 1, 3, 1));
    V12 = _mm_shuffle_ps(MT.r[3], MT.r[1], _MM_SHUFFLE(2, 0, 2, 0));

    D0 = MATH_FNMADD_PS(V00, V10, D0);
    D1 = MATH_FNMADD_PS(V01, V11, D1);
    D2 = MATH_FNMADD_PS(V02, V12, D2);
    // V11 = D0Y,D0W,D2Y,D2Y
    V11 = _mm_shuffle_ps(D0, D2, _MM_SHUFFLE(1, 1, 3, 1));
    V00 = MATH_PERMUTE_PS(MT.r[1], _MM_SHUFFLE(1, 0, 2, 1));
    V10 = _mm_shuffle_ps(V11, D0, _MM_SHUFFLE(0, 3, 0, 2));
    V01 = MATH_PERMUTE_PS(MT.r[0], _MM_SHUFFLE(0, 1, 0, 2));
    V11 = _mm_shuffle_ps(V11, D0, _MM_SHUFFLE(2, 1, 2, 1));
    // V13 = D1Y,D1W,D2W,D2W
    Vector V13 = _mm_shuffle_ps(D1, D2, _MM_SHUFFLE(3, 3, 3, 1));
    V02 = MATH_PERMUTE_PS(MT.r[3], _MM_SHUFFLE(1, 0, 2, 1));
    V12 = _mm_shuffle_ps(V13, D1, _MM_SHUFFLE(0, 3, 0, 2));
    Vector V03 = MATH_PERMUTE_PS(MT.r[2], _MM_SHUFFLE(0, 1, 0, 2));
    V13 = _mm_shuffle_ps(V13, D1, _MM_SHUFFLE(2, 1, 2, 1));

    Vector C0 = _mm_mul_ps(V00, V10);
    Vector C2 = _mm_mul_ps(V01, V11);
    Vector C4 = _mm_mul_ps(V02, V12);
    Vector C6 = _mm_mul_ps(V03, V13);

    // V11 = D0X,D0Y,D2X,D2X
    V11 = _mm_shuffle_ps(D0, D2, _MM_SHUFFLE(0, 0, 1, 0));
    V00 = MATH_PERMUTE_PS(MT.r[1], _MM_SHUFFLE(2, 1, 3, 2));
    V10 = _mm_shuffle_ps(D0, V11, _MM_SHUFFLE(2, 1, 0, 3));
    V01 = MATH_PERMUTE_PS(MT.r[0], _MM_SHUFFLE(1, 3, 2, 3));
    V11 = _mm_shuffle_ps(D0, V11, _MM_SHUFFLE(0, 2, 1, 2));
    // V13 = D1X,D1Y,D2Z,D2Z
    V13 = _mm_shuffle_ps(D1, D2, _MM_SHUFFLE(2, 2, 1, 0));
    V02 = MATH_PERMUTE_PS(MT.r[3], _MM_SHUFFLE(2, 1, 3, 2));
    V12 = _mm_shuffle_ps(D1, V13, _MM_SHUFFLE(2, 1, 0, 3));
    V03 = MATH_PERMUTE_PS(MT.r[2], _MM_SHUFFLE(1, 3, 2, 3));
    V13 = _mm_shuffle_ps(D1, V13, _MM_SHUFFLE(0, 2, 1, 2));

    C0 = MATH_FNMADD_PS(V00, V10, C0);
    C2 = MATH_FNMADD_PS(V01, V11, C2);
    C4 = MATH_FNMADD_PS(V02, V12, C4);
    C6 = MATH_FNMADD_PS(V03, V13, C6);

    V00 = MATH_PERMUTE_PS(MT.r[1], _MM_SHUFFLE(0, 3, 0, 3));
    // V10 = D0Z,D0Z,D2X,D2Y
    V10 = _mm_shuffle_ps(D0, D2, _MM_SHUFFLE(1, 0, 2, 2));
    V10 = MATH_PERMUTE_PS(V10, _MM_SHUFFLE(0, 2, 3, 0));
    V01 = MATH_PERMUTE_PS(MT.r[0], _MM_SHUFFLE(2, 0, 3, 1));
    // V11 = D0X,D0W,D2X,D2Y
    V11 = _mm_shuffle_ps(D0, D2, _MM_SHUFFLE(1, 0, 3, 0));
    V11 = MATH_PERMUTE_PS(V11, _MM_SHUFFLE(2, 1, 0, 3));
    V02 = MATH_PERMUTE_PS(MT.r[3], _MM_SHUFFLE(0, 3, 0, 3));
    // V12 = D1Z,D1Z,D2Z,D2W
    V12 = _mm_shuffle_ps(D1, D2, _MM_SHUFFLE(3, 2, 2, 2));
    V12 = MATH_PERMUTE_PS(V12, _MM_SHUFFLE(0, 2, 3, 0));
    V03 = MATH_PERMUTE_PS(MT.r[2], _MM_SHUFFLE(2, 0, 3, 1));
    // V13 = D1X,D1W,D2Z,D2W
    V13 = _mm_shuffle_ps(D1, D2, _MM_SHUFFLE(3, 2, 3, 0));
    V13 = MATH_PERMUTE_PS(V13, _MM_SHUFFLE(2, 1, 0, 3));

    V00 = _mm_mul_ps(V00, V10);
    V01 = _mm_mul_ps(V01, V11);
    V02 = _mm_mul_ps(V02, V12);
    V03 = _mm_mul_ps(V03, V13);
    Vector C1 = _mm_sub_ps(C0, V00);
    C0 = _mm_add_ps(C0, V00);
    Vector C3 = _mm_add_ps(C2, V01);
    C2 = _mm_sub_ps(C2, V01);
    Vector C5 = _mm_sub_ps(C4, V02);
    C4 = _mm_add_ps(C4, V02);
    Vector C7 = _mm_add_ps(C6, V03);
    C6 = _mm_sub_ps(C6, V03);

    C0 = _mm_shuffle_ps(C0, C1, _MM_SHUFFLE(3, 1, 2, 0));
    C2 = _mm_shuffle_ps(C2, C3, _MM_SHUFFLE(3, 1, 2, 0));
    C4 = _mm_shuffle_ps(C4, C5, _MM_SHUFFLE(3, 1, 2, 0));
    C6 = _mm_shuffle_ps(C6, C7, _MM_SHUFFLE(3, 1, 2, 0));
    C0 = MATH_PERMUTE_PS(C0, _MM_SHUFFLE(3, 1, 2, 0));
    C2 = MATH_PERMUTE_PS(C2, _MM_SHUFFLE(3, 1, 2, 0));
    C4 = MATH_PERMUTE_PS(C4, _MM_SHUFFLE(3, 1, 2, 0));
    C6 = MATH_PERMUTE_PS(C6, _MM_SHUFFLE(3, 1, 2, 0));
    // Get the determinant
    Vector vTemp = Vector4Dot(C0, MT.r[0]);
    if(pDeterminant != nullptr)
        *pDeterminant = vTemp;
    vTemp = _mm_div_ps(g_One, vTemp);
    Matrix mResult;
    mResult.r[0] = _mm_mul_ps(C0, vTemp);
    mResult.r[1] = _mm_mul_ps(C2, vTemp);
    mResult.r[2] = _mm_mul_ps(C4, vTemp);
    mResult.r[3] = _mm_mul_ps(C6, vTemp);
    return mResult;
#endif
}

inline Matrix MathCallConv MatrixVectorTensorProduct
(
    VectorArg V1,
    VectorArg V2
)noexcept{
    Matrix mResult;
    mResult.r[0] = VectorMultiply(VectorSplatX(V2), V1);
    mResult.r[1] = VectorMultiply(VectorSplatY(V2), V1);
    mResult.r[2] = VectorMultiply(VectorSplatZ(V2), V1);
    mResult.r[3] = VectorMultiply(VectorSplatW(V2), V1);
    return mResult;
}

inline Vector MathCallConv MatrixDeterminant(FXMMATRIX M)noexcept{
    static const VectorF32 Sign = { { { 1.0f, -1.0f, 1.0f, -1.0f } } };

    Vector V0 = VectorSwizzle<MATH_SWIZZLE_Y, MATH_SWIZZLE_X, MATH_SWIZZLE_X, MATH_SWIZZLE_X>(M.r[2]);
    Vector V1 = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_Z, MATH_SWIZZLE_Y, MATH_SWIZZLE_Y>(M.r[3]);
    Vector V2 = VectorSwizzle<MATH_SWIZZLE_Y, MATH_SWIZZLE_X, MATH_SWIZZLE_X, MATH_SWIZZLE_X>(M.r[2]);
    Vector V3 = VectorSwizzle<MATH_SWIZZLE_W, MATH_SWIZZLE_W, MATH_SWIZZLE_W, MATH_SWIZZLE_Z>(M.r[3]);
    Vector V4 = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_Z, MATH_SWIZZLE_Y, MATH_SWIZZLE_Y>(M.r[2]);
    Vector V5 = VectorSwizzle<MATH_SWIZZLE_W, MATH_SWIZZLE_W, MATH_SWIZZLE_W, MATH_SWIZZLE_Z>(M.r[3]);

    Vector P0 = VectorMultiply(V0, V1);
    Vector P1 = VectorMultiply(V2, V3);
    Vector P2 = VectorMultiply(V4, V5);

    V0 = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_Z, MATH_SWIZZLE_Y, MATH_SWIZZLE_Y>(M.r[2]);
    V1 = VectorSwizzle<MATH_SWIZZLE_Y, MATH_SWIZZLE_X, MATH_SWIZZLE_X, MATH_SWIZZLE_X>(M.r[3]);
    V2 = VectorSwizzle<MATH_SWIZZLE_W, MATH_SWIZZLE_W, MATH_SWIZZLE_W, MATH_SWIZZLE_Z>(M.r[2]);
    V3 = VectorSwizzle<MATH_SWIZZLE_Y, MATH_SWIZZLE_X, MATH_SWIZZLE_X, MATH_SWIZZLE_X>(M.r[3]);
    V4 = VectorSwizzle<MATH_SWIZZLE_W, MATH_SWIZZLE_W, MATH_SWIZZLE_W, MATH_SWIZZLE_Z>(M.r[2]);
    V5 = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_Z, MATH_SWIZZLE_Y, MATH_SWIZZLE_Y>(M.r[3]);

    P0 = VectorNegativeMultiplySubtract(V0, V1, P0);
    P1 = VectorNegativeMultiplySubtract(V2, V3, P1);
    P2 = VectorNegativeMultiplySubtract(V4, V5, P2);

    V0 = VectorSwizzle<MATH_SWIZZLE_W, MATH_SWIZZLE_W, MATH_SWIZZLE_W, MATH_SWIZZLE_Z>(M.r[1]);
    V1 = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_Z, MATH_SWIZZLE_Y, MATH_SWIZZLE_Y>(M.r[1]);
    V2 = VectorSwizzle<MATH_SWIZZLE_Y, MATH_SWIZZLE_X, MATH_SWIZZLE_X, MATH_SWIZZLE_X>(M.r[1]);

    Vector S = VectorMultiply(M.r[0], Sign.v);
    Vector R = VectorMultiply(V0, P0);
    R = VectorNegativeMultiplySubtract(V1, P1, R);
    R = VectorMultiplyAdd(V2, P2, R);

    return Vector4Dot(S, R);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define MATH3RANKDECOMPOSE(a, b, c, x, y, z)      \
    if((x) < (y))                   \
    {                               \
        if((y) < (z))               \
        {                           \
            (a) = 2;                \
            (b) = 1;                \
            (c) = 0;                \
        }                           \
        else                        \
        {                           \
            (a) = 1;                \
                                    \
            if((x) < (z))           \
            {                       \
                (b) = 2;            \
                (c) = 0;            \
            }                       \
            else                    \
            {                       \
                (b) = 0;            \
                (c) = 2;            \
            }                       \
        }                           \
    }                               \
    else                            \
    {                               \
        if((x) < (z))               \
        {                           \
            (a) = 2;                \
            (b) = 0;                \
            (c) = 1;                \
        }                           \
        else                        \
        {                           \
            (a) = 0;                \
                                    \
            if((y) < (z))           \
            {                       \
                (b) = 2;            \
                (c) = 1;            \
            }                       \
            else                    \
            {                       \
                (b) = 1;            \
                (c) = 2;            \
            }                       \
        }                           \
    }

#define MATH3_DECOMP_EPSILON 0.0001f


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline bool MathCallConv MatrixDecompose
(
    Vector* outScale,
    Vector* outRotQuat,
    Vector* outTrans,
    FXMMATRIX M
)noexcept{
    static const Vector* pvCanonicalBasis[3] = {
        &g_IdentityC0.v,
        &g_IdentityC1.v,
        &g_IdentityC2.v
    };

    assert(outScale != nullptr);
    assert(outRotQuat != nullptr);
    assert(outTrans != nullptr);

    // Get the translation
    outTrans[0] = M.r[3];

    Vector* ppvBasis[3];
    Matrix matTemp;
    ppvBasis[0] = &matTemp.r[0];
    ppvBasis[1] = &matTemp.r[1];
    ppvBasis[2] = &matTemp.r[2];

    matTemp.r[0] = M.r[0];
    matTemp.r[1] = M.r[1];
    matTemp.r[2] = M.r[2];
    matTemp.r[3] = g_IdentityC3.v;

    float* pfScales = reinterpret_cast<float*>(outScale);

    size_t a, b, c;
    VectorGetXPtr(&pfScales[0], Vector3Length(ppvBasis[0][0]));
    VectorGetXPtr(&pfScales[1], Vector3Length(ppvBasis[1][0]));
    VectorGetXPtr(&pfScales[2], Vector3Length(ppvBasis[2][0]));
    pfScales[3] = 0.f;

    MATH3RANKDECOMPOSE(a, b, c, pfScales[0], pfScales[1], pfScales[2])

        if(pfScales[a] < MATH3_DECOMP_EPSILON){
            ppvBasis[a][0] = pvCanonicalBasis[a][0];
        }
    ppvBasis[a][0] = Vector3Normalize(ppvBasis[a][0]);

    if(pfScales[b] < MATH3_DECOMP_EPSILON){
        size_t aa, bb, cc;
        float fAbsX, fAbsY, fAbsZ;

        fAbsX = fabsf(VectorGetX(ppvBasis[a][0]));
        fAbsY = fabsf(VectorGetY(ppvBasis[a][0]));
        fAbsZ = fabsf(VectorGetZ(ppvBasis[a][0]));

        MATH3RANKDECOMPOSE(aa, bb, cc, fAbsX, fAbsY, fAbsZ)

            ppvBasis[b][0] = Vector3Cross(ppvBasis[a][0], pvCanonicalBasis[cc][0]);
    }

    ppvBasis[b][0] = Vector3Normalize(ppvBasis[b][0]);

    if(pfScales[c] < MATH3_DECOMP_EPSILON){
        ppvBasis[c][0] = Vector3Cross(ppvBasis[a][0], ppvBasis[b][0]);
    }

    ppvBasis[c][0] = Vector3Normalize(ppvBasis[c][0]);

    float fDet = VectorGetX(MatrixDeterminant(matTemp));

    // use Kramer's rule to check for handedness of coordinate system
    if(fDet < 0.0f){
        // switch coordinate system by negating the scale and inverting the basis vector on the x-axis
        pfScales[a] = -pfScales[a];
        ppvBasis[a][0] = VectorNegate(ppvBasis[a][0]);

        fDet = -fDet;
    }

    fDet -= 1.0f;
    fDet *= fDet;

    if(MATH3_DECOMP_EPSILON < fDet){
        // Non-SRT matrix encountered
        return false;
    }

    // generate the quaternion from the matrix
    outRotQuat[0] = QuaternionRotationMatrix(matTemp);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef MATH3_DECOMP_EPSILON
#undef MATH3RANKDECOMPOSE


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Matrix MathCallConv MatrixIdentity()noexcept{
    Matrix M;
    M.r[0] = g_IdentityC0.v;
    M.r[1] = g_IdentityC1.v;
    M.r[2] = g_IdentityC2.v;
    M.r[3] = g_IdentityC3.v;
    return M;
}

inline Matrix MathCallConv MatrixSetColumns
(
    VectorArg column0,
    VectorArg column1,
    VectorArg column2,
    VectorArg column3
)noexcept{
    Matrix M;
    M.r[0] = column0;
    M.r[1] = column1;
    M.r[2] = column2;
    M.r[3] = column3;
    return M;
}

inline Matrix MathCallConv MatrixSet
(
    float m00, float m01, float m02, float m03,
    float m10, float m11, float m12, float m13,
    float m20, float m21, float m22, float m23,
    float m30, float m31, float m32, float m33
)noexcept{
    return MatrixSetColumns(
        VectorSet(m00, m10, m20, m30),
        VectorSet(m01, m11, m21, m31),
        VectorSet(m02, m12, m22, m32),
        VectorSet(m03, m13, m23, m33)
    );
}

// NWB stores columns in `r[0..3]`. Translation therefore lives in the fourth
// column (`r[3]`).
inline Matrix MathCallConv MatrixTranslation
(
    float OffsetX,
    float OffsetY,
    float OffsetZ
)noexcept{
    const Vector translationColumn = VectorSet(OffsetX, OffsetY, OffsetZ, 1.0f);
    return MatrixSetColumns(
        g_IdentityC0.v,
        g_IdentityC1.v,
        g_IdentityC2.v,
        translationColumn
    );
}


inline Matrix MathCallConv MatrixTranslationFromVector(VectorArg Offset)noexcept{
    const Vector translationColumn = VectorSelect(g_IdentityC3.v, Offset, g_Select1110.v);
    return MatrixSetColumns(
        g_IdentityC0.v,
        g_IdentityC1.v,
        g_IdentityC2.v,
        translationColumn
    );
}

inline Matrix MathCallConv MatrixScaling
(
    float ScaleX,
    float ScaleY,
    float ScaleZ
)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return MatrixSetColumns(
        VectorSet(ScaleX, 0.0f, 0.0f, 0.0f),
        VectorSet(0.0f, ScaleY, 0.0f, 0.0f),
        VectorSet(0.0f, 0.0f, ScaleZ, 0.0f),
        g_IdentityC3.v
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    const Vector Zero = vdupq_n_f32(0);
    Matrix M;
    M.r[0] = vsetq_lane_f32(ScaleX, Zero, 0);
    M.r[1] = vsetq_lane_f32(ScaleY, Zero, 1);
    M.r[2] = vsetq_lane_f32(ScaleZ, Zero, 2);
    M.r[3] = g_IdentityC3.v;
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    Matrix M;
    M.r[0] = _mm_set_ps(0, 0, 0, ScaleX);
    M.r[1] = _mm_set_ps(0, 0, ScaleY, 0);
    M.r[2] = _mm_set_ps(0, ScaleZ, 0, 0);
    M.r[3] = g_IdentityC3.v;
    return M;
#endif
}

inline Matrix MathCallConv MatrixScalingFromVector(VectorArg Scale)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    return MatrixSetColumns(
        VectorSet(Scale.vector4_f32[0], 0.0f, 0.0f, 0.0f),
        VectorSet(0.0f, Scale.vector4_f32[1], 0.0f, 0.0f),
        VectorSet(0.0f, 0.0f, Scale.vector4_f32[2], 0.0f),
        g_IdentityC3.v
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    Matrix M;
    M.r[0] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(Scale), g_MaskX));
    M.r[1] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(Scale), g_MaskY));
    M.r[2] = vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(Scale), g_MaskZ));
    M.r[3] = g_IdentityC3.v;
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    Matrix M;
    M.r[0] = _mm_and_ps(Scale, g_MaskX);
    M.r[1] = _mm_and_ps(Scale, g_MaskY);
    M.r[2] = _mm_and_ps(Scale, g_MaskZ);
    M.r[3] = g_IdentityC3.v;
    return M;
#endif
}

inline Matrix MathCallConv MatrixRotationX(float Angle)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    float    fSinAngle;
    float    fCosAngle;
    ScalarSinCos(&fSinAngle, &fCosAngle, Angle);
    return MatrixSetColumns(
        g_IdentityC0.v,
        VectorSet(0.0f, fCosAngle, fSinAngle, 0.0f),
        VectorSet(0.0f, -fSinAngle, fCosAngle, 0.0f),
        g_IdentityC3.v
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float    fSinAngle;
    float    fCosAngle;
    ScalarSinCos(&fSinAngle, &fCosAngle, Angle);

    const float32x4_t Zero = vdupq_n_f32(0);

    float32x4_t T1 = vsetq_lane_f32(fCosAngle, Zero, 1);
    T1 = vsetq_lane_f32(fSinAngle, T1, 2);

    float32x4_t T2 = vsetq_lane_f32(-fSinAngle, Zero, 1);
    T2 = vsetq_lane_f32(fCosAngle, T2, 2);

    Matrix M;
    M.r[0] = g_IdentityC0.v;
    M.r[1] = T1;
    M.r[2] = T2;
    M.r[3] = g_IdentityC3.v;
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    float    SinAngle;
    float    CosAngle;
    ScalarSinCos(&SinAngle, &CosAngle, Angle);

    Vector vSin = _mm_set_ss(SinAngle);
    Vector vCos = _mm_set_ss(CosAngle);
    // x = 0,y = cos,z = sin, w = 0
    vCos = _mm_shuffle_ps(vCos, vSin, _MM_SHUFFLE(3, 0, 0, 3));
    Matrix M;
    M.r[0] = g_IdentityC0;
    M.r[1] = vCos;
    // x = 0,y = sin,z = cos, w = 0
    vCos = MATH_PERMUTE_PS(vCos, _MM_SHUFFLE(3, 1, 2, 0));
    // x = 0,y = -sin,z = cos, w = 0
    vCos = _mm_mul_ps(vCos, g_NegateY);
    M.r[2] = vCos;
    M.r[3] = g_IdentityC3;
    return M;
#endif
}

inline Matrix MathCallConv MatrixRotationY(float Angle)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    float    fSinAngle;
    float    fCosAngle;
    ScalarSinCos(&fSinAngle, &fCosAngle, Angle);
    return MatrixSetColumns(
        VectorSet(fCosAngle, 0.0f, -fSinAngle, 0.0f),
        g_IdentityC1.v,
        VectorSet(fSinAngle, 0.0f, fCosAngle, 0.0f),
        g_IdentityC3.v
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float    fSinAngle;
    float    fCosAngle;
    ScalarSinCos(&fSinAngle, &fCosAngle, Angle);

    const float32x4_t Zero = vdupq_n_f32(0);

    float32x4_t T0 = vsetq_lane_f32(fCosAngle, Zero, 0);
    T0 = vsetq_lane_f32(-fSinAngle, T0, 2);

    float32x4_t T2 = vsetq_lane_f32(fSinAngle, Zero, 0);
    T2 = vsetq_lane_f32(fCosAngle, T2, 2);

    Matrix M;
    M.r[0] = T0;
    M.r[1] = g_IdentityC1.v;
    M.r[2] = T2;
    M.r[3] = g_IdentityC3.v;
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    float    SinAngle;
    float    CosAngle;
    ScalarSinCos(&SinAngle, &CosAngle, Angle);

    Vector vSin = _mm_set_ss(SinAngle);
    Vector vCos = _mm_set_ss(CosAngle);
    // x = sin,y = 0,z = cos, w = 0
    vSin = _mm_shuffle_ps(vSin, vCos, _MM_SHUFFLE(3, 0, 3, 0));
    Matrix M;
    M.r[2] = vSin;
    M.r[1] = g_IdentityC1;
    // x = cos,y = 0,z = sin, w = 0
    vSin = MATH_PERMUTE_PS(vSin, _MM_SHUFFLE(3, 0, 1, 2));
    // x = cos,y = 0,z = -sin, w = 0
    vSin = _mm_mul_ps(vSin, g_NegateZ);
    M.r[0] = vSin;
    M.r[3] = g_IdentityC3;
    return M;
#endif
}

inline Matrix MathCallConv MatrixRotationZ(float Angle)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    float    fSinAngle;
    float    fCosAngle;
    ScalarSinCos(&fSinAngle, &fCosAngle, Angle);
    return MatrixSetColumns(
        VectorSet(fCosAngle, fSinAngle, 0.0f, 0.0f),
        VectorSet(-fSinAngle, fCosAngle, 0.0f, 0.0f),
        g_IdentityC2.v,
        g_IdentityC3.v
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float    fSinAngle;
    float    fCosAngle;
    ScalarSinCos(&fSinAngle, &fCosAngle, Angle);

    const float32x4_t Zero = vdupq_n_f32(0);

    float32x4_t T0 = vsetq_lane_f32(fCosAngle, Zero, 0);
    T0 = vsetq_lane_f32(fSinAngle, T0, 1);

    float32x4_t T1 = vsetq_lane_f32(-fSinAngle, Zero, 0);
    T1 = vsetq_lane_f32(fCosAngle, T1, 1);

    Matrix M;
    M.r[0] = T0;
    M.r[1] = T1;
    M.r[2] = g_IdentityC2.v;
    M.r[3] = g_IdentityC3.v;
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    float    SinAngle;
    float    CosAngle;
    ScalarSinCos(&SinAngle, &CosAngle, Angle);

    Vector vSin = _mm_set_ss(SinAngle);
    Vector vCos = _mm_set_ss(CosAngle);
    // x = cos,y = sin,z = 0, w = 0
    vCos = _mm_unpacklo_ps(vCos, vSin);
    Matrix M;
    M.r[0] = vCos;
    // x = sin,y = cos,z = 0, w = 0
    vCos = MATH_PERMUTE_PS(vCos, _MM_SHUFFLE(3, 2, 0, 1));
    // x = cos,y = -sin,z = 0, w = 0
    vCos = _mm_mul_ps(vCos, g_NegateX);
    M.r[1] = vCos;
    M.r[2] = g_IdentityC2;
    M.r[3] = g_IdentityC3;
    return M;
#endif
}

inline Matrix MathCallConv MatrixRotationRollPitchYaw
(
    float Pitch,
    float Yaw,
    float Roll
)noexcept{
    return MatrixRotationQuaternion(QuaternionRotationRollPitchYaw(Pitch, Yaw, Roll));
}

inline Matrix MathCallConv MatrixRotationRollPitchYawFromVector
(
    VectorArg Angles // <Pitch, Yaw, Roll, undefined>
)noexcept{
    return MatrixRotationQuaternion(QuaternionRotationRollPitchYawFromVector(Angles));
}

inline Matrix MathCallConv MatrixRotationNormal
(
    VectorArg NormalAxis,
    float     Angle
)noexcept{
#if defined(_MATH_NO_INTRINSICS_) || defined(_MATH_ARM_NEON_INTRINSICS_)

    float    fSinAngle;
    float    fCosAngle;
    ScalarSinCos(&fSinAngle, &fCosAngle, Angle);

    Vector A = VectorSet(fSinAngle, fCosAngle, 1.0f - fCosAngle, 0.0f);

    Vector C2 = VectorSplatZ(A);
    Vector C1 = VectorSplatY(A);
    Vector C0 = VectorSplatX(A);

    Vector N0 = VectorSwizzle<MATH_SWIZZLE_Y, MATH_SWIZZLE_Z, MATH_SWIZZLE_X, MATH_SWIZZLE_W>(NormalAxis);
    Vector N1 = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_X, MATH_SWIZZLE_Y, MATH_SWIZZLE_W>(NormalAxis);

    Vector V0 = VectorMultiply(C2, N0);
    V0 = VectorMultiply(V0, N1);

    Vector R0 = VectorMultiply(C2, NormalAxis);
    R0 = VectorMultiplyAdd(R0, NormalAxis, C1);

    Vector R1 = VectorMultiplyAdd(C0, NormalAxis, V0);
    Vector R2 = VectorNegativeMultiplySubtract(C0, NormalAxis, V0);

    V0 = VectorSelect(A, R0, g_Select1110.v);
    Vector V1 = VectorPermute<MATH_PERMUTE_0Z, MATH_PERMUTE_1Y, MATH_PERMUTE_1Z, MATH_PERMUTE_0X>(R1, R2);
    Vector V2 = VectorPermute<MATH_PERMUTE_0Y, MATH_PERMUTE_1X, MATH_PERMUTE_0Y, MATH_PERMUTE_1X>(R1, R2);

    Matrix M;
    M.r[0] = VectorPermute<MATH_PERMUTE_0X, MATH_PERMUTE_1X, MATH_PERMUTE_1Y, MATH_PERMUTE_0W>(V0, V1);
    M.r[1] = VectorPermute<MATH_PERMUTE_1Z, MATH_PERMUTE_0Y, MATH_PERMUTE_1W, MATH_PERMUTE_0W>(V0, V1);
    M.r[2] = VectorPermute<MATH_PERMUTE_1X, MATH_PERMUTE_1Y, MATH_PERMUTE_0Z, MATH_PERMUTE_0W>(V0, V2);
    M.r[3] = g_IdentityC3.v;
    return M;

#elif defined(_MATH_SSE_INTRINSICS_)
    float    fSinAngle;
    float    fCosAngle;
    ScalarSinCos(&fSinAngle, &fCosAngle, Angle);

    Vector C2 = _mm_set_ps1(1.0f - fCosAngle);
    Vector C1 = _mm_set_ps1(fCosAngle);
    Vector C0 = _mm_set_ps1(fSinAngle);

    Vector N0 = MATH_PERMUTE_PS(NormalAxis, _MM_SHUFFLE(3, 0, 2, 1));
    Vector N1 = MATH_PERMUTE_PS(NormalAxis, _MM_SHUFFLE(3, 1, 0, 2));

    Vector V0 = _mm_mul_ps(C2, N0);
    V0 = _mm_mul_ps(V0, N1);

    Vector R0 = _mm_mul_ps(C2, NormalAxis);
    R0 = _mm_mul_ps(R0, NormalAxis);
    R0 = _mm_add_ps(R0, C1);

    Vector R1 = _mm_mul_ps(C0, NormalAxis);
    R1 = _mm_add_ps(R1, V0);
    Vector R2 = _mm_mul_ps(C0, NormalAxis);
    R2 = _mm_sub_ps(V0, R2);

    V0 = _mm_and_ps(R0, g_Mask3);
    Vector V1 = _mm_shuffle_ps(R1, R2, _MM_SHUFFLE(2, 1, 2, 0));
    V1 = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(0, 3, 2, 1));
    Vector V2 = _mm_shuffle_ps(R1, R2, _MM_SHUFFLE(0, 0, 1, 1));
    V2 = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(2, 0, 2, 0));

    R2 = _mm_shuffle_ps(V0, V1, _MM_SHUFFLE(1, 0, 3, 0));
    R2 = MATH_PERMUTE_PS(R2, _MM_SHUFFLE(1, 3, 2, 0));

    Matrix M;
    M.r[0] = R2;

    R2 = _mm_shuffle_ps(V0, V1, _MM_SHUFFLE(3, 2, 3, 1));
    R2 = MATH_PERMUTE_PS(R2, _MM_SHUFFLE(1, 3, 0, 2));
    M.r[1] = R2;

    V2 = _mm_shuffle_ps(V2, V0, _MM_SHUFFLE(3, 2, 1, 0));
    M.r[2] = V2;
    M.r[3] = g_IdentityC3.v;
    return M;
#endif
}

inline Matrix MathCallConv MatrixRotationAxis
(
    VectorArg Axis,
    float     Angle
)noexcept{
    assert(!Vector3Equal(Axis, VectorZero()));
    assert(!Vector3IsInfinite(Axis));

    Vector Normal = Vector3Normalize(Axis);
    return MatrixRotationNormal(Normal, Angle);
}

inline Matrix MathCallConv MatrixRotationQuaternion(VectorArg Quaternion)noexcept{
#if defined(_MATH_NO_INTRINSICS_)

    float qx = Quaternion.vector4_f32[0];
    float qxx = qx * qx;

    float qy = Quaternion.vector4_f32[1];
    float qyy = qy * qy;

    float qz = Quaternion.vector4_f32[2];
    float qzz = qz * qz;

    float qw = Quaternion.vector4_f32[3];
    return MatrixSetColumns(
        VectorSet(1.f - 2.f * qyy - 2.f * qzz, 2.f * qx * qy + 2.f * qz * qw, 2.f * qx * qz - 2.f * qy * qw, 0.0f),
        VectorSet(2.f * qx * qy - 2.f * qz * qw, 1.f - 2.f * qxx - 2.f * qzz, 2.f * qy * qz + 2.f * qx * qw, 0.0f),
        VectorSet(2.f * qx * qz + 2.f * qy * qw, 2.f * qy * qz - 2.f * qx * qw, 1.f - 2.f * qxx - 2.f * qyy, 0.0f),
        g_IdentityC3.v
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    static const VectorF32 Constant1110 = { { { 1.0f, 1.0f, 1.0f, 0.0f } } };

    Vector Q0 = VectorAdd(Quaternion, Quaternion);
    Vector Q1 = VectorMultiply(Quaternion, Q0);

    Vector V0 = VectorPermute<MATH_PERMUTE_0Y, MATH_PERMUTE_0X, MATH_PERMUTE_0X, MATH_PERMUTE_1W>(Q1, Constant1110.v);
    Vector V1 = VectorPermute<MATH_PERMUTE_0Z, MATH_PERMUTE_0Z, MATH_PERMUTE_0Y, MATH_PERMUTE_1W>(Q1, Constant1110.v);
    Vector R0 = VectorSubtract(Constant1110, V0);
    R0 = VectorSubtract(R0, V1);

    V0 = VectorSwizzle<MATH_SWIZZLE_X, MATH_SWIZZLE_X, MATH_SWIZZLE_Y, MATH_SWIZZLE_W>(Quaternion);
    V1 = VectorSwizzle<MATH_SWIZZLE_Z, MATH_SWIZZLE_Y, MATH_SWIZZLE_Z, MATH_SWIZZLE_W>(Q0);
    V0 = VectorMultiply(V0, V1);

    V1 = VectorSplatW(Quaternion);
    Vector V2 = VectorSwizzle<MATH_SWIZZLE_Y, MATH_SWIZZLE_Z, MATH_SWIZZLE_X, MATH_SWIZZLE_W>(Q0);
    V1 = VectorMultiply(V1, V2);

    Vector R1 = VectorAdd(V0, V1);
    Vector R2 = VectorSubtract(V0, V1);

    V0 = VectorPermute<MATH_PERMUTE_0Y, MATH_PERMUTE_1X, MATH_PERMUTE_1Y, MATH_PERMUTE_0Z>(R1, R2);
    V1 = VectorPermute<MATH_PERMUTE_0X, MATH_PERMUTE_1Z, MATH_PERMUTE_0X, MATH_PERMUTE_1Z>(R1, R2);

    Matrix M;
    M.r[0] = VectorPermute<MATH_PERMUTE_0X, MATH_PERMUTE_1X, MATH_PERMUTE_1Y, MATH_PERMUTE_0W>(R0, V0);
    M.r[1] = VectorPermute<MATH_PERMUTE_1Z, MATH_PERMUTE_0Y, MATH_PERMUTE_1W, MATH_PERMUTE_0W>(R0, V0);
    M.r[2] = VectorPermute<MATH_PERMUTE_1X, MATH_PERMUTE_1Y, MATH_PERMUTE_0Z, MATH_PERMUTE_0W>(R0, V1);
    M.r[3] = g_IdentityC3.v;
    return M;

#elif defined(_MATH_SSE_INTRINSICS_)
    static const VectorF32  Constant1110 = { { { 1.0f, 1.0f, 1.0f, 0.0f } } };

    Vector Q0 = _mm_add_ps(Quaternion, Quaternion);
    Vector Q1 = _mm_mul_ps(Quaternion, Q0);

    Vector V0 = MATH_PERMUTE_PS(Q1, _MM_SHUFFLE(3, 0, 0, 1));
    V0 = _mm_and_ps(V0, g_Mask3);
    Vector V1 = MATH_PERMUTE_PS(Q1, _MM_SHUFFLE(3, 1, 2, 2));
    V1 = _mm_and_ps(V1, g_Mask3);
    Vector R0 = _mm_sub_ps(Constant1110, V0);
    R0 = _mm_sub_ps(R0, V1);

    V0 = MATH_PERMUTE_PS(Quaternion, _MM_SHUFFLE(3, 1, 0, 0));
    V1 = MATH_PERMUTE_PS(Q0, _MM_SHUFFLE(3, 2, 1, 2));
    V0 = _mm_mul_ps(V0, V1);

    V1 = MATH_PERMUTE_PS(Quaternion, _MM_SHUFFLE(3, 3, 3, 3));
    Vector V2 = MATH_PERMUTE_PS(Q0, _MM_SHUFFLE(3, 0, 2, 1));
    V1 = _mm_mul_ps(V1, V2);

    Vector R1 = _mm_add_ps(V0, V1);
    Vector R2 = _mm_sub_ps(V0, V1);

    V0 = _mm_shuffle_ps(R1, R2, _MM_SHUFFLE(1, 0, 2, 1));
    V0 = MATH_PERMUTE_PS(V0, _MM_SHUFFLE(1, 3, 2, 0));
    V1 = _mm_shuffle_ps(R1, R2, _MM_SHUFFLE(2, 2, 0, 0));
    V1 = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(2, 0, 2, 0));

    Q1 = _mm_shuffle_ps(R0, V0, _MM_SHUFFLE(1, 0, 3, 0));
    Q1 = MATH_PERMUTE_PS(Q1, _MM_SHUFFLE(1, 3, 2, 0));

    Matrix M;
    M.r[0] = Q1;

    Q1 = _mm_shuffle_ps(R0, V0, _MM_SHUFFLE(3, 2, 3, 1));
    Q1 = MATH_PERMUTE_PS(Q1, _MM_SHUFFLE(1, 3, 0, 2));
    M.r[1] = Q1;

    Q1 = _mm_shuffle_ps(V1, R0, _MM_SHUFFLE(3, 2, 1, 0));
    M.r[2] = Q1;
    M.r[3] = g_IdentityC3;
    return M;
#endif
}

inline Matrix MathCallConv MatrixTransformation2D
(
    VectorArg ScalingOrigin,
    float     ScalingOrientation,
    VectorArg Scaling,
    VectorArg RotationOrigin,
    float     Rotation,
    VectorArg2 Translation
)noexcept{
    // Column-equivalent composition:
    // T(Translation) * T(RotationOrigin) * R(Rotation) * T(-RotationOrigin) *
    // T(ScalingOrigin) * R(ScalingOrientation) * S(Scaling) *
    // R(ScalingOrientation)^-1 * T(-ScalingOrigin).

    Vector VScalingOrigin = VectorSelect(g_Select1100.v, ScalingOrigin, g_Select1100.v);
    Vector NegScalingOrigin = VectorNegate(VScalingOrigin);

    Matrix MScalingOriginI = MatrixTranslationFromVector(NegScalingOrigin);
    Matrix MScalingOrientation = MatrixRotationZ(ScalingOrientation);
    Matrix MScalingOrientationI = MatrixRotationZ(-ScalingOrientation);
    Vector VScaling = VectorSelect(g_One.v, Scaling, g_Select1100.v);
    Matrix MScaling = MatrixScalingFromVector(VScaling);
    Vector VRotationOrigin = VectorSelect(g_Select1100.v, RotationOrigin, g_Select1100.v);
    Matrix MRotation = MatrixRotationZ(Rotation);
    Vector VTranslation = VectorSelect(g_Select1100.v, Translation, g_Select1100.v);

    Matrix M = MatrixMultiply(MScalingOrientationI, MScalingOriginI);
    M = MatrixMultiply(MScaling, M);
    M = MatrixMultiply(MScalingOrientation, M);
    M.r[3] = VectorAdd(M.r[3], VScalingOrigin);
    M.r[3] = VectorSubtract(M.r[3], VRotationOrigin);
    M = MatrixMultiply(MRotation, M);
    M.r[3] = VectorAdd(M.r[3], VRotationOrigin);
    M.r[3] = VectorAdd(M.r[3], VTranslation);

    return M;
}

inline Matrix MathCallConv MatrixTransformation
(
    VectorArg ScalingOrigin,
    VectorArg ScalingOrientationQuaternion,
    VectorArg Scaling,
    VectorArg2 RotationOrigin,
    VectorArg3 RotationQuaternion,
    VectorArg3 Translation
)noexcept{
    // Column-equivalent composition:
    // T(Translation) * T(RotationOrigin) * R(RotationQuaternion) *
    // T(-RotationOrigin) * T(ScalingOrigin) * R(ScalingOrientationQuaternion) *
    // S(Scaling) * R(ScalingOrientationQuaternion)^-1 * T(-ScalingOrigin).

    Vector VScalingOrigin = VectorSelect(g_Select1110.v, ScalingOrigin, g_Select1110.v);
    Vector NegScalingOrigin = VectorNegate(VScalingOrigin);

    Matrix MScalingOriginI = MatrixTranslationFromVector(NegScalingOrigin);
    Matrix MScalingOrientation = MatrixRotationQuaternion(ScalingOrientationQuaternion);
    Matrix MScalingOrientationI = MatrixRotationQuaternion(QuaternionConjugate(ScalingOrientationQuaternion));
    const Vector VScaling = VectorSelect(g_One.v, Scaling, g_Select1110.v);
    Matrix MScaling = MatrixScalingFromVector(VScaling);
    Vector VRotationOrigin = VectorSelect(g_Select1110.v, RotationOrigin, g_Select1110.v);
    Matrix MRotation = MatrixRotationQuaternion(RotationQuaternion);
    Vector VTranslation = VectorSelect(g_Select1110.v, Translation, g_Select1110.v);

    Matrix M;
    M = MatrixMultiply(MScalingOrientationI, MScalingOriginI);
    M = MatrixMultiply(MScaling, M);
    M = MatrixMultiply(MScalingOrientation, M);
    M.r[3] = VectorAdd(M.r[3], VScalingOrigin);
    M.r[3] = VectorSubtract(M.r[3], VRotationOrigin);
    M = MatrixMultiply(MRotation, M);
    M.r[3] = VectorAdd(M.r[3], VRotationOrigin);
    M.r[3] = VectorAdd(M.r[3], VTranslation);
    return M;
}

inline Matrix MathCallConv MatrixAffineTransformation2D
(
    VectorArg Scaling,
    VectorArg RotationOrigin,
    float     Rotation,
    VectorArg Translation
)noexcept{
    // Column-equivalent composition:
    // T(Translation) * T(RotationOrigin) * R(Rotation) * T(-RotationOrigin) *
    // S(Scaling).

    Vector VScaling = VectorSelect(g_One.v, Scaling, g_Select1100.v);
    Matrix MScaling = MatrixScalingFromVector(VScaling);
    Vector VRotationOrigin = VectorSelect(g_Select1100.v, RotationOrigin, g_Select1100.v);
    Matrix MRotation = MatrixRotationZ(Rotation);
    Vector VTranslation = VectorSelect(g_Select1100.v, Translation, g_Select1100.v);

    Matrix M;
    M = MScaling;
    M.r[3] = VectorSubtract(M.r[3], VRotationOrigin);
    M = MatrixMultiply(MRotation, M);
    M.r[3] = VectorAdd(M.r[3], VRotationOrigin);
    M.r[3] = VectorAdd(M.r[3], VTranslation);
    return M;
}

inline Matrix MathCallConv MatrixAffineTransformation
(
    VectorArg Scaling,
    VectorArg RotationOrigin,
    VectorArg RotationQuaternion,
    VectorArg2 Translation
)noexcept{
    // Column-equivalent composition:
    // T(Translation) * T(RotationOrigin) * R(RotationQuaternion) *
    // T(-RotationOrigin) * S(Scaling).

    const Vector VScaling = VectorSelect(g_One.v, Scaling, g_Select1110.v);
    Matrix MScaling = MatrixScalingFromVector(VScaling);
    Vector VRotationOrigin = VectorSelect(g_Select1110.v, RotationOrigin, g_Select1110.v);
    Matrix MRotation = MatrixRotationQuaternion(RotationQuaternion);
    Vector VTranslation = VectorSelect(g_Select1110.v, Translation, g_Select1110.v);

    Matrix M;
    M = MScaling;
    M.r[3] = VectorSubtract(M.r[3], VRotationOrigin);
    M = MatrixMultiply(MRotation, M);
    M.r[3] = VectorAdd(M.r[3], VRotationOrigin);
    M.r[3] = VectorAdd(M.r[3], VTranslation);
    return M;
}

inline Matrix MathCallConv MatrixReflect(VectorArg ReflectionPlane)noexcept{
    assert(!Vector3Equal(ReflectionPlane, VectorZero()));
    assert(!PlaneIsInfinite(ReflectionPlane));

    static const VectorF32 NegativeTwo = { { { -2.0f, -2.0f, -2.0f, 0.0f } } };

    Vector P = PlaneNormalize(ReflectionPlane);
    Vector S = VectorMultiply(P, NegativeTwo);

    Vector A = VectorSplatX(P);
    Vector B = VectorSplatY(P);
    Vector C = VectorSplatZ(P);
    Vector D = VectorSplatW(P);

    Matrix M;
    M.r[0] = VectorMultiplyAdd(A, S, g_IdentityC0.v);
    M.r[1] = VectorMultiplyAdd(B, S, g_IdentityC1.v);
    M.r[2] = VectorMultiplyAdd(C, S, g_IdentityC2.v);
    M.r[3] = VectorMultiplyAdd(D, S, g_IdentityC3.v);
    return M;
}

inline Matrix MathCallConv MatrixShadow
(
    VectorArg ShadowPlane,
    VectorArg LightPosition
)noexcept{
    assert(!Vector3Equal(ShadowPlane, VectorZero()));
    assert(!PlaneIsInfinite(ShadowPlane));

    Vector P = PlaneNormalize(ShadowPlane);
    Vector Dot = PlaneDot(P, LightPosition);

    // Column-vector convention: shadow matrix is `dot(P, L) * I - L * P^T`,
    // so each output column is `dot(P, L) * e_j - L * P_j`.
    Vector A = VectorSplatX(P);
    Vector B = VectorSplatY(P);
    Vector C = VectorSplatZ(P);
    Vector D = VectorSplatW(P);

    Matrix M;
    M.r[0] = VectorNegativeMultiplySubtract(A, LightPosition, VectorMultiply(Dot, g_IdentityC0.v));
    M.r[1] = VectorNegativeMultiplySubtract(B, LightPosition, VectorMultiply(Dot, g_IdentityC1.v));
    M.r[2] = VectorNegativeMultiplySubtract(C, LightPosition, VectorMultiply(Dot, g_IdentityC2.v));
    M.r[3] = VectorNegativeMultiplySubtract(D, LightPosition, VectorMultiply(Dot, g_IdentityC3.v));
    return M;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Matrix MathCallConv MatrixLookAtLH
(
    VectorArg EyePosition,
    VectorArg FocusPosition,
    VectorArg UpDirection
)noexcept{
    Vector EyeDirection = VectorSubtract(FocusPosition, EyePosition);
    return MatrixLookToLH(EyePosition, EyeDirection, UpDirection);
}

inline Matrix MathCallConv MatrixLookAtRH
(
    VectorArg EyePosition,
    VectorArg FocusPosition,
    VectorArg UpDirection
)noexcept{
    Vector EyeDirection = VectorSubtract(FocusPosition, EyePosition);
    return MatrixLookToRH(EyePosition, EyeDirection, UpDirection);
}

inline Matrix MathCallConv MatrixLookToLH
(
    VectorArg EyePosition,
    VectorArg EyeDirection,
    VectorArg UpDirection
)noexcept{
    assert(!Vector3Equal(EyeDirection, VectorZero()));
    assert(!Vector3IsInfinite(EyeDirection));
    assert(!Vector3Equal(UpDirection, VectorZero()));
    assert(!Vector3IsInfinite(UpDirection));

    Vector R2 = Vector3Normalize(EyeDirection);

    Vector R0 = Vector3Cross(UpDirection, R2);
    R0 = Vector3Normalize(R0);

    Vector R1 = Vector3Cross(R2, R0);

    Vector NegEyePosition = VectorNegate(EyePosition);

    Vector D0 = Vector3Dot(R0, NegEyePosition);
    Vector D1 = Vector3Dot(R1, NegEyePosition);
    Vector D2 = Vector3Dot(R2, NegEyePosition);

    return MatrixSetColumns(
        VectorSet(VectorGetX(R0), VectorGetX(R1), VectorGetX(R2), 0.0f),
        VectorSet(VectorGetY(R0), VectorGetY(R1), VectorGetY(R2), 0.0f),
        VectorSet(VectorGetZ(R0), VectorGetZ(R1), VectorGetZ(R2), 0.0f),
        VectorSet(VectorGetX(D0), VectorGetX(D1), VectorGetX(D2), 1.0f)
    );
}

inline Matrix MathCallConv MatrixLookToRH
(
    VectorArg EyePosition,
    VectorArg EyeDirection,
    VectorArg UpDirection
)noexcept{
    Vector NegEyeDirection = VectorNegate(EyeDirection);
    return MatrixLookToLH(EyePosition, NegEyeDirection, UpDirection);
}

#ifdef _PREFAST_
#pragma prefast(push)
#pragma prefast(disable:28931, "PREfast noise: Esp:1266")
#endif

inline Matrix MathCallConv MatrixPerspectiveLH
(
    float ViewWidth,
    float ViewHeight,
    float NearZ,
    float FarZ
)noexcept{
    assert(NearZ > 0.f && FarZ > 0.f);
    assert(!ScalarNearEqual(ViewWidth, 0.0f, 0.00001f));
    assert(!ScalarNearEqual(ViewHeight, 0.0f, 0.00001f));
    assert(!ScalarNearEqual(FarZ, NearZ, 0.00001f));

#if defined(_MATH_NO_INTRINSICS_)

    float TwoNearZ = NearZ + NearZ;
    float fRange = FarZ / (FarZ - NearZ);
    return MatrixSetColumns(
        VectorSet(TwoNearZ / ViewWidth, 0.0f, 0.0f, 0.0f),
        VectorSet(0.0f, TwoNearZ / ViewHeight, 0.0f, 0.0f),
        VectorSet(0.0f, 0.0f, fRange, 1.0f),
        VectorSet(0.0f, 0.0f, -fRange * NearZ, 0.0f)
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float TwoNearZ = NearZ + NearZ;
    float fRange = FarZ / (FarZ - NearZ);
    const float32x4_t Zero = vdupq_n_f32(0);
    Matrix M;
    M.r[0] = vsetq_lane_f32(TwoNearZ / ViewWidth, Zero, 0);
    M.r[1] = vsetq_lane_f32(TwoNearZ / ViewHeight, Zero, 1);
    M.r[2] = vsetq_lane_f32(fRange, g_IdentityC3.v, 2);
    M.r[3] = vsetq_lane_f32(-fRange * NearZ, Zero, 2);
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    Matrix M;
    float TwoNearZ = NearZ + NearZ;
    float fRange = FarZ / (FarZ - NearZ);
    // Note: This is recorded on the stack
    Vector rMem = {
        TwoNearZ / ViewWidth,
        TwoNearZ / ViewHeight,
        fRange,
        -fRange * NearZ
    };
    // Copy from memory to SSE register
    Vector vValues = rMem;
    Vector vTemp = _mm_setzero_ps();
    // Copy x only
    vTemp = _mm_move_ss(vTemp, vValues);
    // TwoNearZ / ViewWidth,0,0,0
    M.r[0] = vTemp;
    // 0,TwoNearZ / ViewHeight,0,0
    vTemp = vValues;
    vTemp = _mm_and_ps(vTemp, g_MaskY);
    M.r[1] = vTemp;
    // x=fRange,y=-fRange * NearZ,0,1.0f
    vValues = _mm_shuffle_ps(vValues, g_IdentityC3, _MM_SHUFFLE(3, 2, 3, 2));
    // 0,0,fRange,1.0f
    vTemp = _mm_setzero_ps();
    vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(3, 0, 0, 0));
    M.r[2] = vTemp;
    // 0,0,-fRange * NearZ,0
    vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(2, 1, 0, 0));
    M.r[3] = vTemp;
    return M;
#endif
}

inline Matrix MathCallConv MatrixPerspectiveRH
(
    float ViewWidth,
    float ViewHeight,
    float NearZ,
    float FarZ
)noexcept{
    assert(NearZ > 0.f && FarZ > 0.f);
    assert(!ScalarNearEqual(ViewWidth, 0.0f, 0.00001f));
    assert(!ScalarNearEqual(ViewHeight, 0.0f, 0.00001f));
    assert(!ScalarNearEqual(FarZ, NearZ, 0.00001f));

#if defined(_MATH_NO_INTRINSICS_)

    float TwoNearZ = NearZ + NearZ;
    float fRange = FarZ / (NearZ - FarZ);
    return MatrixSetColumns(
        VectorSet(TwoNearZ / ViewWidth, 0.0f, 0.0f, 0.0f),
        VectorSet(0.0f, TwoNearZ / ViewHeight, 0.0f, 0.0f),
        VectorSet(0.0f, 0.0f, fRange, -1.0f),
        VectorSet(0.0f, 0.0f, fRange * NearZ, 0.0f)
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float TwoNearZ = NearZ + NearZ;
    float fRange = FarZ / (NearZ - FarZ);
    const float32x4_t Zero = vdupq_n_f32(0);

    Matrix M;
    M.r[0] = vsetq_lane_f32(TwoNearZ / ViewWidth, Zero, 0);
    M.r[1] = vsetq_lane_f32(TwoNearZ / ViewHeight, Zero, 1);
    M.r[2] = vsetq_lane_f32(fRange, g_NegIdentityC3.v, 2);
    M.r[3] = vsetq_lane_f32(fRange * NearZ, Zero, 2);
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    Matrix M;
    float TwoNearZ = NearZ + NearZ;
    float fRange = FarZ / (NearZ - FarZ);
    // Note: This is recorded on the stack
    Vector rMem = {
        TwoNearZ / ViewWidth,
        TwoNearZ / ViewHeight,
        fRange,
        fRange * NearZ
    };
    // Copy from memory to SSE register
    Vector vValues = rMem;
    Vector vTemp = _mm_setzero_ps();
    // Copy x only
    vTemp = _mm_move_ss(vTemp, vValues);
    // TwoNearZ / ViewWidth,0,0,0
    M.r[0] = vTemp;
    // 0,TwoNearZ / ViewHeight,0,0
    vTemp = vValues;
    vTemp = _mm_and_ps(vTemp, g_MaskY);
    M.r[1] = vTemp;
    // x=fRange,y=-fRange * NearZ,0,-1.0f
    vValues = _mm_shuffle_ps(vValues, g_NegIdentityC3, _MM_SHUFFLE(3, 2, 3, 2));
    // 0,0,fRange,-1.0f
    vTemp = _mm_setzero_ps();
    vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(3, 0, 0, 0));
    M.r[2] = vTemp;
    // 0,0,-fRange * NearZ,0
    vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(2, 1, 0, 0));
    M.r[3] = vTemp;
    return M;
#endif
}

inline Matrix MathCallConv MatrixPerspectiveFovLH
(
    float FovAngleY,
    float AspectRatio,
    float NearZ,
    float FarZ
)noexcept{
    assert(NearZ > 0.f && FarZ > 0.f);
    assert(!ScalarNearEqual(FovAngleY, 0.0f, 0.00001f * 2.0f));
    assert(!ScalarNearEqual(AspectRatio, 0.0f, 0.00001f));
    assert(!ScalarNearEqual(FarZ, NearZ, 0.00001f));

#if defined(_MATH_NO_INTRINSICS_)

    float    SinFov;
    float    CosFov;
    ScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);

    float Height = CosFov / SinFov;
    float Width = Height / AspectRatio;
    float fRange = FarZ / (FarZ - NearZ);
    return MatrixSetColumns(
        VectorSet(Width, 0.0f, 0.0f, 0.0f),
        VectorSet(0.0f, Height, 0.0f, 0.0f),
        VectorSet(0.0f, 0.0f, fRange, 1.0f),
        VectorSet(0.0f, 0.0f, -fRange * NearZ, 0.0f)
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float    SinFov;
    float    CosFov;
    ScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);

    float fRange = FarZ / (FarZ - NearZ);
    float Height = CosFov / SinFov;
    float Width = Height / AspectRatio;
    const float32x4_t Zero = vdupq_n_f32(0);

    Matrix M;
    M.r[0] = vsetq_lane_f32(Width, Zero, 0);
    M.r[1] = vsetq_lane_f32(Height, Zero, 1);
    M.r[2] = vsetq_lane_f32(fRange, g_IdentityC3.v, 2);
    M.r[3] = vsetq_lane_f32(-fRange * NearZ, Zero, 2);
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    float    SinFov;
    float    CosFov;
    ScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);

    float fRange = FarZ / (FarZ - NearZ);
    // Note: This is recorded on the stack
    float Height = CosFov / SinFov;
    Vector rMem = {
        Height / AspectRatio,
        Height,
        fRange,
        -fRange * NearZ
    };
    // Copy from memory to SSE register
    Vector vValues = rMem;
    Vector vTemp = _mm_setzero_ps();
    // Copy x only
    vTemp = _mm_move_ss(vTemp, vValues);
    // Height / AspectRatio,0,0,0
    Matrix M;
    M.r[0] = vTemp;
    // 0,Height,0,0
    vTemp = vValues;
    vTemp = _mm_and_ps(vTemp, g_MaskY);
    M.r[1] = vTemp;
    // x=fRange,y=-fRange * NearZ,0,1.0f
    vTemp = _mm_setzero_ps();
    vValues = _mm_shuffle_ps(vValues, g_IdentityC3, _MM_SHUFFLE(3, 2, 3, 2));
    // 0,0,fRange,1.0f
    vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(3, 0, 0, 0));
    M.r[2] = vTemp;
    // 0,0,-fRange * NearZ,0.0f
    vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(2, 1, 0, 0));
    M.r[3] = vTemp;
    return M;
#endif
}

inline Matrix MathCallConv MatrixPerspectiveFovRH
(
    float FovAngleY,
    float AspectRatio,
    float NearZ,
    float FarZ
)noexcept{
    assert(NearZ > 0.f && FarZ > 0.f);
    assert(!ScalarNearEqual(FovAngleY, 0.0f, 0.00001f * 2.0f));
    assert(!ScalarNearEqual(AspectRatio, 0.0f, 0.00001f));
    assert(!ScalarNearEqual(FarZ, NearZ, 0.00001f));

#if defined(_MATH_NO_INTRINSICS_)

    float    SinFov;
    float    CosFov;
    ScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);

    float Height = CosFov / SinFov;
    float Width = Height / AspectRatio;
    float fRange = FarZ / (NearZ - FarZ);
    return MatrixSetColumns(
        VectorSet(Width, 0.0f, 0.0f, 0.0f),
        VectorSet(0.0f, Height, 0.0f, 0.0f),
        VectorSet(0.0f, 0.0f, fRange, -1.0f),
        VectorSet(0.0f, 0.0f, fRange * NearZ, 0.0f)
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float    SinFov;
    float    CosFov;
    ScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);
    float fRange = FarZ / (NearZ - FarZ);
    float Height = CosFov / SinFov;
    float Width = Height / AspectRatio;
    const float32x4_t Zero = vdupq_n_f32(0);

    Matrix M;
    M.r[0] = vsetq_lane_f32(Width, Zero, 0);
    M.r[1] = vsetq_lane_f32(Height, Zero, 1);
    M.r[2] = vsetq_lane_f32(fRange, g_NegIdentityC3.v, 2);
    M.r[3] = vsetq_lane_f32(fRange * NearZ, Zero, 2);
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    float    SinFov;
    float    CosFov;
    ScalarSinCos(&SinFov, &CosFov, 0.5f * FovAngleY);
    float fRange = FarZ / (NearZ - FarZ);
    // Note: This is recorded on the stack
    float Height = CosFov / SinFov;
    Vector rMem = {
        Height / AspectRatio,
        Height,
        fRange,
        fRange * NearZ
    };
    // Copy from memory to SSE register
    Vector vValues = rMem;
    Vector vTemp = _mm_setzero_ps();
    // Copy x only
    vTemp = _mm_move_ss(vTemp, vValues);
    // Height / AspectRatio,0,0,0
    Matrix M;
    M.r[0] = vTemp;
    // 0,Height,0,0
    vTemp = vValues;
    vTemp = _mm_and_ps(vTemp, g_MaskY);
    M.r[1] = vTemp;
    // x=fRange,y=-fRange * NearZ,0,-1.0f
    vTemp = _mm_setzero_ps();
    vValues = _mm_shuffle_ps(vValues, g_NegIdentityC3, _MM_SHUFFLE(3, 2, 3, 2));
    // 0,0,fRange,-1.0f
    vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(3, 0, 0, 0));
    M.r[2] = vTemp;
    // 0,0,fRange * NearZ,0.0f
    vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(2, 1, 0, 0));
    M.r[3] = vTemp;
    return M;
#endif
}

inline Matrix MathCallConv MatrixPerspectiveOffCenterLH
(
    float ViewLeft,
    float ViewRight,
    float ViewBottom,
    float ViewTop,
    float NearZ,
    float FarZ
)noexcept{
    assert(NearZ > 0.f && FarZ > 0.f);
    assert(!ScalarNearEqual(ViewRight, ViewLeft, 0.00001f));
    assert(!ScalarNearEqual(ViewTop, ViewBottom, 0.00001f));
    assert(!ScalarNearEqual(FarZ, NearZ, 0.00001f));

#if defined(_MATH_NO_INTRINSICS_)

    float TwoNearZ = NearZ + NearZ;
    float ReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
    float ReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
    float fRange = FarZ / (FarZ - NearZ);
    return MatrixSetColumns(
        VectorSet(TwoNearZ * ReciprocalWidth, 0.0f, 0.0f, 0.0f),
        VectorSet(0.0f, TwoNearZ * ReciprocalHeight, 0.0f, 0.0f),
        VectorSet(-(ViewLeft + ViewRight) * ReciprocalWidth, -(ViewTop + ViewBottom) * ReciprocalHeight, fRange, 1.0f),
        VectorSet(0.0f, 0.0f, -fRange * NearZ, 0.0f)
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float TwoNearZ = NearZ + NearZ;
    float ReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
    float ReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
    float fRange = FarZ / (FarZ - NearZ);
    const float32x4_t Zero = vdupq_n_f32(0);

    Matrix M;
    M.r[0] = vsetq_lane_f32(TwoNearZ * ReciprocalWidth, Zero, 0);
    M.r[1] = vsetq_lane_f32(TwoNearZ * ReciprocalHeight, Zero, 1);
    M.r[2] = VectorSet(-(ViewLeft + ViewRight) * ReciprocalWidth,
        -(ViewTop + ViewBottom) * ReciprocalHeight,
        fRange,
        1.0f);
    M.r[3] = vsetq_lane_f32(-fRange * NearZ, Zero, 2);
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    Matrix M;
    float TwoNearZ = NearZ + NearZ;
    float ReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
    float ReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
    float fRange = FarZ / (FarZ - NearZ);
    // Note: This is recorded on the stack
    Vector rMem = {
        TwoNearZ * ReciprocalWidth,
        TwoNearZ * ReciprocalHeight,
        -fRange * NearZ,
        0
    };
    // Copy from memory to SSE register
    Vector vValues = rMem;
    Vector vTemp = _mm_setzero_ps();
    // Copy x only
    vTemp = _mm_move_ss(vTemp, vValues);
    // TwoNearZ*ReciprocalWidth,0,0,0
    M.r[0] = vTemp;
    // 0,TwoNearZ*ReciprocalHeight,0,0
    vTemp = vValues;
    vTemp = _mm_and_ps(vTemp, g_MaskY);
    M.r[1] = vTemp;
    // 0,0,fRange,1.0f
    M.r[2] = VectorSet(-(ViewLeft + ViewRight) * ReciprocalWidth,
        -(ViewTop + ViewBottom) * ReciprocalHeight,
        fRange,
        1.0f);
    // 0,0,-fRange * NearZ,0.0f
    vValues = _mm_and_ps(vValues, g_MaskZ);
    M.r[3] = vValues;
    return M;
#endif
}

inline Matrix MathCallConv MatrixPerspectiveOffCenterRH
(
    float ViewLeft,
    float ViewRight,
    float ViewBottom,
    float ViewTop,
    float NearZ,
    float FarZ
)noexcept{
    assert(NearZ > 0.f && FarZ > 0.f);
    assert(!ScalarNearEqual(ViewRight, ViewLeft, 0.00001f));
    assert(!ScalarNearEqual(ViewTop, ViewBottom, 0.00001f));
    assert(!ScalarNearEqual(FarZ, NearZ, 0.00001f));

#if defined(_MATH_NO_INTRINSICS_)

    float TwoNearZ = NearZ + NearZ;
    float ReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
    float ReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
    float fRange = FarZ / (NearZ - FarZ);
    return MatrixSetColumns(
        VectorSet(TwoNearZ * ReciprocalWidth, 0.0f, 0.0f, 0.0f),
        VectorSet(0.0f, TwoNearZ * ReciprocalHeight, 0.0f, 0.0f),
        VectorSet((ViewLeft + ViewRight) * ReciprocalWidth, (ViewTop + ViewBottom) * ReciprocalHeight, fRange, -1.0f),
        VectorSet(0.0f, 0.0f, fRange * NearZ, 0.0f)
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float TwoNearZ = NearZ + NearZ;
    float ReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
    float ReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
    float fRange = FarZ / (NearZ - FarZ);
    const float32x4_t Zero = vdupq_n_f32(0);

    Matrix M;
    M.r[0] = vsetq_lane_f32(TwoNearZ * ReciprocalWidth, Zero, 0);
    M.r[1] = vsetq_lane_f32(TwoNearZ * ReciprocalHeight, Zero, 1);
    M.r[2] = VectorSet((ViewLeft + ViewRight) * ReciprocalWidth,
        (ViewTop + ViewBottom) * ReciprocalHeight,
        fRange,
        -1.0f);
    M.r[3] = vsetq_lane_f32(fRange * NearZ, Zero, 2);
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    Matrix M;
    float TwoNearZ = NearZ + NearZ;
    float ReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
    float ReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
    float fRange = FarZ / (NearZ - FarZ);
    // Note: This is recorded on the stack
    Vector rMem = {
        TwoNearZ * ReciprocalWidth,
        TwoNearZ * ReciprocalHeight,
        fRange * NearZ,
        0
    };
    // Copy from memory to SSE register
    Vector vValues = rMem;
    Vector vTemp = _mm_setzero_ps();
    // Copy x only
    vTemp = _mm_move_ss(vTemp, vValues);
    // TwoNearZ*ReciprocalWidth,0,0,0
    M.r[0] = vTemp;
    // 0,TwoNearZ*ReciprocalHeight,0,0
    vTemp = vValues;
    vTemp = _mm_and_ps(vTemp, g_MaskY);
    M.r[1] = vTemp;
    // 0,0,fRange,1.0f
    M.r[2] = VectorSet((ViewLeft + ViewRight) * ReciprocalWidth,
        (ViewTop + ViewBottom) * ReciprocalHeight,
        fRange,
        -1.0f);
    // 0,0,-fRange * NearZ,0.0f
    vValues = _mm_and_ps(vValues, g_MaskZ);
    M.r[3] = vValues;
    return M;
#endif
}

inline Matrix MathCallConv MatrixOrthographicLH
(
    float ViewWidth,
    float ViewHeight,
    float NearZ,
    float FarZ
)noexcept{
    assert(!ScalarNearEqual(ViewWidth, 0.0f, 0.00001f));
    assert(!ScalarNearEqual(ViewHeight, 0.0f, 0.00001f));
    assert(!ScalarNearEqual(FarZ, NearZ, 0.00001f));

#if defined(_MATH_NO_INTRINSICS_)

    float fRange = 1.0f / (FarZ - NearZ);
    return MatrixSetColumns(
        VectorSet(2.0f / ViewWidth, 0.0f, 0.0f, 0.0f),
        VectorSet(0.0f, 2.0f / ViewHeight, 0.0f, 0.0f),
        VectorSet(0.0f, 0.0f, fRange, 0.0f),
        VectorSet(0.0f, 0.0f, -fRange * NearZ, 1.0f)
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float fRange = 1.0f / (FarZ - NearZ);

    const float32x4_t Zero = vdupq_n_f32(0);
    Matrix M;
    M.r[0] = vsetq_lane_f32(2.0f / ViewWidth, Zero, 0);
    M.r[1] = vsetq_lane_f32(2.0f / ViewHeight, Zero, 1);
    M.r[2] = vsetq_lane_f32(fRange, Zero, 2);
    M.r[3] = vsetq_lane_f32(-fRange * NearZ, g_IdentityC3.v, 2);
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    Matrix M;
    float fRange = 1.0f / (FarZ - NearZ);
    // Note: This is recorded on the stack
    Vector rMem = {
        2.0f / ViewWidth,
        2.0f / ViewHeight,
        fRange,
        -fRange * NearZ
    };
    // Copy from memory to SSE register
    Vector vValues = rMem;
    Vector vTemp = _mm_setzero_ps();
    // Copy x only
    vTemp = _mm_move_ss(vTemp, vValues);
    // 2.0f / ViewWidth,0,0,0
    M.r[0] = vTemp;
    // 0,2.0f / ViewHeight,0,0
    vTemp = vValues;
    vTemp = _mm_and_ps(vTemp, g_MaskY);
    M.r[1] = vTemp;
    // x=fRange,y=-fRange * NearZ,0,1.0f
    vTemp = _mm_setzero_ps();
    vValues = _mm_shuffle_ps(vValues, g_IdentityC3, _MM_SHUFFLE(3, 2, 3, 2));
    // 0,0,fRange,0.0f
    vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(2, 0, 0, 0));
    M.r[2] = vTemp;
    // 0,0,-fRange * NearZ,1.0f
    vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(3, 1, 0, 0));
    M.r[3] = vTemp;
    return M;
#endif
}

inline Matrix MathCallConv MatrixOrthographicRH
(
    float ViewWidth,
    float ViewHeight,
    float NearZ,
    float FarZ
)noexcept{
    assert(!ScalarNearEqual(ViewWidth, 0.0f, 0.00001f));
    assert(!ScalarNearEqual(ViewHeight, 0.0f, 0.00001f));
    assert(!ScalarNearEqual(FarZ, NearZ, 0.00001f));

#if defined(_MATH_NO_INTRINSICS_)

    float fRange = 1.0f / (NearZ - FarZ);
    return MatrixSetColumns(
        VectorSet(2.0f / ViewWidth, 0.0f, 0.0f, 0.0f),
        VectorSet(0.0f, 2.0f / ViewHeight, 0.0f, 0.0f),
        VectorSet(0.0f, 0.0f, fRange, 0.0f),
        VectorSet(0.0f, 0.0f, fRange * NearZ, 1.0f)
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float fRange = 1.0f / (NearZ - FarZ);

    const float32x4_t Zero = vdupq_n_f32(0);
    Matrix M;
    M.r[0] = vsetq_lane_f32(2.0f / ViewWidth, Zero, 0);
    M.r[1] = vsetq_lane_f32(2.0f / ViewHeight, Zero, 1);
    M.r[2] = vsetq_lane_f32(fRange, Zero, 2);
    M.r[3] = vsetq_lane_f32(fRange * NearZ, g_IdentityC3.v, 2);
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    Matrix M;
    float fRange = 1.0f / (NearZ - FarZ);
    // Note: This is recorded on the stack
    Vector rMem = {
        2.0f / ViewWidth,
        2.0f / ViewHeight,
        fRange,
        fRange * NearZ
    };
    // Copy from memory to SSE register
    Vector vValues = rMem;
    Vector vTemp = _mm_setzero_ps();
    // Copy x only
    vTemp = _mm_move_ss(vTemp, vValues);
    // 2.0f / ViewWidth,0,0,0
    M.r[0] = vTemp;
    // 0,2.0f / ViewHeight,0,0
    vTemp = vValues;
    vTemp = _mm_and_ps(vTemp, g_MaskY);
    M.r[1] = vTemp;
    // x=fRange,y=fRange * NearZ,0,1.0f
    vTemp = _mm_setzero_ps();
    vValues = _mm_shuffle_ps(vValues, g_IdentityC3, _MM_SHUFFLE(3, 2, 3, 2));
    // 0,0,fRange,0.0f
    vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(2, 0, 0, 0));
    M.r[2] = vTemp;
    // 0,0,fRange * NearZ,1.0f
    vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(3, 1, 0, 0));
    M.r[3] = vTemp;
    return M;
#endif
}

inline Matrix MathCallConv MatrixOrthographicOffCenterLH
(
    float ViewLeft,
    float ViewRight,
    float ViewBottom,
    float ViewTop,
    float NearZ,
    float FarZ
)noexcept{
    assert(!ScalarNearEqual(ViewRight, ViewLeft, 0.00001f));
    assert(!ScalarNearEqual(ViewTop, ViewBottom, 0.00001f));
    assert(!ScalarNearEqual(FarZ, NearZ, 0.00001f));

#if defined(_MATH_NO_INTRINSICS_)

    float ReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
    float ReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
    float fRange = 1.0f / (FarZ - NearZ);
    return MatrixSetColumns(
        VectorSet(ReciprocalWidth + ReciprocalWidth, 0.0f, 0.0f, 0.0f),
        VectorSet(0.0f, ReciprocalHeight + ReciprocalHeight, 0.0f, 0.0f),
        VectorSet(0.0f, 0.0f, fRange, 0.0f),
        VectorSet(-(ViewLeft + ViewRight) * ReciprocalWidth, -(ViewTop + ViewBottom) * ReciprocalHeight, -fRange * NearZ, 1.0f)
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float ReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
    float ReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
    float fRange = 1.0f / (FarZ - NearZ);
    const float32x4_t Zero = vdupq_n_f32(0);
    Matrix M;
    M.r[0] = vsetq_lane_f32(ReciprocalWidth + ReciprocalWidth, Zero, 0);
    M.r[1] = vsetq_lane_f32(ReciprocalHeight + ReciprocalHeight, Zero, 1);
    M.r[2] = vsetq_lane_f32(fRange, Zero, 2);
    M.r[3] = VectorSet(-(ViewLeft + ViewRight) * ReciprocalWidth,
        -(ViewTop + ViewBottom) * ReciprocalHeight,
        -fRange * NearZ,
        1.0f);
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    Matrix M;
    float fReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
    float fReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
    float fRange = 1.0f / (FarZ - NearZ);
    // Note: This is recorded on the stack
    Vector rMem = {
        fReciprocalWidth,
        fReciprocalHeight,
        fRange,
        1.0f
    };
    Vector rMem2 = {
        -(ViewLeft + ViewRight),
        -(ViewTop + ViewBottom),
        -NearZ,
        1.0f
    };
    // Copy from memory to SSE register
    Vector vValues = rMem;
    Vector vTemp = _mm_setzero_ps();
    // Copy x only
    vTemp = _mm_move_ss(vTemp, vValues);
    // fReciprocalWidth*2,0,0,0
    vTemp = _mm_add_ss(vTemp, vTemp);
    M.r[0] = vTemp;
    // 0,fReciprocalHeight*2,0,0
    vTemp = vValues;
    vTemp = _mm_and_ps(vTemp, g_MaskY);
    vTemp = _mm_add_ps(vTemp, vTemp);
    M.r[1] = vTemp;
    // 0,0,fRange,0.0f
    vTemp = vValues;
    vTemp = _mm_and_ps(vTemp, g_MaskZ);
    M.r[2] = vTemp;
    // -(ViewLeft + ViewRight)*fReciprocalWidth,-(ViewTop + ViewBottom)*fReciprocalHeight,fRange*-NearZ,1.0f
    vValues = _mm_mul_ps(vValues, rMem2);
    M.r[3] = vValues;
    return M;
#endif
}

inline Matrix MathCallConv MatrixOrthographicOffCenterRH
(
    float ViewLeft,
    float ViewRight,
    float ViewBottom,
    float ViewTop,
    float NearZ,
    float FarZ
)noexcept{
    assert(!ScalarNearEqual(ViewRight, ViewLeft, 0.00001f));
    assert(!ScalarNearEqual(ViewTop, ViewBottom, 0.00001f));
    assert(!ScalarNearEqual(FarZ, NearZ, 0.00001f));

#if defined(_MATH_NO_INTRINSICS_)

    float ReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
    float ReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
    float fRange = 1.0f / (NearZ - FarZ);
    return MatrixSetColumns(
        VectorSet(ReciprocalWidth + ReciprocalWidth, 0.0f, 0.0f, 0.0f),
        VectorSet(0.0f, ReciprocalHeight + ReciprocalHeight, 0.0f, 0.0f),
        VectorSet(0.0f, 0.0f, fRange, 0.0f),
        VectorSet(-(ViewLeft + ViewRight) * ReciprocalWidth, -(ViewTop + ViewBottom) * ReciprocalHeight, fRange * NearZ, 1.0f)
    );

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    float ReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
    float ReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
    float fRange = 1.0f / (NearZ - FarZ);
    const float32x4_t Zero = vdupq_n_f32(0);
    Matrix M;
    M.r[0] = vsetq_lane_f32(ReciprocalWidth + ReciprocalWidth, Zero, 0);
    M.r[1] = vsetq_lane_f32(ReciprocalHeight + ReciprocalHeight, Zero, 1);
    M.r[2] = vsetq_lane_f32(fRange, Zero, 2);
    M.r[3] = VectorSet(-(ViewLeft + ViewRight) * ReciprocalWidth,
        -(ViewTop + ViewBottom) * ReciprocalHeight,
        fRange * NearZ,
        1.0f);
    return M;
#elif defined(_MATH_SSE_INTRINSICS_)
    Matrix M;
    float fReciprocalWidth = 1.0f / (ViewRight - ViewLeft);
    float fReciprocalHeight = 1.0f / (ViewTop - ViewBottom);
    float fRange = 1.0f / (NearZ - FarZ);
    // Note: This is recorded on the stack
    Vector rMem = {
        fReciprocalWidth,
        fReciprocalHeight,
        fRange,
        1.0f
    };
    Vector rMem2 = {
        -(ViewLeft + ViewRight),
        -(ViewTop + ViewBottom),
        NearZ,
        1.0f
    };
    // Copy from memory to SSE register
    Vector vValues = rMem;
    Vector vTemp = _mm_setzero_ps();
    // Copy x only
    vTemp = _mm_move_ss(vTemp, vValues);
    // fReciprocalWidth*2,0,0,0
    vTemp = _mm_add_ss(vTemp, vTemp);
    M.r[0] = vTemp;
    // 0,fReciprocalHeight*2,0,0
    vTemp = vValues;
    vTemp = _mm_and_ps(vTemp, g_MaskY);
    vTemp = _mm_add_ps(vTemp, vTemp);
    M.r[1] = vTemp;
    // 0,0,fRange,0.0f
    vTemp = vValues;
    vTemp = _mm_and_ps(vTemp, g_MaskZ);
    M.r[2] = vTemp;
    // -(ViewLeft + ViewRight)*fReciprocalWidth,-(ViewTop + ViewBottom)*fReciprocalHeight,fRange*-NearZ,1.0f
    vValues = _mm_mul_ps(vValues, rMem2);
    M.r[3] = vValues;
    return M;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef _PREFAST_
#pragma prefast(pop)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Matrix::Matrix
(
    float m00, float m01, float m02, float m03,
    float m10, float m11, float m12, float m13,
    float m20, float m21, float m22, float m23,
    float m30, float m31, float m32, float m33
)noexcept{
    // Scalar constructor accepts explicit matrix elements `mRC` and stores
    // them into NWB's internal column layout.
    *this = MatrixSet(
        m00, m01, m02, m03,
        m10, m11, m12, m13,
        m20, m21, m22, m23,
        m30, m31, m32, m33
    );
}

_Use_decl_annotations_
inline Matrix::Matrix(const float* pArray)noexcept{
    assert(pArray != nullptr);
    // Raw float-array construction consumes four contiguous NWB columns:
    // `[c0 | c1 | c2 | c3]`.
    r[0] = LoadFloat4(reinterpret_cast<const Float4*>(pArray + 0));
    r[1] = LoadFloat4(reinterpret_cast<const Float4*>(pArray + 4));
    r[2] = LoadFloat4(reinterpret_cast<const Float4*>(pArray + 8));
    r[3] = LoadFloat4(reinterpret_cast<const Float4*>(pArray + 12));
}

inline Matrix Matrix::operator-()const noexcept{
    Matrix R;
    R.r[0] = VectorNegate(r[0]);
    R.r[1] = VectorNegate(r[1]);
    R.r[2] = VectorNegate(r[2]);
    R.r[3] = VectorNegate(r[3]);
    return R;
}

inline Matrix& MathCallConv Matrix::operator+=(FXMMATRIX M)noexcept{
    r[0] = VectorAdd(r[0], M.r[0]);
    r[1] = VectorAdd(r[1], M.r[1]);
    r[2] = VectorAdd(r[2], M.r[2]);
    r[3] = VectorAdd(r[3], M.r[3]);
    return *this;
}

inline Matrix& MathCallConv Matrix::operator-=(FXMMATRIX M)noexcept{
    r[0] = VectorSubtract(r[0], M.r[0]);
    r[1] = VectorSubtract(r[1], M.r[1]);
    r[2] = VectorSubtract(r[2], M.r[2]);
    r[3] = VectorSubtract(r[3], M.r[3]);
    return *this;
}

inline Matrix& MathCallConv Matrix::operator*=(FXMMATRIX M)noexcept{
    *this = MatrixMultiply(*this, M);
    return *this;
}

inline Matrix& Matrix::operator*=(float S)noexcept{
    r[0] = VectorScale(r[0], S);
    r[1] = VectorScale(r[1], S);
    r[2] = VectorScale(r[2], S);
    r[3] = VectorScale(r[3], S);
    return *this;
}

inline Matrix& Matrix::operator/=(float S)noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    Vector vS = VectorReplicate(S);
    r[0] = VectorDivide(r[0], vS);
    r[1] = VectorDivide(r[1], vS);
    r[2] = VectorDivide(r[2], vS);
    r[3] = VectorDivide(r[3], vS);
    return *this;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
    float32x4_t vS = vdupq_n_f32(S);
    r[0] = vdivq_f32(r[0], vS);
    r[1] = vdivq_f32(r[1], vS);
    r[2] = vdivq_f32(r[2], vS);
    r[3] = vdivq_f32(r[3], vS);
#else
    // 2 iterations of Newton-Raphson refinement of reciprocal
    float32x2_t vS = vdup_n_f32(S);
    float32x2_t R0 = vrecpe_f32(vS);
    float32x2_t S0 = vrecps_f32(R0, vS);
    R0 = vmul_f32(S0, R0);
    S0 = vrecps_f32(R0, vS);
    R0 = vmul_f32(S0, R0);
    float32x4_t Reciprocal = vcombine_f32(R0, R0);
    r[0] = vmulq_f32(r[0], Reciprocal);
    r[1] = vmulq_f32(r[1], Reciprocal);
    r[2] = vmulq_f32(r[2], Reciprocal);
    r[3] = vmulq_f32(r[3], Reciprocal);
#endif
    return *this;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128 vS = _mm_set_ps1(S);
    r[0] = _mm_div_ps(r[0], vS);
    r[1] = _mm_div_ps(r[1], vS);
    r[2] = _mm_div_ps(r[2], vS);
    r[3] = _mm_div_ps(r[3], vS);
    return *this;
#endif
}

inline Matrix MathCallConv Matrix::operator+(FXMMATRIX M)const noexcept{
    Matrix R;
    R.r[0] = VectorAdd(r[0], M.r[0]);
    R.r[1] = VectorAdd(r[1], M.r[1]);
    R.r[2] = VectorAdd(r[2], M.r[2]);
    R.r[3] = VectorAdd(r[3], M.r[3]);
    return R;
}

inline Matrix MathCallConv Matrix::operator-(FXMMATRIX M)const noexcept{
    Matrix R;
    R.r[0] = VectorSubtract(r[0], M.r[0]);
    R.r[1] = VectorSubtract(r[1], M.r[1]);
    R.r[2] = VectorSubtract(r[2], M.r[2]);
    R.r[3] = VectorSubtract(r[3], M.r[3]);
    return R;
}

inline Matrix MathCallConv Matrix::operator*(FXMMATRIX M)const noexcept{
    return MatrixMultiply(*this, M);
}

inline Matrix Matrix::operator*(float S)const noexcept{
    Matrix R;
    R.r[0] = VectorScale(r[0], S);
    R.r[1] = VectorScale(r[1], S);
    R.r[2] = VectorScale(r[2], S);
    R.r[3] = VectorScale(r[3], S);
    return R;
}

inline Matrix Matrix::operator/(float S)const noexcept{
#if defined(_MATH_NO_INTRINSICS_)
    Vector vS = VectorReplicate(S);
    Matrix R;
    R.r[0] = VectorDivide(r[0], vS);
    R.r[1] = VectorDivide(r[1], vS);
    R.r[2] = VectorDivide(r[2], vS);
    R.r[3] = VectorDivide(r[3], vS);
    return R;
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
    float32x4_t vS = vdupq_n_f32(S);
    Matrix R;
    R.r[0] = vdivq_f32(r[0], vS);
    R.r[1] = vdivq_f32(r[1], vS);
    R.r[2] = vdivq_f32(r[2], vS);
    R.r[3] = vdivq_f32(r[3], vS);
#else
    // 2 iterations of Newton-Raphson refinement of reciprocal
    float32x2_t vS = vdup_n_f32(S);
    float32x2_t R0 = vrecpe_f32(vS);
    float32x2_t S0 = vrecps_f32(R0, vS);
    R0 = vmul_f32(S0, R0);
    S0 = vrecps_f32(R0, vS);
    R0 = vmul_f32(S0, R0);
    float32x4_t Reciprocal = vcombine_f32(R0, R0);
    Matrix R;
    R.r[0] = vmulq_f32(r[0], Reciprocal);
    R.r[1] = vmulq_f32(r[1], Reciprocal);
    R.r[2] = vmulq_f32(r[2], Reciprocal);
    R.r[3] = vmulq_f32(r[3], Reciprocal);
#endif
    return R;
#elif defined(_MATH_SSE_INTRINSICS_)
    __m128 vS = _mm_set_ps1(S);
    Matrix R;
    R.r[0] = _mm_div_ps(r[0], vS);
    R.r[1] = _mm_div_ps(r[1], vS);
    R.r[2] = _mm_div_ps(r[2], vS);
    R.r[3] = _mm_div_ps(r[3], vS);
    return R;
#endif
}

inline Matrix MathCallConv operator*(
    float S,
    FXMMATRIX M
    )noexcept{
    Matrix R;
    R.r[0] = VectorScale(M.r[0], S);
    R.r[1] = VectorScale(M.r[1], S);
    R.r[2] = VectorScale(M.r[2], S);
    R.r[3] = VectorScale(M.r[3], S);
    return R;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float3x4::Float3x4(const float* pArray)noexcept{
    assert(pArray != nullptr);

    m[0][0] = pArray[0];
    m[0][1] = pArray[1];
    m[0][2] = pArray[2];
    m[0][3] = pArray[3];

    m[1][0] = pArray[4];
    m[1][1] = pArray[5];
    m[1][2] = pArray[6];
    m[1][3] = pArray[7];

    m[2][0] = pArray[8];
    m[2][1] = pArray[9];
    m[2][2] = pArray[10];
    m[2][3] = pArray[11];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

