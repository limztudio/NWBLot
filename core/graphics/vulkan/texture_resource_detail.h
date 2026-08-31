// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>
#include <global/math/convert.h>


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
struct TextureCreateMetadata{
    VkFormat format = VK_FORMAT_UNDEFINED;
    VulkanDetail::TextureFormatBlockLayout formatLayout;
    VkImageAspectFlags aspectMask = 0;
    VkImageType imageType = VK_IMAGE_TYPE_2D;
    VkImageUsageFlags usage = 0;
    VkImageCreateFlags flags = 0;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
};

[[nodiscard]] inline bool TextureDescriptionsEqual(const TextureDesc& lhs, const TextureDesc& rhs)noexcept{
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

[[nodiscard]] constexpr bool IsTextureResourceStateMaskValid(const ResourceStates::Mask states)noexcept{
    return states != ResourceStates::Unknown && (states & ~s_ValidTextureResourceStates) == 0u;
}

[[nodiscard]] constexpr bool IsTextureCreationStateMaskValid(const ResourceStates::Mask states)noexcept{
    return states == ResourceStates::Unknown || IsTextureResourceStateMaskValid(states);
}

[[nodiscard]] inline VkImageUsageFlags RequiredImageUsageForResourceStates(
    const ResourceStates::Mask states
)noexcept{
    VkImageUsageFlags usage = 0u;
    if(states & ResourceStates::ShaderResource)
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if(states & ResourceStates::UnorderedAccess)
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if(states & ResourceStates::RenderTarget)
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if(states & (ResourceStates::DepthRead | ResourceStates::DepthWrite))
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if(states & (ResourceStates::CopySource | ResourceStates::ResolveSource))
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if(states & (ResourceStates::CopyDest | ResourceStates::ResolveDest))
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if(states & ResourceStates::ShadingRateSurface)
        usage |= VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;
    return usage;
}

inline u32 GetMaxMipLevels(const TextureDesc& desc){
    const u32 depth = desc.dimension == TextureDimension::Texture3D ? desc.depth : 1u;
    u32 maxExtent = Max(Max(desc.width, desc.height), depth);
    u32 levels = 1;
    while(maxExtent > 1){
        maxExtent >>= 1;
        ++levels;
    }
    return levels;
}

[[nodiscard]] inline bool IsTextureDescShapeValid(const TextureDesc& desc)noexcept{
    if(
        desc.width == 0u
        || desc.height == 0u
        || desc.depth == 0u
        || desc.mipLevels == 0u
        || desc.arraySize == 0u
    )
        return false;

    bool dimensionValid = false;
    switch(desc.dimension){
    case TextureDimension::Texture1D:
        dimensionValid = desc.height == 1u && desc.depth == 1u && desc.arraySize == 1u;
        break;
    case TextureDimension::Texture1DArray:
        dimensionValid = desc.height == 1u && desc.depth == 1u;
        break;
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DMS:
        dimensionValid = desc.depth == 1u && desc.arraySize == 1u;
        break;
    case TextureDimension::Texture2DArray:
    case TextureDimension::Texture2DMSArray:
        dimensionValid = desc.depth == 1u;
        break;
    case TextureDimension::TextureCube:
        dimensionValid = desc.depth == 1u
            && desc.width == desc.height
            && desc.arraySize == s_TextureCubeLayerCount
        ;
        break;
    case TextureDimension::TextureCubeArray:
        dimensionValid = desc.depth == 1u
            && desc.width == desc.height
            && desc.arraySize >= s_TextureCubeLayerCount
            && (desc.arraySize % s_TextureCubeLayerCount) == 0u
        ;
        break;
    case TextureDimension::Texture3D:
        dimensionValid = desc.arraySize == 1u;
        break;
    default:
        return false;
    }

    return dimensionValid && desc.mipLevels <= GetMaxMipLevels(desc);
}

[[nodiscard]] inline bool TryTextureDimensionToImageType(
    const TextureDimension::Enum dimension,
    VkImageType& outImageType
)noexcept{
    switch(dimension){
    case TextureDimension::Texture1D:
    case TextureDimension::Texture1DArray:
        outImageType = VK_IMAGE_TYPE_1D;
        return true;
    case TextureDimension::Texture2D:
    case TextureDimension::Texture2DArray:
    case TextureDimension::TextureCube:
    case TextureDimension::TextureCubeArray:
    case TextureDimension::Texture2DMS:
    case TextureDimension::Texture2DMSArray:
        outImageType = VK_IMAGE_TYPE_2D;
        return true;
    case TextureDimension::Texture3D:
        outImageType = VK_IMAGE_TYPE_3D;
        return true;
    default:
        outImageType = VK_IMAGE_TYPE_MAX_ENUM;
        return false;
    }
}

inline VkImageType TextureDimensionToImageType(const TextureDimension::Enum dimension){
    VkImageType imageType = VK_IMAGE_TYPE_MAX_ENUM;
    if(TryTextureDimensionToImageType(dimension, imageType))
        return imageType;
    return VK_IMAGE_TYPE_2D;
}

inline VkImageViewType TextureDimensionToViewType(TextureDimension::Enum dimension){
    switch(dimension){
    case TextureDimension::Texture1D: return VK_IMAGE_VIEW_TYPE_1D;
    case TextureDimension::Texture1DArray: return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case TextureDimension::Texture2D: return VK_IMAGE_VIEW_TYPE_2D;
    case TextureDimension::Texture2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureDimension::TextureCube: return VK_IMAGE_VIEW_TYPE_CUBE;
    case TextureDimension::TextureCubeArray: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    case TextureDimension::Texture2DMS: return VK_IMAGE_VIEW_TYPE_2D;
    case TextureDimension::Texture2DMSArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureDimension::Texture3D: return VK_IMAGE_VIEW_TYPE_3D;
    default: return VK_IMAGE_VIEW_TYPE_2D;
    }
}

inline VkImageUsageFlags PickImageUsage(const TextureDesc& desc, const VkImageAspectFlags aspectMask){
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    if(desc.isShaderResource)
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    if(desc.isRenderTarget){
        if((aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0)
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        else
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }

    if(desc.isUAV)
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;

    if(desc.isShadingRateSurface)
        usage |= VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR;

    return usage;
}

inline VkImageCreateFlags PickImageFlags(const TextureDesc& desc){
    VkImageCreateFlags flags = 0;

    if(desc.dimension == TextureDimension::TextureCube || desc.dimension == TextureDimension::TextureCubeArray)
        flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    if(desc.dimension == TextureDimension::Texture3D && desc.isRenderTarget)
        flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;

    return flags;
}

inline VkImageCreateInfo BuildTextureImageCreateInfo(const TextureDesc& desc, const TextureCreateMetadata& metadata){
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = metadata.imageType;
    imageInfo.extent.width = desc.width;
    imageInfo.extent.height = desc.height;
    imageInfo.extent.depth = desc.depth;
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arraySize;
    imageInfo.format = metadata.format;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = metadata.usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = metadata.sampleCount;
    imageInfo.flags = metadata.flags;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    return imageInfo;
}

[[nodiscard]] inline bool IsTextureImageInfoConsistent(
    const TextureDesc& desc,
    const VkImageCreateInfo& imageInfo,
    const VkImageUsageFlags requiredUsage = 0u
)noexcept{
    VkImageType expectedImageType = VK_IMAGE_TYPE_MAX_ENUM;
    if(
        !TryTextureDimensionToImageType(desc.dimension, expectedImageType)
        || !VulkanDetail::IsSupportedSampleCount(desc.sampleCount)
        || desc.sampleQuality != 0u
    )
        return false;

    const VkFormat expectedFormat = VulkanDetail::ConvertFormat(desc.format);
    if(expectedFormat == VK_FORMAT_UNDEFINED)
        return false;
    const VkImageAspectFlags expectedAspectMask = VulkanDetail::GetImageAspectMask(GetFormatInfo(desc.format));
    const VkImageUsageFlags descriptionUsage = (
        PickImageUsage(desc, expectedAspectMask)
        & ~(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
    ) | RequiredImageUsageForResourceStates(desc.initialState);
    const VkImageCreateFlags requiredFlags = PickImageFlags(desc);
    const bool sharingInfoConsistent =
        (
            imageInfo.sharingMode == VK_SHARING_MODE_EXCLUSIVE
            && imageInfo.queueFamilyIndexCount == 0u
            && imageInfo.pQueueFamilyIndices == nullptr
        )
        || (
            imageInfo.sharingMode == VK_SHARING_MODE_CONCURRENT
            && imageInfo.queueFamilyIndexCount >= 2u
            && imageInfo.pQueueFamilyIndices != nullptr
        )
    ;
    return
        imageInfo.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
        && imageInfo.imageType == expectedImageType
        && imageInfo.format == expectedFormat
        && imageInfo.extent.width == desc.width
        && imageInfo.extent.height == desc.height
        && imageInfo.extent.depth == desc.depth
        && imageInfo.mipLevels == desc.mipLevels
        && imageInfo.arrayLayers == desc.arraySize
        && imageInfo.samples == VulkanDetail::GetSampleCountFlagBits(desc.sampleCount)
        && imageInfo.tiling == VK_IMAGE_TILING_OPTIMAL
        && (imageInfo.usage & (descriptionUsage | requiredUsage)) == (descriptionUsage | requiredUsage)
        && (imageInfo.flags & requiredFlags) == requiredFlags
        && (imageInfo.flags & (VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT | VK_IMAGE_CREATE_PROTECTED_BIT)) == 0u
        && sharingInfoConsistent
        && imageInfo.initialLayout == VK_IMAGE_LAYOUT_UNDEFINED
    ;
}

[[nodiscard]] inline bool IsTextureImageWithinFormatLimits(
    const VkImageCreateInfo& imageInfo,
    const VkImageFormatProperties& formatProperties
)noexcept{
    return
        imageInfo.extent.width <= formatProperties.maxExtent.width
        && imageInfo.extent.height <= formatProperties.maxExtent.height
        && imageInfo.extent.depth <= formatProperties.maxExtent.depth
        && imageInfo.mipLevels <= formatProperties.maxMipLevels
        && imageInfo.arrayLayers <= formatProperties.maxArrayLayers
        && (formatProperties.sampleCounts & imageInfo.samples) == imageInfo.samples
    ;
}

inline bool ValidateTextureViewShape(const TextureDimension::Enum dimension, const TextureSubresourceSet& subresources){
    if(dimension == TextureDimension::TextureCube && subresources.numArraySlices != s_TextureCubeLayerCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: cube views must include exactly 6 array layers"));
        return false;
    }
    if(dimension == TextureDimension::TextureCubeArray && (subresources.numArraySlices < s_TextureCubeLayerCount || (subresources.numArraySlices % s_TextureCubeLayerCount) != 0)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create image view: cube array views must include a positive multiple of 6 array layers"));
        return false;
    }

    return true;
}

inline bool ReportTextureCreateDescError(const tchar* operationName, const tchar* message, const bool assertFailure){
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
    if(assertFailure)
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
    return false;
}

inline bool ValidateTextureCreateDesc(
    const TextureDesc& desc,
    const tchar* operationName,
    const bool assertFailure,
    TextureCreateMetadata& outMetadata
){
    outMetadata = {};
    if(!ResourceQueueSharing::IsValid(desc.queueSharing))
        return ReportTextureCreateDescError(operationName, NWB_TEXT("queue sharing contains unknown bits"), assertFailure);
    if(!IsTextureCreationStateMaskValid(desc.initialState))
        return ReportTextureCreateDescError(operationName, NWB_TEXT("initial state is invalid for a texture"), assertFailure);
    if(!VulkanDetail::ValidateTextureShape(desc, operationName)){
        if(assertFailure)
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: invalid texture shape"), operationName);
        return false;
    }

    outMetadata.format = VulkanDetail::ConvertFormat(desc.format);
    if(outMetadata.format == VK_FORMAT_UNDEFINED)
        return ReportTextureCreateDescError(operationName, NWB_TEXT("format is unsupported"), assertFailure);

    const FormatInfo& formatInfo = GetFormatInfo(desc.format);
    if(!VulkanDetail::GetTextureFormatBlockLayout(formatInfo, outMetadata.formatLayout))
        return ReportTextureCreateDescError(operationName, NWB_TEXT("invalid texture format"), assertFailure);

    outMetadata.aspectMask = VulkanDetail::GetImageAspectMask(formatInfo);
    if(!VulkanDetail::IsSupportedSampleCount(desc.sampleCount))
        return ReportTextureCreateDescError(operationName, NWB_TEXT("sample count is unsupported"), assertFailure);
    if(desc.sampleQuality != 0u)
        return ReportTextureCreateDescError(operationName, NWB_TEXT("sample quality must be zero"), assertFailure);
    if(!TryTextureDimensionToImageType(desc.dimension, outMetadata.imageType))
        return ReportTextureCreateDescError(operationName, NWB_TEXT("texture dimension is unsupported"), assertFailure);
    if(
        desc.sampleCount != 1u
        && (
            outMetadata.imageType != VK_IMAGE_TYPE_2D
            || (PickImageFlags(desc) & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0u
        )
    )
        return ReportTextureCreateDescError(
            operationName,
            NWB_TEXT("multisampled textures must be 2D and not cube-compatible"),
            assertFailure
        );
    if(desc.sampleCount != 1 && desc.mipLevels != 1)
        return ReportTextureCreateDescError(operationName, NWB_TEXT("multisampled texture mip levels must be 1"), assertFailure);

    outMetadata.usage = PickImageUsage(desc, outMetadata.aspectMask);
    const VkImageUsageFlags requiredInitialUsage = RequiredImageUsageForResourceStates(desc.initialState);
    if((outMetadata.usage & requiredInitialUsage) != requiredInitialUsage){
        return ReportTextureCreateDescError(
            operationName,
            NWB_TEXT("initial state requires an undeclared image usage"),
            assertFailure
        );
    }
    outMetadata.flags = PickImageFlags(desc);
    outMetadata.sampleCount = VulkanDetail::GetSampleCountFlagBits(desc.sampleCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

