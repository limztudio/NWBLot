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


namespace __hidden_vulkan_device_queue{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static VkSemaphore DecodeSubmissionNativeSemaphore(const Object& semaphore)noexcept{
#if VK_USE_64_BIT_PTR_DEFINES
    return static_cast<VkSemaphore>(semaphore.pointer());
#else
    return static_cast<VkSemaphore>(semaphore.integer);
#endif
}

class ScopedSubmissionHookResolution final : NoCopy{
public:
    explicit ScopedSubmissionHookResolution(const QueueSubmissionPreSubmitHook& hook)noexcept
        : m_hook(hook)
    {}
    ~ScopedSubmissionHookResolution()noexcept{
        resolve({});
    }


public:
    void arm()noexcept{ m_armed = m_hook.resolved != nullptr; }
    void resolve(const QueueSubmissionToken& token)noexcept{
        if(!m_armed)
            return;
        m_armed = false;
        if(!m_hook.resolved(m_hook.context, m_hook.identity, token))
            TerminateInvariant();
    }


private:
    QueueSubmissionPreSubmitHook m_hook;
    bool m_armed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    constexpr u32 s_MinTimestampValidBits = 36u;
    constexpr u32 s_MaxTimestampValidBits = 64u;
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
        || (desc.timestampValidBits != 0u && (desc.timestampValidBits < s_MinTimestampValidBits || desc.timestampValidBits > s_MaxTimestampValidBits))
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
        .familyIndex = nativeQueue.familyIndex,
        .queueIndex = nativeQueue.queueIndex,
        .timestampValidBits = desc.timestampValidBits,
        .id = GpuPhysicalQueueId{
            .index = static_cast<u16>(m_physicalQueueInfos.size()),
            .deviceGeneration = m_deviceGeneration,
        },
        .queueClass = desc.queueClass,
        .capabilities = desc.capabilities,
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

void Device::configureQueueSharingContext(){
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
    // Cross-family async Compute stays an explicit sharing capability.
    m_context.asyncComputeLaneEnabled = graphicsQueue
        && computeQueue
        && computeQueue->m_queueFamilyIndex != graphicsQueue->m_queueFamilyIndex
    ;
    m_context.transferQueueEnabled = transferQueue
        && (!graphicsQueue || transferQueue->m_queueFamilyIndex != graphicsQueue->m_queueFamilyIndex)
        && (!computeQueue || transferQueue->m_queueFamilyIndex != computeQueue->m_queueFamilyIndex)
    ;
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

    NothrowScopedLock producerLock(producerQueue->m_mutex);
    return producerQueue->m_trackingSemaphore != VK_NULL_HANDLE
        && token.value <= producerQueue->m_lastSubmittedID
    ;
}

bool Device::waitForSubmissionToken(const QueueSubmissionToken& token){
    return waitForSubmissionTokenInternal(token, DeviceLossDiagnosticPolicy::Capture);
}

bool Device::waitForSubmissionTokenInternal(
    const QueueSubmissionToken& token,
    const DeviceLossDiagnosticPolicy deviceLossDiagnosticPolicy
){
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

    VkResult result = VK_SUCCESS;
    {
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
        result = m_context.deviceDispatch.vkWaitSemaphores(m_context.device, &waitInfo, UINT64_MAX);
        if(result == VK_SUCCESS){
            producerQueue->m_lastFinishedID = Max(producerQueue->m_lastFinishedID, token.value);
            producerQueue->collectCompletedCommandBuffers();
        }
        else if(result == VK_ERROR_DEVICE_LOST)
            markDeviceLost();
    }

    if(result == VK_ERROR_DEVICE_LOST && deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
        captureDeviceLoss("submission token wait");
    if(result != VK_SUCCESS && deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to wait for submission token: {}"), ResultToString(result));
    return result == VK_SUCCESS;
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
    return CommandListHandle(cmdList, CommandListHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
}

Expected<QueueSubmissionReceipt> Device::executeCommandLists(
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

Expected<QueueSubmissionReceipt> Device::executeCommandLists(
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const GpuPhysicalQueueId& executionQueue,
    const QueueSubmissionDesc& submitDesc
){
    return executeCommandListsInternal(pCommandLists, numCommandLists, executionQueue, submitDesc, false);
}

void Device::prepareSubmissionCommandListWorkspaceLocked(
    Queue& queue,
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const bool graphSubmissionAuthorized
){
    auto& expectedCommandLists = queue.m_executeExpectedCommandLists;
    auto& submittedOwners = queue.m_executeSubmittedOwners;
    expectedCommandLists.clear();

    if(pCommandLists && numCommandLists > 0u){
        expectedCommandLists.reserve(numCommandLists);
        for(usize i = 0u; i < numCommandLists; ++i){
            CommandList* const commandList = pCommandLists[i];
            TrackedCommandBuffer* const owner = commandList->m_currentCmdBuf.get();
            expectedCommandLists.push_back(Queue::SubmissionCommandListIdentity{
                .owner = owner,
                .recordingLeaseSerial = commandList->m_recordingLeaseSerial,
                .nativeRecordingID = commandList->m_nativeRecordingID,
                .recordingWorkerDomain = commandList->m_creationDesc.recordingWorkerDomain,
                .graphRecordingOwnershipSerial = graphSubmissionAuthorized
                    ? commandList->m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire)
                    : 0u,
                .recordingWorkerIndex = commandList->m_creationDesc.recordingWorkerIndex,
                .graphSubmissionAuthorized = graphSubmissionAuthorized,
            });
        }
    }

    submittedOwners.prepare(expectedCommandLists.size());
    if(submittedOwners.indexed()){
        for(const Queue::SubmissionCommandListIdentity& expected : expectedCommandLists){
            if(expected.owner)
                submittedOwners.add(*expected.owner, expected.nativeRecordingID);
        }
    }

}

void Device::finalizeSubmissionCommandListResourcesLocked(
    Queue& queue,
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const GpuPhysicalQueueId& executionQueue,
    const u64 submittedID,
    const bool submissionAccepted
){
    const auto& expectedCommandLists = queue.m_executeExpectedCommandLists;
    if(expectedCommandLists.empty())
        return;

    if(submissionAccepted){
        m_uploadManager.submitChunks(
            executionQueue,
            submittedID,
            expectedCommandLists.data(),
            expectedCommandLists.size(),
            queue.m_executeSubmittedOwners
        );
        m_scratchManager.submitChunks(
            executionQueue,
            submittedID,
            expectedCommandLists.data(),
            expectedCommandLists.size(),
            queue.m_executeSubmittedOwners
        );
        return;
    }

    const auto ownerStillRecorded = [&](const Queue::SubmissionCommandListIdentity& expected) -> bool {
        if(!expected.owner || expected.nativeRecordingID == 0u || !pCommandLists)
            return false;
        for(usize i = 0u; i < numCommandLists; ++i){
            CommandList* const commandList = pCommandLists[i];
            if(
                commandList
                && commandList->m_currentCmdBuf.get() == expected.owner
                && commandList->m_nativeRecordingID == expected.nativeRecordingID
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
            expected.owner,
            expected.nativeRecordingID,
            reusableVersion
        );
        m_scratchManager.discardChunks(
            executionQueue,
            expected.owner,
            expected.nativeRecordingID,
            reusableVersion
        );
    }
}

Expected<QueueSubmissionReceipt> Device::executeGraphCommandLists(
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const GpuPhysicalQueueId& executionQueue,
    const QueueSubmissionDesc& submitDesc
){
    return executeCommandListsInternal(pCommandLists, numCommandLists, executionQueue, submitDesc, true);
}

Expected<QueueSubmissionReceipt> Device::executeCommandListsInternal(
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const GpuPhysicalQueueId& executionQueue,
    const QueueSubmissionDesc& submitDesc,
    const bool graphSubmissionAuthorized,
    const DeviceLossDiagnosticPolicy deviceLossDiagnosticPolicy
){
    SubmissionOperationLease submissionOperation(*this);
    if(!submissionOperation.valid())
        return MakeUnexpected(Failure{});

    Queue* const queue = getQueue(executionQueue);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: requested queue is not available"));
        return MakeUnexpected(Failure{});
    }

    if(numCommandLists > 0u && !pCommandLists){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list array is null"));
        return MakeUnexpected(Failure{});
    }
    if(numCommandLists > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list count exceeds Vulkan limit"));
        return MakeUnexpected(Failure{});
    }
    for(usize i = 0u; i < numCommandLists; ++i){
        CommandList* const commandList = pCommandLists[i];
        for(usize previous = 0u; previous < i; ++previous){
            if(pCommandLists[previous] == commandList){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list {} is duplicated"), i);
                return MakeUnexpected(Failure{});
            }
        }
        if(!commandList || &commandList->m_device != this){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list {} is null or foreign"), i);
            return MakeUnexpected(Failure{});
        }
        if(!commandList->matchesSubmissionLease(executionQueue, queue->m_queueID, graphSubmissionAuthorized)){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Command list {} lease provenance does not match execution queue")
                , i
            );
            return MakeUnexpected(Failure{});
        }
        if(!commandList->hasCommandBufferUnchecked()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list {} has no native command buffer"), i);
            return MakeUnexpected(Failure{});
        }
        if(commandList->m_commandRecordingFailed){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list {} has a sticky native recording failure"), i);
            return MakeUnexpected(Failure{});
        }
        if(commandList->m_isRecording){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to execute command lists: command list {} is still recording"), i);
            return MakeUnexpected(Failure{});
        }
    }
    for(usize i = 0u; i < numCommandLists; ++i){
        if(!pCommandLists[i]->validateTrackedResourcesReadyForSubmission())
            return MakeUnexpected(Failure{});
    }

    if(submitDesc.waitTokenCount > 0u && !submitDesc.waitTokens){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: submission wait token array is null"));
        return MakeUnexpected(Failure{});
    }

    UniqueLock<Futex> submissionWorkspaceLock(queue->m_submissionWorkspaceMutex);
    auto& localWaits = queue->m_executeLocalWaits;
    auto& expectedCommandLists = queue->m_executeExpectedCommandLists;
    localWaits.clear();
    expectedCommandLists.clear();
    if(submitDesc.waitTokenCount > 0u){
        localWaits.reserve(submitDesc.waitTokenCount);
        for(usize i = 0u; i < submitDesc.waitTokenCount; ++i){
            const QueueSubmissionToken& token = submitDesc.waitTokens[i];
            if(!validateSubmissionWaitToken(token)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: dependency token is invalid, unavailable, or unsignalled"));
                return MakeUnexpected(Failure{});
            }

            Queue* const producerQueue = m_physicalQueues[token.physicalQueueIndex];
            NWB_ASSERT(producerQueue);

            // Queue order already covers same-queue dependencies.
            if(token.matchesPhysicalQueue(executionQueue.index, executionQueue.deviceGeneration))
                continue;

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

    prepareSubmissionCommandListWorkspaceLocked(
        *queue,
        pCommandLists,
        numCommandLists,
        graphSubmissionAuthorized
    );

    // Hook runs after waits validate; its signal stays submission-local.
    Queue::SubmissionSignal hookSignal = {};
    const Queue::SubmissionSignal* localSignals = nullptr;
    usize localSignalCount = 0u;
    __hidden_vulkan_device_queue::ScopedSubmissionHookResolution hookResolution(submitDesc.preSubmitHook);
    if(submitDesc.preSubmitHook.valid()){
        const auto nativeSignal = submitDesc.preSubmitHook.invoke(
            submitDesc.preSubmitHook.context,
            submitDesc.preSubmitHook.identity,
            executionQueue
        );
        if(nativeSignal)
            hookResolution.arm();
        if(!nativeSignal || !nativeSignal->valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to prepare exact queue submission hook"));
            return MakeUnexpected(Failure{});
        }

        hookSignal.semaphore = __hidden_vulkan_device_queue::DecodeSubmissionNativeSemaphore(nativeSignal->semaphore);
        hookSignal.value = nativeSignal->value;
        if(hookSignal.semaphore == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exact queue submission hook returned an invalid native semaphore"));
            return MakeUnexpected(Failure{});
        }
        localSignals = &hookSignal;
        localSignalCount = 1u;
    }

    const auto submission = queue->submit(
        pCommandLists,
        numCommandLists,
        expectedCommandLists.empty() ? nullptr : expectedCommandLists.data(),
        localWaits.empty() ? nullptr : localWaits.data(),
        localWaits.size(),
        localSignals,
        localSignalCount,
        submitDesc.forceNativeSubmission
    );
    const bool submissionAccepted = submission && submission->accepted;
    const u64 submittedID = submission ? submission->submittedID : submission.error().lastSubmittedID;
    const VkResult nativeSubmissionResult = submission ? VK_SUCCESS : submission.error().nativeResult;
    const QueueSubmissionToken submissionToken = submissionAccepted
        ? QueueSubmissionToken{
            .value = submittedID,
            .physicalQueueIndex = executionQueue.index,
            .deviceGeneration = executionQueue.deviceGeneration,
            .queue = queue->m_queueID,
        }
        : QueueSubmissionToken{}
    ;

    finalizeSubmissionCommandListResourcesLocked(
        *queue,
        pCommandLists,
        numCommandLists,
        executionQueue,
        submittedID,
        submissionAccepted
    );
    hookResolution.resolve(submissionToken);

    submissionWorkspaceLock.unlock();
    if(nativeSubmissionResult == VK_ERROR_DEVICE_LOST && deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
        captureDeviceLoss(VulkanArenaScope::s_QueueSubmitContext);

    if(!submissionAccepted)
        return MakeUnexpected(Failure{});

    return QueueSubmissionReceipt{ submissionToken, submission->timelineWaitCount };
}

u32 Device::getQueueFamilyIndex(const CommandQueue::Enum queueType)const noexcept{
    return getQueueFamilyIndex(getPrimaryPhysicalQueue(queueType));
}

u32 Device::getQueueFamilyIndex(const GpuPhysicalQueueId& queue)const noexcept{
    const GpuPhysicalQueueInfo* const info = getPhysicalQueueInfo(queue);
    return info ? info->familyIndex : VK_QUEUE_FAMILY_IGNORED;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

