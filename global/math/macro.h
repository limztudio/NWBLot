// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__SSE4_1__) || defined(__SSE4_2__) || defined(_M_AVX) || defined(_M_AVX2) || (defined(_MSC_VER) && !defined(__clang__) && (defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86_FP) && (_M_IX86_FP >= 2))))
#define NWB_HAS_SSE4 1
#endif

#if defined(__FMA__) || defined(__F16C__) || defined(_M_FMA) || defined(_M_F16C)
#define NWB_HAS_FMA3 1
#endif

#if defined(__AVX__) || defined(__AVX2__) || defined(_M_AVX) || defined(_M_AVX2)
#define NWB_HAS_AVX2 1
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64) || defined(_M_ARM64EC)
#define NWB_HAS_NEON 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_AVX2) && !defined(NWB_HAS_SSE4)
#define NWB_HAS_SSE4 1
#endif

#if defined(NWB_HAS_FMA3) && !defined(NWB_HAS_SSE4)
#define NWB_HAS_SSE4 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(NWB_HAS_SSE4) && !defined(NWB_HAS_FMA3) && !defined(NWB_HAS_AVX2) && !defined(NWB_HAS_NEON)
#define NWB_HAS_SCALAR 1
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(_MSC_VER) && !defined(_M_ARM) && !defined(_M_ARM64) && !defined(_M_HYBRID_X86_ARM64) && !defined(_M_ARM64EC) && (!_MANAGED) && (!_M_CEE) && (!defined(_M_IX86_FP) || (_M_IX86_FP > 1)) && !defined(_XM_NO_INTRINSICS_) && !defined(_XM_VECTORCALL_)
#define SIMDCALL __vectorcall
#elif defined(__GNUC__)
#define SIMDCALL
#else
#define SIMDCALL __fastcall
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_SSE4)
#include <xmmintrin.h>
#include <emmintrin.h>
#include <smmintrin.h>
#endif

#if defined(NWB_HAS_FMA3) || defined(NWB_HAS_AVX2)
#include <immintrin.h>
#endif

#if defined(NWB_HAS_NEON)
#if defined(_MSC_VER) && (defined(_M_ARM64) || defined(_M_ARM64EC))
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

