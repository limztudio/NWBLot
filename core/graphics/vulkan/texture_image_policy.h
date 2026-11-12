// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


namespace VulkanTextureDetail{


class TextureImagePolicy{
public:
    [[nodiscard]] static VkImageUsageFlags RequiredImageUsageForResourceStates(ResourceStates::Mask states)noexcept;
    [[nodiscard]] static VkImageViewType TextureDimensionToViewType(TextureDimension::Enum dimension)noexcept;
    [[nodiscard]] static VkImageUsageFlags PickImageUsage(const TextureDesc& desc, VkImageAspectFlags aspectMask)noexcept;
    [[nodiscard]] static VkImageCreateFlags PickImageFlags(const TextureDesc& desc)noexcept;
    [[nodiscard]] static VkImageCreateInfo BuildTextureImageCreateInfo(const TextureDesc& desc, const TextureCreateMetadata& metadata)noexcept;
    [[nodiscard]] static b8 IsTextureImageInfoConsistent(const TextureDesc& desc, const VkImageCreateInfo& imageInfo, VkImageUsageFlags requiredUsage = 0u)noexcept;
    [[nodiscard]] static b8 TextureFormatMetadataMatches(Format::Enum format, VkImageAspectFlags aspectMask, const VulkanDetail::TextureFormatBlockLayout& formatLayout)noexcept;
    [[nodiscard]] static b8 ValidateTextureViewShape(TextureDimension::Enum dimension, const TextureSubresourceSet& subresources);
    [[nodiscard]] static Expected<TextureCreateMetadata> ValidateTextureCreateDesc(const TextureDesc& desc, TStringView operationName, b8 assertFailure);

private:
    [[nodiscard]] static Unexpected<Failure> ReportTextureCreateDescError(TStringView operationName, TStringView message, b8 assertFailure);
};


};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

