// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_clear_pattern.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkClearColorValue BuildTextureClearColorValue(const Color& clearColor)noexcept{
    VkClearColorValue clearValue{};
    clearValue.float32[0] = clearColor.r;
    clearValue.float32[1] = clearColor.g;
    clearValue.float32[2] = clearColor.b;
    clearValue.float32[3] = clearColor.a;
    return clearValue;
}

VkClearColorValue BuildTextureClearColorValue(const UIntColor& clearColor)noexcept{
    VkClearColorValue clearValue{};
    clearValue.uint32[0] = clearColor.r;
    clearValue.uint32[1] = clearColor.g;
    clearValue.uint32[2] = clearColor.b;
    clearValue.uint32[3] = clearColor.a;
    return clearValue;
}

VkClearColorValue BuildTextureClearColorValue(const IntColor& clearColor)noexcept{
    VkClearColorValue clearValue{};
    clearValue.int32[0] = clearColor.r;
    clearValue.int32[1] = clearColor.g;
    clearValue.int32[2] = clearColor.b;
    clearValue.int32[3] = clearColor.a;
    return clearValue;
}

b8 TextureColorClearValueTypeMatchesFormat(const FormatInfo& formatInfo, const b8 integerValue, const b8 signedIntegerValue)noexcept{
    if(formatInfo.kind == FormatKind::Integer)
        return integerValue && formatInfo.isSigned == signedIntegerValue;

    return !integerValue;
}

f32 QuantizeUNormClearScalar(const f32 value, const f32 maxValue)noexcept{
    const f32 scaled = Saturate(value) * maxValue;
    return Min(Floor(scaled + s_ClearFloatRoundingBias), maxValue);
}

SIMDVectorSingle QuantizeUNormClearVector(const SIMDVectorSingle value, const SIMDVectorSingle maxValue)noexcept{
    const SIMDVectorSingle scaled = VectorMultiply(VectorSaturate(value), maxValue);
    return VectorMin(VectorFloor(VectorAdd(scaled, VectorReplicate(s_ClearFloatRoundingBias))), maxValue);
}

f32 QuantizeSNormClearScalar(const f32 value, const f32 maxValue)noexcept{
    const f32 clamped = Clamp(value, -1.0f, 1.0f);
    const f32 scaled = clamped * maxValue;
    const f32 magnitude = Floor(Abs(scaled) + s_ClearFloatRoundingBias);
    return scaled >= 0.0f ? magnitude : -magnitude;
}

SIMDVectorSingle QuantizeSNormClearVector(const SIMDVectorSingle value, const SIMDVectorSingle maxValue)noexcept{
    const SIMDVectorSingle clamped = VectorClamp(value, VectorReplicate(-1.0f), VectorReplicate(1.0f));
    const SIMDVectorSingle scaled = VectorMultiply(clamped, maxValue);
    const SIMDVectorSingle magnitude = VectorFloor(VectorAdd(VectorAbs(scaled), VectorReplicate(s_ClearFloatRoundingBias)));
    return VectorSelect(VectorNegate(magnitude), magnitude, VectorGreaterOrEqual(scaled, VectorZero()));
}

SIMDVectorSingle LinearToSRGBClearVector(const SIMDVectorSingle linear)noexcept{
    const SIMDVectorSingle clamped = VectorSaturate(linear);
    const SIMDVectorSingle threshold = VectorReplicate(s_SRGBClearLinearThreshold);
    const SIMDVectorSingle linearPart = VectorMultiply(clamped, VectorReplicate(s_SRGBClearLinearScale));
    const SIMDVectorSingle power = VectorPow(clamped, VectorReplicate(1.0f / s_SRGBClearNonlinearExponent));
    const SIMDVectorSingle nonlinearPart = VectorSubtract(VectorMultiply(VectorReplicate(s_SRGBClearNonlinearScale), power), VectorReplicate(s_SRGBClearNonlinearOffset));
    return VectorSelect(nonlinearPart, linearPart, VectorLessOrEqual(clamped, threshold));
}

void WriteBC1ColorClearBlock(const SIMDVectorSingle sourceRgba, const b8 srgb, NotNull<u8*> outPattern)noexcept{
    const SIMDVectorSingle srgbEncodedTriple = LinearToSRGBClearVector(sourceRgba);
    const SIMDVectorSingle srgbSelectMask = srgb ? VectorTrueInt() : VectorFalseInt();
    const SIMDVectorSingle encodedRgb = VectorSelect(sourceRgba, srgbEncodedTriple, srgbSelectMask);
    const SIMDVectorSingle maxRgb = VectorSet(static_cast<f32>((1u << s_RGB565RedBitCount) - 1u), static_cast<f32>((1u << s_RGB565GreenBitCount) - 1u), static_cast<f32>((1u << s_RGB565RedBitCount) - 1u), 1.0f);
    const SIMDVectorSingle quantized = VectorMin(VectorFloor(VectorAdd(VectorMultiply(VectorSaturate(encodedRgb), maxRgb), VectorReplicate(s_ClearFloatRoundingBias))), maxRgb);
    u16 color0 = static_cast<u16>((static_cast<u32>(VectorGetX(quantized)) << s_RGB565RedBitShift) | (static_cast<u32>(VectorGetY(quantized)) << s_RGB565GreenBitShift) | static_cast<u32>(VectorGetZ(quantized)));
    u16 color1 = color0;
    u32 indices = 0u;
    if(static_cast<u32>(VectorGetW(quantized)) == 0u){
        color0 = 0u;
        color1 = 0u;
        indices = s_BC1TransparentColorIndices;
    }

    WriteClearPatternValue(color0, outPattern);
    WriteClearPatternValue(color1, MakeNotNull(outPattern.get() + sizeof(color0)));
    WriteClearPatternValue(indices, MakeNotNull(outPattern.get() + sizeof(color0) + sizeof(color1)));
}

void WriteBC4ClearBlockEndpoint(const u8 endpoint, NotNull<u8*> outPattern)noexcept{
    outPattern.get()[0] = endpoint;
    outPattern.get()[1] = endpoint;
    for(u32 byteIndex = s_BC4EndpointByteCount; byteIndex < s_BCSingleClearBlockBytes; ++byteIndex)
        outPattern.get()[byteIndex] = 0u;
}

void WriteBC4UNormClearBlock(const f32 value, NotNull<u8*> outPattern)noexcept{
    const u8 endpoint = static_cast<u8>(QuantizeUNormClearScalar(value, static_cast<f32>(Limit<u8>::s_Max)));
    WriteBC4ClearBlockEndpoint(endpoint, outPattern);
}

void WriteBC4SNormClearBlock(const f32 value, NotNull<u8*> outPattern)noexcept{
    const i8 endpoint = static_cast<i8>(QuantizeSNormClearScalar(value, static_cast<f32>(Limit<i8>::s_Max)));
    WriteBC4ClearBlockEndpoint(static_cast<u8>(endpoint), outPattern);
}

Expected<TextureClearPattern<s_TextureClearMaxPatternBytes>> BuildTextureFloatClearPattern(const Format::Enum format, const VkClearColorValue& clearValue)noexcept{
    TextureClearPattern<s_TextureClearMaxPatternBytes> result{};
    const auto pattern = MakeNotNull(result.bytes.data());
    const Array<f32, 4> values = {{ clearValue.float32[0], clearValue.float32[1], clearValue.float32[2], clearValue.float32[3] }};
    const SIMDVectorSingle sourceValues = VectorSet(values[0], values[1], values[2], values[3]);

    auto writeUNorm8Components = [&](const u32 componentCount, const b8 srgb)noexcept{
        const SIMDVectorSingle encodedValues = LinearToSRGBClearVector(sourceValues);
        const SIMDVectorSingle selectedValues = srgb ? VectorSelect(encodedValues, sourceValues, s_SIMDMaskW) : sourceValues;
        const SIMDVectorSingle quantized = QuantizeUNormClearVector(selectedValues, VectorReplicate(static_cast<f32>(Limit<u8>::s_Max)));
        for(u32 component = 0u; component < componentCount; ++component){
            const u8 packed = static_cast<u8>(VectorGetByIndex(quantized, static_cast<usize>(component)));
            WriteClearPatternValue(packed, MakeNotNull(pattern.get() + component * sizeof(packed)));
        }
        result.byteCount = componentCount * static_cast<u32>(sizeof(u8));
        return result;
    };
    auto writeUNorm8BGRAComponents = [&](const b8 srgb)noexcept{
        const SIMDVectorSingle orderedValues = VectorSwizzle<2, 1, 0, 3>(sourceValues);
        const SIMDVectorSingle encodedOrderedValues = LinearToSRGBClearVector(orderedValues);
        const SIMDVectorSingle selectedValues = srgb ? VectorSelect(encodedOrderedValues, orderedValues, s_SIMDMaskW) : orderedValues;
        const SIMDVectorSingle quantized = QuantizeUNormClearVector(selectedValues, VectorReplicate(static_cast<f32>(Limit<u8>::s_Max)));
        for(u32 component = 0u; component < s_TextureClearRGBAComponentCount; ++component){
            const u8 packed = static_cast<u8>(VectorGetByIndex(quantized, static_cast<usize>(component)));
            WriteClearPatternValue(packed, MakeNotNull(pattern.get() + component * sizeof(packed)));
        }
        result.byteCount = s_TextureClearRGBAComponentCount * static_cast<u32>(sizeof(u8));
        return result;
    };
    auto writeSNorm8Components = [&](const u32 componentCount)noexcept{
        const SIMDVectorSingle signedMagnitude = QuantizeSNormClearVector(sourceValues, VectorReplicate(static_cast<f32>(Limit<i8>::s_Max)));
        for(u32 component = 0u; component < componentCount; ++component){
            const i8 packed = static_cast<i8>(VectorGetByIndex(signedMagnitude, static_cast<usize>(component)));
            WriteClearPatternValue(packed, MakeNotNull(pattern.get() + component * sizeof(packed)));
        }
        result.byteCount = componentCount * static_cast<u32>(sizeof(i8));
        return result;
    };
    auto writeUNorm16Components = [&](const u32 componentCount)noexcept{
        const SIMDVectorSingle quantized = QuantizeUNormClearVector(sourceValues, VectorReplicate(static_cast<f32>(Limit<u16>::s_Max)));
        for(u32 component = 0u; component < componentCount; ++component){
            const u16 packed = static_cast<u16>(VectorGetByIndex(quantized, static_cast<usize>(component)));
            WriteClearPatternValue(packed, MakeNotNull(pattern.get() + component * sizeof(packed)));
        }
        result.byteCount = componentCount * static_cast<u32>(sizeof(u16));
        return result;
    };
    auto writeSNorm16Components = [&](const u32 componentCount)noexcept{
        const SIMDVectorSingle signedMagnitude = QuantizeSNormClearVector(sourceValues, VectorReplicate(static_cast<f32>(Limit<i16>::s_Max)));
        for(u32 component = 0u; component < componentCount; ++component){
            const i16 packed = static_cast<i16>(VectorGetByIndex(signedMagnitude, static_cast<usize>(component)));
            WriteClearPatternValue(packed, MakeNotNull(pattern.get() + component * sizeof(packed)));
        }
        result.byteCount = componentCount * static_cast<u32>(sizeof(i16));
        return result;
    };
    auto writeHalfComponents = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            const Half value = ConvertFloatToHalf(values[component]);
            WriteClearPatternValue(value, MakeNotNull(pattern.get() + component * sizeof(value)));
        }
        result.byteCount = componentCount * static_cast<u32>(sizeof(Half));
        return result;
    };
    auto writeFloatComponents = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            WriteClearPatternValue(values[component], MakeNotNull(pattern.get() + component * sizeof(f32)));
        }
        result.byteCount = componentCount * static_cast<u32>(sizeof(f32));
        return result;
    };
    auto writeUNorm4BGRAComponents = [&]()noexcept{
        const SIMDVectorSingle quantized4444 = QuantizeUNormClearVector(VectorSwizzle<2, 1, 0, 3>(sourceValues), VectorReplicate(static_cast<f32>((1u << s_ClearChannelBits4444) - 1u)));
        const u16 packed = static_cast<u16>(
            (static_cast<u32>(VectorGetX(quantized4444)) << s_ClearShift12)
            | (static_cast<u32>(VectorGetY(quantized4444)) << s_ClearShift8)
            | (static_cast<u32>(VectorGetZ(quantized4444)) << s_ClearShift4)
            | static_cast<u32>(VectorGetW(quantized4444))
        );
        WriteClearPatternValue(packed, pattern);
        result.byteCount = sizeof(packed);
        return result;
    };
    auto writeUNorm565BGRComponents = [&]()noexcept{
        const SIMDVectorSingle source565 = VectorAndInt(VectorSwizzle<2, 1, 0, 3>(sourceValues), s_SIMDMask3);
        const SIMDVectorSingle max565 = VectorSet(static_cast<f32>((1u << s_ClearChannelBits565R) - 1u), static_cast<f32>((1u << s_ClearChannelBits565G) - 1u), static_cast<f32>((1u << s_ClearChannelBits565R) - 1u), 0.0f);
        const SIMDVectorSingle quantized565 = VectorMin(VectorFloor(VectorAdd(VectorMultiply(VectorSaturate(source565), max565), VectorReplicate(s_ClearFloatRoundingBias))), max565);
        const u16 packed = static_cast<u16>((static_cast<u32>(VectorGetX(quantized565)) << s_ClearShift11) | (static_cast<u32>(VectorGetY(quantized565)) << s_ClearShift5) | static_cast<u32>(VectorGetZ(quantized565)));
        WriteClearPatternValue(packed, pattern);
        result.byteCount = sizeof(packed);
        return result;
    };
    auto writeUNorm5551BGRComponents = [&]()noexcept{
        const SIMDVectorSingle source5551 = VectorSwizzle<2, 1, 0, 3>(sourceValues);
        const SIMDVectorSingle max5551 = VectorSet(static_cast<f32>((1u << s_ClearChannelBits565R) - 1u), static_cast<f32>((1u << s_ClearChannelBits555) - 1u), static_cast<f32>((1u << s_ClearChannelBits565R) - 1u), 1.0f);
        const SIMDVectorSingle quantized5551 = VectorMin(VectorFloor(VectorAdd(VectorMultiply(VectorSaturate(source5551), max5551), VectorReplicate(s_ClearFloatRoundingBias))), max5551);
        const u16 packed = static_cast<u16>(
            (static_cast<u32>(VectorGetX(quantized5551)) << s_ClearShift11)
            | (static_cast<u32>(VectorGetY(quantized5551)) << s_ClearShift6)
            | (static_cast<u32>(VectorGetZ(quantized5551)) << 1u)
            | static_cast<u32>(VectorGetW(quantized5551))
        );
        WriteClearPatternValue(packed, pattern);
        result.byteCount = sizeof(packed);
        return result;
    };
    auto writeUNorm1010102RGBComponents = [&]()noexcept{
        const SIMDVectorSingle max1010102 = VectorSet(
            static_cast<f32>((1u << s_ClearChannelBits101010) - 1u),
            static_cast<f32>((1u << s_ClearChannelBits101010) - 1u),
            static_cast<f32>((1u << s_ClearChannelBits101010) - 1u),
            static_cast<f32>((1u << s_ClearChannelBits21030) - 1u)
        );
        const SIMDVectorSingle quantized1010102 = VectorMin(VectorFloor(VectorAdd(VectorMultiply(VectorSaturate(sourceValues), max1010102), VectorReplicate(s_ClearFloatRoundingBias))), max1010102);
        const u32 packed =
            static_cast<u32>(VectorGetX(quantized1010102))
            | (static_cast<u32>(VectorGetY(quantized1010102)) << s_ClearShift10)
            | (static_cast<u32>(VectorGetZ(quantized1010102)) << s_ClearShift20)
            | (static_cast<u32>(VectorGetW(quantized1010102)) << s_ClearShift30);
        WriteClearPatternValue(packed, pattern);
        result.byteCount = sizeof(packed);
        return result;
    };
    auto writeUFloat111110RGBComponents = [&]()noexcept{
        const u32 packed =
            ConvertFloatToUnsignedFloat<s_UFloat111110GreenMantissaBits>(values[0])
            | (ConvertFloatToUnsignedFloat<s_UFloat111110GreenMantissaBits>(values[1]) << s_ClearShift11)
            | (ConvertFloatToUnsignedFloat<s_UFloat111110BlueMantissaBits>(values[2]) << s_ClearShift22);
        WriteClearPatternValue(packed, pattern);
        result.byteCount = sizeof(packed);
        return result;
    };
    auto writeBC1Components = [&](const b8 srgb)noexcept{
        WriteBC1ColorClearBlock(sourceValues, srgb, pattern);
        result.byteCount = s_BCSingleClearBlockBytes;
        return result;
    };
    auto writeBC2Components = [&](const b8 srgb)noexcept{
        const u64 alphaNibble = static_cast<u64>(QuantizeUNormClearScalar(values[3], static_cast<f32>((1u << s_ClearChannelBits4444) - 1u)));
        const u64 alphaBits = (static_cast<u64>(s_BC2AlphaNibbleSplat) | (static_cast<u64>(s_BC2AlphaNibbleSplat) << s_ClearHighWordShift)) * alphaNibble;
        WriteClearPatternValue(alphaBits, pattern);
        WriteBC1ColorClearBlock(VectorSetW(sourceValues, 1.0f), srgb, MakeNotNull(pattern.get() + sizeof(alphaBits)));
        result.byteCount = s_BCDoubleClearBlockBytes;
        return result;
    };
    auto writeBC3Components = [&](const b8 srgb)noexcept{
        WriteBC4UNormClearBlock(values[3], pattern);
        WriteBC1ColorClearBlock(VectorSetW(sourceValues, 1.0f), srgb, MakeNotNull(pattern.get() + s_BCSingleClearBlockBytes));
        result.byteCount = s_BCDoubleClearBlockBytes;
        return result;
    };
    auto writeBC4UNormComponents = [&]()noexcept{
        WriteBC4UNormClearBlock(values[0], pattern);
        result.byteCount = s_BCSingleClearBlockBytes;
        return result;
    };
    auto writeBC4SNormComponents = [&]()noexcept{
        WriteBC4SNormClearBlock(values[0], pattern);
        result.byteCount = s_BCSingleClearBlockBytes;
        return result;
    };
    auto writeBC5UNormComponents = [&]()noexcept{
        WriteBC4UNormClearBlock(values[0], pattern);
        WriteBC4UNormClearBlock(values[1], MakeNotNull(pattern.get() + s_BCSingleClearBlockBytes));
        result.byteCount = s_BCDoubleClearBlockBytes;
        return result;
    };
    auto writeBC5SNormComponents = [&]()noexcept{
        WriteBC4SNormClearBlock(values[0], pattern);
        WriteBC4SNormClearBlock(values[1], MakeNotNull(pattern.get() + s_BCSingleClearBlockBytes));
        result.byteCount = s_BCDoubleClearBlockBytes;
        return result;
    };

    switch(format){
    case Format::R8_UNORM: return writeUNorm8Components(1u, false);
    case Format::R8_SNORM: return writeSNorm8Components(1u);
    case Format::RG8_UNORM: return writeUNorm8Components(s_TextureClearRGComponentCount, false);
    case Format::RG8_SNORM: return writeSNorm8Components(s_TextureClearRGComponentCount);
    case Format::RGB8_UNORM: return writeUNorm8Components(3u, false);
    case Format::RGBA8_UNORM: return writeUNorm8Components(s_TextureClearRGBAComponentCount, false);
    case Format::RGBA8_SNORM: return writeSNorm8Components(s_TextureClearRGBAComponentCount);
    case Format::RGBA8_UNORM_SRGB: return writeUNorm8Components(s_TextureClearRGBAComponentCount, true);
    case Format::BGRA8_UNORM: return writeUNorm8BGRAComponents(false);
    case Format::BGRA8_UNORM_SRGB: return writeUNorm8BGRAComponents(true);
    case Format::BGRA4_UNORM: return writeUNorm4BGRAComponents();
    case Format::B5G6R5_UNORM: return writeUNorm565BGRComponents();
    case Format::B5G5R5A1_UNORM: return writeUNorm5551BGRComponents();
    case Format::R10G10B10A2_UNORM: return writeUNorm1010102RGBComponents();
    case Format::R11G11B10_FLOAT: return writeUFloat111110RGBComponents();
    case Format::R16_UNORM: return writeUNorm16Components(1u);
    case Format::R16_SNORM: return writeSNorm16Components(1u);
    case Format::R16_FLOAT: return writeHalfComponents(1u);
    case Format::RG16_UNORM: return writeUNorm16Components(s_TextureClearRGComponentCount);
    case Format::RG16_SNORM: return writeSNorm16Components(s_TextureClearRGComponentCount);
    case Format::RG16_FLOAT: return writeHalfComponents(s_TextureClearRGComponentCount);
    case Format::RGBA16_UNORM: return writeUNorm16Components(s_TextureClearRGBAComponentCount);
    case Format::RGBA16_SNORM: return writeSNorm16Components(s_TextureClearRGBAComponentCount);
    case Format::RGBA16_FLOAT: return writeHalfComponents(s_TextureClearRGBAComponentCount);
    case Format::R32_FLOAT: return writeFloatComponents(1u);
    case Format::RG32_FLOAT: return writeFloatComponents(s_TextureClearRGComponentCount);
    case Format::RGB32_FLOAT: return writeFloatComponents(s_TextureClearColorComponentCount);
    case Format::RGBA32_FLOAT: return writeFloatComponents(s_TextureClearRGBAComponentCount);
    case Format::BC1_UNORM: return writeBC1Components(false);
    case Format::BC1_UNORM_SRGB: return writeBC1Components(true);
    case Format::BC2_UNORM: return writeBC2Components(false);
    case Format::BC2_UNORM_SRGB: return writeBC2Components(true);
    case Format::BC3_UNORM: return writeBC3Components(false);
    case Format::BC3_UNORM_SRGB: return writeBC3Components(true);
    case Format::BC4_UNORM: return writeBC4UNormComponents();
    case Format::BC4_SNORM: return writeBC4SNormComponents();
    case Format::BC5_UNORM: return writeBC5UNormComponents();
    case Format::BC5_SNORM: return writeBC5SNormComponents();
    default:
        return MakeUnexpected(Failure{});
    }
}

Expected<TextureClearPattern<s_TextureClearMaxPatternBytes>> BuildTextureUIntClearPattern(const Format::Enum format, const VkClearColorValue& clearValue)noexcept{
    TextureClearPattern<s_TextureClearMaxPatternBytes> result{};
    const auto pattern = MakeNotNull(result.bytes.data());
    const Array<u32, 4> values = {{ clearValue.uint32[0], clearValue.uint32[1], clearValue.uint32[2], clearValue.uint32[3] }};

    auto writeU8Components = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            const u8 value = static_cast<u8>(Min(values[component], static_cast<u32>(Limit<u8>::s_Max)));
            WriteClearPatternValue(value, MakeNotNull(pattern.get() + component * sizeof(value)));
        }
        result.byteCount = componentCount * static_cast<u32>(sizeof(u8));
        return result;
    };
    auto writeU16Components = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            const u16 value = static_cast<u16>(Min(values[component], static_cast<u32>(Limit<u16>::s_Max)));
            WriteClearPatternValue(value, MakeNotNull(pattern.get() + component * sizeof(value)));
        }
        result.byteCount = componentCount * static_cast<u32>(sizeof(u16));
        return result;
    };
    auto writeU32Components = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            WriteClearPatternValue(values[component], MakeNotNull(pattern.get() + component * sizeof(u32)));
        }
        result.byteCount = componentCount * static_cast<u32>(sizeof(u32));
        return result;
    };

    switch(format){
    case Format::R8_UINT: return writeU8Components(1u);
    case Format::RG8_UINT: return writeU8Components(s_TextureClearRGComponentCount);
    case Format::RGBA8_UINT: return writeU8Components(s_TextureClearRGBAComponentCount);
    case Format::R16_UINT: return writeU16Components(1u);
    case Format::RG16_UINT: return writeU16Components(s_TextureClearRGComponentCount);
    case Format::RGBA16_UINT: return writeU16Components(s_TextureClearRGBAComponentCount);
    case Format::R32_UINT: return writeU32Components(1u);
    case Format::RG32_UINT: return writeU32Components(s_TextureClearRGComponentCount);
    case Format::RGB32_UINT: return writeU32Components(s_TextureClearColorComponentCount);
    case Format::RGBA32_UINT: return writeU32Components(s_TextureClearRGBAComponentCount);
    default:
        return MakeUnexpected(Failure{});
    }
}

Expected<TextureClearPattern<s_TextureClearMaxPatternBytes>> BuildTextureIntClearPattern(const Format::Enum format, const VkClearColorValue& clearValue)noexcept{
    TextureClearPattern<s_TextureClearMaxPatternBytes> result{};
    const auto pattern = MakeNotNull(result.bytes.data());
    const Array<i32, 4> values = {{ clearValue.int32[0], clearValue.int32[1], clearValue.int32[2], clearValue.int32[3] }};

    auto clampValue = [](const i32 value, const i32 minValue, const i32 maxValue)noexcept{
        if(value < minValue)
            return minValue;
        if(value > maxValue)
            return maxValue;
        return value;
    };
    auto writeI8Components = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            const i8 value = static_cast<i8>(clampValue(values[component], static_cast<i32>(Limit<i8>::s_Min), static_cast<i32>(Limit<i8>::s_Max)));
            WriteClearPatternValue(value, MakeNotNull(pattern.get() + component * sizeof(value)));
        }
        result.byteCount = componentCount * static_cast<u32>(sizeof(i8));
        return result;
    };
    auto writeI16Components = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            const i16 value = static_cast<i16>(clampValue(values[component], static_cast<i32>(Limit<i16>::s_Min), static_cast<i32>(Limit<i16>::s_Max)));
            WriteClearPatternValue(value, MakeNotNull(pattern.get() + component * sizeof(value)));
        }
        result.byteCount = componentCount * static_cast<u32>(sizeof(i16));
        return result;
    };
    auto writeI32Components = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component)
            WriteClearPatternValue(values[component], MakeNotNull(pattern.get() + component * sizeof(i32)));
        result.byteCount = componentCount * static_cast<u32>(sizeof(i32));
        return result;
    };

    switch(format){
    case Format::R8_SINT: return writeI8Components(1u);
    case Format::RG8_SINT: return writeI8Components(s_TextureClearRGComponentCount);
    case Format::RGBA8_SINT: return writeI8Components(s_TextureClearRGBAComponentCount);
    case Format::R16_SINT: return writeI16Components(1u);
    case Format::RG16_SINT: return writeI16Components(s_TextureClearRGComponentCount);
    case Format::RGBA16_SINT: return writeI16Components(s_TextureClearRGBAComponentCount);
    case Format::R32_SINT: return writeI32Components(1u);
    case Format::RG32_SINT: return writeI32Components(s_TextureClearRGComponentCount);
    case Format::RGB32_SINT: return writeI32Components(s_TextureClearColorComponentCount);
    case Format::RGBA32_SINT: return writeI32Components(s_TextureClearRGBAComponentCount);
    default:
        return MakeUnexpected(Failure{});
    }
}

Expected<TextureClearPattern<s_TextureClearDepthPatternBytes>> BuildTextureDepthClearPattern(const Format::Enum format, const f32 depth)noexcept{
    TextureClearPattern<s_TextureClearDepthPatternBytes> result{};
    const auto pattern = MakeNotNull(result.bytes.data());
    switch(format){
    case Format::D16:{
        const u16 packed = static_cast<u16>(QuantizeUNormClearScalar(depth, static_cast<f32>(Limit<u16>::s_Max)));
        WriteClearPatternValue(packed, pattern);
        result.byteCount = sizeof(packed);
        return result;
    }
    case Format::D24S8:{
        const u32 packed = static_cast<u32>(QuantizeUNormClearScalar(depth, static_cast<f32>(s_D24ClearValueMask)));
        WriteClearPatternValue(packed, pattern);
        result.byteCount = sizeof(packed);
        return result;
    }
    case Format::D32:
    case Format::D32S8:{
        const f32 packed = Saturate(depth);
        WriteClearPatternValue(packed, pattern);
        result.byteCount = sizeof(packed);
        return result;
    }
    default:
        return MakeUnexpected(Failure{});
    }
}

Expected<TextureClearPattern<s_TextureClearStencilPatternBytes>> BuildTextureStencilClearPattern(const Format::Enum format, const u8 stencil)noexcept{
    TextureClearPattern<s_TextureClearStencilPatternBytes> result{};
    const auto pattern = MakeNotNull(result.bytes.data());
    switch(format){
    case Format::D24S8:
    case Format::D32S8:
        WriteClearPatternValue(stencil, pattern);
        result.byteCount = sizeof(stencil);
        return result;
    default:
        return MakeUnexpected(Failure{});
    }
}

void FillTextureClearBytes(const usize byteCount, const Span<const u8> pattern, void* bytes)noexcept{
    if(byteCount == 0u || pattern.empty())
        return;
    const auto outBytes = MakeNotNull(static_cast<u8*>(bytes));
    NWB_MEMCPY(outBytes.get(), byteCount, pattern.data(), pattern.size());
    for(usize filled = pattern.size(); filled < byteCount; ){
        const usize copySize = Min(filled, byteCount - filled);
        NWB_MEMCPY(outBytes.get() + filled, byteCount - filled, outBytes.get(), copySize);
        filled += copySize;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

