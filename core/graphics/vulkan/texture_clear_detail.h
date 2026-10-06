// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "texture_clear_contract.h"
#include "arena_names.h"

#include <global/math/convert.h>
#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_TextureClearColorComponentCount = 3u;
inline constexpr u32 s_TextureClearRGComponentCount = 2u;
inline constexpr u32 s_TextureClearRGBAComponentCount = 4u;
inline constexpr u32 s_TextureClearDepthPatternBytes = 4u;
inline constexpr u32 s_TextureClearStencilPatternBytes = 1u;
inline constexpr u32 s_TextureClearMaxPatternBytes = 16u;
inline constexpr u32 s_TextureClearUploadAlignment = 4u;
inline constexpr u32 s_BCSingleClearBlockBytes = 8u;
inline constexpr u32 s_BCDoubleClearBlockBytes = 16u;
inline constexpr u32 s_BC4EndpointByteCount = 2u;
inline constexpr u32 s_RGB565RedBitCount = 5u;
inline constexpr u32 s_RGB565GreenBitCount = 6u;
inline constexpr u32 s_RGB565RedBitShift = 11u;
inline constexpr u32 s_RGB565GreenBitShift = 5u;
inline constexpr u32 s_BC1TransparentColorIndices = Limit<u32>::s_Max;
inline constexpr u32 s_D24ClearValueMask = 0x00ffffffu;
inline constexpr f32 s_ClearFloatRoundingBias = 0.5f;
inline constexpr u32 s_ClearChannelBits4444 = 4u;
inline constexpr u32 s_ClearChannelBits565R = 5u;
inline constexpr u32 s_ClearChannelBits565G = 6u;
inline constexpr u32 s_ClearChannelBits555 = 5u;
inline constexpr u32 s_ClearChannelBits101010 = 10u;
inline constexpr u32 s_ClearChannelBits21030 = 2u;
inline constexpr u32 s_ClearShift4 = 4u;
inline constexpr u32 s_ClearShift5 = 5u;
inline constexpr u32 s_ClearShift6 = 6u;
inline constexpr u32 s_ClearShift8 = 8u;
inline constexpr u32 s_ClearShift10 = 10u;
inline constexpr u32 s_ClearShift11 = 11u;
inline constexpr u32 s_ClearShift12 = 12u;
inline constexpr u32 s_ClearShift20 = 20u;
inline constexpr u32 s_ClearShift22 = 22u;
inline constexpr u32 s_UFloat111110GreenMantissaBits = 6u;
inline constexpr u32 s_UFloat111110BlueMantissaBits = 5u;
inline constexpr u32 s_ClearShift30 = 30u;
inline constexpr f32 s_SRGBClearLinearThreshold = 0.0031308f;
inline constexpr f32 s_SRGBClearLinearScale = 12.92f;
inline constexpr f32 s_SRGBClearNonlinearScale = 1.055f;
inline constexpr f32 s_SRGBClearNonlinearExponent = 2.4f;
inline constexpr f32 s_SRGBClearNonlinearOffset = 0.055f;
inline bool TextureClearRectEmpty(const Rect& rect)noexcept{
    return rect.minX >= rect.maxX || rect.minY >= rect.maxY;
}

inline Rect ResolveTextureClearRect(const TextureDesc& desc, const MipLevel mipLevel, const Rect& rect)noexcept{
    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(desc, mipLevel);
    const i32 width = static_cast<i32>(mipExtent.width);
    const i32 height = static_cast<i32>(mipExtent.height);
    return Rect(
        Max<i32>(0, Min<i32>(rect.minX, width)),
        Max<i32>(0, Min<i32>(rect.maxX, width)),
        Max<i32>(0, Min<i32>(rect.minY, height)),
        Max<i32>(0, Min<i32>(rect.maxY, height))
    );
}

inline bool TextureClearBoxCoversSubresources(const TextureDesc& desc, const TextureSubresourceSet& subresources, const Box& box)noexcept{
    return TextureClearBoxFullyCoversSubresources(desc, subresources, box);
}

inline bool TextureClearSubresourcesContainedBy(const TextureSubresourceSet& requested, const TextureSubresourceSet& container)noexcept{
    const MipLevel requestedMipEnd = requested.baseMipLevel + requested.numMipLevels;
    const MipLevel containerMipEnd = container.baseMipLevel + container.numMipLevels;
    const ArraySlice requestedArrayEnd = requested.baseArraySlice + requested.numArraySlices;
    const ArraySlice containerArrayEnd = container.baseArraySlice + container.numArraySlices;

    return
        requested.numMipLevels != 0u
        && requested.numArraySlices != 0u
        && requested.baseMipLevel >= container.baseMipLevel
        && requestedMipEnd <= containerMipEnd
        && requested.baseArraySlice >= container.baseArraySlice
        && requestedArrayEnd <= containerArrayEnd
    ;
}

inline void BuildArrayLayerImageSubresourceRanges(
    const TextureSubresourceSet& subresources,
    const VkImageAspectFlags aspectMask,
    Vector<VkImageSubresourceRange, Alloc::ScratchArena>& ranges
)noexcept{
    const ArraySlice arrayEnd = subresources.baseArraySlice + subresources.numArraySlices;
    u32 rangeIndex = 0u;
    for(ArraySlice arraySlice = subresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice)
        ranges[rangeIndex++] = VulkanDetail::BuildImageSubresourceRange(TextureSubresourceSet(subresources.baseMipLevel, subresources.numMipLevels, arraySlice, 1u), aspectMask);
}

inline VkClearRect BuildTextureAttachmentClearRect(const TextureSubresourceSet& requested, const TextureSubresourceSet& attachment, const Rect& rect)noexcept{
    VkClearRect clearRect{};
    clearRect.rect.offset = { rect.minX, rect.minY };
    clearRect.rect.extent = { static_cast<u32>(rect.width()), static_cast<u32>(rect.height()) };
    clearRect.baseArrayLayer = requested.baseArraySlice - attachment.baseArraySlice;
    clearRect.layerCount = requested.numArraySlices;
    return clearRect;
}

struct TextureAttachmentClearTarget{
    TextureSubresourceSet resolvedSubresources;
    u32 colorAttachmentIndex = 0u;
    bool isReadOnly = false;
};

inline bool ResolveTextureAttachmentClearSubresources(
    Texture& texture,
    const FramebufferAttachment& attachment,
    const TextureSubresourceSet& requestedSubresources,
    TextureSubresourceSet& outResolvedSubresources
)noexcept{
    if(attachment.texture != &texture)
        return false;

    const TextureSubresourceSet resolvedAttachmentSubresources = attachment.subresources.resolve(
        texture.getCreationDescription(),
        TextureSubresourceMipResolve::Single
    );
    if(!TextureClearSubresourcesContainedBy(requestedSubresources, resolvedAttachmentSubresources))
        return false;

    outResolvedSubresources = resolvedAttachmentSubresources;
    return true;
}

inline bool FindTextureColorAttachmentClearTarget(
    Texture& texture,
    const TextureSubresourceSet& requestedSubresources,
    const FramebufferDesc& fbDesc,
    TextureAttachmentClearTarget& outTarget
)noexcept{
    u32 colorAttachmentIndex = 0u;
    for(usize i = 0u; i < fbDesc.colorAttachments.size(); ++i){
        const FramebufferAttachment& attachment = fbDesc.colorAttachments[i];
        if(!attachment.texture)
            continue;

        const u32 activeColorAttachmentIndex = colorAttachmentIndex++;
        TextureSubresourceSet resolvedAttachmentSubresources;
        if(!ResolveTextureAttachmentClearSubresources(texture, attachment, requestedSubresources, resolvedAttachmentSubresources))
            continue;

        outTarget.resolvedSubresources = resolvedAttachmentSubresources;
        outTarget.colorAttachmentIndex = activeColorAttachmentIndex;
        outTarget.isReadOnly = attachment.isReadOnly;
        return true;
    }

    return false;
}

inline bool TextureAttachmentClearRectContainedByFramebuffer(const VkClearRect& clearRect, const FramebufferInfoEx& framebufferInfo)noexcept{
    if(clearRect.rect.offset.x < 0 || clearRect.rect.offset.y < 0)
        return false;

    const u64 endX = static_cast<u64>(clearRect.rect.offset.x) + clearRect.rect.extent.width;
    const u64 endY = static_cast<u64>(clearRect.rect.offset.y) + clearRect.rect.extent.height;
    const u64 endLayer = static_cast<u64>(clearRect.baseArrayLayer) + clearRect.layerCount;
    return
        endX <= framebufferInfo.width
        && endY <= framebufferInfo.height
        && endLayer <= framebufferInfo.arraySize
    ;
}

inline bool TextureClearBoxAlignedToBlocks(const Box& box, const VkExtent3D& mipExtent, const VulkanDetail::TextureFormatBlockLayout& formatLayout)noexcept{
    if(formatLayout.blockWidth <= 1u && formatLayout.blockHeight <= 1u)
        return true;

    const i32 blockWidth = static_cast<i32>(formatLayout.blockWidth);
    const i32 blockHeight = static_cast<i32>(formatLayout.blockHeight);
    return
        (box.minX % blockWidth) == 0
        && (box.minY % blockHeight) == 0
        && (box.maxX == static_cast<i32>(mipExtent.width) || (box.maxX % blockWidth) == 0)
        && (box.maxY == static_cast<i32>(mipExtent.height) || (box.maxY % blockHeight) == 0)
    ;
}

inline constexpr u64 s_TextureClearMergedLayerUploadThreshold = 64ull * 1024ull;

struct TextureClearUploadLayout{
    u64 uploadSize = 0ull;
    u64 layerPitch = 0ull;
    usize clearByteCount = 0u;
    u32 copyOffsetAlignment = 0u;
    u32 stagingAlignment = 0u;
    bool mergeArrayLayerCopies = false;
};

inline bool BuildTextureClearUploadLayout(
    const u64 elementCount,
    const u32 elementSize,
    const u64 arrayLayerCount,
    TextureClearUploadLayout& outLayout
)noexcept{
    outLayout = {};
    if(
        elementCount == 0ull
        || elementSize == 0u
        || arrayLayerCount == 0ull
        || elementCount > Limit<u64>::s_Max / elementSize
    )
        return false;

    const u64 uploadSize = elementCount * elementSize;
    u32 copyOffsetAlignment = 0u;
    u32 stagingAlignment = 0u;
    if(
        !VulkanDetail::TryComputeCommonAlignment(
            s_TextureClearUploadAlignment,
            elementSize,
            copyOffsetAlignment
        )
        || !VulkanDetail::TryComputeUploadSuballocationAlignment(copyOffsetAlignment, stagingAlignment)
    )
        return false;

    u64 layerPitch = uploadSize;
    if(!AlignUpU64Checked(layerPitch, copyOffsetAlignment, layerPitch))
        return false;
    const bool mergeArrayLayerCopies =
        arrayLayerCount > 1ull
        && layerPitch <= s_TextureClearMergedLayerUploadThreshold / arrayLayerCount
    ;
    if(mergeArrayLayerCopies && arrayLayerCount - 1ull > (Limit<u64>::s_Max - uploadSize) / layerPitch)
        return false;

    const u64 clearByteCount = mergeArrayLayerCopies ? layerPitch * (arrayLayerCount - 1ull) + uploadSize : uploadSize;
    if(clearByteCount > static_cast<u64>(Limit<usize>::s_Max))
        return false;

    outLayout.uploadSize = uploadSize;
    outLayout.layerPitch = layerPitch;
    outLayout.clearByteCount = static_cast<usize>(clearByteCount);
    outLayout.copyOffsetAlignment = copyOffsetAlignment;
    outLayout.stagingAlignment = stagingAlignment;
    outLayout.mergeArrayLayerCopies = mergeArrayLayerCopies;
    return true;
}

inline void WriteClearPatternValue(u8* outBytes, const usize outByteCount, const void* value, const usize valueByteCount)noexcept{
    GLB_MEMCPY(outBytes, outByteCount, value, valueByteCount);
}

inline VkClearColorValue BuildTextureClearColorValue(const Color& clearColor)noexcept{
    VkClearColorValue clearValue{};
    clearValue.float32[0] = clearColor.r;
    clearValue.float32[1] = clearColor.g;
    clearValue.float32[2] = clearColor.b;
    clearValue.float32[3] = clearColor.a;
    return clearValue;
}

inline VkClearColorValue BuildTextureClearColorValue(const UIntColor& clearColor)noexcept{
    VkClearColorValue clearValue{};
    clearValue.uint32[0] = clearColor.r;
    clearValue.uint32[1] = clearColor.g;
    clearValue.uint32[2] = clearColor.b;
    clearValue.uint32[3] = clearColor.a;
    return clearValue;
}

inline VkClearColorValue BuildTextureClearColorValue(const IntColor& clearColor)noexcept{
    VkClearColorValue clearValue{};
    clearValue.int32[0] = clearColor.r;
    clearValue.int32[1] = clearColor.g;
    clearValue.int32[2] = clearColor.b;
    clearValue.int32[3] = clearColor.a;
    return clearValue;
}

inline bool TextureColorClearValueTypeMatchesFormat(const FormatInfo& formatInfo, const bool integerValue, const bool signedIntegerValue)noexcept{
    if(formatInfo.kind == FormatKind::Integer)
        return integerValue && formatInfo.isSigned == signedIntegerValue;

    return !integerValue;
}

inline bool TextureColorClearAspectIsValid(const VkImageAspectFlags aspectMask)noexcept{
    return aspectMask == VK_IMAGE_ASPECT_COLOR_BIT;
}

inline bool TextureDepthStencilClearAspectsAreValid(
    const VkImageAspectFlags aspectMask,
    const bool clearDepth,
    const bool clearStencil
)noexcept{
    return
        (aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) == 0u
        && (!clearDepth || (aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0u)
        && (!clearStencil || (aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0u)
    ;
}

inline f32 ClampClearFloat(const f32 value, const f32 minValue, const f32 maxValue)noexcept{
    return VectorGetX(VectorClamp(VectorReplicate(value), VectorReplicate(minValue), VectorReplicate(maxValue)));
}

inline u32 FloatToUNormClearValue(const f32 value, const u32 maxValue)noexcept{
    const SIMDVector clamped = VectorSaturate(VectorReplicate(value));
    const SIMDVector scaled = VectorMultiply(clamped, VectorReplicate(static_cast<f32>(maxValue)));
    const SIMDVector rounded = VectorFloor(VectorAdd(scaled, VectorReplicate(s_ClearFloatRoundingBias)));
    return static_cast<u32>(VectorGetX(VectorMin(rounded, VectorReplicate(static_cast<f32>(maxValue)))));
}

inline SIMDVector QuantizeUNormClearVector(const SIMDVector saturated01, const f32 maxValue)noexcept{
    const SIMDVector scaled = VectorMultiply(VectorSaturate(saturated01), VectorReplicate(maxValue));
    return VectorMin(VectorFloor(VectorAdd(scaled, VectorReplicate(s_ClearFloatRoundingBias))), VectorReplicate(maxValue));
}

inline i32 FloatToSNormClearValue(const f32 value, const i32 maxValue)noexcept{
    const SIMDVector clamped = VectorClamp(VectorReplicate(value), VectorReplicate(-1.0f), VectorReplicate(1.0f));
    const SIMDVector scaled = VectorMultiply(clamped, VectorReplicate(static_cast<f32>(maxValue)));
    const SIMDVector magnitude = VectorFloor(VectorAdd(VectorAbs(scaled), VectorReplicate(s_ClearFloatRoundingBias)));
    const SIMDVector signedMagnitude = VectorSelect(VectorNegate(magnitude), magnitude, VectorGreaterOrEqual(scaled, VectorZero()));
    return static_cast<i32>(VectorGetX(signedMagnitude));
}

inline SIMDVector LinearToSRGBClearVector(const SIMDVector linear)noexcept{
    const SIMDVector clamped = VectorSaturate(linear);
    const SIMDVector threshold = VectorReplicate(s_SRGBClearLinearThreshold);
    const SIMDVector linearPart = VectorMultiply(clamped, VectorReplicate(s_SRGBClearLinearScale));
    const SIMDVector power = VectorPow(clamped, VectorReplicate(1.0f / s_SRGBClearNonlinearExponent));
    const SIMDVector nonlinearPart = VectorSubtract(VectorMultiply(VectorReplicate(s_SRGBClearNonlinearScale), power), VectorReplicate(s_SRGBClearNonlinearOffset));
    return VectorSelect(nonlinearPart, linearPart, VectorLessOrEqual(clamped, threshold));
}

inline void WriteBC1ColorClearBlock(u8* outPattern, const f32 r, const f32 g, const f32 b, const f32 a, const bool srgb)noexcept{
    const SIMDVector sourceRgba = VectorSet(r, g, b, a);
    const SIMDVector srgbEncodedTriple = LinearToSRGBClearVector(sourceRgba);
    const SIMDVector srgbSelectMask = srgb ? VectorTrueInt() : VectorFalseInt();
    const SIMDVector encodedRgb = VectorSelect(sourceRgba, srgbEncodedTriple, srgbSelectMask);
    const SIMDVector maxRgb = VectorSet(static_cast<f32>((1u << s_RGB565RedBitCount) - 1u), static_cast<f32>((1u << s_RGB565GreenBitCount) - 1u), static_cast<f32>((1u << s_RGB565RedBitCount) - 1u), 1.0f);
    const SIMDVector quantized = VectorMin(VectorFloor(VectorAdd(VectorMultiply(VectorSaturate(encodedRgb), maxRgb), VectorReplicate(s_ClearFloatRoundingBias))), maxRgb);
    u16 color0 = static_cast<u16>((static_cast<u32>(VectorGetX(quantized)) << s_RGB565RedBitShift) | (static_cast<u32>(VectorGetY(quantized)) << s_RGB565GreenBitShift) | static_cast<u32>(VectorGetZ(quantized)));
    u16 color1 = color0;
    u32 indices = 0u;
    if(static_cast<u32>(VectorGetW(quantized)) == 0u){
        color0 = 0u;
        color1 = 0u;
        indices = s_BC1TransparentColorIndices;
    }

    WriteClearPatternValue(outPattern, sizeof(color0), &color0, sizeof(color0));
    WriteClearPatternValue(outPattern + sizeof(color0), sizeof(color1), &color1, sizeof(color1));
    WriteClearPatternValue(outPattern + sizeof(color0) + sizeof(color1), sizeof(indices), &indices, sizeof(indices));
}

inline void WriteBC4UNormClearBlock(u8* outPattern, const f32 value)noexcept{
    const u8 endpoint = static_cast<u8>(FloatToUNormClearValue(value, static_cast<u32>(Limit<u8>::s_Max)));
    outPattern[0] = endpoint;
    outPattern[1] = endpoint;
    for(u32 byteIndex = s_BC4EndpointByteCount; byteIndex < s_BCSingleClearBlockBytes; ++byteIndex)
        outPattern[byteIndex] = 0u;
}

inline void WriteBC4SNormClearBlock(u8* outPattern, const f32 value)noexcept{
    const i8 endpoint = static_cast<i8>(FloatToSNormClearValue(value, static_cast<i32>(Limit<i8>::s_Max)));
    outPattern[0] = static_cast<u8>(endpoint);
    outPattern[1] = static_cast<u8>(endpoint);
    for(u32 byteIndex = s_BC4EndpointByteCount; byteIndex < s_BCSingleClearBlockBytes; ++byteIndex)
        outPattern[byteIndex] = 0u;
}

inline bool BuildTextureFloatClearPattern(const Format::Enum format, const VkClearColorValue& clearValue, u8* outPattern, u32& outPatternSize)noexcept{
    outPatternSize = 0u;
    const f32 values[] = {
        clearValue.float32[0],
        clearValue.float32[1],
        clearValue.float32[2],
        clearValue.float32[3],
    };

    auto writeUNorm8Components = [&](const u32 componentCount, const bool srgb)noexcept{
        const SIMDVector sourceValues = VectorSet(values[0], values[1], values[2], values[3]);
        const SIMDVector encodedValues = LinearToSRGBClearVector(sourceValues);
        const SIMDVector selectedValues = srgb
            ? VectorSet(VectorGetX(encodedValues), VectorGetY(encodedValues), VectorGetZ(encodedValues), VectorGetW(sourceValues))
            : sourceValues;
        const SIMDVector quantized = QuantizeUNormClearVector(selectedValues, static_cast<f32>(Limit<u8>::s_Max));
        for(u32 component = 0u; component < componentCount; ++component){
            const u8 packed = static_cast<u8>(VectorGetByIndex(quantized, static_cast<usize>(component)));
            WriteClearPatternValue(outPattern + component * sizeof(packed), sizeof(packed), &packed, sizeof(packed));
        }
        outPatternSize = componentCount * static_cast<u32>(sizeof(u8));
        return true;
    };
    auto writeUNorm8BGRAComponents = [&](const bool srgb)noexcept{
        const SIMDVector orderedValues = VectorSet(values[2], values[1], values[0], values[3]);
        const SIMDVector encodedOrderedValues = LinearToSRGBClearVector(orderedValues);
        const SIMDVector selectedValues = srgb
            ? VectorSet(VectorGetX(encodedOrderedValues), VectorGetY(encodedOrderedValues), VectorGetZ(encodedOrderedValues), VectorGetW(orderedValues))
            : orderedValues;
        const SIMDVector quantized = QuantizeUNormClearVector(selectedValues, static_cast<f32>(Limit<u8>::s_Max));
        for(u32 component = 0u; component < s_TextureClearRGBAComponentCount; ++component){
            const u8 packed = static_cast<u8>(VectorGetByIndex(quantized, static_cast<usize>(component)));
            WriteClearPatternValue(outPattern + component * sizeof(packed), sizeof(packed), &packed, sizeof(packed));
        }
        outPatternSize = s_TextureClearRGBAComponentCount * static_cast<u32>(sizeof(u8));
        return true;
    };
    auto writeSNorm8Components = [&](const u32 componentCount)noexcept{
        const SIMDVector clamped = VectorClamp(VectorSet(values[0], values[1], values[2], values[3]), VectorReplicate(-1.0f), VectorReplicate(1.0f));
        const SIMDVector scaled = VectorMultiply(clamped, VectorReplicate(static_cast<f32>(Limit<i8>::s_Max)));
        const SIMDVector magnitude = VectorFloor(VectorAdd(VectorAbs(scaled), VectorReplicate(s_ClearFloatRoundingBias)));
        const SIMDVector signedMagnitude = VectorSelect(VectorNegate(magnitude), magnitude, VectorGreaterOrEqual(scaled, VectorZero()));
        for(u32 component = 0u; component < componentCount; ++component){
            const i8 packed = static_cast<i8>(VectorGetByIndex(signedMagnitude, static_cast<usize>(component)));
            WriteClearPatternValue(outPattern + component * sizeof(packed), sizeof(packed), &packed, sizeof(packed));
        }
        outPatternSize = componentCount * static_cast<u32>(sizeof(i8));
        return true;
    };
    auto writeUNorm16Components = [&](const u32 componentCount)noexcept{
        const SIMDVector quantized = QuantizeUNormClearVector(VectorSet(values[0], values[1], values[2], values[3]), static_cast<f32>(Limit<u16>::s_Max));
        for(u32 component = 0u; component < componentCount; ++component){
            const u16 packed = static_cast<u16>(VectorGetByIndex(quantized, static_cast<usize>(component)));
            WriteClearPatternValue(outPattern + component * sizeof(packed), sizeof(packed), &packed, sizeof(packed));
        }
        outPatternSize = componentCount * static_cast<u32>(sizeof(u16));
        return true;
    };
    auto writeSNorm16Components = [&](const u32 componentCount)noexcept{
        const SIMDVector clamped = VectorClamp(VectorSet(values[0], values[1], values[2], values[3]), VectorReplicate(-1.0f), VectorReplicate(1.0f));
        const SIMDVector scaled = VectorMultiply(clamped, VectorReplicate(static_cast<f32>(Limit<i16>::s_Max)));
        const SIMDVector magnitude = VectorFloor(VectorAdd(VectorAbs(scaled), VectorReplicate(s_ClearFloatRoundingBias)));
        const SIMDVector signedMagnitude = VectorSelect(VectorNegate(magnitude), magnitude, VectorGreaterOrEqual(scaled, VectorZero()));
        for(u32 component = 0u; component < componentCount; ++component){
            const i16 packed = static_cast<i16>(VectorGetByIndex(signedMagnitude, static_cast<usize>(component)));
            WriteClearPatternValue(outPattern + component * sizeof(packed), sizeof(packed), &packed, sizeof(packed));
        }
        outPatternSize = componentCount * static_cast<u32>(sizeof(i16));
        return true;
    };
    auto writeHalfComponents = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            const Half value = ConvertFloatToHalf(values[component]);
            WriteClearPatternValue(outPattern + component * sizeof(value), sizeof(value), &value, sizeof(value));
        }
        outPatternSize = componentCount * static_cast<u32>(sizeof(Half));
        return true;
    };
    auto writeFloatComponents = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            WriteClearPatternValue(outPattern + component * sizeof(f32), sizeof(f32), &values[component], sizeof(f32));
        }
        outPatternSize = componentCount * static_cast<u32>(sizeof(f32));
        return true;
    };
    auto writeUNorm4BGRAComponents = [&]()noexcept{
        const SIMDVector quantized4444 = QuantizeUNormClearVector(VectorSet(values[2], values[1], values[0], values[3]), static_cast<f32>((1u << s_ClearChannelBits4444) - 1u));
        const u16 packed = static_cast<u16>(
            (static_cast<u32>(VectorGetX(quantized4444)) << s_ClearShift12)
            | (static_cast<u32>(VectorGetY(quantized4444)) << s_ClearShift8)
            | (static_cast<u32>(VectorGetZ(quantized4444)) << s_ClearShift4)
            | static_cast<u32>(VectorGetW(quantized4444))
        );
        WriteClearPatternValue(outPattern, sizeof(packed), &packed, sizeof(packed));
        outPatternSize = sizeof(packed);
        return true;
    };
    auto writeUNorm565BGRComponents = [&]()noexcept{
        const SIMDVector source565 = VectorSet(values[2], values[1], values[0], 0.0f);
        const SIMDVector max565 = VectorSet(static_cast<f32>((1u << s_ClearChannelBits565R) - 1u), static_cast<f32>((1u << s_ClearChannelBits565G) - 1u), static_cast<f32>((1u << s_ClearChannelBits565R) - 1u), 0.0f);
        const SIMDVector quantized565 = VectorMin(VectorFloor(VectorAdd(VectorMultiply(VectorSaturate(source565), max565), VectorReplicate(s_ClearFloatRoundingBias))), max565);
        const u16 packed = static_cast<u16>(
            (static_cast<u32>(VectorGetX(quantized565)) << s_ClearShift11)
            | (static_cast<u32>(VectorGetY(quantized565)) << s_ClearShift5)
            | static_cast<u32>(VectorGetZ(quantized565))
        );
        WriteClearPatternValue(outPattern, sizeof(packed), &packed, sizeof(packed));
        outPatternSize = sizeof(packed);
        return true;
    };
    auto writeUNorm5551BGRComponents = [&]()noexcept{
        const SIMDVector source5551 = VectorSet(values[2], values[1], values[0], values[3]);
        const SIMDVector max5551 = VectorSet(static_cast<f32>((1u << s_ClearChannelBits565R) - 1u), static_cast<f32>((1u << s_ClearChannelBits555) - 1u), static_cast<f32>((1u << s_ClearChannelBits565R) - 1u), 1.0f);
        const SIMDVector quantized5551 = VectorMin(VectorFloor(VectorAdd(VectorMultiply(VectorSaturate(source5551), max5551), VectorReplicate(s_ClearFloatRoundingBias))), max5551);
        const u16 packed = static_cast<u16>(
            (static_cast<u32>(VectorGetX(quantized5551)) << s_ClearShift11)
            | (static_cast<u32>(VectorGetY(quantized5551)) << s_ClearShift6)
            | (static_cast<u32>(VectorGetZ(quantized5551)) << 1u)
            | static_cast<u32>(VectorGetW(quantized5551))
        );
        WriteClearPatternValue(outPattern, sizeof(packed), &packed, sizeof(packed));
        outPatternSize = sizeof(packed);
        return true;
    };
    auto writeUNorm1010102RGBComponents = [&]()noexcept{
        const SIMDVector source1010102 = VectorSet(values[0], values[1], values[2], values[3]);
        const SIMDVector max1010102 = VectorSet(static_cast<f32>((1u << s_ClearChannelBits101010) - 1u), static_cast<f32>((1u << s_ClearChannelBits101010) - 1u), static_cast<f32>((1u << s_ClearChannelBits101010) - 1u), static_cast<f32>((1u << s_ClearChannelBits21030) - 1u));
        const SIMDVector quantized1010102 = VectorMin(VectorFloor(VectorAdd(VectorMultiply(VectorSaturate(source1010102), max1010102), VectorReplicate(s_ClearFloatRoundingBias))), max1010102);
        const u32 packed =
            static_cast<u32>(VectorGetX(quantized1010102))
            | (static_cast<u32>(VectorGetY(quantized1010102)) << s_ClearShift10)
            | (static_cast<u32>(VectorGetZ(quantized1010102)) << s_ClearShift20)
            | (static_cast<u32>(VectorGetW(quantized1010102)) << s_ClearShift30);
        WriteClearPatternValue(outPattern, sizeof(packed), &packed, sizeof(packed));
        outPatternSize = sizeof(packed);
        return true;
    };
    auto writeUFloat111110RGBComponents = [&]()noexcept{
        const u32 packed =
            ConvertFloatToUnsignedFloat<s_UFloat111110GreenMantissaBits>(values[0])
            | (ConvertFloatToUnsignedFloat<s_UFloat111110GreenMantissaBits>(values[1]) << s_ClearShift11)
            | (ConvertFloatToUnsignedFloat<s_UFloat111110BlueMantissaBits>(values[2]) << s_ClearShift22);
        WriteClearPatternValue(outPattern, sizeof(packed), &packed, sizeof(packed));
        outPatternSize = sizeof(packed);
        return true;
    };
    auto writeBC1Components = [&](const bool srgb)noexcept{
        WriteBC1ColorClearBlock(outPattern, values[0], values[1], values[2], values[3], srgb);
        outPatternSize = s_BCSingleClearBlockBytes;
        return true;
    };
    auto writeBC2Components = [&](const bool srgb)noexcept{
        const SIMDVector saturatedAlpha = VectorSaturate(VectorReplicate(values[3]));
        const SIMDVector scaledAlpha = VectorMultiply(saturatedAlpha, VectorReplicate(static_cast<f32>((1u << s_ClearChannelBits4444) - 1u)));
        const u64 alphaNibble = static_cast<u64>(VectorGetX(VectorMin(VectorFloor(VectorAdd(scaledAlpha, VectorReplicate(s_ClearFloatRoundingBias))), VectorReplicate(static_cast<f32>((1u << s_ClearChannelBits4444) - 1u)))));
        const SIMDVector splatMask = VectorSetInt(0x11111111u, 0x11111111u, 0u, 0u);
        const u64 alphaBits = (static_cast<u64>(VectorGetIntX(splatMask)) | (static_cast<u64>(VectorGetIntY(splatMask)) << 32u)) * alphaNibble;
        WriteClearPatternValue(outPattern, sizeof(alphaBits), &alphaBits, sizeof(alphaBits));
        WriteBC1ColorClearBlock(outPattern + sizeof(alphaBits), values[0], values[1], values[2], 1.0f, srgb);
        outPatternSize = s_BCDoubleClearBlockBytes;
        return true;
    };
    auto writeBC3Components = [&](const bool srgb)noexcept{
        WriteBC4UNormClearBlock(outPattern, values[3]);
        WriteBC1ColorClearBlock(outPattern + s_BCSingleClearBlockBytes, values[0], values[1], values[2], 1.0f, srgb);
        outPatternSize = s_BCDoubleClearBlockBytes;
        return true;
    };
    auto writeBC4UNormComponents = [&]()noexcept{
        WriteBC4UNormClearBlock(outPattern, values[0]);
        outPatternSize = s_BCSingleClearBlockBytes;
        return true;
    };
    auto writeBC4SNormComponents = [&]()noexcept{
        WriteBC4SNormClearBlock(outPattern, values[0]);
        outPatternSize = s_BCSingleClearBlockBytes;
        return true;
    };
    auto writeBC5UNormComponents = [&]()noexcept{
        WriteBC4UNormClearBlock(outPattern, values[0]);
        WriteBC4UNormClearBlock(outPattern + s_BCSingleClearBlockBytes, values[1]);
        outPatternSize = s_BCDoubleClearBlockBytes;
        return true;
    };
    auto writeBC5SNormComponents = [&]()noexcept{
        WriteBC4SNormClearBlock(outPattern, values[0]);
        WriteBC4SNormClearBlock(outPattern + s_BCSingleClearBlockBytes, values[1]);
        outPatternSize = s_BCDoubleClearBlockBytes;
        return true;
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
        return false;
    }
}

inline bool BuildTextureUIntClearPattern(const Format::Enum format, const VkClearColorValue& clearValue, u8* outPattern, u32& outPatternSize)noexcept{
    outPatternSize = 0u;
    const u32 values[] = {
        clearValue.uint32[0],
        clearValue.uint32[1],
        clearValue.uint32[2],
        clearValue.uint32[3],
    };

    auto writeU8Components = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            const u8 value = static_cast<u8>(Min(values[component], static_cast<u32>(Limit<u8>::s_Max)));
            WriteClearPatternValue(outPattern + component * sizeof(value), sizeof(value), &value, sizeof(value));
        }
        outPatternSize = componentCount * static_cast<u32>(sizeof(u8));
        return true;
    };
    auto writeU16Components = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            const u16 value = static_cast<u16>(Min(values[component], static_cast<u32>(Limit<u16>::s_Max)));
            WriteClearPatternValue(outPattern + component * sizeof(value), sizeof(value), &value, sizeof(value));
        }
        outPatternSize = componentCount * static_cast<u32>(sizeof(u16));
        return true;
    };
    auto writeU32Components = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            WriteClearPatternValue(outPattern + component * sizeof(u32), sizeof(u32), &values[component], sizeof(u32));
        }
        outPatternSize = componentCount * static_cast<u32>(sizeof(u32));
        return true;
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
        return false;
    }
}

inline bool BuildTextureIntClearPattern(const Format::Enum format, const VkClearColorValue& clearValue, u8* outPattern, u32& outPatternSize)noexcept{
    outPatternSize = 0u;
    const i32 values[] = {
        clearValue.int32[0],
        clearValue.int32[1],
        clearValue.int32[2],
        clearValue.int32[3],
    };

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
            WriteClearPatternValue(outPattern + component * sizeof(value), sizeof(value), &value, sizeof(value));
        }
        outPatternSize = componentCount * static_cast<u32>(sizeof(i8));
        return true;
    };
    auto writeI16Components = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component){
            const i16 value = static_cast<i16>(clampValue(values[component], static_cast<i32>(Limit<i16>::s_Min), static_cast<i32>(Limit<i16>::s_Max)));
            WriteClearPatternValue(outPattern + component * sizeof(value), sizeof(value), &value, sizeof(value));
        }
        outPatternSize = componentCount * static_cast<u32>(sizeof(i16));
        return true;
    };
    auto writeI32Components = [&](const u32 componentCount)noexcept{
        for(u32 component = 0u; component < componentCount; ++component)
            WriteClearPatternValue(outPattern + component * sizeof(i32), sizeof(i32), &values[component], sizeof(i32));
        outPatternSize = componentCount * static_cast<u32>(sizeof(i32));
        return true;
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
        return false;
    }
}

inline bool BuildTextureDepthClearPattern(const Format::Enum format, const f32 depth, u8* outPattern, u32& outPatternSize)noexcept{
    outPatternSize = 0u;
    switch(format){
    case Format::D16:{
        const u16 packed = static_cast<u16>(FloatToUNormClearValue(depth, static_cast<u32>(Limit<u16>::s_Max)));
        WriteClearPatternValue(outPattern, sizeof(packed), &packed, sizeof(packed));
        outPatternSize = sizeof(packed);
        return true;
    }
    case Format::D24S8:{
        const u32 packed = FloatToUNormClearValue(depth, s_D24ClearValueMask);
        WriteClearPatternValue(outPattern, sizeof(packed), &packed, sizeof(packed));
        outPatternSize = sizeof(packed);
        return true;
    }
    case Format::D32:
    case Format::D32S8:{
        const f32 packed = ClampClearFloat(depth, 0.0f, 1.0f);
        WriteClearPatternValue(outPattern, sizeof(packed), &packed, sizeof(packed));
        outPatternSize = sizeof(packed);
        return true;
    }
    default:
        return false;
    }
}

inline bool BuildTextureStencilClearPattern(const Format::Enum format, const u8 stencil, u8* outPattern, u32& outPatternSize)noexcept{
    outPatternSize = 0u;
    switch(format){
    case Format::D24S8:
    case Format::D32S8:
        WriteClearPatternValue(outPattern, sizeof(stencil), &stencil, sizeof(stencil));
        outPatternSize = sizeof(stencil);
        return true;
    default:
        return false;
    }
}

inline void FillTextureClearBytes(void* bytes, const usize byteCount, const u8* pattern, const u32 patternSize)noexcept{
    u8* outBytes = static_cast<u8*>(bytes);
    for(usize offset = 0u; offset < byteCount; offset += patternSize)
        GLB_MEMCPY(outBytes + offset, byteCount - offset, pattern, patternSize);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

