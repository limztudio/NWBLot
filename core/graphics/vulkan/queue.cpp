// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TrackedCommandBuffer::TrackedCommandBuffer(
    Queue& queue,
    const VulkanContext& context,
    const u32 queueFamilyIndex,
    const VkCommandPool commandPool,
    const bool ownsCommandPool,
    Futex* const sharedCommandPoolMutex
)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_cmdPool(commandPool)
    , m_ownsCmdPool(ownsCommandPool)
    , m_sharedCommandPoolMutex(sharedCommandPoolMutex)
    , m_referencedResources(context.objectArena)
    , m_referencedStagingBuffers(context.objectArena)
    , m_referencedDescriptorHeaps(context.objectArena)
    , m_retainedTextureStateCommits(context.objectArena)
    , m_context(context)
    , m_queue(queue)
{
    if(m_ownsCmdPool){
        auto poolInfo = VulkanDetail::MakeVkStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        const VkResult createResult = vkCreateCommandPool(
            m_context.device,
            &poolInfo,
            m_context.allocationCallbacks,
            &m_cmdPool
        );
        if(createResult != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create private command pool: {}"), ResultToString(createResult));
            m_cmdPool = VK_NULL_HANDLE;
            return;
        }
    }
    else if(m_cmdPool == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot allocate a command buffer without a worker command pool."));
        return;
    }

    auto allocInfo = VulkanDetail::MakeVkStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    allocInfo.commandPool = m_cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    const VkResult allocateResult = vkAllocateCommandBuffers(m_context.device, &allocInfo, &m_cmdBuf);
    if(allocateResult != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate command buffer: {}"), ResultToString(allocateResult));
        m_cmdBuf = VK_NULL_HANDLE;
        if(m_ownsCmdPool)
            vkDestroyCommandPool(m_context.device, m_cmdPool, m_context.allocationCallbacks);
        m_cmdPool = VK_NULL_HANDLE;
        m_ownsCmdPool = false;
    }
}

TrackedCommandBuffer::~TrackedCommandBuffer(){
    if(m_cmdBuf && m_cmdPool){
        if(m_sharedCommandPoolMutex){
            ScopedLock lock(*m_sharedCommandPoolMutex);

            vkFreeCommandBuffers(m_context.device, m_cmdPool, 1, &m_cmdBuf);
        }
        else
            vkFreeCommandBuffers(m_context.device, m_cmdPool, 1, &m_cmdBuf);
        m_cmdBuf = VK_NULL_HANDLE;
    }

    if(m_ownsCmdPool && m_cmdPool){
        vkDestroyCommandPool(m_context.device, m_cmdPool, m_context.allocationCallbacks);
    }
    m_cmdPool = VK_NULL_HANDLE;
    m_ownsCmdPool = false;

    clearTrackedReferences();
    m_queue.unregisterCommandBuffer(*this);
}

void TrackedCommandBuffer::appendRetainedTextureStateCommit(
    Texture& texture,
    const MipLevel mipLevel,
    const ArraySlice arraySlice
){
    // The closing barrier and its deferred state publication outlive CommandList::clearState(). Keep the texture
    // alive with the command buffer until Queue::submit accepts or discards the command buffer.
    m_referencedResources.emplace_back(&texture, Handle<GraphicsResource>::deleter_type(&m_context.objectArena));

    m_retainedTextureStateCommits.push_back(RetainedTextureStateCommit{
        .texture = &texture,
        .mipLevel = mipLevel,
        .arraySlice = arraySlice,
    });
}

void TrackedCommandBuffer::commitRetainedTextureStateCommits(){
    for(const RetainedTextureStateCommit& commit : m_retainedTextureStateCommits){
        if(commit.texture)
            commit.texture->setRetainedSubresourceStateKnown(commit.arraySlice, commit.mipLevel, true);
    }
    m_retainedTextureStateCommits.clear();
}

void TrackedCommandBuffer::discardRetainedTextureStateCommits(){
    m_retainedTextureStateCommits.clear();
}

void TrackedCommandBuffer::clearTrackedReferences(){
    discardRetainedTextureStateCommits();

    for(GpuDescriptorHeap* heap : m_referencedDescriptorHeaps){
        if(heap)
            heap->discardCommandBufferUse(*this);
    }
    m_referencedDescriptorHeaps.clear();

    m_referencedResources.clear();
    m_referencedStagingBuffers.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Queue::Queue(
    const VulkanContext& context,
    Device& device,
    const GpuPhysicalQueueInfo& info,
    VkQueue queue
)
    : m_context(context)
    , m_device(device)
    , m_queue(queue)
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
    auto timelineInfo = VulkanDetail::MakeVkStruct<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;

    auto semaphoreInfo = VulkanDetail::MakeVkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    semaphoreInfo.pNext = &timelineInfo;

    const VkResult res = vkCreateSemaphore(m_context.device, &semaphoreInfo, m_context.allocationCallbacks, &m_trackingSemaphore);
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

        const VkResult res = vkWaitSemaphores(m_context.device, &waitInfo, UINT64_MAX);
        if(res != VK_SUCCESS)
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to wait on queue timeline semaphore during teardown: {}"), ResultToString(res));
    }

    m_commandBuffersInFlight.clear();
    m_commandBuffersPool.clear();
    destroyWorkerCommandArenas();

    if(m_trackingSemaphore){
        vkDestroySemaphore(m_context.device, m_trackingSemaphore, m_context.allocationCallbacks);
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

void Queue::registerCommandBuffer(TrackedCommandBuffer& commandBuffer)noexcept{
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

    cmdBuf->m_recordingID = m_lastRecordingID.fetch_add(1u, MemoryOrder::relaxed) + 1u;
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

    const VkResult createResult = vkCreateCommandPool(
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

        const VkResult res = vkResetCommandBuffer(cmdBuf->m_cmdBuf, 0);
        if(res != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to reset command buffer, creating a new one: {}"), ResultToString(res));
            return createCommandBuffer(VK_NULL_HANDLE, nullptr, 0u, 0u);
        }

        transitionCommandBufferState(*cmdBuf, TrackedCommandBufferArenaState::Leased);
        m_commandBufferResetEventCount.fetch_add(1u, MemoryOrder::relaxed);
        m_directCommandBufferResetEventCount.fetch_add(1u, MemoryOrder::relaxed);
        cmdBuf->m_recordingID = m_lastRecordingID.fetch_add(1u, MemoryOrder::relaxed) + 1u;
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

        const VkResult res = vkResetCommandBuffer(cmdBuf->m_cmdBuf, 0);
        if(res != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to reset worker command buffer, creating a new one: {}"), ResultToString(res));
            vkFreeCommandBuffers(m_context.device, arena->commandPool, 1u, &cmdBuf->m_cmdBuf);
            cmdBuf->m_cmdBuf = VK_NULL_HANDLE;
            cmdBuf.reset();
            return createCommandBuffer(arena->commandPool, &arena->mutex, recordingWorkerDomain, recordingWorkerIndex);
        }

        transitionCommandBufferState(*cmdBuf, TrackedCommandBufferArenaState::Leased);
        m_commandBufferResetEventCount.fetch_add(1u, MemoryOrder::relaxed);
        arena->resetEventCount.fetch_add(1u, MemoryOrder::relaxed);
        cmdBuf->m_recordingID = m_lastRecordingID.fetch_add(1u, MemoryOrder::relaxed) + 1u;
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
                vkDestroyCommandPool(m_context.device, arena->commandPool, m_context.allocationCallbacks);
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

        recycleCommandBuffer(Move(*it));
        it = m_commandBuffersInFlight.erase(it);
    }
}

void Queue::addWaitSemaphore(VkSemaphore semaphore, u64 value){
    if(!semaphore)
        return;

    ScopedLock lock(m_mutex);
    m_waitSemaphores.push_back(semaphore);
    m_waitSemaphoreValues.push_back(value);
}

void Queue::addSignalSemaphore(VkSemaphore semaphore, u64 value){
    if(!semaphore)
        return;

    ScopedLock lock(m_mutex);
    m_signalSemaphores.push_back(semaphore);
    m_signalSemaphoreValues.push_back(value);
}

u64 Queue::submit(
    CommandList* const* ppCmd,
    const usize numCmd,
    const SubmissionWait* const localWaits,
    const usize localWaitCount,
    bool* const outSubmissionAccepted,
    const SubmissionSignal* const localSignals,
    const usize localSignalCount
){
    ScopedLock lock(m_mutex);
    if(outSubmissionAccepted)
        *outSubmissionAccepted = false;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_QueueSubmitArena);

    const bool hasCommands = ppCmd && numCmd > 0;
    if(localWaitCount > 0u && !localWaits){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: local wait array is null"));
        clearPendingSemaphores();
        return m_lastSubmittedID;
    }
    if(localSignalCount > 0u && !localSignals){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: local signal array is null"));
        clearPendingSemaphores();
        return m_lastSubmittedID;
    }

    const bool hasPendingSemaphores = localWaitCount > 0u
        || localSignalCount > 0u
        || !m_waitSemaphores.empty()
        || !m_signalSemaphores.empty()
    ;

    if(hasCommands && numCmd > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: command list count exceeds Vulkan limit"));
        clearPendingSemaphores();
        return m_lastSubmittedID;
    }
    if(
        localWaitCount > static_cast<usize>(Limit<u32>::s_Max)
        || m_waitSemaphores.size() > static_cast<usize>(Limit<u32>::s_Max) - localWaitCount
        || localSignalCount >= static_cast<usize>(Limit<u32>::s_Max)
        || m_signalSemaphores.size() >= static_cast<usize>(Limit<u32>::s_Max) - localSignalCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: queued semaphore count exceeds Vulkan limit"));
        clearPendingSemaphores();
        return m_lastSubmittedID;
    }
    if((hasCommands || hasPendingSemaphores) && m_lastSubmittedID == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: queue submission ID exhausted"));
        clearPendingSemaphores();
        return m_lastSubmittedID;
    }
    if(hasCommands){
        for(usize i = 0; i < numCmd; ++i){
            auto* cmdList = ppCmd[i];
            if(
                cmdList
                && cmdList->m_currentCmdBuf
                && (
                    cmdList->m_desc.queueType != m_queueID
                    || cmdList->m_desc.physicalQueue != m_physicalQueue
                )
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: command list physical queue does not match the execution queue"));
                clearPendingSemaphores();
                return m_lastSubmittedID;
            }
        }
    }
    for(usize i = 0u; i < localSignalCount; ++i){
        if(localSignals[i].semaphore == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: local signal semaphore is null"));
            clearPendingSemaphores();
            return m_lastSubmittedID;
        }
    }

    Vector<TrackedCommandBufferPtr, Alloc::ScratchArena> trackedBuffers{scratchArena};
    Vector<VkCommandBufferSubmitInfo, Alloc::ScratchArena> cmdBufInfos{scratchArena};

    if(hasCommands){
        trackedBuffers.reserve(numCmd);
        cmdBufInfos.reserve(numCmd);

        for(usize i = 0; i < numCmd; ++i){
            auto* cmdList = ppCmd[i];
            if(!cmdList || !cmdList->m_currentCmdBuf)
                continue;

            auto cmdBufInfo = VulkanDetail::MakeVkStruct<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
            cmdBufInfo.commandBuffer = cmdList->m_currentCmdBuf->m_cmdBuf;
            cmdBufInfos.push_back(cmdBufInfo);

            cmdList->m_currentCmdBuf->m_submissionID = m_lastSubmittedID + 1;
            trackedBuffers.push_back(Move(cmdList->m_currentCmdBuf));
        }
    }

    if(cmdBufInfos.empty() && !hasPendingSemaphores)
        return m_lastSubmittedID;

    if(m_trackingSemaphore == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Queue submission skipped because timeline semaphore is unavailable."));
        clearPendingSemaphores();

        for(auto& tracked : trackedBuffers)
            recycleCommandBuffer(Move(tracked));
        return m_lastSubmittedID;
    }

    u64 submissionID = ++m_lastSubmittedID;

    auto timelineSignal = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    timelineSignal.semaphore = m_trackingSemaphore;
    timelineSignal.value = submissionID;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    Vector<VkSemaphoreSubmitInfo, Alloc::ScratchArena> waitInfos{scratchArena};
    waitInfos.reserve(localWaitCount + m_waitSemaphores.size());
    for(usize i = 0; i < localWaitCount; ++i){
        if(localWaits[i].semaphore == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: local wait semaphore is null"));
            m_lastSubmittedID = submissionID - 1;
            clearPendingSemaphores();
            for(auto& tracked : trackedBuffers)
                recycleCommandBuffer(Move(tracked));
            return m_lastSubmittedID;
        }

        VkSemaphoreSubmitInfo waitInfo = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        waitInfo.semaphore = localWaits[i].semaphore;
        waitInfo.value = localWaits[i].value;
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitInfos.push_back(waitInfo);
    }
    for(usize i = 0; i < m_waitSemaphores.size(); ++i){
        VkSemaphoreSubmitInfo waitInfo = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        waitInfo.semaphore = m_waitSemaphores[i];
        waitInfo.value = m_waitSemaphoreValues[i];
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitInfos.push_back(waitInfo);
    }

    Vector<VkSemaphoreSubmitInfo, Alloc::ScratchArena> signalInfos{scratchArena};
    signalInfos.reserve(1u + localSignalCount + m_signalSemaphores.size());
    signalInfos.push_back(timelineSignal);

    for(usize i = 0u; i < localSignalCount; ++i){
        VkSemaphoreSubmitInfo signalInfo = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        signalInfo.semaphore = localSignals[i].semaphore;
        signalInfo.value = localSignals[i].value;
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalInfos.push_back(signalInfo);
    }

    for(usize i = 0; i < m_signalSemaphores.size(); ++i){
        VkSemaphoreSubmitInfo signalInfo = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        signalInfo.semaphore = m_signalSemaphores[i];
        signalInfo.value = m_signalSemaphoreValues[i];
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalInfos.push_back(signalInfo);
    }

    auto submitInfo = VulkanDetail::MakeVkStruct<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
    submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
    submitInfo.pWaitSemaphoreInfos = waitInfos.data();
    submitInfo.commandBufferInfoCount = static_cast<uint32_t>(cmdBufInfos.size());
    submitInfo.pCommandBufferInfos = cmdBufInfos.data();
    submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
    submitInfo.pSignalSemaphoreInfos = signalInfos.data();

#if !defined(NWB_FINAL)
    if(m_device.consumeSubmissionRejectionForTesting(m_queueID)){
        // Match the real vkQueueSubmit2 rejection path exactly: the detached command buffers return to this queue's
        // pool, the tentative timeline value is rolled back, and Device::executeCommandLists performs its normal
        // upload/scratch cleanup after observing outSubmissionAccepted=false.
        clearPendingSemaphores();
        m_lastSubmittedID = submissionID - 1;
        for(auto& tracked : trackedBuffers)
            recycleCommandBuffer(Move(tracked));
        return m_lastSubmittedID;
    }
#endif

    const VkResult res = vkQueueSubmit2(m_queue, 1, &submitInfo, VK_NULL_HANDLE);

    clearPendingSemaphores();

    if(res != VK_SUCCESS){
        m_lastSubmittedID = submissionID - 1;

        if(res == VK_ERROR_DEVICE_LOST){
            m_device.captureGpuCrash("queue submit");
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Device was lost during queue submission."));
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command buffers to queue: {}"), ResultToString(res));
        }

        for(auto& tracked : trackedBuffers){
            recycleCommandBuffer(Move(tracked));
        }

        return m_lastSubmittedID;
    }

    const QueueSubmissionToken submissionToken{
        .queue = m_queueID,
        .value = submissionID,
        .physicalQueueIndex = m_physicalQueue.index,
        .deviceGeneration = m_physicalQueue.deviceGeneration,
    };
    for(auto& tracked : trackedBuffers){
        tracked->commitRetainedTextureStateCommits();
        transitionCommandBufferState(*tracked, TrackedCommandBufferArenaState::Pending);
        for(GpuDescriptorHeap* heap : tracked->m_referencedDescriptorHeaps){
            if(heap)
                heap->submitCommandBufferUse(*tracked, submissionToken);
        }
        m_commandBuffersInFlight.push_back(Move(tracked));
    }
    if(outSubmissionAccepted)
        *outSubmissionAccepted = true;

    return submissionID;
}

void Queue::updateLastFinishedID(){
    if(!m_trackingSemaphore){
        m_lastFinishedID = m_lastSubmittedID;
        return;
    }

    u64 completedValue = 0;
    const VkResult res = vkGetSemaphoreCounterValue(m_context.device, m_trackingSemaphore, &completedValue);
    if(res == VK_SUCCESS)
        // vkQueueWaitIdle() establishes a stronger completion fact than a later timeline query. Never let a stale
        // driver value make already-retired command buffers or descriptor uses appear in flight again.
        m_lastFinishedID = Max(m_lastFinishedID, completedValue);
    else{
        if(res == VK_ERROR_DEVICE_LOST)
            m_device.captureGpuCrash("queue timeline query");
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to query queue timeline semaphore value: {}"), ResultToString(res));
    }
}

void Queue::waitForIdle(){
    ScopedLock lock(m_mutex);

    const VkResult res = vkQueueWaitIdle(m_queue);
    if(res != VK_SUCCESS){
        if(res == VK_ERROR_DEVICE_LOST)
            m_device.captureGpuCrash("queue wait idle");
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Queue wait-for-idle failed: {}"), ResultToString(res));
    }
    if(res == VK_SUCCESS){
        m_lastFinishedID = m_lastSubmittedID;
        collectCompletedCommandBuffers();
    }
}

void Queue::clearPendingSemaphores(){
    m_waitSemaphores.clear();
    m_waitSemaphoreValues.clear();
    m_signalSemaphores.clear();
    m_signalSemaphoreValues.clear();
}

void Queue::recycleCommandBuffer(TrackedCommandBufferPtr&& cmdBuf){
    if(!cmdBuf)
        return;

    cmdBuf->clearTrackedReferences();
    transitionCommandBufferState(*cmdBuf, TrackedCommandBufferArenaState::Reusable);
    if(cmdBuf->m_recordingWorkerIndex == 0u){
        m_commandBuffersPool.push_back(Move(cmdBuf));
        return;
    }

    WorkerCommandArena* const workerArena = findWorkerCommandArena(
        cmdBuf->m_recordingWorkerDomain,
        cmdBuf->m_recordingWorkerIndex
    );
    if(!workerArena){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Cannot recycle command buffer because physical queue {} worker {}:{} no longer has a command arena"),
            m_physicalQueue.index,
            cmdBuf->m_recordingWorkerDomain,
            cmdBuf->m_recordingWorkerIndex
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Worker command buffer lost its owning command arena"));
        m_commandBuffersPool.push_back(Move(cmdBuf));
        return;
    }

    // Queue submission/timeline retirement holds m_mutex before arriving here. It may take a worker arena lock,
    // but worker recording never takes m_mutex, so the lock order cannot form a cycle.
    ScopedLock lock(workerArena->mutex);

    workerArena->commandBuffersPool.push_back(Move(cmdBuf));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Device::queueWaitForSemaphore(CommandQueue::Enum waitQueue, VkSemaphore semaphore, u64 value){
    Queue* q = getQueue(waitQueue);
    if(q)
        q->addWaitSemaphore(semaphore, value);
}

void Device::queueSignalSemaphore(CommandQueue::Enum executionQueue, VkSemaphore semaphore, u64 value){
    Queue* q = getQueue(executionQueue);
    if(q)
        q->addSignalSemaphore(semaphore, value);
}

u64 Device::queueGetCompletedInstance(CommandQueue::Enum queue){
    return queueGetCompletedInstance(getPrimaryPhysicalQueue(queue));
}

u64 Device::queueGetCompletedInstance(const GpuPhysicalQueueId& queue){
    // executeCommandLists() uses this on a rejected submit to retire CPU-side upload/scratch bookkeeping. A
    // VK_ERROR_DEVICE_LOST rejection has already marked the device terminal, so do not issue a second timeline query
    // while performing that cleanup.
    if(isDeviceLost())
        return 0;

    Queue* q = getQueue(queue);
    if(q){
        ScopedLock lock(q->m_mutex);
        q->updateLastFinishedID();
        return q->m_lastFinishedID;
    }
    return 0;
}

void Device::queueWaitForCommandList(CommandQueue::Enum waitQueue, CommandQueue::Enum executionQueue, u64 instance){
    Queue* wait = getQueue(waitQueue);
    Queue* exec = getQueue(executionQueue);

    if(wait && exec)
        wait->addWaitSemaphore(exec->m_trackingSemaphore, instance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

