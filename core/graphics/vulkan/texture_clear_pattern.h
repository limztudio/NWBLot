// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "texture_clear_detail.h"

#include <core/graphics/vulkan/texture_clear_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_BCSingleClearBlockBytes = NWB::Core::s_TextureClearDataBCSingleClearBlockBytes;
inline constexpr u32 s_BCDoubleClearBlockBytes = NWB::Core::s_TextureClearDataBCDoubleClearBlockBytes;
inline constexpr u32 s_BC4EndpointByteCount = NWB::Core::s_TextureClearDataBC4EndpointByteCount;
inline constexpr u32 s_RGB565RedBitCount = NWB::Core::s_TextureClearDataRGB565RedBitCount;
inline constexpr u32 s_RGB565GreenBitCount = NWB::Core::s_TextureClearDataRGB565GreenBitCount;
inline constexpr u32 s_RGB565RedBitShift = NWB::Core::s_TextureClearDataRGB565RedBitShift;
inline constexpr u32 s_RGB565GreenBitShift = NWB::Core::s_TextureClearDataRGB565GreenBitShift;
inline constexpr u32 s_BC1TransparentColorIndices = NWB::Core::s_TextureClearDataBC1TransparentColorIndices;
inline constexpr u32 s_D24ClearValueMask = NWB::Core::s_TextureClearDataD24ClearValueMask;
inline constexpr u32 s_ClearChannelBits4444 = NWB::Core::s_TextureClearDataClearChannelBits4444;
inline constexpr u32 s_ClearChannelBits565R = NWB::Core::s_TextureClearDataClearChannelBits565R;
inline constexpr u32 s_ClearChannelBits565G = NWB::Core::s_TextureClearDataClearChannelBits565G;
inline constexpr u32 s_ClearChannelBits555 = NWB::Core::s_TextureClearDataClearChannelBits555;
inline constexpr u32 s_ClearChannelBits101010 = NWB::Core::s_TextureClearDataClearChannelBits101010;
inline constexpr u32 s_ClearChannelBits21030 = NWB::Core::s_TextureClearDataClearChannelBits21030;
inline constexpr u32 s_ClearShift4 = NWB::Core::s_TextureClearDataClearShift4;
inline constexpr u32 s_ClearShift5 = NWB::Core::s_TextureClearDataClearShift5;
inline constexpr u32 s_ClearShift6 = NWB::Core::s_TextureClearDataClearShift6;
inline constexpr u32 s_ClearShift8 = NWB::Core::s_TextureClearDataClearShift8;
inline constexpr u32 s_ClearShift10 = NWB::Core::s_TextureClearDataClearShift10;
inline constexpr u32 s_ClearShift11 = NWB::Core::s_TextureClearDataClearShift11;
inline constexpr u32 s_ClearShift12 = NWB::Core::s_TextureClearDataClearShift12;
inline constexpr u32 s_ClearShift20 = NWB::Core::s_TextureClearDataClearShift20;
inline constexpr u32 s_ClearShift22 = NWB::Core::s_TextureClearDataClearShift22;
inline constexpr u32 s_UFloat111110GreenMantissaBits = NWB::Core::s_TextureClearDataUFloat111110GreenMantissaBits;
inline constexpr u32 s_UFloat111110BlueMantissaBits = NWB::Core::s_TextureClearDataUFloat111110BlueMantissaBits;
inline constexpr u32 s_ClearShift30 = NWB::Core::s_TextureClearDataClearShift30;
inline constexpr u32 s_BC2AlphaNibbleSplat = NWB::Core::s_TextureClearDataBC2AlphaNibbleSplat;
inline constexpr u32 s_ClearHighWordShift = NWB::Core::s_TextureClearDataClearHighWordShift;


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

