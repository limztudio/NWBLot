// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__SSE2__) || (defined(_M_X64) && !defined(_M_ARM64EC))
#define FOUNDATION_HAS_SSE2 1
#endif

#if defined(__SSE4_1__) || defined(__SSE4_2__)
#define FOUNDATION_HAS_SSE4 1
#endif

#if defined(__FMA__) || defined(_M_FMA)
#define FOUNDATION_HAS_FMA3 1
#endif

#if defined(__F16C__) || defined(_M_F16C)
#define FOUNDATION_HAS_F16C 1
#endif

#if defined(__AVX__) || defined(__AVX2__) || defined(_M_AVX) || defined(_M_AVX2)
#define FOUNDATION_HAS_AVX 1
#endif

#if defined(__AVX2__) || defined(_M_AVX2)
#define FOUNDATION_HAS_AVX2 1
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64) || defined(_M_ARM64EC)
#define FOUNDATION_HAS_NEON 1
#endif

#if defined(FOUNDATION_HAS_NEON) && (defined(__aarch64__) || defined(_M_ARM64) || defined(_M_ARM64EC))
#define FOUNDATION_HAS_NEON64 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_HAS_AVX2) && !defined(FOUNDATION_HAS_AVX)
#define FOUNDATION_HAS_AVX 1
#endif

#if defined(FOUNDATION_HAS_AVX) && !defined(FOUNDATION_HAS_SSE4)
#define FOUNDATION_HAS_SSE4 1
#endif

#if defined(FOUNDATION_HAS_FMA3) && !defined(FOUNDATION_HAS_SSE4)
#define FOUNDATION_HAS_SSE4 1
#endif

#if defined(FOUNDATION_HAS_F16C) && !defined(FOUNDATION_HAS_SSE4)
#define FOUNDATION_HAS_SSE4 1
#endif

#if defined(FOUNDATION_HAS_SSE4) && !defined(FOUNDATION_HAS_SSE2)
#define FOUNDATION_HAS_SSE2 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_HAS_SSE2)
#include <xmmintrin.h>
#include <emmintrin.h>
#endif

#if defined(FOUNDATION_HAS_SSE4)
#include <smmintrin.h>
#endif

#if defined(FOUNDATION_HAS_FMA3) || defined(FOUNDATION_HAS_F16C) || defined(FOUNDATION_HAS_AVX) || defined(FOUNDATION_HAS_AVX2)
#include <immintrin.h>
#endif

#if defined(FOUNDATION_HAS_NEON)
#include <arm_neon.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(FOUNDATION_HAS_SSE2) && !defined(FOUNDATION_HAS_NEON)
#define FOUNDATION_HAS_SCALAR 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(_MSC_VER) && !defined(_M_ARM64) && !defined(_M_ARM64EC)
#define FOUNDATION_SIMD_CALL __vectorcall
#else
#define FOUNDATION_SIMD_CALL
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define FOUNDATION_SIMD_VECTOR_CONST_CONVERSIONS \
    FOUNDATION_INLINE operator SIMDVectorSingle()const noexcept{ return v; } \
    FOUNDATION_INLINE operator int32x4_t()const noexcept{ return vreinterpretq_s32_f32(v); } \
    FOUNDATION_INLINE operator uint32x4_t()const noexcept{ return vreinterpretq_u32_f32(v); }

#define FOUNDATION_SIMD_VECTOR_CONST_X86_CONVERSIONS \
    FOUNDATION_INLINE operator SIMDVectorSingle()const noexcept{ return v; } \
    FOUNDATION_INLINE operator __m128i()const noexcept{ return _mm_castps_si128(v); } \
    FOUNDATION_INLINE operator __m128d()const noexcept{ return _mm_castps_pd(v); }

#define FOUNDATION_SIMD_VECTOR_CONST_SCALAR_CONVERSION \
    FOUNDATION_INLINE operator SIMDVectorSingle()const noexcept{ return v; }

#if defined(FOUNDATION_HAS_NEON)
#define FOUNDATION_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS FOUNDATION_SIMD_VECTOR_CONST_CONVERSIONS
#elif !defined(FOUNDATION_HAS_SCALAR)
#define FOUNDATION_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS FOUNDATION_SIMD_VECTOR_CONST_X86_CONVERSIONS
#else
#define FOUNDATION_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS FOUNDATION_SIMD_VECTOR_CONST_SCALAR_CONVERSION
#endif

#define FOUNDATION_SIMD_VECTOR_DOUBLE_CONST_CONVERSION \
    FOUNDATION_INLINE operator SIMDVectorDouble()const noexcept{ return v; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__GNUC__)
#define FOUNDATION_GLOBAL_CONST extern const __attribute__((weak))
#else
#define FOUNDATION_GLOBAL_CONST extern const __declspec(selectany)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

