// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_image_policy.h"
#include "texture_shape_policy.h"
#include "selected_texture_copy_contract.h"
#include "texture_transfer_layout.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 SliceCoversWholeMipExtent(const TextureDesc& description, const TextureSlice& resolvedSlice)noexcept{
    const VkExtent3D mipExtent = VulkanDetail::GetTextureMipExtent(description, resolvedSlice.mipLevel);
    return resolvedSlice.x == 0u
        && resolvedSlice.y == 0u
        && resolvedSlice.z == 0u
        && resolvedSlice.width == mipExtent.width
        && resolvedSlice.height == mipExtent.height
        && resolvedSlice.depth == mipExtent.depth
    ;
}

Expected<TextureCopyContract> ResolveTextureCopyContract(const TextureDesc& sourceDesc, const TextureSlice& sourceSlice, const TextureDesc& destinationDesc, const TextureSlice& destinationSlice)noexcept{
    TextureCopyContract contract{};
    if(!TextureShapePolicy::isValid(sourceDesc) || !TextureShapePolicy::isValid(destinationDesc))
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
    const auto resolvedSource = VulkanDetail::TextureTransferLayout::resolveSlice(sourceDesc, sourceSlice, *formatLayout);
    const auto resolvedDestination = VulkanDetail::TextureTransferLayout::resolveSlice(destinationDesc, destinationSlice, *formatLayout);
    if(!resolvedSource || !resolvedDestination)
        return MakeUnexpected(Failure{});
    contract.sourceSlice = *resolvedSource;
    contract.destinationSlice = *resolvedDestination;

    if(sourceDesc.sampleCount != 1u){
        if(
            *sourceImageType != VK_IMAGE_TYPE_2D
            || (TextureImagePolicy::pickImageFlags(sourceDesc) & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0u
            || (TextureImagePolicy::pickImageFlags(destinationDesc) & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) != 0u
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

    const b8 sourceWholeMip = SliceCoversWholeMipExtent(sourceDesc, contract.sourceSlice);
    const b8 destinationWholeMip = SliceCoversWholeMipExtent(destinationDesc, contract.destinationSlice);

    contract.formatLayout = *formatLayout;
    contract.imageType = *sourceImageType;
    contract.aspectMask = VulkanDetail::GetImageAspectMask(GetFormatInfo(sourceDesc.format));
    if(contract.aspectMask == 0u)
        return MakeUnexpected(Failure{});
    if(sourceDesc.sampleCount > 1u && (contract.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u)
        contract.queueRequirement = TextureCopyQueueRequirement::Graphics;
    else if(!sourceWholeMip || !destinationWholeMip)
        contract.queueRequirement = TextureCopyQueueRequirement::ComputeOrGraphics;

    return contract;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TextureCopyContract> ResolveTextureCopyContract(const TextureDesc& sourceDescription, const TextureSlice& sourceSlice, const TextureDesc& destinationDescription, const TextureSlice& destinationSlice)noexcept{
    const auto nativeContract = VulkanTextureDetail::ResolveTextureCopyContract(sourceDescription, sourceSlice, destinationDescription, destinationSlice);
    if(!nativeContract)
        return MakeUnexpected(nativeContract.error());
    return TextureCopyContract{ .sourceSlice = nativeContract->sourceSlice, .destinationSlice = nativeContract->destinationSlice, .queueRequirement = nativeContract->queueRequirement, };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

