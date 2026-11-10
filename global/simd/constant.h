// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(16) SIMDVectorConstF{
    using ScalarArray = f32[4];

    union{
        f32 f[4];
        SIMDVectorSingle v;
    };

    NWB_INLINE operator const ScalarArray&()const noexcept{ return f; }
    NWB_SIMD_VECTOR_CONST_SELECTED_CONVERSIONS
};

struct alignas(32) SIMDVectorDoubleConstF{
    using ScalarArray = f64[4];

    union{
        f64 f[4];
        SIMDVectorDouble v;
    };

    NWB_INLINE operator const ScalarArray&()const noexcept{ return f; }
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr f32 s_PI = 3.141592654f;
inline constexpr f64 s_DoublePI = 0x1.921fb54442d18p+1;
constexpr f32 s_2PI = 6.283185307f;
inline constexpr f64 s_Double2PI = 0x1.921fb54442d18p+2;
constexpr f32 s_1DIV2PI = 0.159154943f;
constexpr f32 s_PIDIV2 = 1.570796327f;
inline constexpr f64 s_DoublePIDIV2 = 0x1.921fb54442d18p+0;
constexpr f32 s_PIDIV4 = 0.785398163f;
inline constexpr f64 s_DoublePIDIV4 = 0x1.921fb54442d18p-1;
constexpr f32 s_3PIDIV4 = 2.3561944905f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// IEEE-754 binary32/binary64 bit layout.
inline constexpr u32 s_F32SignBitMask = 0x80000000u;
inline constexpr u32 s_F32AbsoluteBitMask = 0x7fffffffu;
inline constexpr u32 s_F32SignBitShift = 31u;
inline constexpr u32 s_F32BitCount = 32u;
inline constexpr u32 s_F32InfinityBits = 0x7f800000u;
inline constexpr u32 s_F32QuietNaNBits = 0x7fc00000u;
inline constexpr u32 s_F32QuietBit = 0x00400000u;
inline constexpr u32 s_F32MinNormalBits = 0x00800000u;
inline constexpr u32 s_F32FractionLimitBits = 0x4affffffu;
inline constexpr u32 s_F32MantissaBitCount = 23u;
inline constexpr u32 s_F32ImplicitMantissaBit = 0x800000u;
inline constexpr u32 s_F32MantissaBitMask = 0x7fffffu;
inline constexpr u32 s_F32ExponentFieldMask = 0xffu;
inline constexpr u32 s_F32ExponentBias = 127u;
inline constexpr u32 s_F32IntegerExponent = s_F32ExponentBias + s_F32MantissaBitCount;
inline constexpr u32 s_F32HalfBits = 0x3f000000u;
inline constexpr u32 s_F32OneBits = 0x3f800000u;
inline constexpr u32 s_F32NegativeOneBits = 0xbf800000u;
inline constexpr f32 s_F32NoFractionThreshold = 8388608.0f;
inline constexpr u64 s_F64SignBitMask = 0x8000000000000000ull;
inline constexpr u64 s_F64AbsoluteBitMask = 0x7fffffffffffffffull;
inline constexpr u32 s_F64SignBitShift = 63u;
inline constexpr u64 s_F64MinNormalBits = 0x0010000000000000ull;
inline constexpr u32 s_F64MantissaBitCount = 52u;
inline constexpr u64 s_F64MantissaBitMask = 0x000fffffffffffffull;
inline constexpr u64 s_F64ExponentFieldMask = 0x7ffull;
inline constexpr u64 s_F64HalfBits = 0x3fe0000000000000ull;
inline constexpr u64 s_F64OneBits = 0x3ff0000000000000ull;
inline constexpr i64 s_F64ExponentBias = 1023ll;
inline constexpr u64 s_F64IntegerExponent = static_cast<u64>(s_F64ExponentBias) + s_F64MantissaBitCount;
inline constexpr f64 s_F64Epsilon = 0x1p-52;

inline constexpr i32 s_SIMDMovemask2Low = 0x3;
inline constexpr i32 s_SIMDSse4InsertLaneYMask = 0x10;
inline constexpr i32 s_SIMDSse4InsertLaneZMask = 0x20;
inline constexpr i32 s_SIMDSse4InsertLaneWMask = 0x30;
inline constexpr i32 s_SIMDSse4Dot2BroadcastMask = 0x3F;
inline constexpr i32 s_SIMDSse4Dot2TransformMask = 0xBF;
inline constexpr i32 s_SIMDSse4Dot3BroadcastMask = 0x7F;
inline constexpr i32 s_SIMDSse4Dot4BroadcastMask = 0xFF;
inline constexpr i32 s_SIMDAvxPermute2F128LowLow = 0x00;
inline constexpr i32 s_SIMDAvxPermute2F128HighHigh = 0x11;
inline constexpr i32 s_SIMDAvxPermute2F128Low0Low1 = 0x20;
inline constexpr i32 s_SIMDAvxPermute2F128High0High1 = 0x31;
inline constexpr i32 s_SIMDAvxPermutePdAllLow = 0x0;
inline constexpr i32 s_SIMDAvxPermutePdAllHigh = 0xF;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_SELECT_0 = 0x00000000;
constexpr u64 s_DoubleSELECT_0 = 0x00000000;
constexpr u32 s_SELECT_1 = 0xFFFFFFFF;
constexpr u64 s_DoubleSELECT_1 = 0xFFFFFFFFFFFFFFFFull;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDSinCoefficients0 = { { { -0.16666667f, +0.0083333310f, -0.00019840874f, +2.7525562e-06f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDSinCoefficients1 = { { { -2.3889859e-08f, -0.16665852f /*Est1*/, +0.0083139502f /*Est2*/, -0.00018524670f /*Est3*/ } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDCosCoefficients0 = { { { -0.5f, +0.041666638f, -0.0013888378f, +2.4760495e-05f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDCosCoefficients1 = { { { -2.6051615e-07f, -0.49992746f /*Est1*/, +0.041493919f /*Est2*/, -0.0012712436f /*Est3*/ } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDArcCoefficients0 = { { { +1.5707963050f, -0.2145988016f, +0.0889789874f, -0.0501743046f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDArcCoefficients1 = { { { +0.0308918810f, -0.0170881256f, +0.0066700901f, -0.0012624911f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDATanCoefficients0 = { { { -0.3333314528f, +0.1999355085f, -0.1420889944f, +0.1065626393f } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDATanCoefficients1 = { { { -0.0752896400f, +0.0429096138f, -0.0161657367f, +0.0028662257f } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDIdentityR0 = { { { 1.0f, 0.0f, 0.0f, 0.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDIdentityR0 = { { { 1.0, 0.0, 0.0, 0.0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDIdentityR1 = { { { 0.0f, 1.0f, 0.0f, 0.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDIdentityR1 = { { { 0.0, 1.0, 0.0, 0.0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDIdentityR2 = { { { 0.0f, 0.0f, 1.0f, 0.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDIdentityR2 = { { { 0.0, 0.0, 1.0, 0.0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDIdentityR3 = { { { 0.0f, 0.0f, 0.0f, 1.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDIdentityR3 = { { { 0.0, 0.0, 0.0, 1.0 } } };


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
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNegativeOne = { { { -1.0f, -1.0f, -1.0f, -1.0f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDNegativeOne = { { { -1.0, -1.0, -1.0, -1.0 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDOneHalf = { { { 0.5f, 0.5f, 0.5f, 0.5f } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDOneHalf = { { { 0.5, 0.5, 0.5, 0.5 } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDHalfPi = { { { s_PIDIV2, s_PIDIV2, s_PIDIV2, s_PIDIV2 } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDPi = { { { s_PI, s_PI, s_PI, s_PI } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDTwoPi = { { { s_2PI, s_2PI, s_2PI, s_2PI } } };
NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDReciprocalTwoPi = { { { s_1DIV2PI, s_1DIV2PI, s_1DIV2PI, s_1DIV2PI } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline const SIMDVectorDoubleConstF s_DoubleSIMDEpsilon = { { { s_F64Epsilon, s_F64Epsilon, s_F64Epsilon, s_F64Epsilon } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDInfinity = { { { 0x7F800000, 0x7F800000, 0x7F800000, 0x7F800000 } } };
inline const SIMDVectorDoubleConstI s_DoubleSIMDInfinity = { { { 0x7FF0000000000000, 0x7FF0000000000000, 0x7FF0000000000000, 0x7FF0000000000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDQNaN = { { { 0x7FC00000, 0x7FC00000, 0x7FC00000, 0x7FC00000 } } };
inline const SIMDVectorDoubleConstI s_DoubleSIMDQNaN = { { { 0x7FF8000000000000, 0x7FF8000000000000, 0x7FF8000000000000, 0x7FF8000000000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDQNaNTest = { { { 0x007FFFFF, 0x007FFFFF, 0x007FFFFF, 0x007FFFFF } } };
NWB_GLOBAL_CONST SIMDVectorConstI s_SIMDAbsMask = { { { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF } } };
inline const SIMDVectorDoubleConstI s_DoubleSIMDAbsMask = { { { 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDNegOneMask = { { { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GLOBAL_CONST SIMDVectorConstF s_SIMDNoFraction = { { { s_F32NoFractionThreshold, s_F32NoFractionThreshold, s_F32NoFractionThreshold, s_F32NoFractionThreshold } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDSelect0101 = { { { s_SELECT_0, s_SELECT_1, s_SELECT_0, s_SELECT_1 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDSelect0101 = { { { s_DoubleSELECT_0, s_DoubleSELECT_1, s_DoubleSELECT_0, s_DoubleSELECT_1 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDSelect1110 = { { { s_SELECT_1, s_SELECT_1, s_SELECT_1, s_SELECT_0 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDSelect1110 = { { { s_DoubleSELECT_1, s_DoubleSELECT_1, s_DoubleSELECT_1, s_DoubleSELECT_0 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDSelect1011 = { { { s_SELECT_1, s_SELECT_0, s_SELECT_1, s_SELECT_1 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDFlipY = { { { 0, 0x80000000, 0, 0 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDFlipZ = { { { 0, 0, 0x80000000, 0 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDFlipW = { { { 0, 0, 0, 0x80000000 } } };
NWB_GLOBAL_CONST SIMDVectorConstU s_SIMDFlipYW = { { { 0, 0x80000000, 0, 0x80000000 } } };


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

