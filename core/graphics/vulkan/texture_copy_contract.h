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
    enum Enum : u8{
        Transfer = 0u,
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

[[nodiscard]] inline bool ResolveTextureCopyContract(
    const TextureDesc& sourceDesc,
    const TextureSlice& sourceSlice,
    const TextureDesc& destinationDesc,
    const TextureSlice& destinationSlice,
    TextureCopyContract& outContract
)noexcept{
    outContract = {};
    if(!IsTextureDescShapeValid(sourceDesc) || !IsTextureDescShapeValid(destinationDesc))
        return false;

    VkImageType sourceImageType = VK_IMAGE_TYPE_MAX_ENUM;
    VkImageType destinationImageType = VK_IMAGE_TYPE_MAX_ENUM;
    if(
        sourceDesc.format != destinationDesc.format
        || sourceDesc.sampleCount != destinationDesc.sampleCount
        || sourceDesc.sampleQuality != 0u
        || destinationDesc.sampleQuality != 0u
        || !VulkanDetail::IsSupportedSampleCount(sourceDesc.sampleCount)
        || VulkanDetail::ConvertFormat(sourceDesc.format) == VK_FORMAT_UNDEFINED
        || !TryTextureDimensionToImageType(sourceDesc.dimension, sourceImageType)
        || !TryTextureDimensionToImageType(destinationDesc.dimension, destinationImageType)
        || sourceImageType != destinationImageType
        || !VulkanDetail::GetTextureFormatBlockLayout(
            GetFormatInfo(sourceDesc.format),
            outContract.formatLayout
        )
        || !VulkanDetail::IsTextureSliceInBounds(
            sourceDesc,
            sourceSlice,
            outContract.formatLayout,
            &outContract.sourceSlice
        )
        || !VulkanDetail::IsTextureSliceInBounds(
            destinationDesc,
            destinationSlice,
            outContract.formatLayout,
            &outContract.destinationSlice
        )
    )
        return false;

    if(sourceDesc.sampleCount != 1u){
        if(
            sourceImageType != VK_IMAGE_TYPE_2D
            || (PickImageFlags(sourceDesc) & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0u
            || (PickImageFlags(destinationDesc) & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0u
            || sourceDesc.mipLevels != 1u
            || destinationDesc.mipLevels != 1u
            || outContract.formatLayout.blockWidth != 1u
            || outContract.formatLayout.blockHeight != 1u
        )
            return false;
    }

    if(
        outContract.sourceSlice.width != outContract.destinationSlice.width
        || outContract.sourceSlice.height != outContract.destinationSlice.height
        || outContract.sourceSlice.depth != outContract.destinationSlice.depth
        || outContract.sourceSlice.x > static_cast<u32>(Limit<i32>::s_Max)
        || outContract.sourceSlice.y > static_cast<u32>(Limit<i32>::s_Max)
        || outContract.sourceSlice.z > static_cast<u32>(Limit<i32>::s_Max)
        || outContract.destinationSlice.x > static_cast<u32>(Limit<i32>::s_Max)
        || outContract.destinationSlice.y > static_cast<u32>(Limit<i32>::s_Max)
        || outContract.destinationSlice.z > static_cast<u32>(Limit<i32>::s_Max)
    )
        return false;

    const VkExtent3D sourceMipExtent = VulkanDetail::GetTextureMipExtent(
        sourceDesc,
        outContract.sourceSlice.mipLevel
    );
    const VkExtent3D destinationMipExtent = VulkanDetail::GetTextureMipExtent(
        destinationDesc,
        outContract.destinationSlice.mipLevel
    );
    const bool sourceWholeMip = outContract.sourceSlice.x == 0u
        && outContract.sourceSlice.y == 0u
        && outContract.sourceSlice.z == 0u
        && outContract.sourceSlice.width == sourceMipExtent.width
        && outContract.sourceSlice.height == sourceMipExtent.height
        && outContract.sourceSlice.depth == sourceMipExtent.depth
    ;
    const bool destinationWholeMip = outContract.destinationSlice.x == 0u
        && outContract.destinationSlice.y == 0u
        && outContract.destinationSlice.z == 0u
        && outContract.destinationSlice.width == destinationMipExtent.width
        && outContract.destinationSlice.height == destinationMipExtent.height
        && outContract.destinationSlice.depth == destinationMipExtent.depth
    ;

    outContract.imageType = sourceImageType;
    outContract.aspectMask = VulkanDetail::GetImageAspectMask(GetFormatInfo(sourceDesc.format));
    if(outContract.aspectMask == 0u)
        return false;
    if(
        sourceDesc.sampleCount > 1u
        && (outContract.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u
    )
        outContract.queueRequirement = TextureCopyQueueRequirement::Graphics;
    else if(!sourceWholeMip || !destinationWholeMip)
        outContract.queueRequirement = TextureCopyQueueRequirement::ComputeOrGraphics;

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

