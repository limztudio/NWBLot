// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "queue_command_arena_statistics.h"
#include "arena_names.h"

#include <core/common/log.h>

#include <global/atomic_identity.h>
#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Queue::Queue(const VulkanContext& context, Device& device, const GpuPhysicalQueueInfo& info, NativeQueueState& nativeQueue)
    : m_context(context)
    , m_device(device)
    , m_nativeQueue(nativeQueue)
    , m_physicalQueue(info.id)
    , m_queueFamilyIndex(info.familyIndex)
    , m_queueID(info.queueClass)
    , m_acceptedTimelineWaits(context.objectArena)
    , m_submitDescriptorHeapUseCommitTickets(context.objectArena)
    , m_submitValidatedTimerQueryCommandBuffers(context.objectArena)
    , m_submitWaitInfos(context.objectArena)
    , m_submitSignalInfos(context.objectArena)
    , m_submitCommandBufferInfos(context.objectArena)
    , m_submitPreparedCommandBuffers(context.objectArena)
    , m_executeLocalWaits(context.objectArena)
    , m_executeExpectedCommandLists(context.objectArena)
    , m_executeSubmittedOwners(context.objectArena)
    , m_lastRecordingID(0u)
    , m_lastSubmittedID(0)
    , m_lastFinishedID(0)
    , m_commandBuffersInFlight(context.objectArena)
    , m_commandBuffersPool(context.objectArena)
    , m_workerCommandArenas(context.objectArena)
{
    FOUNDATION_ASSERT(m_nativeQueue.familyIndex == info.familyIndex && m_nativeQueue.queueIndex == info.queueIndex);
    auto timelineInfo = VulkanDetail::MakeVkStruct<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;

    auto semaphoreInfo = VulkanDetail::MakeVkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    semaphoreInfo.pNext = &timelineInfo;

    const VkResult res = m_context.deviceDispatch.vkCreateSemaphore(m_context.device, &semaphoreInfo, m_context.allocationCallbacks, &m_trackingSemaphore);
    if(res != VK_SUCCESS){
        m_trackingSemaphore = VK_NULL_HANDLE;
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create queue timeline semaphore: {}"), ResultToString(res));
    }
}

Queue::~Queue()noexcept{
    m_submitPreparedCommandBuffers.clear();
    m_executeExpectedCommandLists.clear();
    m_commandBuffersInFlight.clear();
    m_commandBuffersPool.clear();
    destroyWorkerCommandArenas();

    if(m_trackingSemaphore){
        m_context.deviceDispatch.vkDestroySemaphore(m_context.device, m_trackingSemaphore, m_context.allocationCallbacks);
        m_trackingSemaphore = VK_NULL_HANDLE;
    }
}

Expected<TrackedCommandBufferPtr, VkResult> Queue::getOrCreateCommandBuffer(const u64 recordingWorkerDomain, const u32 recordingWorkerIndex){
    if(recordingWorkerIndex == 0u)
        return getOrCreateDirectCommandBuffer();

    return getOrCreateWorkerCommandBuffer(recordingWorkerDomain, recordingWorkerIndex);
}


GpuCommandArenaStatistics Queue::commandArenaStatistics()const noexcept{
    return CommandArenaStatistics(*this).collect();
}

GpuCommandArenaWorkerStatistics Queue::commandArenaWorkerStatistics(const u64 recordingWorkerDomain, const u32 recordingWorkerIndex)const noexcept{
    return CommandArenaStatistics(*this).collectWorker(recordingWorkerDomain, recordingWorkerIndex);
}


void Queue::registerCommandBuffer(TrackedCommandBuffer& commandBuffer)noexcept{
    if(&commandBuffer.m_queue != this || &commandBuffer.m_context != &m_context || commandBuffer.m_arenaState != TrackedCommandBufferArenaState::Untracked)
        TerminateInvariant();
    commandBuffer.m_arenaState = TrackedCommandBufferArenaState::Leased;
    const u64 currentCount = m_currentCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed) + 1u;
    m_leasedCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
    m_commandBufferGrowthEventCount.fetch_add(1u, MemoryOrder::relaxed);
    if(commandBuffer.m_recordingWorkerIndex == 0u){
        const u64 directCurrentCount = m_directCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed) + 1u;
        m_directLeasedCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        m_directCommandBufferGrowthEventCount.fetch_add(1u, MemoryOrder::relaxed);
        UpdateAtomicMaxRelaxed(m_directHighWaterCommandBufferCount, directCurrentCount);
    }
    else{
        WorkerCommandArena* const arena = findWorkerCommandArena(commandBuffer.m_recordingWorkerDomain, commandBuffer.m_recordingWorkerIndex);
        if(!arena)
            TerminateInvariant();
        const u64 workerCurrentCount = arena->currentCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed) + 1u;
        arena->leasedCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        arena->growthEventCount.fetch_add(1u, MemoryOrder::relaxed);
        UpdateAtomicMaxRelaxed(arena->highWaterCommandBufferCount, workerCurrentCount);
    }
    UpdateAtomicMaxRelaxed(m_highWaterCommandBufferCount, currentCount);
}

b8 Queue::validateCommandBufferSubmissionState(const TrackedCommandBuffer& commandBuffer)const{
    if(&commandBuffer.m_queue != this || &commandBuffer.m_context != &m_context){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Cannot submit a command buffer through a foreign queue or context"));
        return false;
    }
    if(commandBuffer.m_arenaState != TrackedCommandBufferArenaState::Leased){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Native submission requires a leased command buffer"));
        return false;
    }
    if(m_leasedCommandBufferCount.load(MemoryOrder::relaxed) == 0u){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Native submission observed an empty leased command-buffer count"));
        return false;
    }
    if(commandBuffer.m_recordingWorkerIndex == 0u){
        if(m_directLeasedCommandBufferCount.load(MemoryOrder::relaxed) != 0u)
            return true;
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Native submission observed an empty direct leased command-buffer count"));
        return false;
    }

    const WorkerCommandArena* const workerArena = findWorkerCommandArena(commandBuffer.m_recordingWorkerDomain, commandBuffer.m_recordingWorkerIndex);
    if(workerArena && workerArena->leasedCommandBufferCount.load(MemoryOrder::relaxed) != 0u)
        return true;
    NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Native submission could not validate its worker command-buffer arena"));
    return false;
}

void Queue::transitionCommandBufferState(TrackedCommandBuffer& commandBuffer, const TrackedCommandBufferArenaState::Enum nextState){
    if(&commandBuffer.m_queue != this || &commandBuffer.m_context != &m_context){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Cannot transition a command buffer through a foreign queue"));
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Command buffer arena transition owner mismatch"));
        return;
    }

    commitCommandBufferStateTransition(commandBuffer, nextState);
}

void Queue::commitCommandBufferStateTransition(TrackedCommandBuffer& commandBuffer, const TrackedCommandBufferArenaState::Enum nextState)noexcept{
    if(&commandBuffer.m_queue != this || &commandBuffer.m_context != &m_context)
        TerminateInvariant();
    const TrackedCommandBufferArenaState::Enum previousState = commandBuffer.m_arenaState;
    if(previousState == nextState)
        return;
    WorkerCommandArena* const workerArena = commandBuffer.m_recordingWorkerIndex == 0u ? nullptr : findWorkerCommandArena(commandBuffer.m_recordingWorkerDomain, commandBuffer.m_recordingWorkerIndex);
    if(commandBuffer.m_recordingWorkerIndex != 0u && !workerArena)
        TerminateInvariant();

    switch(previousState){
    case TrackedCommandBufferArenaState::Leased:{
        DecrementAtomicOrTerminate(m_leasedCommandBufferCount);
        if(commandBuffer.m_recordingWorkerIndex == 0u){
            DecrementAtomicOrTerminate(m_directLeasedCommandBufferCount);
        }
        else
            DecrementAtomicOrTerminate(workerArena->leasedCommandBufferCount);
        break;
    }
    case TrackedCommandBufferArenaState::Reusable:{
        DecrementAtomicOrTerminate(m_reusableCommandBufferCount);
        if(commandBuffer.m_recordingWorkerIndex == 0u){
            DecrementAtomicOrTerminate(m_directReusableCommandBufferCount);
        }
        else
            DecrementAtomicOrTerminate(workerArena->reusableCommandBufferCount);
        break;
    }
    case TrackedCommandBufferArenaState::Pending:{
        DecrementAtomicOrTerminate(m_pendingCommandBufferCount);
        if(commandBuffer.m_recordingWorkerIndex == 0u){
            DecrementAtomicOrTerminate(m_pendingDirectCommandBufferCount);
        }
        else{
            const u64 previousPendingCount = DecrementAtomicOrTerminate(workerArena->pendingCommandBufferCount);
            if(previousPendingCount == 1u)
                DecrementAtomicOrTerminate(m_pendingWorkerEpochCount);
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
        else
            workerArena->leasedCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        break;
    case TrackedCommandBufferArenaState::Reusable:
        m_reusableCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        if(commandBuffer.m_recordingWorkerIndex == 0u)
            m_directReusableCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        else
            workerArena->reusableCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        break;
    case TrackedCommandBufferArenaState::Pending:
        m_pendingCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        if(commandBuffer.m_recordingWorkerIndex == 0u)
            m_pendingDirectCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed);
        else if(workerArena->pendingCommandBufferCount.fetch_add(1u, MemoryOrder::relaxed) == 0u)
            m_pendingWorkerEpochCount.fetch_add(1u, MemoryOrder::relaxed);
        break;
    default:
        break;
    }
    commandBuffer.m_arenaState = nextState;
}

void Queue::unregisterCommandBuffer(TrackedCommandBuffer& commandBuffer)noexcept{
    if(commandBuffer.m_arenaState == TrackedCommandBufferArenaState::Untracked)
        return;

    if(&commandBuffer.m_queue != this || &commandBuffer.m_context != &m_context)
        TerminateInvariant();
    commitCommandBufferStateTransition(commandBuffer, TrackedCommandBufferArenaState::Untracked);
    DecrementAtomicOrTerminate(m_currentCommandBufferCount);
    if(commandBuffer.m_recordingWorkerIndex == 0u){
        DecrementAtomicOrTerminate(m_directCommandBufferCount);
    }
    else{
        WorkerCommandArena* const arena = findWorkerCommandArena(commandBuffer.m_recordingWorkerDomain, commandBuffer.m_recordingWorkerIndex);
        if(!arena)
            TerminateInvariant();
        DecrementAtomicOrTerminate(arena->currentCommandBufferCount);
    }
}


void Queue::collectCompletedCommandBuffers(){
    auto it = m_commandBuffersInFlight.begin();
    while(it != m_commandBuffersInFlight.end()){
        auto& cmdBuf = *MakeNotNull(it->get());
        if(cmdBuf.m_submissionID > m_lastFinishedID)
            break;

        it = recycleCommandBuffer(m_commandBuffersInFlight, it);
    }
}


Expected<TrackedCommandBufferPtr, VkResult> Queue::createCommandBuffer(const VkCommandPool commandPool, Futex* const sharedCommandPoolMutex, const u64 recordingWorkerDomain, const u32 recordingWorkerIndex){
    const b8 ownsCommandPool = recordingWorkerIndex == 0u;
    if(!ownsCommandPool && commandPool == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Cannot create a worker command buffer without a command pool for physical queue {} worker {}"), m_physicalQueue.index, recordingWorkerIndex);
        return MakeUnexpected(VK_ERROR_INITIALIZATION_FAILED);
    }

    auto* const allocatedCommandBuffer = NewArenaObject<TrackedCommandBuffer>(m_context.objectArena, *this, m_context, commandPool, ownsCommandPool, nullptr);
    if(!allocatedCommandBuffer){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to allocate command-buffer tracking storage for physical queue {} worker {}"), m_physicalQueue.index, recordingWorkerIndex);
        return MakeUnexpected(VK_ERROR_OUT_OF_HOST_MEMORY);
    }
    TrackedCommandBufferPtr cmdBuf(allocatedCommandBuffer, TrackedCommandBufferPtr::deleter_type(&m_context.objectArena), s_AdoptRef);
    const auto initialized = cmdBuf->initialize(m_queueFamilyIndex);
    if(!initialized)
        return MakeUnexpected(initialized.error());
    if(cmdBuf->m_cmdBuf == VK_NULL_HANDLE)
        return MakeUnexpected(VK_ERROR_INITIALIZATION_FAILED);

    cmdBuf->m_sharedCommandPoolMutex = sharedCommandPoolMutex;
    cmdBuf->m_recordingID = NextAtomicWrappingNonzero(m_lastRecordingID);
    cmdBuf->m_recordingWorkerDomain = recordingWorkerDomain;
    cmdBuf->m_recordingWorkerIndex = recordingWorkerIndex;
    registerCommandBuffer(*cmdBuf);
    return Move(cmdBuf);
}


Queue::WorkerCommandArena* Queue::findWorkerCommandArena(const u64 recordingWorkerDomain, const u32 recordingWorkerIndex)const noexcept{
    if(recordingWorkerIndex == 0u)
        return nullptr;

    for(WorkerCommandArena* arena = m_workerCommandArenaHead.load(MemoryOrder::acquire); arena; arena = arena->next.load(MemoryOrder::acquire)){
        if(arena->recordingWorkerDomain == recordingWorkerDomain && arena->recordingWorkerIndex == recordingWorkerIndex)
            return arena;
    }
    return nullptr;
}


Expected<NotNull<Queue::WorkerCommandArena*>, VkResult> Queue::getOrCreateWorkerCommandArena(const u64 recordingWorkerDomain, const u32 recordingWorkerIndex){
    if(recordingWorkerIndex == 0u)
        return MakeUnexpected(VK_ERROR_INITIALIZATION_FAILED);

    if(WorkerCommandArena* const arena = findWorkerCommandArena(recordingWorkerDomain, recordingWorkerIndex))
        return MakeNotNull(arena);

    ScopedLock lock(m_workerCommandArenasMutex);
    if(WorkerCommandArena* const arena = findWorkerCommandArena(recordingWorkerDomain, recordingWorkerIndex))
        return MakeNotNull(arena);

    auto* const arena = NewArenaObject<WorkerCommandArena>(m_context.objectArena, m_context.objectArena, recordingWorkerDomain, recordingWorkerIndex);
    if(!arena){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to allocate command-arena storage for physical queue {} worker {}:{}"), m_physicalQueue.index, recordingWorkerDomain, recordingWorkerIndex);
        return MakeUnexpected(VK_ERROR_OUT_OF_HOST_MEMORY);
    }

    ScopeExit destroyArena([&]()noexcept{
        if(arena->commandPool)
            m_context.deviceDispatch.vkDestroyCommandPool(m_context.device, arena->commandPool, m_context.allocationCallbacks);
        DestroyArenaObjectNoexcept(m_context.objectArena, arena);
    });

    auto poolInfo = VulkanDetail::MakeVkStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    poolInfo.queueFamilyIndex = m_queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    const VkResult createResult = m_context.deviceDispatch.vkCreateCommandPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &arena->commandPool);
    if(createResult != VK_SUCCESS){
        arena->commandPool = VK_NULL_HANDLE;
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create command pool for physical queue {} worker {}:{}: {}"), m_physicalQueue.index, recordingWorkerDomain, recordingWorkerIndex, ResultToString(createResult));
        return MakeUnexpected(createResult);
    }

    m_workerCommandArenas.push_back(MakeNotNull(arena));
    arena->next.store(m_workerCommandArenaHead.load(MemoryOrder::relaxed), MemoryOrder::relaxed);
    m_workerCommandArenaHead.store(arena, MemoryOrder::release);
    m_explicitWorkerArenaCount.fetch_add(1u, MemoryOrder::relaxed);
    destroyArena.release();
    return MakeNotNull(arena);
}


Expected<TrackedCommandBufferPtr, VkResult> Queue::getOrCreateDirectCommandBuffer(){
    UniqueLock<Futex> lock(m_mutex);

    const VkResult completionResult = updateLastFinishedID();
    if(completionResult != VK_SUCCESS){
        lock.unlock();
        if(completionResult == VK_ERROR_DEVICE_LOST)
            m_device.captureDeviceLoss(VulkanArenaScope::s_QueueTimelineQueryContext);
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to query queue timeline semaphore value: {}"), ResultToString(completionResult));
        return MakeUnexpected(completionResult);
    }
    collectCompletedCommandBuffers();

    auto available = m_commandBuffersPool.end();
    for(auto it = m_commandBuffersPool.begin(); it != m_commandBuffersPool.end(); ++it){
        FOUNDATION_ASSERT(*it);
        if((*it)->m_recordingWorkerIndex == 0u){
            available = it;
            break;
        }
    }
    if(available != m_commandBuffersPool.end()){
        TrackedCommandBufferPtr cmdBuf = Move(*available);
        m_commandBuffersPool.erase(available);
        FOUNDATION_ASSERT(cmdBuf && cmdBuf->m_cmdBuf != VK_NULL_HANDLE);

        const VkResult res = m_context.deviceDispatch.vkResetCommandBuffer(cmdBuf->m_cmdBuf, 0);
        if(res != VK_SUCCESS){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to reset command buffer, creating a new one: {}"), ResultToString(res));
            return createCommandBuffer(VK_NULL_HANDLE, nullptr, 0u, 0u);
        }

        transitionCommandBufferState(*cmdBuf, TrackedCommandBufferArenaState::Leased);
        m_commandBufferResetEventCount.fetch_add(1u, MemoryOrder::relaxed);
        m_directCommandBufferResetEventCount.fetch_add(1u, MemoryOrder::relaxed);
        cmdBuf->m_recordingID = NextAtomicWrappingNonzero(m_lastRecordingID);
        cmdBuf->m_recordingWorkerDomain = 0u;
        cmdBuf->m_recordingWorkerIndex = 0u;

        return Move(cmdBuf);
    }

    return createCommandBuffer(VK_NULL_HANDLE, nullptr, 0u, 0u);
}


Expected<TrackedCommandBufferPtr, VkResult> Queue::getOrCreateWorkerCommandBuffer(const u64 recordingWorkerDomain, const u32 recordingWorkerIndex){
    const auto arenaResult = getOrCreateWorkerCommandArena(recordingWorkerDomain, recordingWorkerIndex);
    if(!arenaResult)
        return MakeUnexpected(arenaResult.error());
    const auto arena = *arenaResult;

    // Each explicit worker owns this native pool shard.
    // No m_mutex here: submission stays serialized there, while alloc/reset stay independent for recording workers.
    ScopedLock lock(arena->mutex);

    if(!arena->commandBuffersPool.empty()){
        auto available = arena->commandBuffersPool.begin();
        TrackedCommandBufferPtr cmdBuf = Move(*available);
        FOUNDATION_ASSERT(cmdBuf && cmdBuf->m_cmdBuf != VK_NULL_HANDLE);
        cmdBuf->m_sharedCommandPoolMutex = nullptr;
        arena->commandBuffersPool.erase(available);

        const VkResult res = m_context.deviceDispatch.vkResetCommandBuffer(cmdBuf->m_cmdBuf, 0);
        if(res != VK_SUCCESS){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to reset worker command buffer, creating a new one: {}"), ResultToString(res));
            m_context.deviceDispatch.vkFreeCommandBuffers(m_context.device, arena->commandPool, 1u, &cmdBuf->m_cmdBuf);
            cmdBuf->m_cmdBuf = VK_NULL_HANDLE;
            cmdBuf.reset();
            return createCommandBuffer(arena->commandPool, &arena->mutex, recordingWorkerDomain, recordingWorkerIndex);
        }

        transitionCommandBufferState(*cmdBuf, TrackedCommandBufferArenaState::Leased);
        m_commandBufferResetEventCount.fetch_add(1u, MemoryOrder::relaxed);
        arena->resetEventCount.fetch_add(1u, MemoryOrder::relaxed);
        cmdBuf->m_recordingID = NextAtomicWrappingNonzero(m_lastRecordingID);
        cmdBuf->m_recordingWorkerDomain = recordingWorkerDomain;
        cmdBuf->m_recordingWorkerIndex = recordingWorkerIndex;
        cmdBuf->m_sharedCommandPoolMutex = &arena->mutex;
        return Move(cmdBuf);
    }

    // Queue timeline polling/reclamation occurs in Device::runGarbageCollection().
    // An empty worker shard grows instead of taking the queue submission lock and serializing parallel ready-frontier recording.
    return createCommandBuffer(arena->commandPool, &arena->mutex, recordingWorkerDomain, recordingWorkerIndex);
}

void Queue::destroyWorkerCommandArenas()noexcept{
    NothrowScopedLock workerArenasLock(m_workerCommandArenasMutex);
    for(const auto arena : m_workerCommandArenas){


        {
            NothrowScopedLock arenaLock(arena->mutex);
            for(TrackedCommandBufferPtr& commandBuffer : arena->commandBuffersPool){
                FOUNDATION_ASSERT(commandBuffer);
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
    for(const auto arena : m_workerCommandArenas){

        DestroyArenaObject(m_context.objectArena, arena.get());
    }
    m_workerCommandArenas.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

