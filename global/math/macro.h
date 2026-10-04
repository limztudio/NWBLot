// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__SSE4_1__) || defined(__SSE4_2__) || defined(_M_AVX) || defined(_M_AVX2) || (defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && (_M_IX86_FP >= 2))))
#define GLB_HAS_SSE4 1
#endif

#if defined(__FMA__) || defined(_M_FMA)
#define GLB_HAS_FMA3 1
#endif

#if defined(__F16C__) || defined(_M_F16C)
#define GLB_HAS_F16C 1
#endif

#if defined(__AVX__) || defined(__AVX2__) || defined(_M_AVX) || defined(_M_AVX2)
#define GLB_HAS_AVX2 1
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64) || defined(_M_ARM64EC)
#define GLB_HAS_NEON 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(GLB_HAS_AVX2) && !defined(GLB_HAS_SSE4)
#define GLB_HAS_SSE4 1
#endif

#if defined(GLB_HAS_FMA3) && !defined(GLB_HAS_SSE4)
#define GLB_HAS_SSE4 1
#endif

#if defined(GLB_HAS_F16C) && !defined(GLB_HAS_SSE4)
#define GLB_HAS_SSE4 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(GLB_HAS_SSE4) && !defined(GLB_HAS_FMA3) && !defined(GLB_HAS_F16C) && !defined(GLB_HAS_AVX2) && !defined(GLB_HAS_NEON)
#define GLB_HAS_SCALAR 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(_MSC_VER) && !defined(_M_ARM) && !defined(_M_ARM64) && !defined(_M_HYBRID_X86_ARM64) && !defined(_M_ARM64EC) && (!_MANAGED) && (!_M_CEE) && (!defined(_M_IX86_FP) || (_M_IX86_FP > 1)) && !defined(GLB_NO_INTRINSICS) && !defined(GLB_NO_VECTORCALL)
#define SIMDCALL __vectorcall
#elif defined(__GNUC__)
#define SIMDCALL
#else
#define SIMDCALL __fastcall
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(GLB_HAS_SSE4)
#include <xmmintrin.h>
#include <emmintrin.h>
#include <smmintrin.h>
#endif

#if defined(GLB_HAS_FMA3) || defined(GLB_HAS_F16C) || defined(GLB_HAS_AVX2)
#include <immintrin.h>
#endif

#if defined(GLB_HAS_NEON)
#include <arm_neon.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

