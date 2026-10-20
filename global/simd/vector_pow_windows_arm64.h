// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_arithmetic.h"

#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC) && !defined(_DEBUG)
#include "../floating_point_environment.h"

#include <cerrno>
#include <cfenv>
#include <float.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/*
 * Packed adaptation of ARM optimized-routines powf and its shared data.
 * Copyright (c) 2017-2026, Arm Limited.
 * SPDX-License-Identifier: MIT
 * Upstream commit: f67e7daa5457646fad9f75580263dba654c988aa.
 * Sources: https://github.com/ARM-software/optimized-routines/tree/f67e7daa5457646fad9f75580263dba654c988aa/math
 * License terms: vector_pow_windows_arm64_license.txt.
 */

namespace SIMDVectorPowWindowsArm64Detail{

using UInt4 = u32 __attribute__((ext_vector_type(4)));
using Int4 = i32 __attribute__((ext_vector_type(4)));
using UInt64x4 = u64 __attribute__((ext_vector_type(4)));
using Int64x4 = i64 __attribute__((ext_vector_type(4)));

struct ArmTablePair{ f64 invc, logc; };
inline constexpr ArmTablePair s_ArmLogTable[]{
    { 0x1.661ec79f8f3bep+0, -0x1.efec65b963019p-2 * 32 },
    { 0x1.571ed4aaf883dp+0, -0x1.b0b6832d4fca4p-2 * 32 },
    { 0x1.49539f0f010bp+0, -0x1.7418b0a1fb77bp-2 * 32 },
    { 0x1.3c995b0b80385p+0, -0x1.39de91a6dcf7bp-2 * 32 },
    { 0x1.30d190c8864a5p+0, -0x1.01d9bf3f2b631p-2 * 32 },
    { 0x1.25e227b0b8eap+0, -0x1.97c1d1b3b7afp-3 * 32 },
    { 0x1.1bb4a4a1a343fp+0, -0x1.2f9e393af3c9fp-3 * 32 },
    { 0x1.12358f08ae5bap+0, -0x1.960cbbf788d5cp-4 * 32 },
    { 0x1.0953f419900a7p+0, -0x1.a6f9db6475fcep-5 * 32 },
    { 0x1p+0, 0x0p+0 },
    { 0x1.e608cfd9a47acp-1, 0x1.338ca9f24f53dp-4 * 32 },
    { 0x1.ca4b31f026aap-1, 0x1.476a9543891bap-3 * 32 },
    { 0x1.b2036576afce6p-1, 0x1.e840b4ac4e4d2p-3 * 32 },
    { 0x1.9c2d163a1aa2dp-1, 0x1.40645f0c6651cp-2 * 32 },
    { 0x1.886e6037841edp-1, 0x1.88e9c2c1b9ff8p-2 * 32 },
    { 0x1.767dcf5534862p-1, 0x1.ce0a44eb17bccp-2 * 32 }
};
inline constexpr u64 s_ArmExpTable[]{
    0x3ff0000000000000, 0x3fefd9b0d3158574, 0x3fefb5586cf9890f, 0x3fef9301d0125b51,
    0x3fef72b83c7d517b, 0x3fef54873168b9aa, 0x3fef387a6e756238, 0x3fef1e9df51fdee1,
    0x3fef06fe0a31b715, 0x3feef1a7373aa9cb, 0x3feedea64c123422, 0x3feece086061892d,
    0x3feebfdad5362a27, 0x3feeb42b569d4f82, 0x3feeab07dd485429, 0x3feea47eb03a5585,
    0x3feea09e667f3bcd, 0x3fee9f75e8ec5f74, 0x3feea11473eb0187, 0x3feea589994cce13,
    0x3feeace5422aa0db, 0x3feeb737b0cdc5e5, 0x3feec49182a3f090, 0x3feed503b23e255d,
    0x3feee89f995ad3ad, 0x3feeff76f2fb5e47, 0x3fef199bdd85529c, 0x3fef3720dcef9069,
    0x3fef5818dcfba487, 0x3fef7c97337b9b5f, 0x3fefa4afa2a490da, 0x3fefd0765b6e4540
};

inline constexpr f64 s_ArmLogA0 = 0x1.27616c9496e0bp-2 * 32;
inline constexpr f64 s_ArmLogA1 = -0x1.71969a075c67ap-2 * 32;
inline constexpr f64 s_ArmLogA2 = 0x1.ec70a6ca7baddp-2 * 32;
inline constexpr f64 s_ArmLogA3 = -0x1.7154748bef6c8p-1 * 32;
inline constexpr f64 s_ArmLogA4 = 0x1.71547652ab82bp0 * 32;
inline constexpr f64 s_ArmExpC0 = 0x1.c6af84b912394p-5 / 32 / 32 / 32;
inline constexpr f64 s_ArmExpC1 = 0x1.ebfce50fac4f3p-3 / 32 / 32;
inline constexpr f64 s_ArmExpC2 = 0x1.62e42ff0c52d6p-1 / 32;
inline constexpr f64 s_ArmOverflow = 0x1.fffffffd1d571p+6 * 32;
inline constexpr f64 s_ArmNearOverflow = 0x1.fffffffa3aae2p+6 * 32;
inline constexpr f64 s_ArmUnderflow = -150.0 * 32;
inline constexpr f64 s_ArmMayUnderflow = -149.0 * 32;

// AArch64 FPSR/FPCR bits: cumulative input-denormal flag, flush-to-zero, input-denormal trap and the five exception trap enables.
inline constexpr u32 s_ArmFpsrInputDenormalFlag = 0x80u;
inline constexpr u32 s_ArmFpcrFlushToZero = 0x01000000u;
inline constexpr u32 s_ArmFpcrInputDenormalTrap = 0x8000u;
inline constexpr u32 s_ArmFpcrExceptionTrapMask = 0x1f00u;

// powf log table (16 entries) and exp table (32 entries) index splits.
inline constexpr u32 s_ArmLogOffsetBits = 0x3f330000u;
inline constexpr u32 s_ArmLogTableIndexShift = 19u;
inline constexpr u32 s_ArmLogTableIndexMask = 15u;
inline constexpr u32 s_ArmLogExponentMask = 0xff800000u;
inline constexpr u32 s_ArmLogExponentShift = 18u;
inline constexpr u32 s_ArmExpTableIndexMask = 31u;
inline constexpr u32 s_ArmExpScaleShift = 47u;

// x in [2^-16, 2^16] and |y| <= 4 keep log2(x) * y within [-64, 64].
inline constexpr u32 s_ArmBoundedXMinBits = 0x37800000u;
inline constexpr u32 s_ArmBoundedXMaxBits = 0x47800000u;
inline constexpr u32 s_ArmBoundedYMaxBits = 0x40800000u;

// ArmCheckInt result for a binary32 exponent value.
inline constexpr i32 s_ArmNotInteger = 0;
inline constexpr i32 s_ArmOddInteger = 1;
inline constexpr i32 s_ArmEvenInteger = 2;

template<bool Equal>
NWB_INLINE UInt64x4 ArmCompareMask(const SIMDVectorDouble a, const SIMDVectorDouble b)noexcept{
    const auto aLow = SIMDVectorDoubleDetail::GetLow(a), aHigh = SIMDVectorDoubleDetail::GetHigh(a);
    const auto bLow = SIMDVectorDoubleDetail::GetLow(b), bHigh = SIMDVectorDoubleDetail::GetHigh(b);
    uint64x2_t low{}, high{};
    if constexpr(Equal){
        __asm__ __volatile__("fcmge %0.2d, %1.2d, %2.2d" : "=w"(low) : "w"(aLow), "w"(bLow));
        __asm__ __volatile__("fcmge %0.2d, %1.2d, %2.2d" : "=w"(high) : "w"(aHigh), "w"(bHigh));
    }
    else{
        __asm__ __volatile__("fcmgt %0.2d, %1.2d, %2.2d" : "=w"(low) : "w"(aLow), "w"(bLow));
        __asm__ __volatile__("fcmgt %0.2d, %1.2d, %2.2d" : "=w"(high) : "w"(aHigh), "w"(bHigh));
    }
    return BitCast<UInt64x4>(SIMDVectorDoubleDetail::FromUIntPairs(low, high));
}

template<bool Ordered>
struct ArmArithmetic{
    u32 control;

    template<typename T, typename Operation>
    [[nodiscard]] NWB_INLINE T Evaluate(const Operation& operation)const noexcept{
#pragma STDC FENV_ACCESS ON
        if constexpr(!Ordered)
            return operation();
        else{
            u64 initialNativeStatus{};
            __asm__ __volatile__("mrs %0, fpsr" : "=r"(initialNativeStatus));
            const auto saved = HoldFloatingPointExceptions();
            if(!saved)
                __builtin_trap();
            const T value = operation();
            const i32 flags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
            u64 operationNativeStatus{};
            __asm__ __volatile__("mrs %0, fpsr" : "=r"(operationNativeStatus));
            std::fexcept_t raised{};
            if(std::fegetexceptflag(&raised, FE_ALL_EXCEPT) != 0 || !SetFloatingPointEnvironment(*saved))
                __builtin_trap();
            u64 restoredNativeStatus{};
            __asm__ __volatile__("mrs %0, fpsr" : "=r"(restoredNativeStatus));
            restoredNativeStatus |= (initialNativeStatus | operationNativeStatus) & s_ArmFpsrInputDenormalFlag;
            __asm__ __volatile__("msr fpsr, %0" : : "r"(restoredNativeStatus));
            // Exact tiny results still trap when underflow is enabled.
            i32 enabled = flags & ~static_cast<i32>(control) & FE_ALL_EXCEPT;
            if constexpr(IsSame_V<T, SIMDVectorSingle>){
                const UInt4 magnitude = BitCast<UInt4>(value) & s_F32AbsoluteBitMask;
                if((control & FE_UNDERFLOW) == 0u && __builtin_reduce_or((magnitude != 0u) & (magnitude < s_F32MinNormalBits)) != 0u)
                    enabled |= FE_UNDERFLOW;
            }
            else{
                const UInt64x4 magnitude = BitCast<UInt64x4>(value) & 0x7fffffffffffffffull;
                if((control & FE_UNDERFLOW) == 0u && __builtin_reduce_or((magnitude != 0u) & (magnitude < 0x0010000000000000ull)) != 0u)
                    enabled |= FE_UNDERFLOW;
            }
            if(enabled != 0){
                if(!RaiseFloatingPointExceptions(enabled))
                    __builtin_trap();
            }
            else if(std::fesetexceptflag(&raised, flags) != 0)
                __builtin_trap();
            return value;
        }
    }

    [[nodiscard]] NWB_INLINE SIMDVectorDouble DoubleAdd(SIMDVectorDouble a, SIMDVectorDouble b)const noexcept{
#pragma STDC FENV_ACCESS ON
        return Evaluate<SIMDVectorDouble>([&]()noexcept{ return VectorDoubleAdd(a, b); });
    }

    [[nodiscard]] NWB_INLINE SIMDVectorDouble DoubleSubtract(SIMDVectorDouble a, SIMDVectorDouble b)const noexcept{
#pragma STDC FENV_ACCESS ON
        return Evaluate<SIMDVectorDouble>([&]()noexcept{ return VectorDoubleSubtract(a, b); });
    }

    [[nodiscard]] NWB_INLINE SIMDVectorDouble DoubleMultiply(SIMDVectorDouble a, SIMDVectorDouble b)const noexcept{
#pragma STDC FENV_ACCESS ON
        return Evaluate<SIMDVectorDouble>([&]()noexcept{ return VectorDoubleMultiply(a, b); });
    }

    [[nodiscard]] NWB_INLINE SIMDVectorDouble DoubleFma(SIMDVectorDouble a, SIMDVectorDouble b, SIMDVectorDouble c)const noexcept{
#pragma STDC FENV_ACCESS ON
        return Evaluate<SIMDVectorDouble>([&]()noexcept{ return VectorDoubleMultiplyAdd(a, b, c); });
    }

    [[nodiscard]] NWB_INLINE SIMDVectorSingle Narrow(SIMDVectorDouble a)const noexcept{
#pragma STDC FENV_ACCESS ON
        if constexpr(Ordered){
            // Conversion tests tininess before rounding and before inexact.
            const UInt64x4 magnitude = BitCast<UInt64x4>(a) & 0x7fffffffffffffffull;
            u64 nativeControl{};
            __asm__ __volatile__("mrs %0, fpcr" : "=r"(nativeControl));
            // FZ consumes binary64 subnormal inputs as zero.
            const u64 minimum = (nativeControl & s_ArmFpcrFlushToZero) != 0u ? 0x0010000000000000ull : 1u;
            const auto tiny = (magnitude >= minimum) & (magnitude < 0x3810000000000000ull);
            if((control & FE_UNDERFLOW) == 0u && __builtin_reduce_or(tiny) != 0u){
                if(!RaiseFloatingPointExceptions(FE_UNDERFLOW))
                    __builtin_trap();
            }
        }
        return Evaluate<SIMDVectorSingle>([&]()noexcept{ return ConvertVectorDoubleToSingle(a); });
    }

    [[nodiscard]] NWB_INLINE SIMDVectorSingle Add(SIMDVectorSingle a, SIMDVectorSingle b)const noexcept{
#pragma STDC FENV_ACCESS ON
        return Evaluate<SIMDVectorSingle>([&]()noexcept{ return VectorAdd(a, b); });
    }

    [[nodiscard]] NWB_INLINE SIMDVectorSingle Multiply(SIMDVectorSingle a, SIMDVectorSingle b)const noexcept{
#pragma STDC FENV_ACCESS ON
        return Evaluate<SIMDVectorSingle>([&]()noexcept{ return VectorMultiply(a, b); });
    }

    [[nodiscard]] NWB_INLINE SIMDVectorSingle Divide(SIMDVectorSingle a, SIMDVectorSingle b)const noexcept{
#pragma STDC FENV_ACCESS ON
        __asm__ __volatile__("" : "+w"(a), "+w"(b));
        return Evaluate<SIMDVectorSingle>([&]()noexcept{ return VectorDivide(a, b); });
    }

    [[nodiscard]] NWB_INLINE SIMDVectorSingle Invalid(SIMDVectorSingle x)const noexcept{
#pragma STDC FENV_ACCESS ON
        const SIMDVectorSingle z = Evaluate<SIMDVectorSingle>([&]()noexcept{ return VectorSubtract(x, x); });
        return Divide(z, z);
    }
};

[[nodiscard]] NWB_INLINE i32 ArmCheckInt(const u32 bits)noexcept{
    const u32 exponent = (bits >> s_F32MantissaBitCount) & s_F32ExponentFieldMask;
    if(exponent < s_F32ExponentBias)
        return s_ArmNotInteger;
    if(exponent > s_F32IntegerExponent)
        return s_ArmEvenInteger;
    const u32 shift = s_F32IntegerExponent - exponent;
    if((bits & ((1u << shift) - 1u)) != 0u)
        return s_ArmNotInteger;
    return (bits & (1u << shift)) != 0u ? s_ArmOddInteger : s_ArmEvenInteger;
}

[[nodiscard]] NWB_INLINE bool ArmSpecialBits(const u32 bits)noexcept{
    return 2u * bits - 1u >= 2u * s_F32InfinityBits - 1u;
}

[[nodiscard]] NWB_INLINE bool ArmSignaling(const u32 bits)noexcept{
    return 2u * (bits ^ s_F32QuietBit) > 2u * s_F32QuietNaNBits;
}

template<bool Ordered, bool Bounded = false>
NWB_INLINE SIMDVectorSingle ArmPackedBody(const SIMDVectorSingle x, const SIMDVectorSingle y, const u32 control)noexcept{
#pragma STDC FENV_ACCESS ON
    const ArmArithmetic<Ordered> math{ control };
    UInt4 ix = BitCast<UInt4>(x), iy = BitCast<UInt4>(y), admitted{}, result{}, sign{};
    Int4 errors{}, special{};
    static_assert(!Ordered || !Bounded);
    if constexpr(Bounded){
        const Int4 zeroExponent = (iy & s_F32AbsoluteBitMask) == 0u;
        const Int4 oneBase = ix == 0x3f800000u, oneExponent = iy == 0x3f800000u, zeroBase = ix == 0u;
        const Int4 shortcut = zeroExponent | oneBase | oneExponent | zeroBase;
        special = shortcut ? Int4{} : Int4{ 1, 1, 1, 1 };
        admitted = shortcut ? UInt4{ 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u } : ix;
        result = (zeroExponent | oneBase) ? UInt4{ 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u } : ix;
    }
    else{
        bool ordinary = false;
        if constexpr(!Ordered){
            const UInt4 absoluteY = iy & s_F32AbsoluteBitMask;
            const Int4 normalInputs = (ix >= s_F32MinNormalBits) & (ix < s_F32InfinityBits) & (ix != 0x3f800000u) & (absoluteY != 0u) & (absoluteY < s_F32InfinityBits) & (iy != 0x3f800000u);
            ordinary = __builtin_reduce_and(normalInputs) != 0;
        }
        if(ordinary){
            admitted = ix;
            special = Int4{ 1, 1, 1, 1 };
        }
        else{
            for(usize lane = 0u; lane < 4u; ++lane){
                u32 a = ix[lane], b = iy[lane];
                admitted[lane] = 0x3f800000u;
                if(ArmSpecialBits(b)){
                    if(2u * b == 0u){
                        result[lane] = ArmSignaling(a) ? VectorGetIntX(math.Add(VectorReplicate(BitCast<f32>(a)), VectorReplicate(BitCast<f32>(b)))) : 0x3f800000u;
                    }
                    else if(a == 0x3f800000u){
                        result[lane] = ArmSignaling(b) ? VectorGetIntX(math.Add(VectorReplicate(BitCast<f32>(a)), VectorReplicate(BitCast<f32>(b)))) : 0x3f800000u;
                    }
                    else if(2u * a > 2u * s_F32InfinityBits || 2u * b > 2u * s_F32InfinityBits){
                        result[lane] = VectorGetIntX(math.Add(VectorReplicate(BitCast<f32>(a)), VectorReplicate(BitCast<f32>(b))));
                    }
                    else if(2u * a == 2u * 0x3f800000u)
                        result[lane] = 0x3f800000u;
                    else if((2u * a < 2u * 0x3f800000u) == !(b & s_F32SignBitMask))
                        result[lane] = 0u;
                    else
                        result[lane] = VectorGetIntX(math.Multiply(VectorReplicate(BitCast<f32>(b)), VectorReplicate(BitCast<f32>(b))));
                    continue;
                }
                if(ArmSpecialBits(a)){
                    SIMDVectorSingle square = math.Multiply(VectorReplicate(BitCast<f32>(a)), VectorReplicate(BitCast<f32>(a)));
                    const bool negative = (a & s_F32SignBitMask) != 0u && ArmCheckInt(b) == s_ArmOddInteger;
                    if(negative)
                        square = VectorXorInt(square, s_SIMDNegativeZero);
                    if(2u * a == 0u && (b & s_F32SignBitMask) != 0u){
                        errors[lane] = ERANGE;
                        errno = ERANGE;
                        square = math.Multiply(math.Divide(s_SIMDOne, VectorZero()), negative ? VectorReplicate(-1.0f) : s_SIMDOne);
                    }
                    else if((b & s_F32SignBitMask) != 0u)
                        square = math.Divide(s_SIMDOne, square);
                    result[lane] = VectorGetIntX(square);
                    continue;
                }
                if((a & s_F32SignBitMask) != 0u){
                    const i32 integral = ArmCheckInt(b);
                    if(integral == s_ArmNotInteger){
                        result[lane] = VectorGetIntX(math.Invalid(VectorReplicate(BitCast<f32>(a))));
                        errors[lane] = EDOM;
                        errno = EDOM;
                        continue;
                    }
                    if(integral == s_ArmOddInteger)
                        sign[lane] = 0x10000u;
                    a &= s_F32AbsoluteBitMask;
                }
                if(b == 0x3f800000u){
                    if(a < s_F32MinNormalBits){
                        // The CRT normalizes subnormal x before its y == 1 return.
                        const SIMDVectorSingle normalizedShortcut = math.Multiply(VectorReplicate(BitCast<f32>(ix[lane])), VectorReplicate(0x1p23f));
                        __asm__ __volatile__("" : : "w"(normalizedShortcut));
                    }
                    result[lane] = ix[lane];
                    continue;
                }
                if(a == 0x3f800000u){
                    result[lane] = sign[lane] != 0u ? 0xbf800000u : 0x3f800000u;
                    continue;
                }
                admitted[lane] = a;
                special[lane] = 1;
            }
        }
    }
    if constexpr(!Bounded){
        const auto subnormal = admitted < s_F32MinNormalBits;
        const SIMDVectorSingle normalized = math.Multiply(BitCast<SIMDVectorSingle>(subnormal ? admitted : UInt4{}), VectorReplicate(0x1p23f));
        const UInt4 norm = BitCast<UInt4>(normalized) & s_F32AbsoluteBitMask;
        admitted = subnormal ? norm - (23u << 23u) : admitted;
    }
    const UInt4 tmp = admitted - s_ArmLogOffsetBits, indices = (tmp >> s_ArmLogTableIndexShift) & s_ArmLogTableIndexMask;
    const UInt4 top = tmp & s_ArmLogExponentMask;
    const Int4 exponent = BitCast<Int4>(top) >> s_ArmLogExponentShift;
    const SIMDVectorDouble z = ConvertVectorSingleToDouble(BitCast<SIMDVectorSingle>(admitted - top));
    SIMDVectorDouble invc{}, logc{};
    for(usize lane = 0u; lane < 4u; ++lane){
        invc[lane] = s_ArmLogTable[indices[lane]].invc;
        logc[lane] = s_ArmLogTable[indices[lane]].logc;
    }
    const SIMDVectorDouble r = math.DoubleFma(z, invc, VectorDoubleReplicate(-1.0));
    const SIMDVectorDouble r2 = math.DoubleMultiply(r, r);
    const SIMDVectorDouble y0 = math.DoubleAdd(logc, __builtin_convertvector(exponent, SIMDVectorDouble));
    const SIMDVectorDouble q = math.DoubleFma(r, VectorDoubleReplicate(s_ArmLogA4), y0);
    const SIMDVectorDouble p = math.DoubleFma(r, VectorDoubleReplicate(s_ArmLogA2), VectorDoubleReplicate(s_ArmLogA3));
    const SIMDVectorDouble q2 = math.DoubleFma(p, r2, q);
    const SIMDVectorDouble t = math.DoubleFma(r, VectorDoubleReplicate(s_ArmLogA0), VectorDoubleReplicate(s_ArmLogA1));
    const SIMDVectorDouble r4 = math.DoubleMultiply(r2, r2);
    const SIMDVectorDouble logarithm = math.DoubleFma(t, r4, q2);
    const SIMDVectorSingle safeY = BitCast<SIMDVectorSingle>(special != 0 ? iy : UInt4{});
    const SIMDVectorDouble power = math.DoubleMultiply(logarithm, ConvertVectorSingleToDouble(safeY));
    SIMDVectorDouble safePower = power;
    if constexpr(!Bounded){
        const UInt64x4 overflow = ArmCompareMask<false>(power, VectorDoubleReplicate(s_ArmOverflow));
        const UInt64x4 nearOverflow = ArmCompareMask<false>(power, VectorDoubleReplicate(s_ArmNearOverflow));
        const UInt64x4 underflow = ArmCompareMask<true>(VectorDoubleReplicate(s_ArmUnderflow), power);
        const UInt64x4 mayUnderflow = ArmCompareMask<false>(VectorDoubleReplicate(s_ArmMayUnderflow), power);
        const UInt64x4 exceptionalMask = overflow | nearOverflow | underflow | mayUnderflow;
        if(__builtin_reduce_or(exceptionalMask) != 0u){
            for(usize lane = 0u; lane < 4u; ++lane){
                if(special[lane] == 0)
                    continue;
                const bool negative = sign[lane] != 0u;
                i32 exceptional = 0;
                if(overflow[lane] != 0u)
                    exceptional = 1;
                else if(nearOverflow[lane] != 0u){
                    const SIMDVectorSingle perturb = math.Add(negative ? VectorReplicate(-1.0f) : s_SIMDOne, negative ? VectorReplicate(-0x1p-25f) : VectorReplicate(0x1p-25f));
                    if(VectorGetIntX(perturb) != (negative ? 0xbf800000u : 0x3f800000u))
                        exceptional = 1;
                }
                if(exceptional == 0 && underflow[lane] != 0u)
                    exceptional = 2;
                else if(exceptional == 0 && mayUnderflow[lane] != 0u)
                    exceptional = 3;
                if(exceptional != 0){
                    const u32 multiplier = exceptional == 1 ? 0x70000000u : (exceptional == 2 ? 0x10000000u : 0x1a200000u);
                    const u32 signedMultiplier = multiplier | (negative ? s_F32SignBitMask : 0u);
                    errno = ERANGE;
                    errors[lane] = ERANGE;
                    result[lane] = VectorGetIntX(math.Multiply(VectorReplicate(BitCast<f32>(signedMultiplier)), VectorReplicate(BitCast<f32>(multiplier))));
                    special[lane] = 0;
                    safePower[lane] = 0.0;
                }
            }
        }
    }
    const SIMDVectorDouble kd = SIMDVectorDoubleDetail::FromPairs(vrndaq_f64(SIMDVectorDoubleDetail::GetLow(safePower)), vrndaq_f64(SIMDVectorDoubleDetail::GetHigh(safePower)));
    const Int64x4 ki = __builtin_convertvector(kd, Int64x4);
    const SIMDVectorDouble rem = math.DoubleSubtract(safePower, kd);
    const SIMDVectorDouble ep = math.DoubleFma(rem, VectorDoubleReplicate(s_ArmExpC0), VectorDoubleReplicate(s_ArmExpC1));
    const SIMDVectorDouble er2 = math.DoubleMultiply(rem, rem);
    const SIMDVectorDouble eq = math.DoubleFma(rem, VectorDoubleReplicate(s_ArmExpC2), s_DoubleSIMDOne);
    const SIMDVectorDouble exponential = math.DoubleFma(er2, ep, eq);
    UInt64x4 scale{};
    for(usize lane = 0u; lane < 4u; ++lane)
        scale[lane] = s_ArmExpTable[ki[lane] & s_ArmExpTableIndexMask] + ((static_cast<u64>(ki[lane]) + sign[lane]) << s_ArmExpScaleShift);
    const UInt4 narrowed = BitCast<UInt4>(math.Narrow(math.DoubleMultiply(exponential, BitCast<SIMDVectorDouble>(scale))));
    if constexpr(Bounded)
        return BitCast<SIMDVectorSingle>(special != 0 ? narrowed : result);
    if(__builtin_reduce_or(errors) != 0){
        for(i32 lane = 3; lane >= 0; --lane){
            if(errors[lane] != 0)
                errno = errors[lane];
        }
    }
    return BitCast<SIMDVectorSingle>(special != 0 ? narrowed : result);
}

template<bool Ordered>
NWB_NOINLINE SIMDVectorSingle ArmPackedPacket(const SIMDVectorSingle x, const SIMDVectorSingle y, const u32 control)noexcept{
#pragma STDC FENV_ACCESS ON
    return ArmPackedBody<Ordered, false>(x, y, control);
}

NWB_NOINLINE inline SIMDVectorSingle NWB_SIMD_CALL ArmEnabledPower(const SIMDVectorSingle x, const SIMDVectorSingle y)noexcept{
#pragma STDC FENV_ACCESS ON
    u32 control{};
    if(_controlfp_s(&control, 0u, 0u) != 0)
        __builtin_trap();
    if((control & FE_ALL_EXCEPT) != FE_ALL_EXCEPT){
        SIMDVectorSingle result = s_SIMDOne;
        for(i32 lane = 3; lane >= 0; --lane){
            const SIMDVectorSingle a = VectorSetIntByIndex(s_SIMDOne, VectorGetIntByIndex(x, lane), 0u);
            const SIMDVectorSingle b = VectorSetIntByIndex(s_SIMDOne, VectorGetIntByIndex(y, lane), 0u);
            const SIMDVectorSingle packet = ArmPackedPacket<true>(a, b, control);
            result = VectorSetIntByIndex(result, VectorGetIntX(packet), lane);
        }
        return result;
    }
    return ArmPackedPacket<false>(x, y, control);
}

NWB_NOINLINE inline SIMDVectorSingle NWB_SIMD_CALL ArmIDEOriginal(const SIMDVectorSingle x, const SIMDVectorSingle y)noexcept{
    return VectorSet(::Pow(VectorGetX(x), VectorGetX(y)), ::Pow(VectorGetY(x), VectorGetY(y)), ::Pow(VectorGetZ(x), VectorGetZ(y)), ::Pow(VectorGetW(x), VectorGetW(y)));
}

NWB_NOINLINE inline SIMDVectorSingle NWB_SIMD_CALL Pow(const SIMDVectorSingle x, const SIMDVectorSingle y)noexcept{
#pragma STDC FENV_ACCESS ON
    u64 nativeControl{};
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(nativeControl));
    // Native input-denormal traps have a different packed instruction category.
    if((nativeControl & s_ArmFpcrInputDenormalTrap) != 0u)
        return ArmIDEOriginal(x, y);
    if((nativeControl & s_ArmFpcrExceptionTrapMask) != 0u)
        return ArmEnabledPower(x, y);
    const UInt4 ix = BitCast<UInt4>(x), iy = BitCast<UInt4>(y), absoluteY = iy & s_F32AbsoluteBitMask;
    // These bounds keep log2(x) * y within [-64, 64].
    const Int4 normalX = (ix >= s_ArmBoundedXMinBits) & (ix <= s_ArmBoundedXMaxBits);
    const Int4 zeroY = absoluteY == 0u;
    const Int4 normalY = (absoluteY >= s_F32MinNormalBits) & (absoluteY <= s_ArmBoundedYMaxBits);
    const Int4 zeroX = (ix == 0u) & (((iy & s_F32SignBitMask) == 0u) | zeroY);
    const Int4 bounded = (normalX | zeroX) & (normalY | zeroY);
    if(__builtin_reduce_and(bounded) != 0)
        return ArmPackedBody<false, true>(x, y, FE_ALL_EXCEPT);
    return ArmPackedPacket<false>(x, y, FE_ALL_EXCEPT);
}

};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

