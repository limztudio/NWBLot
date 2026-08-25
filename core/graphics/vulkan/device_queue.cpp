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


#if !defined(NWB_FINAL)
[[nodiscard]] static Object EncodeSubmissionNativeSemaphore(const VkSemaphore semaphore)noexcept{
#if VK_USE_64_BIT_PTR_DEFINES
    return Object(static_cast<void*>(semaphore));
#else
    return Object(static_cast<u64>(semaphore));
#endif
}
#endif

[[nodiscard]] static VkSemaphore DecodeSubmissionNativeSemaphore(const Object& semaphore)noexcept{
#if VK_USE_64_BIT_PTR_DEFINES
    return static_cast<VkSemaphore>(semaphore.pointer);
#else
    return static_cast<VkSemaphore>(semaphore.integer);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Device::registerPhysicalQueue(const VulkanPhysicalQueueDesc& desc){
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
        desc.queue == VK_NULL_HANDLE
        || queueClassIndex >= static_cast<u32>(CommandQueue::kCount)
        || desc.familyIndex == Limit<u32>::s_Max
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
        if(existing.familyIndex == desc.familyIndex && existing.queueIndex == desc.queueIndex){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Refusing duplicate physical queue family/index registry entry."));
            return false;
        }
    }

    const GpuPhysicalQueueInfo info{
        .id = GpuPhysicalQueueId{
            static_cast<u16>(m_physicalQueueInfos.size()),
            m_deviceGeneration,
        },
        .queueClass = desc.queueClass,
        .capabilities = desc.capabilities,
        .familyIndex = desc.familyIndex,
        .queueIndex = desc.queueIndex,
        .timestampValidBits = desc.timestampValidBits,
        .dedicated = desc.dedicated,
    };
    Queue* const queue = NewArenaObject<Queue>(m_context.objectArena, m_context, *this, info, desc.queue);
    if(!queue)
        return false;

    m_physicalQueueInfos.push_back(info);
    m_physicalQueues.push_back(queue);
    if(desc.primaryForClass || !m_primaryQueues[queueClassIndex])
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
    // RenderLane remains a compatibility faÃ§ade: it may target Compute only when the primary Compute transport is
    // a separate family. Graph packets bypass this policy and submit through their exact physical queue ID.
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

#if !defined(NWB_FINAL)

bool Device::createSubmissionSignalForTesting(QueueSubmissionNativeSignal& outSignal){
    outSignal = {};
    auto semaphoreInfo = VulkanDetail::MakeVkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    VkSemaphore semaphore = VK_NULL_HANDLE;
    const VkResult res = vkCreateSemaphore(m_context.device, &semaphoreInfo, m_context.allocationCallbacks, &semaphore);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create test submission semaphore: {}"), ResultToString(res));
        return false;
    }

    outSignal.semaphore = __hidden_vulkan_device_queue::EncodeSubmissionNativeSemaphore(semaphore);
    outSignal.value = 0u;
    return true;
}

bool Device::createSubmissionTimelineForTesting(Queue::SubmissionWait& outWait){
    outWait = {};
    auto timelineInfo = VulkanDetail::MakeVkStruct<VkSemaphoreTypeCreateInfo>(
        VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO
    );
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0u;
    auto semaphoreInfo = VulkanDetail::MakeVkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    semaphoreInfo.pNext = &timelineInfo;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    const VkResult res = vkCreateSemaphore(m_context.device, &semaphoreInfo, m_context.allocationCallbacks, &semaphore);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create test submission timeline: {}"), ResultToString(res));
        return false;
    }

    outWait.semaphore = semaphore;
    outWait.value = 1u;
    return true;
}

void Device::destroySubmissionSignalForTesting(QueueSubmissionNativeSignal& signal){
    const VkSemaphore semaphore = __hidden_vulkan_device_queue::DecodeSubmissionNativeSemaphore(signal.semaphore);
    if(semaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(m_context.device, semaphore, m_context.allocationCallbacks);
    signal = {};
}

bool Device::signalSubmissionTimelineForTesting(const Queue::SubmissionWait& wait){
    auto signalInfo = VulkanDetail::MakeVkStruct<VkSemaphoreSignalInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO);
    signalInfo.semaphore = wait.semaphore;
    signalInfo.value = wait.value;
    const VkResult res = vkSignalSemaphore(m_context.device, &signalInfo);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to signal test submission timeline: {}"), ResultToString(res));
        return false;
    }
    return true;
}

void Device::destroySubmissionTimelineForTesting(Queue::SubmissionWait& wait){
    if(wait.semaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(m_context.device, wait.semaphore, m_context.allocationCallbacks);
    wait = {};
}

void Device::rejectNextSubmissionForTesting(const CommandQueue::Enum queue){
    const u32 index = static_cast<u32>(queue);
    if(index >= static_cast<u32>(CommandQueue::kCount))
        return;

    m_submissionRejectionsForTesting[index].fetch_add(1u, MemoryOrder::relaxed);
}

void Device::clearSubmissionRejectionsForTesting(){
    for(Atomic<u32>& count : m_submissionRejectionsForTesting)
        count.store(0u, MemoryOrder::relaxed);
}

void Device::clearSubmissionWaitTokensForTesting(){
    m_submissionWaitCaptureArmedForTesting.store(false, MemoryOrder::release);
    ScopedLock lock(m_submissionWaitTokensForTestingMutex);
    m_submissionWaitQueueForTesting = {};
    m_submissionWaitTokensForTesting.clear();
}

void Device::armSubmissionWaitCaptureForTesting(){
    m_submissionWaitCaptureArmedForTesting.store(true, MemoryOrder::release);
}

usize Device::lastSubmissionWaitTokenCountForTesting(
    const GpuPhysicalQueueId& executionQueue
)const noexcept{
    ScopedLock lock(m_submissionWaitTokensForTestingMutex);
    return executionQueue == m_submissionWaitQueueForTesting
        ? m_submissionWaitTokensForTesting.size()
        : 0u
    ;
}

QueueSubmissionToken Device::lastSubmissionWaitTokenForTesting(
    const GpuPhysicalQueueId& executionQueue,
    const usize index
)const noexcept{
    ScopedLock lock(m_submissionWaitTokensForTestingMutex);
    if(executionQueue != m_submissionWaitQueueForTesting || index >= m_submissionWaitTokensForTesting.size())
        return {};
    return m_submissionWaitTokensForTesting[index];
}

bool Device::consumeSubmissionRejectionForTesting(const CommandQueue::Enum queue){
    const u32 index = static_cast<u32>(queue);
    if(index >= static_cast<u32>(CommandQueue::kCount))
        return false;

    Atomic<u32>& count = m_submissionRejectionsForTesting[index];
    u32 pending = count.load(MemoryOrder::relaxed);
    while(pending > 0u){
        if(count.compare_exchange_weak(pending, pending - 1u, MemoryOrder::relaxed))
            return true;
    }
    return false;
}

void Device::captureSubmissionWaitTokensForTesting(
    const GpuPhysicalQueueId& executionQueue,
    const QueueSubmissionToken* const waitTokens,
    const usize waitTokenCount
){
    if(!m_submissionWaitCaptureArmedForTesting.exchange(false, MemoryOrder::acq_rel))
        return;
    ScopedLock lock(m_submissionWaitTokensForTestingMutex);
    m_submissionWaitQueueForTesting = executionQueue;
    if(!waitTokens || waitTokenCount == 0u){
        m_submissionWaitTokensForTesting.clear();
        return;
    }
    m_submissionWaitTokensForTesting.assign(waitTokens, waitTokens + waitTokenCount);
}

#endif


CommandListHandle Device::createCommandList(const CommandListParameters& params){
    CommandListParameters resolvedParams = params;
    if(resolvedParams.recordingWorkerIndex == 0u)
        resolvedParams.recordingWorkerDomain = 0u;
    if(resolvedParams.resolveRenderLane){
        resolvedParams.queueType = resolveRenderLane(resolvedParams.renderLane);
        resolvedParams.resolveRenderLane = false;
    }

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
    if(outCommandListsSubmitted)
        *outCommandListsSubmitted = false;

    // Device loss makes recovery submissions unsafe.
    if(isDeviceLost())
        return 0u;

    Queue* queue = getQueue(executionQueue);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: requested queue is not available"));
        return 0;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CommandListExecuteArena);
    Vector<TrackedCommandBuffer*, Alloc::ScratchArena> submittedOwners{scratchArena};
    Vector<Queue::SubmissionCommandListIdentity, Alloc::ScratchArena> expectedCommandLists{scratchArena};
    if(pCommandLists && numCommandLists > 0){
        submittedOwners.reserve(numCommandLists);
        expectedCommandLists.reserve(numCommandLists);
        for(usize i = 0; i < numCommandLists; ++i){
            CommandList* const commandList = pCommandLists[i];
            const TrackedCommandBufferPtr owner = commandList ? commandList->m_currentCmdBuf : nullptr;
            expectedCommandLists.push_back(Queue::SubmissionCommandListIdentity{
                .owner = owner,
                .recordingLeaseSerial = commandList ? commandList->recordingLeaseSerial() : 0u,
            });
            if(owner)
                submittedOwners.push_back(owner.get());
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
        *outCommandListsSubmitted = submissionAccepted && !submittedOwners.empty();

    if(!submittedOwners.empty()){
        if(submissionAccepted){
            m_uploadManager.submitChunks(executionQueue, submittedID, submittedOwners.data(), submittedOwners.size());
            m_scratchManager.submitChunks(executionQueue, submittedID, submittedOwners.data(), submittedOwners.size());
        }
        else{
            const auto ownerStillRecorded = [&](TrackedCommandBuffer* owner) -> bool {
                if(!owner || !pCommandLists)
                    return false;
                for(usize i = 0; i < numCommandLists; ++i){
                    auto* cmdList = pCommandLists[i];
                    if(cmdList && cmdList->m_currentCmdBuf.get() == owner)
                        return true;
                }
                return false;
            };

            const u64 reusableVersion = queueGetCompletedInstance(executionQueue);
            for(TrackedCommandBuffer* owner : submittedOwners){
                if(ownerStillRecorded(owner))
                    continue;
                m_uploadManager.discardChunks(executionQueue, owner, reusableVersion);
                m_scratchManager.discardChunks(executionQueue, owner, reusableVersion);
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
    // Do not submit or wait after terminal device loss.
    if(isDeviceLost())
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
        if(!VulkanDetail::SubmissionCommandListMatchesExecutionQueue(commandList->m_desc, executionQueue, queue->m_queueID)){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list {} physical queue does not match the execution queue"), i);
            return {};
        }
    }
    for(usize i = 0u; i < numCommandLists; ++i){
        if(!pCommandLists[i]->validateTrackedTexturesReadyForSubmission())
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

    Vector<TrackedCommandBuffer*, Alloc::ScratchArena> submittedOwners{scratchArena};
    Vector<Queue::SubmissionCommandListIdentity, Alloc::ScratchArena> expectedCommandLists{scratchArena};
    if(pCommandLists && numCommandLists > 0u){
        submittedOwners.reserve(numCommandLists);
        expectedCommandLists.reserve(numCommandLists);
        for(usize i = 0u; i < numCommandLists; ++i){
            CommandList* const commandList = pCommandLists[i];
            const TrackedCommandBufferPtr owner = commandList->m_currentCmdBuf;
            expectedCommandLists.push_back(Queue::SubmissionCommandListIdentity{
                .owner = owner,
                .recordingLeaseSerial = commandList->recordingLeaseSerial(),
            });
            submittedOwners.push_back(owner.get());
        }
    }

    // The hook runs only after this submission's queue and timeline waits validate. Its native signal is passed as
    // submission-local data into Queue::submit, so a concurrent submit cannot consume the presentation semaphore.
    Queue::SubmissionSignal hookSignal = {};
    const Queue::SubmissionSignal* localSignals = nullptr;
    usize localSignalCount = 0u;
    if(submitDesc.preSubmitHook.valid()){
        QueueSubmissionNativeSignal nativeSignal;
        if(
            !submitDesc.preSubmitHook.invoke(
                submitDesc.preSubmitHook.context,
                executionQueue,
                nativeSignal
            )
            || !nativeSignal.valid()
        ){
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

#if !defined(NWB_FINAL)
    // An explicitly armed test seam retains the uncollapsed graph/runtime token edge list at the final Device
    // boundary. This is intentionally after every validation branch so tests can distinguish an accepted submit
    // from an invalid descriptor without adding recurring debug-submit allocations.
    captureSubmissionWaitTokensForTesting(executionQueue, submitDesc.waitTokens, submitDesc.waitTokenCount);
#endif

    bool submissionAccepted = false;
    const u64 submittedID = queue->submit(
        pCommandLists,
        numCommandLists,
        expectedCommandLists.empty() ? nullptr : expectedCommandLists.data(),
        localWaits.empty() ? nullptr : localWaits.data(),
        localWaits.size(),
        &submissionAccepted,
        localSignals,
        localSignalCount
    );

    if(!submittedOwners.empty()){
        if(submissionAccepted){
            m_uploadManager.submitChunks(executionQueue, submittedID, submittedOwners.data(), submittedOwners.size());
            m_scratchManager.submitChunks(executionQueue, submittedID, submittedOwners.data(), submittedOwners.size());
        }
        else{
            const auto ownerStillRecorded = [&](TrackedCommandBuffer* owner) -> bool {
                if(!owner || !pCommandLists)
                    return false;
                for(usize i = 0u; i < numCommandLists; ++i){
                    CommandList* const cmdList = pCommandLists[i];
                    if(cmdList && cmdList->m_currentCmdBuf.get() == owner)
                        return true;
                }
                return false;
            };

            const u64 reusableVersion = queueGetCompletedInstance(executionQueue);
            for(TrackedCommandBuffer* owner : submittedOwners){
                if(ownerStillRecorded(owner))
                    continue;
                m_uploadManager.discardChunks(executionQueue, owner, reusableVersion);
                m_scratchManager.discardChunks(executionQueue, owner, reusableVersion);
            }
        }
    }

    if(!submissionAccepted)
        return {};

    return QueueSubmissionToken{
        .queue = queue->m_queueID,
        .value = submittedID,
        .physicalQueueIndex = executionQueue.index,
        .deviceGeneration = executionQueue.deviceGeneration,
    };
}

QueueSubmissionToken Device::executeCommandLists(
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const RenderLane::Enum executionLane,
    const QueueSubmissionDesc& submitDesc
){
    return executeCommandLists(pCommandLists, numCommandLists, resolveRenderLane(executionLane), submitDesc);
}

CommandQueue::Enum Device::resolveRenderLane(const RenderLane::Enum lane)const{
    switch(lane){
    case RenderLane::Graphics:
        return CommandQueue::Graphics;
    case RenderLane::AsyncCompute:
        return isRenderLaneDedicated(lane) ? CommandQueue::Compute : CommandQueue::Graphics;
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: invalid render lane"));
        return CommandQueue::Graphics;
    }
}

bool Device::isRenderLaneDedicated(const RenderLane::Enum lane)const{
    return
        lane == RenderLane::AsyncCompute
        && m_context.asyncComputeLaneEnabled
        && m_primaryQueues[static_cast<u32>(CommandQueue::Compute)] != nullptr
    ;
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

