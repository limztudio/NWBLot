// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "texture_image_policy.h"
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

using VulkanBufferDetail::IsBufferResourceStateMaskValid;
using VulkanTextureDetail::IsTextureResourceStateMaskValid;
using VulkanBufferDetail::IsBufferDescriptionCompatibleWithResourceStates;
using VulkanDetail::IsBufferRangeInBounds;

// These read-only image states share native residency; callers still synchronize data and queue ownership.
[[nodiscard]] constexpr b8 AreTextureReadStatesCompatible(const ResourceStates::Mask lhs, const ResourceStates::Mask rhs)noexcept{
    constexpr ResourceStates::Mask s_ReadStates = ResourceStates::ShaderResource | ResourceStates::DepthRead;
    return lhs != ResourceStates::Unknown && rhs != ResourceStates::Unknown && ((lhs | rhs) & ~s_ReadStates) == ResourceStates::Unknown;
}

// These checks preserve native readiness semantics; callers separately validate declarations when required.
[[nodiscard]] inline b8 IsBufferReadyForState(const Device& device, Buffer* buffer, const ResourceStates::Mask state)noexcept{
    return device.isBufferReadyForGpuUse(buffer, buffer ? VulkanBufferDetail::RequiredBufferUsageForResourceStates(buffer->getCreationDescription(), state) : 0u);
}

[[nodiscard]] inline b8 IsTextureReadyForState(const Device& device, Texture* texture, const ResourceStates::Mask state)noexcept{ return device.isTextureReadyForGpuUse(texture, VulkanTextureDetail::TextureImagePolicy::requiredImageUsageForResourceStates(state)); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

