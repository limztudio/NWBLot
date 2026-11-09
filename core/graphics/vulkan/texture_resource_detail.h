// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>

#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_TextureCubeLayerCount = 6u;
inline constexpr ResourceStates::Mask s_ValidTextureResourceStates =
    ResourceStates::Common
    | ResourceStates::ShaderResource
    | ResourceStates::UnorderedAccess
    | ResourceStates::RenderTarget
    | ResourceStates::DepthWrite
    | ResourceStates::DepthRead
    | ResourceStates::CopyDest
    | ResourceStates::CopySource
    | ResourceStates::ResolveDest
    | ResourceStates::ResolveSource
    | ResourceStates::Present
    | ResourceStates::ShadingRateSurface
;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TextureCreateMetadata{
    VkFormat format = VK_FORMAT_UNDEFINED;
    VulkanDetail::TextureFormatBlockLayout formatLayout;
    VkImageAspectFlags aspectMask = 0;
    VkImageType imageType = VK_IMAGE_TYPE_2D;
    VkImageUsageFlags usage = 0;
    VkImageCreateFlags flags = 0;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
};

[[nodiscard]] inline b8 TextureDescriptionsEqual(const TextureDesc& lhs, const TextureDesc& rhs)noexcept{
    return
        lhs.name == rhs.name
        && NWB_MEMCMP(&lhs.clearValue, &rhs.clearValue, sizeof(Color)) == 0
        && lhs.width == rhs.width
        && lhs.height == rhs.height
        && lhs.depth == rhs.depth
        && lhs.arraySize == rhs.arraySize
        && lhs.mipLevels == rhs.mipLevels
        && lhs.sampleCount == rhs.sampleCount
        && lhs.sampleQuality == rhs.sampleQuality
        && lhs.initialState == rhs.initialState
        && lhs.format == rhs.format
        && lhs.dimension == rhs.dimension
        && lhs.queueSharing == rhs.queueSharing
        && lhs.isShaderResource == rhs.isShaderResource
        && lhs.isRenderTarget == rhs.isRenderTarget
        && lhs.isUAV == rhs.isUAV
        && lhs.isTypeless == rhs.isTypeless
        && lhs.isShadingRateSurface == rhs.isShadingRateSurface
        && lhs.isVirtual == rhs.isVirtual
        && lhs.useClearValue == rhs.useClearValue
        && lhs.keepInitialState == rhs.keepInitialState
    ;
}

[[nodiscard]] constexpr b8 IsTextureResourceStateMaskValid(const ResourceStates::Mask states)noexcept{
    return states != ResourceStates::Unknown && (states & ~s_ValidTextureResourceStates) == 0u;
}

[[nodiscard]] constexpr b8 IsTextureCreationStateMaskValid(const ResourceStates::Mask states)noexcept{
    return states == ResourceStates::Unknown || IsTextureResourceStateMaskValid(states);
}


[[nodiscard]] constexpr VkImageType GetTextureImageType(const TextureDimension::Enum dimension)noexcept{
    switch(dimension){
    case TextureDimension::Texture1D:
    case TextureDimension::Texture1DArray:
        return VK_IMAGE_TYPE_1D;
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
    case TextureDimension::Texture2DMS:
    case TextureDimension::Texture2DMSArray:
        return VK_IMAGE_TYPE_2D;
    case TextureDimension::Texture3D:
        return VK_IMAGE_TYPE_3D;
    default:
        return VK_IMAGE_TYPE_MAX_ENUM;
    }
}

[[nodiscard]] constexpr Expected<VkImageType> TryTextureDimensionToImageType(const TextureDimension::Enum dimension)noexcept{
    const VkImageType imageType = GetTextureImageType(dimension);
    if(imageType == VK_IMAGE_TYPE_MAX_ENUM)
        return MakeUnexpected(Failure{});
    return imageType;
}

// Creation copies the cached aspect and block layout from the creation format.

[[nodiscard]] inline b8 IsTextureImageWithinFormatLimits(const VkImageCreateInfo& imageInfo, const VkImageFormatProperties& formatProperties)noexcept{
    return
        imageInfo.extent.width <= formatProperties.maxExtent.width
        && imageInfo.extent.height <= formatProperties.maxExtent.height
        && imageInfo.extent.depth <= formatProperties.maxExtent.depth
        && imageInfo.mipLevels <= formatProperties.maxMipLevels
        && imageInfo.arrayLayers <= formatProperties.maxArrayLayers
        && (formatProperties.sampleCounts & imageInfo.samples) == imageInfo.samples
    ;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

