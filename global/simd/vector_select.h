// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_access.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSelectControl(u32 selectX, u32 selectY, u32 selectZ, u32 selectW)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(selectX ? s_SELECT_1 : 0u, selectY ? s_SELECT_1 : 0u, selectZ ? s_SELECT_1 : 0u, selectW ? s_SELECT_1 : 0u);
#elif defined(NWB_HAS_NEON)
    const int32x2_t lo = vcreate_s32(static_cast<u64>(selectX) | (static_cast<u64>(selectY) << s_F32BitCount));
    const int32x2_t hi = vcreate_s32(static_cast<u64>(selectZ) | (static_cast<u64>(selectW) << s_F32BitCount));
    return vreinterpretq_f32_u32(vmvnq_u32(vceqq_s32(vcombine_s32(lo, hi), vdupq_n_s32(0))));
#else
    __m128i value = _mm_set_epi32(static_cast<i32>(selectW), static_cast<i32>(selectZ), static_cast<i32>(selectY), static_cast<i32>(selectX));
    const __m128i isZero = _mm_cmpeq_epi32(value, _mm_setzero_si128());
    return _mm_castsi128_ps(_mm_xor_si128(isZero, _mm_set1_epi32(-1)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSelectControl(u32 selectX, u32 selectY, u32 selectZ, u32 selectW)noexcept{
    return VectorDoubleSetInt(selectX ? s_DoubleSELECT_1 : 0u, selectY ? s_DoubleSELECT_1 : 0u, selectZ ? s_DoubleSELECT_1 : 0u, selectW ? s_DoubleSELECT_1 : 0u);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorAndInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) & VectorGetIntX(v1), VectorGetIntY(v0) & VectorGetIntY(v1), VectorGetIntZ(v0) & VectorGetIntZ(v1), VectorGetIntW(v0) & VectorGetIntW(v1));
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vandq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_and_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAndInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(vandq_u64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)), vandq_u64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_and_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_and_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), _mm_and_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#else
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits & v1Bits);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorOrInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) | VectorGetIntX(v1), VectorGetIntY(v0) | VectorGetIntY(v1), VectorGetIntZ(v0) | VectorGetIntZ(v1), VectorGetIntW(v0) | VectorGetIntW(v1));
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vorrq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_or_si128(_mm_castps_si128(v0), _mm_castps_si128(v1)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleOrInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(vorrq_u64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)), vorrq_u64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_or_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_or_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), _mm_or_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#else
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits | v1Bits);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorXorInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) ^ VectorGetIntX(v1), VectorGetIntY(v0) ^ VectorGetIntY(v1), VectorGetIntZ(v0) ^ VectorGetIntZ(v1), VectorGetIntW(v0) ^ VectorGetIntW(v1));
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(veorq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_xor_si128(_mm_castps_si128(v0), _mm_castps_si128(v1)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleXorInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(veorq_u64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)), veorq_u64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_xor_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_xor_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), _mm_xor_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)));
#else
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits ^ v1Bits);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorAndCInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(v0.u[0] & ~v1.u[0], v0.u[1] & ~v1.u[1], v0.u[2] & ~v1.u[2], v0.u[3] & ~v1.u[3]);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_andnot_si128(_mm_castps_si128(v1), _mm_castps_si128(v0)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAndCInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(vbicq_u64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)), vbicq_u64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1)));
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_andnot_pd(BitCast<__m256d>(v1), BitCast<__m256d>(v0)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(_mm_andnot_pd(SIMDVectorDoubleDetail::GetLow(v1), SIMDVectorDoubleDetail::GetLow(v0)), _mm_andnot_pd(SIMDVectorDoubleDetail::GetHigh(v1), SIMDVectorDoubleDetail::GetHigh(v0)));
#else
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits & ~v1Bits);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorNorInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(~(v0.u[0] | v1.u[0]), ~(v0.u[1] | v1.u[1]), ~(v0.u[2] | v1.u[2]), ~(v0.u[3] | v1.u[3]));
#elif defined(NWB_HAS_NEON)
    const uint32x4_t value = vorrq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1));
    return vreinterpretq_f32_u32(vbicq_u32(vreinterpretq_u32_f32(s_SIMDNegOneMask), value));
#else
    const __m128i value = _mm_or_si128(_mm_castps_si128(v0), _mm_castps_si128(v1));
    return _mm_castsi128_ps(_mm_andnot_si128(value, s_SIMDNegOneMask));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNorInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return VectorDoubleXorInt(VectorDoubleOrInt(v0, v1), VectorDoubleTrueInt());
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorSelect(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle control)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorOrInt(VectorAndCInt(v0, control), VectorAndInt(v1, control));
#elif defined(NWB_HAS_NEON)
    return vbslq_f32(vreinterpretq_u32_f32(control), v1, v0);
#else
    return _mm_or_ps(_mm_andnot_ps(control, v0), _mm_and_ps(v1, control));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSelect(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble control)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(
        vbslq_u64(SIMDVectorDoubleDetail::GetLowBits(control), SIMDVectorDoubleDetail::GetLowBits(v1), SIMDVectorDoubleDetail::GetLowBits(v0)),
        vbslq_u64(SIMDVectorDoubleDetail::GetHighBits(control), SIMDVectorDoubleDetail::GetHighBits(v1), SIMDVectorDoubleDetail::GetHighBits(v0))
    );
#elif defined(NWB_HAS_AVX)
    const __m256d mask = BitCast<__m256d>(control);
    return BitCast<SIMDVectorDouble>(_mm256_or_pd(_mm256_andnot_pd(mask, BitCast<__m256d>(v0)), _mm256_and_pd(mask, BitCast<__m256d>(v1))));
#elif defined(NWB_HAS_SSE2)
    const __m128d low = SIMDVectorDoubleDetail::GetLow(control);
    const __m128d high = SIMDVectorDoubleDetail::GetHigh(control);
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_or_pd(_mm_andnot_pd(low, SIMDVectorDoubleDetail::GetLow(v0)), _mm_and_pd(low, SIMDVectorDoubleDetail::GetLow(v1))),
        _mm_or_pd(_mm_andnot_pd(high, SIMDVectorDoubleDetail::GetHigh(v0)), _mm_and_pd(high, SIMDVectorDoubleDetail::GetHigh(v1)))
    );
#else
    return VectorDoubleOrInt(VectorDoubleAndCInt(v0, control), VectorDoubleAndInt(v1, control));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

