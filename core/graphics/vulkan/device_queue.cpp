// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "device_detail.h"

#include <core/common/log.h>
#include <global/atomic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_device_queue{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static VkSemaphore DecodeSubmissionNativeSemaphore(const Object& semaphore)noexcept{
#if VK_USE_64_BIT_PTR_DEFINES
    return static_cast<VkSemaphore>(semaphore.pointer);
#else
    return static_cast<VkSemaphore>(semaphore.integer);
#endif
}

class ScopedSubmissionHookResolution final : NoCopy{
public:
    explicit ScopedSubmissionHookResolution(const QueueSubmissionPreSubmitHook& hook)noexcept
        : m_hook(hook)
    {}
    ~ScopedSubmissionHookResolution(){
        if(m_armed && !resolve({}))
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Submission hook rejected rollback resolution"));
    }


public:
    void arm()noexcept{ m_armed = m_hook.resolved != nullptr; }
    [[nodiscard]] bool resolve(const QueueSubmissionToken& token)noexcept{
        if(!m_armed)
            return true;
        m_armed = false;
        return m_hook.resolved(m_hook.context, m_hook.identity, token);
    }


private:
    QueueSubmissionPreSubmitHook m_hook;
    bool m_armed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Device::beginSubmissionOperation()noexcept{
    ScopedLock lock(m_submissionOperationMutex);
    if(submissionsBlocked())
        return false;

    ++m_activeSubmissionOperationCount;
    return true;
}

void Device::endSubmissionOperation()noexcept{
    bool drained = false;
    {
        ScopedLock lock(m_submissionOperationMutex);
        NWB_ASSERT(m_activeSubmissionOperationCount > 0u);
        --m_activeSubmissionOperationCount;
        drained = m_activeSubmissionOperationCount == 0u;
    }
    if(drained)
        m_submissionOperationCondition.notify_all();
}

bool Device::beginLifecycleDrain()noexcept{
    // A public submission hook executes while its submit operation remains leased. Re-entering teardown from that
    // callback must fail before closing the gate, otherwise this thread would wait forever for its own lease.
    if(submissionOperationActiveOnCurrentThread())
        return false;

    UniqueLock<Futex> lock(m_submissionOperationMutex);
    if(m_submissionSuspended.load(MemoryOrder::acquire))
        return false;

    m_submissionSuspended.store(true, MemoryOrder::release);
    m_submissionOperationCondition.wait(lock, [this](){ return m_activeSubmissionOperationCount == 0u; });
    return true;
}

void Device::endLifecycleDrain()noexcept{
    ScopedLock lock(m_submissionOperationMutex);
    NWB_ASSERT(!m_lifecycleDestructionPrepared.load(MemoryOrder::acquire));
    NWB_ASSERT(m_activeSubmissionOperationCount == 0u);
    NWB_ASSERT(m_submissionSuspended.load(MemoryOrder::acquire));
    m_submissionSuspended.store(false, MemoryOrder::release);
}

bool Device::sealLifecycleDrainForDestruction()noexcept{
    ScopedLock lock(m_submissionOperationMutex);
    if(!m_submissionSuspended.load(MemoryOrder::acquire))
        return false;
    NWB_ASSERT(m_activeSubmissionOperationCount == 0u);
    m_lifecycleDestructionPrepared.store(true, MemoryOrder::release);
    return true;
}

QueueSubmissionToken Device::consumeAcquiredImageSemaphore(const VkSemaphore semaphore){
    SubmissionOperationLease submissionOperation(*this);
    if(!submissionOperation.valid() || semaphore == VK_NULL_HANDLE)
        return {};

    const GpuPhysicalQueueId executionQueue = getPrimaryPhysicalQueue(CommandQueue::Graphics);
    Queue* const queue = getQueue(executionQueue);
    if(!queue)
        return {};

    const Queue::SubmissionWait acquireWait{ semaphore, 0u };
    bool submissionAccepted = false;
    const u64 submittedID = queue->submit(
        nullptr,
        0u,
        nullptr,
        &acquireWait,
        1u,
        &submissionAccepted,
        nullptr,
        0u,
        true
    );
    if(!submissionAccepted)
        return {};

    return QueueSubmissionToken{
        .queue = queue->m_queueID,
        .value = submittedID,
        .physicalQueueIndex = executionQueue.index,
        .deviceGeneration = executionQueue.deviceGeneration,
    };
}

bool Device::presentNativeQueue(
    const u32 nativeQueueIndex,
    const VkPresentInfoKHR& presentInfo,
    VkResult& outResult
){
    SubmissionOperationLease submissionOperation(*this);
    outResult = VK_ERROR_UNKNOWN;
    if(!submissionOperation.valid())
        return false;
    if(
        nativeQueueIndex >= m_nativeQueueStates.size()
        || !m_nativeQueueStates[nativeQueueIndex]
        || m_nativeQueueStates[nativeQueueIndex]->queue == VK_NULL_HANDLE
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Presentation references an invalid canonical native queue state."));
        return false;
    }

    NativeQueueState& nativeQueue = *m_nativeQueueStates[nativeQueueIndex];
    ScopedLock hostLock(nativeQueue.hostMutex);
    if(submissionsBlocked())
        return false;
    outResult = m_context.deviceDispatch.vkQueuePresentKHR(nativeQueue.queue, &presentInfo);
    return true;
}

bool Device::registerPhysicalQueue(
    const VulkanPhysicalQueueDesc& desc,
    NativeQueueState& nativeQueue
){
    const u32 queueClassIndex = static_cast<u32>(desc.queueClass);
    const u8 requiredCapabilities = static_cast<u8>(VulkanDetail::DeviceMinimumQueueCapabilities(desc.queueClass));
    const u8 providedCapabilities = static_cast<u8>(desc.capabilities);
    const u8 knownCapabilities = static_cast<u8>(
        static_cast<u8>(GpuQueueCapability::Graphics)
        | static_cast<u8>(GpuQueueCapability::Compute)
        | static_cast<u8>(GpuQueueCapability::Transfer)
    );
    const u8 graphicsOrComputeCapabilities = static_cast<u8>(
        static_cast<u8>(GpuQueueCapability::Graphics)
        | static_cast<u8>(GpuQueueCapability::Compute)
    );
    const bool missingRequiredTransferCapability =
        (providedCapabilities & graphicsOrComputeCapabilities) != 0u
        && (providedCapabilities & static_cast<u8>(GpuQueueCapability::Transfer)) == 0u
    ;
    if(
        nativeQueue.queue == VK_NULL_HANDLE
        || nativeQueue.familyIndex == Limit<u32>::s_Max
        || nativeQueue.queueIndex == Limit<u32>::s_Max
        || queueClassIndex >= static_cast<u32>(CommandQueue::kCount)
        || requiredCapabilities == 0u
        || (providedCapabilities & requiredCapabilities) != requiredCapabilities
        || (providedCapabilities & static_cast<u8>(~knownCapabilities)) != 0u
        || missingRequiredTransferCapability
        || (desc.timestampValidBits != 0u && (desc.timestampValidBits < 36u || desc.timestampValidBits > 64u))
        || m_physicalQueueInfos.size() >= static_cast<usize>(Limit<u16>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Refusing invalid physical queue registry entry."));
        return false;
    }

    for(const GpuPhysicalQueueInfo& existing : m_physicalQueueInfos){
        if(existing.familyIndex == nativeQueue.familyIndex && existing.queueIndex == nativeQueue.queueIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Refusing duplicate physical queue family/index registry entry."));
            return false;
        }
    }
    if(desc.primaryForClass && m_explicitPrimaryQueues[queueClassIndex]){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Refusing duplicate primary physical queue class entry."));
        return false;
    }

    const GpuPhysicalQueueInfo info{
        .id = GpuPhysicalQueueId{
            static_cast<u16>(m_physicalQueueInfos.size()),
            m_deviceGeneration,
        },
        .queueClass = desc.queueClass,
        .capabilities = desc.capabilities,
        .familyIndex = nativeQueue.familyIndex,
        .queueIndex = nativeQueue.queueIndex,
        .timestampValidBits = desc.timestampValidBits,
        .dedicated = desc.dedicated,
    };
    Queue* const queue = NewArenaObject<Queue>(m_context.objectArena, m_context, *this, info, nativeQueue);
    if(!queue)
        return false;
    if(queue->m_trackingSemaphore == VK_NULL_HANDLE){
        DestroyArenaObject(m_context.objectArena, queue);
        return false;
    }

    m_physicalQueueInfos.push_back(info);
    m_physicalQueues.push_back(queue);
    if(desc.primaryForClass){
        m_primaryQueues[queueClassIndex] = queue;
        m_explicitPrimaryQueues[queueClassIndex] = true;
    }
    else if(!m_primaryQueues[queueClassIndex])
        m_primaryQueues[queueClassIndex] = queue;
    return true;
}

void Device::configureLegacyQueueContext(){
    const Queue* const graphicsQueue = m_primaryQueues[static_cast<u32>(CommandQueue::Graphics)];
    const Queue* const computeQueue = m_primaryQueues[static_cast<u32>(CommandQueue::Compute)];
    const Queue* const transferQueue = m_primaryQueues[static_cast<u32>(CommandQueue::Transfer)];

    const auto resolveAuxiliaryFamily = [this](const Queue* const primaryQueue, const CommandQueue::Enum queueClass){
        if(!primaryQueue)
            return s_InvalidQueueFamilyIndex;
        for(const Queue* const physicalQueue : m_physicalQueues){
            if(
                physicalQueue
                && physicalQueue->m_queueID == queueClass
                && physicalQueue->m_queueFamilyIndex != primaryQueue->m_queueFamilyIndex
            )
                return static_cast<i32>(physicalQueue->m_queueFamilyIndex);
        }
        return s_InvalidQueueFamilyIndex;
    };

    m_context.graphicsQueueFamilyIndex = graphicsQueue
        ? static_cast<i32>(graphicsQueue->m_queueFamilyIndex)
        : s_InvalidQueueFamilyIndex
    ;
    m_context.auxiliaryGraphicsQueueFamilyIndex = resolveAuxiliaryFamily(graphicsQueue, CommandQueue::Graphics);
    m_context.asyncComputeQueueFamilyIndex = computeQueue
        ? static_cast<i32>(computeQueue->m_queueFamilyIndex)
        : s_InvalidQueueFamilyIndex
    ;
    m_context.auxiliaryAsyncComputeQueueFamilyIndex = resolveAuxiliaryFamily(computeQueue, CommandQueue::Compute);
    m_context.transferQueueFamilyIndex = transferQueue
        ? static_cast<i32>(transferQueue->m_queueFamilyIndex)
        : s_InvalidQueueFamilyIndex
    ;
    m_context.auxiliaryTransferQueueFamilyIndex = resolveAuxiliaryFamily(transferQueue, CommandQueue::Transfer);
    // Cross-family async Compute remains an explicit resource-sharing capability even though submission routes use
    // queue classes or exact physical queue IDs directly.
    m_context.asyncComputeLaneEnabled = graphicsQueue
        && computeQueue
        && computeQueue->m_queueFamilyIndex != graphicsQueue->m_queueFamilyIndex
    ;
    m_context.transferQueueEnabled = transferQueue
        && (!graphicsQueue || transferQueue->m_queueFamilyIndex != graphicsQueue->m_queueFamilyIndex)
        && (!computeQueue || transferQueue->m_queueFamilyIndex != computeQueue->m_queueFamilyIndex)
    ;
}

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

bool Device::validateSubmissionWaitToken(const QueueSubmissionToken& token)const noexcept{
    if(
        !token.valid()
        || !token.hasPhysicalQueueIdentity()
        || !matchesPhysicalQueueIdentity(token.queue, token.physicalQueueIndex, token.deviceGeneration)
        || token.physicalQueueIndex >= m_physicalQueues.size()
    )
        return false;

    Queue* const producerQueue = m_physicalQueues[token.physicalQueueIndex];
    if(
        !producerQueue
        || producerQueue->m_queueID != token.queue
        || !token.matchesPhysicalQueue(
            producerQueue->m_physicalQueue.index,
            producerQueue->m_physicalQueue.deviceGeneration
        )
    )
        return false;

    ScopedLock producerLock(producerQueue->m_mutex);
    return producerQueue->m_trackingSemaphore != VK_NULL_HANDLE
        && token.value <= producerQueue->m_lastSubmittedID
    ;
}

bool Device::waitForSubmissionToken(const QueueSubmissionToken& token)noexcept{
    if(
        !token.valid()
        || !token.hasPhysicalQueueIdentity()
        || !matchesPhysicalQueueIdentity(token.queue, token.physicalQueueIndex, token.deviceGeneration)
        || token.physicalQueueIndex >= m_physicalQueues.size()
    )
        return false;

    Queue* const producerQueue = m_physicalQueues[token.physicalQueueIndex];
    if(!producerQueue)
        return false;

    ScopedLock producerLock(producerQueue->m_mutex);
    if(
        producerQueue->m_trackingSemaphore == VK_NULL_HANDLE
        || producerQueue->m_queueID != token.queue
        || token.value > producerQueue->m_lastSubmittedID
    )
        return false;
    if(token.value <= producerQueue->m_lastFinishedID)
        return true;

    auto waitInfo = VulkanDetail::MakeVkStruct<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
    waitInfo.semaphoreCount = 1u;
    waitInfo.pSemaphores = &producerQueue->m_trackingSemaphore;
    waitInfo.pValues = &token.value;
    const VkResult result = m_context.deviceDispatch.vkWaitSemaphores(m_context.device, &waitInfo, UINT64_MAX);
    if(result != VK_SUCCESS){
        if(result == VK_ERROR_DEVICE_LOST)
            captureDeviceLoss("submission token wait");
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to wait for submission token: {}"), ResultToString(result));
        return false;
    }

    producerQueue->m_lastFinishedID = Max(producerQueue->m_lastFinishedID, token.value);
    producerQueue->collectCompletedCommandBuffers();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandListHandle Device::createCommandList(const CommandListParameters& params){
    CommandListParameters resolvedParams = params;
    if(resolvedParams.recordingWorkerIndex == 0u)
        resolvedParams.recordingWorkerDomain = 0u;
    Queue* queue = nullptr;
    if(resolvedParams.physicalQueue.valid()){
        queue = getQueue(resolvedParams.physicalQueue);
        if(!queue){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create command list: requested physical queue is not available"));
            return nullptr;
        }
        resolvedParams.queueType = queue->m_queueID;
    }
    else{
        queue = getQueue(resolvedParams.queueType);
        if(!queue){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create command list: requested queue is not available"));
            return nullptr;
        }
        resolvedParams.physicalQueue = queue->m_physicalQueue;
    }

    auto* cmdList = NewArenaObject<CommandList>(m_context.objectArena, *this, resolvedParams);
    return CommandListHandle(cmdList, CommandListHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

u64 Device::executeCommandLists(
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const CommandQueue::Enum executionQueue,
    bool* const outCommandListsSubmitted
){
    return executeCommandLists(
        pCommandLists,
        numCommandLists,
        getPrimaryPhysicalQueue(executionQueue),
        outCommandListsSubmitted
    );
}

u64 Device::executeCommandLists(
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const GpuPhysicalQueueId& executionQueue,
    bool* const outCommandListsSubmitted
){
    SubmissionOperationLease submissionOperation(*this);
    if(outCommandListsSubmitted)
        *outCommandListsSubmitted = false;

    if(!submissionOperation.valid())
        return 0u;

    Queue* queue = getQueue(executionQueue);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: requested queue is not available"));
        return 0;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CommandListExecuteArena);
    Vector<Queue::SubmissionCommandListIdentity, Alloc::ScratchArena> expectedCommandLists{scratchArena};
    bool hasSubmittedOwner = false;
    if(pCommandLists && numCommandLists > 0){
        expectedCommandLists.reserve(numCommandLists);
        for(usize i = 0; i < numCommandLists; ++i){
            CommandList* const commandList = pCommandLists[i];
            const TrackedCommandBufferPtr owner = commandList ? commandList->m_currentCmdBuf : nullptr;
            expectedCommandLists.push_back(Queue::SubmissionCommandListIdentity{
                .owner = owner,
                .recordingLeaseSerial = commandList ? commandList->recordingLeaseSerial() : 0u,
                .nativeRecordingID = commandList ? commandList->m_nativeRecordingID : 0u,
                .recordingWorkerDomain = commandList ? commandList->m_creationDesc.recordingWorkerDomain : 0u,
                .recordingWorkerIndex = commandList ? commandList->m_creationDesc.recordingWorkerIndex : 0u,
            });
            if(owner)
                hasSubmittedOwner = true;
        }
    }

    bool submissionAccepted = false;
    const u64 submittedID = queue->submit(
        pCommandLists,
        numCommandLists,
        expectedCommandLists.empty() ? nullptr : expectedCommandLists.data(),
        nullptr,
        0u,
        &submissionAccepted
    );
    if(outCommandListsSubmitted)
        *outCommandListsSubmitted = submissionAccepted && hasSubmittedOwner;

    if(!expectedCommandLists.empty()){
        if(submissionAccepted){
            m_uploadManager.submitChunks(
                executionQueue,
                submittedID,
                expectedCommandLists.data(),
                expectedCommandLists.size()
            );
            m_scratchManager.submitChunks(
                executionQueue,
                submittedID,
                expectedCommandLists.data(),
                expectedCommandLists.size()
            );
        }
        else{
            const auto ownerStillRecorded = [&](const Queue::SubmissionCommandListIdentity& expected) -> bool {
                if(!expected.owner || expected.nativeRecordingID == 0u || !pCommandLists)
                    return false;
                for(usize i = 0; i < numCommandLists; ++i){
                    auto* cmdList = pCommandLists[i];
                    if(
                        cmdList
                        && cmdList->m_currentCmdBuf.get() == expected.owner.get()
                        && cmdList->m_nativeRecordingID == expected.nativeRecordingID
                    )
                        return true;
                }
                return false;
            };

            const u64 reusableVersion = queueGetCompletedInstance(executionQueue);
            for(const Queue::SubmissionCommandListIdentity& expected : expectedCommandLists){
                if(ownerStillRecorded(expected))
                    continue;
                m_uploadManager.discardChunks(
                    executionQueue,
                    expected.owner.get(),
                    expected.nativeRecordingID,
                    reusableVersion
                );
                m_scratchManager.discardChunks(
                    executionQueue,
                    expected.owner.get(),
                    expected.nativeRecordingID,
                    reusableVersion
                );
            }
        }
    }

    return submittedID;
}

QueueSubmissionToken Device::executeCommandLists(
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const CommandQueue::Enum executionQueue,
    const QueueSubmissionDesc& submitDesc
){
    return executeCommandLists(
        pCommandLists,
        numCommandLists,
        getPrimaryPhysicalQueue(executionQueue),
        submitDesc
    );
}

QueueSubmissionToken Device::executeCommandLists(
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const GpuPhysicalQueueId& executionQueue,
    const QueueSubmissionDesc& submitDesc
){
    SubmissionOperationLease submissionOperation(*this);
    if(!submissionOperation.valid())
        return {};

    Queue* const queue = getQueue(executionQueue);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: requested queue is not available"));
        return {};
    }

    if(numCommandLists > 0u && !pCommandLists){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list array is null"));
        return {};
    }
    if(numCommandLists > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list count exceeds Vulkan limit"));
        return {};
    }
    for(usize i = 0u; i < numCommandLists; ++i){
        CommandList* const commandList = pCommandLists[i];
        for(usize previous = 0u; previous < i; ++previous){
            if(pCommandLists[previous] == commandList){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list {} is duplicated"), i);
                return {};
            }
        }
        if(
            !commandList
            || &commandList->m_device != this
            || !commandList->hasCommandBuffer()
        ){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list {} is null, foreign, or has no native command buffer"), i);
            return {};
        }
        if(commandList->commandRecordingFailed()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list {} has a sticky native recording failure"), i);
            return {};
        }
        if(commandList->m_isRecording){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list {} is still recording"), i);
            return {};
        }
        if(!commandList->matchesSubmissionLease(executionQueue, queue->m_queueID)){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Command list {} lease provenance does not match execution queue")
                , i
            );
            return {};
        }
    }
    for(usize i = 0u; i < numCommandLists; ++i){
        if(!pCommandLists[i]->validateTrackedResourcesReadyForSubmission())
            return {};
    }

    if(submitDesc.waitTokenCount > 0u && !submitDesc.waitTokens){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: submission wait token array is null"));
        return {};
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CommandListExecuteArena);
    Vector<Queue::SubmissionWait, Alloc::ScratchArena> localWaits{scratchArena};
    if(submitDesc.waitTokenCount > 0u){
        localWaits.reserve(submitDesc.waitTokenCount);
        for(usize i = 0u; i < submitDesc.waitTokenCount; ++i){
            const QueueSubmissionToken& token = submitDesc.waitTokens[i];
            if(!validateSubmissionWaitToken(token)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: dependency token is invalid, unavailable, or unsignalled"));
                return {};
            }

            Queue* const producerQueue = m_physicalQueues[token.physicalQueueIndex];
            NWB_ASSERT(producerQueue);

            // Queue order already covers same-queue dependencies.
            if(token.matchesPhysicalQueue(executionQueue.index, executionQueue.deviceGeneration))
                continue;

            // Collapse same-semaphore waits to their largest timeline value.
            bool merged = false;
            for(Queue::SubmissionWait& wait : localWaits){
                if(wait.semaphore != producerQueue->m_trackingSemaphore)
                    continue;

                wait.value = Max(wait.value, token.value);
                merged = true;
                break;
            }
            if(!merged)
                localWaits.push_back(Queue::SubmissionWait{ producerQueue->m_trackingSemaphore, token.value });
        }
    }

    Vector<Queue::SubmissionCommandListIdentity, Alloc::ScratchArena> expectedCommandLists{scratchArena};
    if(pCommandLists && numCommandLists > 0u){
        expectedCommandLists.reserve(numCommandLists);
        for(usize i = 0u; i < numCommandLists; ++i){
            CommandList* const commandList = pCommandLists[i];
            const TrackedCommandBufferPtr owner = commandList->m_currentCmdBuf;
            expectedCommandLists.push_back(Queue::SubmissionCommandListIdentity{
                .owner = owner,
                .recordingLeaseSerial = commandList->recordingLeaseSerial(),
                .nativeRecordingID = commandList->m_nativeRecordingID,
                .recordingWorkerDomain = commandList->m_creationDesc.recordingWorkerDomain,
                .recordingWorkerIndex = commandList->m_creationDesc.recordingWorkerIndex,
            });
        }
    }

    // The hook runs only after this submission's queue and timeline waits validate. Its native signal is passed as
    // submission-local data into Queue::submit, so a concurrent submit cannot consume the presentation semaphore.
    Queue::SubmissionSignal hookSignal = {};
    const Queue::SubmissionSignal* localSignals = nullptr;
    usize localSignalCount = 0u;
    __hidden_vulkan_device_queue::ScopedSubmissionHookResolution hookResolution(submitDesc.preSubmitHook);
    if(submitDesc.preSubmitHook.valid()){
        QueueSubmissionNativeSignal nativeSignal;
        const bool hookPrepared = submitDesc.preSubmitHook.invoke(
                submitDesc.preSubmitHook.context,
                submitDesc.preSubmitHook.identity,
                executionQueue,
                nativeSignal
        );
        if(hookPrepared)
            hookResolution.arm();
        if(!hookPrepared || !nativeSignal.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to prepare exact queue submission hook"));
            return {};
        }

        hookSignal.semaphore = __hidden_vulkan_device_queue::DecodeSubmissionNativeSemaphore(nativeSignal.semaphore);
        hookSignal.value = nativeSignal.value;
        if(hookSignal.semaphore == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exact queue submission hook returned an invalid native semaphore"));
            return {};
        }
        localSignals = &hookSignal;
        localSignalCount = 1u;
    }

    bool submissionAccepted = false;
    const u64 submittedID = queue->submit(
        pCommandLists,
        numCommandLists,
        expectedCommandLists.empty() ? nullptr : expectedCommandLists.data(),
        localWaits.empty() ? nullptr : localWaits.data(),
        localWaits.size(),
        &submissionAccepted,
        localSignals,
        localSignalCount,
        submitDesc.forceNativeSubmission
    );
    const QueueSubmissionToken submissionToken = submissionAccepted
        ? QueueSubmissionToken{
            .queue = queue->m_queueID,
            .value = submittedID,
            .physicalQueueIndex = executionQueue.index,
            .deviceGeneration = executionQueue.deviceGeneration,
        }
        : QueueSubmissionToken{}
    ;
    if(!hookResolution.resolve(submissionToken))
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Exact queue submission hook rejected its resolution token"));

    if(!expectedCommandLists.empty()){
        if(submissionAccepted){
            m_uploadManager.submitChunks(
                executionQueue,
                submittedID,
                expectedCommandLists.data(),
                expectedCommandLists.size()
            );
            m_scratchManager.submitChunks(
                executionQueue,
                submittedID,
                expectedCommandLists.data(),
                expectedCommandLists.size()
            );
        }
        else{
            const auto ownerStillRecorded = [&](const Queue::SubmissionCommandListIdentity& expected) -> bool {
                if(!expected.owner || expected.nativeRecordingID == 0u || !pCommandLists)
                    return false;
                for(usize i = 0u; i < numCommandLists; ++i){
                    CommandList* const cmdList = pCommandLists[i];
                    if(
                        cmdList
                        && cmdList->m_currentCmdBuf.get() == expected.owner.get()
                        && cmdList->m_nativeRecordingID == expected.nativeRecordingID
                    )
                        return true;
                }
                return false;
            };

            const u64 reusableVersion = queueGetCompletedInstance(executionQueue);
            for(const Queue::SubmissionCommandListIdentity& expected : expectedCommandLists){
                if(ownerStillRecorded(expected))
                    continue;
                m_uploadManager.discardChunks(
                    executionQueue,
                    expected.owner.get(),
                    expected.nativeRecordingID,
                    reusableVersion
                );
                m_scratchManager.discardChunks(
                    executionQueue,
                    expected.owner.get(),
                    expected.nativeRecordingID,
                    reusableVersion
                );
            }
        }
    }

    if(!submissionAccepted)
        return {};

    return submissionToken;
}

u32 Device::getQueueFamilyIndex(const CommandQueue::Enum queueType)const{
    return getQueueFamilyIndex(getPrimaryPhysicalQueue(queueType));
}

u32 Device::getQueueFamilyIndex(const GpuPhysicalQueueId& queue)const{
    const GpuPhysicalQueueInfo* const info = getPhysicalQueueInfo(queue);
    return info ? info->familyIndex : VK_QUEUE_FAMILY_IGNORED;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

