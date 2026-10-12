// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(16) SIMDVectorConstF{
    union{
        f32 f[4];
        SIMDVectorSingle v;
    };

    NWB_INLINE operator const f32*()const noexcept{ return f; }
    NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS
};

struct alignas(32) SIMDVectorDoubleConstF{
    union{
        f64 f[4];
        SIMDVectorDouble v;
    };

    NWB_INLINE operator const f64*()const noexcept{ return f; }
    NWB_SIMD_VECTOR_DOUBLE_CONST_CONVERSION
};

struct alignas(16) SIMDVectorConstI{
    union{
        i32 i[4];
        SIMDVectorSingle v;
    };

    NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS
};

struct alignas(32) SIMDVectorDoubleConstI{
    union{
        i64 i[4];
        SIMDVectorDouble v;
    };

    NWB_SIMD_VECTOR_DOUBLE_CONST_CONVERSION
};

struct alignas(16) SIMDVectorConstU{
    union{
        u32 u[4];
        SIMDVectorSingle v;
    };

    NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS
};

struct alignas(32) SIMDVectorDoubleConstU{
    union{
        u64 u[4];
        SIMDVectorDouble v;
    };

    NWB_SIMD_VECTOR_DOUBLE_CONST_CONVERSION
};

struct alignas(16) SIMDVectorConstB{
    union{
        u8 b[16];
        SIMDVectorSingle v;
    };

    NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr f32 s_PI = 3.141592654f;
inline constexpr f64 s_DoublePI = 0x1.921fb54442d18p+1;
constexpr f32 s_2PI = 6.283185307f;
inline constexpr f64 s_Double2PI = 0x1.921fb54442d18p+2;
constexpr f32 s_1DIVPI = 0.318309886f;
inline constexpr f64 s_Double1DIVPI = 0x1.45f306dc9c883p-2;
constexpr f32 s_1DIV2PI = 0.159154943f;
inline constexpr f64 s_Double1DIV2PI = 0x1.45f306dc9c883p-3;
constexpr f32 s_PIDIV2 = 1.570796327f;
inline constexpr f64 s_DoublePIDIV2 = 0x1.921fb54442d18p+0;
constexpr f32 s_PIDIV4 = 0.785398163f;
inline constexpr f64 s_DoublePIDIV4 = 0x1.921fb54442d18p-1;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_SELECT_0 = 0x00000000;
constexpr u64 s_DoubleSELECT_0 = 0x00000000;
constexpr u32 s_SELECT_1 = 0xFFFFFFFF;
constexpr u64 s_DoubleSELECT_1 = 0xFFFFFFFFFFFFFFFFull;

constexpr u32 s_PERMUTE_0X = 0;
constexpr u32 s_DoublePERMUTE_0X = 0;
constexpr u32 s_PERMUTE_0Y = 1;
constexpr u32 s_DoublePERMUTE_0Y = 1;
constexpr u32 s_PERMUTE_0Z = 2;
constexpr u32 s_DoublePERMUTE_0Z = 2;
constexpr u32 s_PERMUTE_0W = 3;
constexpr u32 s_DoublePERMUTE_0W = 3;
constexpr u32 s_PERMUTE_1X = 4;
constexpr u32 s_DoublePERMUTE_1X = 4;
constexpr u32 s_PERMUTE_1Y = 5;
constexpr u32 s_DoublePERMUTE_1Y = 5;
constexpr u32 s_PERMUTE_1Z = 6;
constexpr u32 s_DoublePERMUTE_1Z = 6;
constexpr u32 s_PERMUTE_1W = 7;
constexpr u32 s_DoublePERMUTE_1W = 7;

constexpr u32 s_SWIZZLE_X = 0;
constexpr u32 s_DoubleSWIZZLE_X = 0;
constexpr u32 s_SWIZZLE_Y = 1;
constexpr u32 s_DoubleSWIZZLE_Y = 1;
constexpr u32 s_SWIZZLE_Z = 2;
constexpr u32 s_DoubleSWIZZLE_Z = 2;
constexpr u32 s_SWIZZLE_W = 3;
constexpr u32 s_DoubleSWIZZLE_W = 3;

constexpr u32 s_CRMASK_CR6TRUE = 0x00000080;
constexpr u32 s_CRMASK_CR6FALSE = 0x00000020;
constexpr u32 s_CRMASK_CR6BOUNDS = s_CRMASK_CR6FALSE;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDSinCoefficients0 = { { { -0.16666667f, +0.0083333310f, -0.00019840874f, +2.7525562e-06f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDSinCoefficients1 = { { { -2.3889859e-08f, -0.16665852f /*Est1*/, +0.0083139502f /*Est2*/, -0.00018524670f /*Est3*/ } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDCosCoefficients0 = { { { -0.5f, +0.041666638f, -0.0013888378f, +2.4760495e-05f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDCosCoefficients1 = { { { -2.6051615e-07f, -0.49992746f /*Est1*/, +0.041493919f /*Est2*/, -0.0012712436f /*Est3*/ } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDTanCoefficients0 = { { { 1.0f, 0.333333333f, 0.133333333f, 5.396825397e-2f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDTanCoefficients1 = { { { 2.186948854e-2f, 8.863235530e-3f, 3.592128167e-3f, 1.455834485e-3f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDTanCoefficients2 = { { { 5.900274264e-4f, 2.391290764e-4f, 9.691537707e-5f, 3.927832950e-5f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDArcCoefficients0 = { { { +1.5707963050f, -0.2145988016f, +0.0889789874f, -0.0501743046f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDArcCoefficients1 = { { { +0.0308918810f, -0.0170881256f, +0.0066700901f, -0.0012624911f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDATanCoefficients0 = { { { -0.3333314528f, +0.1999355085f, -0.1420889944f, +0.1065626393f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDATanCoefficients1 = { { { -0.0752896400f, +0.0429096138f, -0.0161657367f, +0.0028662257f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDATanEstCoefficients0 = { { { +0.999866f, +0.999866f, +0.999866f, +0.999866f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDATanEstCoefficients1 = { { { -0.3302995f, +0.180141f, -0.085133f, +0.0208351f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDTanEstCoefficients = { { { 2.484f, -1.954923183e-1f, 2.467401101f, s_1DIVPI } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDArcEstCoefficients = { { { +1.5707288f, -0.2121144f, +0.0742610f, -0.0187293f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDPiConstants0 = { { { s_PI, s_2PI, s_1DIVPI, s_1DIV2PI } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDIdentityR0 = { { { 1.0f, 0.0f, 0.0f, 0.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDIdentityR0 = { { { 1.0, 0.0, 0.0, 0.0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDIdentityR1 = { { { 0.0f, 1.0f, 0.0f, 0.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDIdentityR1 = { { { 0.0, 1.0, 0.0, 0.0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDIdentityR2 = { { { 0.0f, 0.0f, 1.0f, 0.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDIdentityR2 = { { { 0.0, 0.0, 1.0, 0.0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDIdentityR3 = { { { 0.0f, 0.0f, 0.0f, 1.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDIdentityR3 = { { { 0.0, 0.0, 0.0, 1.0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegIdentityR0 = { { { -1.0f, 0.0f, 0.0f, 0.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegIdentityR1 = { { { 0.0f, -1.0f, 0.0f, 0.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegIdentityR2 = { { { 0.0f, 0.0f, -1.0f, 0.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegIdentityR3 = { { { 0.0f, 0.0f, 0.0f, -1.0f } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDNegativeZero = { { { 0x80000000, 0x80000000, 0x80000000, 0x80000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDNegativeZero = { { { 0x8000000000000000, 0x8000000000000000, 0x8000000000000000, 0x8000000000000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDNegate3 = { { { 0x80000000, 0x80000000, 0x80000000, 0x00000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDNegate3 = { { { 0x8000000000000000, 0x8000000000000000, 0x8000000000000000, 0x00000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDMaskXY = { { { 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDMaskXY = { { { 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0x00000000, 0x00000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDMask3 = { { { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDMask3 = { { { 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0x00000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDMaskX = { { { 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDMaskX = { { { 0xFFFFFFFFFFFFFFFF, 0x00000000, 0x00000000, 0x00000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDMaskY = { { { 0x00000000, 0xFFFFFFFF, 0x00000000, 0x00000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDMaskY = { { { 0x00000000, 0xFFFFFFFFFFFFFFFF, 0x00000000, 0x00000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDMaskZ = { { { 0x00000000, 0x00000000, 0xFFFFFFFF, 0x00000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDMaskZ = { { { 0x00000000, 0x00000000, 0xFFFFFFFFFFFFFFFF, 0x00000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDMaskW = { { { 0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFF } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDMaskW = { { { 0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFFFFFFFFFF } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDOne = { { { 1.0f, 1.0f, 1.0f, 1.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDOne = { { { 1.0, 1.0, 1.0, 1.0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDOne3 = { { { 1.0f, 1.0f, 1.0f, 0.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDOne3 = { { { 1.0, 1.0, 1.0, 0.0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDZero = { { { 0.0f, 0.0f, 0.0f, 0.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDZero = { { { 0.0, 0.0, 0.0, 0.0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDTwo = { { { 2.f, 2.f, 2.f, 2.f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDTwo = { { { 2., 2., 2., 2. } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDFour = { { { 4.f, 4.f, 4.f, 4.f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDFour = { { { 4., 4., 4., 4. } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDSix = { { { 6.f, 6.f, 6.f, 6.f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegativeOne = { { { -1.0f, -1.0f, -1.0f, -1.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDNegativeOne = { { { -1.0, -1.0, -1.0, -1.0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDOneHalf = { { { 0.5f, 0.5f, 0.5f, 0.5f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDOneHalf = { { { 0.5, 0.5, 0.5, 0.5 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegativeOneHalf = { { { -0.5f, -0.5f, -0.5f, -0.5f } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegativeTwoPi = { { { -s_2PI, -s_2PI, -s_2PI, -s_2PI } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDNegativeTwoPi = { { { -s_Double2PI, -s_Double2PI, -s_Double2PI, -s_Double2PI } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegativePi = { { { -s_PI, -s_PI, -s_PI, -s_PI } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDNegativePi = { { { -s_DoublePI, -s_DoublePI, -s_DoublePI, -s_DoublePI } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDHalfPi = { { { s_PIDIV2, s_PIDIV2, s_PIDIV2, s_PIDIV2 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDPi = { { { s_PI, s_PI, s_PI, s_PI } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDPi = { { { s_DoublePI, s_DoublePI, s_DoublePI, s_DoublePI } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDReciprocalPi = { { { s_1DIVPI, s_1DIVPI, s_1DIVPI, s_1DIVPI } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDTwoPi = { { { s_2PI, s_2PI, s_2PI, s_2PI } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDTwoPi = { { { s_Double2PI, s_Double2PI, s_Double2PI, s_Double2PI } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDReciprocalTwoPi = { { { s_1DIV2PI, s_1DIV2PI, s_1DIV2PI, s_1DIV2PI } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDEpsilon = { { { 1.192092896e-7f, 1.192092896e-7f, 1.192092896e-7f, 1.192092896e-7f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDEpsilon = { { { 0x1p-52, 0x1p-52, 0x1p-52, 0x1p-52 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDInfinity = { { { 0x7F800000, 0x7F800000, 0x7F800000, 0x7F800000 } } };
inline const SIMDVectorDoubleConstI s_DoubleSIMDInfinity = { { { 0x7FF0000000000000, 0x7FF0000000000000, 0x7FF0000000000000, 0x7FF0000000000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDQNaN = { { { 0x7FC00000, 0x7FC00000, 0x7FC00000, 0x7FC00000 } } };
inline const SIMDVectorDoubleConstI s_DoubleSIMDQNaN = { { { 0x7FF8000000000000, 0x7FF8000000000000, 0x7FF8000000000000, 0x7FF8000000000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDQNaNTest = { { { 0x007FFFFF, 0x007FFFFF, 0x007FFFFF, 0x007FFFFF } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDAbsMask = { { { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF } } };
inline const SIMDVectorDoubleConstI s_DoubleSIMDAbsMask = { { { 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDFltMin = { { { 0x00800000, 0x00800000, 0x00800000, 0x00800000 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDFltMax = { { { 0x7F7FFFFF, 0x7F7FFFFF, 0x7F7FFFFF, 0x7F7FFFFF } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDNegOneMask = { { { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDMaskA8R8G8B8 = { { { 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDFlipA8R8G8B8 = { { { 0x00000000, 0x00000000, 0x00000000, 0x80000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDFixAA8R8G8B8 = { { { 0.0f, 0.0f, 0.0f, f32(0x80000000U) } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNormalizeA8R8G8B8 = { { { 1.0f / (255.0f * f32(0x10000)), 1.0f / (255.0f * f32(0x100)), 1.0f / 255.0f, 1.0f / (255.0f * f32(0x1000000)) } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDMaskA2B10G10R10 = { { { 0x000003FF, 0x000FFC00, 0x3FF00000, 0xC0000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDFlipA2B10G10R10 = { { { 0x00000200, 0x00080000, 0x20000000, 0x80000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDFixAA2B10G10R10 = { { { -512.0f, -512.0f * f32(0x400), -512.0f * f32(0x100000), f32(0x80000000U) } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNormalizeA2B10G10R10 = { { { 1.0f / 511.0f, 1.0f / (511.0f * f32(0x400)), 1.0f / (511.0f * f32(0x100000)), 1.0f / (3.0f * f32(0x40000000)) } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDMaskX16Y16 = { { { 0x0000FFFF, 0xFFFF0000, 0x00000000, 0x00000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDFlipX16Y16 = { { { 0x00008000, 0x00000000, 0x00000000, 0x00000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDFixX16Y16 = { { { -32768.0f, 0.0f, 0.0f, 0.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNormalizeX16Y16 = { { { 1.0f / 32767.0f, 1.0f / (32767.0f * 65536.0f), 0.0f, 0.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDMaskX16Y16Z16W16 = { { { 0x0000FFFF, 0x0000FFFF, 0xFFFF0000, 0xFFFF0000 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDFlipX16Y16Z16W16 = { { { 0x00008000, 0x00008000, 0x00000000, 0x00000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDFixX16Y16Z16W16 = { { { -32768.0f, -32768.0f, 0.0f, 0.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNormalizeX16Y16Z16W16 = { { { 1.0f / 32767.0f, 1.0f / 32767.0f, 1.0f / (32767.0f * 65536.0f), 1.0f / (32767.0f * 65536.0f) } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNoFraction = { { { 8388608.0f, 8388608.0f, 8388608.0f, 8388608.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDMaskByte = { { { 0x000000FF, 0x000000FF, 0x000000FF, 0x000000FF } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegateX = { { { -1.0f, 1.0f, 1.0f, 1.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegateY = { { { 1.0f, -1.0f, 1.0f, 1.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegateZ = { { { 1.0f, 1.0f, -1.0f, 1.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegateW = { { { 1.0f, 1.0f, 1.0f, -1.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDSelect0101 = { { { s_SELECT_0, s_SELECT_1, s_SELECT_0, s_SELECT_1 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDSelect0101 = { { { s_DoubleSELECT_0, s_DoubleSELECT_1, s_DoubleSELECT_0, s_DoubleSELECT_1 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDSelect1010 = { { { s_SELECT_1, s_SELECT_0, s_SELECT_1, s_SELECT_0 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDOneHalfMinusEpsilon = { { { 0x3EFFFFFD, 0x3EFFFFFD, 0x3EFFFFFD, 0x3EFFFFFD } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDSelect1000 = { { { s_SELECT_1, s_SELECT_0, s_SELECT_0, s_SELECT_0 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDSelect1100 = { { { s_SELECT_1, s_SELECT_1, s_SELECT_0, s_SELECT_0 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDSelect1100 = { { { s_DoubleSELECT_1, s_DoubleSELECT_1, s_DoubleSELECT_0, s_DoubleSELECT_0 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDSelect1110 = { { { s_SELECT_1, s_SELECT_1, s_SELECT_1, s_SELECT_0 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDSelect1110 = { { { s_DoubleSELECT_1, s_DoubleSELECT_1, s_DoubleSELECT_1, s_DoubleSELECT_0 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDSelect1011 = { { { s_SELECT_1, s_SELECT_0, s_SELECT_1, s_SELECT_1 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDFixupY16 = { { { 1.0f, 1.0f / 65536.0f, 0.0f, 0.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDFixupY16W16 = { { { 1.0f, 1.0f, 1.0f / 65536.0f, 1.0f / 65536.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDFlipY = { { { 0, 0x80000000, 0, 0 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDFlipZ = { { { 0, 0, 0x80000000, 0 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDFlipW = { { { 0, 0, 0, 0x80000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDFlipYZ = { { { 0, 0x80000000, 0x80000000, 0 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDFlipZW = { { { 0, 0, 0x80000000, 0x80000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDFlipYW = { { { 0, 0x80000000, 0, 0x80000000 } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDMaskDec4 = { { { 0x3FF, 0x3FF << 10, 0x3FF << 20, static_cast<i32>(0xC0000000) } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDXorDec4 = { { { 0x200, 0x200 << 10, 0x200 << 20, 0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDAddUDec4 = { { { 0, 0, 0, 32768.0f * 65536.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDAddDec4 = { { { -512.0f, -512.0f * 1024.0f, -512.0f * 1024.0f * 1024.0f, 0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDMulDec4 = { { { 1.0f, 1.0f / 1024.0f, 1.0f / (1024.0f * 1024.0f), 1.0f / (1024.0f * 1024.0f * 1024.0f) } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDMaskByte4 = { { { 0xFF, 0xFF00, 0xFF0000, 0xFF000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDXorByte4 = { { { 0x80, 0x8000, 0x800000, 0x00000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDAddByte4 = { { { -128.0f, -128.0f * 256.0f, -128.0f * 65536.0f, 0 } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDFixUnsigned = { { { 32768.0f * 65536.0f, 32768.0f * 65536.0f, 32768.0f * 65536.0f, 32768.0f * 65536.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDMaxInt = { { { 65536.0f * 32768.0f - 128.0f, 65536.0f * 32768.0f - 128.0f, 65536.0f * 32768.0f - 128.0f, 65536.0f * 32768.0f - 128.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDMaxUInt = { { { 65536.0f * 65536.0f - 256.0f, 65536.0f * 65536.0f - 256.0f, 65536.0f * 65536.0f - 256.0f, 65536.0f * 65536.0f - 256.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDUnsignedFix = { { { 32768.0f * 65536.0f, 32768.0f * 65536.0f, 32768.0f * 65536.0f, 32768.0f * 65536.0f } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDsrgbScale = { { { 12.92f, 12.92f, 12.92f, 1.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDsrgbA = { { { 0.055f, 0.055f, 0.055f, 0.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDsrgbA1 = { { { 1.055f, 1.055f, 1.055f, 1.0f } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDExponentBias = { { { 127, 127, 127, 127 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDSubnormalExponent = { { { -126, -126, -126, -126 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDNumTrailing = { { { 23, 23, 23, 23 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDMinNormal = { { { 0x00800000, 0x00800000, 0x00800000, 0x00800000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDNegInfinity = { { { 0xFF800000, 0xFF800000, 0xFF800000, 0xFF800000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDNegQNaN = { { { 0xFFC00000, 0xFFC00000, 0xFFC00000, 0xFFC00000 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDBin128 = { { { 0x43000000, 0x43000000, 0x43000000, 0x43000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDBinNeg150 = { { { 0xC3160000, 0xC3160000, 0xC3160000, 0xC3160000 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMD253 = { { { 253, 253, 253, 253 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDExpEst1 = { { { -6.93147182e-1f, -6.93147182e-1f, -6.93147182e-1f, -6.93147182e-1f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDExpEst2 = { { { +2.40226462e-1f, +2.40226462e-1f, +2.40226462e-1f, +2.40226462e-1f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDExpEst3 = { { { -5.55036440e-2f, -5.55036440e-2f, -5.55036440e-2f, -5.55036440e-2f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDExpEst4 = { { { +9.61597636e-3f, +9.61597636e-3f, +9.61597636e-3f, +9.61597636e-3f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDExpEst5 = { { { -1.32823968e-3f, -1.32823968e-3f, -1.32823968e-3f, -1.32823968e-3f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDExpEst6 = { { { +1.47491097e-4f, +1.47491097e-4f, +1.47491097e-4f, +1.47491097e-4f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDExpEst7 = { { { -1.08635004e-5f, -1.08635004e-5f, -1.08635004e-5f, -1.08635004e-5f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDLogEst0 = { { { +1.442693f, +1.442693f, +1.442693f, +1.442693f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDLogEst1 = { { { -0.721242f, -0.721242f, -0.721242f, -0.721242f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDLogEst2 = { { { +0.479384f, +0.479384f, +0.479384f, +0.479384f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDLogEst3 = { { { -0.350295f, -0.350295f, -0.350295f, -0.350295f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDLogEst4 = { { { +0.248590f, +0.248590f, +0.248590f, +0.248590f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDLogEst5 = { { { -0.145700f, -0.145700f, -0.145700f, -0.145700f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDLogEst6 = { { { +0.057148f, +0.057148f, +0.057148f, +0.057148f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDLogEst7 = { { { -0.010578f, -0.010578f, -0.010578f, -0.010578f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDLgE = { { { +1.442695f, +1.442695f, +1.442695f, +1.442695f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDInvLgE = { { { +6.93147182e-1f, +6.93147182e-1f, +6.93147182e-1f, +6.93147182e-1f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDLg10 = { { { +3.321928f, +3.321928f, +3.321928f, +3.321928f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDInvLg10 = { { { +3.010299956e-1f, +3.010299956e-1f, +3.010299956e-1f, +3.010299956e-1f } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDUByteMax = { { { 255.0f, 255.0f, 255.0f, 255.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDByteMin = { { { -127.0f, -127.0f, -127.0f, -127.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDByteMax = { { { 127.0f, 127.0f, 127.0f, 127.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDShortMin = { { { -32767.0f, -32767.0f, -32767.0f, -32767.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDShortMax = { { { 32767.0f, 32767.0f, 32767.0f, 32767.0f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDUShortMax = { { { 65535.0f, 65535.0f, 65535.0f, 65535.0f } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

