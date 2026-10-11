// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(32) SIMDVectorDoubleConstF{
    union{
        f64 f[4];
        SIMDVectorDouble v;
    };

    NWB_INLINE operator SIMDVectorDouble()const noexcept{ return v; }
};

struct alignas(32) SIMDVectorDoubleConstI{
    union{
        i64 i[4];
        SIMDVectorDouble v;
    };

    NWB_INLINE operator SIMDVectorDouble()const noexcept{ return v; }
};

struct alignas(32) SIMDVectorDoubleConstU{
    union{
        u64 u[4];
        SIMDVectorDouble v;
    };

    NWB_INLINE operator SIMDVectorDouble()const noexcept{ return v; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u64 s_DoubleSELECT_0 = 0x00000000;
constexpr u64 s_DoubleSELECT_1 = 0xFFFFFFFFFFFFFFFFull;
constexpr u32 s_DoublePERMUTE_0X = 0;
constexpr u32 s_DoublePERMUTE_0Y = 1;
constexpr u32 s_DoublePERMUTE_0Z = 2;
constexpr u32 s_DoublePERMUTE_0W = 3;
constexpr u32 s_DoublePERMUTE_1X = 4;
constexpr u32 s_DoublePERMUTE_1Y = 5;
constexpr u32 s_DoublePERMUTE_1Z = 6;
constexpr u32 s_DoublePERMUTE_1W = 7;
constexpr u32 s_DoubleSWIZZLE_X = 0;
constexpr u32 s_DoubleSWIZZLE_Y = 1;
constexpr u32 s_DoubleSWIZZLE_Z = 2;
constexpr u32 s_DoubleSWIZZLE_W = 3;
constexpr u32 s_DoubleCRMASK_CR6 = 0x000000F0;
constexpr u32 s_DoubleCRMASK_CR6TRUE = 0x00000080;
constexpr u32 s_DoubleCRMASK_CR6FALSE = 0x00000020;
constexpr u32 s_DoubleCRMASK_CR6BOUNDS = s_DoubleCRMASK_CR6FALSE;

inline constexpr f64 s_DoublePI = 0x1.921fb54442d18p+1;
inline constexpr f64 s_Double2PI = 0x1.921fb54442d18p+2;
inline constexpr f64 s_Double1DIVPI = 0x1.45f306dc9c883p-2;
inline constexpr f64 s_Double1DIV2PI = 0x1.45f306dc9c883p-3;

inline constexpr f64 s_DoublePIDIV2 = 0x1.921fb54442d18p+0;

inline constexpr f64 s_DoublePIDIV4 = 0x1.921fb54442d18p-1;

inline const SIMDVectorDoubleConstF s_DoubleSIMDIdentityR0 = { { { 1.0, 0.0, 0.0, 0.0 } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDIdentityR1 = { { { 0.0, 1.0, 0.0, 0.0 } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDIdentityR2 = { { { 0.0, 0.0, 1.0, 0.0 } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDIdentityR3 = { { { 0.0, 0.0, 0.0, 1.0 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDNegativeZero = { { { 0x8000000000000000, 0x8000000000000000, 0x8000000000000000, 0x8000000000000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDNegate3 = { { { 0x8000000000000000, 0x8000000000000000, 0x8000000000000000, 0x00000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDMaskXY = { { { 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0x00000000, 0x00000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDMask3 = { { { 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0x00000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDMaskX = { { { 0xFFFFFFFFFFFFFFFF, 0x00000000, 0x00000000, 0x00000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDMaskY = { { { 0x00000000, 0xFFFFFFFFFFFFFFFF, 0x00000000, 0x00000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDMaskZ = { { { 0x00000000, 0x00000000, 0xFFFFFFFFFFFFFFFF, 0x00000000 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDMaskW = { { { 0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFFFFFFFFFF } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDOne = { { { 1.0, 1.0, 1.0, 1.0 } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDOne3 = { { { 1.0, 1.0, 1.0, 0.0 } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDZero = { { { 0.0, 0.0, 0.0, 0.0 } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDTwo = { { { 2., 2., 2., 2. } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDFour = { { { 4., 4., 4., 4. } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDNegativeOne = { { { -1.0, -1.0, -1.0, -1.0 } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDOneHalf = { { { 0.5, 0.5, 0.5, 0.5 } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDNegativeTwoPi = { { { -s_Double2PI, -s_Double2PI, -s_Double2PI, -s_Double2PI } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDNegativePi = { { { -s_DoublePI, -s_DoublePI, -s_DoublePI, -s_DoublePI } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDPi = { { { s_DoublePI, s_DoublePI, s_DoublePI, s_DoublePI } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDTwoPi = { { { s_Double2PI, s_Double2PI, s_Double2PI, s_Double2PI } } };
inline const SIMDVectorDoubleConstF s_DoubleSIMDEpsilon = { { { 0x1p-52, 0x1p-52, 0x1p-52, 0x1p-52 } } };
inline const SIMDVectorDoubleConstI s_DoubleSIMDInfinity = { { { 0x7FF0000000000000, 0x7FF0000000000000, 0x7FF0000000000000, 0x7FF0000000000000 } } };
inline const SIMDVectorDoubleConstI s_DoubleSIMDQNaN = { { { 0x7FF8000000000000, 0x7FF8000000000000, 0x7FF8000000000000, 0x7FF8000000000000 } } };
inline const SIMDVectorDoubleConstI s_DoubleSIMDAbsMask = { { { 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF, 0x7FFFFFFFFFFFFFFF } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDSelect0101 = { { { s_DoubleSELECT_0, s_DoubleSELECT_1, s_DoubleSELECT_0, s_DoubleSELECT_1 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDSelect1100 = { { { s_DoubleSELECT_1, s_DoubleSELECT_1, s_DoubleSELECT_0, s_DoubleSELECT_0 } } };
inline const SIMDVectorDoubleConstU s_DoubleSIMDSelect1110 = { { { s_DoubleSELECT_1, s_DoubleSELECT_1, s_DoubleSELECT_1, s_DoubleSELECT_0 } } };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

