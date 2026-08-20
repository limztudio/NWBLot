// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TrackedCommandBuffer::TrackedCommandBuffer(
    const VulkanContext& context,
    const u32 queueFamilyIndex,
    const VkCommandPool commandPool,
    const bool ownsCommandPool
)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_cmdPool(commandPool)
    , m_ownsCmdPool(ownsCommandPool)
    , m_referencedResources(context.objectArena)
    , m_referencedStagingBuffers(context.objectArena)
    , m_referencedDescriptorHeaps(context.objectArena)
    , m_retainedTextureStateCommits(context.objectArena)
    , m_context(context)
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
        vkFreeCommandBuffers(m_context.device, m_cmdPool, 1, &m_cmdBuf);
        m_cmdBuf = VK_NULL_HANDLE;
    }

    if(m_ownsCmdPool && m_cmdPool){
        vkDestroyCommandPool(m_context.device, m_cmdPool, m_context.allocationCallbacks);
    }
    m_cmdPool = VK_NULL_HANDLE;
    m_ownsCmdPool = false;

    clearTrackedReferences();
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

TrackedCommandBufferPtr Queue::createCommandBuffer(const VkCommandPool commandPool, const u32 recordingWorkerIndex){
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
        m_context,
        m_queueFamilyIndex,
        commandPool,
        ownsCommandPool
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
    cmdBuf->m_recordingWorkerIndex = recordingWorkerIndex;
    return TrackedCommandBufferPtr(cmdBuf, TrackedCommandBufferPtr::deleter_type(&m_context.objectArena), AdoptRef);
}


Queue::WorkerCommandArena* Queue::findWorkerCommandArena(const u32 recordingWorkerIndex){
    NWB_ASSERT(recordingWorkerIndex != 0u);
    if(recordingWorkerIndex == 0u)
        return nullptr;

    ScopedLock lock(m_workerCommandArenasMutex);
    for(WorkerCommandArena* const arena : m_workerCommandArenas){
        if(arena && arena->recordingWorkerIndex == recordingWorkerIndex)
            return arena;
    }
    return nullptr;
}


Queue::WorkerCommandArena* Queue::getOrCreateWorkerCommandArena(const u32 recordingWorkerIndex){
    NWB_ASSERT(recordingWorkerIndex != 0u);
    if(recordingWorkerIndex == 0u)
        return nullptr;

    ScopedLock lock(m_workerCommandArenasMutex);
    for(WorkerCommandArena* const arena : m_workerCommandArenas){
        if(arena && arena->recordingWorkerIndex == recordingWorkerIndex)
            return arena;
    }

    auto* const arena = NewArenaObject<WorkerCommandArena>(
        m_context.objectArena,
        m_context.objectArena,
        recordingWorkerIndex
    );
    if(!arena){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to allocate command-arena storage for physical queue {} worker {}"),
            m_physicalQueue.index,
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
            NWB_TEXT("Vulkan: Failed to create command pool for physical queue {} worker {}: {}"),
            m_physicalQueue.index,
            recordingWorkerIndex,
            ResultToString(createResult)
        );
        DestroyArenaObject(m_context.objectArena, arena);
        return nullptr;
    }

    m_workerCommandArenas.push_back(arena);
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
            return createCommandBuffer(VK_NULL_HANDLE, 0u);

        const VkResult res = vkResetCommandBuffer(cmdBuf->m_cmdBuf, 0);
        if(res != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to reset command buffer, creating a new one: {}"), ResultToString(res));
            return createCommandBuffer(VK_NULL_HANDLE, 0u);
        }

        cmdBuf->m_recordingID = m_lastRecordingID.fetch_add(1u, MemoryOrder::relaxed) + 1u;
        cmdBuf->m_recordingWorkerIndex = 0u;

        return cmdBuf;
    }

    return createCommandBuffer(VK_NULL_HANDLE, 0u);
}


TrackedCommandBufferPtr Queue::getOrCreateWorkerCommandBuffer(const u32 recordingWorkerIndex){
    WorkerCommandArena* const arena = getOrCreateWorkerCommandArena(recordingWorkerIndex);
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
            return createCommandBuffer(arena->commandPool, recordingWorkerIndex);

        const VkResult res = vkResetCommandBuffer(cmdBuf->m_cmdBuf, 0);
        if(res != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to reset worker command buffer, creating a new one: {}"), ResultToString(res));
            return createCommandBuffer(arena->commandPool, recordingWorkerIndex);
        }

        cmdBuf->m_recordingID = m_lastRecordingID.fetch_add(1u, MemoryOrder::relaxed) + 1u;
        cmdBuf->m_recordingWorkerIndex = recordingWorkerIndex;
        return cmdBuf;
    }

    // Queue timeline polling/reclamation occurs in Device::runGarbageCollection(). An empty worker shard grows
    // instead of taking the queue submission lock and serializing parallel ready-frontier recording.
    return createCommandBuffer(arena->commandPool, recordingWorkerIndex);
}


TrackedCommandBufferPtr Queue::getOrCreateCommandBuffer(const u32 recordingWorkerIndex){
    if(recordingWorkerIndex == 0u)
        return getOrCreateDirectCommandBuffer();

    return getOrCreateWorkerCommandBuffer(recordingWorkerIndex);
}


void Queue::destroyWorkerCommandArenas(){
    ScopedLock workerArenasLock(m_workerCommandArenasMutex);
    for(WorkerCommandArena* const arena : m_workerCommandArenas){
        if(!arena)
            continue;

        {
            ScopedLock arenaLock(arena->mutex);
            arena->commandBuffersPool.clear();
            if(arena->commandPool){
                vkDestroyCommandPool(m_context.device, arena->commandPool, m_context.allocationCallbacks);
                arena->commandPool = VK_NULL_HANDLE;
            }
        }
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
    if(cmdBuf->m_recordingWorkerIndex == 0u){
        m_commandBuffersPool.push_back(Move(cmdBuf));
        return;
    }

    WorkerCommandArena* const workerArena = findWorkerCommandArena(cmdBuf->m_recordingWorkerIndex);
    if(!workerArena){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Cannot recycle command buffer because physical queue {} worker {} no longer has a command arena"),
            m_physicalQueue.index,
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

