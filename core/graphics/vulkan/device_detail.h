// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_PipelineCacheVirtualPath = "vulkan/pipeline_cache.bin";
// Pipeline-cache validation rejects data from different GPUs or drivers.
inline constexpr AStringView s_PipelineCacheVolumeName = "runtime_pipeline_cache";

[[nodiscard]] inline constexpr GpuQueueCapability::Mask DeviceMinimumQueueCapabilities(
    const CommandQueue::Enum queue
)noexcept{
    switch(queue){
    case CommandQueue::Graphics:
        return static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        );
    case CommandQueue::Compute:
        return static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        );
    case CommandQueue::Transfer:
        return GpuQueueCapability::Transfer;
    default:
        return GpuQueueCapability::None;
    }
}

[[nodiscard]] inline constexpr GpuQueueCapability::Mask DeviceQueueCapabilitiesForQueueFlags(
    const VkQueueFlags queueFlags
)noexcept{
    u8 capabilities = 0u;
    if((queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u){
        capabilities |= static_cast<u8>(GpuQueueCapability::Graphics);
        capabilities |= static_cast<u8>(GpuQueueCapability::Transfer);
    }
    if((queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u){
        capabilities |= static_cast<u8>(GpuQueueCapability::Compute);
        capabilities |= static_cast<u8>(GpuQueueCapability::Transfer);
    }
    if((queueFlags & VK_QUEUE_TRANSFER_BIT) != 0u)
        capabilities |= static_cast<u8>(GpuQueueCapability::Transfer);
    return static_cast<GpuQueueCapability::Mask>(capabilities);
}

[[nodiscard]] inline constexpr bool SubmissionCommandListMatchesExecutionQueue(
    const CommandListParameters& commandList,
    const GpuPhysicalQueueId& executionQueue,
    const CommandQueue::Enum executionQueueClass
)noexcept{
    return
        executionQueue.valid()
        && commandList.physicalQueue.valid()
        && executionQueueClass < CommandQueue::kCount
        && commandList.queueType == executionQueueClass
        && commandList.physicalQueue == executionQueue
    ;
}

[[nodiscard]] inline const GpuPhysicalQueueInfo* SelectComputeCapableQueue(
    const GpuPhysicalQueueInfo* const queues,
    const usize queueCount,
    const GpuPhysicalQueueId& preferredQueue
)noexcept{
    if(!queues || queueCount == 0u)
        return nullptr;

    const auto supportsCompute = [](const GpuPhysicalQueueInfo& queue){
        return (static_cast<u8>(queue.capabilities) & static_cast<u8>(GpuQueueCapability::Compute)) != 0u;
    };
    for(usize queueIndex = 0u; queueIndex < queueCount; ++queueIndex){
        if(queues[queueIndex].id == preferredQueue && supportsCompute(queues[queueIndex]))
            return &queues[queueIndex];
    }
    for(usize queueIndex = 0u; queueIndex < queueCount; ++queueIndex){
        if(supportsCompute(queues[queueIndex]))
            return &queues[queueIndex];
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

