// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"

#include <global/atomic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_PipelineCacheVirtualPath = "vulkan/pipeline_cache.bin";
// Pipeline-cache validation rejects data from different GPUs or drivers.
inline constexpr AStringView s_PipelineCacheVolumeName = "runtime_pipeline_cache";
inline constexpr usize s_PipelineCacheHeaderVersionOneSize = 32u;

namespace PipelineCacheDataValidation{
    enum Enum : u8{
        Usable,
        Malformed,
        Incompatible,
    };
};

[[nodiscard]] PipelineCacheDataValidation::Enum ValidatePipelineCacheData(
    BinaryByteView cacheData,
    const VkPhysicalDeviceProperties& properties
)noexcept;


class DeviceGenerationAllocator final : NoCopy{
public:
    [[nodiscard]] u16 allocate()noexcept{
        u32 generation = m_nextGeneration.load(MemoryOrder::relaxed);
        while(generation != 0u && generation <= static_cast<u32>(Limit<u16>::s_Max)){
            if(m_nextGeneration.compare_exchange_weak(
                generation,
                generation + 1u,
                MemoryOrder::relaxed,
                MemoryOrder::relaxed
            ))
                return static_cast<u16>(generation);
        }
        return 0u;
    }


private:
    Atomic<u32> m_nextGeneration{ 1u };
};

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

