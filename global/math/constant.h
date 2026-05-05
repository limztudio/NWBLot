// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SIMD_VECTOR_CONST_CONVERSIONS \
    inline operator SIMDVector()const noexcept{ return v; } \
    inline operator int32x4_t()const noexcept{ return vreinterpretq_s32_f32(v); } \
    inline operator uint32x4_t()const noexcept{ return vreinterpretq_u32_f32(v); }

#define NWB_SIMD_VECTOR_CONST_X86_CONVERSIONS \
    inline operator SIMDVector()const noexcept{ return v; } \
    inline operator __m128i()const noexcept{ return _mm_castps_si128(v); } \
    inline operator __m128d()const noexcept{ return _mm_castps_pd(v); }

#define NWB_SIMD_VECTOR_CONST_SCALAR_CONVERSION \
    inline operator SIMDVector()const noexcept{ return v; }

#if defined(NWB_HAS_NEON)
#define NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS NWB_SIMD_VECTOR_CONST_CONVERSIONS
#elif !defined(NWB_HAS_SCALAR)
#define NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS NWB_SIMD_VECTOR_CONST_X86_CONVERSIONS
#else
#define NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS NWB_SIMD_VECTOR_CONST_SCALAR_CONVERSION
#endif

struct alignas(16) SIMDVectorConstF{
    union{
        f32 f[4];
        SIMDVector v;
    };

    inline operator const f32*()const noexcept{ return f; }
    NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS
};

struct alignas(16) SIMDVectorConstI{
    union{
        i32 i[4];
        SIMDVector v;
    };

    NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS
};

struct alignas(16) SIMDVectorConstU{
    union{
        u32 u[4];
        SIMDVector v;
    };

    NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS
};

struct alignas(16) SIMDVectorConstB{
    union{
        u8 b[16];
        SIMDVector v;
    };

    NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS
};

#undef NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS
#undef NWB_SIMD_VECTOR_CONST_SCALAR_CONVERSION
#undef NWB_SIMD_VECTOR_CONST_X86_CONVERSIONS
#undef NWB_SIMD_VECTOR_CONST_CONVERSIONS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr f32 s_PI = 3.141592654f;
constexpr f32 s_2PI = 6.283185307f;
constexpr f32 s_1DIVPI = 0.318309886f;
constexpr f32 s_1DIV2PI = 0.159154943f;
constexpr f32 s_PIDIV2 = 1.570796327f;
constexpr f32 s_PIDIV4 = 0.785398163f;

constexpr u32 s_SELECT_0 = 0x00000000;
constexpr u32 s_SELECT_1 = 0xFFFFFFFF;

constexpr u32 s_PERMUTE_0X = 0;
constexpr u32 s_PERMUTE_0Y = 1;
constexpr u32 s_PERMUTE_0Z = 2;
constexpr u32 s_PERMUTE_0W = 3;
constexpr u32 s_PERMUTE_1X = 4;
constexpr u32 s_PERMUTE_1Y = 5;
constexpr u32 s_PERMUTE_1Z = 6;
constexpr u32 s_PERMUTE_1W = 7;

constexpr u32 s_SWIZZLE_X = 0;
constexpr u32 s_SWIZZLE_Y = 1;
constexpr u32 s_SWIZZLE_Z = 2;
constexpr u32 s_SWIZZLE_W = 3;

constexpr u32 s_CRMASK_CR6 = 0x000000F0;
constexpr u32 s_CRMASK_CR6TRUE = 0x00000080;
constexpr u32 s_CRMASK_CR6FALSE = 0x00000020;
constexpr u32 s_CRMASK_CR6BOUNDS = s_CRMASK_CR6FALSE;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__GNUC__) && !defined(__MINGW32__)
#define GLOBALCONST extern const __attribute__((weak))
#else
#define GLOBALCONST extern const __declspec(selectany)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GLOBALCONST SIMDVectorConstF s_SIMDSinCoefficients0 = { { { -0.16666667f, +0.0083333310f, -0.00019840874f, +2.7525562e-06f } } };
GLOBALCONST SIMDVectorConstF s_SIMDSinCoefficients1 = { { { -2.3889859e-08f, -0.16665852f /*Est1*/, +0.0083139502f /*Est2*/, -0.00018524670f /*Est3*/ } } };
GLOBALCONST SIMDVectorConstF s_SIMDCosCoefficients0 = { { { -0.5f, +0.041666638f, -0.0013888378f, +2.4760495e-05f } } };
GLOBALCONST SIMDVectorConstF s_SIMDCosCoefficients1 = { { { -2.6051615e-07f, -0.49992746f /*Est1*/, +0.041493919f /*Est2*/, -0.0012712436f /*Est3*/ } } };
GLOBALCONST SIMDVectorConstF s_SIMDTanCoefficients0 = { { { 1.0f, 0.333333333f, 0.133333333f, 5.396825397e-2f } } };
GLOBALCONST SIMDVectorConstF s_SIMDTanCoefficients1 = { { { 2.186948854e-2f, 8.863235530e-3f, 3.592128167e-3f, 1.455834485e-3f } } };
GLOBALCONST SIMDVectorConstF s_SIMDTanCoefficients2 = { { { 5.900274264e-4f, 2.391290764e-4f, 9.691537707e-5f, 3.927832950e-5f } } };
GLOBALCONST SIMDVectorConstF s_SIMDArcCoefficients0 = { { { +1.5707963050f, -0.2145988016f, +0.0889789874f, -0.0501743046f } } };
GLOBALCONST SIMDVectorConstF s_SIMDArcCoefficients1 = { { { +0.0308918810f, -0.0170881256f, +0.0066700901f, -0.0012624911f } } };
GLOBALCONST SIMDVectorConstF s_SIMDATanCoefficients0 = { { { -0.3333314528f, +0.1999355085f, -0.1420889944f, +0.1065626393f } } };
GLOBALCONST SIMDVectorConstF s_SIMDATanCoefficients1 = { { { -0.0752896400f, +0.0429096138f, -0.0161657367f, +0.0028662257f } } };
GLOBALCONST SIMDVectorConstF s_SIMDATanEstCoefficients0 = { { { +0.999866f, +0.999866f, +0.999866f, +0.999866f } } };
GLOBALCONST SIMDVectorConstF s_SIMDATanEstCoefficients1 = { { { -0.3302995f, +0.180141f, -0.085133f, +0.0208351f } } };
GLOBALCONST SIMDVectorConstF s_SIMDTanEstCoefficients = { { { 2.484f, -1.954923183e-1f, 2.467401101f, s_1DIVPI } } };
GLOBALCONST SIMDVectorConstF s_SIMDArcEstCoefficients = { { { +1.5707288f, -0.2121144f, +0.0742610f, -0.0187293f } } };
GLOBALCONST SIMDVectorConstF s_SIMDPiConstants0 = { { { s_PI, s_2PI, s_1DIVPI, s_1DIV2PI } } };
GLOBALCONST SIMDVectorConstF s_SIMDIdentityR0 = { { { 1.0f, 0.0f, 0.0f, 0.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDIdentityR1 = { { { 0.0f, 1.0f, 0.0f, 0.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDIdentityR2 = { { { 0.0f, 0.0f, 1.0f, 0.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDIdentityR3 = { { { 0.0f, 0.0f, 0.0f, 1.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDNegIdentityR0 = { { { -1.0f, 0.0f, 0.0f, 0.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDNegIdentityR1 = { { { 0.0f, -1.0f, 0.0f, 0.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDNegIdentityR2 = { { { 0.0f, 0.0f, -1.0f, 0.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDNegIdentityR3 = { { { 0.0f, 0.0f, 0.0f, -1.0f } } };
GLOBALCONST SIMDVectorConstU s_SIMDNegativeZero = { { { 0x80000000, 0x80000000, 0x80000000, 0x80000000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDNegate3 = { { { 0x80000000, 0x80000000, 0x80000000, 0x00000000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDMaskXY = { { { 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDMask3 = { { { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDMaskX = { { { 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDMaskY = { { { 0x00000000, 0xFFFFFFFF, 0x00000000, 0x00000000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDMaskZ = { { { 0x00000000, 0x00000000, 0xFFFFFFFF, 0x00000000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDMaskW = { { { 0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFF } } };
GLOBALCONST SIMDVectorConstF s_SIMDOne = { { { 1.0f, 1.0f, 1.0f, 1.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDOne3 = { { { 1.0f, 1.0f, 1.0f, 0.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDZero = { { { 0.0f, 0.0f, 0.0f, 0.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDTwo = { { { 2.f, 2.f, 2.f, 2.f } } };
GLOBALCONST SIMDVectorConstF s_SIMDFour = { { { 4.f, 4.f, 4.f, 4.f } } };
GLOBALCONST SIMDVectorConstF s_SIMDSix = { { { 6.f, 6.f, 6.f, 6.f } } };
GLOBALCONST SIMDVectorConstF s_SIMDNegativeOne = { { { -1.0f, -1.0f, -1.0f, -1.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDOneHalf = { { { 0.5f, 0.5f, 0.5f, 0.5f } } };
GLOBALCONST SIMDVectorConstF s_SIMDNegativeOneHalf = { { { -0.5f, -0.5f, -0.5f, -0.5f } } };
GLOBALCONST SIMDVectorConstF s_SIMDNegativeTwoPi = { { { -s_2PI, -s_2PI, -s_2PI, -s_2PI } } };
GLOBALCONST SIMDVectorConstF s_SIMDNegativePi = { { { -s_PI, -s_PI, -s_PI, -s_PI } } };
GLOBALCONST SIMDVectorConstF s_SIMDHalfPi = { { { s_PIDIV2, s_PIDIV2, s_PIDIV2, s_PIDIV2 } } };
GLOBALCONST SIMDVectorConstF s_SIMDPi = { { { s_PI, s_PI, s_PI, s_PI } } };
GLOBALCONST SIMDVectorConstF s_SIMDReciprocalPi = { { { s_1DIVPI, s_1DIVPI, s_1DIVPI, s_1DIVPI } } };
GLOBALCONST SIMDVectorConstF s_SIMDTwoPi = { { { s_2PI, s_2PI, s_2PI, s_2PI } } };
GLOBALCONST SIMDVectorConstF s_SIMDReciprocalTwoPi = { { { s_1DIV2PI, s_1DIV2PI, s_1DIV2PI, s_1DIV2PI } } };
GLOBALCONST SIMDVectorConstF s_SIMDEpsilon = { { { 1.192092896e-7f, 1.192092896e-7f, 1.192092896e-7f, 1.192092896e-7f } } };
GLOBALCONST SIMDVectorConstI s_SIMDInfinity = { { { 0x7F800000, 0x7F800000, 0x7F800000, 0x7F800000 } } };
GLOBALCONST SIMDVectorConstI s_SIMDQNaN = { { { 0x7FC00000, 0x7FC00000, 0x7FC00000, 0x7FC00000 } } };
GLOBALCONST SIMDVectorConstI s_SIMDQNaNTest = { { { 0x007FFFFF, 0x007FFFFF, 0x007FFFFF, 0x007FFFFF } } };
GLOBALCONST SIMDVectorConstI s_SIMDAbsMask = { { { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF } } };
GLOBALCONST SIMDVectorConstI s_SIMDFltMin = { { { 0x00800000, 0x00800000, 0x00800000, 0x00800000 } } };
GLOBALCONST SIMDVectorConstI s_SIMDFltMax = { { { 0x7F7FFFFF, 0x7F7FFFFF, 0x7F7FFFFF, 0x7F7FFFFF } } };
GLOBALCONST SIMDVectorConstU s_SIMDNegOneMask = { { { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF } } };
GLOBALCONST SIMDVectorConstU s_SIMDMaskA8R8G8B8 = { { { 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDFlipA8R8G8B8 = { { { 0x00000000, 0x00000000, 0x00000000, 0x80000000 } } };
GLOBALCONST SIMDVectorConstF s_SIMDFixAA8R8G8B8 = { { { 0.0f, 0.0f, 0.0f, f32(0x80000000U) } } };
GLOBALCONST SIMDVectorConstF s_SIMDNormalizeA8R8G8B8 = { { { 1.0f / (255.0f * f32(0x10000)), 1.0f / (255.0f * f32(0x100)), 1.0f / 255.0f, 1.0f / (255.0f * f32(0x1000000)) } } };
GLOBALCONST SIMDVectorConstU s_SIMDMaskA2B10G10R10 = { { { 0x000003FF, 0x000FFC00, 0x3FF00000, 0xC0000000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDFlipA2B10G10R10 = { { { 0x00000200, 0x00080000, 0x20000000, 0x80000000 } } };
GLOBALCONST SIMDVectorConstF s_SIMDFixAA2B10G10R10 = { { { -512.0f, -512.0f * f32(0x400), -512.0f * f32(0x100000), f32(0x80000000U) } } };
GLOBALCONST SIMDVectorConstF s_SIMDNormalizeA2B10G10R10 = { { { 1.0f / 511.0f, 1.0f / (511.0f * f32(0x400)), 1.0f / (511.0f * f32(0x100000)), 1.0f / (3.0f * f32(0x40000000)) } } };
GLOBALCONST SIMDVectorConstU s_SIMDMaskX16Y16 = { { { 0x0000FFFF, 0xFFFF0000, 0x00000000, 0x00000000 } } };
GLOBALCONST SIMDVectorConstI s_SIMDFlipX16Y16 = { { { 0x00008000, 0x00000000, 0x00000000, 0x00000000 } } };
GLOBALCONST SIMDVectorConstF s_SIMDFixX16Y16 = { { { -32768.0f, 0.0f, 0.0f, 0.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDNormalizeX16Y16 = { { { 1.0f / 32767.0f, 1.0f / (32767.0f * 65536.0f), 0.0f, 0.0f } } };
GLOBALCONST SIMDVectorConstU s_SIMDMaskX16Y16Z16W16 = { { { 0x0000FFFF, 0x0000FFFF, 0xFFFF0000, 0xFFFF0000 } } };
GLOBALCONST SIMDVectorConstI s_SIMDFlipX16Y16Z16W16 = { { { 0x00008000, 0x00008000, 0x00000000, 0x00000000 } } };
GLOBALCONST SIMDVectorConstF s_SIMDFixX16Y16Z16W16 = { { { -32768.0f, -32768.0f, 0.0f, 0.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDNormalizeX16Y16Z16W16 = { { { 1.0f / 32767.0f, 1.0f / 32767.0f, 1.0f / (32767.0f * 65536.0f), 1.0f / (32767.0f * 65536.0f) } } };
GLOBALCONST SIMDVectorConstF s_SIMDNoFraction = { { { 8388608.0f, 8388608.0f, 8388608.0f, 8388608.0f } } };
GLOBALCONST SIMDVectorConstI s_SIMDMaskByte = { { { 0x000000FF, 0x000000FF, 0x000000FF, 0x000000FF } } };
GLOBALCONST SIMDVectorConstF s_SIMDNegateX = { { { -1.0f, 1.0f, 1.0f, 1.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDNegateY = { { { 1.0f, -1.0f, 1.0f, 1.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDNegateZ = { { { 1.0f, 1.0f, -1.0f, 1.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDNegateW = { { { 1.0f, 1.0f, 1.0f, -1.0f } } };
GLOBALCONST SIMDVectorConstU s_SIMDSelect0101 = { { { s_SELECT_0, s_SELECT_1, s_SELECT_0, s_SELECT_1 } } };
GLOBALCONST SIMDVectorConstU s_SIMDSelect1010 = { { { s_SELECT_1, s_SELECT_0, s_SELECT_1, s_SELECT_0 } } };
GLOBALCONST SIMDVectorConstI s_SIMDOneHalfMinusEpsilon = { { { 0x3EFFFFFD, 0x3EFFFFFD, 0x3EFFFFFD, 0x3EFFFFFD } } };
GLOBALCONST SIMDVectorConstU s_SIMDSelect1000 = { { { s_SELECT_1, s_SELECT_0, s_SELECT_0, s_SELECT_0 } } };
GLOBALCONST SIMDVectorConstU s_SIMDSelect1100 = { { { s_SELECT_1, s_SELECT_1, s_SELECT_0, s_SELECT_0 } } };
GLOBALCONST SIMDVectorConstU s_SIMDSelect1110 = { { { s_SELECT_1, s_SELECT_1, s_SELECT_1, s_SELECT_0 } } };
GLOBALCONST SIMDVectorConstU s_SIMDSelect1011 = { { { s_SELECT_1, s_SELECT_0, s_SELECT_1, s_SELECT_1 } } };
GLOBALCONST SIMDVectorConstF s_SIMDFixupY16 = { { { 1.0f, 1.0f / 65536.0f, 0.0f, 0.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDFixupY16W16 = { { { 1.0f, 1.0f, 1.0f / 65536.0f, 1.0f / 65536.0f } } };
GLOBALCONST SIMDVectorConstU s_SIMDFlipY = { { { 0, 0x80000000, 0, 0 } } };
GLOBALCONST SIMDVectorConstU s_SIMDFlipZ = { { { 0, 0, 0x80000000, 0 } } };
GLOBALCONST SIMDVectorConstU s_SIMDFlipW = { { { 0, 0, 0, 0x80000000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDFlipYZ = { { { 0, 0x80000000, 0x80000000, 0 } } };
GLOBALCONST SIMDVectorConstU s_SIMDFlipZW = { { { 0, 0, 0x80000000, 0x80000000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDFlipYW = { { { 0, 0x80000000, 0, 0x80000000 } } };
GLOBALCONST SIMDVectorConstI s_SIMDMaskDec4 = { { { 0x3FF, 0x3FF << 10, 0x3FF << 20, static_cast<i32>(0xC0000000) } } };
GLOBALCONST SIMDVectorConstI s_SIMDXorDec4 = { { { 0x200, 0x200 << 10, 0x200 << 20, 0 } } };
GLOBALCONST SIMDVectorConstF s_SIMDAddUDec4 = { { { 0, 0, 0, 32768.0f * 65536.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDAddDec4 = { { { -512.0f, -512.0f * 1024.0f, -512.0f * 1024.0f * 1024.0f, 0 } } };
GLOBALCONST SIMDVectorConstF s_SIMDMulDec4 = { { { 1.0f, 1.0f / 1024.0f, 1.0f / (1024.0f * 1024.0f), 1.0f / (1024.0f * 1024.0f * 1024.0f) } } };
GLOBALCONST SIMDVectorConstU s_SIMDMaskByte4 = { { { 0xFF, 0xFF00, 0xFF0000, 0xFF000000 } } };
GLOBALCONST SIMDVectorConstI s_SIMDXorByte4 = { { { 0x80, 0x8000, 0x800000, 0x00000000 } } };
GLOBALCONST SIMDVectorConstF s_SIMDAddByte4 = { { { -128.0f, -128.0f * 256.0f, -128.0f * 65536.0f, 0 } } };
GLOBALCONST SIMDVectorConstF s_SIMDFixUnsigned = { { { 32768.0f * 65536.0f, 32768.0f * 65536.0f, 32768.0f * 65536.0f, 32768.0f * 65536.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDMaxInt = { { { 65536.0f * 32768.0f - 128.0f, 65536.0f * 32768.0f - 128.0f, 65536.0f * 32768.0f - 128.0f, 65536.0f * 32768.0f - 128.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDMaxUInt = { { { 65536.0f * 65536.0f - 256.0f, 65536.0f * 65536.0f - 256.0f, 65536.0f * 65536.0f - 256.0f, 65536.0f * 65536.0f - 256.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDUnsignedFix = { { { 32768.0f * 65536.0f, 32768.0f * 65536.0f, 32768.0f * 65536.0f, 32768.0f * 65536.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDsrgbScale = { { { 12.92f, 12.92f, 12.92f, 1.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDsrgbA = { { { 0.055f, 0.055f, 0.055f, 0.0f } } };
GLOBALCONST SIMDVectorConstF s_SIMDsrgbA1 = { { { 1.055f, 1.055f, 1.055f, 1.0f } } };
GLOBALCONST SIMDVectorConstI s_SIMDExponentBias = { { { 127, 127, 127, 127 } } };
GLOBALCONST SIMDVectorConstI s_SIMDSubnormalExponent = { { { -126, -126, -126, -126 } } };
GLOBALCONST SIMDVectorConstI s_SIMDNumTrailing = { { { 23, 23, 23, 23 } } };
GLOBALCONST SIMDVectorConstI s_SIMDMinNormal = { { { 0x00800000, 0x00800000, 0x00800000, 0x00800000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDNegInfinity = { { { 0xFF800000, 0xFF800000, 0xFF800000, 0xFF800000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDNegQNaN = { { { 0xFFC00000, 0xFFC00000, 0xFFC00000, 0xFFC00000 } } };
GLOBALCONST SIMDVectorConstI s_SIMDBin128 = { { { 0x43000000, 0x43000000, 0x43000000, 0x43000000 } } };
GLOBALCONST SIMDVectorConstU s_SIMDBinNeg150 = { { { 0xC3160000, 0xC3160000, 0xC3160000, 0xC3160000 } } };
GLOBALCONST SIMDVectorConstI s_SIMD253 = { { { 253, 253, 253, 253 } } };
GLOBALCONST SIMDVectorConstF s_SIMDExpEst1 = { { { -6.93147182e-1f, -6.93147182e-1f, -6.93147182e-1f, -6.93147182e-1f } } };
GLOBALCONST SIMDVectorConstF s_SIMDExpEst2 = { { { +2.40226462e-1f, +2.40226462e-1f, +2.40226462e-1f, +2.40226462e-1f } } };
GLOBALCONST SIMDVectorConstF s_SIMDExpEst3 = { { { -5.55036440e-2f, -5.55036440e-2f, -5.55036440e-2f, -5.55036440e-2f } } };
GLOBALCONST SIMDVectorConstF s_SIMDExpEst4 = { { { +9.61597636e-3f, +9.61597636e-3f, +9.61597636e-3f, +9.61597636e-3f } } };
GLOBALCONST SIMDVectorConstF s_SIMDExpEst5 = { { { -1.32823968e-3f, -1.32823968e-3f, -1.32823968e-3f, -1.32823968e-3f } } };
GLOBALCONST SIMDVectorConstF s_SIMDExpEst6 = { { { +1.47491097e-4f, +1.47491097e-4f, +1.47491097e-4f, +1.47491097e-4f } } };
GLOBALCONST SIMDVectorConstF s_SIMDExpEst7 = { { { -1.08635004e-5f, -1.08635004e-5f, -1.08635004e-5f, -1.08635004e-5f } } };
GLOBALCONST SIMDVectorConstF s_SIMDLogEst0 = { { { +1.442693f, +1.442693f, +1.442693f, +1.442693f } } };
GLOBALCONST SIMDVectorConstF s_SIMDLogEst1 = { { { -0.721242f, -0.721242f, -0.721242f, -0.721242f } } };
GLOBALCONST SIMDVectorConstF s_SIMDLogEst2 = { { { +0.479384f, +0.479384f, +0.479384f, +0.479384f } } };
GLOBALCONST SIMDVectorConstF s_SIMDLogEst3 = { { { -0.350295f, -0.350295f, -0.350295f, -0.350295f } } };
GLOBALCONST SIMDVectorConstF s_SIMDLogEst4 = { { { +0.248590f, +0.248590f, +0.248590f, +0.248590f } } };
GLOBALCONST SIMDVectorConstF s_SIMDLogEst5 = { { { -0.145700f, -0.145700f, -0.145700f, -0.145700f } } };
GLOBALCONST SIMDVectorConstF s_SIMDLogEst6 = { { { +0.057148f, +0.057148f, +0.057148f, +0.057148f } } };
GLOBALCONST SIMDVectorConstF s_SIMDLogEst7 = { { { -0.010578f, -0.010578f, -0.010578f, -0.010578f } } };
GLOBALCONST SIMDVectorConstF s_SIMDLgE = { { { +1.442695f, +1.442695f, +1.442695f, +1.442695f } } };
GLOBALCONST SIMDVectorConstF s_SIMDInvLgE = { { { +6.93147182e-1f, +6.93147182e-1f, +6.93147182e-1f, +6.93147182e-1f } } };
GLOBALCONST SIMDVectorConstF s_SIMDLg10 = { { { +3.321928f, +3.321928f, +3.321928f, +3.321928f } } };
GLOBALCONST SIMDVectorConstF s_SIMDInvLg10 = { { { +3.010299956e-1f, +3.010299956e-1f, +3.010299956e-1f, +3.010299956e-1f } } };
GLOBALCONST SIMDVectorConstF g_SIMDUByteMax = { { { 255.0f, 255.0f, 255.0f, 255.0f } } };
GLOBALCONST SIMDVectorConstF g_SIMDByteMin = { { { -127.0f, -127.0f, -127.0f, -127.0f } } };
GLOBALCONST SIMDVectorConstF g_SIMDByteMax = { { { 127.0f, 127.0f, 127.0f, 127.0f } } };
GLOBALCONST SIMDVectorConstF g_SIMDShortMin = { { { -32767.0f, -32767.0f, -32767.0f, -32767.0f } } };
GLOBALCONST SIMDVectorConstF g_SIMDShortMax = { { { 32767.0f, 32767.0f, 32767.0f, 32767.0f } } };
GLOBALCONST SIMDVectorConstF g_SIMDUShortMax = { { { 65535.0f, 65535.0f, 65535.0f, 65535.0f } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef GLOBALCONST


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

