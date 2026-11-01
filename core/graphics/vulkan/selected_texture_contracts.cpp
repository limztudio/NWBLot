// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "selected_texture_clear_contract.h"
#include "selected_texture_copy_contract.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(static_cast<u8>(TextureClearValueKind::Float) == static_cast<u8>(VulkanTextureDetail::TextureClearValueKind::Float));
static_assert(static_cast<u8>(TextureClearValueKind::UInt) == static_cast<u8>(VulkanTextureDetail::TextureClearValueKind::UInt));
static_assert(static_cast<u8>(TextureClearValueKind::Int) == static_cast<u8>(VulkanTextureDetail::TextureClearValueKind::Int));
static_assert(static_cast<u8>(TextureClearValueKind::DepthStencil) == static_cast<u8>(VulkanTextureDetail::TextureClearValueKind::DepthStencil));
static_assert(static_cast<u8>(TextureClearQueueRequirement::Transfer) == static_cast<u8>(VulkanTextureDetail::TextureClearQueueRequirement::Transfer));
static_assert(static_cast<u8>(TextureClearQueueRequirement::ComputeOrGraphics) == static_cast<u8>(VulkanTextureDetail::TextureClearQueueRequirement::ComputeOrGraphics));
static_assert(static_cast<u8>(TextureClearQueueRequirement::Graphics) == static_cast<u8>(VulkanTextureDetail::TextureClearQueueRequirement::Graphics));
static_assert(static_cast<u8>(TextureCopyQueueRequirement::Transfer) == static_cast<u8>(VulkanTextureDetail::TextureCopyQueueRequirement::Transfer));
static_assert(static_cast<u8>(TextureCopyQueueRequirement::ComputeOrGraphics) == static_cast<u8>(VulkanTextureDetail::TextureCopyQueueRequirement::ComputeOrGraphics));
static_assert(static_cast<u8>(TextureCopyQueueRequirement::Graphics) == static_cast<u8>(VulkanTextureDetail::TextureCopyQueueRequirement::Graphics));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanTextureDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TextureCopyContract> ResolveTextureCopyContract(const TextureDesc& sourceDesc, const TextureSlice& sourceSlice, const TextureDesc& destinationDesc, const TextureSlice& destinationSlice)noexcept{
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

    const VkExtent3D sourceMipExtent = VulkanDetail::GetTextureMipExtent(sourceDesc, contract.sourceSlice.mipLevel);
    const VkExtent3D destinationMipExtent = VulkanDetail::GetTextureMipExtent(destinationDesc, contract.destinationSlice.mipLevel);
    const b8 sourceWholeMip = contract.sourceSlice.x == 0u
        && contract.sourceSlice.y == 0u
        && contract.sourceSlice.z == 0u
        && contract.sourceSlice.width == sourceMipExtent.width
        && contract.sourceSlice.height == sourceMipExtent.height
        && contract.sourceSlice.depth == sourceMipExtent.depth
    ;
    const b8 destinationWholeMip = contract.destinationSlice.x == 0u
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
    if(sourceDesc.sampleCount > 1u && (contract.aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) != 0u)
        contract.queueRequirement = static_cast<decltype(contract.queueRequirement)>(TextureCopyQueueRequirement::Graphics);
    else if(!sourceWholeMip || !destinationWholeMip)
        contract.queueRequirement = static_cast<decltype(contract.queueRequirement)>(TextureCopyQueueRequirement::ComputeOrGraphics);

    return contract;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TextureClearContract> ResolveTextureClearContract(const TextureDesc& description, const TextureSubresourceSet& subresources, const TextureClearValueKind::Enum valueKind, const b8 clearDepth, const b8 clearStencil)noexcept{
    const auto nativeContract = VulkanTextureDetail::ResolveTextureClearContract(description, subresources, static_cast<VulkanTextureDetail::TextureClearValueKind::Enum>(valueKind), clearDepth, clearStencil);
    if(!nativeContract)
        return MakeUnexpected(nativeContract.error());
    return TextureClearContract{ .subresources = nativeContract->subresources, .queueRequirement = static_cast<TextureClearQueueRequirement::Enum>(nativeContract->queueRequirement), };
}

Expected<TextureCopyContract> ResolveTextureCopyContract(const TextureDesc& sourceDescription, const TextureSlice& sourceSlice, const TextureDesc& destinationDescription, const TextureSlice& destinationSlice)noexcept{
    const auto nativeContract = VulkanTextureDetail::ResolveTextureCopyContract(sourceDescription, sourceSlice, destinationDescription, destinationSlice);
    if(!nativeContract)
        return MakeUnexpected(nativeContract.error());
    return TextureCopyContract{ .sourceSlice = nativeContract->sourceSlice, .destinationSlice = nativeContract->destinationSlice, .queueRequirement = static_cast<TextureCopyQueueRequirement::Enum>(nativeContract->queueRequirement), };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

