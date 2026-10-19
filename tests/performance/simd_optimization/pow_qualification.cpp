// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/floating_point_environment.h>
#include <global/numeric_text.h>
#include <global/simdmath.h>
#include <global/scope_exit.h>
#include <global/text_utils.h>

#include <cerrno>
#include <iostream>

#if defined(NWB_PLATFORM_WINDOWS)
#include <excpt.h>
#include <float.h>
#pragma push_macro("interface")
#undef interface
#include <windows.h>
#pragma pop_macro("interface")
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_pow_qualification{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using UInt4 = u32 __attribute__((ext_vector_type(4)));
using Int4 = i32 __attribute__((ext_vector_type(4)));
using UInt64x4 = u64 __attribute__((ext_vector_type(4)));
using Int64x4 = i64 __attribute__((ext_vector_type(4)));
using Float4Packet = f32 __attribute__((ext_vector_type(4)));
using Evaluate = SIMDVectorSingle(*)(SIMDVectorSingle, SIMDVectorSingle)noexcept;

inline constexpr f32 s_EncodeExponent = 1.0f / 2.4f;
inline constexpr f32 s_DecodeScale = 1.0f / 1.055f;

struct FlagsSnapshot{
    friend bool operator==(const FlagsSnapshot&, const FlagsSnapshot&)noexcept = default;
    template<typename Stream>
    friend Stream& operator<<(Stream& stream, const FlagsSnapshot& value){
        return stream << value.standard << '/' << value.mapped << '/' << value.native;
    }


    i32 standard;
    u32 mapped;
    u64 native;
};

struct alignas(SIMDVectorSingle) Observation{
    SIMDVectorSingle value;
    FlagsSnapshot flags;
    i32 error;
};
static_assert(alignof(Expected<Observation>) >= alignof(Observation));

struct Counts{
    u64 vectors = 0u;
    u64 bits = 0u;
    u64 flags = 0u;
    u64 errors = 0u;
    u64 quantized = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_NOINLINE SIMDVectorSingle Reference(const SIMDVectorSingle x, const SIMDVectorSingle y)noexcept{
    return VectorSet(Pow(VectorGetX(x), VectorGetX(y)), Pow(VectorGetY(x), VectorGetY(y)), Pow(VectorGetZ(x), VectorGetZ(y)), Pow(VectorGetW(x), VectorGetW(y)));
}

NWB_NOINLINE SIMDVectorSingle Production(const SIMDVectorSingle x, const SIMDVectorSingle y)noexcept{
    return VectorPow(x, y);
}

NWB_NOINLINE SIMDVectorSingle PackedSingle(const SIMDVectorSingle x, const SIMDVectorSingle y)noexcept{
    return VectorExp2(VectorMultiply(VectorLog2(x), y));
}

NWB_NOINLINE SIMDVectorSingle PackedDouble(const SIMDVectorSingle x, const SIMDVectorSingle y)noexcept{
#pragma STDC FENV_ACCESS ON
    const UInt4 xBits = BitCast<UInt4>(x), yBits = BitCast<UInt4>(y);
    const UInt4 magnitude = xBits & 0x7fffffffu, yMagnitude = yBits & 0x7fffffffu;
    const auto ordinary = (magnitude != 0u) & (magnitude < 0x7f800000u) & (yMagnitude < 0x7f800000u);
    const UInt4 admittedX = ordinary ? magnitude : UInt4{ 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u };
    const SIMDVectorSingle safeX = BitCast<SIMDVectorSingle>(admittedX);
    const auto subnormal = admittedX < 0x00800000u;
    const SIMDVectorSingle normalized = VectorSelect(safeX, VectorScale(VectorSelect(s_SIMDOne, safeX, BitCast<SIMDVectorSingle>(subnormal)), 0x1p24f), BitCast<SIMDVectorSingle>(subnormal));
    const UInt4 normalizedBits = BitCast<UInt4>(normalized);
    const Int4 exponent = __builtin_convertvector(normalizedBits >> 23u, Int4) - 127 - (subnormal ? 24 : 0);
    const SIMDVectorSingle mantissa = BitCast<SIMDVectorSingle>((normalizedBits & 0x007fffffu) | 0x3f800000u);
    SIMDVectorDouble m = ConvertVectorSingleToDouble(mantissa);
    SIMDVectorDouble e = __builtin_convertvector(exponent, SIMDVectorDouble);
    const SIMDVectorDouble upper = VectorDoubleGreater(m, VectorDoubleReplicate(0x1.6a09e667f3bcdp0));
    m = VectorDoubleSelect(m, VectorDoubleScale(m, 0.5), upper);
    e = VectorDoubleAdd(e, VectorDoubleSelect(VectorDoubleZero(), s_DoubleSIMDOne, upper));
    const SIMDVectorDouble z = VectorDoubleDivide(VectorDoubleSubtract(m, s_DoubleSIMDOne), VectorDoubleAdd(m, s_DoubleSIMDOne));
    const SIMDVectorDouble zSquared = VectorDoubleMultiply(z, z);
    constexpr f64 s_LogCoefficients[]{ 1.0, 1.0 / 3.0, 1.0 / 5.0, 1.0 / 7.0, 1.0 / 9.0, 1.0 / 11.0, 1.0 / 13.0, 1.0 / 15.0, 1.0 / 17.0, 1.0 / 19.0, 1.0 / 21.0, 1.0 / 23.0, 1.0 / 25.0, 1.0 / 27.0, 1.0 / 29.0, 1.0 / 31.0 };
    SIMDVectorDouble logarithm = VectorDoubleReplicate(s_LogCoefficients[15]);
    for(i32 degree = 14; degree >= 0; --degree)
        logarithm = VectorDoubleMultiplyAdd(logarithm, zSquared, VectorDoubleReplicate(s_LogCoefficients[degree]));
    logarithm = VectorDoubleMultiply(VectorDoubleScale(z, 2.0), logarithm);
    const SIMDVectorDouble log2 = VectorDoubleAdd(e, VectorDoubleScale(logarithm, 0x1.71547652b82fep0));
    const SIMDVectorDouble safeY = ConvertVectorSingleToDouble(BitCast<SIMDVectorSingle>(ordinary ? yBits : UInt4{}));
    const SIMDVectorDouble power = VectorDoubleMultiply(log2, safeY);
    const SIMDVectorDouble bounded = VectorDoubleMin(VectorDoubleReplicate(129.0), VectorDoubleMax(VectorDoubleReplicate(-151.0), power));
    const SIMDVectorDouble integral = VectorDoubleFloor(bounded);
    const SIMDVectorDouble remainder = VectorDoubleScale(VectorDoubleSubtract(bounded, integral), 0x1.62e42fefa39efp-1);
    // The series use binary64 lanes so the final binary32 rounding has guard bits.
    constexpr f64 s_InverseFactorials[]{ 1.0, 1.0, 0.5, 1.0 / 6.0, 1.0 / 24.0, 1.0 / 120.0, 1.0 / 720.0, 1.0 / 5040.0, 1.0 / 40320.0, 1.0 / 362880.0, 1.0 / 3628800.0, 1.0 / 39916800.0, 1.0 / 479001600.0,
        1.0 / 6227020800.0, 1.0 / 87178291200.0, 1.0 / 1307674368000.0, 1.0 / 20922789888000.0, 1.0 / 355687428096000.0, 1.0 / 6402373705728000.0, 1.0 / 121645100408832000.0, 1.0 / 2432902008176640000.0 };
    SIMDVectorDouble exponential = VectorDoubleReplicate(s_InverseFactorials[20]);
    for(i32 degree = 19; degree >= 0; --degree)
        exponential = VectorDoubleMultiplyAdd(exponential, remainder, VectorDoubleReplicate(s_InverseFactorials[degree]));
    const Int64x4 integralBits = __builtin_convertvector(integral, Int64x4);
    const SIMDVectorDouble scale = BitCast<SIMDVectorDouble>(__builtin_convertvector(integralBits + 1023, UInt64x4) << 52u);
    UInt4 result = BitCast<UInt4>(ConvertVectorDoubleToSingle(VectorDoubleMultiply(exponential, scale)));
    const UInt4 yExponent = yMagnitude >> 23u;
    const UInt4 shifts = yExponent < 127u ? 23u : (yExponent > 150u ? 0u : 150u - yExponent);
    const UInt4 significand = (yMagnitude & 0x007fffffu) | 0x00800000u;
    const auto integerY = (yMagnitude == 0u) | (yExponent >= 151u) | ((yExponent >= 127u) & ((significand & ((UInt4{ 1u, 1u, 1u, 1u } << shifts) - 1u)) == 0u));
    const auto oddY = integerY & (yExponent <= 150u) & (((significand >> shifts) & 1u) != 0u);
    const UInt4 sign = oddY ? (xBits & 0x80000000u) : UInt4{};
    result |= sign;
    const auto yNegative = (yBits & 0x80000000u) != 0u;
    result = magnitude == 0u ? (yNegative ? 0x7f800000u : 0u) | sign : result;
    result = magnitude == 0x7f800000u ? (yNegative ? 0u : 0x7f800000u) | sign : result;
    const auto domain = ((xBits & 0x80000000u) != 0u) & (magnitude != 0u) & (magnitude < 0x7f800000u) & !integerY;
    result = domain ? 0x7fc00000u : result;
    const UInt4 infiniteY = yNegative ? (magnitude < 0x3f800000u ? 0x7f800000u : 0u) : (magnitude < 0x3f800000u ? 0u : 0x7f800000u);
    result = yMagnitude == 0x7f800000u ? (magnitude == 0x3f800000u ? 0x3f800000u : infiniteY) : result;
    const auto nanX = magnitude > 0x7f800000u, nanY = yMagnitude > 0x7f800000u;
    const SIMDVectorSingle quiet = VectorAdd(BitCast<SIMDVectorSingle>(nanX ? xBits : UInt4{}), BitCast<SIMDVectorSingle>(nanY ? yBits : UInt4{}));
    result = (nanX | nanY) ? BitCast<UInt4>(quiet) : result;
    result = ((xBits == 0x3f800000u) | (yMagnitude == 0u)) ? 0x3f800000u : result;
    // errno also needs the libm side effects, beyond the packed result bits.
    if(__builtin_reduce_or(domain) != 0)
        errno = EDOM;
    else if(__builtin_reduce_or((magnitude == 0u) & yNegative & !nanY) != 0 || (TestFloatingPointExceptions(FE_OVERFLOW | FE_UNDERFLOW) != 0))
        errno = ERANGE;
    return BitCast<SIMDVectorSingle>(result);
}

[[nodiscard]] Expected<Observation> Observe(const Evaluate evaluate, const SIMDVectorSingle x, const SIMDVectorSingle y)noexcept{
#pragma STDC FENV_ACCESS ON
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    const u64 clearNativeStatus = 0u;
    __asm__ __volatile__("msr fpsr, %0" : : "r"(clearNativeStatus));
#endif
#if defined(NWB_PLATFORM_WINDOWS)
    _clearfp();
#endif
    if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT))
        return MakeUnexpected(Failure{});
    errno = E2BIG;
    const SIMDVectorSingle result = evaluate(x, y);
    const i32 error = errno;
    const i32 flags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    u32 mappedStatus{};
#if defined(NWB_PLATFORM_WINDOWS)
    mappedStatus = _statusfp();
#endif
    u64 nativeStatus{};
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(nativeStatus));
#endif
    return Observation{ result, { flags, mappedStatus, nativeStatus }, error };
}

[[nodiscard]] SIMDVectorSingle Encode(const SIMDVectorSingle linear, const Evaluate evaluate)noexcept{
    const SIMDVectorSingle clamped = VectorSaturate(linear);
    const SIMDVectorSingle nonlinear = VectorSubtract(VectorScale(evaluate(clamped, VectorReplicate(s_EncodeExponent)), 1.055f), VectorReplicate(0.055f));
    const SIMDVectorSingle encoded = VectorSelect(nonlinear, VectorScale(clamped, 12.92f), VectorLessOrEqual(clamped, VectorReplicate(0.0031308f)));
    return VectorMin(VectorFloor(VectorAdd(VectorScale(VectorSaturate(encoded), 255.0f), VectorReplicate(0.5f))), VectorReplicate(255.0f));
}

[[nodiscard]] u64 NextSeed(u64& seed)noexcept{
    seed ^= seed << 13u;
    seed ^= seed >> 7u;
    seed ^= seed << 17u;
    return seed;
}

[[nodiscard]] bool Compare(const StringView category, const Evaluate evaluate, const SIMDVectorSingle x, const SIMDVectorSingle y, Counts& counts, const bool quantize = false){
    const auto expected = Observe(Reference, x, y), actual = Observe(evaluate, x, y);
    if(!expected || !actual)
        return false;
    ++counts.vectors;
    const bool flagsDiffer = expected->flags != actual->flags, errorDiffers = expected->error != actual->error;
    counts.flags += flagsDiffer;
    counts.errors += errorDiffers;
    for(usize lane = 0u; lane < 4u; ++lane){
        const u32 expectedBits = VectorGetIntByIndex(expected->value, lane), actualBits = VectorGetIntByIndex(actual->value, lane);
        if(expectedBits != actualBits){
            if(counts.bits < 4u)
                NWB_COUT << "bits category=" << category << " lane=" << lane << " x=0x" << StreamHex << VectorGetIntByIndex(x, lane) << " y=0x" << VectorGetIntByIndex(y, lane) << " expected=0x" << expectedBits << " actual=0x" << actualBits << StreamDec << '\n';
            ++counts.bits;
        }
    }
    if(flagsDiffer && counts.flags <= 2u)
        NWB_COUT << "flags category=" << category << " x0=0x" << StreamHex << VectorGetIntX(x) << " y0=0x" << VectorGetIntX(y) << StreamDec << " expected=" << expected->flags << " actual=" << actual->flags << '\n';
    if(errorDiffers && counts.errors <= 2u)
        NWB_COUT << "errno category=" << category << " x0=0x" << StreamHex << VectorGetIntX(x) << " y0=0x" << VectorGetIntX(y) << StreamDec << " expected=" << expected->error << " actual=" << actual->error << '\n';
    if(quantize){
        const SIMDVectorSingle expectedQuantized = Encode(x, Reference), actualQuantized = Encode(x, evaluate);
        for(usize lane = 0u; lane < 4u; ++lane){
            if(VectorGetIntByIndex(expectedQuantized, lane) != VectorGetIntByIndex(actualQuantized, lane)){
                if(counts.quantized < 4u)
                    NWB_COUT << "quantized category=" << category << " lane=" << lane << " x=0x" << StreamHex << VectorGetIntByIndex(x, lane) << StreamDec << " expected=" << VectorGetByIndex(expectedQuantized, lane) << " actual=" << VectorGetByIndex(actualQuantized, lane) << '\n';
                ++counts.quantized;
            }
        }
    }
    return true;
}

void PrintCounts(const StringView category, const Counts& counts){
    NWB_COUT << "summary category=" << category << " vectors=" << counts.vectors << " lanes=" << counts.vectors * 4u << " bits=" << counts.bits << " flags=" << counts.flags << " errno=" << counts.errors << " quantized=" << counts.quantized << '\n';
}

[[nodiscard]] u64 Verify(const Evaluate evaluate){
#pragma STDC FENV_ACCESS ON
    constexpr u32 s_Exceptional[]{ 0u, 0x80000000u, 1u, 0x80000001u, 0x007fffffu, 0x00800000u, 0x80800000u, 0x7f7fffffu, 0xff7fffffu, 0x7f800000u, 0xff800000u, 0x7fc00123u, 0xffc00124u, 0x7f800125u, 0xff800126u,
        0x3f800000u, 0xbf800000u, 0x40000000u, 0xc0000000u, 0x3f000000u, 0xbf000000u, 0x40400000u, 0xc0400000u, 0x4b7fffffu, 0x4b800000u, 0x4b800001u, 0x3eaaaaabu, 0x42fe0000u, 0xc3160000u };
    constexpr i32 s_Modes[]{ FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    u64 failures = 0u;
    for(const i32 mode : s_Modes){
        if(!SetFloatingPointRoundingMode(mode))
            return failures + 1u;
        NWB_COUT << "rounding=" << mode << '\n';
        Counts exceptional{}, random{}, positive{}, srgb{};
        for(const u32 x : s_Exceptional){
            for(const u32 y : s_Exceptional){
                for(usize lane = 0u; lane < 4u; ++lane){
                    const SIMDVectorSingle left = VectorSetIntByIndex(s_SIMDOne, x, lane), right = VectorSetIntByIndex(s_SIMDOne, y, lane);
                    if(!Compare("exceptional", evaluate, left, right, exceptional))
                        return failures + 1u;
                }
            }
        }
        u64 seed = 0x8923409102431291ull;
        for(usize index = 0u; index < 32768u; ++index){
            UInt4 x{}, y{};
            for(usize lane = 0u; lane < 4u; ++lane){
                x[lane] = static_cast<u32>(NextSeed(seed));
                y[lane] = static_cast<u32>(NextSeed(seed));
            }
            if(!Compare("random", evaluate, BitCast<SIMDVectorSingle>(x), BitCast<SIMDVectorSingle>(y), random))
                return failures + 1u;
        }
        for(usize index = 0u; index < 65536u; ++index){
            UInt4 x{}, y{};
            for(usize lane = 0u; lane < 4u; ++lane){
                x[lane] = 0x00800000u + static_cast<u32>(NextSeed(seed) % 0x7f000000u);
                y[lane] = (0x3c000000u + static_cast<u32>(NextSeed(seed) % 0x06000000u)) | (static_cast<u32>(NextSeed(seed)) & 0x80000000u);
            }
            if(!Compare("positive_finite", evaluate, BitCast<SIMDVectorSingle>(x), BitCast<SIMDVectorSingle>(y), positive))
                return failures + 1u;
        }
        for(u32 first = 0u; first < 65536u; first += 4u){
            const SIMDVectorSingle linear = VectorScale(VectorSet(static_cast<f32>(first), static_cast<f32>(first + 1u), static_cast<f32>(first + 2u), static_cast<f32>(first + 3u)), 1.0f / 65535.0f);
            if(!Compare("srgb_uniform16", evaluate, linear, VectorReplicate(s_EncodeExponent), srgb, true))
                return failures + 1u;
        }
        for(u32 first = 0u; first < 256u; first += 4u){
            const SIMDVectorSingle encoded = VectorScale(VectorSet(static_cast<f32>(first), static_cast<f32>(first + 1u), static_cast<f32>(first + 2u), static_cast<f32>(first + 3u)), 1.0f / 255.0f);
            const SIMDVectorSingle base = VectorScale(VectorAdd(encoded, VectorReplicate(0.055f)), s_DecodeScale);
            if(!Compare("srgb_decode8", evaluate, base, VectorReplicate(2.4f), srgb))
                return failures + 1u;
        }
        // Search every 8-bit transition in the actual rounded reference expression.
        for(u32 transition = 1u; transition < 256u; ++transition){
            u32 lower = 0u, upper = 0x3f800000u;
            while(lower < upper){
                const u32 midpoint = lower + (upper - lower) / 2u;
                if(VectorGetX(Encode(VectorReplicate(BitCast<f32>(midpoint)), Reference)) < static_cast<f32>(transition))
                    lower = midpoint + 1u;
                else
                    upper = midpoint;
            }
            for(i32 offset = -16; offset <= 16; ++offset){
                const SIMDVectorSingle linear = VectorReplicate(BitCast<f32>(lower + offset));
                if(!Compare("srgb_transition", evaluate, linear, VectorReplicate(s_EncodeExponent), srgb, true))
                    return failures + 1u;
            }
        }
        PrintCounts("exceptional", exceptional);
        PrintCounts("random", random);
        PrintCounts("positive_finite", positive);
        PrintCounts("srgb", srgb);
        for(const auto& counts : { exceptional, random, positive, srgb })
            failures += counts.bits + counts.flags + counts.errors + counts.quantized;
    }
    return failures;
}

#if defined(NWB_PLATFORM_WINDOWS)
[[nodiscard]] u32 ObserveTrap(const Evaluate evaluate, const SIMDVectorSingle x, const SIMDVectorSingle y){
    u32 initial{}, control{}, exception{};
    if(_controlfp_s(&initial, 0u, 0u) != 0)
        return 1u;
    const u32 previousFlags = _clearfp();
    if(_controlfp_s(&control, initial & ~(_EM_INVALID | _EM_OVERFLOW | _EM_ZERODIVIDE), _MCW_EM) != 0)
        return 2u;
    __try{
        const SIMDVectorSingle value = evaluate(x, y);
        volatile u32 sink = VectorGetIntX(value);
        if(sink == previousFlags)
            exception = 0u;
    }
    __except(EXCEPTION_EXECUTE_HANDLER){
        exception = _exception_code();
    }
    const u32 raisedFlags = _clearfp();
    if(_controlfp_s(&control, initial, _MCW_EM) != 0)
        return 3u;
    NWB_COUT << "trap exception=0x" << StreamHex << exception << " flags=0x" << raisedFlags << " mask=0x" << (control & _MCW_EM) << StreamDec << '\n';
    return exception;
}
#endif

[[nodiscard]] i32 VerifyTraps([[maybe_unused]] const Evaluate evaluate){
#if defined(NWB_PLATFORM_WINDOWS)
    const SIMDVectorSingle x[]{ VectorSet(-1.0f, 1.0f, 1.0f, 1.0f), VectorSet(0x1.fffffep127f, 1.0f, 1.0f, 1.0f), VectorSet(0.0f, 1.0f, 1.0f, 1.0f), VectorSet(0x1.fffffep127f, -1.0f, 1.0f, 1.0f), VectorSet(-1.0f, 0x1.fffffep127f, 1.0f, 1.0f) };
    const SIMDVectorSingle y[]{ VectorSet(0.5f, 1.0f, 1.0f, 1.0f), VectorSet(2.0f, 1.0f, 1.0f, 1.0f), VectorSet(-1.0f, 1.0f, 1.0f, 1.0f), VectorSet(2.0f, 0.5f, 1.0f, 1.0f), VectorSet(0.5f, 2.0f, 1.0f, 1.0f) };
    u32 failures = 0u;
    constexpr u32 s_ReferenceCategories[]{ EXCEPTION_FLT_INVALID_OPERATION, EXCEPTION_FLT_OVERFLOW, EXCEPTION_FLT_DIVIDE_BY_ZERO };
    bool delivered = true;
    for(usize index = 0u; index < LengthOf(x); ++index){
        NWB_COUT << "trap case=" << index << " reference\n";
        const u32 expected = ObserveTrap(Reference, x[index], y[index]);
        NWB_COUT << "trap case=" << index << " candidate\n";
        const u32 actual = ObserveTrap(evaluate, x[index], y[index]);
        delivered = delivered && (index < LengthOf(s_ReferenceCategories) ? expected == s_ReferenceCategories[index] : (expected == EXCEPTION_FLT_INVALID_OPERATION || expected == EXCEPTION_FLT_OVERFLOW));
        failures += expected != actual;
    }
    NWB_COUT << "trap differences=" << failures << " distinguishable=" << delivered << '\n';
    return delivered ? (failures == 0u ? 0 : 1) : 2;
#else
    NWB_COUT << "trap qualification unavailable on this platform\n";
    return 2;
#endif
}


#if defined(NWB_PLATFORM_WINDOWS)
struct TrapEdgeObservation{
    friend bool operator==(const TrapEdgeObservation&, const TrapEdgeObservation&)noexcept = default;


    u32 code{}, flags{}, control{};
    i32 error{};
    u64 nativeStatus{}, nativeControl{};
};

[[nodiscard]] Expected<TrapEdgeObservation> ObserveTrapEdge(const Evaluate evaluate, const SIMDVectorSingle x, const SIMDVectorSingle y, const u32 enabled){
    u32 initial{}, control{}, exception{};
    if(_controlfp_s(&initial, 0u, 0u) != 0)
        return MakeUnexpected(Failure{});
    if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT))
        return MakeUnexpected(Failure{});
#if defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    u64 initialNativeControl{};
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(initialNativeControl));
    const u64 clearNativeStatus = 0u;
    __asm__ __volatile__("msr fpsr, %0" : : "r"(clearNativeStatus));
#endif
    if(_controlfp_s(&control, (initial | _MCW_EM) & ~enabled, _MCW_EM) != 0)
        return MakeUnexpected(Failure{});
    errno = E2BIG;
    __try{
        const SIMDVectorSingle value = evaluate(x, y);
        volatile u32 sink = VectorGetIntX(value);
        static_cast<void>(sink);
    }
    __except(EXCEPTION_EXECUTE_HANDLER){
        exception = _exception_code();
    }
    const i32 observedError = errno;
    u64 nativeStatus{}, nativeControl{};
#if defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(nativeStatus));
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(nativeControl));
#endif
    u32 observedControl{};
    if(_controlfp_s(&observedControl, 0u, 0u) != 0)
        return MakeUnexpected(Failure{});
    const u32 flags = _clearfp();
    if(_controlfp_s(&control, initial, _MCW_EM) != 0)
        return MakeUnexpected(Failure{});
#if defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    __asm__ __volatile__("msr fpcr, %0" : : "r"(initialNativeControl));
#endif
    return TrapEdgeObservation{ exception, flags, observedControl, observedError, nativeStatus, nativeControl };
}
#endif

[[nodiscard]] i32 VerifyTrapEdges(const Evaluate evaluate){
#if defined(NWB_PLATFORM_WINDOWS)
    const i32 savedError = errno;
#if defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    u64 savedNativeStatus{}, savedNativeControl{};
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(savedNativeStatus));
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(savedNativeControl));
#endif
    const auto environment = HoldFloatingPointExceptions();
    if(!environment)
        return 2;
    ScopeExit restore([&]()noexcept{
        if(!SetFloatingPointEnvironment(*environment))
            __builtin_trap();
#if defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
        __asm__ __volatile__("msr fpsr, %0" : : "r"(savedNativeStatus));
        __asm__ __volatile__("msr fpcr, %0" : : "r"(savedNativeControl));
#endif
        errno = savedError;
    });
    constexpr i32 s_Modes[]{ FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
#if defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    constexpr u32 s_DenormalModes[]{ _DN_SAVE, _DN_FLUSH };
#else
    constexpr u32 s_DenormalModes[]{ _DN_SAVE };
#endif
    const SIMDVectorSingle x[]{ VectorSet(0.5f, 1.0f, 1.0f, 1.0f), VectorSet(0.5f, 1.0f, 1.0f, 1.0f), VectorSetInt(1u, 0x3f800000u, 0x3f800000u, 0x3f800000u) };
    const SIMDVectorSingle y[]{ VectorSet(149.0f, 1.0f, 1.0f, 1.0f), VectorSet(127.0f, 1.0f, 1.0f, 1.0f), VectorSet(0.0f, 1.0f, 1.0f, 1.0f) };
    u32 differences = 0u, cases = 0u;
    bool delivered = true;
    for(const u32 denormalMode : s_DenormalModes){
        u32 control{};
        if(_controlfp_s(&control, denormalMode, _MCW_DN) != 0 || (control & _MCW_DN) != denormalMode)
            return 2;
        for(const i32 mode : s_Modes){
            if(!SetFloatingPointRoundingMode(mode))
                return 2;
            for(const u32 enabled : { _EM_UNDERFLOW, _EM_UNDERFLOW | _EM_INEXACT }){
                for(usize index = 0u; index < LengthOf(x); ++index){
                    const auto expected = ObserveTrapEdge(Reference, x[index], y[index], enabled), actual = ObserveTrapEdge(evaluate, x[index], y[index], enabled);
                    if(!expected || !actual)
                        return 2;
                    if(index < 2u && denormalMode == _DN_SAVE)
                        delivered = delivered && expected->code == EXCEPTION_FLT_UNDERFLOW;
                    const bool different = *expected != *actual;
                    differences += different;
                    ++cases;
                    NWB_COUT << "trap_edge DN=" << denormalMode << " rounding=" << mode << " enabled=" << enabled << " case=" << index
                             << " expected=" << expected->code << '/' << expected->flags << '/' << expected->control << '/' << expected->error << '/' << expected->nativeStatus << '/' << expected->nativeControl
                             << " actual=" << actual->code << '/' << actual->flags << '/' << actual->control << '/' << actual->error << '/' << actual->nativeStatus << '/' << actual->nativeControl
                             << " differs=" << different << '\n';
                }
            }
        }
    }
    NWB_COUT << "trap edge cases=" << cases << " differences=" << differences << " distinguishable=" << delivered << '\n';
    return delivered ? (differences == 0u ? 0 : 1) : 2;
#else
    static_cast<void>(evaluate);
    return 2;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(const int argc, const char* const* argv){
    using namespace __hidden_pow_qualification;
    if(argc != 2 && argc != 3){
        NWB_CERR << "usage: simd_pow_qualification production|packed_single|packed_double [traps]\n";
        return 2;
    }
    const StringView variant(argv[1]);
    const Evaluate evaluate = variant == "production" ? Production : (variant == "packed_single" ? PackedSingle : (variant == "packed_double" ? PackedDouble : nullptr));
    if(!evaluate)
        return 2;
    if(argc == 3){
        if(StringView(argv[2]) != "traps")
            return 2;
        const i32 originalTraps = VerifyTraps(evaluate);
        return originalTraps == 0 ? VerifyTrapEdges(evaluate) : originalTraps;
    }
    const i32 originalError = errno;
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    u64 originalNativeStatus{};
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(originalNativeStatus));
#endif
    const auto environment = HoldFloatingPointExceptions();
    if(!environment)
        return 2;
    ScopeExit restore([&]()noexcept{
        if(!SetFloatingPointEnvironment(*environment))
            __builtin_trap();
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
        __asm__ __volatile__("msr fpsr, %0" : : "r"(originalNativeStatus));
#endif
        errno = originalError;
    });
    NWB_COUT << "variant=" << variant << '\n';
    const u64 failures = Verify(evaluate);
    NWB_COUT << "qualification differences=" << failures << " admitted=" << (failures == 0u) << '\n';
    return failures == 0u ? 0 : 1;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

