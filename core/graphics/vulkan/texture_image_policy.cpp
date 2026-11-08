// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_image_policy.h"
#include "texture_shape_policy.h"

#include <core/graphics/rhi/queue_sharing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


namespace VulkanTextureDetail{


VkImageUsageFlags TextureImagePolicy::requiredImageUsageForResourceStates(const ResourceStates::Mask states)noexcept{
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

VkImageViewType TextureImagePolicy::textureDimensionToViewType(TextureDimension::Enum dimension)noexcept{
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

VkImageUsageFlags TextureImagePolicy::pickImageUsage(const TextureDesc& desc, const VkImageAspectFlags aspectMask)noexcept{
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

VkImageCreateFlags TextureImagePolicy::pickImageFlags(const TextureDesc& desc)noexcept{
    VkImageCreateFlags flags = 0;

    if(desc.dimension == TextureDimension::TextureCube || desc.dimension == TextureDimension::TextureCubeArray)
        flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    if(desc.dimension == TextureDimension::Texture3D && desc.isRenderTarget)
        flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;

    return flags;
}

VkImageCreateInfo TextureImagePolicy::buildTextureImageCreateInfo(const TextureDesc& desc, const TextureCreateMetadata& metadata)noexcept{
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

b8 TextureImagePolicy::isTextureImageInfoConsistent(const TextureDesc& desc, const VkImageCreateInfo& imageInfo, const VkImageUsageFlags requiredUsage)noexcept{
    const auto expectedImageType = TryTextureDimensionToImageType(desc.dimension);
    if(!expectedImageType || !VulkanDetail::IsSupportedSampleCount(desc.sampleCount) || desc.sampleQuality != 0u)
        return false;

    const VkFormat expectedFormat = VulkanDetail::ConvertFormat(desc.format);
    if(expectedFormat == VK_FORMAT_UNDEFINED)
        return false;
    const VkImageAspectFlags expectedAspectMask = VulkanDetail::GetImageAspectMask(GetFormatInfo(desc.format));
    const VkImageUsageFlags descriptionUsage = (pickImageUsage(desc, expectedAspectMask) & ~(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) | requiredImageUsageForResourceStates(desc.initialState);
    const VkImageCreateFlags requiredFlags = pickImageFlags(desc);
    const b8 sharingInfoConsistent =
        (imageInfo.sharingMode == VK_SHARING_MODE_EXCLUSIVE && imageInfo.queueFamilyIndexCount == 0u && imageInfo.pQueueFamilyIndices == nullptr)
        || (imageInfo.sharingMode == VK_SHARING_MODE_CONCURRENT && imageInfo.queueFamilyIndexCount >= 2u && imageInfo.pQueueFamilyIndices != nullptr)
    ;
    return
        imageInfo.sType == VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
        && imageInfo.imageType == *expectedImageType
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

b8 TextureImagePolicy::textureFormatMetadataMatches(const Format::Enum format, const VkImageAspectFlags aspectMask, const VulkanDetail::TextureFormatBlockLayout& formatLayout)noexcept{
    const FormatInfo& formatInfo = GetFormatInfo(format);
    const auto expectedFormatLayout = VulkanDetail::GetTextureFormatBlockLayout(formatInfo);
    return
        expectedFormatLayout
        && aspectMask == VulkanDetail::GetImageAspectMask(formatInfo)
        && formatLayout.blockWidth == expectedFormatLayout->blockWidth
        && formatLayout.blockHeight == expectedFormatLayout->blockHeight
        && formatLayout.bytesPerBlock == expectedFormatLayout->bytesPerBlock
    ;
}

b8 TextureImagePolicy::validateTextureViewShape(const TextureDimension::Enum dimension, const TextureSubresourceSet& subresources){
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

Unexpected<Failure> TextureImagePolicy::reportTextureCreateDescError(TStringView operationName, TStringView message, const b8 assertFailure){
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
    if(assertFailure)
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: {}"), operationName, message);
    return MakeUnexpected(Failure{});
}

Expected<TextureCreateMetadata> TextureImagePolicy::validateTextureCreateDesc(const TextureDesc& desc, TStringView operationName, const b8 assertFailure){
    TextureCreateMetadata metadata{};
    if(!ResourceQueueSharing::IsValid(desc.queueSharing))
        return reportTextureCreateDescError(operationName, NWB_TEXT("queue sharing contains unknown bits"), assertFailure);
    if(!IsTextureCreationStateMaskValid(desc.initialState))
        return reportTextureCreateDescError(operationName, NWB_TEXT("initial state is invalid for a texture"), assertFailure);
    if(!TextureShapePolicy::validate(desc, operationName)){
        if(assertFailure)
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: invalid texture shape"), operationName);
        return MakeUnexpected(Failure{});
    }

    metadata.format = VulkanDetail::ConvertFormat(desc.format);
    if(metadata.format == VK_FORMAT_UNDEFINED)
        return reportTextureCreateDescError(operationName, NWB_TEXT("format is unsupported"), assertFailure);

    const FormatInfo& formatInfo = GetFormatInfo(desc.format);
    const auto formatLayout = VulkanDetail::GetTextureFormatBlockLayout(formatInfo);
    if(!formatLayout)
        return reportTextureCreateDescError(operationName, NWB_TEXT("invalid texture format"), assertFailure);
    metadata.formatLayout = *formatLayout;

    metadata.aspectMask = VulkanDetail::GetImageAspectMask(formatInfo);
    if(!VulkanDetail::IsSupportedSampleCount(desc.sampleCount))
        return reportTextureCreateDescError(operationName, NWB_TEXT("sample count is unsupported"), assertFailure);
    if(desc.sampleQuality != 0u)
        return reportTextureCreateDescError(operationName, NWB_TEXT("sample quality must be zero"), assertFailure);
    const auto imageType = TryTextureDimensionToImageType(desc.dimension);
    if(!imageType)
        return reportTextureCreateDescError(operationName, NWB_TEXT("texture dimension is unsupported"), assertFailure);
    metadata.imageType = *imageType;
    if(desc.sampleCount != 1u && (metadata.imageType != VK_IMAGE_TYPE_2D || (pickImageFlags(desc) & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0u))
        return reportTextureCreateDescError(operationName, NWB_TEXT("multisampled textures must be 2D and not cube-compatible"), assertFailure);
    if(desc.sampleCount != 1 && desc.mipLevels != 1)
        return reportTextureCreateDescError(operationName, NWB_TEXT("multisampled texture mip levels must be 1"), assertFailure);

    metadata.usage = pickImageUsage(desc, metadata.aspectMask);
    const VkImageUsageFlags requiredInitialUsage = requiredImageUsageForResourceStates(desc.initialState);
    if((metadata.usage & requiredInitialUsage) != requiredInitialUsage){
        return reportTextureCreateDescError(operationName, NWB_TEXT("initial state requires an undeclared image usage"), assertFailure);
    }
    metadata.flags = pickImageFlags(desc);
    metadata.sampleCount = VulkanDetail::GetSampleCountFlagBits(desc.sampleCount);
    return metadata;
}



};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
