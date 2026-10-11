// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_construct.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_SSE2)
namespace SIMDVectorDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE NativeUIntPair EqualBitsPair(NativeUIntPair v0, NativeUIntPair v1)noexcept{
#if defined(NWB_HAS_SSE4)
    return _mm_cmpeq_epi64(v0, v1);
#else
    const __m128i words = _mm_cmpeq_epi32(v0, v1);
    return _mm_and_si128(words, _mm_shuffle_epi32(words, _MM_SHUFFLE(2, 3, 0, 1)));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetX(v0) == VectorGetX(v1) ? 0xFFFFFFFFu : 0u, VectorGetY(v0) == VectorGetY(v1) ? 0xFFFFFFFFu : 0u, VectorGetZ(v0) == VectorGetZ(v1) ? 0xFFFFFFFFu : 0u, VectorGetW(v0) == VectorGetW(v1) ? 0xFFFFFFFFu : 0u);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vceqq_f32(v0, v1));
#else
    return _mm_cmpeq_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(
        vceqq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vceqq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_cmp_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1), _CMP_EQ_OQ));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_cmpeq_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        _mm_cmpeq_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#else
    return BitCast<SIMDVectorDouble>(v0 == v1);
#endif
}

NWB_INLINE u32 NWB_SIMD_CALL VectorEqualR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqual(v0, v1)), VectorComponentMask::s_XYZW);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorDoubleEqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return SIMDVectorDoubleDetail::ComparisonMaskR(
        VectorDoubleMoveMask(VectorDoubleEqual(v0, v1)),
        VectorDoubleComponentMask::s_DoubleXYZW
    );
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorEqualInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetIntX(v0) == VectorGetIntX(v1) ? 0xFFFFFFFFu : 0u, VectorGetIntY(v0) == VectorGetIntY(v1) ? 0xFFFFFFFFu : 0u, VectorGetIntZ(v0) == VectorGetIntZ(v1) ? 0xFFFFFFFFu : 0u, VectorGetIntW(v0) == VectorGetIntW(v1) ? 0xFFFFFFFFu : 0u);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vceqq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1)));
#else
    return _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_castps_si128(v0), _mm_castps_si128(v1)));
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleEqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(
        vceqq_u64(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)),
        vceqq_u64(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1))
    );
#elif defined(NWB_HAS_AVX2)
    return BitCast<SIMDVectorDouble>(_mm256_cmpeq_epi64(BitCast<__m256i>(v0), BitCast<__m256i>(v1)));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromUIntPairs(
        SIMDVectorDoubleDetail::EqualBitsPair(SIMDVectorDoubleDetail::GetLowBits(v0), SIMDVectorDoubleDetail::GetLowBits(v1)),
        SIMDVectorDoubleDetail::EqualBitsPair(SIMDVectorDoubleDetail::GetHighBits(v0), SIMDVectorDoubleDetail::GetHighBits(v1))
    );
#else
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits == v1Bits);
#endif
}

NWB_INLINE u32 NWB_SIMD_CALL VectorEqualIntR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorEqualInt(v0, v1)), VectorComponentMask::s_XYZW);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorDoubleEqualIntR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return SIMDVectorDoubleDetail::ComparisonMaskR(
        VectorDoubleMoveMask(VectorDoubleEqualInt(v0, v1)),
        VectorDoubleComponentMask::s_DoubleXYZW
    );
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorNearEqual(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle epsilon)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(
        Abs(VectorGetX(v0) - VectorGetX(v1)) <= VectorGetX(epsilon) ? 0xFFFFFFFFu : 0u,
        Abs(VectorGetY(v0) - VectorGetY(v1)) <= VectorGetY(epsilon) ? 0xFFFFFFFFu : 0u,
        Abs(VectorGetZ(v0) - VectorGetZ(v1)) <= VectorGetZ(epsilon) ? 0xFFFFFFFFu : 0u,
        Abs(VectorGetW(v0) - VectorGetW(v1)) <= VectorGetW(epsilon) ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcleq_f32(vabsq_f32(vsubq_f32(v0, v1)), epsilon));
#else
    const SIMDVectorSingle delta = _mm_andnot_ps(s_SIMDNegativeZero, _mm_sub_ps(v0, v1));
    return _mm_cmple_ps(delta, epsilon);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNearEqual(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble epsilon)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(
        vcleq_f64(vabsq_f64(vsubq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1))), SIMDVectorDoubleDetail::GetLow(epsilon)),
        vcleq_f64(vabsq_f64(vsubq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))), SIMDVectorDoubleDetail::GetHigh(epsilon))
    );
#elif defined(NWB_HAS_AVX)
    const __m256d difference = _mm256_and_pd(
        _mm256_sub_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1)),
        BitCast<__m256d>(SIMDVectorDouble(s_DoubleSIMDAbsMask))
    );
    return BitCast<SIMDVectorDouble>(_mm256_cmp_pd(difference, BitCast<__m256d>(epsilon), _CMP_LE_OQ));
#elif defined(NWB_HAS_SSE2)
    const __m128d absMask = SIMDVectorDoubleDetail::GetLow(s_DoubleSIMDAbsMask);
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_cmple_pd(_mm_and_pd(_mm_sub_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), absMask), SIMDVectorDoubleDetail::GetLow(epsilon)),
        _mm_cmple_pd(_mm_and_pd(_mm_sub_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)), absMask), SIMDVectorDoubleDetail::GetHigh(epsilon))
    );
#else
    return BitCast<SIMDVectorDouble>(__builtin_elementwise_abs(v0 - v1) <= epsilon);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorNotEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(
        v0.f[0] != v1.f[0] ? 0xFFFFFFFFu : 0u,
        v0.f[1] != v1.f[1] ? 0xFFFFFFFFu : 0u,
        v0.f[2] != v1.f[2] ? 0xFFFFFFFFu : 0u,
        v0.f[3] != v1.f[3] ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vmvnq_u32(vceqq_f32(v0, v1)));
#else
    return _mm_cmpneq_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNotEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    const uint64x2_t allBits = vdupq_n_u64(0xFFFFFFFFFFFFFFFFu);
    return SIMDVectorDoubleDetail::FromUIntPairs(
        veorq_u64(vceqq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)), allBits),
        veorq_u64(vceqq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1)), allBits)
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_cmp_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1), _CMP_NEQ_UQ));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_cmpneq_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        _mm_cmpneq_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#else
    return BitCast<SIMDVectorDouble>(v0 != v1);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorNotEqualInt(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(
        v0.u[0] != v1.u[0] ? 0xFFFFFFFFu : 0u,
        v0.u[1] != v1.u[1] ? 0xFFFFFFFFu : 0u,
        v0.u[2] != v1.u[2] ? 0xFFFFFFFFu : 0u,
        v0.u[3] != v1.u[3] ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vmvnq_u32(vceqq_u32(vreinterpretq_u32_f32(v0), vreinterpretq_u32_f32(v1))));
#else
    const __m128i equal = _mm_cmpeq_epi32(_mm_castps_si128(v0), _mm_castps_si128(v1));
    return _mm_xor_ps(_mm_castsi128_ps(equal), s_SIMDNegOneMask);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNotEqualInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return VectorDoubleXorInt(VectorDoubleEqualInt(v0, v1), VectorDoubleTrueInt());
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorGreater(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcgtq_f32(v0, v1));
#elif defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetX(v0) > VectorGetX(v1) ? 0xFFFFFFFFu : 0u, VectorGetY(v0) > VectorGetY(v1) ? 0xFFFFFFFFu : 0u, VectorGetZ(v0) > VectorGetZ(v1) ? 0xFFFFFFFFu : 0u, VectorGetW(v0) > VectorGetW(v1) ? 0xFFFFFFFFu : 0u);
#else
    return _mm_cmpgt_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleGreater(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(
        vcgtq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vcgtq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_cmp_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1), _CMP_GT_OQ));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_cmpgt_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        _mm_cmpgt_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#else
    return BitCast<SIMDVectorDouble>(v0 > v1);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorGreaterOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSetInt(VectorGetX(v0) >= VectorGetX(v1) ? 0xFFFFFFFFu : 0u, VectorGetY(v0) >= VectorGetY(v1) ? 0xFFFFFFFFu : 0u, VectorGetZ(v0) >= VectorGetZ(v1) ? 0xFFFFFFFFu : 0u, VectorGetW(v0) >= VectorGetW(v1) ? 0xFFFFFFFFu : 0u);
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcgeq_f32(v0, v1));
#else
    return _mm_cmpge_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleGreaterOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(
        vcgeq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vcgeq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_cmp_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1), _CMP_GE_OQ));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_cmpge_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        _mm_cmpge_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#else
    return BitCast<SIMDVectorDouble>(v0 >= v1);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorLess(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(
        v0.f[0] < v1.f[0] ? 0xFFFFFFFFu : 0u,
        v0.f[1] < v1.f[1] ? 0xFFFFFFFFu : 0u,
        v0.f[2] < v1.f[2] ? 0xFFFFFFFFu : 0u,
        v0.f[3] < v1.f[3] ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcltq_f32(v0, v1));
#else
    return _mm_cmplt_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleLess(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(
        vcltq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vcltq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_cmp_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1), _CMP_LT_OQ));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_cmplt_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        _mm_cmplt_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#else
    return BitCast<SIMDVectorDouble>(v0 < v1);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorLessOrEqual(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeU32(
        v0.f[0] <= v1.f[0] ? 0xFFFFFFFFu : 0u,
        v0.f[1] <= v1.f[1] ? 0xFFFFFFFFu : 0u,
        v0.f[2] <= v1.f[2] ? 0xFFFFFFFFu : 0u,
        v0.f[3] <= v1.f[3] ? 0xFFFFFFFFu : 0u
    );
#elif defined(NWB_HAS_NEON)
    return vreinterpretq_f32_u32(vcleq_f32(v0, v1));
#else
    return _mm_cmple_ps(v0, v1);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleLessOrEqual(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromUIntPairs(
        vcleq_f64(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vcleq_f64(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_cmp_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1), _CMP_LE_OQ));
#elif defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_cmple_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        _mm_cmple_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#else
    return BitCast<SIMDVectorDouble>(v0 <= v1);
#endif
}

NWB_INLINE u32 NWB_SIMD_CALL VectorGreaterR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreater(v0, v1)), VectorComponentMask::s_XYZW);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorDoubleGreaterR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return SIMDVectorDoubleDetail::ComparisonMaskR(
        VectorDoubleMoveMask(VectorDoubleGreater(v0, v1)),
        VectorDoubleComponentMask::s_DoubleXYZW
    );
}

NWB_INLINE u32 NWB_SIMD_CALL VectorGreaterOrEqualR(SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    return SIMDVectorDetail::ComparisonMaskR(VectorMoveMask(VectorGreaterOrEqual(v0, v1)), VectorComponentMask::s_XYZW);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorDoubleGreaterOrEqualR(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return SIMDVectorDoubleDetail::ComparisonMaskR(
        VectorDoubleMoveMask(VectorDoubleGreaterOrEqual(v0, v1)),
        VectorDoubleComponentMask::s_DoubleXYZW
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorInBounds(SIMDVectorSingle value, SIMDVectorSingle bounds)noexcept{
    return VectorLessOrEqual(VectorAndInt(value, s_SIMDAbsMask), bounds);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleInBounds(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{
    return VectorDoubleLessOrEqual(VectorDoubleAndInt(value, s_DoubleSIMDAbsMask), bounds);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorInBoundsR(SIMDVectorSingle value, SIMDVectorSingle bounds)noexcept{
    return SIMDVectorDetail::BoundsMaskR(VectorMoveMask(VectorInBounds(value, bounds)), VectorComponentMask::s_XYZW);
}

NWB_INLINE u32 NWB_SIMD_CALL VectorDoubleInBoundsR(SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{
    return SIMDVectorDoubleDetail::BoundsMaskR(
        VectorDoubleMoveMask(VectorDoubleInBounds(value, bounds)),
        VectorDoubleComponentMask::s_DoubleXYZW
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorEqualR(u32& outCR, SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    outCR = VectorEqualR(v0, v1);
    return VectorEqual(v0, v1);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleEqualR(u32& outCR, SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    outCR = VectorDoubleEqualR(v0, v1);
    return VectorDoubleEqual(v0, v1);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorEqualIntR(u32& outCR, SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    outCR = VectorEqualIntR(v0, v1);
    return VectorEqualInt(v0, v1);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleEqualIntR(u32& outCR, SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    outCR = VectorDoubleEqualIntR(v0, v1);
    return VectorDoubleEqualInt(v0, v1);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorGreaterR(u32& outCR, SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    outCR = VectorGreaterR(v0, v1);
    return VectorGreater(v0, v1);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleGreaterR(u32& outCR, SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    outCR = VectorDoubleGreaterR(v0, v1);
    return VectorDoubleGreater(v0, v1);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorGreaterOrEqualR(u32& outCR, SIMDVectorSingle v0, SIMDVectorSingle v1)noexcept{
    outCR = VectorGreaterOrEqualR(v0, v1);
    return VectorGreaterOrEqual(v0, v1);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleGreaterOrEqualR(u32& outCR, SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    outCR = VectorDoubleGreaterOrEqualR(v0, v1);
    return VectorDoubleGreaterOrEqual(v0, v1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorInBoundsR(u32& outCR, SIMDVectorSingle value, SIMDVectorSingle bounds)noexcept{
    outCR = VectorInBoundsR(value, bounds);
    return VectorInBounds(value, bounds);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleInBoundsR(u32& outCR, SIMDVectorDouble value, SIMDVectorDouble bounds)noexcept{
    outCR = VectorDoubleInBoundsR(value, bounds);
    return VectorDoubleInBounds(value, bounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorIsNaN(SIMDVectorSingle value)noexcept{
    return VectorNotEqual(value, value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleIsNaN(SIMDVectorDouble value)noexcept{
    return VectorDoubleNotEqual(value, value);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorIsInfinite(SIMDVectorSingle value)noexcept{
    return VectorEqualInt(VectorAndInt(value, s_SIMDAbsMask), s_SIMDInfinity);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleIsInfinite(SIMDVectorDouble value)noexcept{
    return VectorDoubleEqualInt(VectorDoubleAndInt(value, s_DoubleSIMDAbsMask), s_DoubleSIMDInfinity);
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL VectorIsFinite(SIMDVectorSingle value, u32 activeMask)noexcept{
    const SIMDVectorSingle invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    return (VectorMoveMask(invalid) & activeMask) == 0u;
}

[[nodiscard]] NWB_INLINE bool NWB_SIMD_CALL VectorDoubleIsFinite(SIMDVectorDouble value, u32 activeMask)noexcept{
    const SIMDVectorDouble invalid = VectorDoubleOrInt(VectorDoubleIsNaN(value), VectorDoubleIsInfinite(value));
    return (VectorDoubleMoveMask(invalid) & activeMask) == 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

