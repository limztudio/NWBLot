// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"
#include "buffer_resource_detail.h"
#include "texture_resource_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Queue ownership domains are opaque provider identities; the invalid value never names a transport.
inline constexpr u32 s_InvalidQueueOwnershipDomain = Limit<u32>::s_Max;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline u32 GetQueueOwnershipDomain(const Device& device, const GpuPhysicalQueueId& queue)noexcept{ return device.getQueueFamilyIndex(queue); }

[[nodiscard]] constexpr bool IsBufferResourceStateMaskValid(const ResourceStates::Mask state)noexcept{ return VulkanBufferDetail::IsBufferResourceStateMaskValid(state); }

[[nodiscard]] constexpr bool IsTextureResourceStateMaskValid(const ResourceStates::Mask state)noexcept{ return VulkanTextureDetail::IsTextureResourceStateMaskValid(state); }

// These read-only image states share native residency; callers still synchronize data and queue ownership.
[[nodiscard]] constexpr bool AreTextureReadStatesCompatible(
    const ResourceStates::Mask lhs,
    const ResourceStates::Mask rhs
)noexcept{
    constexpr ResourceStates::Mask s_ReadStates = ResourceStates::ShaderResource | ResourceStates::DepthRead;
    return lhs != ResourceStates::Unknown && rhs != ResourceStates::Unknown && ((lhs | rhs) & ~s_ReadStates) == ResourceStates::Unknown;
}

[[nodiscard]] inline bool IsBufferDescriptionCompatibleWithResourceStates(const BufferDesc& description, const ResourceStates::Mask state)noexcept{ return VulkanBufferDetail::IsBufferDescriptionCompatibleWithResourceStates(description, state); }

// These checks preserve native readiness semantics; callers separately validate declarations when required.
[[nodiscard]] inline bool IsBufferReadyForState(const Device& device, Buffer* buffer, const ResourceStates::Mask state)noexcept{ return device.isBufferReadyForGpuUse(buffer, buffer ? VulkanBufferDetail::RequiredBufferUsageForResourceStates(buffer->getCreationDescription(), state) : 0u); }

[[nodiscard]] inline bool IsTextureReadyForState(const Device& device, Texture* texture, const ResourceStates::Mask state)noexcept{ return device.isTextureReadyForGpuUse(texture, VulkanTextureDetail::RequiredImageUsageForResourceStates(state)); }

[[nodiscard]] inline bool IsBufferRangeInBounds(const BufferDesc& description, const u64 offset, const u64 size)noexcept{ return VulkanDetail::IsBufferRangeInBounds(description, offset, size); }

[[nodiscard]] inline bool BufferRangesOverlap(const u64 firstOffset, const u64 firstSize, const u64 secondOffset, const u64 secondSize)noexcept{ return VulkanDetail::BufferRangesOverlap(firstOffset, firstSize, secondOffset, secondSize); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

