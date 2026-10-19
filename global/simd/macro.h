// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__SSE2__) || (defined(_M_X64) && !defined(_M_ARM64EC))
#define NWB_HAS_SSE2 1
#endif

#if defined(__SSE4_1__) || defined(__SSE4_2__)
#define NWB_HAS_SSE4 1
#endif

#if defined(__FMA__) || defined(_M_FMA)
#define NWB_HAS_FMA3 1
#endif

#if defined(__F16C__) || defined(_M_F16C)
#define NWB_HAS_F16C 1
#endif

#if defined(__AVX__) || defined(__AVX2__) || defined(_M_AVX) || defined(_M_AVX2)
#define NWB_HAS_AVX 1
#endif

#if defined(__AVX2__) || defined(_M_AVX2)
#define NWB_HAS_AVX2 1
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64) || defined(_M_ARM64EC)
#define NWB_HAS_NEON 1
#endif

#if defined(NWB_HAS_NEON) && (defined(__aarch64__) || defined(_M_ARM64) || defined(_M_ARM64EC))
#define NWB_HAS_NEON64 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_AVX2) && !defined(NWB_HAS_AVX)
#define NWB_HAS_AVX 1
#endif

#if defined(NWB_HAS_AVX) && !defined(NWB_HAS_SSE4)
#define NWB_HAS_SSE4 1
#endif

#if defined(NWB_HAS_FMA3) && !defined(NWB_HAS_SSE4)
#define NWB_HAS_SSE4 1
#endif

#if defined(NWB_HAS_F16C) && !defined(NWB_HAS_SSE4)
#define NWB_HAS_SSE4 1
#endif

#if defined(NWB_HAS_SSE4) && !defined(NWB_HAS_SSE2)
#define NWB_HAS_SSE2 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_SSE2)
#include <xmmintrin.h>
#include <emmintrin.h>
#endif

#if defined(NWB_HAS_SSE4)
#include <smmintrin.h>
#endif

#if defined(NWB_HAS_FMA3) || defined(NWB_HAS_F16C) || defined(NWB_HAS_AVX) || defined(NWB_HAS_AVX2)
#include <immintrin.h>
#endif

#if defined(NWB_HAS_NEON)
#include <arm_neon.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(NWB_HAS_SSE2) && !defined(NWB_HAS_NEON)
#define NWB_HAS_SCALAR 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(_MSC_VER) && !defined(_M_ARM) && !defined(_M_ARM64) && !defined(_M_ARM64EC) && (!defined(_M_IX86_FP) || (_M_IX86_FP > 1))
#define NWB_SIMD_CALL __vectorcall
#elif defined(__GNUC__)
#define NWB_SIMD_CALL
#else
#define NWB_SIMD_CALL __fastcall
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SIMD_VECTOR_CONST_CONVERSIONS \
    NWB_INLINE operator SIMDVectorSingle()const noexcept{ return v; } \
    NWB_INLINE operator int32x4_t()const noexcept{ return vreinterpretq_s32_f32(v); } \
    NWB_INLINE operator uint32x4_t()const noexcept{ return vreinterpretq_u32_f32(v); }

#define NWB_SIMD_VECTOR_CONST_X86_CONVERSIONS \
    NWB_INLINE operator SIMDVectorSingle()const noexcept{ return v; } \
    NWB_INLINE operator __m128i()const noexcept{ return _mm_castps_si128(v); } \
    NWB_INLINE operator __m128d()const noexcept{ return _mm_castps_pd(v); }

#define NWB_SIMD_VECTOR_CONST_SCALAR_CONVERSION \
    NWB_INLINE operator SIMDVectorSingle()const noexcept{ return v; }

#if defined(NWB_HAS_NEON)
#define NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS NWB_SIMD_VECTOR_CONST_CONVERSIONS
#elif !defined(NWB_HAS_SCALAR)
#define NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS NWB_SIMD_VECTOR_CONST_X86_CONVERSIONS
#else
#define NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS NWB_SIMD_VECTOR_CONST_SCALAR_CONVERSION
#endif

#define NWB_SIMD_VECTOR_DOUBLE_CONST_CONVERSION \
    NWB_INLINE operator SIMDVectorDouble()const noexcept{ return v; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__GNUC__) && !defined(__MINGW32__)
#define NWB_GLOBAL_CONST extern const __attribute__((weak))
#else
#define NWB_GLOBAL_CONST extern const __declspec(selectany)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

