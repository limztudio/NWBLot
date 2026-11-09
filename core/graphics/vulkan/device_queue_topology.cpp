// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "device_detail.h"

#include <core/common/log.h>

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


QueueFamilySharingInfo ResolveQueueFamilySharing(const ResourceQueueSharing::Mask sharing, const VulkanContext& context)noexcept{
    QueueFamilySharingInfo result;
    const u8 sharingBits = static_cast<u8>(sharing);
    const auto appendFamily = [&result](const i32 familyIndex)noexcept{
        if(familyIndex == s_InvalidQueueFamilyIndex)
            return;
        for(u32 index = 0u; index < result.familyIndexCount; ++index){
            if(result.familyIndices[index] == static_cast<u32>(familyIndex))
                return;
        }
        FOUNDATION_ASSERT(result.familyIndexCount < result.familyIndices.size());
        result.familyIndices[result.familyIndexCount] = static_cast<u32>(familyIndex);
        ++result.familyIndexCount;
    };

    if(sharingBits & static_cast<u8>(ResourceQueueSharing::Graphics)){
        appendFamily(context.graphicsQueueFamilyIndex);
        appendFamily(context.auxiliaryGraphicsQueueFamilyIndex);
    }
    if((sharingBits & static_cast<u8>(ResourceQueueSharing::AsyncCompute)) && context.asyncComputeLaneEnabled){
        appendFamily(context.asyncComputeQueueFamilyIndex);
        appendFamily(context.auxiliaryAsyncComputeQueueFamilyIndex);
    }
    if((sharingBits & static_cast<u8>(ResourceQueueSharing::Transfer)) && context.transferQueueEnabled){
        appendFamily(context.transferQueueFamilyIndex);
        appendFamily(context.auxiliaryTransferQueueFamilyIndex);
    }

    if(result.familyIndexCount < s_MinConcurrentSharingQueueFamilyCount){
        result.familyIndexCount = 0u;
        return result;
    }

    result.mode = VK_SHARING_MODE_CONCURRENT;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u16 Device::getPhysicalQueueIndex(const CommandQueue::Enum queue)const noexcept{
    return getPrimaryPhysicalQueue(queue).index;
}

GpuPhysicalQueueId Device::getPrimaryPhysicalQueue(const CommandQueue::Enum queue)const noexcept{
    const u32 queueIndex = static_cast<u32>(queue);
    if(queueIndex >= static_cast<u32>(CommandQueue::kCount))
        return {};
    const Queue* const result = m_primaryQueues[queueIndex];
    return result ? result->m_physicalQueue : GpuPhysicalQueueId{};
}

GpuPhysicalQueueTopology Device::getPhysicalQueueTopology()const noexcept{
    return GpuPhysicalQueueTopology{ .queues = m_physicalQueueInfos.empty() ? nullptr : m_physicalQueueInfos.data(), .queueCount = m_physicalQueueInfos.size(), };
}

const GpuPhysicalQueueInfo* Device::getPhysicalQueueInfo(const GpuPhysicalQueueId& queue)const noexcept{
    if(!queue.valid() || queue.deviceGeneration != m_deviceGeneration || queue.index >= m_physicalQueueInfos.size())
        return nullptr;
    const GpuPhysicalQueueInfo& info = m_physicalQueueInfos[queue.index];
    return info.id == queue ? &info : nullptr;
}

Expected<GpuQueueTimelineSnapshot> Device::getQueueTimelineSnapshot(const GpuPhysicalQueueId& queue){
    if(isDeviceLost())
        return MakeUnexpected(Failure{});

    Queue* const physicalQueue = getQueue(queue);
    if(!physicalQueue)
        return MakeUnexpected(Failure{});

    VkResult result = VK_SUCCESS;
    GpuQueueTimelineSnapshot snapshot;
    {
        ScopedLock lock(physicalQueue->m_mutex);
        result = physicalQueue->updateLastFinishedID();
        snapshot = GpuQueueTimelineSnapshot{ .queue = queue, .submittedValue = physicalQueue->m_lastSubmittedID, .completedValue = physicalQueue->m_lastFinishedID, };
    }
    if(result == VK_ERROR_DEVICE_LOST)
        captureDeviceLoss("queue timeline snapshot");
    if(result != VK_SUCCESS){
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to snapshot queue timeline semaphore: {}"), ResultToString(result));
        return MakeUnexpected(Failure{});
    }
    return snapshot;
}

b8 Device::supportsComparableGpuTimestamps(const GpuPhysicalQueueId& queue)const noexcept{
    const GpuPhysicalQueueInfo* const queueInfo = getPhysicalQueueInfo(queue);
    return supportsComparableGpuTimestamps() && queueInfo && queueInfo->timestampValidBits == s_CompleteTimestampValidBits;
}

GpuCommandArenaStatistics Device::getCommandArenaStatistics(const GpuPhysicalQueueId& queue)const noexcept{
    if(!getPhysicalQueueInfo(queue))
        return {};
    FOUNDATION_ASSERT(queue.index < m_physicalQueues.size());
    const auto physicalQueue = m_physicalQueues[queue.index];

    return physicalQueue->commandArenaStatistics();
}

GpuCommandArenaWorkerStatistics Device::getCommandArenaWorkerStatistics(const GpuPhysicalQueueId& queue, const u64 recordingWorkerDomain, const u32 recordingWorkerIndex)const noexcept{
    if(!getPhysicalQueueInfo(queue))
        return {};
    FOUNDATION_ASSERT(queue.index < m_physicalQueues.size());
    const auto physicalQueue = m_physicalQueues[queue.index];

    return physicalQueue->commandArenaWorkerStatistics(recordingWorkerDomain, recordingWorkerIndex);
}

b8 Device::matchesPhysicalQueueIdentity(const CommandQueue::Enum queue, const u16 physicalQueueIndex, const u16 deviceGeneration)const noexcept{
    const GpuPhysicalQueueInfo* const info = getPhysicalQueueInfo(GpuPhysicalQueueId{ .index = physicalQueueIndex, .deviceGeneration = deviceGeneration });
    return info && info->queueClass == queue;
}

b8 Device::matchesPhysicalQueueIdentity(const GpuPhysicalQueueId& queue)const noexcept{
    return getPhysicalQueueInfo(queue) != nullptr;
}

Queue* Device::getQueue(const CommandQueue::Enum queueType)noexcept{
    const u32 index = static_cast<u32>(queueType);
    return index < static_cast<u32>(CommandQueue::kCount) ? m_primaryQueues[index] : nullptr;
}

Queue* Device::getQueue(const GpuPhysicalQueueId& queue)noexcept{
    if(!queue.valid() || queue.deviceGeneration != m_deviceGeneration || queue.index >= m_physicalQueues.size())
        return nullptr;
    const auto result = m_physicalQueues[queue.index];

    return result->m_physicalQueue == queue ? result.get() : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

