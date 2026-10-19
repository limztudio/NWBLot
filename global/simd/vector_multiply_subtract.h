// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_arithmetic.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorMultiplySubtract(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle v2)noexcept{
#if defined(NWB_HAS_FMA3)
    return _mm_fmsub_ps(v0, v1, v2);
#elif defined(NWB_HAS_NEON64)
    return vfmaq_f32(vnegq_f32(v2), v0, v1);
#else
    return VectorSubtract(VectorMultiply(v0, v1), v2);
#endif
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMultiplySubtract(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble v2)noexcept{
#if defined(NWB_HAS_NEON64)
    return SIMDVectorDoubleDetail::FromPairs(
        vfmaq_f64(vnegq_f64(SIMDVectorDoubleDetail::GetLow(v2)), SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1)),
        vfmaq_f64(vnegq_f64(SIMDVectorDoubleDetail::GetHigh(v2)), SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1))
    );
#elif defined(NWB_HAS_FMA3) && defined(NWB_HAS_AVX)
    return BitCast<SIMDVectorDouble>(_mm256_fmsub_pd(BitCast<__m256d>(v0), BitCast<__m256d>(v1), BitCast<__m256d>(v2)));
#elif defined(NWB_HAS_FMA3) && defined(NWB_HAS_SSE2)
    return SIMDVectorDoubleDetail::FromPairs(
        _mm_fmsub_pd(SIMDVectorDoubleDetail::GetLow(v0), SIMDVectorDoubleDetail::GetLow(v1), SIMDVectorDoubleDetail::GetLow(v2)),
        _mm_fmsub_pd(SIMDVectorDoubleDetail::GetHigh(v0), SIMDVectorDoubleDetail::GetHigh(v1), SIMDVectorDoubleDetail::GetHigh(v2))
    );
#elif defined(NWB_HAS_FMA3) || defined(NWB_HAS_NEON)
    return __builtin_elementwise_fma(v0, v1, -v2);
#else
    return VectorDoubleSubtract(VectorDoubleMultiply(v0, v1), v2);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

