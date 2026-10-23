// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../bit.h"
#include "type.h"
#include "constant.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace HalfConvertDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// IEEE-754 binary32/binary16 layout; scalar fallback documents the shared bit contract.
inline constexpr u32 s_F32ToF16OverflowBits = 0x47800000u;
inline constexpr u32 s_F32ToF16UnderflowBits = 0x33000000u;
inline constexpr u32 s_F32ToF16MinNormalBits = 0x38800000u;
inline constexpr u32 s_F32ToF16NormalExponentAdjustment = 0xc8000000u;
inline constexpr u32 s_F32ToF16SubnormalShiftBase = 125u;
inline constexpr u32 s_F32ToF16SignBitShift = 16u;
inline constexpr u32 s_F32ToF16MantissaShift = 13u;
inline constexpr u32 s_F16SignBitMask = 0x8000u;
inline constexpr u32 s_F16ExponentBitMask = 0x7c00u;
inline constexpr u32 s_F16ExponentFieldMask = 0x1fu;
inline constexpr u32 s_F16MantissaBitCount = 10u;
inline constexpr u32 s_F16MantissaBitMask = 0x03ffu;
inline constexpr u32 s_F16ImplicitMantissaBit = 0x0400u;
inline constexpr u32 s_F16QuietNanBit = 0x0200u;
inline constexpr u32 s_F16NormalRoundingBias = 0x0fffu;
inline constexpr i32 s_F32ToF16ExponentBiasDelta = 112;
inline constexpr i32 s_F16InfinityF32Exponent = static_cast<i32>(s_F16ExponentFieldMask) + s_F32ToF16ExponentBiasDelta;
inline constexpr i32 s_F16ZeroF32Exponent = -s_F32ToF16ExponentBiasDelta;
inline constexpr usize s_F16CVectorElementCount = 4u;
inline constexpr usize s_F16CVectorElementShift = 2u;
inline constexpr int s_F16CNearestRoundingControl = 0;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE Half FloatToHalfScalar(const f32 value)noexcept{
    u32 bits = BitCast<u32>(value);
    const u32 sign = (bits & s_F32SignBitMask) >> s_F32ToF16SignBitShift;
    bits &= s_F32AbsoluteBitMask;

    u32 result = 0u;
    if(bits >= s_F32ToF16OverflowBits){
        result = s_F16ExponentBitMask | ((bits > s_F32InfinityBits) ? (s_F16QuietNanBit | ((bits >> s_F32ToF16MantissaShift) & s_F16MantissaBitMask)) : 0u);
    }
    else if(bits <= s_F32ToF16UnderflowBits){
        result = 0u;
    }
    else if(bits < s_F32ToF16MinNormalBits){
        const u32 shift = s_F32ToF16SubnormalShiftBase - (bits >> s_F32MantissaBitCount);
        bits = s_F32ImplicitMantissaBit | (bits & s_F32MantissaBitMask);
        result = bits >> (shift + 1u);
        const u32 sticky = (bits & ((1u << shift) - 1u)) != 0u ? 1u : 0u;
        result += (result | sticky) & ((bits >> shift) & 1u);
    }
    else{
        bits += s_F32ToF16NormalExponentAdjustment;
        result = ((bits + s_F16NormalRoundingBias + ((bits >> s_F32ToF16MantissaShift) & 1u)) >> s_F32ToF16MantissaShift) & (s_F16SignBitMask - 1u);
    }

    return static_cast<Half>(result | sign);
}

[[nodiscard]] NWB_INLINE f32 HalfToFloatScalar(const Half value)noexcept{
    u32 mantissa = static_cast<u32>(value & s_F16MantissaBitMask);
    i32 exponent = static_cast<i32>(value & s_F16ExponentBitMask);
    if(exponent == static_cast<i32>(s_F16ExponentBitMask)){
        exponent = s_F16InfinityF32Exponent;
    }
    else if(exponent != 0){
        exponent = static_cast<i32>((value >> s_F16MantissaBitCount) & s_F16ExponentFieldMask);
    }
    else if(mantissa != 0u){
        exponent = 1;
        do{
            --exponent;
            mantissa <<= 1u;
        }while((mantissa & s_F16ImplicitMantissaBit) == 0u);
        mantissa &= s_F16MantissaBitMask;
    }
    else{
        exponent = s_F16ZeroF32Exponent;
    }

    const u32 result = ((static_cast<u32>(value) & s_F16SignBitMask) << s_F32ToF16SignBitShift) | (static_cast<u32>(exponent + s_F32ToF16ExponentBiasDelta) << s_F32MantissaBitCount) | (mantissa << s_F32ToF16MantissaShift);
    return BitCast<f32>(result);
}

#if defined(NWB_HAS_F16C)
[[nodiscard]] NWB_INLINE Half FloatToHalfF16C(const f32 value)noexcept{
    const __m128 floatValue = _mm_set_ss(value);
    const __m128i halfValue = _mm_cvtps_ph(floatValue, s_F16CNearestRoundingControl);
    return static_cast<Half>(_mm_cvtsi128_si32(halfValue));
}

[[nodiscard]] NWB_INLINE f32 HalfToFloatF16C(const Half value)noexcept{
    const __m128i halfValue = _mm_cvtsi32_si128(static_cast<int>(value));
    const __m128 floatValue = _mm_cvtph_ps(halfValue);
    return _mm_cvtss_f32(floatValue);
}

NWB_INLINE Half* FloatBufferToHalfF16C(Half* outHalfBuffer, const f32* floatBuffer, const usize count)noexcept{
    usize i = 0u;
    const usize vectorCount = count >> s_F16CVectorElementShift;
    for(usize vectorIndex = 0u; vectorIndex < vectorCount; ++vectorIndex){
        const __m128 floatValue = _mm_loadu_ps(floatBuffer + i);
        const __m128i halfValue = _mm_cvtps_ph(floatValue, s_F16CNearestRoundingControl);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(outHalfBuffer + i), halfValue);
        i += s_F16CVectorElementCount;
    }

    for(; i < count; ++i)
        outHalfBuffer[i] = FloatToHalfF16C(floatBuffer[i]);
    return outHalfBuffer;
}

NWB_INLINE f32* HalfBufferToFloatF16C(f32* outFloatBuffer, const Half* halfBuffer, const usize count)noexcept{
    usize i = 0u;
    const usize vectorCount = count >> s_F16CVectorElementShift;
    for(usize vectorIndex = 0u; vectorIndex < vectorCount; ++vectorIndex){
        const __m128i halfValue = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(halfBuffer + i));
        const __m128 floatValue = _mm_cvtph_ps(halfValue);
        _mm_storeu_ps(outFloatBuffer + i, floatValue);
        i += s_F16CVectorElementCount;
    }

    for(; i < count; ++i)
        outFloatBuffer[i] = HalfToFloatF16C(halfBuffer[i]);
    return outFloatBuffer;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UnsignedFloatConvertDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ExponentFieldMask = 0x1fu;
inline constexpr u32 s_ExponentBitCount = 5u;
inline constexpr u32 s_ExponentBias = 15u;
inline constexpr u32 s_MaxMantissaBitCount = 10u;
inline constexpr u32 s_OverflowBits = 0x47800000u;
inline constexpr u32 s_MinNormalBits = 0x38800000u;
inline constexpr u32 s_NormalRoundingBiasBits = 0xc8000000u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<u32 MantissaBitCount>
[[nodiscard]] NWB_INLINE u32 ConvertFloatToUnsignedFloat(const f32 value)noexcept{
    static_assert(MantissaBitCount > 0u, "Unsigned floating-point values need a mantissa");
    static_assert(MantissaBitCount <= UnsignedFloatConvertDetail::s_MaxMantissaBitCount, "Unsupported unsigned floating-point mantissa width");

    u32 bits = BitCast<u32>(value);
    const u32 absBits = bits & s_F32AbsoluteBitMask;
    const u32 targetInf = UnsignedFloatConvertDetail::s_ExponentFieldMask << MantissaBitCount;
    const u32 targetMantissaMask = (1u << MantissaBitCount) - 1u;

    if(absBits > s_F32InfinityBits){
        const u32 payload = (absBits >> (s_F32MantissaBitCount - MantissaBitCount)) & targetMantissaMask;
        return targetInf | (payload != 0u ? payload : 1u);
    }
    if((bits & s_F32SignBitMask) != 0u || absBits == 0u)
        return 0u;

    bits = absBits;
    if(bits >= UnsignedFloatConvertDetail::s_OverflowBits)
        return targetInf;

    const u32 zeroThreshold = (s_F32ExponentBias - UnsignedFloatConvertDetail::s_ExponentBias - MantissaBitCount) << s_F32MantissaBitCount;
    if(bits <= zeroThreshold)
        return 0u;

    if(bits < UnsignedFloatConvertDetail::s_MinNormalBits){
        const u32 sourceExponent = bits >> s_F32MantissaBitCount;
        const u32 roundShift = (s_F32ExponentBias - 1u) - sourceExponent + (UnsignedFloatConvertDetail::s_MaxMantissaBitCount - MantissaBitCount);
        bits = s_F32ImplicitMantissaBit | (bits & s_F32MantissaBitMask);
        u32 result = bits >> roundShift;
        const u32 sticky = (bits & ((1u << (roundShift - 1u)) - 1u)) != 0u ? 1u : 0u;
        result += (result | sticky) & ((bits >> (roundShift - 1u)) & 1u);
        return result;
    }

    const u32 shift = s_F32MantissaBitCount - MantissaBitCount;
    bits += UnsignedFloatConvertDetail::s_NormalRoundingBiasBits;
    return ((bits + ((1u << (shift - 1u)) - 1u) + ((bits >> shift) & 1u)) >> shift) & ((1u << (UnsignedFloatConvertDetail::s_ExponentBitCount + MantissaBitCount)) - 1u);
}

[[nodiscard]] NWB_INLINE Half ConvertFloatToHalf(const f32 value)noexcept{
#if defined(NWB_HAS_F16C)
    return HalfConvertDetail::FloatToHalfF16C(value);
#else
    return HalfConvertDetail::FloatToHalfScalar(value);
#endif
}

[[nodiscard]] NWB_INLINE f32 ConvertHalfToFloat(const Half value)noexcept{
#if defined(NWB_HAS_F16C)
    return HalfConvertDetail::HalfToFloatF16C(value);
#else
    return HalfConvertDetail::HalfToFloatScalar(value);
#endif
}

NWB_INLINE Half* ConvertFloatBufferToHalf(Half* outHalfBuffer, const f32* floatBuffer, const usize count)noexcept{
#if defined(NWB_HAS_F16C)
    return HalfConvertDetail::FloatBufferToHalfF16C(outHalfBuffer, floatBuffer, count);
#else
    for(usize i = 0u; i < count; ++i)
        outHalfBuffer[i] = ConvertFloatToHalf(floatBuffer[i]);
    return outHalfBuffer;
#endif
}

NWB_INLINE f32* ConvertHalfBufferToFloat(f32* outFloatBuffer, const Half* halfBuffer, const usize count)noexcept{
#if defined(NWB_HAS_F16C)
    return HalfConvertDetail::HalfBufferToFloatF16C(outFloatBuffer, halfBuffer, count);
#else
    for(usize i = 0u; i < count; ++i)
        outFloatBuffer[i] = ConvertHalfToFloat(halfBuffer[i]);
    return outFloatBuffer;
#endif
}

[[nodiscard]] NWB_INLINE Half2U MakeHalf2U(const f32 x, const f32 y)noexcept{
    return Half2U(ConvertFloatToHalf(x), ConvertFloatToHalf(y));
}

[[nodiscard]] NWB_INLINE Half4U MakeHalf4U(const f32 x, const f32 y, const f32 z, const f32 w)noexcept{
    const Float4U values(x, y, z, w);
    Half4U result;
    ConvertFloatBufferToHalf(result.raw, values.raw, HalfConvertDetail::s_F16CVectorElementCount);
    return result;
}

[[nodiscard]] NWB_INLINE Float2U LoadHalf2U(const Half2U& value)noexcept{
    return Float2U(ConvertHalfToFloat(value.x), ConvertHalfToFloat(value.y));
}

[[nodiscard]] NWB_INLINE Float4U LoadHalf4U(const Half4U& value)noexcept{
    Float4U result{};
    ConvertHalfBufferToFloat(result.raw, value.raw, HalfConvertDetail::s_F16CVectorElementCount);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

