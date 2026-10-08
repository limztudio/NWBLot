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


Expected<TextureClearContract> ResolveTextureClearContract(
    const TextureDesc& description,
    const TextureSubresourceSet& subresources,
    const TextureClearValueKind::Enum valueKind,
    const bool clearDepth,
    const bool clearStencil
)noexcept{
    const auto nativeContract = VulkanTextureDetail::ResolveTextureClearContract(
        description, subresources,
        static_cast<VulkanTextureDetail::TextureClearValueKind::Enum>(valueKind),
        clearDepth, clearStencil
    );
    if(!nativeContract)
        return MakeUnexpected(nativeContract.error());
    return TextureClearContract{
        .subresources = nativeContract->subresources,
        .queueRequirement = static_cast<TextureClearQueueRequirement::Enum>(nativeContract->queueRequirement),
    };
}

Expected<TextureCopyContract> ResolveTextureCopyContract(
    const TextureDesc& sourceDescription,
    const TextureSlice& sourceSlice,
    const TextureDesc& destinationDescription,
    const TextureSlice& destinationSlice
)noexcept{
    const auto nativeContract = VulkanTextureDetail::ResolveTextureCopyContract(
        sourceDescription, sourceSlice, destinationDescription, destinationSlice
    );
    if(!nativeContract)
        return MakeUnexpected(nativeContract.error());
    return TextureCopyContract{
        .sourceSlice = nativeContract->sourceSlice,
        .destinationSlice = nativeContract->destinationSlice,
        .queueRequirement = static_cast<TextureCopyQueueRequirement::Enum>(nativeContract->queueRequirement),
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

