// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


namespace VulkanTextureDetail{


class TextureImagePolicy{
public:
    [[nodiscard]] static VkImageUsageFlags requiredImageUsageForResourceStates(ResourceStates::Mask states)noexcept;
    [[nodiscard]] static VkImageViewType textureDimensionToViewType(TextureDimension::Enum dimension)noexcept;
    [[nodiscard]] static VkImageUsageFlags pickImageUsage(const TextureDesc& desc, VkImageAspectFlags aspectMask)noexcept;
    [[nodiscard]] static VkImageCreateFlags pickImageFlags(const TextureDesc& desc)noexcept;
    [[nodiscard]] static VkImageCreateInfo buildTextureImageCreateInfo(const TextureDesc& desc, const TextureCreateMetadata& metadata)noexcept;
    [[nodiscard]] static b8 isTextureImageInfoConsistent(const TextureDesc& desc, const VkImageCreateInfo& imageInfo, VkImageUsageFlags requiredUsage = 0u)noexcept;
    [[nodiscard]] static b8 textureFormatMetadataMatches(Format::Enum format, VkImageAspectFlags aspectMask, const VulkanDetail::TextureFormatBlockLayout& formatLayout)noexcept;
    [[nodiscard]] static b8 validateTextureViewShape(TextureDimension::Enum dimension, const TextureSubresourceSet& subresources);
    [[nodiscard]] static Expected<TextureCreateMetadata> validateTextureCreateDesc(const TextureDesc& desc, TStringView operationName, b8 assertFailure);

private:
    [[nodiscard]] static Unexpected<Failure> reportTextureCreateDescError(TStringView operationName, TStringView message, b8 assertFailure);
};


};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
