// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_compare.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL VectorNegate(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(-VectorGetX(value), -VectorGetY(value), -VectorGetZ(value), -VectorGetW(value));
#elif defined(NWB_HAS_NEON)
    return vnegq_f32(value);
#else
    return _mm_xor_ps(value, s_SIMDNegativeZero);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorAdd(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) + VectorGetX(v1), VectorGetY(v0) + VectorGetY(v1), VectorGetZ(v0) + VectorGetZ(v1), VectorGetW(v0) + VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vaddq_f32(v0, v1);
#else
    return _mm_add_ps(v0, v1);
#endif
}

// Adds raw unsigned 32-bit lane representations with modulo arithmetic.
NWB_INLINE SIMDVector SIMDCALL VectorAddInt(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(
        VectorGetIntX(v0) + VectorGetIntX(v1),
        VectorGetIntY(v0) + VectorGetIntY(v1),
        VectorGetIntZ(v0) + VectorGetIntZ(v1),
        VectorGetIntW(v0) + VectorGetIntW(v1)
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vaddq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_add_epi32(_mm_castps_si128(v0), _mm_castps_si128(v1)));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSubtract(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) - VectorGetX(v1), VectorGetY(v0) - VectorGetY(v1), VectorGetZ(v0) - VectorGetZ(v1), VectorGetW(v0) - VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vsubq_f32(v0, v1);
#else
    return _mm_sub_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMultiply(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) * VectorGetX(v1), VectorGetY(v0) * VectorGetY(v1), VectorGetZ(v0) * VectorGetZ(v1), VectorGetW(v0) * VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vmulq_f32(v0, v1);
#else
    return _mm_mul_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorDivide(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) / VectorGetX(v1), VectorGetY(v0) / VectorGetY(v1), VectorGetZ(v0) / VectorGetZ(v1), VectorGetW(v0) / VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    return vdivq_f32(v0, v1);
#else
    float32x4_t reciprocal = vrecpeq_f32(v1);
    float32x4_t scale = vrecpsq_f32(reciprocal, v1);
    reciprocal = vmulq_f32(scale, reciprocal);
    scale = vrecpsq_f32(reciprocal, v1);
    reciprocal = vmulq_f32(scale, reciprocal);
    return vmulq_f32(v0, reciprocal);
#endif
#else
    return _mm_div_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMultiplyAdd(SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept{
#if defined(__FMA__) || defined(_M_FMA)
    return _mm_fmadd_ps(v0, v1, v2);
#elif defined(NWB_HAS_NEON)
    return vmlaq_f32(v2, v0, v1);
#else
    return VectorAdd(VectorMultiply(v0, v1), v2);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorNegativeMultiplySubtract(SIMDVector v0, SIMDVector v1, SIMDVector v2)noexcept{
#if defined(__FMA__) || defined(_M_FMA)
    return _mm_fnmadd_ps(v0, v1, v2);
#elif defined(NWB_HAS_NEON)
    return vmlsq_f32(v2, v0, v1);
#else
    return VectorSubtract(v2, VectorMultiply(v0, v1));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorScale(SIMDVector value, f32 scale)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(value.f[0] * scale, value.f[1] * scale, value.f[2] * scale, value.f[3] * scale);
#elif defined(NWB_HAS_NEON)
    return vmulq_n_f32(value, scale);
#else
    return _mm_mul_ps(_mm_set1_ps(scale), value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMin(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) < VectorGetX(v1) ? VectorGetX(v0) : VectorGetX(v1), VectorGetY(v0) < VectorGetY(v1) ? VectorGetY(v0) : VectorGetY(v1), VectorGetZ(v0) < VectorGetZ(v1) ? VectorGetZ(v0) : VectorGetZ(v1), VectorGetW(v0) < VectorGetW(v1) ? VectorGetW(v0) : VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vminq_f32(v0, v1);
#else
    return _mm_min_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMax(SIMDVector v0, SIMDVector v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(VectorGetX(v0) > VectorGetX(v1) ? VectorGetX(v0) : VectorGetX(v1), VectorGetY(v0) > VectorGetY(v1) ? VectorGetY(v0) : VectorGetY(v1), VectorGetZ(v0) > VectorGetZ(v1) ? VectorGetZ(v0) : VectorGetZ(v1), VectorGetW(v0) > VectorGetW(v1) ? VectorGetW(v0) : VectorGetW(v1));
#elif defined(NWB_HAS_NEON)
    return vmaxq_f32(v0, v1);
#else
    return _mm_max_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorClamp(SIMDVector value, SIMDVector minValue, SIMDVector maxValue)noexcept{
    NWB_ASSERT((VectorMoveMask(VectorLessOrEqual(minValue, maxValue)) & VectorComponentMask::s_XYZW) == VectorComponentMask::s_XYZW);
    return VectorMin(maxValue, VectorMax(minValue, value));
}

NWB_INLINE SIMDVector SIMDCALL VectorSaturate(SIMDVector value)noexcept{
    return VectorMin(VectorSplatOne(), VectorMax(VectorZero(), value));
}

NWB_INLINE SIMDVector SIMDCALL VectorAbs(SIMDVector value)noexcept{
    return VectorAndInt(value, s_SIMDAbsMask);
}

NWB_INLINE SIMDVector SIMDCALL VectorSum(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReplicate(VectorGetX(value) + VectorGetY(value) + VectorGetZ(value) + VectorGetW(value));
#elif defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    SIMDVector temp = vpaddq_f32(value, value);
    return vpaddq_f32(temp, temp);
#else
    float32x2_t lo = vget_low_f32(value);
    float32x2_t hi = vget_high_f32(value);
    lo = vadd_f32(lo, hi);
    lo = vpadd_f32(lo, lo);
    return vcombine_f32(lo, lo);
#endif
#else
    SIMDVector temp = _mm_hadd_ps(value, value);
    return _mm_hadd_ps(temp, temp);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMergeX(SIMDVector x, SIMDVector y, SIMDVector z, SIMDVector w)noexcept{
    const SIMDVector xy = VectorPermute<0, 4, 1, 5>(x, y);
    const SIMDVector zw = VectorPermute<0, 4, 1, 5>(z, w);
    return VectorPermute<0, 1, 4, 5>(xy, zw);
}

NWB_INLINE SIMDVector SIMDCALL Vector4MinComponent(SIMDVector value)noexcept{
    SIMDVector result = VectorMin(value, VectorSwizzle<2, 3, 0, 1>(value));
    result = VectorMin(result, VectorSwizzle<1, 0, 3, 2>(result));
    return result;
}

NWB_INLINE SIMDVector SIMDCALL Vector4MaxComponent(SIMDVector value)noexcept{
    SIMDVector result = VectorMax(value, VectorSwizzle<2, 3, 0, 1>(value));
    result = VectorMax(result, VectorSwizzle<1, 0, 3, 2>(result));
    return result;
}

NWB_INLINE SIMDVector SIMDCALL Vector3MinComponent(SIMDVector value)noexcept{
    return Vector4MinComponent(VectorSwizzle<0, 1, 2, 0>(value));
}

NWB_INLINE SIMDVector SIMDCALL Vector3MaxComponent(SIMDVector value)noexcept{
    return Vector4MaxComponent(VectorSwizzle<0, 1, 2, 0>(value));
}

NWB_INLINE SIMDVector SIMDCALL VectorReciprocal(SIMDVector value)noexcept{
#if defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    return vdivq_f32(vdupq_n_f32(1.0f), value);
#else
    float32x4_t reciprocal = vrecpeq_f32(value);
    float32x4_t scale = vrecpsq_f32(reciprocal, value);
    reciprocal = vmulq_f32(scale, reciprocal);
    scale = vrecpsq_f32(reciprocal, value);
    return vmulq_f32(scale, reciprocal);
#endif
#elif defined(NWB_HAS_SSE4)
    return _mm_div_ps(s_SIMDOne, value);
#else
    return VectorDivide(VectorSplatOne(), value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorReciprocalEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReciprocal(value);
#elif defined(NWB_HAS_NEON)
    return vrecpeq_f32(value);
#else
    return _mm_rcp_ps(value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSqrt(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(Sqrt(VectorGetX(value)), Sqrt(VectorGetY(value)), Sqrt(VectorGetZ(value)), Sqrt(VectorGetW(value)));
#elif defined(NWB_HAS_NEON)
    float32x4_t s0 = vrsqrteq_f32(value);
    float32x4_t p0 = vmulq_f32(value, s0);
    float32x4_t r0 = vrsqrtsq_f32(p0, s0);
    float32x4_t s1 = vmulq_f32(s0, r0);
    float32x4_t p1 = vmulq_f32(value, s1);
    float32x4_t r1 = vrsqrtsq_f32(p1, s1);
    float32x4_t s2 = vmulq_f32(s1, r1);
    float32x4_t p2 = vmulq_f32(value, s2);
    float32x4_t r2 = vrsqrtsq_f32(p2, s2);
    float32x4_t result = vmulq_f32(value, vmulq_f32(s2, r2));
    const SIMDVector select = VectorEqualInt(VectorEqualInt(value, s_SIMDInfinity), VectorEqual(value, VectorZero()));
    return VectorSelect(value, result, select);
#else
    return _mm_sqrt_ps(value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorSqrtEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_NEON)
    float32x4_t s0 = vrsqrteq_f32(value);
    float32x4_t p0 = vmulq_f32(value, s0);
    float32x4_t r0 = vrsqrtsq_f32(p0, s0);
    SIMDVector result = vmulq_f32(value, vmulq_f32(s0, r0));
    const SIMDVector select = VectorEqualInt(VectorEqualInt(value, s_SIMDInfinity), VectorEqual(value, VectorZero()));
    return VectorSelect(value, result, select);
#else
    return VectorSqrt(value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorReciprocalSqrt(SIMDVector value)noexcept{
#if defined(NWB_HAS_NEON)
    float32x4_t s0 = vrsqrteq_f32(value);
    float32x4_t p0 = vmulq_f32(value, s0);
    float32x4_t r0 = vrsqrtsq_f32(p0, s0);
    float32x4_t s1 = vmulq_f32(s0, r0);
    float32x4_t p1 = vmulq_f32(value, s1);
    float32x4_t r1 = vrsqrtsq_f32(p1, s1);
    return vmulq_f32(s1, r1);
#elif defined(NWB_HAS_SSE4)
    return _mm_div_ps(s_SIMDOne, _mm_sqrt_ps(value));
#else
    return VectorReciprocal(VectorSqrt(value));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorReciprocalSqrtEst(SIMDVector value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorReciprocalSqrt(value);
#elif defined(NWB_HAS_NEON)
    return vrsqrteq_f32(value);
#else
    return _mm_rsqrt_ps(value);
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorRound(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_round_ps(value, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
#elif defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    return vrndnq_f32(value);
#else
    const uint32x4_t sign = vandq_u32(vreinterpretq_u32_f32(value), vreinterpretq_u32_f32(s_SIMDNegativeZero));
    const float32x4_t magic = vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(s_SIMDNoFraction), sign));
    float32x4_t result = vaddq_f32(value, magic);
    result = vsubq_f32(result, magic);
    const uint32x4_t mask = vcleq_f32(vabsq_f32(value), s_SIMDNoFraction);
    return vbslq_f32(mask, result, value);
#endif
#else
    return SIMDConvertDetail::MakeF32(
        SIMDVectorDetail::RoundToNearest(value.f[0]),
        SIMDVectorDetail::RoundToNearest(value.f[1]),
        SIMDVectorDetail::RoundToNearest(value.f[2]),
        SIMDVectorDetail::RoundToNearest(value.f[3])
    );
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorTruncate(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_round_ps(value, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
#elif defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    return vrndq_f32(value);
#else
    const uint32x4_t mask = vcltq_f32(vabsq_f32(value), s_SIMDNoFraction);
    const int32x4_t integer = vcvtq_s32_f32(value);
    const float32x4_t result = vcvtq_f32_s32(integer);
    return vbslq_f32(mask, result, value);
#endif
#else
    return SIMDConvertDetail::MakeU32(
        SIMDVectorDetail::TruncateBits(value.f[0]),
        SIMDVectorDetail::TruncateBits(value.f[1]),
        SIMDVectorDetail::TruncateBits(value.f[2]),
        SIMDVectorDetail::TruncateBits(value.f[3])
    );
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorFloor(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_floor_ps(value);
#elif defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    return vrndmq_f32(value);
#else
    const uint32x4_t mask = vcltq_f32(vabsq_f32(value), s_SIMDNoFraction);
    const int32x4_t integer = vcvtq_s32_f32(value);
    float32x4_t result = vcvtq_f32_s32(integer);
    const uint32x4_t largerMask = vcgtq_f32(result, value);
    const float32x4_t larger = vcvtq_f32_s32(vreinterpretq_s32_u32(largerMask));
    result = vaddq_f32(result, larger);
    return vbslq_f32(mask, result, value);
#endif
#else
    return SIMDConvertDetail::MakeF32(Floor(value.f[0]), Floor(value.f[1]), Floor(value.f[2]), Floor(value.f[3]));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorCeiling(SIMDVector value)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_ceil_ps(value);
#elif defined(NWB_HAS_NEON)
#if defined(__aarch64__) || defined(_M_ARM64)
    return vrndpq_f32(value);
#else
    const uint32x4_t mask = vcltq_f32(vabsq_f32(value), s_SIMDNoFraction);
    const int32x4_t integer = vcvtq_s32_f32(value);
    float32x4_t result = vcvtq_f32_s32(integer);
    const uint32x4_t smallerMask = vcltq_f32(result, value);
    const float32x4_t smaller = vcvtq_f32_s32(vreinterpretq_s32_u32(smallerMask));
    result = vsubq_f32(result, smaller);
    return vbslq_f32(mask, result, value);
#endif
#else
    return SIMDConvertDetail::MakeF32(Ceil(value.f[0]), Ceil(value.f[1]), Ceil(value.f[2]), Ceil(value.f[3]));
#endif
}

NWB_INLINE SIMDVector SIMDCALL VectorMod(SIMDVector v0, SIMDVector v1)noexcept{
    const SIMDVector quotient = VectorFloor(VectorDivide(v0, v1));
    return VectorNegativeMultiplySubtract(v1, quotient, v0);
}

NWB_INLINE SIMDVector SIMDCALL VectorModAngles(SIMDVector angles)noexcept{
    SIMDVector quotient = VectorMultiply(angles, s_SIMDReciprocalTwoPi);
    quotient = VectorRound(quotient);
    return VectorNegativeMultiplySubtract(s_SIMDTwoPi, quotient, angles);
}

NWB_INLINE SIMDVector SIMDCALL VectorAddAngles(SIMDVector v0, SIMDVector v1)noexcept{
    SIMDVector result = VectorAdd(v0, v1);
    SIMDVector mask = VectorLess(result, s_SIMDNegativePi);
    SIMDVector offset = VectorSelect(VectorZero(), s_SIMDTwoPi, mask);
    mask = VectorGreaterOrEqual(result, s_SIMDPi);
    offset = VectorSelect(offset, s_SIMDNegativeTwoPi, mask);
    return VectorAdd(result, offset);
}

NWB_INLINE SIMDVector SIMDCALL VectorSubtractAngles(SIMDVector v0, SIMDVector v1)noexcept{
    SIMDVector result = VectorSubtract(v0, v1);
    SIMDVector mask = VectorLess(result, s_SIMDNegativePi);
    SIMDVector offset = VectorSelect(VectorZero(), s_SIMDTwoPi, mask);
    mask = VectorGreaterOrEqual(result, s_SIMDPi);
    offset = VectorSelect(offset, s_SIMDNegativeTwoPi, mask);
    return VectorAdd(result, offset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

