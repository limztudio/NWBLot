// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanStateTrackingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] VkImageMemoryBarrier2 BuildTextureStateBarrier(
    VkImage image,
    VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    ResourceStates::Mask oldState,
    ResourceStates::Mask stateBits,
    bool rayTracingStageAvailable
);

[[nodiscard]] ResourceStates::Mask NormalizeOwnershipState(ResourceStates::Mask state);

[[nodiscard]] VkImageMemoryBarrier2 BuildTextureOwnershipReleaseBarrier(
    VkImage image,
    VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    ResourceStates::Mask state,
    u32 sourceQueueFamily,
    u32 destinationQueueFamily,
    bool rayTracingStageAvailable
);

[[nodiscard]] VkImageMemoryBarrier2 BuildTextureOwnershipAcquireBarrier(
    VkImage image,
    VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    ResourceStates::Mask state,
    u32 sourceQueueFamily,
    u32 destinationQueueFamily,
    bool rayTracingStageAvailable
);

[[nodiscard]] VkBufferMemoryBarrier2 BuildBufferOwnershipReleaseBarrier(
    VkBuffer buffer,
    ResourceStates::Mask state,
    u32 sourceQueueFamily,
    u32 destinationQueueFamily,
    bool rayTracingStageAvailable
);

[[nodiscard]] VkBufferMemoryBarrier2 BuildBufferOwnershipAcquireBarrier(
    VkBuffer buffer,
    ResourceStates::Mask state,
    u32 sourceQueueFamily,
    u32 destinationQueueFamily,
    bool rayTracingStageAvailable
);

[[nodiscard]] bool NeedsTextureStateBarrier(ResourceStates::Mask oldState, ResourceStates::Mask stateBits, bool uavBarrierEnabled);

[[nodiscard]] bool ImageBarrierOverlapsTextureSubresources(
    const VkImageMemoryBarrier2& barrier,
    VkImage image,
    VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources
)noexcept;

void NormalizeBarrierScopeForQueueCapabilities(
    GpuQueueCapability::Mask capabilities,
    VkPipelineStageFlags2& stageMask,
    VkAccessFlags2& accessMask
)noexcept;

void AppendTextureStateBarrier(
    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& barriers,
    VkImage image,
    VkImageAspectFlags aspectMask,
    ArraySlice arraySlice,
    MipLevel mipLevel,
    ResourceStates::Mask oldState,
    ResourceStates::Mask stateBits,
    bool rayTracingStageAvailable
);

void AppendTextureStateBarriersBefore(
    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& barriers,
    VkImage image,
    VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    MipLevel mipEnd,
    ArraySlice currentArraySlice,
    MipLevel currentMipLevel,
    ResourceStates::Mask oldState,
    ResourceStates::Mask stateBits,
    bool rayTracingStageAvailable
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

