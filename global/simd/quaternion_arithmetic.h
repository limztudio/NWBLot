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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDQuaternionDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const SIMDVectorDoubleConstF s_DoubleSIMDQuaternionControlWZYX = { { { 1.0, -1.0, 1.0, -1.0 } } };
static const SIMDVectorDoubleConstF s_DoubleSIMDQuaternionControlZWXY = { { { 1.0, 1.0, -1.0, -1.0 } } };
static const SIMDVectorDoubleConstF s_DoubleSIMDQuaternionControlYXWZ = { { { -1.0, 1.0, 1.0, -1.0 } } };
inline constexpr f64 s_DoubleQuaternionInterpolationEpsilon = 1.0e-12;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL QuaternionIsNaN(SIMDVectorSingle q)noexcept{ return Vector4IsNaN(q); }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL QuaternionDoubleIsNaN(SIMDVectorDouble q)noexcept{ return VectorDouble4IsNaN(q); }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL QuaternionIsInfinite(SIMDVectorSingle q)noexcept{ return Vector4IsInfinite(q); }

FOUNDATION_INLINE b8 FOUNDATION_SIMD_CALL QuaternionDoubleIsInfinite(SIMDVectorDouble q)noexcept{ return VectorDouble4IsInfinite(q); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL QuaternionDoubleDot(SIMDVectorDouble q0, SIMDVectorDouble q1)noexcept{ return VectorDouble4Dot(q0, q1); }

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL QuaternionMultiply(SIMDVectorSingle lhs, SIMDVectorSingle rhs)noexcept{
#if defined(FOUNDATION_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(
        (lhs.f[3] * rhs.f[0]) + (lhs.f[0] * rhs.f[3]) + (lhs.f[1] * rhs.f[2]) - (lhs.f[2] * rhs.f[1]),
        (lhs.f[3] * rhs.f[1]) - (lhs.f[0] * rhs.f[2]) + (lhs.f[1] * rhs.f[3]) + (lhs.f[2] * rhs.f[0]),
        (lhs.f[3] * rhs.f[2]) + (lhs.f[0] * rhs.f[1]) - (lhs.f[1] * rhs.f[0]) + (lhs.f[2] * rhs.f[3]),
        (lhs.f[3] * rhs.f[3]) - (lhs.f[0] * rhs.f[0]) - (lhs.f[1] * rhs.f[1]) - (lhs.f[2] * rhs.f[2])
    );
#elif defined(FOUNDATION_HAS_NEON)
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
#if defined(FOUNDATION_HAS_AVX)
    result = _mm_permute_ps(result, _MM_SHUFFLE(3, 3, 3, 3));
#if defined(FOUNDATION_HAS_AVX2)
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
#if defined(FOUNDATION_HAS_AVX)
    rhsShuffle = _mm_permute_ps(rhsShuffle, _MM_SHUFFLE(0, 1, 2, 3));
#else
    rhsShuffle = _mm_shuffle_ps(rhsShuffle, rhsShuffle, _MM_SHUFFLE(0, 1, 2, 3));
#endif

    lhsX = _mm_mul_ps(lhsX, rhsShuffle);
    result = VectorMultiplyAdd(lhsX, SIMDQuaternionDetail::s_SIMDQuaternionControlWZYX, result);

#if defined(FOUNDATION_HAS_AVX)
    rhsShuffle = _mm_permute_ps(rhsShuffle, _MM_SHUFFLE(2, 3, 0, 1));
#else
    rhsShuffle = _mm_shuffle_ps(rhsShuffle, rhsShuffle, _MM_SHUFFLE(2, 3, 0, 1));
#endif
    lhsY = _mm_mul_ps(lhsY, rhsShuffle);
    lhsY = _mm_mul_ps(lhsY, SIMDQuaternionDetail::s_SIMDQuaternionControlZWXY);

#if defined(FOUNDATION_HAS_AVX)
    rhsShuffle = _mm_permute_ps(rhsShuffle, _MM_SHUFFLE(0, 1, 2, 3));
#else
    rhsShuffle = _mm_shuffle_ps(rhsShuffle, rhsShuffle, _MM_SHUFFLE(0, 1, 2, 3));
#endif
    lhsZ = _mm_mul_ps(lhsZ, rhsShuffle);
    lhsY = VectorMultiplyAdd(lhsZ, SIMDQuaternionDetail::s_SIMDQuaternionControlYXWZ, lhsY);
    return _mm_add_ps(result, lhsY);
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL QuaternionDoubleMultiply(SIMDVectorDouble lhs, SIMDVectorDouble rhs)noexcept{
    SIMDVectorDouble result = VectorDoubleMultiply(VectorDoubleSplatW(lhs), rhs);
    SIMDVectorDouble component = VectorDoubleMultiply(VectorDoubleSwizzle<3, 2, 1, 0>(rhs), SIMDQuaternionDoubleDetail::s_DoubleSIMDQuaternionControlWZYX);
    result = VectorDoubleMultiplyAdd(VectorDoubleSplatX(lhs), component, result);
    component = VectorDoubleMultiply(VectorDoubleSwizzle<2, 3, 0, 1>(rhs), SIMDQuaternionDoubleDetail::s_DoubleSIMDQuaternionControlZWXY);
    result = VectorDoubleMultiplyAdd(VectorDoubleSplatY(lhs), component, result);
    component = VectorDoubleMultiply(VectorDoubleSwizzle<1, 0, 3, 2>(rhs), SIMDQuaternionDoubleDetail::s_DoubleSIMDQuaternionControlYXWZ);
    return VectorDoubleMultiplyAdd(VectorDoubleSplatZ(lhs), component, result);
}

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL QuaternionLengthSq(SIMDVectorSingle q)noexcept{ return Vector4LengthSq(q); }

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL QuaternionDoubleLengthSq(SIMDVectorDouble q)noexcept{ return VectorDouble4LengthSq(q); }

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL QuaternionNormalize(SIMDVectorSingle q)noexcept{ return Vector4Normalize(q); }

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL QuaternionDoubleNormalize(SIMDVectorDouble q)noexcept{ return VectorDouble4Normalize(q); }

FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL QuaternionConjugate(SIMDVectorSingle q)noexcept{ return VectorXorInt(q, s_SIMDNegate3); }

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL QuaternionDoubleConjugate(SIMDVectorDouble q)noexcept{ return VectorDoubleXorInt(q, s_DoubleSIMDNegate3); }

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL QuaternionDoubleInverse(SIMDVectorDouble q)noexcept{
    const SIMDVectorDouble lengthSq = QuaternionDoubleLengthSq(q);
    const SIMDVectorDouble conjugate = QuaternionDoubleConjugate(q);
    const SIMDVectorDouble control = VectorDoubleLessOrEqual(lengthSq, s_DoubleSIMDEpsilon);
    const SIMDVectorDouble result = VectorDoubleDivide(conjugate, lengthSq);
    return VectorDoubleSelect(result, VectorDoubleZero(), control);
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL QuaternionDoubleLn(SIMDVectorDouble q)noexcept{
    const SIMDVectorDouble oneMinusEpsilon = VectorDoubleReplicate(1.0 - SIMDQuaternionDoubleDetail::s_DoubleQuaternionInterpolationEpsilon);
    const SIMDVectorDouble qw = VectorDoubleSplatW(q);
    const SIMDVectorDouble q0 = VectorDoubleAndInt(q, s_DoubleSIMDMask3);
    const SIMDVectorDouble controlW = VectorDoubleInBounds(qw, oneMinusEpsilon);
    const SIMDVectorDouble theta = SIMDVectorDoubleDetail::ACosReplicated(qw);
    const SIMDVectorDouble sinTheta = SIMDVectorDoubleDetail::SinReplicated(theta);
    const SIMDVectorDouble scale = VectorDoubleDivide(theta, sinTheta);
    const SIMDVectorDouble result = VectorDoubleMultiply(q0, scale);
    return VectorDoubleSelect(q0, result, controlW);
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL QuaternionDoubleExp(SIMDVectorDouble q)noexcept{
    const SIMDVectorDouble theta = VectorDouble3Length(q);
    SIMDVectorDouble sinTheta{};
    SIMDVectorDouble cosTheta{};
#if defined(FOUNDATION_HAS_AVX2) || defined(FOUNDATION_HAS_NEON64)
    SIMDVectorDoubleDetail::SinCosReplicated(sinTheta, cosTheta, theta);
#else
    VectorDoubleSinCos(sinTheta, cosTheta, theta);
#endif

    const SIMDVectorDouble scale = VectorDoubleDivide(sinTheta, theta);
    SIMDVectorDouble result = VectorDoubleMultiply(q, scale);
    result = VectorDoubleSelect(result, q, VectorDoubleNearEqual(theta, VectorDoubleZero(), s_DoubleSIMDEpsilon));
    return VectorDoubleSelect(cosTheta, result, s_DoubleSIMDSelect1110);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

