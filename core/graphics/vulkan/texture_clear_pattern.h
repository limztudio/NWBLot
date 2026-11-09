// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "texture_clear_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_BCSingleClearBlockBytes = 8u;
inline constexpr u32 s_BCDoubleClearBlockBytes = 16u;
inline constexpr u32 s_BC4EndpointByteCount = 2u;
inline constexpr u32 s_RGB565RedBitCount = 5u;
inline constexpr u32 s_RGB565GreenBitCount = 6u;
inline constexpr u32 s_RGB565RedBitShift = 11u;
inline constexpr u32 s_RGB565GreenBitShift = 5u;
inline constexpr u32 s_BC1TransparentColorIndices = Limit<u32>::s_Max;
inline constexpr u32 s_D24ClearValueMask = 0x00ffffffu;
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
inline constexpr u32 s_BC2AlphaNibbleSplat = 0x11111111u;
inline constexpr u32 s_ClearHighWordShift = 32u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueT>
inline void WriteClearPatternValue(const ValueT& value, NotNull<u8*> outBytes)noexcept{
    NWB_MEMCPY(outBytes.get(), sizeof(value), &value, sizeof(value));
}

template<usize ByteCount>
struct TextureClearPattern{
    Array<u8, ByteCount> bytes{};
    u32 byteCount = 0u;


public:
    [[nodiscard]] constexpr Span<const u8> span()const noexcept{ return Span<const u8>(bytes.data(), byteCount); }
};

[[nodiscard]] VkClearColorValue BuildTextureClearColorValue(const Color& clearColor)noexcept;
[[nodiscard]] VkClearColorValue BuildTextureClearColorValue(const UIntColor& clearColor)noexcept;
[[nodiscard]] VkClearColorValue BuildTextureClearColorValue(const IntColor& clearColor)noexcept;
[[nodiscard]] b8 TextureColorClearValueTypeMatchesFormat(const FormatInfo& formatInfo, b8 integerValue, b8 signedIntegerValue)noexcept;

[[nodiscard]] inline constexpr b8 TextureColorClearAspectIsValid(const VkImageAspectFlags aspectMask)noexcept{
    return aspectMask == VK_IMAGE_ASPECT_COLOR_BIT;
}

[[nodiscard]] inline constexpr b8 TextureDepthStencilClearAspectsAreValid(const VkImageAspectFlags aspectMask, const b8 clearDepth, const b8 clearStencil)noexcept{
    return (aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) == 0u && (!clearDepth || (aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0u) && (!clearStencil || (aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0u);
}

void WriteBC1ColorClearBlock(SIMDVectorSingle sourceRgba, b8 srgb, NotNull<u8*> outPattern)noexcept;
void WriteBC4ClearBlockEndpoint(u8 endpoint, NotNull<u8*> outPattern)noexcept;
void WriteBC4UNormClearBlock(f32 value, NotNull<u8*> outPattern)noexcept;
void WriteBC4SNormClearBlock(f32 value, NotNull<u8*> outPattern)noexcept;
[[nodiscard]] Expected<TextureClearPattern<s_TextureClearMaxPatternBytes>> BuildTextureFloatClearPattern(Format::Enum format, const VkClearColorValue& clearValue)noexcept;
[[nodiscard]] Expected<TextureClearPattern<s_TextureClearMaxPatternBytes>> BuildTextureUIntClearPattern(Format::Enum format, const VkClearColorValue& clearValue)noexcept;
[[nodiscard]] Expected<TextureClearPattern<s_TextureClearMaxPatternBytes>> BuildTextureIntClearPattern(Format::Enum format, const VkClearColorValue& clearValue)noexcept;
[[nodiscard]] Expected<TextureClearPattern<s_TextureClearDepthPatternBytes>> BuildTextureDepthClearPattern(Format::Enum format, f32 depth)noexcept;
[[nodiscard]] Expected<TextureClearPattern<s_TextureClearStencilPatternBytes>> BuildTextureStencilClearPattern(Format::Enum format, u8 stencil)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

