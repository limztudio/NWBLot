// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "queue_command_arena_statistics.h"
#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandArenaStatistics Queue::CommandArenaStatistics::collect()const noexcept{
    const u64 directCommandBufferCount = m_queue.m_directCommandBufferCount.load(MemoryOrder::relaxed);
    const u64 explicitWorkerArenaCount = m_queue.m_explicitWorkerArenaCount.load(MemoryOrder::relaxed);
    const u64 currentCommandBufferCount = m_queue.m_currentCommandBufferCount.load(MemoryOrder::relaxed);
    const u64 commandPoolEpochCount = directCommandBufferCount + explicitWorkerArenaCount;

    return GpuCommandArenaStatistics{
        .workerArenaCount = explicitWorkerArenaCount + (directCommandBufferCount > 0u ? 1u : 0u),
        .commandPoolEpochCount = commandPoolEpochCount,
        .pendingCommandPoolEpochCount =
            m_queue.m_pendingDirectCommandBufferCount.load(MemoryOrder::relaxed)
            + m_queue.m_pendingWorkerEpochCount.load(MemoryOrder::relaxed),
        .currentCommandBufferCount = currentCommandBufferCount,
        .highWaterCommandBufferCount = m_queue.m_highWaterCommandBufferCount.load(MemoryOrder::relaxed),
        .reusableCommandBufferCount = m_queue.m_reusableCommandBufferCount.load(MemoryOrder::relaxed),
        .leasedCommandBufferCount = m_queue.m_leasedCommandBufferCount.load(MemoryOrder::relaxed),
        .pendingCommandBufferCount = m_queue.m_pendingCommandBufferCount.load(MemoryOrder::relaxed),
        .growthEventCount = m_queue.m_commandBufferGrowthEventCount.load(MemoryOrder::relaxed),
        .resetEventCount = m_queue.m_commandBufferResetEventCount.load(MemoryOrder::relaxed),
        .nativeHandleStorageLowerBoundBytes = commandPoolEpochCount * sizeof(VkCommandPool) + currentCommandBufferCount * sizeof(VkCommandBuffer),
        .queue = m_queue.m_physicalQueue,
    };
}

GpuCommandArenaWorkerStatistics Queue::CommandArenaStatistics::collectWorker(const u64 recordingWorkerDomain, const u32 recordingWorkerIndex)const noexcept{
    if(recordingWorkerIndex == 0u){
        if(recordingWorkerDomain != 0u)
            return {};

        const u64 currentCommandBufferCount = m_queue.m_directCommandBufferCount.load(MemoryOrder::relaxed);
        const u64 pendingCommandBufferCount = m_queue.m_pendingDirectCommandBufferCount.load(MemoryOrder::relaxed);
        return GpuCommandArenaWorkerStatistics{
            .recordingWorkerDomain = 0u,
            .commandPoolEpochCount = currentCommandBufferCount,
            .pendingCommandPoolEpochCount = pendingCommandBufferCount,
            .currentCommandBufferCount = currentCommandBufferCount,
            .highWaterCommandBufferCount = m_queue.m_directHighWaterCommandBufferCount.load(MemoryOrder::relaxed),
            .reusableCommandBufferCount = m_queue.m_directReusableCommandBufferCount.load(MemoryOrder::relaxed),
            .leasedCommandBufferCount = m_queue.m_directLeasedCommandBufferCount.load(MemoryOrder::relaxed),
            .pendingCommandBufferCount = pendingCommandBufferCount,
            .growthEventCount = m_queue.m_directCommandBufferGrowthEventCount.load(MemoryOrder::relaxed),
            .resetEventCount = m_queue.m_directCommandBufferResetEventCount.load(MemoryOrder::relaxed),
            .nativeHandleStorageLowerBoundBytes = currentCommandBufferCount * (sizeof(VkCommandPool) + sizeof(VkCommandBuffer)),
            .queue = m_queue.m_physicalQueue,
            .recordingWorkerIndex = 0u,
        };
    }

    const WorkerCommandArena* const arena = m_queue.findWorkerCommandArena(recordingWorkerDomain, recordingWorkerIndex);
    if(!arena)
        return {};
    const u64 currentCommandBufferCount = arena->currentCommandBufferCount.load(MemoryOrder::relaxed);
    const u64 pendingCommandBufferCount = arena->pendingCommandBufferCount.load(MemoryOrder::relaxed);
    return GpuCommandArenaWorkerStatistics{
        .recordingWorkerDomain = recordingWorkerDomain,
        .commandPoolEpochCount = 1u,
        .pendingCommandPoolEpochCount = pendingCommandBufferCount > 0u ? 1u : 0u,
        .currentCommandBufferCount = currentCommandBufferCount,
        .highWaterCommandBufferCount = arena->highWaterCommandBufferCount.load(MemoryOrder::relaxed),
        .reusableCommandBufferCount = arena->reusableCommandBufferCount.load(MemoryOrder::relaxed),
        .leasedCommandBufferCount = arena->leasedCommandBufferCount.load(MemoryOrder::relaxed),
        .pendingCommandBufferCount = pendingCommandBufferCount,
        .growthEventCount = arena->growthEventCount.load(MemoryOrder::relaxed),
        .resetEventCount = arena->resetEventCount.load(MemoryOrder::relaxed),
        .nativeHandleStorageLowerBoundBytes = sizeof(VkCommandPool) + currentCommandBufferCount * sizeof(VkCommandBuffer),
        .queue = m_queue.m_physicalQueue,
        .recordingWorkerIndex = recordingWorkerIndex,
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

