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


// Inline native validation at these provider calls to avoid an additional resolver frame.
bool ResolveTextureClearContract(
    const TextureDesc& description,
    const TextureSubresourceSet& subresources,
    const TextureClearValueKind::Enum valueKind,
    const bool clearDepth,
    const bool clearStencil,
    TextureClearContract& outContract
)noexcept{
    VulkanTextureDetail::TextureClearContract nativeContract;
    bool valid = false;
    [[clang::always_inline]] valid = VulkanTextureDetail::ResolveTextureClearContract(
        description, subresources,
        static_cast<VulkanTextureDetail::TextureClearValueKind::Enum>(valueKind),
        clearDepth, clearStencil, nativeContract
    );
    outContract = {
        .subresources = nativeContract.subresources,
        .queueRequirement = static_cast<TextureClearQueueRequirement::Enum>(nativeContract.queueRequirement),
    };
    return valid;
}

bool ResolveTextureCopyContract(
    const TextureDesc& sourceDescription,
    const TextureSlice& sourceSlice,
    const TextureDesc& destinationDescription,
    const TextureSlice& destinationSlice,
    TextureCopyContract& outContract
)noexcept{
    VulkanDetail::TextureFormatBlockLayout formatLayout;
    VkImageType imageType = VK_IMAGE_TYPE_MAX_ENUM;
    VkImageAspectFlags aspectMask = 0u;
    bool valid = false;
    [[clang::always_inline]] valid = VulkanTextureDetail::ResolveTextureCopyContractWithMetadata(
        sourceDescription, sourceSlice, destinationDescription, destinationSlice,
        outContract, formatLayout, imageType, aspectMask
    );
    return valid;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

