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


Queue* Device::getQueue(const CommandQueue::Enum queueType){
    const u32 index = static_cast<u32>(queueType);
    return index < static_cast<u32>(CommandQueue::kCount) ? m_primaryQueues[index] : nullptr;
}

Queue* Device::getQueue(const GpuPhysicalQueueId& queue){
    if(!queue.valid() || queue.deviceGeneration != m_deviceGeneration || queue.index >= m_physicalQueues.size())
        return nullptr;
    Queue* const result = m_physicalQueues[queue.index];
    return result && result->m_physicalQueue == queue ? result : nullptr;
}

GpuPhysicalQueueId Device::getPrimaryPhysicalQueue(const CommandQueue::Enum queue)const noexcept{
    const u32 queueIndex = static_cast<u32>(queue);
    if(queueIndex >= static_cast<u32>(CommandQueue::kCount))
        return {};
    const Queue* const result = m_primaryQueues[queueIndex];
    return result ? result->m_physicalQueue : GpuPhysicalQueueId{};
}

u16 Device::getPhysicalQueueIndex(const CommandQueue::Enum queue)const noexcept{
    return getPrimaryPhysicalQueue(queue).index;
}

GpuPhysicalQueueTopology Device::getPhysicalQueueTopology()const noexcept{
    return GpuPhysicalQueueTopology{
        .queues = m_physicalQueueInfos.empty() ? nullptr : m_physicalQueueInfos.data(),
        .queueCount = m_physicalQueueInfos.size(),
    };
}

const GpuPhysicalQueueInfo* Device::getPhysicalQueueInfo(const GpuPhysicalQueueId& queue)const noexcept{
    if(
        !queue.valid()
        || queue.deviceGeneration != m_deviceGeneration
        || queue.index >= m_physicalQueueInfos.size()
    )
        return nullptr;
    const GpuPhysicalQueueInfo& info = m_physicalQueueInfos[queue.index];
    return info.id == queue ? &info : nullptr;
}

GpuCommandArenaStatistics Device::getCommandArenaStatistics(const GpuPhysicalQueueId& queue)const noexcept{
    if(!getPhysicalQueueInfo(queue) || queue.index >= m_physicalQueues.size())
        return {};
    const Queue* const physicalQueue = m_physicalQueues[queue.index];
    return physicalQueue ? physicalQueue->commandArenaStatistics() : GpuCommandArenaStatistics{};
}

GpuCommandArenaWorkerStatistics Device::getCommandArenaWorkerStatistics(
    const GpuPhysicalQueueId& queue,
    const u64 recordingWorkerDomain,
    const u32 recordingWorkerIndex
)const noexcept{
    if(!getPhysicalQueueInfo(queue) || queue.index >= m_physicalQueues.size())
        return {};
    const Queue* const physicalQueue = m_physicalQueues[queue.index];
    return physicalQueue
        ? physicalQueue->commandArenaWorkerStatistics(recordingWorkerDomain, recordingWorkerIndex)
        : GpuCommandArenaWorkerStatistics{}
    ;
}

bool Device::matchesPhysicalQueueIdentity(
    const CommandQueue::Enum queue,
    const u16 physicalQueueIndex,
    const u16 deviceGeneration
)const noexcept{
    const GpuPhysicalQueueInfo* const info = getPhysicalQueueInfo(
        GpuPhysicalQueueId{ physicalQueueIndex, deviceGeneration }
    );
    return info && info->queueClass == queue;
}

bool Device::matchesPhysicalQueueIdentity(const GpuPhysicalQueueId& queue)const noexcept{
    return getPhysicalQueueInfo(queue) != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

