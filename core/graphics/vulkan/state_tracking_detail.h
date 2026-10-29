// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanStateTrackingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_InitialStateUnknownReason = NWB_TEXT("initial state cannot be unknown");
inline constexpr TStringView s_SubresourceRangeOutsideTextureReason = NWB_TEXT("subresource range is empty or outside the texture");
inline constexpr TStringView s_DestinationQueueUnavailableReason = NWB_TEXT("destination queue is unavailable");

[[nodiscard]] VkImageMemoryBarrier2 BuildTextureStateBarrier(VkImage image, VkImageAspectFlags aspectMask, const TextureSubresourceSet& subresources, ResourceStates::Mask oldState, ResourceStates::Mask stateBits, b8 rayTracingStageAvailable)noexcept;

[[nodiscard]] ResourceStates::Mask NormalizeOwnershipState(ResourceStates::Mask state)noexcept;

[[nodiscard]] VkImageMemoryBarrier2 BuildTextureOwnershipReleaseBarrier(
    VkImage image,
    VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    ResourceStates::Mask state,
    u32 sourceQueueFamily,
    u32 destinationQueueFamily,
    b8 rayTracingStageAvailable
)noexcept;

[[nodiscard]] VkImageMemoryBarrier2 BuildTextureOwnershipAcquireBarrier(
    VkImage image,
    VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    ResourceStates::Mask state,
    u32 sourceQueueFamily,
    u32 destinationQueueFamily,
    b8 rayTracingStageAvailable
)noexcept;

[[nodiscard]] b8 IsBufferStateRangeValid(BufferRange range, const BufferDesc& description)noexcept;

[[nodiscard]] VkBufferMemoryBarrier2 BuildBufferStateBarrier(VkBuffer buffer, BufferRange range, ResourceStates::Mask oldState, ResourceStates::Mask stateBits, b8 rayTracingStageAvailable)noexcept;

[[nodiscard]] VkBufferMemoryBarrier2 BuildBufferOwnershipReleaseBarrier(VkBuffer buffer, ResourceStates::Mask state, u32 sourceQueueFamily, u32 destinationQueueFamily, b8 rayTracingStageAvailable, BufferRange range = s_EntireBuffer)noexcept;

[[nodiscard]] VkBufferMemoryBarrier2 BuildBufferOwnershipAcquireBarrier(VkBuffer buffer, ResourceStates::Mask state, u32 sourceQueueFamily, u32 destinationQueueFamily, b8 rayTracingStageAvailable, BufferRange range = s_EntireBuffer)noexcept;

[[nodiscard]] b8 NeedsResourceStateBarrier(ResourceStates::Mask oldState, ResourceStates::Mask stateBits, b8 uavBarrierEnabled, b8 forceMemoryDependency)noexcept;

[[nodiscard]] b8 ImageBarrierOverlapsTextureSubresources(const VkImageMemoryBarrier2& barrier, VkImage image, VkImageAspectFlags aspectMask, const TextureSubresourceSet& subresources)noexcept;

void NormalizeBarrierScopeForQueueCapabilities(GpuQueueCapability::Mask capabilities, VkPipelineStageFlags2& stageMask, VkAccessFlags2& accessMask)noexcept;

void AppendTextureStateBarrier(
    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& barriers,
    VkImage image,
    VkImageAspectFlags aspectMask,
    ArraySlice arraySlice,
    MipLevel mipLevel,
    ResourceStates::Mask oldState,
    ResourceStates::Mask stateBits,
    b8 rayTracingStageAvailable
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
    b8 rayTracingStageAvailable
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

