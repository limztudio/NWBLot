// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef __cplusplus
#error Source Math requires C++
#endif

#define SOURCE_MATH_VERSION 320

#if defined(_MSC_VER) && (_MSC_VER < 1910)
#error Source Math requires Visual C++ 2017 or later.
#endif

#if defined(_MSC_VER) \
    && !defined(_M_ARM) \
    && !defined(_M_ARM64) \
    && !defined(_M_HYBRID_X86_ARM64) \
    && !defined(_M_ARM64EC) \
    && (!_MANAGED) \
    && (!_M_CEE) \
    && (!defined(_M_IX86_FP) || (_M_IX86_FP > 1)) \
    && !defined(_MATH_NO_INTRINSICS_) \
    && !defined(_MATH_VECTORCALL_)
#define _MATH_VECTORCALL_ 1
#endif

#if _MATH_VECTORCALL_
#define MathCallConv __vectorcall
#elif defined(__GNUC__)
#define MathCallConv
#else
#define MathCallConv __fastcall
#endif

#if !defined(_MATH_AVX2_INTRINSICS_) && defined(__AVX2__) && !defined(_MATH_NO_INTRINSICS_)
#define _MATH_AVX2_INTRINSICS_
#endif

#if !defined(_MATH_FMA3_INTRINSICS_) && defined(_MATH_AVX2_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
#define _MATH_FMA3_INTRINSICS_
#endif

#if !defined(_MATH_F16C_INTRINSICS_) && defined(_MATH_AVX2_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
#define _MATH_F16C_INTRINSICS_
#endif

#if !defined(_MATH_F16C_INTRINSICS_) && defined(__F16C__) && !defined(_MATH_NO_INTRINSICS_)
#define _MATH_F16C_INTRINSICS_
#endif

#if defined(_MATH_FMA3_INTRINSICS_) && !defined(_MATH_AVX_INTRINSICS_)
#define _MATH_AVX_INTRINSICS_
#endif

#if defined(_MATH_F16C_INTRINSICS_) && !defined(_MATH_AVX_INTRINSICS_)
#define _MATH_AVX_INTRINSICS_
#endif

#if !defined(_MATH_AVX_INTRINSICS_) && defined(__AVX__) && !defined(_MATH_NO_INTRINSICS_)
#define _MATH_AVX_INTRINSICS_
#endif

#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_SSE4_INTRINSICS_)
#define _MATH_SSE4_INTRINSICS_
#endif

#if defined(_MATH_SSE4_INTRINSICS_) && !defined(_MATH_SSE3_INTRINSICS_)
#define _MATH_SSE3_INTRINSICS_
#endif

#if defined(_MATH_SSE3_INTRINSICS_) && !defined(_MATH_SSE_INTRINSICS_)
#define _MATH_SSE_INTRINSICS_
#endif

#if !defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
#if (defined(_M_IX86) || defined(_M_X64) || __i386__ || __x86_64__ || __powerpc64__) \
    && !defined(_M_HYBRID_X86_ARM64) \
    && !defined(_M_ARM64EC)
#define _MATH_SSE_INTRINSICS_
#elif defined(_M_ARM) || defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __arm__ || __aarch64__
#define _MATH_ARM_NEON_INTRINSICS_
#elif !defined(_MATH_NO_INTRINSICS_)
#error Source Math does not support this target
#endif
#endif // !_MATH_ARM_NEON_INTRINSICS_ && !_MATH_SSE_INTRINSICS_ && !_MATH_NO_INTRINSICS_

#if defined(_MATH_SSE_INTRINSICS_) \
    && defined(_MSC_VER) \
    && (_MSC_VER >= 1920) \
    && !defined(__clang__) \
    && !defined(_MATH_SVML_INTRINSICS_) \
    && !defined(_MATH_DISABLE_INTEL_SVML_)
#define _MATH_SVML_INTRINSICS_
#endif

#if !defined(_MATH_NO_VECTOR_OVERLOADS_) && (defined(__clang__) || defined(__GNUC__)) && !defined(_MATH_NO_INTRINSICS_)
#define _MATH_NO_VECTOR_OVERLOADS_
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4514 4820)
// C4514/4820: Off by default noise
#endif
#include <math.h>
#include <float.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifndef _MATH_NO_INTRINSICS_

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4987)
// C4987: Off by default noise
#endif
#if defined(_MSC_VER) || defined(__MINGW32__)
#include <intrin.h>
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#if(defined(__clang__) || defined(__GNUC__)) && (__x86_64__ || __i386__) && !defined(__MINGW32__)
#include <cpuid.h>
#endif

#ifdef _MATH_SSE_INTRINSICS_
#include <xmmintrin.h>
#include <emmintrin.h>

#ifdef _MATH_SSE3_INTRINSICS_
#include <pmmintrin.h>
#endif

#ifdef _MATH_SSE4_INTRINSICS_
#include <smmintrin.h>
#endif

#ifdef _MATH_AVX_INTRINSICS_
#include <immintrin.h>
#endif

#elif defined(_MATH_ARM_NEON_INTRINSICS_)
#if defined(_MSC_VER) && !defined(__clang__) && (defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC))
#include <arm64_neon.h>
#else
#include <arm_neon.h>
#endif
#endif
#endif // !_MATH_NO_INTRINSICS_

#ifndef __has_include
#define __has_include(x) 0
#endif

#if __has_include(<sal.h>)
#include <sal.h>
#elif __has_include("sal.h")
#include "sal.h"
#else
#ifndef _Use_decl_annotations_
#define _Use_decl_annotations_
#endif
#ifndef _Analysis_assume_
#define _Analysis_assume_(expr) ((void)0)
#endif
#ifndef _In_
#define _In_
#endif
#ifndef _In_reads_
#define _In_reads_(size)
#endif
#ifndef _In_reads_bytes_
#define _In_reads_bytes_(size)
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif
#ifndef _Out_writes_
#define _Out_writes_(size)
#endif
#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(size)
#endif
#ifndef _Success_
#define _Success_(expr)
#endif
#endif

#include <assert.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4005 4668)
// C4005/4668: Old header issue
#endif
#include <stdint.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#if(__cplusplus >= 201703L)
#define MATH_ALIGNED_DATA(x) alignas(x)
#define MATH_ALIGNED_STRUCT(x) struct alignas(x)
#elif defined(__GNUC__)
#define MATH_ALIGNED_DATA(x) __attribute__ ((aligned(x)))
#define MATH_ALIGNED_STRUCT(x) struct __attribute__ ((aligned(x)))
#else
#define MATH_ALIGNED_DATA(x) __declspec(align(x))
#define MATH_ALIGNED_STRUCT(x) __declspec(align(x)) struct
#endif

#if(__cplusplus >= 202002L)
#include <compare>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)

#if defined(_MATH_NO_MOVNT_)
#define MATH_STREAM_PS( p, a ) _mm_store_ps((p), (a))
#define MATH256_STREAM_PS( p, a ) _mm256_store_ps((p), (a))
#define MATH_SFENCE()
#else
#define MATH_STREAM_PS( p, a ) _mm_stream_ps((p), (a))
#define MATH256_STREAM_PS( p, a ) _mm256_stream_ps((p), (a))
#define MATH_SFENCE() _mm_sfence()
#endif

#if defined(_MATH_FMA3_INTRINSICS_)
#define MATH_FMADD_PS( a, b, c ) _mm_fmadd_ps((a), (b), (c))
#define MATH_FNMADD_PS( a, b, c ) _mm_fnmadd_ps((a), (b), (c))
#else
#define MATH_FMADD_PS( a, b, c ) _mm_add_ps(_mm_mul_ps((a), (b)), (c))
#define MATH_FNMADD_PS( a, b, c ) _mm_sub_ps((c), _mm_mul_ps((a), (b)))
#endif

#if defined(_MATH_AVX_INTRINSICS_) && defined(_MATH_FAVOR_INTEL_)
#define MATH_PERMUTE_PS( v, c ) _mm_permute_ps((v), c )
#else
#define MATH_PERMUTE_PS( v, c ) _mm_shuffle_ps((v), (v), c )
#endif

#if(defined(__GNUC__) && !defined(__clang__) && (__GNUC__ < 11)) || defined(__powerpc64__)
#define MATH_LOADU_SI16( p ) _mm_cvtsi32_si128(*reinterpret_cast<unsigned short const*>(p))
#else
#define MATH_LOADU_SI16( p ) _mm_loadu_si16(p)
#endif

#endif // _MATH_SSE_INTRINSICS_ && !_MATH_NO_INTRINSICS_

#if defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)

#if defined(__clang__) || defined(__GNUC__)
#define MATH_PREFETCH( a ) __builtin_prefetch(a)
#elif defined(_MSC_VER)
#define MATH_PREFETCH( a ) __prefetch(a)
#else
#define MATH_PREFETCH( a )
#endif

#endif // _MATH_ARM_NEON_INTRINSICS_ && !_MATH_NO_INTRINSICS_

namespace SourceMathInternal{
#if defined(__XNAMATH_H__) && defined(MATH_PI)
#undef MATH_PI
#undef MATH_2PI
#undef MATH_1DIVPI
#undef MATH_1DIV2PI
#undef MATH_PIDIV2
#undef MATH_PIDIV4
#undef MATH_SELECT_0
#undef MATH_SELECT_1
#undef MATH_PERMUTE_0X
#undef MATH_PERMUTE_0Y
#undef MATH_PERMUTE_0Z
#undef MATH_PERMUTE_0W
#undef MATH_PERMUTE_1X
#undef MATH_PERMUTE_1Y
#undef MATH_PERMUTE_1Z
#undef MATH_PERMUTE_1W
#undef MATH_CRMASK_CR6
#undef MATH_CRMASK_CR6TRUE
#undef MATH_CRMASK_CR6FALSE
#undef MATH_CRMASK_CR6BOUNDS
#undef MATH_CACHE_LINE_SIZE
#endif

    constexpr float MATH_PI = 3.141592654f;
    constexpr float MATH_2PI = 6.283185307f;
    constexpr float MATH_1DIVPI = 0.318309886f;
    constexpr float MATH_1DIV2PI = 0.159154943f;
    constexpr float MATH_PIDIV2 = 1.570796327f;
    constexpr float MATH_PIDIV4 = 0.785398163f;

    constexpr uint32_t MATH_SELECT_0 = 0x00000000;
    constexpr uint32_t MATH_SELECT_1 = 0xFFFFFFFF;

    constexpr uint32_t MATH_PERMUTE_0X = 0;
    constexpr uint32_t MATH_PERMUTE_0Y = 1;
    constexpr uint32_t MATH_PERMUTE_0Z = 2;
    constexpr uint32_t MATH_PERMUTE_0W = 3;
    constexpr uint32_t MATH_PERMUTE_1X = 4;
    constexpr uint32_t MATH_PERMUTE_1Y = 5;
    constexpr uint32_t MATH_PERMUTE_1Z = 6;
    constexpr uint32_t MATH_PERMUTE_1W = 7;

    constexpr uint32_t MATH_SWIZZLE_X = 0;
    constexpr uint32_t MATH_SWIZZLE_Y = 1;
    constexpr uint32_t MATH_SWIZZLE_Z = 2;
    constexpr uint32_t MATH_SWIZZLE_W = 3;

    constexpr uint32_t MATH_CRMASK_CR6 = 0x000000F0;
    constexpr uint32_t MATH_CRMASK_CR6TRUE = 0x00000080;
    constexpr uint32_t MATH_CRMASK_CR6FALSE = 0x00000020;
    constexpr uint32_t MATH_CRMASK_CR6BOUNDS = MATH_CRMASK_CR6FALSE;

#if defined(_M_ARM) || defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __arm__ || __aarch64__
    constexpr size_t MATH_CACHE_LINE_SIZE = 128;
#else
    constexpr size_t MATH_CACHE_LINE_SIZE = 64;
#endif

#if defined(__XNAMATH_H__) && defined(ComparisonAllTrue)
#undef ComparisonAllTrue
#undef ComparisonAnyTrue
#undef ComparisonAllFalse
#undef ComparisonAnyFalse
#undef ComparisonMixed
#undef ComparisonAllInBounds
#undef ComparisonAnyOutOfBounds
#endif

    constexpr float ConvertToRadians(float fDegrees)noexcept { return fDegrees * (MATH_PI / 180.0f); }
    constexpr float ConvertToDegrees(float fRadians)noexcept { return fRadians * (180.0f / MATH_PI); }

    // Condition register evaluation proceeding a recording (R) comparison

    constexpr bool ComparisonAllTrue(uint32_t CR)noexcept { return (CR & MATH_CRMASK_CR6TRUE) == MATH_CRMASK_CR6TRUE; }
    constexpr bool ComparisonAnyTrue(uint32_t CR)noexcept { return (CR & MATH_CRMASK_CR6FALSE) != MATH_CRMASK_CR6FALSE; }
    constexpr bool ComparisonAllFalse(uint32_t CR)noexcept { return (CR & MATH_CRMASK_CR6FALSE) == MATH_CRMASK_CR6FALSE; }
    constexpr bool ComparisonAnyFalse(uint32_t CR)noexcept { return (CR & MATH_CRMASK_CR6TRUE) != MATH_CRMASK_CR6TRUE; }
    constexpr bool ComparisonMixed(uint32_t CR)noexcept { return (CR & MATH_CRMASK_CR6) == 0; }
    constexpr bool ComparisonAllInBounds(uint32_t CR)noexcept { return (CR & MATH_CRMASK_CR6BOUNDS) == MATH_CRMASK_CR6BOUNDS; }
    constexpr bool ComparisonAnyOutOfBounds(uint32_t CR)noexcept{
        return (CR & MATH_CRMASK_CR6BOUNDS) != MATH_CRMASK_CR6BOUNDS;
    }

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4068 4201 4365 4324 4820)
    // C4068: ignore unknown pragmas
    // C4201: nonstandard extension used : nameless struct/union
    // C4365: Off by default noise
    // C4324/4820: padding warnings
#endif

#ifdef _PREFAST_
#pragma prefast(push)
#pragma prefast(disable : 25000, "VectorArg is 16 bytes")
#endif

#if defined(_MATH_NO_INTRINSICS_)
    struct __vector4{
        union{
            float       vector4_f32[4];
            uint32_t    vector4_u32[4];
        };
    };
#endif // _MATH_NO_INTRINSICS_

    // Vector intrinsic: Four 32 bit floating point components aligned on a 16 byte
    // boundary and mapped to hardware vector registers
#if defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    using Vector = __m128;
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    using Vector = float32x4_t;
#else
    using Vector = __vector4;
#endif

    // Fix-up for 1st-3rd Vector parameters: pass in-register for x86, ARM,
    // ARM64, and vector call; by reference otherwise.
#if (defined(_M_IX86) || defined(_M_ARM) || defined(_M_ARM64) || _MATH_VECTORCALL_ || __i386__ || __arm__ || __aarch64__) \
    && !defined(_MATH_NO_INTRINSICS_)
    typedef const Vector VectorArg;
#else
    typedef const Vector& VectorArg;
#endif

    // Fix-up for 4th Vector parameter: pass in-register for ARM, ARM64, and
    // vector call; by reference otherwise.
#if (defined(_M_ARM) || defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) \
    || defined(_M_ARM64EC) || _MATH_VECTORCALL_ || __arm__ || __aarch64__) \
    && !defined(_MATH_NO_INTRINSICS_)
    typedef const Vector VectorArg2;
#else
    typedef const Vector& VectorArg2;
#endif

    // Fix-up for 5th and 6th Vector parameters: pass in-register for ARM64 and
    // vector call; by reference otherwise.
#if (defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || _MATH_VECTORCALL_ || __aarch64__) \
    && !defined(_MATH_NO_INTRINSICS_)
    typedef const Vector VectorArg3;
#else
    typedef const Vector& VectorArg3;
#endif

    // Fix-up for(7th+) Vector parameters to pass by reference
    typedef const Vector& VectorConstArg;

    // Conversion types for constants
    MATH_ALIGNED_STRUCT(16) VectorF32{
        union{
            float f[4];
            Vector v;
        };

        inline operator Vector()const noexcept { return v; }
        inline operator const float* ()const noexcept { return f; }
    #ifdef _MATH_NO_INTRINSICS_
    #elif defined(_MATH_SSE_INTRINSICS_)
        inline operator __m128i()const noexcept { return _mm_castps_si128(v); }
        inline operator __m128d()const noexcept { return _mm_castps_pd(v); }
    #elif defined(_MATH_ARM_NEON_INTRINSICS_) && (defined(__GNUC__) || defined(_ARM64_DISTINCT_NEON_TYPES))
        inline operator int32x4_t()const noexcept { return vreinterpretq_s32_f32(v); }
        inline operator uint32x4_t()const noexcept { return vreinterpretq_u32_f32(v); }
    #endif
    };

    MATH_ALIGNED_STRUCT(16) VectorI32{
        union{
            int32_t i[4];
            Vector v;
        };

        inline operator Vector()const noexcept { return v; }
    #ifdef _MATH_NO_INTRINSICS_
    #elif defined(_MATH_SSE_INTRINSICS_)
        inline operator __m128i()const noexcept { return _mm_castps_si128(v); }
        inline operator __m128d()const noexcept { return _mm_castps_pd(v); }
    #elif defined(_MATH_ARM_NEON_INTRINSICS_) && (defined(__GNUC__) || defined(_ARM64_DISTINCT_NEON_TYPES))
        inline operator int32x4_t()const noexcept { return vreinterpretq_s32_f32(v); }
        inline operator uint32x4_t()const noexcept { return vreinterpretq_u32_f32(v); }
    #endif
    };

    MATH_ALIGNED_STRUCT(16) VectorU8{
        union{
            uint8_t u[16];
            Vector v;
        };

        inline operator Vector()const noexcept { return v; }
    #ifdef _MATH_NO_INTRINSICS_
    #elif defined(_MATH_SSE_INTRINSICS_)
        inline operator __m128i()const noexcept { return _mm_castps_si128(v); }
        inline operator __m128d()const noexcept { return _mm_castps_pd(v); }
    #elif defined(_MATH_ARM_NEON_INTRINSICS_) && (defined(__GNUC__) || defined(_ARM64_DISTINCT_NEON_TYPES))
        inline operator int32x4_t()const noexcept { return vreinterpretq_s32_f32(v); }
        inline operator uint32x4_t()const noexcept { return vreinterpretq_u32_f32(v); }
    #endif
    };

    MATH_ALIGNED_STRUCT(16) VectorU32{
        union{
            uint32_t u[4];
            Vector v;
        };

        inline operator Vector()const noexcept { return v; }
    #ifdef _MATH_NO_INTRINSICS_
    #elif defined(_MATH_SSE_INTRINSICS_)
        inline operator __m128i()const noexcept { return _mm_castps_si128(v); }
        inline operator __m128d()const noexcept { return _mm_castps_pd(v); }
    #elif defined(_MATH_ARM_NEON_INTRINSICS_) && (defined(__GNUC__) || defined(_ARM64_DISTINCT_NEON_TYPES))
        inline operator int32x4_t()const noexcept { return vreinterpretq_s32_f32(v); }
        inline operator uint32x4_t()const noexcept { return vreinterpretq_u32_f32(v); }
    #endif
    };

    // Vector operators

#ifndef _MATH_NO_VECTOR_OVERLOADS_
    Vector    MathCallConv     operator+(VectorArg V)noexcept;
    Vector    MathCallConv     operator-(VectorArg V)noexcept;

    Vector&   MathCallConv     operator+=(Vector& V1, VectorArg V2)noexcept;
    Vector&   MathCallConv     operator-=(Vector& V1, VectorArg V2)noexcept;
    Vector&   MathCallConv     operator*=(Vector& V1, VectorArg V2)noexcept;
    Vector&   MathCallConv     operator/=(Vector& V1, VectorArg V2)noexcept;

    Vector& operator*=(Vector& V, float S)noexcept;
    Vector& operator/=(Vector& V, float S)noexcept;

    Vector    MathCallConv     operator+(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     operator-(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     operator*(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     operator/(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     operator*(VectorArg V, float S)noexcept;
    Vector    MathCallConv     operator*(float S, VectorArg V)noexcept;
    Vector    MathCallConv     operator/(VectorArg V, float S)noexcept;
#endif /* !_MATH_NO_VECTOR_OVERLOADS_ */

    // Matrix type: Sixteen 32 bit floating point components aligned on a
    // 16 byte boundary and mapped to four hardware vector registers.
    //
    // NWB stores the matrix basis/translation as columns in `r[0..3]`.
    // Scalar union members such as `_11` / `m[row][column]` exist for raw
    // storage interop and compact boundary-format access only.

    struct Matrix;

    // Fix-up for 1st Matrix parameter: pass in-register for ARM64 and vector
    // call; by reference otherwise.
#if (defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || _MATH_VECTORCALL_ || __aarch64__) \
    && !defined(_MATH_NO_INTRINSICS_)
    typedef const Matrix FXMMATRIX;
#else
    typedef const Matrix& FXMMATRIX;
#endif

    // Fix-up for(2nd+) Matrix parameters to pass by reference
    typedef const Matrix& CXMMATRIX;

#ifdef _MATH_NO_INTRINSICS_
    struct Matrix
    #else
    MATH_ALIGNED_STRUCT(16) Matrix
    #endif
    {
        // NWB stores matrices as columns in `r[0..3]`.
    #ifdef _MATH_NO_INTRINSICS_
        union{
            Vector r[4];
            struct{
                float _11, _12, _13, _14;
                float _21, _22, _23, _24;
                float _31, _32, _33, _34;
                float _41, _42, _43, _44;
            };
            float m[4][4];
        };
    #else
        Vector r[4];
    #endif

        Matrix() = default;

        Matrix(const Matrix&) = default;

    #if defined(_MSC_VER) && (_MSC_FULL_VER < 191426431)
        Matrix& operator=(const Matrix& M)noexcept { r[0] = M.r[0]; r[1] = M.r[1]; r[2] = M.r[2]; r[3] = M.r[3]; return *this; }
    #else
        Matrix& operator=(const Matrix&) = default;

        Matrix(Matrix&&) = default;
        Matrix& operator=(Matrix&&) = default;
    #endif

        // Constructor accepts NWB's four internal matrix columns in order.
        constexpr Matrix(VectorArg Column0, VectorArg Column1, VectorArg Column2, VectorConstArg Column3)noexcept
            : r{ Column0, Column1, Column2, Column3 }{}
        Matrix(float m00, float m01, float m02, float m03,
            float m10, float m11, float m12, float m13,
            float m20, float m21, float m22, float m23,
            float m30, float m31, float m32, float m33)noexcept;
        // Raw pointer constructor accepts four contiguous NWB columns:
        // `[c0 | c1 | c2 | c3]`.
        explicit Matrix(_In_reads_(16) const float* pArray)noexcept;

    #ifdef _MATH_NO_INTRINSICS_
        // Mathematical row/column accessor over NWB's internal column storage.
        float       operator()(size_t Row, size_t Column)const noexcept { return r[Column].vector4_f32[Row]; }
        float& operator()(size_t Row, size_t Column)noexcept { return r[Column].vector4_f32[Row]; }
    #endif

        Matrix    operator+()const noexcept { return *this; }
        Matrix    operator-()const noexcept;

        Matrix&   MathCallConv     operator+=(FXMMATRIX M)noexcept;
        Matrix&   MathCallConv     operator-=(FXMMATRIX M)noexcept;
        Matrix&   MathCallConv     operator*=(FXMMATRIX M)noexcept;
        Matrix&   operator*=(float S)noexcept;
        Matrix&   operator/=(float S)noexcept;

        Matrix    MathCallConv     operator+(FXMMATRIX M)const noexcept;
        Matrix    MathCallConv     operator-(FXMMATRIX M)const noexcept;
        Matrix    MathCallConv     operator*(FXMMATRIX M)const noexcept;
        Matrix    operator*(float S)const noexcept;
        Matrix    operator/(float S)const noexcept;

        friend Matrix     MathCallConv     operator*(float S, FXMMATRIX M)noexcept;
    };

    // 2D Vector; 32 bit floating point components
    struct Float2{
        float x;
        float y;

        Float2() = default;

        Float2(const Float2&) = default;
        Float2& operator=(const Float2&) = default;

        Float2(Float2&&) = default;
        Float2& operator=(Float2&&) = default;

        constexpr Float2(float _x, float _y)noexcept
            : x(_x), y(_y){}
        explicit Float2(_In_reads_(2) const float* pArray)noexcept
            : x(pArray[0]), y(pArray[1]){}

    #if(__cplusplus >= 202002L)
        bool operator==(const Float2&)const = default;
        auto operator<=>(const Float2&)const = default;
    #endif
    };

    // 2D Vector; 32 bit floating point components aligned on a 16 byte boundary
    MATH_ALIGNED_STRUCT(16) AlignedFloat2 : public Float2{
        using Float2::Float2;
    };

    // 2D Vector; 32 bit signed integer components
    struct Int2{
        int32_t x;
        int32_t y;

        Int2() = default;

        Int2(const Int2&) = default;
        Int2& operator=(const Int2&) = default;

        Int2(Int2&&) = default;
        Int2& operator=(Int2&&) = default;

        constexpr Int2(int32_t _x, int32_t _y)noexcept
            : x(_x), y(_y){}
        explicit Int2(_In_reads_(2) const int32_t* pArray)noexcept
            : x(pArray[0]), y(pArray[1]){}

    #if(__cplusplus >= 202002L)
        bool operator==(const Int2&)const = default;
        auto operator<=>(const Int2&)const = default;
    #endif
    };

    // 2D Vector; 32 bit unsigned integer components
    struct UInt2{
        uint32_t x;
        uint32_t y;

        UInt2() = default;

        UInt2(const UInt2&) = default;
        UInt2& operator=(const UInt2&) = default;

        UInt2(UInt2&&) = default;
        UInt2& operator=(UInt2&&) = default;

        constexpr UInt2(uint32_t _x, uint32_t _y)noexcept
            : x(_x), y(_y){}
        explicit UInt2(_In_reads_(2) const uint32_t* pArray)noexcept
            : x(pArray[0]), y(pArray[1]){}

    #if(__cplusplus >= 202002L)
        bool operator==(const UInt2&)const = default;
        auto operator<=>(const UInt2&)const = default;
    #endif
    };

    // 3D Vector; 32 bit floating point components
    struct Float3{
        float x;
        float y;
        float z;

        Float3() = default;

        Float3(const Float3&) = default;
        Float3& operator=(const Float3&) = default;

        Float3(Float3&&) = default;
        Float3& operator=(Float3&&) = default;

        constexpr Float3(float _x, float _y, float _z)noexcept
            : x(_x), y(_y), z(_z){}
        explicit Float3(_In_reads_(3) const float* pArray)noexcept
            : x(pArray[0]), y(pArray[1]), z(pArray[2]){}
    };

    // 3D Vector; 32 bit floating point components aligned on a 16 byte boundary
    MATH_ALIGNED_STRUCT(16) AlignedFloat3 : public Float3{
        using Float3::Float3;
    };

    // 3D Vector; 32 bit signed integer components
    struct Int3{
        int32_t x;
        int32_t y;
        int32_t z;

        Int3() = default;

        Int3(const Int3&) = default;
        Int3& operator=(const Int3&) = default;

        Int3(Int3&&) = default;
        Int3& operator=(Int3&&) = default;

        constexpr Int3(int32_t _x, int32_t _y, int32_t _z)noexcept
            : x(_x), y(_y), z(_z){}
        explicit Int3(_In_reads_(3) const int32_t* pArray)noexcept
            : x(pArray[0]), y(pArray[1]), z(pArray[2]){}

    #if(__cplusplus >= 202002L)
        bool operator==(const Int3&)const = default;
        auto operator<=>(const Int3&)const = default;
    #endif
    };

    // 3D Vector; 32 bit unsigned integer components
    struct UInt3{
        uint32_t x;
        uint32_t y;
        uint32_t z;

        UInt3() = default;

        UInt3(const UInt3&) = default;
        UInt3& operator=(const UInt3&) = default;

        UInt3(UInt3&&) = default;
        UInt3& operator=(UInt3&&) = default;

        constexpr UInt3(uint32_t _x, uint32_t _y, uint32_t _z)noexcept
            : x(_x), y(_y), z(_z){}
        explicit UInt3(_In_reads_(3) const uint32_t* pArray)noexcept
            : x(pArray[0]), y(pArray[1]), z(pArray[2]){}

    #if(__cplusplus >= 202002L)
        bool operator==(const UInt3&)const = default;
        auto operator<=>(const UInt3&)const = default;
    #endif
    };

    // 4D Vector; 32 bit floating point components
    struct Float4{
        float x;
        float y;
        float z;
        float w;

        Float4() = default;

        Float4(const Float4&) = default;
        Float4& operator=(const Float4&) = default;

        Float4(Float4&&) = default;
        Float4& operator=(Float4&&) = default;

        constexpr Float4(float _x, float _y, float _z, float _w)noexcept
            : x(_x), y(_y), z(_z), w(_w){}
        explicit Float4(_In_reads_(4) const float* pArray)noexcept
            : x(pArray[0]), y(pArray[1]), z(pArray[2]), w(pArray[3]){}

    #if(__cplusplus >= 202002L)
        bool operator==(const Float4&)const = default;
        auto operator<=>(const Float4&)const = default;
    #endif
    };

    // 4D Vector; 32 bit floating point components aligned on a 16 byte boundary
    MATH_ALIGNED_STRUCT(16) AlignedFloat4 : public Float4{
        using Float4::Float4;
    };

    // 4D Vector; 32 bit signed integer components
    struct Int4{
        int32_t x;
        int32_t y;
        int32_t z;
        int32_t w;

        Int4() = default;

        Int4(const Int4&) = default;
        Int4& operator=(const Int4&) = default;

        Int4(Int4&&) = default;
        Int4& operator=(Int4&&) = default;

        constexpr Int4(int32_t _x, int32_t _y, int32_t _z, int32_t _w)noexcept
            : x(_x), y(_y), z(_z), w(_w){}
        explicit Int4(_In_reads_(4) const int32_t* pArray)noexcept
            : x(pArray[0]), y(pArray[1]), z(pArray[2]), w(pArray[3]){}

    #if(__cplusplus >= 202002L)
        bool operator==(const Int4&)const = default;
        auto operator<=>(const Int4&)const = default;
    #endif
    };

    // 4D Vector; 32 bit unsigned integer components
    struct UInt4{
        uint32_t x;
        uint32_t y;
        uint32_t z;
        uint32_t w;

        UInt4() = default;

        UInt4(const UInt4&) = default;
        UInt4& operator=(const UInt4&) = default;

        UInt4(UInt4&&) = default;
        UInt4& operator=(UInt4&&) = default;

        constexpr UInt4(uint32_t _x, uint32_t _y, uint32_t _z, uint32_t _w)noexcept
            : x(_x), y(_y), z(_z), w(_w){}
        explicit UInt4(_In_reads_(4) const uint32_t* pArray)noexcept
            : x(pArray[0]), y(pArray[1]), z(pArray[2]), w(pArray[3]){}

    #if(__cplusplus >= 202002L)
        bool operator==(const UInt4&)const = default;
        auto operator<=>(const UInt4&)const = default;
    #endif
    };

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif

    // 3x4 affine matrix stored as the top three mathematical rows at the
    // memory boundary. This is the preferred compact format for NWB's
    // column-vector convention.
    struct Float3x4{
        union{
            struct{
                float _11, _12, _13, _14;
                float _21, _22, _23, _24;
                float _31, _32, _33, _34;
            };
            float m[3][4];
            float f[12];
        };

        Float3x4() = default;

        Float3x4(const Float3x4&) = default;
        Float3x4& operator=(const Float3x4&) = default;

        Float3x4(Float3x4&&) = default;
        Float3x4& operator=(Float3x4&&) = default;

        constexpr Float3x4(float m00, float m01, float m02, float m03,
            float m10, float m11, float m12, float m13,
            float m20, float m21, float m22, float m23)noexcept
            : _11(m00), _12(m01), _13(m02), _14(m03),
              _21(m10), _22(m11), _23(m12), _24(m13),
              _31(m20), _32(m21), _33(m22), _34(m23){}
        explicit Float3x4(_In_reads_(12) const float* pArray)noexcept;

        float  operator()(size_t Row, size_t Column)const noexcept { return m[Row][Column]; }
        float& operator()(size_t Row, size_t Column)noexcept { return m[Row][Column]; }

    #if(__cplusplus >= 202002L)
        bool operator==(const Float3x4& rhs)const noexcept{
            for(size_t index = 0; index < 12; ++index){
                if(f[index] != rhs.f[index])
                    return false;
            }

            return true;
        }

        std::partial_ordering operator<=>(const Float3x4& rhs)const noexcept{
            for(size_t index = 0; index < 12; ++index){
                const std::partial_ordering comparison = f[index] <=> rhs.f[index];
                if(comparison != 0)
                    return comparison;
            }

            return std::partial_ordering::equivalent;
        }
    #endif
    };

    // 3x4 affine matrix stored as the top three mathematical rows at the
    // memory boundary, aligned on a 16 byte boundary.
    MATH_ALIGNED_STRUCT(16) AlignedFloat3x4 : public Float3x4{
        using Float3x4::Float3x4;
    };

#ifdef __clang__
#pragma clang diagnostic pop
#endif
#ifdef _PREFAST_
#pragma prefast(pop)
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Data conversion operations

    Vector    MathCallConv     ConvertVectorIntToFloat(VectorArg VInt, uint32_t DivExponent)noexcept;
    Vector    MathCallConv     ConvertVectorFloatToInt(VectorArg VFloat, uint32_t MulExponent)noexcept;
    Vector    MathCallConv     ConvertVectorUIntToFloat(VectorArg VUInt, uint32_t DivExponent)noexcept;
    Vector    MathCallConv     ConvertVectorFloatToUInt(VectorArg VFloat, uint32_t MulExponent)noexcept;

#if defined(__XNAMATH_H__) && defined(VectorSetBinaryConstant)
#undef VectorSetBinaryConstant
#undef VectorSplatConstant
#undef VectorSplatConstantInt
#endif

    Vector    MathCallConv     VectorSetBinaryConstant(uint32_t C0, uint32_t C1, uint32_t C2, uint32_t C3)noexcept;
    Vector    MathCallConv     VectorSplatConstant(int32_t IntConstant, uint32_t DivExponent)noexcept;
    Vector    MathCallConv     VectorSplatConstantInt(int32_t IntConstant)noexcept;

    // Load operations

    Vector    MathCallConv     LoadInt(_In_ const uint32_t* pSource)noexcept;
    Vector    MathCallConv     LoadFloat(_In_ const float* pSource)noexcept;

    Vector    MathCallConv     LoadInt2(_In_reads_(2) const uint32_t* pSource)noexcept;
    Vector    MathCallConv     LoadInt2A(_In_reads_(2) const uint32_t* PSource)noexcept;
    Vector    MathCallConv     LoadFloat2(_In_ const Float2* pSource)noexcept;
    Vector    MathCallConv     LoadFloat2A(_In_ const AlignedFloat2* pSource)noexcept;
    Vector    MathCallConv     LoadSInt2(_In_ const Int2* pSource)noexcept;
    Vector    MathCallConv     LoadUInt2(_In_ const UInt2* pSource)noexcept;

    Vector    MathCallConv     LoadInt3(_In_reads_(3) const uint32_t* pSource)noexcept;
    Vector    MathCallConv     LoadInt3A(_In_reads_(3) const uint32_t* pSource)noexcept;
    Vector    MathCallConv     LoadFloat3(_In_ const Float3* pSource)noexcept;
    Vector    MathCallConv     LoadFloat3A(_In_ const AlignedFloat3* pSource)noexcept;
    Vector    MathCallConv     LoadSInt3(_In_ const Int3* pSource)noexcept;
    Vector    MathCallConv     LoadUInt3(_In_ const UInt3* pSource)noexcept;

    Vector    MathCallConv     LoadInt4(_In_reads_(4) const uint32_t* pSource)noexcept;
    Vector    MathCallConv     LoadInt4A(_In_reads_(4) const uint32_t* pSource)noexcept;
    Vector    MathCallConv     LoadFloat4(_In_ const Float4* pSource)noexcept;
    Vector    MathCallConv     LoadFloat4A(_In_ const AlignedFloat4* pSource)noexcept;
    Vector    MathCallConv     LoadSInt4(_In_ const Int4* pSource)noexcept;
    Vector    MathCallConv     LoadUInt4(_In_ const UInt4* pSource)noexcept;

    Matrix    MathCallConv     LoadFloat3x4(_In_ const Float3x4* pSource)noexcept;
    Matrix    MathCallConv     LoadFloat3x4A(_In_ const AlignedFloat3x4* pSource)noexcept;

    // Store operations

    void        MathCallConv     StoreInt(_Out_ uint32_t* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreFloat(_Out_ float* pDestination, _In_ VectorArg V)noexcept;

    void        MathCallConv     StoreInt2(_Out_writes_(2) uint32_t* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreInt2A(_Out_writes_(2) uint32_t* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreFloat2(_Out_ Float2* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreFloat2A(_Out_ AlignedFloat2* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreSInt2(_Out_ Int2* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreUInt2(_Out_ UInt2* pDestination, _In_ VectorArg V)noexcept;

    void        MathCallConv     StoreInt3(_Out_writes_(3) uint32_t* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreInt3A(_Out_writes_(3) uint32_t* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreFloat3(_Out_ Float3* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreFloat3A(_Out_ AlignedFloat3* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreSInt3(_Out_ Int3* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreUInt3(_Out_ UInt3* pDestination, _In_ VectorArg V)noexcept;

    void        MathCallConv     StoreInt4(_Out_writes_(4) uint32_t* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreInt4A(_Out_writes_(4) uint32_t* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreFloat4(_Out_ Float4* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreFloat4A(_Out_ AlignedFloat4* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreSInt4(_Out_ Int4* pDestination, _In_ VectorArg V)noexcept;
    void        MathCallConv     StoreUInt4(_Out_ UInt4* pDestination, _In_ VectorArg V)noexcept;

    void        MathCallConv     StoreFloat3x4(_Out_ Float3x4* pDestination, _In_ FXMMATRIX M)noexcept;
    void        MathCallConv     StoreFloat3x4A(_Out_ AlignedFloat3x4* pDestination, _In_ FXMMATRIX M)noexcept;

    // General vector operations

    Vector    MathCallConv     VectorZero()noexcept;
    Vector    MathCallConv     VectorSet(float x, float y, float z, float w)noexcept;
    Vector    MathCallConv     VectorSetInt(uint32_t x, uint32_t y, uint32_t z, uint32_t w)noexcept;
    Vector    MathCallConv     VectorReplicate(float Value)noexcept;
    Vector    MathCallConv     VectorReplicatePtr(_In_ const float* pValue)noexcept;
    Vector    MathCallConv     VectorReplicateInt(uint32_t Value)noexcept;
    Vector    MathCallConv     VectorReplicateIntPtr(_In_ const uint32_t* pValue)noexcept;
    Vector    MathCallConv     VectorTrueInt()noexcept;
    Vector    MathCallConv     VectorFalseInt()noexcept;
    Vector    MathCallConv     VectorSplatX(VectorArg V)noexcept;
    Vector    MathCallConv     VectorSplatY(VectorArg V)noexcept;
    Vector    MathCallConv     VectorSplatZ(VectorArg V)noexcept;
    Vector    MathCallConv     VectorSplatW(VectorArg V)noexcept;
    Vector    MathCallConv     VectorSplatOne()noexcept;
    Vector    MathCallConv     VectorSplatInfinity()noexcept;
    Vector    MathCallConv     VectorSplatQNaN()noexcept;
    Vector    MathCallConv     VectorSplatEpsilon()noexcept;
    Vector    MathCallConv     VectorSplatSignMask()noexcept;

    float       MathCallConv     VectorGetByIndex(VectorArg V, size_t i)noexcept;
    float       MathCallConv     VectorGetX(VectorArg V)noexcept;
    float       MathCallConv     VectorGetY(VectorArg V)noexcept;
    float       MathCallConv     VectorGetZ(VectorArg V)noexcept;
    float       MathCallConv     VectorGetW(VectorArg V)noexcept;

    void        MathCallConv     VectorGetByIndexPtr(_Out_ float* f, _In_ VectorArg V, _In_ size_t i)noexcept;
    void        MathCallConv     VectorGetXPtr(_Out_ float* x, _In_ VectorArg V)noexcept;
    void        MathCallConv     VectorGetYPtr(_Out_ float* y, _In_ VectorArg V)noexcept;
    void        MathCallConv     VectorGetZPtr(_Out_ float* z, _In_ VectorArg V)noexcept;
    void        MathCallConv     VectorGetWPtr(_Out_ float* w, _In_ VectorArg V)noexcept;

    uint32_t    MathCallConv     VectorGetIntByIndex(VectorArg V, size_t i)noexcept;
    uint32_t    MathCallConv     VectorGetIntX(VectorArg V)noexcept;
    uint32_t    MathCallConv     VectorGetIntY(VectorArg V)noexcept;
    uint32_t    MathCallConv     VectorGetIntZ(VectorArg V)noexcept;
    uint32_t    MathCallConv     VectorGetIntW(VectorArg V)noexcept;

    void        MathCallConv     VectorGetIntByIndexPtr(_Out_ uint32_t* x, _In_ VectorArg V, _In_ size_t i)noexcept;
    void        MathCallConv     VectorGetIntXPtr(_Out_ uint32_t* x, _In_ VectorArg V)noexcept;
    void        MathCallConv     VectorGetIntYPtr(_Out_ uint32_t* y, _In_ VectorArg V)noexcept;
    void        MathCallConv     VectorGetIntZPtr(_Out_ uint32_t* z, _In_ VectorArg V)noexcept;
    void        MathCallConv     VectorGetIntWPtr(_Out_ uint32_t* w, _In_ VectorArg V)noexcept;

    Vector    MathCallConv     VectorSetByIndex(VectorArg V, float f, size_t i)noexcept;
    Vector    MathCallConv     VectorSetX(VectorArg V, float x)noexcept;
    Vector    MathCallConv     VectorSetY(VectorArg V, float y)noexcept;
    Vector    MathCallConv     VectorSetZ(VectorArg V, float z)noexcept;
    Vector    MathCallConv     VectorSetW(VectorArg V, float w)noexcept;

    Vector    MathCallConv     VectorSetByIndexPtr(_In_ VectorArg V, _In_ const float* f, _In_ size_t i)noexcept;
    Vector    MathCallConv     VectorSetXPtr(_In_ VectorArg V, _In_ const float* x)noexcept;
    Vector    MathCallConv     VectorSetYPtr(_In_ VectorArg V, _In_ const float* y)noexcept;
    Vector    MathCallConv     VectorSetZPtr(_In_ VectorArg V, _In_ const float* z)noexcept;
    Vector    MathCallConv     VectorSetWPtr(_In_ VectorArg V, _In_ const float* w)noexcept;

    Vector    MathCallConv     VectorSetIntByIndex(VectorArg V, uint32_t x, size_t i)noexcept;
    Vector    MathCallConv     VectorSetIntX(VectorArg V, uint32_t x)noexcept;
    Vector    MathCallConv     VectorSetIntY(VectorArg V, uint32_t y)noexcept;
    Vector    MathCallConv     VectorSetIntZ(VectorArg V, uint32_t z)noexcept;
    Vector    MathCallConv     VectorSetIntW(VectorArg V, uint32_t w)noexcept;

    Vector    MathCallConv     VectorSetIntByIndexPtr(_In_ VectorArg V, _In_ const uint32_t* x, _In_ size_t i)noexcept;
    Vector    MathCallConv     VectorSetIntXPtr(_In_ VectorArg V, _In_ const uint32_t* x)noexcept;
    Vector    MathCallConv     VectorSetIntYPtr(_In_ VectorArg V, _In_ const uint32_t* y)noexcept;
    Vector    MathCallConv     VectorSetIntZPtr(_In_ VectorArg V, _In_ const uint32_t* z)noexcept;
    Vector    MathCallConv     VectorSetIntWPtr(_In_ VectorArg V, _In_ const uint32_t* w)noexcept;

#if defined(__XNAMATH_H__) && defined(VectorSwizzle)
#undef VectorSwizzle
#endif

    Vector    MathCallConv     VectorSwizzle(VectorArg V, uint32_t E0, uint32_t E1, uint32_t E2, uint32_t E3)noexcept;
    Vector    MathCallConv     VectorPermute(
        VectorArg V1,
        VectorArg V2,
        uint32_t PermuteX,
        uint32_t PermuteY,
        uint32_t PermuteZ,
        uint32_t PermuteW
    )noexcept;
    Vector    MathCallConv     VectorSelectControl(
        uint32_t VectorIndex0,
        uint32_t VectorIndex1,
        uint32_t VectorIndex2,
        uint32_t VectorIndex3
    )noexcept;
    Vector    MathCallConv     VectorSelect(VectorArg V1, VectorArg V2, VectorArg Control)noexcept;
    Vector    MathCallConv     VectorMergeXY(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorMergeZW(VectorArg V1, VectorArg V2)noexcept;

#if defined(__XNAMATH_H__) && defined(VectorShiftLeft)
#undef VectorShiftLeft
#undef VectorRotateLeft
#undef VectorRotateRight
#undef VectorInsert
#endif

    Vector    MathCallConv     VectorShiftLeft(VectorArg V1, VectorArg V2, uint32_t Elements)noexcept;
    Vector    MathCallConv     VectorRotateLeft(VectorArg V, uint32_t Elements)noexcept;
    Vector    MathCallConv     VectorRotateRight(VectorArg V, uint32_t Elements)noexcept;
    Vector    MathCallConv     VectorInsert(VectorArg VD, VectorArg VS, uint32_t VSLeftRotateElements,
        uint32_t Select0, uint32_t Select1, uint32_t Select2, uint32_t Select3)noexcept;

    Vector    MathCallConv     VectorEqual(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorEqualR(_Out_ uint32_t* pCR, _In_ VectorArg V1, _In_ VectorArg V2)noexcept;
    Vector    MathCallConv     VectorEqualInt(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorEqualIntR(_Out_ uint32_t* pCR, _In_ VectorArg V, _In_ VectorArg V2)noexcept;
    Vector    MathCallConv     VectorNearEqual(VectorArg V1, VectorArg V2, VectorArg Epsilon)noexcept;
    Vector    MathCallConv     VectorNotEqual(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorNotEqualInt(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorGreater(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorGreaterR(_Out_ uint32_t* pCR, _In_ VectorArg V1, _In_ VectorArg V2)noexcept;
    Vector    MathCallConv     VectorGreaterOrEqual(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorGreaterOrEqualR(_Out_ uint32_t* pCR, _In_ VectorArg V1, _In_ VectorArg V2)noexcept;
    Vector    MathCallConv     VectorLess(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorLessOrEqual(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorInBounds(VectorArg V, VectorArg Bounds)noexcept;
    Vector    MathCallConv     VectorInBoundsR(_Out_ uint32_t* pCR, _In_ VectorArg V, _In_ VectorArg Bounds)noexcept;

    Vector    MathCallConv     VectorIsNaN(VectorArg V)noexcept;
    Vector    MathCallConv     VectorIsInfinite(VectorArg V)noexcept;

    Vector    MathCallConv     VectorMin(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorMax(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorRound(VectorArg V)noexcept;
    Vector    MathCallConv     VectorTruncate(VectorArg V)noexcept;
    Vector    MathCallConv     VectorFloor(VectorArg V)noexcept;
    Vector    MathCallConv     VectorCeiling(VectorArg V)noexcept;
    Vector    MathCallConv     VectorClamp(VectorArg V, VectorArg Min, VectorArg Max)noexcept;
    Vector    MathCallConv     VectorSaturate(VectorArg V)noexcept;

    Vector    MathCallConv     VectorAndInt(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorAndCInt(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorOrInt(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorNorInt(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorXorInt(VectorArg V1, VectorArg V2)noexcept;

    Vector    MathCallConv     VectorNegate(VectorArg V)noexcept;
    Vector    MathCallConv     VectorAdd(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorSum(VectorArg V)noexcept;
    Vector    MathCallConv     VectorAddAngles(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorSubtract(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorSubtractAngles(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorMultiply(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorMultiplyAdd(VectorArg V1, VectorArg V2, VectorArg V3)noexcept;
    Vector    MathCallConv     VectorDivide(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorNegativeMultiplySubtract(VectorArg V1, VectorArg V2, VectorArg V3)noexcept;
    Vector    MathCallConv     VectorScale(VectorArg V, float ScaleFactor)noexcept;
    Vector    MathCallConv     VectorReciprocalEst(VectorArg V)noexcept;
    Vector    MathCallConv     VectorReciprocal(VectorArg V)noexcept;
    Vector    MathCallConv     VectorSqrtEst(VectorArg V)noexcept;
    Vector    MathCallConv     VectorSqrt(VectorArg V)noexcept;
    Vector    MathCallConv     VectorReciprocalSqrtEst(VectorArg V)noexcept;
    Vector    MathCallConv     VectorReciprocalSqrt(VectorArg V)noexcept;
    Vector    MathCallConv     VectorExp2(VectorArg V)noexcept;
    Vector    MathCallConv     VectorExp10(VectorArg V)noexcept;
    Vector    MathCallConv     VectorExpE(VectorArg V)noexcept;
    Vector    MathCallConv     VectorExp(VectorArg V)noexcept;
    Vector    MathCallConv     VectorLog2(VectorArg V)noexcept;
    Vector    MathCallConv     VectorLog10(VectorArg V)noexcept;
    Vector    MathCallConv     VectorLogE(VectorArg V)noexcept;
    Vector    MathCallConv     VectorLog(VectorArg V)noexcept;
    Vector    MathCallConv     VectorPow(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorAbs(VectorArg V)noexcept;
    Vector    MathCallConv     VectorMod(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     VectorModAngles(VectorArg Angles)noexcept;
    Vector    MathCallConv     VectorSin(VectorArg V)noexcept;
    Vector    MathCallConv     VectorSinEst(VectorArg V)noexcept;
    Vector    MathCallConv     VectorCos(VectorArg V)noexcept;
    Vector    MathCallConv     VectorCosEst(VectorArg V)noexcept;
    void        MathCallConv     VectorSinCos(_Out_ Vector* pSin, _Out_ Vector* pCos, _In_ VectorArg V)noexcept;
    void        MathCallConv     VectorSinCosEst(_Out_ Vector* pSin, _Out_ Vector* pCos, _In_ VectorArg V)noexcept;
    Vector    MathCallConv     VectorTan(VectorArg V)noexcept;
    Vector    MathCallConv     VectorTanEst(VectorArg V)noexcept;
    Vector    MathCallConv     VectorSinH(VectorArg V)noexcept;
    Vector    MathCallConv     VectorCosH(VectorArg V)noexcept;
    Vector    MathCallConv     VectorTanH(VectorArg V)noexcept;
    Vector    MathCallConv     VectorASin(VectorArg V)noexcept;
    Vector    MathCallConv     VectorASinEst(VectorArg V)noexcept;
    Vector    MathCallConv     VectorACos(VectorArg V)noexcept;
    Vector    MathCallConv     VectorACosEst(VectorArg V)noexcept;
    Vector    MathCallConv     VectorATan(VectorArg V)noexcept;
    Vector    MathCallConv     VectorATanEst(VectorArg V)noexcept;
    Vector    MathCallConv     VectorATan2(VectorArg Y, VectorArg X)noexcept;
    Vector    MathCallConv     VectorATan2Est(VectorArg Y, VectorArg X)noexcept;
    Vector    MathCallConv     VectorLerp(VectorArg V0, VectorArg V1, float t)noexcept;
    Vector    MathCallConv     VectorLerpV(VectorArg V0, VectorArg V1, VectorArg T)noexcept;
    Vector    MathCallConv     VectorHermite(
        VectorArg Position0,
        VectorArg Tangent0,
        VectorArg Position1,
        VectorArg2 Tangent1,
        float t
    )noexcept;
    Vector    MathCallConv     VectorHermiteV(
        VectorArg Position0,
        VectorArg Tangent0,
        VectorArg Position1,
        VectorArg2 Tangent1,
        VectorArg3 T
    )noexcept;
    Vector    MathCallConv     VectorCatmullRom(
        VectorArg Position0,
        VectorArg Position1,
        VectorArg Position2,
        VectorArg2 Position3,
        float t
    )noexcept;
    Vector    MathCallConv     VectorCatmullRomV(
        VectorArg Position0,
        VectorArg Position1,
        VectorArg Position2,
        VectorArg2 Position3,
        VectorArg3 T
    )noexcept;
    Vector    MathCallConv     VectorBaryCentric(
        VectorArg Position0,
        VectorArg Position1,
        VectorArg Position2,
        float f,
        float g
    )noexcept;
    Vector    MathCallConv     VectorBaryCentricV(
        VectorArg Position0,
        VectorArg Position1,
        VectorArg Position2,
        VectorArg2 F,
        VectorArg3 G
    )noexcept;

    // 2D vector operations

    bool        MathCallConv     Vector2Equal(VectorArg V1, VectorArg V2)noexcept;
    uint32_t    MathCallConv     Vector2EqualR(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector2EqualInt(VectorArg V1, VectorArg V2)noexcept;
    uint32_t    MathCallConv     Vector2EqualIntR(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector2NearEqual(VectorArg V1, VectorArg V2, VectorArg Epsilon)noexcept;
    bool        MathCallConv     Vector2NotEqual(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector2NotEqualInt(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector2Greater(VectorArg V1, VectorArg V2)noexcept;
    uint32_t    MathCallConv     Vector2GreaterR(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector2GreaterOrEqual(VectorArg V1, VectorArg V2)noexcept;
    uint32_t    MathCallConv     Vector2GreaterOrEqualR(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector2Less(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector2LessOrEqual(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector2InBounds(VectorArg V, VectorArg Bounds)noexcept;

    bool        MathCallConv     Vector2IsNaN(VectorArg V)noexcept;
    bool        MathCallConv     Vector2IsInfinite(VectorArg V)noexcept;

    Vector    MathCallConv     Vector2Dot(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     Vector2Cross(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     Vector2LengthSq(VectorArg V)noexcept;
    Vector    MathCallConv     Vector2ReciprocalLengthEst(VectorArg V)noexcept;
    Vector    MathCallConv     Vector2ReciprocalLength(VectorArg V)noexcept;
    Vector    MathCallConv     Vector2LengthEst(VectorArg V)noexcept;
    Vector    MathCallConv     Vector2Length(VectorArg V)noexcept;
    Vector    MathCallConv     Vector2NormalizeEst(VectorArg V)noexcept;
    Vector    MathCallConv     Vector2Normalize(VectorArg V)noexcept;
    Vector    MathCallConv     Vector2ClampLength(VectorArg V, float LengthMin, float LengthMax)noexcept;
    Vector    MathCallConv     Vector2ClampLengthV(VectorArg V, VectorArg LengthMin, VectorArg LengthMax)noexcept;
    Vector    MathCallConv     Vector2Reflect(VectorArg Incident, VectorArg Normal)noexcept;
    Vector    MathCallConv     Vector2Refract(VectorArg Incident, VectorArg Normal, float RefractionIndex)noexcept;
    Vector    MathCallConv     Vector2RefractV(VectorArg Incident, VectorArg Normal, VectorArg RefractionIndex)noexcept;
    Vector    MathCallConv     Vector2Orthogonal(VectorArg V)noexcept;
    Vector    MathCallConv     Vector2AngleBetweenNormalsEst(VectorArg N1, VectorArg N2)noexcept;
    Vector    MathCallConv     Vector2AngleBetweenNormals(VectorArg N1, VectorArg N2)noexcept;
    Vector    MathCallConv     Vector2AngleBetweenVectors(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     Vector2LinePointDistance(VectorArg LinePoint1, VectorArg LinePoint2, VectorArg Point)noexcept;
    Vector    MathCallConv     Vector2IntersectLine(
        VectorArg Line1Point1,
        VectorArg Line1Point2,
        VectorArg Line2Point1,
        VectorArg2 Line2Point2
    )noexcept;
    Vector    MathCallConv     Vector2Transform(VectorArg V, FXMMATRIX M)noexcept;
    Float4*   MathCallConv     Vector2TransformStream(
        _Out_writes_bytes_(sizeof(Float4) + OutputStride * (VectorCount - 1)) Float4* pOutputStream,
        _In_ size_t OutputStride,
        _In_reads_bytes_(sizeof(Float2) + InputStride * (VectorCount - 1)) const Float2* pInputStream,
        _In_ size_t InputStride,
        _In_ size_t VectorCount,
        _In_ FXMMATRIX M
    )noexcept;
    Vector    MathCallConv     Vector2TransformCoord(VectorArg V, FXMMATRIX M)noexcept;
    Float2*   MathCallConv     Vector2TransformCoordStream(
        _Out_writes_bytes_(sizeof(Float2) + OutputStride * (VectorCount - 1)) Float2* pOutputStream,
        _In_ size_t OutputStride,
        _In_reads_bytes_(sizeof(Float2) + InputStride * (VectorCount - 1)) const Float2* pInputStream,
        _In_ size_t InputStride,
        _In_ size_t VectorCount,
        _In_ FXMMATRIX M
    )noexcept;
    Vector    MathCallConv     Vector2TransformNormal(VectorArg V, FXMMATRIX M)noexcept;
    Float2*   MathCallConv     Vector2TransformNormalStream(
        _Out_writes_bytes_(sizeof(Float2) + OutputStride * (VectorCount - 1)) Float2* pOutputStream,
        _In_ size_t OutputStride,
        _In_reads_bytes_(sizeof(Float2) + InputStride * (VectorCount - 1)) const Float2* pInputStream,
        _In_ size_t InputStride,
        _In_ size_t VectorCount,
        _In_ FXMMATRIX M
    )noexcept;

    // 3D vector operations

    bool        MathCallConv     Vector3Equal(VectorArg V1, VectorArg V2)noexcept;
    uint32_t    MathCallConv     Vector3EqualR(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector3EqualInt(VectorArg V1, VectorArg V2)noexcept;
    uint32_t    MathCallConv     Vector3EqualIntR(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector3NearEqual(VectorArg V1, VectorArg V2, VectorArg Epsilon)noexcept;
    bool        MathCallConv     Vector3NotEqual(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector3NotEqualInt(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector3Greater(VectorArg V1, VectorArg V2)noexcept;
    uint32_t    MathCallConv     Vector3GreaterR(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector3GreaterOrEqual(VectorArg V1, VectorArg V2)noexcept;
    uint32_t    MathCallConv     Vector3GreaterOrEqualR(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector3Less(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector3LessOrEqual(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector3InBounds(VectorArg V, VectorArg Bounds)noexcept;

    bool        MathCallConv     Vector3IsNaN(VectorArg V)noexcept;
    bool        MathCallConv     Vector3IsInfinite(VectorArg V)noexcept;

    Vector    MathCallConv     Vector3Dot(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     Vector3Cross(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     Vector3LengthSq(VectorArg V)noexcept;
    Vector    MathCallConv     Vector3ReciprocalLengthEst(VectorArg V)noexcept;
    Vector    MathCallConv     Vector3ReciprocalLength(VectorArg V)noexcept;
    Vector    MathCallConv     Vector3LengthEst(VectorArg V)noexcept;
    Vector    MathCallConv     Vector3Length(VectorArg V)noexcept;
    Vector    MathCallConv     Vector3NormalizeEst(VectorArg V)noexcept;
    Vector    MathCallConv     Vector3Normalize(VectorArg V)noexcept;
    Vector    MathCallConv     Vector3ClampLength(VectorArg V, float LengthMin, float LengthMax)noexcept;
    Vector    MathCallConv     Vector3ClampLengthV(VectorArg V, VectorArg LengthMin, VectorArg LengthMax)noexcept;
    Vector    MathCallConv     Vector3Reflect(VectorArg Incident, VectorArg Normal)noexcept;
    Vector    MathCallConv     Vector3Refract(VectorArg Incident, VectorArg Normal, float RefractionIndex)noexcept;
    Vector    MathCallConv     Vector3RefractV(VectorArg Incident, VectorArg Normal, VectorArg RefractionIndex)noexcept;
    Vector    MathCallConv     Vector3Orthogonal(VectorArg V)noexcept;
    Vector    MathCallConv     Vector3AngleBetweenNormalsEst(VectorArg N1, VectorArg N2)noexcept;
    Vector    MathCallConv     Vector3AngleBetweenNormals(VectorArg N1, VectorArg N2)noexcept;
    Vector    MathCallConv     Vector3AngleBetweenVectors(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     Vector3LinePointDistance(VectorArg LinePoint1, VectorArg LinePoint2, VectorArg Point)noexcept;
    void        MathCallConv     Vector3ComponentsFromNormal(
        _Out_ Vector* pParallel,
        _Out_ Vector* pPerpendicular,
        _In_ VectorArg V,
        _In_ VectorArg Normal
    )noexcept;
    Vector    MathCallConv     Vector3Rotate(VectorArg V, VectorArg RotationQuaternion)noexcept;
    Vector    MathCallConv     Vector3InverseRotate(VectorArg V, VectorArg RotationQuaternion)noexcept;
    Vector    MathCallConv     Vector3Transform(VectorArg V, FXMMATRIX M)noexcept;
    Float4*   MathCallConv     Vector3TransformStream(
        _Out_writes_bytes_(sizeof(Float4) + OutputStride * (VectorCount - 1)) Float4* pOutputStream,
        _In_ size_t OutputStride,
        _In_reads_bytes_(sizeof(Float3) + InputStride * (VectorCount - 1)) const Float3* pInputStream,
        _In_ size_t InputStride,
        _In_ size_t VectorCount,
        _In_ FXMMATRIX M
    )noexcept;
    Vector    MathCallConv     Vector3TransformCoord(VectorArg V, FXMMATRIX M)noexcept;
    Float3*   MathCallConv     Vector3TransformCoordStream(
        _Out_writes_bytes_(sizeof(Float3) + OutputStride * (VectorCount - 1)) Float3* pOutputStream,
        _In_ size_t OutputStride,
        _In_reads_bytes_(sizeof(Float3) + InputStride * (VectorCount - 1)) const Float3* pInputStream,
        _In_ size_t InputStride,
        _In_ size_t VectorCount,
        _In_ FXMMATRIX M
    )noexcept;
    Vector    MathCallConv     Vector3TransformNormal(VectorArg V, FXMMATRIX M)noexcept;
    Float3*   MathCallConv     Vector3TransformNormalStream(
        _Out_writes_bytes_(sizeof(Float3) + OutputStride * (VectorCount - 1)) Float3* pOutputStream,
        _In_ size_t OutputStride,
        _In_reads_bytes_(sizeof(Float3) + InputStride * (VectorCount - 1)) const Float3* pInputStream,
        _In_ size_t InputStride,
        _In_ size_t VectorCount,
        _In_ FXMMATRIX M
    )noexcept;
    Vector    MathCallConv     Vector3Project(
        VectorArg V,
        float ViewportX,
        float ViewportY,
        float ViewportWidth,
        float ViewportHeight,
        float ViewportMinZ,
        float ViewportMaxZ,
        FXMMATRIX Projection,
        CXMMATRIX View,
        CXMMATRIX World
    )noexcept;
    Float3*   MathCallConv     Vector3ProjectStream(
        _Out_writes_bytes_(sizeof(Float3) + OutputStride * (VectorCount - 1)) Float3* pOutputStream,
        _In_ size_t OutputStride,
        _In_reads_bytes_(sizeof(Float3) + InputStride * (VectorCount - 1)) const Float3* pInputStream,
        _In_ size_t InputStride,
        _In_ size_t VectorCount,
        _In_ float ViewportX,
        _In_ float ViewportY,
        _In_ float ViewportWidth,
        _In_ float ViewportHeight,
        _In_ float ViewportMinZ,
        _In_ float ViewportMaxZ,
        _In_ FXMMATRIX Projection,
        _In_ CXMMATRIX View,
        _In_ CXMMATRIX World
    )noexcept;
    Vector    MathCallConv     Vector3Unproject(
        VectorArg V,
        float ViewportX,
        float ViewportY,
        float ViewportWidth,
        float ViewportHeight,
        float ViewportMinZ,
        float ViewportMaxZ,
        FXMMATRIX Projection,
        CXMMATRIX View,
        CXMMATRIX World
    )noexcept;
    Float3*   MathCallConv     Vector3UnprojectStream(
        _Out_writes_bytes_(sizeof(Float3) + OutputStride * (VectorCount - 1)) Float3* pOutputStream,
        _In_ size_t OutputStride,
        _In_reads_bytes_(sizeof(Float3) + InputStride * (VectorCount - 1)) const Float3* pInputStream,
        _In_ size_t InputStride,
        _In_ size_t VectorCount,
        _In_ float ViewportX,
        _In_ float ViewportY,
        _In_ float ViewportWidth,
        _In_ float ViewportHeight,
        _In_ float ViewportMinZ,
        _In_ float ViewportMaxZ,
        _In_ FXMMATRIX Projection,
        _In_ CXMMATRIX View,
        _In_ CXMMATRIX World
    )noexcept;

    // 4D vector operations

    bool        MathCallConv     Vector4Equal(VectorArg V1, VectorArg V2)noexcept;
    uint32_t    MathCallConv     Vector4EqualR(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector4EqualInt(VectorArg V1, VectorArg V2)noexcept;
    uint32_t    MathCallConv     Vector4EqualIntR(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector4NearEqual(VectorArg V1, VectorArg V2, VectorArg Epsilon)noexcept;
    bool        MathCallConv     Vector4NotEqual(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector4NotEqualInt(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector4Greater(VectorArg V1, VectorArg V2)noexcept;
    uint32_t    MathCallConv     Vector4GreaterR(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector4GreaterOrEqual(VectorArg V1, VectorArg V2)noexcept;
    uint32_t    MathCallConv     Vector4GreaterOrEqualR(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector4Less(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector4LessOrEqual(VectorArg V1, VectorArg V2)noexcept;
    bool        MathCallConv     Vector4InBounds(VectorArg V, VectorArg Bounds)noexcept;

    bool        MathCallConv     Vector4IsNaN(VectorArg V)noexcept;
    bool        MathCallConv     Vector4IsInfinite(VectorArg V)noexcept;

    Vector    MathCallConv     Vector4Dot(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     Vector4Cross(VectorArg V1, VectorArg V2, VectorArg V3)noexcept;
    Vector    MathCallConv     Vector4LengthSq(VectorArg V)noexcept;
    Vector    MathCallConv     Vector4ReciprocalLengthEst(VectorArg V)noexcept;
    Vector    MathCallConv     Vector4ReciprocalLength(VectorArg V)noexcept;
    Vector    MathCallConv     Vector4LengthEst(VectorArg V)noexcept;
    Vector    MathCallConv     Vector4Length(VectorArg V)noexcept;
    Vector    MathCallConv     Vector4NormalizeEst(VectorArg V)noexcept;
    Vector    MathCallConv     Vector4Normalize(VectorArg V)noexcept;
    Vector    MathCallConv     Vector4ClampLength(VectorArg V, float LengthMin, float LengthMax)noexcept;
    Vector    MathCallConv     Vector4ClampLengthV(VectorArg V, VectorArg LengthMin, VectorArg LengthMax)noexcept;
    Vector    MathCallConv     Vector4Reflect(VectorArg Incident, VectorArg Normal)noexcept;
    Vector    MathCallConv     Vector4Refract(VectorArg Incident, VectorArg Normal, float RefractionIndex)noexcept;
    Vector    MathCallConv     Vector4RefractV(VectorArg Incident, VectorArg Normal, VectorArg RefractionIndex)noexcept;
    Vector    MathCallConv     Vector4Orthogonal(VectorArg V)noexcept;
    Vector    MathCallConv     Vector4AngleBetweenNormalsEst(VectorArg N1, VectorArg N2)noexcept;
    Vector    MathCallConv     Vector4AngleBetweenNormals(VectorArg N1, VectorArg N2)noexcept;
    Vector    MathCallConv     Vector4AngleBetweenVectors(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     Vector4Transform(VectorArg V, FXMMATRIX M)noexcept;
    Float4*   MathCallConv     Vector4TransformStream(
        _Out_writes_bytes_(sizeof(Float4) + OutputStride * (VectorCount - 1)) Float4* pOutputStream,
        _In_ size_t OutputStride,
        _In_reads_bytes_(sizeof(Float4) + InputStride * (VectorCount - 1)) const Float4* pInputStream,
        _In_ size_t InputStride,
        _In_ size_t VectorCount,
        _In_ FXMMATRIX M
    )noexcept;

    // Matrix operations

    bool        MathCallConv     MatrixIsNaN(FXMMATRIX M)noexcept;
    bool        MathCallConv     MatrixIsInfinite(FXMMATRIX M)noexcept;
    bool        MathCallConv     MatrixIsIdentity(FXMMATRIX M)noexcept;

    Matrix    MathCallConv     MatrixMultiply(FXMMATRIX M1, CXMMATRIX M2)noexcept;
    Matrix    MathCallConv     MatrixMultiplyTranspose(FXMMATRIX M1, CXMMATRIX M2)noexcept;
    Matrix    MathCallConv     MatrixTranspose(FXMMATRIX M)noexcept;
    Matrix    MathCallConv     MatrixInverse(_Out_opt_ Vector* pDeterminant, _In_ FXMMATRIX M)noexcept;
    Matrix    MathCallConv     MatrixVectorTensorProduct(VectorArg V1, VectorArg V2)noexcept;
    Vector    MathCallConv     MatrixDeterminant(FXMMATRIX M)noexcept;

    _Success_(return)
        bool        MathCallConv     MatrixDecompose(
            _Out_ Vector* outScale,
            _Out_ Vector* outRotQuat,
            _Out_ Vector* outTrans,
            _In_ FXMMATRIX M
        )noexcept;

    Matrix    MathCallConv     MatrixIdentity()noexcept;
    Matrix    MathCallConv     MatrixSet(float m00, float m01, float m02, float m03,
        float m10, float m11, float m12, float m13,
        float m20, float m21, float m22, float m23,
        float m30, float m31, float m32, float m33)noexcept;
    Matrix    MathCallConv     MatrixTranslation(float OffsetX, float OffsetY, float OffsetZ)noexcept;
    Matrix    MathCallConv     MatrixTranslationFromVector(VectorArg Offset)noexcept;
    Matrix    MathCallConv     MatrixScaling(float ScaleX, float ScaleY, float ScaleZ)noexcept;
    Matrix    MathCallConv     MatrixScalingFromVector(VectorArg Scale)noexcept;
    Matrix    MathCallConv     MatrixRotationX(float Angle)noexcept;
    Matrix    MathCallConv     MatrixRotationY(float Angle)noexcept;
    Matrix    MathCallConv     MatrixRotationZ(float Angle)noexcept;

    // Under NWB's column-vector convention this applies yaw, then pitch, then
    // roll.
    Matrix    MathCallConv     MatrixRotationRollPitchYaw(float Pitch, float Yaw, float Roll)noexcept;

    // Under NWB's column-vector convention this applies yaw, then pitch, then
    // roll.
    Matrix    MathCallConv     MatrixRotationRollPitchYawFromVector(VectorArg Angles)noexcept;

    Matrix    MathCallConv     MatrixRotationNormal(VectorArg NormalAxis, float Angle)noexcept;
    Matrix    MathCallConv     MatrixRotationAxis(VectorArg Axis, float Angle)noexcept;
    Matrix    MathCallConv     MatrixRotationQuaternion(VectorArg Quaternion)noexcept;
    Matrix    MathCallConv     MatrixTransformation2D(VectorArg ScalingOrigin, float ScalingOrientation, VectorArg Scaling,
        VectorArg RotationOrigin, float Rotation, VectorArg2 Translation)noexcept;
    Matrix    MathCallConv     MatrixTransformation(
        VectorArg ScalingOrigin,
        VectorArg ScalingOrientationQuaternion,
        VectorArg Scaling,
        VectorArg2 RotationOrigin,
        VectorArg3 RotationQuaternion,
        VectorArg3 Translation
    )noexcept;
    Matrix    MathCallConv     MatrixAffineTransformation2D(
        VectorArg Scaling,
        VectorArg RotationOrigin,
        float Rotation,
        VectorArg Translation
    )noexcept;
    Matrix    MathCallConv     MatrixAffineTransformation(
        VectorArg Scaling,
        VectorArg RotationOrigin,
        VectorArg RotationQuaternion,
        VectorArg2 Translation
    )noexcept;
    Matrix    MathCallConv     MatrixReflect(VectorArg ReflectionPlane)noexcept;
    Matrix    MathCallConv     MatrixShadow(VectorArg ShadowPlane, VectorArg LightPosition)noexcept;

    Matrix    MathCallConv     MatrixLookAtLH(VectorArg EyePosition, VectorArg FocusPosition, VectorArg UpDirection)noexcept;
    Matrix    MathCallConv     MatrixLookAtRH(VectorArg EyePosition, VectorArg FocusPosition, VectorArg UpDirection)noexcept;
    Matrix    MathCallConv     MatrixLookToLH(VectorArg EyePosition, VectorArg EyeDirection, VectorArg UpDirection)noexcept;
    Matrix    MathCallConv     MatrixLookToRH(VectorArg EyePosition, VectorArg EyeDirection, VectorArg UpDirection)noexcept;
    Matrix    MathCallConv     MatrixPerspectiveLH(float ViewWidth, float ViewHeight, float NearZ, float FarZ)noexcept;
    Matrix    MathCallConv     MatrixPerspectiveRH(float ViewWidth, float ViewHeight, float NearZ, float FarZ)noexcept;
    Matrix    MathCallConv     MatrixPerspectiveFovLH(float FovAngleY, float AspectRatio, float NearZ, float FarZ)noexcept;
    Matrix    MathCallConv     MatrixPerspectiveFovRH(float FovAngleY, float AspectRatio, float NearZ, float FarZ)noexcept;
    Matrix    MathCallConv     MatrixPerspectiveOffCenterLH(
        float ViewLeft,
        float ViewRight,
        float ViewBottom,
        float ViewTop,
        float NearZ,
        float FarZ
    )noexcept;
    Matrix    MathCallConv     MatrixPerspectiveOffCenterRH(
        float ViewLeft,
        float ViewRight,
        float ViewBottom,
        float ViewTop,
        float NearZ,
        float FarZ
    )noexcept;
    Matrix    MathCallConv     MatrixOrthographicLH(float ViewWidth, float ViewHeight, float NearZ, float FarZ)noexcept;
    Matrix    MathCallConv     MatrixOrthographicRH(float ViewWidth, float ViewHeight, float NearZ, float FarZ)noexcept;
    Matrix    MathCallConv     MatrixOrthographicOffCenterLH(
        float ViewLeft,
        float ViewRight,
        float ViewBottom,
        float ViewTop,
        float NearZ,
        float FarZ
    )noexcept;
    Matrix    MathCallConv     MatrixOrthographicOffCenterRH(
        float ViewLeft,
        float ViewRight,
        float ViewBottom,
        float ViewTop,
        float NearZ,
        float FarZ
    )noexcept;


    // Quaternion operations

    bool        MathCallConv     QuaternionEqual(VectorArg Q1, VectorArg Q2)noexcept;
    bool        MathCallConv     QuaternionNotEqual(VectorArg Q1, VectorArg Q2)noexcept;

    bool        MathCallConv     QuaternionIsNaN(VectorArg Q)noexcept;
    bool        MathCallConv     QuaternionIsInfinite(VectorArg Q)noexcept;
    bool        MathCallConv     QuaternionIsIdentity(VectorArg Q)noexcept;

    Vector    MathCallConv     QuaternionDot(VectorArg Q1, VectorArg Q2)noexcept;
    Vector    MathCallConv     QuaternionMultiply(VectorArg Q1, VectorArg Q2)noexcept;
    Vector    MathCallConv     QuaternionLengthSq(VectorArg Q)noexcept;
    Vector    MathCallConv     QuaternionReciprocalLength(VectorArg Q)noexcept;
    Vector    MathCallConv     QuaternionLength(VectorArg Q)noexcept;
    Vector    MathCallConv     QuaternionNormalizeEst(VectorArg Q)noexcept;
    Vector    MathCallConv     QuaternionNormalize(VectorArg Q)noexcept;
    Vector    MathCallConv     QuaternionConjugate(VectorArg Q)noexcept;
    Vector    MathCallConv     QuaternionInverse(VectorArg Q)noexcept;
    Vector    MathCallConv     QuaternionLn(VectorArg Q)noexcept;
    Vector    MathCallConv     QuaternionExp(VectorArg Q)noexcept;
    Vector    MathCallConv     QuaternionSlerp(VectorArg Q0, VectorArg Q1, float t)noexcept;
    Vector    MathCallConv     QuaternionSlerpV(VectorArg Q0, VectorArg Q1, VectorArg T)noexcept;
    Vector    MathCallConv     QuaternionSquad(VectorArg Q0, VectorArg Q1, VectorArg Q2, VectorArg2 Q3, float t)noexcept;
    Vector    MathCallConv     QuaternionSquadV(VectorArg Q0, VectorArg Q1, VectorArg Q2, VectorArg2 Q3, VectorArg3 T)noexcept;
    void        MathCallConv     QuaternionSquadSetup(
        _Out_ Vector* pA,
        _Out_ Vector* pB,
        _Out_ Vector* pC,
        _In_ VectorArg Q0,
        _In_ VectorArg Q1,
        _In_ VectorArg Q2,
        _In_ VectorArg2 Q3
    )noexcept;
    Vector    MathCallConv     QuaternionBaryCentric(VectorArg Q0, VectorArg Q1, VectorArg Q2, float f, float g)noexcept;
    Vector    MathCallConv     QuaternionBaryCentricV(
        VectorArg Q0,
        VectorArg Q1,
        VectorArg Q2,
        VectorArg2 F,
        VectorArg3 G
    )noexcept;

    Vector    MathCallConv     QuaternionIdentity()noexcept;

    // Under NWB's column-vector convention this applies yaw, then pitch, then
    // roll.
    Vector    MathCallConv     QuaternionRotationRollPitchYaw(float Pitch, float Yaw, float Roll)noexcept;

    // Under NWB's column-vector convention this applies yaw, then pitch, then
    // roll.
    Vector    MathCallConv     QuaternionRotationRollPitchYawFromVector(VectorArg Angles)noexcept;

    Vector    MathCallConv     QuaternionRotationNormal(VectorArg NormalAxis, float Angle)noexcept;
    Vector    MathCallConv     QuaternionRotationAxis(VectorArg Axis, float Angle)noexcept;
    Vector    MathCallConv     QuaternionRotationMatrix(FXMMATRIX M)noexcept;

    void        MathCallConv     QuaternionToAxisAngle(_Out_ Vector* pAxis, _Out_ float* pAngle, _In_ VectorArg Q)noexcept;

    // Plane operations

    bool        MathCallConv     PlaneEqual(VectorArg P1, VectorArg P2)noexcept;
    bool        MathCallConv     PlaneNearEqual(VectorArg P1, VectorArg P2, VectorArg Epsilon)noexcept;
    bool        MathCallConv     PlaneNotEqual(VectorArg P1, VectorArg P2)noexcept;

    bool        MathCallConv     PlaneIsNaN(VectorArg P)noexcept;
    bool        MathCallConv     PlaneIsInfinite(VectorArg P)noexcept;

    Vector    MathCallConv     PlaneDot(VectorArg P, VectorArg V)noexcept;
    Vector    MathCallConv     PlaneDotCoord(VectorArg P, VectorArg V)noexcept;
    Vector    MathCallConv     PlaneDotNormal(VectorArg P, VectorArg V)noexcept;
    Vector    MathCallConv     PlaneNormalizeEst(VectorArg P)noexcept;
    Vector    MathCallConv     PlaneNormalize(VectorArg P)noexcept;
    Vector    MathCallConv     PlaneIntersectLine(VectorArg P, VectorArg LinePoint1, VectorArg LinePoint2)noexcept;
    void        MathCallConv     PlaneIntersectPlane(
        _Out_ Vector* pLinePoint1,
        _Out_ Vector* pLinePoint2,
        _In_ VectorArg P1,
        _In_ VectorArg P2
    )noexcept;

    // Transforms a plane given an inverse transpose matrix
    Vector    MathCallConv     PlaneTransform(VectorArg P, FXMMATRIX ITM)noexcept;

    // Transforms an array of planes given an inverse transpose matrix
    Float4*   MathCallConv     PlaneTransformStream(
        _Out_writes_bytes_(sizeof(Float4) + OutputStride * (PlaneCount - 1)) Float4* pOutputStream,
        _In_ size_t OutputStride,
        _In_reads_bytes_(sizeof(Float4) + InputStride * (PlaneCount - 1)) const Float4* pInputStream,
        _In_ size_t InputStride,
        _In_ size_t PlaneCount,
        _In_ FXMMATRIX ITM
    )noexcept;

    Vector    MathCallConv     PlaneFromPointNormal(VectorArg Point, VectorArg Normal)noexcept;
    Vector    MathCallConv     PlaneFromPoints(VectorArg Point1, VectorArg Point2, VectorArg Point3)noexcept;

    // Color operations

    bool        MathCallConv     ColorEqual(VectorArg C1, VectorArg C2)noexcept;
    bool        MathCallConv     ColorNotEqual(VectorArg C1, VectorArg C2)noexcept;
    bool        MathCallConv     ColorGreater(VectorArg C1, VectorArg C2)noexcept;
    bool        MathCallConv     ColorGreaterOrEqual(VectorArg C1, VectorArg C2)noexcept;
    bool        MathCallConv     ColorLess(VectorArg C1, VectorArg C2)noexcept;
    bool        MathCallConv     ColorLessOrEqual(VectorArg C1, VectorArg C2)noexcept;

    bool        MathCallConv     ColorIsNaN(VectorArg C)noexcept;
    bool        MathCallConv     ColorIsInfinite(VectorArg C)noexcept;

    Vector    MathCallConv     ColorNegative(VectorArg C)noexcept;
    Vector    MathCallConv     ColorModulate(VectorArg C1, VectorArg C2)noexcept;
    Vector    MathCallConv     ColorAdjustSaturation(VectorArg C, float Saturation)noexcept;
    Vector    MathCallConv     ColorAdjustContrast(VectorArg C, float Contrast)noexcept;

    Vector    MathCallConv     ColorRGBToHSL(VectorArg rgb)noexcept;
    Vector    MathCallConv     ColorHSLToRGB(VectorArg hsl)noexcept;

    Vector    MathCallConv     ColorRGBToHSV(VectorArg rgb)noexcept;
    Vector    MathCallConv     ColorHSVToRGB(VectorArg hsv)noexcept;

    Vector    MathCallConv     ColorRGBToYUV(VectorArg rgb)noexcept;
    Vector    MathCallConv     ColorYUVToRGB(VectorArg yuv)noexcept;

    Vector    MathCallConv     ColorRGBToYUV_HD(VectorArg rgb)noexcept;
    Vector    MathCallConv     ColorYUVToRGB_HD(VectorArg yuv)noexcept;

    Vector    MathCallConv     ColorRGBToYUV_UHD(VectorArg rgb)noexcept;
    Vector    MathCallConv     ColorYUVToRGB_UHD(VectorArg yuv)noexcept;

    Vector    MathCallConv     ColorRGBToXYZ(VectorArg rgb)noexcept;
    Vector    MathCallConv     ColorXYZToRGB(VectorArg xyz)noexcept;

    Vector    MathCallConv     ColorXYZToSRGB(VectorArg xyz)noexcept;
    Vector    MathCallConv     ColorSRGBToXYZ(VectorArg srgb)noexcept;

    Vector    MathCallConv     ColorRGBToSRGB(VectorArg rgb)noexcept;
    Vector    MathCallConv     ColorSRGBToRGB(VectorArg srgb)noexcept;


    // Miscellaneous operations

    bool            VerifyCPUSupport()noexcept;

    Vector    MathCallConv     FresnelTerm(VectorArg CosIncidentAngle, VectorArg RefractionIndex)noexcept;

    bool            ScalarNearEqual(float S1, float S2, float Epsilon)noexcept;
    float           ScalarModAngle(float Value)noexcept;

    float           ScalarSin(float Value)noexcept;
    float           ScalarSinEst(float Value)noexcept;

    float           ScalarCos(float Value)noexcept;
    float           ScalarCosEst(float Value)noexcept;

    void            ScalarSinCos(_Out_ float* pSin, _Out_ float* pCos, float Value)noexcept;
    void            ScalarSinCosEst(_Out_ float* pSin, _Out_ float* pCos, float Value)noexcept;

    float           ScalarASin(float Value)noexcept;
    float           ScalarASinEst(float Value)noexcept;

    float           ScalarACos(float Value)noexcept;
    float           ScalarACosEst(float Value)noexcept;

    // Templates

#if defined(__XNAMATH_H__) && defined(Min)
#undef Min
#undef Max
#endif

    template<class T> inline T Min(T a, T b)noexcept { return (a < b) ? a : b; }
    template<class T> inline T Max(T a, T b)noexcept { return (a > b) ? a : b; }


#if defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)

// PermuteHelper internal template (SSE only)
    namespace MathInternal{
        // Slow path fallback for permutes that do not map to a single SSE shuffle opcode.
        template<uint32_t Shuffle, bool WhichX, bool WhichY, bool WhichZ, bool WhichW> struct PermuteHelper{
            static Vector     MathCallConv     Permute(VectorArg v1, VectorArg v2)noexcept{
                static const VectorU32 selectMask =
                { { {
                        WhichX ? 0xFFFFFFFF : 0,
                        WhichY ? 0xFFFFFFFF : 0,
                        WhichZ ? 0xFFFFFFFF : 0,
                        WhichW ? 0xFFFFFFFF : 0,
                } } };

                Vector shuffled1 = MATH_PERMUTE_PS(v1, Shuffle);
                Vector shuffled2 = MATH_PERMUTE_PS(v2, Shuffle);

                Vector masked1 = _mm_andnot_ps(selectMask, shuffled1);
                Vector masked2 = _mm_and_ps(selectMask, shuffled2);

                return _mm_or_ps(masked1, masked2);
            }
        };

        // Fast path for permutes that only read from the first vector.
        template<uint32_t Shuffle> struct PermuteHelper<Shuffle, false, false, false, false>{
            static Vector     MathCallConv     Permute(VectorArg v1, VectorArg)noexcept { return MATH_PERMUTE_PS(v1, Shuffle); }
        };

        // Fast path for permutes that only read from the second vector.
        template<uint32_t Shuffle> struct PermuteHelper<Shuffle, true, true, true, true>{
            static Vector     MathCallConv     Permute(VectorArg, VectorArg v2)noexcept { return MATH_PERMUTE_PS(v2, Shuffle); }
        };

        // Fast path for permutes that read XY from the first vector, ZW from the second.
        template<uint32_t Shuffle> struct PermuteHelper<Shuffle, false, false, true, true>{
            static Vector     MathCallConv     Permute(
                VectorArg v1,
                VectorArg v2
            )noexcept{
                return _mm_shuffle_ps(v1, v2, Shuffle);
            }
        };

        // Fast path for permutes that read XY from the second vector, ZW from the first.
        template<uint32_t Shuffle> struct PermuteHelper<Shuffle, true, true, false, false>{
            static Vector     MathCallConv     Permute(
                VectorArg v1,
                VectorArg v2
            )noexcept{
                return _mm_shuffle_ps(v2, v1, Shuffle);
            }
        };
    }

#endif // _MATH_SSE_INTRINSICS_ && !_MATH_NO_INTRINSICS_

    // General permute template
    template<uint32_t PermuteX, uint32_t PermuteY, uint32_t PermuteZ, uint32_t PermuteW>
    inline Vector     MathCallConv     VectorPermute(VectorArg V1, VectorArg V2)noexcept{
        static_assert(PermuteX <= 7, "PermuteX template parameter out of range");
        static_assert(PermuteY <= 7, "PermuteY template parameter out of range");
        static_assert(PermuteZ <= 7, "PermuteZ template parameter out of range");
        static_assert(PermuteW <= 7, "PermuteW template parameter out of range");

    #if defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
        constexpr uint32_t Shuffle = _MM_SHUFFLE(PermuteW & 3, PermuteZ & 3, PermuteY & 3, PermuteX & 3);

        constexpr bool WhichX = PermuteX > 3;
        constexpr bool WhichY = PermuteY > 3;
        constexpr bool WhichZ = PermuteZ > 3;
        constexpr bool WhichW = PermuteW > 3;

        return MathInternal::PermuteHelper<Shuffle, WhichX, WhichY, WhichZ, WhichW>::Permute(V1, V2);
    #else

        return VectorPermute(V1, V2, PermuteX, PermuteY, PermuteZ, PermuteW);

    #endif
    }

    // Special-case permute templates
    template<> constexpr Vector MathCallConv     VectorPermute<0, 1, 2, 3>(VectorArg V1, VectorArg)noexcept { return V1; }
    template<> constexpr Vector MathCallConv     VectorPermute<4, 5, 6, 7>(VectorArg, VectorArg V2)noexcept { return V2; }

#if defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    template<> inline Vector      MathCallConv     VectorPermute<0, 1, 4, 5>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_movelh_ps(V1, V2);
    }
    template<> inline Vector      MathCallConv     VectorPermute<6, 7, 2, 3>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_movehl_ps(V1, V2);
    }
    template<> inline Vector      MathCallConv     VectorPermute<0, 4, 1, 5>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_unpacklo_ps(V1, V2);
    }
    template<> inline Vector      MathCallConv     VectorPermute<2, 6, 3, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_unpackhi_ps(V1, V2);
    }
    template<> inline Vector      MathCallConv     VectorPermute<2, 3, 6, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_castpd_ps(_mm_unpackhi_pd(_mm_castps_pd(V1), _mm_castps_pd(V2)));
    }
#endif

#if defined(_MATH_SSE4_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    template<> inline Vector      MathCallConv     VectorPermute<4, 1, 2, 3>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0x1);
    }
    template<> inline Vector      MathCallConv     VectorPermute<0, 5, 2, 3>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0x2);
    }
    template<> inline Vector      MathCallConv     VectorPermute<4, 5, 2, 3>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0x3);
    }
    template<> inline Vector      MathCallConv     VectorPermute<0, 1, 6, 3>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0x4);
    }
    template<> inline Vector      MathCallConv     VectorPermute<4, 1, 6, 3>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0x5);
    }
    template<> inline Vector      MathCallConv     VectorPermute<0, 5, 6, 3>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0x6);
    }
    template<> inline Vector      MathCallConv     VectorPermute<4, 5, 6, 3>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0x7);
    }
    template<> inline Vector      MathCallConv     VectorPermute<0, 1, 2, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0x8);
    }
    template<> inline Vector      MathCallConv     VectorPermute<4, 1, 2, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0x9);
    }
    template<> inline Vector      MathCallConv     VectorPermute<0, 5, 2, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0xA);
    }
    template<> inline Vector      MathCallConv     VectorPermute<4, 5, 2, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0xB);
    }
    template<> inline Vector      MathCallConv     VectorPermute<0, 1, 6, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0xC);
    }
    template<> inline Vector      MathCallConv     VectorPermute<4, 1, 6, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0xD);
    }
    template<> inline Vector      MathCallConv     VectorPermute<0, 5, 6, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return _mm_blend_ps(V1, V2, 0xE);
    }
#endif

#if defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)

    // If the indices are all in the range 0-3 or 4-7, then use VectorSwizzle instead
    // The mirror cases are not spelled out here as the programmer can always swap the arguments
    // (i.e. prefer permutes where the X element comes from the V1 vector instead of the V2 vector)

    template<> inline Vector      MathCallConv     VectorPermute<0, 1, 4, 5>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vget_low_f32(V1), vget_low_f32(V2));
    }
    template<> inline Vector      MathCallConv     VectorPermute<1, 0, 4, 5>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vrev64_f32(vget_low_f32(V1)), vget_low_f32(V2));
    }
    template<> inline Vector      MathCallConv     VectorPermute<0, 1, 5, 4>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vget_low_f32(V1), vrev64_f32(vget_low_f32(V2)));
    }
    template<> inline Vector      MathCallConv     VectorPermute<1, 0, 5, 4>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vrev64_f32(vget_low_f32(V1)), vrev64_f32(vget_low_f32(V2)));
    }

    template<> inline Vector      MathCallConv     VectorPermute<2, 3, 6, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vget_high_f32(V1), vget_high_f32(V2));
    }
    template<> inline Vector      MathCallConv     VectorPermute<3, 2, 6, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vrev64_f32(vget_high_f32(V1)), vget_high_f32(V2));
    }
    template<> inline Vector      MathCallConv     VectorPermute<2, 3, 7, 6>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vget_high_f32(V1), vrev64_f32(vget_high_f32(V2)));
    }
    template<> inline Vector      MathCallConv     VectorPermute<3, 2, 7, 6>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vrev64_f32(vget_high_f32(V1)), vrev64_f32(vget_high_f32(V2)));
    }

    template<> inline Vector      MathCallConv     VectorPermute<0, 1, 6, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vget_low_f32(V1), vget_high_f32(V2));
    }
    template<> inline Vector      MathCallConv     VectorPermute<1, 0, 6, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vrev64_f32(vget_low_f32(V1)), vget_high_f32(V2));
    }
    template<> inline Vector      MathCallConv     VectorPermute<0, 1, 7, 6>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vget_low_f32(V1), vrev64_f32(vget_high_f32(V2)));
    }
    template<> inline Vector      MathCallConv     VectorPermute<1, 0, 7, 6>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vrev64_f32(vget_low_f32(V1)), vrev64_f32(vget_high_f32(V2)));
    }

    template<> inline Vector      MathCallConv     VectorPermute<3, 2, 4, 5>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vrev64_f32(vget_high_f32(V1)), vget_low_f32(V2));
    }
    template<> inline Vector      MathCallConv     VectorPermute<2, 3, 5, 4>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vget_high_f32(V1), vrev64_f32(vget_low_f32(V2)));
    }
    template<> inline Vector      MathCallConv     VectorPermute<3, 2, 5, 4>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vcombine_f32(vrev64_f32(vget_high_f32(V1)), vrev64_f32(vget_low_f32(V2)));
    }

    template<> inline Vector      MathCallConv     VectorPermute<0, 4, 2, 6>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vtrnq_f32(V1, V2).val[0];
    }
    template<> inline Vector      MathCallConv     VectorPermute<1, 5, 3, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vtrnq_f32(V1, V2).val[1];
    }

    template<> inline Vector      MathCallConv     VectorPermute<0, 4, 1, 5>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vzipq_f32(V1, V2).val[0];
    }
    template<> inline Vector      MathCallConv     VectorPermute<2, 6, 3, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vzipq_f32(V1, V2).val[1];
    }

    template<> inline Vector      MathCallConv     VectorPermute<0, 2, 4, 6>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vuzpq_f32(V1, V2).val[0];
    }
    template<> inline Vector      MathCallConv     VectorPermute<1, 3, 5, 7>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vuzpq_f32(V1, V2).val[1];
    }

    template<> inline Vector      MathCallConv     VectorPermute<1, 2, 3, 4>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vextq_f32(V1, V2, 1);
    }
    template<> inline Vector      MathCallConv     VectorPermute<2, 3, 4, 5>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vextq_f32(V1, V2, 2);
    }
    template<> inline Vector      MathCallConv     VectorPermute<3, 4, 5, 6>(
        VectorArg V1,
        VectorArg V2
    )noexcept{
        return vextq_f32(V1, V2, 3);
    }

#endif // _MATH_ARM_NEON_INTRINSICS_ && !_MATH_NO_INTRINSICS_


    // General swizzle template
    template<uint32_t SwizzleX, uint32_t SwizzleY, uint32_t SwizzleZ, uint32_t SwizzleW>
    inline Vector     MathCallConv     VectorSwizzle(VectorArg V)noexcept{
        static_assert(SwizzleX <= 3, "SwizzleX template parameter out of range");
        static_assert(SwizzleY <= 3, "SwizzleY template parameter out of range");
        static_assert(SwizzleZ <= 3, "SwizzleZ template parameter out of range");
        static_assert(SwizzleW <= 3, "SwizzleW template parameter out of range");

    #if defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
        return MATH_PERMUTE_PS(V, _MM_SHUFFLE(SwizzleW, SwizzleZ, SwizzleY, SwizzleX));
    #else

        return VectorSwizzle(V, SwizzleX, SwizzleY, SwizzleZ, SwizzleW);

    #endif
    }

    // Specialized swizzles
    template<> constexpr Vector MathCallConv VectorSwizzle<0, 1, 2, 3>(VectorArg V)noexcept { return V; }

#if defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    template<> inline Vector      MathCallConv     VectorSwizzle<0, 1, 0, 1>(
        VectorArg V
    )noexcept{
        return _mm_movelh_ps(V, V);
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<2, 3, 2, 3>(
        VectorArg V
    )noexcept{
        return _mm_movehl_ps(V, V);
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<0, 0, 1, 1>(
        VectorArg V
    )noexcept{
        return _mm_unpacklo_ps(V, V);
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<2, 2, 3, 3>(
        VectorArg V
    )noexcept{
        return _mm_unpackhi_ps(V, V);
    }
#endif

#if defined(_MATH_SSE3_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    template<> inline Vector      MathCallConv     VectorSwizzle<0, 0, 2, 2>(VectorArg V)noexcept { return _mm_moveldup_ps(V); }
    template<> inline Vector      MathCallConv     VectorSwizzle<1, 1, 3, 3>(VectorArg V)noexcept { return _mm_movehdup_ps(V); }
#endif

#if defined(_MATH_AVX2_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && defined(_MATH_FAVOR_INTEL_)
    template<> inline Vector      MathCallConv     VectorSwizzle<0, 0, 0, 0>(
        VectorArg V
    )noexcept{
        return _mm_broadcastss_ps(V);
    }
#endif

#if defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)

    template<> inline Vector      MathCallConv     VectorSwizzle<0, 0, 0, 0>(
        VectorArg V
    )noexcept{
        return vdupq_lane_f32(vget_low_f32(V), 0);
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<1, 1, 1, 1>(
        VectorArg V
    )noexcept{
        return vdupq_lane_f32(vget_low_f32(V), 1);
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<2, 2, 2, 2>(
        VectorArg V
    )noexcept{
        return vdupq_lane_f32(vget_high_f32(V), 0);
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<3, 3, 3, 3>(
        VectorArg V
    )noexcept{
        return vdupq_lane_f32(vget_high_f32(V), 1);
    }

    template<> inline Vector      MathCallConv     VectorSwizzle<1, 0, 3, 2>(VectorArg V)noexcept { return vrev64q_f32(V); }

    template<> inline Vector      MathCallConv     VectorSwizzle<0, 1, 0, 1>(
        VectorArg V
    )noexcept{
        float32x2_t vt = vget_low_f32(V);
        return vcombine_f32(vt, vt);
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<2, 3, 2, 3>(
        VectorArg V
    )noexcept{
        float32x2_t vt = vget_high_f32(V);
        return vcombine_f32(vt, vt);
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<1, 0, 1, 0>(
        VectorArg V
    )noexcept{
        float32x2_t vt = vrev64_f32(vget_low_f32(V));
        return vcombine_f32(vt, vt);
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<3, 2, 3, 2>(
        VectorArg V
    )noexcept{
        float32x2_t vt = vrev64_f32(vget_high_f32(V));
        return vcombine_f32(vt, vt);
    }

    template<> inline Vector      MathCallConv     VectorSwizzle<0, 1, 3, 2>(
        VectorArg V
    )noexcept{
        return vcombine_f32(vget_low_f32(V), vrev64_f32(vget_high_f32(V)));
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<1, 0, 2, 3>(
        VectorArg V
    )noexcept{
        return vcombine_f32(vrev64_f32(vget_low_f32(V)), vget_high_f32(V));
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<2, 3, 1, 0>(
        VectorArg V
    )noexcept{
        return vcombine_f32(vget_high_f32(V), vrev64_f32(vget_low_f32(V)));
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<3, 2, 0, 1>(
        VectorArg V
    )noexcept{
        return vcombine_f32(vrev64_f32(vget_high_f32(V)), vget_low_f32(V));
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<3, 2, 1, 0>(
        VectorArg V
    )noexcept{
        return vcombine_f32(vrev64_f32(vget_high_f32(V)), vrev64_f32(vget_low_f32(V)));
    }

    template<> inline Vector      MathCallConv     VectorSwizzle<0, 0, 2, 2>(
        VectorArg V
    )noexcept{
        return vtrnq_f32(V, V).val[0];
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<1, 1, 3, 3>(
        VectorArg V
    )noexcept{
        return vtrnq_f32(V, V).val[1];
    }

    template<> inline Vector      MathCallConv     VectorSwizzle<0, 0, 1, 1>(
        VectorArg V
    )noexcept{
        return vzipq_f32(V, V).val[0];
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<2, 2, 3, 3>(
        VectorArg V
    )noexcept{
        return vzipq_f32(V, V).val[1];
    }

    template<> inline Vector      MathCallConv     VectorSwizzle<0, 2, 0, 2>(
        VectorArg V
    )noexcept{
        return vuzpq_f32(V, V).val[0];
    }
    template<> inline Vector      MathCallConv     VectorSwizzle<1, 3, 1, 3>(
        VectorArg V
    )noexcept{
        return vuzpq_f32(V, V).val[1];
    }

    template<> inline Vector      MathCallConv     VectorSwizzle<1, 2, 3, 0>(VectorArg V)noexcept { return vextq_f32(V, V, 1); }
    template<> inline Vector      MathCallConv     VectorSwizzle<2, 3, 0, 1>(VectorArg V)noexcept { return vextq_f32(V, V, 2); }
    template<> inline Vector      MathCallConv     VectorSwizzle<3, 0, 1, 2>(VectorArg V)noexcept { return vextq_f32(V, V, 3); }

#endif // _MATH_ARM_NEON_INTRINSICS_ && !_MATH_NO_INTRINSICS_


    template<uint32_t Elements>
    inline Vector     MathCallConv     VectorShiftLeft(VectorArg V1, VectorArg V2)noexcept{
        static_assert(Elements < 4, "Elements template parameter out of range");
        return VectorPermute<Elements, (Elements + 1), (Elements + 2), (Elements + 3)>(V1, V2);
    }

    template<uint32_t Elements>
    inline Vector     MathCallConv     VectorRotateLeft(VectorArg V)noexcept{
        static_assert(Elements < 4, "Elements template parameter out of range");
        return VectorSwizzle<Elements & 3, (Elements + 1) & 3, (Elements + 2) & 3, (Elements + 3) & 3>(V);
    }

    template<uint32_t Elements>
    inline Vector     MathCallConv     VectorRotateRight(VectorArg V)noexcept{
        static_assert(Elements < 4, "Elements template parameter out of range");
        return VectorSwizzle<(4 - Elements) & 3, (5 - Elements) & 3, (6 - Elements) & 3, (7 - Elements) & 3>(V);
    }

    template<uint32_t VSLeftRotateElements, uint32_t Select0, uint32_t Select1, uint32_t Select2, uint32_t Select3>
    inline Vector     MathCallConv     VectorInsert(VectorArg VD, VectorArg VS)noexcept{
        Vector Control = VectorSelectControl(Select0 & 1, Select1 & 1, Select2 & 1, Select3 & 1);
        return VectorSelect(VD, VectorRotateLeft<VSLeftRotateElements>(VS), Control);
    }

    // Globals

    // The purpose of the following global constants is to prevent redundant
    // reloading of the constants when they are referenced by more than one
    // separate inline math routine called within the same function.  Declaring
    // a constant locally within a routine is sufficient to prevent redundant
    // reloads of that constant when that single routine is called multiple
    // times in a function, but if the constant is used (and declared) in a
    // separate math routine it would be reloaded.

#ifndef MATH_GLOBAL_CONST
#if defined(__GNUC__) && !defined(__MINGW32__)
#define MATH_GLOBAL_CONST extern const __attribute__((weak))
#else
#define MATH_GLOBAL_CONST extern const __declspec(selectany)
#endif
#endif

    MATH_GLOBAL_CONST VectorF32 g_SinCoefficients0 = { { { -0.16666667f, +0.0083333310f, -0.00019840874f, +2.7525562e-06f } } };
    MATH_GLOBAL_CONST VectorF32 g_SinCoefficients1 = { { {
        -2.3889859e-08f,
        -0.16665852f /*Est1*/,
        +0.0083139502f /*Est2*/,
        -0.00018524670f /*Est3*/
    } } };
    MATH_GLOBAL_CONST VectorF32 g_CosCoefficients0 = { { { -0.5f, +0.041666638f, -0.0013888378f, +2.4760495e-05f } } };
    MATH_GLOBAL_CONST VectorF32 g_CosCoefficients1 = { { {
        -2.6051615e-07f,
        -0.49992746f /*Est1*/,
        +0.041493919f /*Est2*/,
        -0.0012712436f /*Est3*/
    } } };
    MATH_GLOBAL_CONST VectorF32 g_TanCoefficients0 = { { { 1.0f, 0.333333333f, 0.133333333f, 5.396825397e-2f } } };
    MATH_GLOBAL_CONST VectorF32 g_TanCoefficients1 = { { {
        2.186948854e-2f,
        8.863235530e-3f,
        3.592128167e-3f,
        1.455834485e-3f
    } } };
    MATH_GLOBAL_CONST VectorF32 g_TanCoefficients2 = { { {
        5.900274264e-4f,
        2.391290764e-4f,
        9.691537707e-5f,
        3.927832950e-5f
    } } };
    MATH_GLOBAL_CONST VectorF32 g_ArcCoefficients0 = { { { +1.5707963050f, -0.2145988016f, +0.0889789874f, -0.0501743046f } } };
    MATH_GLOBAL_CONST VectorF32 g_ArcCoefficients1 = { { { +0.0308918810f, -0.0170881256f, +0.0066700901f, -0.0012624911f } } };
    MATH_GLOBAL_CONST VectorF32 g_ATanCoefficients0 = { { {
        -0.3333314528f,
        +0.1999355085f,
        -0.1420889944f,
        +0.1065626393f
    } } };
    MATH_GLOBAL_CONST VectorF32 g_ATanCoefficients1 = { { {
        -0.0752896400f,
        +0.0429096138f,
        -0.0161657367f,
        +0.0028662257f
    } } };
    MATH_GLOBAL_CONST VectorF32 g_ATanEstCoefficients0 = { { { +0.999866f, +0.999866f, +0.999866f, +0.999866f } } };
    MATH_GLOBAL_CONST VectorF32 g_ATanEstCoefficients1 = { { { -0.3302995f, +0.180141f, -0.085133f, +0.0208351f } } };
    MATH_GLOBAL_CONST VectorF32 g_TanEstCoefficients = { { { 2.484f, -1.954923183e-1f, 2.467401101f, MATH_1DIVPI } } };
    MATH_GLOBAL_CONST VectorF32 g_ArcEstCoefficients = { { { +1.5707288f, -0.2121144f, +0.0742610f, -0.0187293f } } };
    MATH_GLOBAL_CONST VectorF32 g_PiConstants0 = { { { MATH_PI, MATH_2PI, MATH_1DIVPI, MATH_1DIV2PI } } };
    // Canonical basis vectors for NWB's internal column-oriented matrix
    // layout.
    MATH_GLOBAL_CONST VectorF32 g_IdentityC0 = { { { 1.0f, 0.0f, 0.0f, 0.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_IdentityC1 = { { { 0.0f, 1.0f, 0.0f, 0.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_IdentityC2 = { { { 0.0f, 0.0f, 1.0f, 0.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_IdentityC3 = { { { 0.0f, 0.0f, 0.0f, 1.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_NegIdentityC0 = { { { -1.0f, 0.0f, 0.0f, 0.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_NegIdentityC1 = { { { 0.0f, -1.0f, 0.0f, 0.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_NegIdentityC2 = { { { 0.0f, 0.0f, -1.0f, 0.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_NegIdentityC3 = { { { 0.0f, 0.0f, 0.0f, -1.0f } } };
    MATH_GLOBAL_CONST VectorU32 g_NegativeZero = { { { 0x80000000, 0x80000000, 0x80000000, 0x80000000 } } };
    MATH_GLOBAL_CONST VectorU32 g_Negate3 = { { { 0x80000000, 0x80000000, 0x80000000, 0x00000000 } } };
    MATH_GLOBAL_CONST VectorU32 g_MaskXY = { { { 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000 } } };
    MATH_GLOBAL_CONST VectorU32 g_Mask3 = { { { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000 } } };
    MATH_GLOBAL_CONST VectorU32 g_MaskX = { { { 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000 } } };
    MATH_GLOBAL_CONST VectorU32 g_MaskY = { { { 0x00000000, 0xFFFFFFFF, 0x00000000, 0x00000000 } } };
    MATH_GLOBAL_CONST VectorU32 g_MaskZ = { { { 0x00000000, 0x00000000, 0xFFFFFFFF, 0x00000000 } } };
    MATH_GLOBAL_CONST VectorU32 g_MaskW = { { { 0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFF } } };
    MATH_GLOBAL_CONST VectorF32 g_One = { { { 1.0f, 1.0f, 1.0f, 1.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_One3 = { { { 1.0f, 1.0f, 1.0f, 0.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_Zero = { { { 0.0f, 0.0f, 0.0f, 0.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_Two = { { { 2.f, 2.f, 2.f, 2.f } } };
    MATH_GLOBAL_CONST VectorF32 g_Four = { { { 4.f, 4.f, 4.f, 4.f } } };
    MATH_GLOBAL_CONST VectorF32 g_Six = { { { 6.f, 6.f, 6.f, 6.f } } };
    MATH_GLOBAL_CONST VectorF32 g_NegativeOne = { { { -1.0f, -1.0f, -1.0f, -1.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_OneHalf = { { { 0.5f, 0.5f, 0.5f, 0.5f } } };
    MATH_GLOBAL_CONST VectorF32 g_NegativeOneHalf = { { { -0.5f, -0.5f, -0.5f, -0.5f } } };
    MATH_GLOBAL_CONST VectorF32 g_NegativeTwoPi = { { { -MATH_2PI, -MATH_2PI, -MATH_2PI, -MATH_2PI } } };
    MATH_GLOBAL_CONST VectorF32 g_NegativePi = { { { -MATH_PI, -MATH_PI, -MATH_PI, -MATH_PI } } };
    MATH_GLOBAL_CONST VectorF32 g_HalfPi = { { { MATH_PIDIV2, MATH_PIDIV2, MATH_PIDIV2, MATH_PIDIV2 } } };
    MATH_GLOBAL_CONST VectorF32 g_Pi = { { { MATH_PI, MATH_PI, MATH_PI, MATH_PI } } };
    MATH_GLOBAL_CONST VectorF32 g_ReciprocalPi = { { { MATH_1DIVPI, MATH_1DIVPI, MATH_1DIVPI, MATH_1DIVPI } } };
    MATH_GLOBAL_CONST VectorF32 g_TwoPi = { { { MATH_2PI, MATH_2PI, MATH_2PI, MATH_2PI } } };
    MATH_GLOBAL_CONST VectorF32 g_ReciprocalTwoPi = { { { MATH_1DIV2PI, MATH_1DIV2PI, MATH_1DIV2PI, MATH_1DIV2PI } } };
    MATH_GLOBAL_CONST VectorF32 g_Epsilon = { { { 1.192092896e-7f, 1.192092896e-7f, 1.192092896e-7f, 1.192092896e-7f } } };
    MATH_GLOBAL_CONST VectorI32 g_Infinity = { { { 0x7F800000, 0x7F800000, 0x7F800000, 0x7F800000 } } };
    MATH_GLOBAL_CONST VectorI32 g_QNaN = { { { 0x7FC00000, 0x7FC00000, 0x7FC00000, 0x7FC00000 } } };
    MATH_GLOBAL_CONST VectorI32 g_QNaNTest = { { { 0x007FFFFF, 0x007FFFFF, 0x007FFFFF, 0x007FFFFF } } };
    MATH_GLOBAL_CONST VectorI32 g_AbsMask = { { { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF } } };
    MATH_GLOBAL_CONST VectorI32 g_FltMin = { { { 0x00800000, 0x00800000, 0x00800000, 0x00800000 } } };
    MATH_GLOBAL_CONST VectorI32 g_FltMax = { { { 0x7F7FFFFF, 0x7F7FFFFF, 0x7F7FFFFF, 0x7F7FFFFF } } };
    MATH_GLOBAL_CONST VectorU32 g_NegOneMask = { { { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF } } };
    MATH_GLOBAL_CONST VectorU32 g_MaskA8R8G8B8 = { { { 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000 } } };
    MATH_GLOBAL_CONST VectorU32 g_FlipA8R8G8B8 = { { { 0x00000000, 0x00000000, 0x00000000, 0x80000000 } } };
    MATH_GLOBAL_CONST VectorF32 g_FixAA8R8G8B8 = { { { 0.0f, 0.0f, 0.0f, float(0x80000000U) } } };
    MATH_GLOBAL_CONST VectorF32 g_NormalizeA8R8G8B8 = { { {
        1.0f / (255.0f * float(0x10000)),
        1.0f / (255.0f * float(0x100)),
        1.0f / 255.0f,
        1.0f / (255.0f * float(0x1000000))
    } } };
    MATH_GLOBAL_CONST VectorU32 g_MaskA2B10G10R10 = { { { 0x000003FF, 0x000FFC00, 0x3FF00000, 0xC0000000 } } };
    MATH_GLOBAL_CONST VectorU32 g_FlipA2B10G10R10 = { { { 0x00000200, 0x00080000, 0x20000000, 0x80000000 } } };
    MATH_GLOBAL_CONST VectorF32 g_FixAA2B10G10R10 = { { {
        -512.0f,
        -512.0f * float(0x400),
        -512.0f * float(0x100000),
        float(0x80000000U)
    } } };
    MATH_GLOBAL_CONST VectorF32 g_NormalizeA2B10G10R10 = { { {
        1.0f / 511.0f,
        1.0f / (511.0f * float(0x400)),
        1.0f / (511.0f * float(0x100000)),
        1.0f / (3.0f * float(0x40000000))
    } } };
    MATH_GLOBAL_CONST VectorU32 g_MaskX16Y16 = { { { 0x0000FFFF, 0xFFFF0000, 0x00000000, 0x00000000 } } };
    MATH_GLOBAL_CONST VectorI32 g_FlipX16Y16 = { { { 0x00008000, 0x00000000, 0x00000000, 0x00000000 } } };
    MATH_GLOBAL_CONST VectorF32 g_FixX16Y16 = { { { -32768.0f, 0.0f, 0.0f, 0.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_NormalizeX16Y16 = { { { 1.0f / 32767.0f, 1.0f / (32767.0f * 65536.0f), 0.0f, 0.0f } } };
    MATH_GLOBAL_CONST VectorU32 g_MaskX16Y16Z16W16 = { { { 0x0000FFFF, 0x0000FFFF, 0xFFFF0000, 0xFFFF0000 } } };
    MATH_GLOBAL_CONST VectorI32 g_FlipX16Y16Z16W16 = { { { 0x00008000, 0x00008000, 0x00000000, 0x00000000 } } };
    MATH_GLOBAL_CONST VectorF32 g_FixX16Y16Z16W16 = { { { -32768.0f, -32768.0f, 0.0f, 0.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_NormalizeX16Y16Z16W16 = { { {
        1.0f / 32767.0f,
        1.0f / 32767.0f,
        1.0f / (32767.0f * 65536.0f),
        1.0f / (32767.0f * 65536.0f)
    } } };
    MATH_GLOBAL_CONST VectorF32 g_NoFraction = { { { 8388608.0f, 8388608.0f, 8388608.0f, 8388608.0f } } };
    MATH_GLOBAL_CONST VectorI32 g_MaskByte = { { { 0x000000FF, 0x000000FF, 0x000000FF, 0x000000FF } } };
    MATH_GLOBAL_CONST VectorF32 g_NegateX = { { { -1.0f, 1.0f, 1.0f, 1.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_NegateY = { { { 1.0f, -1.0f, 1.0f, 1.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_NegateZ = { { { 1.0f, 1.0f, -1.0f, 1.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_NegateW = { { { 1.0f, 1.0f, 1.0f, -1.0f } } };
    MATH_GLOBAL_CONST VectorU32 g_Select0101 = { { { MATH_SELECT_0, MATH_SELECT_1, MATH_SELECT_0, MATH_SELECT_1 } } };
    MATH_GLOBAL_CONST VectorU32 g_Select1010 = { { { MATH_SELECT_1, MATH_SELECT_0, MATH_SELECT_1, MATH_SELECT_0 } } };
    MATH_GLOBAL_CONST VectorI32 g_OneHalfMinusEpsilon = { { { 0x3EFFFFFD, 0x3EFFFFFD, 0x3EFFFFFD, 0x3EFFFFFD } } };
    MATH_GLOBAL_CONST VectorU32 g_Select1000 = { { { MATH_SELECT_1, MATH_SELECT_0, MATH_SELECT_0, MATH_SELECT_0 } } };
    MATH_GLOBAL_CONST VectorU32 g_Select1100 = { { { MATH_SELECT_1, MATH_SELECT_1, MATH_SELECT_0, MATH_SELECT_0 } } };
    MATH_GLOBAL_CONST VectorU32 g_Select1110 = { { { MATH_SELECT_1, MATH_SELECT_1, MATH_SELECT_1, MATH_SELECT_0 } } };
    MATH_GLOBAL_CONST VectorU32 g_Select1011 = { { { MATH_SELECT_1, MATH_SELECT_0, MATH_SELECT_1, MATH_SELECT_1 } } };
    MATH_GLOBAL_CONST VectorF32 g_FixupY16 = { { { 1.0f, 1.0f / 65536.0f, 0.0f, 0.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_FixupY16W16 = { { { 1.0f, 1.0f, 1.0f / 65536.0f, 1.0f / 65536.0f } } };
    MATH_GLOBAL_CONST VectorU32 g_FlipY = { { { 0, 0x80000000, 0, 0 } } };
    MATH_GLOBAL_CONST VectorU32 g_FlipZ = { { { 0, 0, 0x80000000, 0 } } };
    MATH_GLOBAL_CONST VectorU32 g_FlipW = { { { 0, 0, 0, 0x80000000 } } };
    MATH_GLOBAL_CONST VectorU32 g_FlipYZ = { { { 0, 0x80000000, 0x80000000, 0 } } };
    MATH_GLOBAL_CONST VectorU32 g_FlipZW = { { { 0, 0, 0x80000000, 0x80000000 } } };
    MATH_GLOBAL_CONST VectorU32 g_FlipYW = { { { 0, 0x80000000, 0, 0x80000000 } } };
    MATH_GLOBAL_CONST VectorI32 g_MaskDec4 = { { { 0x3FF, 0x3FF << 10, 0x3FF << 20, static_cast<int>(0xC0000000) } } };
    MATH_GLOBAL_CONST VectorI32 g_XorDec4 = { { { 0x200, 0x200 << 10, 0x200 << 20, 0 } } };
    MATH_GLOBAL_CONST VectorF32 g_AddUDec4 = { { { 0, 0, 0, 32768.0f * 65536.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_AddDec4 = { { { -512.0f, -512.0f * 1024.0f, -512.0f * 1024.0f * 1024.0f, 0 } } };
    MATH_GLOBAL_CONST VectorF32 g_MulDec4 = { { {
        1.0f,
        1.0f / 1024.0f,
        1.0f / (1024.0f * 1024.0f),
        1.0f / (1024.0f * 1024.0f * 1024.0f)
    } } };
    MATH_GLOBAL_CONST VectorU32 g_MaskByte4 = { { { 0xFF, 0xFF00, 0xFF0000, 0xFF000000 } } };
    MATH_GLOBAL_CONST VectorI32 g_XorByte4 = { { { 0x80, 0x8000, 0x800000, 0x00000000 } } };
    MATH_GLOBAL_CONST VectorF32 g_AddByte4 = { { { -128.0f, -128.0f * 256.0f, -128.0f * 65536.0f, 0 } } };
    MATH_GLOBAL_CONST VectorF32 g_FixUnsigned = { { {
        32768.0f * 65536.0f,
        32768.0f * 65536.0f,
        32768.0f * 65536.0f,
        32768.0f * 65536.0f
    } } };
    MATH_GLOBAL_CONST VectorF32 g_MaxInt = { { {
        65536.0f * 32768.0f - 128.0f,
        65536.0f * 32768.0f - 128.0f,
        65536.0f * 32768.0f - 128.0f,
        65536.0f * 32768.0f - 128.0f
    } } };
    MATH_GLOBAL_CONST VectorF32 g_MaxUInt = { { {
        65536.0f * 65536.0f - 256.0f,
        65536.0f * 65536.0f - 256.0f,
        65536.0f * 65536.0f - 256.0f,
        65536.0f * 65536.0f - 256.0f
    } } };
    MATH_GLOBAL_CONST VectorF32 g_UnsignedFix = { { {
        32768.0f * 65536.0f,
        32768.0f * 65536.0f,
        32768.0f * 65536.0f,
        32768.0f * 65536.0f
    } } };
    MATH_GLOBAL_CONST VectorF32 g_srgbScale = { { { 12.92f, 12.92f, 12.92f, 1.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_srgbA = { { { 0.055f, 0.055f, 0.055f, 0.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_srgbA1 = { { { 1.055f, 1.055f, 1.055f, 1.0f } } };
    MATH_GLOBAL_CONST VectorI32 g_ExponentBias = { { { 127, 127, 127, 127 } } };
    MATH_GLOBAL_CONST VectorI32 g_SubnormalExponent = { { { -126, -126, -126, -126 } } };
    MATH_GLOBAL_CONST VectorI32 g_NumTrailing = { { { 23, 23, 23, 23 } } };
    MATH_GLOBAL_CONST VectorI32 g_MinNormal = { { { 0x00800000, 0x00800000, 0x00800000, 0x00800000 } } };
    MATH_GLOBAL_CONST VectorU32 g_NegInfinity = { { { 0xFF800000, 0xFF800000, 0xFF800000, 0xFF800000 } } };
    MATH_GLOBAL_CONST VectorU32 g_NegQNaN = { { { 0xFFC00000, 0xFFC00000, 0xFFC00000, 0xFFC00000 } } };
    MATH_GLOBAL_CONST VectorI32 g_Bin128 = { { { 0x43000000, 0x43000000, 0x43000000, 0x43000000 } } };
    MATH_GLOBAL_CONST VectorU32 g_BinNeg150 = { { { 0xC3160000, 0xC3160000, 0xC3160000, 0xC3160000 } } };
    MATH_GLOBAL_CONST VectorI32 g_253 = { { { 253, 253, 253, 253 } } };
    MATH_GLOBAL_CONST VectorF32 g_ExpEst1 = { { { -6.93147182e-1f, -6.93147182e-1f, -6.93147182e-1f, -6.93147182e-1f } } };
    MATH_GLOBAL_CONST VectorF32 g_ExpEst2 = { { { +2.40226462e-1f, +2.40226462e-1f, +2.40226462e-1f, +2.40226462e-1f } } };
    MATH_GLOBAL_CONST VectorF32 g_ExpEst3 = { { { -5.55036440e-2f, -5.55036440e-2f, -5.55036440e-2f, -5.55036440e-2f } } };
    MATH_GLOBAL_CONST VectorF32 g_ExpEst4 = { { { +9.61597636e-3f, +9.61597636e-3f, +9.61597636e-3f, +9.61597636e-3f } } };
    MATH_GLOBAL_CONST VectorF32 g_ExpEst5 = { { { -1.32823968e-3f, -1.32823968e-3f, -1.32823968e-3f, -1.32823968e-3f } } };
    MATH_GLOBAL_CONST VectorF32 g_ExpEst6 = { { { +1.47491097e-4f, +1.47491097e-4f, +1.47491097e-4f, +1.47491097e-4f } } };
    MATH_GLOBAL_CONST VectorF32 g_ExpEst7 = { { { -1.08635004e-5f, -1.08635004e-5f, -1.08635004e-5f, -1.08635004e-5f } } };
    MATH_GLOBAL_CONST VectorF32 g_LogEst0 = { { { +1.442693f, +1.442693f, +1.442693f, +1.442693f } } };
    MATH_GLOBAL_CONST VectorF32 g_LogEst1 = { { { -0.721242f, -0.721242f, -0.721242f, -0.721242f } } };
    MATH_GLOBAL_CONST VectorF32 g_LogEst2 = { { { +0.479384f, +0.479384f, +0.479384f, +0.479384f } } };
    MATH_GLOBAL_CONST VectorF32 g_LogEst3 = { { { -0.350295f, -0.350295f, -0.350295f, -0.350295f } } };
    MATH_GLOBAL_CONST VectorF32 g_LogEst4 = { { { +0.248590f, +0.248590f, +0.248590f, +0.248590f } } };
    MATH_GLOBAL_CONST VectorF32 g_LogEst5 = { { { -0.145700f, -0.145700f, -0.145700f, -0.145700f } } };
    MATH_GLOBAL_CONST VectorF32 g_LogEst6 = { { { +0.057148f, +0.057148f, +0.057148f, +0.057148f } } };
    MATH_GLOBAL_CONST VectorF32 g_LogEst7 = { { { -0.010578f, -0.010578f, -0.010578f, -0.010578f } } };
    MATH_GLOBAL_CONST VectorF32 g_LgE = { { { +1.442695f, +1.442695f, +1.442695f, +1.442695f } } };
    MATH_GLOBAL_CONST VectorF32 g_InvLgE = { { { +6.93147182e-1f, +6.93147182e-1f, +6.93147182e-1f, +6.93147182e-1f } } };
    MATH_GLOBAL_CONST VectorF32 g_Lg10 = { { { +3.321928f, +3.321928f, +3.321928f, +3.321928f } } };
    MATH_GLOBAL_CONST VectorF32 g_InvLg10 = { { { +3.010299956e-1f, +3.010299956e-1f, +3.010299956e-1f, +3.010299956e-1f } } };
    MATH_GLOBAL_CONST VectorF32 g_UByteMax = { { { 255.0f, 255.0f, 255.0f, 255.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_ByteMin = { { { -127.0f, -127.0f, -127.0f, -127.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_ByteMax = { { { 127.0f, 127.0f, 127.0f, 127.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_ShortMin = { { { -32767.0f, -32767.0f, -32767.0f, -32767.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_ShortMax = { { { 32767.0f, 32767.0f, 32767.0f, 32767.0f } } };
    MATH_GLOBAL_CONST VectorF32 g_UShortMax = { { { 65535.0f, 65535.0f, 65535.0f, 65535.0f } } };

    // Implementation

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4068 4214 4204 4365 4616 4640 6001 6101)
    // C4068/4616: ignore unknown pragmas
    // C4214/4204: nonstandard extension used
    // C4365/4640: Off by default noise
    // C6001/6101: False positives
#endif

#ifdef _PREFAST_
#pragma prefast(push)
#pragma prefast(disable : 25000, "VectorArg is 16 bytes")
#pragma prefast(disable : 26495, "Union initialization confuses /analyze")
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#pragma clang diagnostic ignored "-Wundefined-reinterpret-cast"
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif


    inline Vector MathCallConv VectorSetBinaryConstant(uint32_t C0, uint32_t C1, uint32_t C2, uint32_t C3)noexcept{
    #if defined(_MATH_NO_INTRINSICS_)
        VectorU32 vResult;
        vResult.u[0] = (0 - (C0 & 1)) & 0x3F800000;
        vResult.u[1] = (0 - (C1 & 1)) & 0x3F800000;
        vResult.u[2] = (0 - (C2 & 1)) & 0x3F800000;
        vResult.u[3] = (0 - (C3 & 1)) & 0x3F800000;
        return vResult.v;
    #elif defined(_MATH_ARM_NEON_INTRINSICS_)
        VectorU32 vResult;
        vResult.u[0] = (0 - (C0 & 1)) & 0x3F800000;
        vResult.u[1] = (0 - (C1 & 1)) & 0x3F800000;
        vResult.u[2] = (0 - (C2 & 1)) & 0x3F800000;
        vResult.u[3] = (0 - (C3 & 1)) & 0x3F800000;
        return vResult.v;
    #else // MATH_SSE_INTRINSICS_
        static const VectorU32 g_vMask1 = { { { 1, 1, 1, 1 } } };
        // Move the parms to a vector
        __m128i vTemp = _mm_set_epi32(static_cast<int>(C3), static_cast<int>(C2), static_cast<int>(C1), static_cast<int>(C0));
        // Mask off the low bits
        vTemp = _mm_and_si128(vTemp, g_vMask1);
        // 0xFFFFFFFF on true bits
        vTemp = _mm_cmpeq_epi32(vTemp, g_vMask1);
        // 0xFFFFFFFF -> 1.0f, 0x00000000 -> 0.0f
        vTemp = _mm_and_si128(vTemp, g_One);
        return _mm_castsi128_ps(vTemp);
    #endif
    }


    inline Vector MathCallConv VectorSplatConstant(int32_t IntConstant, uint32_t DivExponent)noexcept{
        assert(IntConstant >= -16 && IntConstant <= 15);
        assert(DivExponent < 32);
    #if defined(_MATH_NO_INTRINSICS_)

        using SourceMathInternal::ConvertVectorIntToFloat;

        VectorI32 V = { { { IntConstant, IntConstant, IntConstant, IntConstant } } };
        return ConvertVectorIntToFloat(V.v, DivExponent);

    #elif defined(_MATH_ARM_NEON_INTRINSICS_)
            // Splat the int
        int32x4_t vScale = vdupq_n_s32(IntConstant);
        // Convert to a float
        Vector vResult = vcvtq_f32_s32(vScale);
        // Convert DivExponent into 1.0f/(1<<DivExponent)
        uint32_t uScale = 0x3F800000U - (DivExponent << 23);
        // Splat the scalar value (It's really a float)
        vScale = vreinterpretq_s32_u32(vdupq_n_u32(uScale));
        // Multiply by the reciprocal (Perform a right shift by DivExponent)
        vResult = vmulq_f32(vResult, reinterpret_cast<const float32x4_t*>(&vScale)[0]);
        return vResult;
    #else // MATH_SSE_INTRINSICS_
            // Splat the int
        __m128i vScale = _mm_set1_epi32(IntConstant);
        // Convert to a float
        Vector vResult = _mm_cvtepi32_ps(vScale);
        // Convert DivExponent into 1.0f/(1<<DivExponent)
        uint32_t uScale = 0x3F800000U - (DivExponent << 23);
        // Splat the scalar value (It's really a float)
        vScale = _mm_set1_epi32(static_cast<int>(uScale));
        // Multiply by the reciprocal (Perform a right shift by DivExponent)
        vResult = _mm_mul_ps(vResult, _mm_castsi128_ps(vScale));
        return vResult;
    #endif
    }


    inline Vector MathCallConv VectorSplatConstantInt(int32_t IntConstant)noexcept{
        assert(IntConstant >= -16 && IntConstant <= 15);
    #if defined(_MATH_NO_INTRINSICS_)

        VectorI32 V = { { { IntConstant, IntConstant, IntConstant, IntConstant } } };
        return V.v;

    #elif defined(_MATH_ARM_NEON_INTRINSICS_)
        int32x4_t V = vdupq_n_s32(IntConstant);
        return reinterpret_cast<float32x4_t*>(&V)[0];
    #else // MATH_SSE_INTRINSICS_
        __m128i V = _mm_set1_epi32(IntConstant);
        return _mm_castsi128_ps(V);
    #endif
    }

#include "source_math_convert.inl"
#include "source_math_vector.inl"
#include "source_math_matrix.inl"
#include "source_math_misc.inl"

#ifdef __clang__
#pragma clang diagnostic pop
#endif
#ifdef _PREFAST_
#pragma prefast(pop)
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

