// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureCopyQueueRequirement{
    static constexpr u8 s_TextureCopyQueueRequirementTransferBase = 0u;
    enum Enum : u8{
        Transfer = s_TextureCopyQueueRequirementTransferBase,
        ComputeOrGraphics,
        Graphics,
    };
};

struct TextureCopyContract{
    TextureSlice sourceSlice;
    TextureSlice destinationSlice;
    VulkanDetail::TextureFormatBlockLayout formatLayout;
    VkImageType imageType = VK_IMAGE_TYPE_MAX_ENUM;
    VkImageAspectFlags aspectMask = 0u;
    TextureCopyQueueRequirement::Enum queueRequirement = TextureCopyQueueRequirement::Transfer;
};

[[nodiscard]] inline Expected<TextureCopyContract> ResolveTextureCopyContract(
    const TextureDesc& sourceDesc,
    const TextureSlice& sourceSlice,
    const TextureDesc& destinationDesc,
    const TextureSlice& destinationSlice
)noexcept{
    TextureCopyContract contract{};
    if(!IsTextureDescShapeValid(sourceDesc) || !IsTextureDescShapeValid(destinationDesc))
        return MakeUnexpected(Failure{});

    if(
        sourceDesc.format != destinationDesc.format
        || sourceDesc.sampleCount != destinationDesc.sampleCount
        || sourceDesc.sampleQuality != 0u
        || destinationDesc.sampleQuality != 0u
        || !VulkanDetail::IsSupportedSampleCount(sourceDesc.sampleCount)
        || VulkanDetail::ConvertFormat(sourceDesc.format) == VK_FORMAT_UNDEFINED
    )
        return MakeUnexpected(Failure{});
    const auto sourceImageType = TryTextureDimensionToImageType(sourceDesc.dimension);
    const auto destinationImageType = TryTextureDimensionToImageType(destinationDesc.dimension);
    if(!sourceImageType || !destinationImageType || *sourceImageType != *destinationImageType)
        return MakeUnexpected(Failure{});
    const auto formatLayout = VulkanDetail::GetTextureFormatBlockLayout(GetFormatInfo(sourceDesc.format));
    if(!formatLayout)
        return MakeUnexpected(Failure{});
    const auto resolvedSource = VulkanDetail::ResolveTextureSlice(sourceDesc, sourceSlice, *formatLayout);
    const auto resolvedDestination = VulkanDetail::ResolveTextureSlice(destinationDesc, destinationSlice, *formatLayout);
    if(!resolvedSource || !resolvedDestination)
        return MakeUnexpected(Failure{});
    contract.sourceSlice = *resolvedSource;
    contract.destinationSlice = *resolvedDestination;

    if(sourceDesc.sampleCount != 1u){
        if(
            *sourceImageType != VK_IMAGE_TYPE_2D
            || (PickImageFlags(sourceDesc) & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0u
            || (PickImageFlags(destinationDesc) & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0u
            || sourceDesc.mipLevels != 1u
            || destinationDesc.mipLevels != 1u
            || formatLayout->blockWidth != 1u
            || formatLayout->blockHeight != 1u
        )
            return MakeUnexpected(Failure{});
    }

    if(
        contract.sourceSlice.width != contract.destinationSlice.width
        || contract.sourceSlice.height != contract.destinationSlice.height
        || contract.sourceSlice.depth != contract.destinationSlice.depth
        || contract.sourceSlice.x > static_cast<u32>(Limit<i32>::s_Max)
        || contract.sourceSlice.y > static_cast<u32>(Limit<i32>::s_Max)
        || contract.sourceSlice.z > static_cast<u32>(Limit<i32>::s_Max)
        || contract.destinationSlice.x > static_cast<u32>(Limit<i32>::s_Max)
        || contract.destinationSlice.y > static_cast<u32>(Limit<i32>::s_Max)
        || contract.destinationSlice.z > static_cast<u32>(Limit<i32>::s_Max)
    )
        return MakeUnexpected(Failure{});

    const VkExtent3D sourceMipExtent = VulkanDetail::GetTextureMipExtent(
        sourceDesc,
        contract.sourceSlice.mipLevel
    );
    const VkExtent3D destinationMipExtent = VulkanDetail::GetTextureMipExtent(
        destinationDesc,
        contract.destinationSlice.mipLevel
    );
    const bool sourceWholeMip = contract.sourceSlice.x == 0u
        && contract.sourceSlice.y == 0u
        && contract.sourceSlice.z == 0u
        && contract.sourceSlice.width == sourceMipExtent.width
        && contract.sourceSlice.height == sourceMipExtent.height
        && contract.sourceSlice.depth == sourceMipExtent.depth
    ;
    const bool destinationWholeMip = contract.destinationSlice.x == 0u
        && contract.destinationSlice.y == 0u
        && contract.destinationSlice.z == 0u
        && contract.destinationSlice.width == destinationMipExtent.width
        && contract.destinationSlice.height == destinationMipExtent.height
        && contract.destinationSlice.depth == destinationMipExtent.depth
    ;

    contract.formatLayout = *formatLayout;
    contract.imageType = *sourceImageType;
    contract.aspectMask = VulkanDetail::GetImageAspectMask(GetFormatInfo(sourceDesc.format));
    if(contract.aspectMask == 0u)
        return MakeUnexpected(Failure{});
    if(
        sourceDesc.sampleCount > 1u
        && (contract.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u
    )
        contract.queueRequirement = static_cast<decltype(contract.queueRequirement)>(TextureCopyQueueRequirement::Graphics);
    else if(!sourceWholeMip || !destinationWholeMip)
        contract.queueRequirement = static_cast<decltype(contract.queueRequirement)>(TextureCopyQueueRequirement::ComputeOrGraphics);

    return contract;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

