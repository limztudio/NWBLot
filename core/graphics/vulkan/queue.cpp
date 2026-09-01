// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Queue::Queue(
    const VulkanContext& context,
    Device& device,
    const GpuPhysicalQueueInfo& info,
    NativeQueueState& nativeQueue
)
    : m_context(context)
    , m_device(device)
    , m_nativeQueue(nativeQueue)
    , m_queueID(info.queueClass)
    , m_physicalQueue(info.id)
    , m_queueFamilyIndex(info.familyIndex)
    , m_waitSemaphores(context.objectArena)
    , m_waitSemaphoreValues(context.objectArena)
    , m_signalSemaphores(context.objectArena)
    , m_signalSemaphoreValues(context.objectArena)
    , m_lastRecordingID(0u)
    , m_lastSubmittedID(0)
    , m_lastFinishedID(0)
    , m_commandBuffersInFlight(context.objectArena)
    , m_commandBuffersPool(context.objectArena)
    , m_workerCommandArenas(context.objectArena)
{
    NWB_ASSERT(m_nativeQueue.familyIndex == info.familyIndex && m_nativeQueue.queueIndex == info.queueIndex);
    auto timelineInfo = VulkanDetail::MakeVkStruct<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;

    auto semaphoreInfo = VulkanDetail::MakeVkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    semaphoreInfo.pNext = &timelineInfo;

    const VkResult res = m_context.deviceDispatch.vkCreateSemaphore(m_context.device, &semaphoreInfo, m_context.allocationCallbacks, &m_trackingSemaphore);
    if(res != VK_SUCCESS){
        m_trackingSemaphore = VK_NULL_HANDLE;
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create queue timeline semaphore: {}"), ResultToString(res));
    }
}
Queue::~Queue(){
    if(m_trackingSemaphore && m_lastSubmittedID > 0){
        auto waitInfo = VulkanDetail::MakeVkStruct<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &m_trackingSemaphore;
        waitInfo.pValues = &m_lastSubmittedID;

        const VkResult res = m_context.deviceDispatch.vkWaitSemaphores(m_context.device, &waitInfo, UINT64_MAX);
        if(res != VK_SUCCESS)
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to wait on queue timeline semaphore during teardown: {}"), ResultToString(res));
    }

    m_commandBuffersInFlight.clear();
    m_commandBuffersPool.clear();
    destroyWorkerCommandArenas();

    if(m_trackingSemaphore){
        m_context.deviceDispatch.vkDestroySemaphore(m_context.device, m_trackingSemaphore, m_context.allocationCallbacks);
        m_trackingSemaphore = VK_NULL_HANDLE;
    }
}

GpuCommandArenaStatistics Queue::commandArenaStatistics()const noexcept{
    const u64 directCommandBufferCount = m_directCommandBufferCount.load(MemoryOrder::relaxed);
    const u64 explicitWorkerArenaCount = m_explicitWorkerArenaCount.load(MemoryOrder::relaxed);
    const u64 currentCommandBufferCount = m_currentCommandBufferCount.load(MemoryOrder::relaxed);
    const u64 commandPoolEpochCount = directCommandBufferCount + explicitWorkerArenaCount;

    return GpuCommandArenaStatistics{
        .queue = m_physicalQueue,
        .workerArenaCount = explicitWorkerArenaCount + (directCommandBufferCount > 0u ? 1u : 0u),
        .commandPoolEpochCount = commandPoolEpochCount,
        .pendingCommandPoolEpochCount =
            m_pendingDirectCommandBufferCount.load(MemoryOrder::relaxed)
            + m_pendingWorkerEpochCount.load(MemoryOrder::relaxed),
        .currentCommandBufferCount = currentCommandBufferCount,
        .highWaterCommandBufferCount = m_highWaterCommandBufferCount.load(MemoryOrder::relaxed),
        .reusableCommandBufferCount = m_reusableCommandBufferCount.load(MemoryOrder::relaxed),
        .leasedCommandBufferCount = m_leasedCommandBufferCount.load(MemoryOrder::relaxed),
        .pendingCommandBufferCount = m_pendingCommandBufferCount.load(MemoryOrder::relaxed),
        .growthEventCount = m_commandBufferGrowthEventCount.load(MemoryOrder::relaxed),
        .resetEventCount = m_commandBufferResetEventCount.load(MemoryOrder::relaxed),
        .nativeHandleStorageLowerBoundBytes =
            commandPoolEpochCount * sizeof(VkCommandPool)
            + currentCommandBufferCount * sizeof(VkCommandBuffer),
    };
}

GpuCommandArenaWorkerStatistics Queue::commandArenaWorkerStatistics(
    const u64 recordingWorkerDomain,
    const u32 recordingWorkerIndex
)const noexcept{
    if(recordingWorkerIndex == 0u){
        if(recordingWorkerDomain != 0u)
            return {};

        const u64 currentCommandBufferCount = m_directCommandBufferCount.load(MemoryOrder::relaxed);
        const u64 pendingCommandBufferCount = m_pendingDirectCommandBufferCount.load(MemoryOrder::relaxed);
        return GpuCommandArenaWorkerStatistics{
            .queue = m_physicalQueue,
            .recordingWorkerDomain = 0u,
            .recordingWorkerIndex = 0u,
            .commandPoolEpochCount = currentCommandBufferCount,
            .pendingCommandPoolEpochCount = pendingCommandBufferCount,
            .currentCommandBufferCount = currentCommandBufferCount,
            .highWaterCommandBufferCount = m_directHighWaterCommandBufferCount.load(MemoryOrder::relaxed),
            .reusableCommandBufferCount = m_directReusableCommandBufferCount.load(MemoryOrder::relaxed),
            .leasedCommandBufferCount = m_directLeasedCommandBufferCount.load(MemoryOrder::relaxed),
            .pendingCommandBufferCount = pendingCommandBufferCount,
            .growthEventCount = m_directCommandBufferGrowthEventCount.load(MemoryOrder::relaxed),
            .resetEventCount = m_directCommandBufferResetEventCount.load(MemoryOrder::relaxed),
            .nativeHandleStorageLowerBoundBytes =
                currentCommandBufferCount * (sizeof(VkCommandPool) + sizeof(VkCommandBuffer)),
        };
    }

    const WorkerCommandArena* const arena = findWorkerCommandArena(recordingWorkerDomain, recordingWorkerIndex);
    if(!arena)
        return {};
    const u64 currentCommandBufferCount = arena->currentCommandBufferCount.load(MemoryOrder::relaxed);
    const u64 pendingCommandBufferCount = arena->pendingCommandBufferCount.load(MemoryOrder::relaxed);
    return GpuCommandArenaWorkerStatistics{
        .queue = m_physicalQueue,
        .recordingWorkerDomain = recordingWorkerDomain,
        .recordingWorkerIndex = recordingWorkerIndex,
        .commandPoolEpochCount = 1u,
        .pendingCommandPoolEpochCount = pendingCommandBufferCount > 0u ? 1u : 0u,
        .currentCommandBufferCount = currentCommandBufferCount,
        .highWaterCommandBufferCount = arena->highWaterCommandBufferCount.load(MemoryOrder::relaxed),
        .reusableCommandBufferCount = arena->reusableCommandBufferCount.load(MemoryOrder::relaxed),
        .leasedCommandBufferCount = arena->leasedCommandBufferCount.load(MemoryOrder::relaxed),
        .pendingCommandBufferCount = pendingCommandBufferCount,
        .growthEventCount = arena->growthEventCount.load(MemoryOrder::relaxed),
        .resetEventCount = arena->resetEventCount.load(MemoryOrder::relaxed),
        .nativeHandleStorageLowerBoundBytes =
            sizeof(VkCommandPool) + currentCommandBufferCount * sizeof(VkCommandBuffer),
    };
}

void Queue::updateCommandBufferHighWater(Atomic<u64>& highWaterCount, const u64 currentCount)noexcept{
    u64 highWater = highWaterCount.load(MemoryOrder::relaxed);
    while(highWater < currentCount){
        if(highWaterCount.compare_exchange_weak(highWater, currentCount, MemoryOrder::relaxed))
            return;
    }
}

u64 Queue::nextRecordingID()noexcept{
    u64 recordingID = m_lastRecordingID.fetch_add(1u, MemoryOrder::relaxed) + 1u;
    while(recordingID == 0u)
        recordingID = m_lastRecordingID.fetch_add(1u, MemoryOrder::relaxed) + 1u;
    return recordingID;
}

void Queue::registerCommandBuffer(TrackedCommandBuffer& commandBuffer)noexcept{
    if(&commandBuffer.m_queue != this || &commandBuffer.m_context != &m_context){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Cannot register a command buffer with a foreign queue or context"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Command buffer registration owner mismatch"));
        return;
    }
    NWB_ASSERT(commandBuffer.m_arenaState == TrackedCommandBufferArenaState::Untracked);
    commandBuffer.m_arenaState = TrackedCommandBufferArenaState::Leased;
    const u64 currentCount = m_currentCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed) + 1u;
    m_leasedCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
    m_commandBufferGrowthEventCount.fetch_add(1u, MemoryOrder::relaxed);
    if(commandBuffer.m_recordingWorkerIndex == 0u){
        const u64 directCurrentCount = m_directCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed) + 1u;
        m_directLeasedCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        m_directCommandBufferGrowthEventCount.fetch_add(1u, MemoryOrder::relaxed);
        updateCommandBufferHighWater(m_directHighWaterCommandBufferCount, directCurrentCount);
    }
    else{
        WorkerCommandArena* const arena = findWorkerCommandArena(
            commandBuffer.m_recordingWorkerDomain,
            commandBuffer.m_recordingWorkerIndex
        );
        NWB_ASSERT(arena);
        if(arena){
            const u64 workerCurrentCount = arena->currentCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed) + 1u;
            arena->leasedCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
            arena->growthEventCount.fetch_add(1u, MemoryOrder::relaxed);
            updateCommandBufferHighWater(arena->highWaterCommandBufferCount, workerCurrentCount);
        }
    }
    updateCommandBufferHighWater(m_highWaterCommandBufferCount, currentCount);
}

void Queue::transitionCommandBufferState(
    TrackedCommandBuffer& commandBuffer,
    const TrackedCommandBufferArenaState::Enum nextState
)noexcept{
    if(&commandBuffer.m_queue != this || &commandBuffer.m_context != &m_context){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Cannot transition a command buffer through a foreign queue"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Command buffer arena transition owner mismatch"));
        return;
    }
    const TrackedCommandBufferArenaState::Enum previousState = commandBuffer.m_arenaState;
    if(previousState == nextState)
        return;
    WorkerCommandArena* const workerArena = commandBuffer.m_recordingWorkerIndex == 0u
        ? nullptr
        : findWorkerCommandArena(commandBuffer.m_recordingWorkerDomain, commandBuffer.m_recordingWorkerIndex)
    ;
    NWB_ASSERT(commandBuffer.m_recordingWorkerIndex == 0u || workerArena);

    switch(previousState){
    case TrackedCommandBufferArenaState::Leased:{
        const u64 previousLeasedCount = m_leasedCommandBufferCount.fetch_sub(1u, MemoryOrder::relaxed);
        if(previousLeasedCount == 0u)
            NWB_ASSERT_MSG(false, NWB_TEXT("Command arena leased-buffer count underflow"));
        if(commandBuffer.m_recordingWorkerIndex == 0u){
            const u64 previousDirectLeasedCount = m_directLeasedCommandBufferCount.fetch_sub(1u, MemoryOrder::relaxed);
            if(previousDirectLeasedCount == 0u)
                NWB_ASSERT_MSG(false, NWB_TEXT("Direct command arena leased-buffer count underflow"));
        }
        else if(workerArena){
            const u64 previousWorkerLeasedCount = workerArena->leasedCommandBufferCount.fetch_sub(1u, MemoryOrder::relaxed);
            if(previousWorkerLeasedCount == 0u)
                NWB_ASSERT_MSG(false, NWB_TEXT("Worker command arena leased-buffer count underflow"));
        }
        break;
    }
    case TrackedCommandBufferArenaState::Reusable:{
        const u64 previousReusableCount = m_reusableCommandBufferCount.fetch_sub(1u, MemoryOrder::relaxed);
        if(previousReusableCount == 0u)
            NWB_ASSERT_MSG(false, NWB_TEXT("Command arena reusable-buffer count underflow"));
        if(commandBuffer.m_recordingWorkerIndex == 0u){
            const u64 previousDirectReusableCount = m_directReusableCommandBufferCount.fetch_sub(1u, MemoryOrder::relaxed);
            if(previousDirectReusableCount == 0u)
                NWB_ASSERT_MSG(false, NWB_TEXT("Direct command arena reusable-buffer count underflow"));
        }
        else if(workerArena){
            const u64 previousWorkerReusableCount = workerArena->reusableCommandBufferCount.fetch_sub(1u, MemoryOrder::relaxed);
            if(previousWorkerReusableCount == 0u)
                NWB_ASSERT_MSG(false, NWB_TEXT("Worker command arena reusable-buffer count underflow"));
        }
        break;
    }
    case TrackedCommandBufferArenaState::Pending:{
        const u64 previousPendingCommandBufferCount = m_pendingCommandBufferCount.fetch_sub(1u, MemoryOrder::relaxed);
        if(previousPendingCommandBufferCount == 0u)
            NWB_ASSERT_MSG(false, NWB_TEXT("Command arena pending-buffer count underflow"));
        if(commandBuffer.m_recordingWorkerIndex == 0u){
            const u64 previousDirectPendingCount = m_pendingDirectCommandBufferCount.fetch_sub(1u, MemoryOrder::relaxed);
            if(previousDirectPendingCount == 0u)
                NWB_ASSERT_MSG(false, NWB_TEXT("Command arena direct pending-buffer count underflow"));
        }
        else{
            if(workerArena){
                const u64 previousPendingCount = workerArena->pendingCommandBufferCount.fetch_sub(1u, MemoryOrder::relaxed);
                NWB_ASSERT(previousPendingCount > 0u);
                if(previousPendingCount == 1u){
                    const u64 previousPendingEpochCount = m_pendingWorkerEpochCount.fetch_sub(1u, MemoryOrder::relaxed);
                    if(previousPendingEpochCount == 0u)
                        NWB_ASSERT_MSG(false, NWB_TEXT("Command arena pending worker-epoch count underflow"));
                }
            }
        }
        break;
    }
    default:
        break;
    }

    switch(nextState){
    case TrackedCommandBufferArenaState::Leased:
        m_leasedCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        if(commandBuffer.m_recordingWorkerIndex == 0u)
            m_directLeasedCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        else if(workerArena)
            workerArena->leasedCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        break;
    case TrackedCommandBufferArenaState::Reusable:
        m_reusableCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        if(commandBuffer.m_recordingWorkerIndex == 0u)
            m_directReusableCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        else if(workerArena)
            workerArena->reusableCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        break;
    case TrackedCommandBufferArenaState::Pending:
        m_pendingCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        if(commandBuffer.m_recordingWorkerIndex == 0u)
            m_pendingDirectCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        else{
            if(workerArena && workerArena->pendingCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed) == 0u)
                m_pendingWorkerEpochCount.fetch_add(1u, MemoryOrder::relaxed);
        }
        break;
    default:
        break;
    }
    commandBuffer.m_arenaState = nextState;
}

void Queue::unregisterCommandBuffer(TrackedCommandBuffer& commandBuffer)noexcept{
    if(commandBuffer.m_arenaState == TrackedCommandBufferArenaState::Untracked)
        return;

    transitionCommandBufferState(commandBuffer, TrackedCommandBufferArenaState::Untracked);
    const u64 previousCurrentCount = m_currentCommandBufferCount.fetch_sub(1u, MemoryOrder::relaxed);
    if(previousCurrentCount == 0u)
        NWB_ASSERT_MSG(false, NWB_TEXT("Command arena current-buffer count underflow"));
    if(commandBuffer.m_recordingWorkerIndex == 0u){
        const u64 previousDirectCount = m_directCommandBufferCount.fetch_sub(1u, MemoryOrder::relaxed);
        if(previousDirectCount == 0u)
            NWB_ASSERT_MSG(false, NWB_TEXT("Command arena direct-buffer count underflow"));
    }
    else{
        WorkerCommandArena* const arena = findWorkerCommandArena(
            commandBuffer.m_recordingWorkerDomain,
            commandBuffer.m_recordingWorkerIndex
        );
        if(arena){
            const u64 previousWorkerCount = arena->currentCommandBufferCount.fetch_sub(1u, MemoryOrder::relaxed);
            if(previousWorkerCount == 0u)
                NWB_ASSERT_MSG(false, NWB_TEXT("Worker command arena current-buffer count underflow"));
        }
    }
}

TrackedCommandBufferPtr Queue::createCommandBuffer(
    const VkCommandPool commandPool,
    Futex* const sharedCommandPoolMutex,
    const u64 recordingWorkerDomain,
    const u32 recordingWorkerIndex
){
    const bool ownsCommandPool = recordingWorkerIndex == 0u;
    if(!ownsCommandPool && commandPool == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Cannot create a worker command buffer without a command pool for physical queue {} worker {}"),
            m_physicalQueue.index,
            recordingWorkerIndex
        );
        return nullptr;
    }

    auto* cmdBuf = NewArenaObject<TrackedCommandBuffer>(
        m_context.objectArena,
        *this,
        m_context,
        m_queueFamilyIndex,
        commandPool,
        ownsCommandPool,
        sharedCommandPoolMutex
    );
    if(!cmdBuf){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to allocate command-buffer tracking storage for physical queue {} worker {}"),
            m_physicalQueue.index,
            recordingWorkerIndex
        );
        return nullptr;
    }
    if(!cmdBuf->m_cmdBuf){
        DestroyArenaObject(m_context.objectArena, cmdBuf);
        return nullptr;
    }

    cmdBuf->m_recordingID = nextRecordingID();
    cmdBuf->m_recordingWorkerDomain = recordingWorkerDomain;
    cmdBuf->m_recordingWorkerIndex = recordingWorkerIndex;
    registerCommandBuffer(*cmdBuf);
    return TrackedCommandBufferPtr(cmdBuf, TrackedCommandBufferPtr::deleter_type(&m_context.objectArena), AdoptRef);
}


Queue::WorkerCommandArena* Queue::findWorkerCommandArena(
    const u64 recordingWorkerDomain,
    const u32 recordingWorkerIndex
)const{
    NWB_ASSERT(recordingWorkerIndex != 0u);
    if(recordingWorkerIndex == 0u)
        return nullptr;

    for(
        WorkerCommandArena* arena = m_workerCommandArenaHead.load(MemoryOrder::acquire);
        arena;
        arena = arena->next.load(MemoryOrder::acquire)
    ){
        if(
            arena
            && arena->recordingWorkerDomain == recordingWorkerDomain
            && arena->recordingWorkerIndex == recordingWorkerIndex
        )
            return arena;
    }
    return nullptr;
}


Queue::WorkerCommandArena* Queue::getOrCreateWorkerCommandArena(
    const u64 recordingWorkerDomain,
    const u32 recordingWorkerIndex
){
    NWB_ASSERT(recordingWorkerIndex != 0u);
    if(recordingWorkerIndex == 0u)
        return nullptr;

    if(WorkerCommandArena* const arena = findWorkerCommandArena(recordingWorkerDomain, recordingWorkerIndex))
        return arena;

    ScopedLock lock(m_workerCommandArenasMutex);
    if(WorkerCommandArena* const arena = findWorkerCommandArena(recordingWorkerDomain, recordingWorkerIndex))
        return arena;

    auto* const arena = NewArenaObject<WorkerCommandArena>(
        m_context.objectArena,
        m_context.objectArena,
        recordingWorkerDomain,
        recordingWorkerIndex
    );
    if(!arena){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to allocate command-arena storage for physical queue {} worker {}:{}"),
            m_physicalQueue.index,
            recordingWorkerDomain,
            recordingWorkerIndex
        );
        return nullptr;
    }

    auto poolInfo = VulkanDetail::MakeVkStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    poolInfo.queueFamilyIndex = m_queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    const VkResult createResult = m_context.deviceDispatch.vkCreateCommandPool(
        m_context.device,
        &poolInfo,
        m_context.allocationCallbacks,
        &arena->commandPool
    );
    if(createResult != VK_SUCCESS){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create command pool for physical queue {} worker {}:{}: {}"),
            m_physicalQueue.index,
            recordingWorkerDomain,
            recordingWorkerIndex,
            ResultToString(createResult)
        );
        DestroyArenaObject(m_context.objectArena, arena);
        return nullptr;
    }

    m_workerCommandArenas.push_back(arena);
    arena->next.store(m_workerCommandArenaHead.load(MemoryOrder::relaxed), MemoryOrder::relaxed);
    m_workerCommandArenaHead.store(arena, MemoryOrder::release);
    m_explicitWorkerArenaCount.fetch_add(1u, MemoryOrder::relaxed);
    return arena;
}


TrackedCommandBufferPtr Queue::getOrCreateDirectCommandBuffer(){
    ScopedLock lock(m_mutex);

    updateLastFinishedID();
    collectCompletedCommandBuffers();

    auto available = m_commandBuffersPool.end();
    for(auto it = m_commandBuffersPool.begin(); it != m_commandBuffersPool.end(); ++it){
        if(*it && (*it)->m_recordingWorkerIndex == 0u){
            available = it;
            break;
        }
    }
    if(available != m_commandBuffersPool.end()){
        TrackedCommandBufferPtr cmdBuf = Move(*available);
        m_commandBuffersPool.erase(available);

        if(!cmdBuf || cmdBuf->m_cmdBuf == VK_NULL_HANDLE)
            return createCommandBuffer(VK_NULL_HANDLE, nullptr, 0u, 0u);

        const VkResult res = m_context.deviceDispatch.vkResetCommandBuffer(cmdBuf->m_cmdBuf, 0);
        if(res != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to reset command buffer, creating a new one: {}"), ResultToString(res));
            return createCommandBuffer(VK_NULL_HANDLE, nullptr, 0u, 0u);
        }

        transitionCommandBufferState(*cmdBuf, TrackedCommandBufferArenaState::Leased);
        m_commandBufferResetEventCount.fetch_add(1u, MemoryOrder::relaxed);
        m_directCommandBufferResetEventCount.fetch_add(1u, MemoryOrder::relaxed);
        cmdBuf->m_recordingID = nextRecordingID();
        cmdBuf->m_recordingWorkerDomain = 0u;
        cmdBuf->m_recordingWorkerIndex = 0u;

        return cmdBuf;
    }

    return createCommandBuffer(VK_NULL_HANDLE, nullptr, 0u, 0u);
}


TrackedCommandBufferPtr Queue::getOrCreateWorkerCommandBuffer(
    const u64 recordingWorkerDomain,
    const u32 recordingWorkerIndex
){
    WorkerCommandArena* const arena = getOrCreateWorkerCommandArena(recordingWorkerDomain, recordingWorkerIndex);
    if(!arena)
        return nullptr;

    // Each explicit worker owns this native pool shard. Do not take m_mutex here: submission/timeline retirement
    // remains serialized there, while allocation and reset stay independent for ready-frontier recording workers.
    ScopedLock lock(arena->mutex);

    if(!arena->commandBuffersPool.empty()){
        auto available = arena->commandBuffersPool.begin();
        TrackedCommandBufferPtr cmdBuf = Move(*available);
        arena->commandBuffersPool.erase(available);

        if(!cmdBuf || cmdBuf->m_cmdBuf == VK_NULL_HANDLE)
            return createCommandBuffer(arena->commandPool, &arena->mutex, recordingWorkerDomain, recordingWorkerIndex);

        const VkResult res = m_context.deviceDispatch.vkResetCommandBuffer(cmdBuf->m_cmdBuf, 0);
        if(res != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to reset worker command buffer, creating a new one: {}"), ResultToString(res));
            m_context.deviceDispatch.vkFreeCommandBuffers(m_context.device, arena->commandPool, 1u, &cmdBuf->m_cmdBuf);
            cmdBuf->m_cmdBuf = VK_NULL_HANDLE;
            cmdBuf.reset();
            return createCommandBuffer(arena->commandPool, &arena->mutex, recordingWorkerDomain, recordingWorkerIndex);
        }

        transitionCommandBufferState(*cmdBuf, TrackedCommandBufferArenaState::Leased);
        m_commandBufferResetEventCount.fetch_add(1u, MemoryOrder::relaxed);
        arena->resetEventCount.fetch_add(1u, MemoryOrder::relaxed);
        cmdBuf->m_recordingID = nextRecordingID();
        cmdBuf->m_recordingWorkerDomain = recordingWorkerDomain;
        cmdBuf->m_recordingWorkerIndex = recordingWorkerIndex;
        return cmdBuf;
    }

    // Queue timeline polling/reclamation occurs in Device::runGarbageCollection(). An empty worker shard grows
    // instead of taking the queue submission lock and serializing parallel ready-frontier recording.
    return createCommandBuffer(arena->commandPool, &arena->mutex, recordingWorkerDomain, recordingWorkerIndex);
}


TrackedCommandBufferPtr Queue::getOrCreateCommandBuffer(
    const u64 recordingWorkerDomain,
    const u32 recordingWorkerIndex
){
    if(recordingWorkerIndex == 0u)
        return getOrCreateDirectCommandBuffer();

    return getOrCreateWorkerCommandBuffer(recordingWorkerDomain, recordingWorkerIndex);
}


void Queue::destroyWorkerCommandArenas(){
    ScopedLock workerArenasLock(m_workerCommandArenasMutex);
    for(WorkerCommandArena* const arena : m_workerCommandArenas){
        if(!arena)
            continue;

        {
            ScopedLock arenaLock(arena->mutex);
            for(TrackedCommandBufferPtr& commandBuffer : arena->commandBuffersPool){
                if(!commandBuffer)
                    continue;
                commandBuffer->m_cmdBuf = VK_NULL_HANDLE;
                commandBuffer->m_cmdPool = VK_NULL_HANDLE;
                commandBuffer->m_sharedCommandPoolMutex = nullptr;
            }
            arena->commandBuffersPool.clear();
            if(arena->commandPool){
                m_context.deviceDispatch.vkDestroyCommandPool(m_context.device, arena->commandPool, m_context.allocationCallbacks);
                arena->commandPool = VK_NULL_HANDLE;
            }
        }
    }

    m_workerCommandArenaHead.store(nullptr, MemoryOrder::release);
    for(WorkerCommandArena* const arena : m_workerCommandArenas){
        if(!arena)
            continue;
        DestroyArenaObject(m_context.objectArena, arena);
    }
    m_workerCommandArenas.clear();
}

void Queue::collectCompletedCommandBuffers(){
    auto it = m_commandBuffersInFlight.begin();
    while(it != m_commandBuffersInFlight.end()){
        TrackedCommandBuffer* cmdBuf = it->get();
        if(cmdBuf->m_submissionID > m_lastFinishedID)
            break;

        it = recycleCommandBuffer(m_commandBuffersInFlight, it);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

