// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"
#include "native_resource_provenance.h"

#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanNativeSharingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared exclusive/concurrent queue-family validation for imported native handles. Callers pass the
// resource-specific log prefix (for example "buffer handle for native buffer") so buffer and texture
// diagnostics keep their exact wording while the ladder itself lives in one place.
template<typename UsageFlags, typename CreateFlags>
[[nodiscard]] inline bool ValidateNativeResourceSharing(
    const Device& device,
    const VolkInstanceTable& instanceDispatch,
    const VkPhysicalDevice physicalDevice,
    const ResourceQueueSharing::Mask queueSharing,
    const NativeResourceProvenance<UsageFlags, CreateFlags>& provenance,
    const char* const logPrefix
){
    if(provenance.sharingMode == VK_SHARING_MODE_EXCLUSIVE){
        if(provenance.queueFamilyIndexCount != 0u || provenance.queueFamilyIndices != nullptr){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: exclusive sharing must not carry queue-family indices"), logPrefix);
            return false;
        }
        if(device.usesConcurrentQueueSharing(queueSharing)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: exclusive native sharing contradicts concurrent logical sharing"), logPrefix);
            return false;
        }
        return true;
    }
    if(provenance.sharingMode != VK_SHARING_MODE_CONCURRENT){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: native sharing mode is invalid"), logPrefix);
        return false;
    }
    if(provenance.queueFamilyIndexCount < 2u || !provenance.queueFamilyIndices){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: concurrent sharing requires at least two queue-family indices"), logPrefix);
        return false;
    }
    if(queueSharing == ResourceQueueSharing::Exclusive){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: concurrent native sharing requires explicit logical queue classes"), logPrefix);
        return false;
    }

    u32 physicalQueueFamilyCount = 0u;
    instanceDispatch.vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &physicalQueueFamilyCount, nullptr);
    if(
        physicalQueueFamilyCount == 0u
        || provenance.queueFamilyIndexCount > physicalQueueFamilyCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: native queue-family count is invalid"), logPrefix);
        return false;
    }
    for(u32 familyIndex = 0u; familyIndex < provenance.queueFamilyIndexCount; ++familyIndex){
        const u32 nativeFamilyIndex = provenance.queueFamilyIndices[familyIndex];
        if(nativeFamilyIndex >= physicalQueueFamilyCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: native queue-family index is out of range"), logPrefix);
            return false;
        }
        for(u32 earlierIndex = 0u; earlierIndex < familyIndex; ++earlierIndex){
            if(provenance.queueFamilyIndices[earlierIndex] == nativeFamilyIndex){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: native queue-family indices are not unique"), logPrefix);
                return false;
            }
        }
    }

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    bool hasLogicalQueue = false;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(!ResourceQueueSharing::IncludesQueueClass(queueSharing, queue.queueClass))
            continue;
        hasLogicalQueue = true;
        if(!ResourceQueueSharing::QueueFamilyIndexListContains(
            provenance.queueFamilyIndices,
            provenance.queueFamilyIndexCount,
            queue.familyIndex
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: native sharing omits a logically admitted queue family"), logPrefix);
            return false;
        }
    }
    if(!hasLogicalQueue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: logical sharing admits no device queue"), logPrefix);
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
