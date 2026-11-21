// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "device_detail.h"
#include "submission_workspace.h"

#include <core/common/log.h>

#include <global/scope_exit.h>
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
    b8 m_armed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CommandListHandle> Device::createCommandList(const CommandListParameters& params){
    CommandListParameters resolvedParams = params;
    if(resolvedParams.recordingWorkerIndex == 0u)
        resolvedParams.recordingWorkerDomain = 0u;
    Queue* queue = nullptr;
    if(resolvedParams.physicalQueue.valid()){
        queue = getQueue(resolvedParams.physicalQueue);
        if(!queue){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create command list: requested physical queue is not available"));
            return MakeUnexpected(Failure{});
        }
        resolvedParams.queueType = queue->m_queueID;
    }
    else{
        queue = getQueue(resolvedParams.queueType);
        if(!queue){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to create command list: requested queue is not available"));
            return MakeUnexpected(Failure{});
        }
        resolvedParams.physicalQueue = queue->m_physicalQueue;
    }

    auto* cmdList = NewArenaObject<CommandList>(m_context.objectArena, *this, resolvedParams);
    if(!cmdList)
        return MakeUnexpected(Failure{});
    return CommandListHandle(cmdList, CommandListHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
}

Expected<QueueSubmissionReceipt> Device::executeCommandLists(CommandList* const* commandLists, const usize numCommandLists, const CommandQueue::Enum executionQueue, const QueueSubmissionDesc& submitDesc){
    return executeCommandLists(commandLists, numCommandLists, getPrimaryPhysicalQueue(executionQueue), submitDesc);
}


Expected<QueueSubmissionReceipt> Device::executeCommandLists(CommandList* const* commandLists, const usize numCommandLists, const GpuPhysicalQueueId& executionQueue, const QueueSubmissionDesc& submitDesc){
    return executeCommandListsInternal(commandLists, numCommandLists, executionQueue, submitDesc, false);
}

b8 Device::validateSubmissionWaitToken(const QueueSubmissionToken& token)const noexcept{
    if(!token.valid() || !matchesPhysicalQueueIdentity(token.queue, token.physicalQueueIndex, token.deviceGeneration))
        return false;

    FOUNDATION_ASSERT(token.physicalQueueIndex < m_physicalQueues.size());
    const auto producerQueue = m_physicalQueues[token.physicalQueueIndex];
    FOUNDATION_ASSERT(producerQueue->m_queueID == token.queue && token.matchesPhysicalQueue(producerQueue->m_physicalQueue.index, producerQueue->m_physicalQueue.deviceGeneration));

    return producerQueue->m_trackingSemaphore != VK_NULL_HANDLE && token.value <= producerQueue->m_publishedSubmittedID.load(MemoryOrder::acquire);
}

b8 Device::waitForSubmissionToken(const QueueSubmissionToken& token){
    return waitForSubmissionTokenInternal(token, DeviceLossDiagnosticPolicy::Capture);
}

u32 Device::getQueueFamilyIndex(const CommandQueue::Enum queueType)const noexcept{
    return getQueueFamilyIndex(getPrimaryPhysicalQueue(queueType));
}

u32 Device::getQueueFamilyIndex(const GpuPhysicalQueueId& queue)const noexcept{
    const GpuPhysicalQueueInfo* const info = getPhysicalQueueInfo(queue);
    return info ? info->familyIndex : VK_QUEUE_FAMILY_IGNORED;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<QueueSubmissionReceipt> Device::executeGraphCommandLists(CommandList* const* commandLists, const usize numCommandLists, const GpuPhysicalQueueId& executionQueue, const QueueSubmissionDesc& submitDesc){
    return executeCommandListsInternal(commandLists, numCommandLists, executionQueue, submitDesc, true);
}

Device::GraphCommandListBatchResult Device::executeGraphCommandListsBatch(
    const Span<const GraphCommandListBatchEntry> entries,
    const GpuPhysicalQueueId& executionQueue,
    Span<QueueSubmissionReceipt> outReceipts
){
    SubmissionOperationLease submissionOperation(*this);
    if(!submissionOperation.valid() || entries.empty() || outReceipts.size() != entries.size())
        return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::ValidationFailed };

    Queue* const queue = getQueue(executionQueue);
    if(!queue){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: requested queue is not available"));
        return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::ValidationFailed };
    }

    UniqueLock<Futex> submissionWorkspaceLock(queue->m_submissionWorkspaceMutex);
    u64 executeBatchStamp = queue->m_nextExecuteBatchStamp++;
    if(executeBatchStamp == 0u)
        executeBatchStamp = queue->m_nextExecuteBatchStamp++;

    auto& localWaits = queue->m_executeLocalWaits;
    auto& expectedCommandLists = queue->m_executeExpectedCommandLists;
    localWaits.clear();
    expectedCommandLists.clear();
    queue->m_executeSubmittedUploadMask = 0u;

    auto& queueEntries = queue->m_executeQueueEntries;
    auto& waitOffsets = queue->m_executeWaitOffsets;
    auto& queueReceipts = queue->m_executeQueueReceipts;
    queueEntries.resize(entries.size());
    waitOffsets.resize(entries.size());
    queueReceipts.resize(entries.size());

    u8 uploadMask = 0u;
    for(usize entryIndex = 0u; entryIndex < entries.size(); ++entryIndex){
        const GraphCommandListBatchEntry& entry = entries[entryIndex];
        if(entry.submitDesc.preSubmitHook.valid() && entryIndex + 1u != entries.size())
            return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::ValidationFailed };
        if(entry.numCommandLists > 0u && !entry.commandLists){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list array is null"));
            return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::ValidationFailed };
        }
        if(entry.numCommandLists > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list count exceeds Vulkan limit"));
            return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::ValidationFailed };
        }
        if(entry.submitDesc.waitTokenCount > 0u && !entry.submitDesc.waitTokens){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: submission wait token array is null"));
            return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::ValidationFailed };
        }
        for(usize i = 0u; i < entry.numCommandLists; ++i){
            CommandList* const commandList = entry.commandLists[i];
            if(!commandList || &commandList->m_device != this){
                NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list {} is null or foreign"), i);
                return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::ValidationFailed };
            }
            u64 graphOwnershipSerial = 0u;
            if(!commandList->matchesSubmissionLease(executionQueue, queue->m_queueID, true, &graphOwnershipSerial)){
                NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Command list {} lease provenance does not match execution queue"), i);
                return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::ValidationFailed };
            }
            if(commandList->m_lastExecuteBatchStamp == executeBatchStamp){
                NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list {} is duplicated"), i);
                return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::ValidationFailed };
            }
            commandList->m_lastExecuteBatchStamp = executeBatchStamp;

            TrackedCommandBuffer& owner = *commandList->m_currentCmdBuf;
            uploadMask |= owner.m_usedUploadChunkMask;
            expectedCommandLists.push_back(Queue::SubmissionCommandListIdentity{
                .owner = &owner,
                .recordingLeaseSerial = commandList->m_recordingLeaseSerial,
                .nativeRecordingID = commandList->m_nativeRecordingID,
                .recordingWorkerDomain = commandList->m_creationDesc.recordingWorkerDomain,
                .graphRecordingOwnershipSerial = graphOwnershipSerial,
                .recordingWorkerIndex = commandList->m_creationDesc.recordingWorkerIndex,
                .graphSubmissionAuthorized = true,
            });
        }

        const usize waitOffset = localWaits.size();
        u64 maxSameQueueWaitValue = 0u;
        for(usize i = 0u; i < entry.submitDesc.waitTokenCount; ++i){
            const QueueSubmissionToken& token = entry.submitDesc.waitTokens[i];
            if(token.matchesPhysicalQueue(executionQueue.index, executionQueue.deviceGeneration)){
                if(!token.valid() || !matchesPhysicalQueueIdentity(token.queue, token.physicalQueueIndex, token.deviceGeneration)){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: dependency token is invalid, unavailable, or unsignalled"));
                    return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::ValidationFailed };
                }
                maxSameQueueWaitValue = Max(maxSameQueueWaitValue, token.value);
                continue;
            }
            if(!validateSubmissionWaitToken(token)){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: dependency token is invalid, unavailable, or unsignalled"));
                return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::ValidationFailed };
            }

            const auto producerQueue = m_physicalQueues[token.physicalQueueIndex];
            b8 merged = false;
            for(usize w = waitOffset; w < localWaits.size(); ++w){
                Queue::SubmissionWait& wait = localWaits[w];
                if(wait.semaphore != producerQueue->m_trackingSemaphore)
                    continue;
                wait.value = Max(wait.value, token.value);
                merged = true;
                break;
            }
            if(!merged)
                localWaits.push_back(Queue::SubmissionWait{ producerQueue->m_trackingSemaphore, token.value });
        }
        waitOffsets[entryIndex] = waitOffset;
        queueEntries[entryIndex] = Queue::BatchSubmitEntry{
            .ppCmd = entry.commandLists,
            .numCmd = entry.numCommandLists,
            .expectedCommandLists = nullptr,
            .localWaits = nullptr,
            .localWaitCount = localWaits.size() - waitOffset,
            .localSignals = nullptr,
            .localSignalCount = 0u,
            .maxSameQueueWaitValue = maxSameQueueWaitValue,
            .forceNativeSubmission = entry.submitDesc.forceNativeSubmission,
            .allowSignalCoalescing = entry.allowSignalCoalescing && !entry.submitDesc.preSubmitHook.valid(),
        };
    }
    queue->m_executeSubmittedUploadMask = uploadMask;

    usize expectedOffset = 0u;
    for(usize entryIndex = 0u; entryIndex < entries.size(); ++entryIndex){
        Queue::BatchSubmitEntry& qEntry = queueEntries[entryIndex];
        qEntry.expectedCommandLists = entries[entryIndex].numCommandLists > 0u ? expectedCommandLists.data() + expectedOffset : nullptr;
        qEntry.localWaits = qEntry.localWaitCount > 0u ? localWaits.data() + waitOffsets[entryIndex] : nullptr;
        expectedOffset += entries[entryIndex].numCommandLists;
    }

    Queue::SubmissionSignal hookSignal = {};
    const QueueSubmissionPreSubmitHook& lastHook = entries.back().submitDesc.preSubmitHook;
    __hidden_vulkan_device_queue::ScopedSubmissionHookResolution hookResolution(lastHook);
    b8 hookInvoked = false;
    if(lastHook.valid()){
        const auto nativeSignal = lastHook.invoke(lastHook.context, lastHook.identity, executionQueue);
        hookInvoked = true;
        if(nativeSignal)
            hookResolution.arm();
        if(!nativeSignal || !nativeSignal->valid()){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to prepare exact queue submission hook"));
            return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::NativeSubmitFailed };
        }
        hookSignal.semaphore = __hidden_vulkan_device_queue::DecodeSubmissionNativeSemaphore(nativeSignal->semaphore);
        hookSignal.value = nativeSignal->value;
        if(hookSignal.semaphore == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Exact queue submission hook returned an invalid native semaphore"));
            return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::NativeSubmitFailed };
        }
        queueEntries.back().localSignals = &hookSignal;
        queueEntries.back().localSignalCount = 1u;
    }

    const b8 callerPreValidated = !hookInvoked;
    const Queue::BatchSubmissionResult batchResult = queue->submitBatch(Span<const Queue::BatchSubmitEntry>(queueEntries.data(), queueEntries.size()), Span<Queue::SubmissionReceipt>(queueReceipts.data(), queueReceipts.size()), callerPreValidated);
    const b8 submissionAccepted = batchResult.status == Queue::BatchSubmissionResult::Status::Accepted;
    const u64 submittedID = batchResult.lastSubmittedID;

    const QueueSubmissionToken hookToken = submissionAccepted
        ? QueueSubmissionToken{ .value = queueReceipts.back().submittedID, .physicalQueueIndex = executionQueue.index, .deviceGeneration = executionQueue.deviceGeneration, .queue = queue->m_queueID, }
        : QueueSubmissionToken{}
    ;

    if(submissionAccepted || batchResult.status == Queue::BatchSubmissionResult::Status::NativeSubmitFailed){
        SubmissionWorkspace submissionWorkspace(*this, *queue);
        submissionWorkspace.finalizeBatch(entries, executionQueue, submittedID, submissionAccepted);
    }

    hookResolution.resolve(hookToken);
    submissionWorkspaceLock.unlock();

    if(batchResult.nativeResult == VK_ERROR_DEVICE_LOST)
        captureDeviceLoss(VulkanArenaScope::s_QueueSubmitContext);

    if(!submissionAccepted){
        return GraphCommandListBatchResult{
            (batchResult.status == Queue::BatchSubmissionResult::Status::NativeSubmitFailed || hookInvoked)
                ? GraphCommandListBatchResult::Status::NativeSubmitFailed
                : GraphCommandListBatchResult::Status::ValidationFailed
        };
    }

    for(usize entryIndex = 0u; entryIndex < entries.size(); ++entryIndex){
        outReceipts[entryIndex] = QueueSubmissionReceipt{
            QueueSubmissionToken{ .value = queueReceipts[entryIndex].submittedID, .physicalQueueIndex = executionQueue.index, .deviceGeneration = executionQueue.deviceGeneration, .queue = queue->m_queueID, },
            queueReceipts[entryIndex].timelineWaitCount,
        };
    }
    return GraphCommandListBatchResult{ GraphCommandListBatchResult::Status::Accepted };
}

Expected<QueueSubmissionReceipt> Device::executeCommandListsInternal(
    CommandList* const* commandLists,
    const usize numCommandLists,
    const GpuPhysicalQueueId& executionQueue,
    const QueueSubmissionDesc& submitDesc,
    const b8 graphSubmissionAuthorized,
    const DeviceLossDiagnosticPolicy::Enum deviceLossDiagnosticPolicy
){
    SubmissionOperationLease submissionOperation(*this);
    if(!submissionOperation.valid())
        return MakeUnexpected(Failure{});

    Queue* const queue = getQueue(executionQueue);
    if(!queue){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: requested queue is not available"));
        return MakeUnexpected(Failure{});
    }

    if(numCommandLists > 0u && !commandLists){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list array is null"));
        return MakeUnexpected(Failure{});
    }
    if(numCommandLists > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list count exceeds Vulkan limit"));
        return MakeUnexpected(Failure{});
    }
    if(submitDesc.waitTokenCount > 0u && !submitDesc.waitTokens){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: submission wait token array is null"));
        return MakeUnexpected(Failure{});
    }

    UniqueLock<Futex> submissionWorkspaceLock(queue->m_submissionWorkspaceMutex);
    u64 executeBatchStamp = queue->m_nextExecuteBatchStamp++;
    if(executeBatchStamp == 0u)
        executeBatchStamp = queue->m_nextExecuteBatchStamp++;
    for(usize i = 0u; i < numCommandLists; ++i){
        CommandList* const commandList = commandLists[i];
        if(!commandList || &commandList->m_device != this){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list {} is null or foreign"), i);
            return MakeUnexpected(Failure{});
        }
        if(!commandList->matchesSubmissionLease(executionQueue, queue->m_queueID, graphSubmissionAuthorized)){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Command list {} lease provenance does not match execution queue"), i);
            return MakeUnexpected(Failure{});
        }
        if(commandList->m_lastExecuteBatchStamp == executeBatchStamp){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: command list {} is duplicated"), i);
            return MakeUnexpected(Failure{});
        }
        commandList->m_lastExecuteBatchStamp = executeBatchStamp;
    }

    auto& localWaits = queue->m_executeLocalWaits;
    auto& expectedCommandLists = queue->m_executeExpectedCommandLists;
    localWaits.clear();
    expectedCommandLists.clear();
    u64 maxSameQueueWaitValue = 0u;
    if(submitDesc.waitTokenCount > 0u){
        localWaits.reserve(submitDesc.waitTokenCount);
        for(usize i = 0u; i < submitDesc.waitTokenCount; ++i){
            const QueueSubmissionToken& token = submitDesc.waitTokens[i];
            if(token.matchesPhysicalQueue(executionQueue.index, executionQueue.deviceGeneration)){
                if(!token.valid() || !matchesPhysicalQueueIdentity(token.queue, token.physicalQueueIndex, token.deviceGeneration)){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: dependency token is invalid, unavailable, or unsignalled"));
                    return MakeUnexpected(Failure{});
                }
                maxSameQueueWaitValue = Max(maxSameQueueWaitValue, token.value);
                continue;
            }
            if(!validateSubmissionWaitToken(token)){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to execute command lists: dependency token is invalid, unavailable, or unsignalled"));
                return MakeUnexpected(Failure{});
            }

            const auto producerQueue = m_physicalQueues[token.physicalQueueIndex];

            b8 merged = false;
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

    SubmissionWorkspace submissionWorkspace(*this, *queue);
    submissionWorkspace.prepare(commandLists, numCommandLists, graphSubmissionAuthorized);

    Queue::SubmissionSignal hookSignal = {};
    const Queue::SubmissionSignal* localSignals = nullptr;
    usize localSignalCount = 0u;
    __hidden_vulkan_device_queue::ScopedSubmissionHookResolution hookResolution(submitDesc.preSubmitHook);
    if(submitDesc.preSubmitHook.valid()){
        const auto nativeSignal = submitDesc.preSubmitHook.invoke(submitDesc.preSubmitHook.context, submitDesc.preSubmitHook.identity, executionQueue);
        if(nativeSignal)
            hookResolution.arm();
        if(!nativeSignal || !nativeSignal->valid()){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to prepare exact queue submission hook"));
            return MakeUnexpected(Failure{});
        }

        hookSignal.semaphore = __hidden_vulkan_device_queue::DecodeSubmissionNativeSemaphore(nativeSignal->semaphore);
        hookSignal.value = nativeSignal->value;
        if(hookSignal.semaphore == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Exact queue submission hook returned an invalid native semaphore"));
            return MakeUnexpected(Failure{});
        }
        localSignals = &hookSignal;
        localSignalCount = 1u;
    }

    const auto submission = queue->submit(
        commandLists,
        numCommandLists,
        expectedCommandLists.empty() ? nullptr : expectedCommandLists.data(),
        localWaits.empty() ? nullptr : localWaits.data(),
        localWaits.size(),
        localSignals,
        localSignalCount,
        submitDesc.forceNativeSubmission,
        maxSameQueueWaitValue
    );
    const b8 submissionAccepted = submission && submission->accepted;
    const u64 submittedID = submission ? submission->submittedID : submission.error().lastSubmittedID;
    const VkResult nativeSubmissionResult = submission ? VK_SUCCESS : submission.error().nativeResult;
    const QueueSubmissionToken submissionToken = submissionAccepted
        ? QueueSubmissionToken{ .value = submittedID, .physicalQueueIndex = executionQueue.index, .deviceGeneration = executionQueue.deviceGeneration, .queue = queue->m_queueID, }
        : QueueSubmissionToken{}
    ;

    submissionWorkspace.finalize(commandLists, numCommandLists, executionQueue, submittedID, submissionAccepted);
    hookResolution.resolve(submissionToken);

    submissionWorkspaceLock.unlock();
    if(nativeSubmissionResult == VK_ERROR_DEVICE_LOST && deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
        captureDeviceLoss(VulkanArenaScope::s_QueueSubmitContext);

    if(!submissionAccepted)
        return MakeUnexpected(Failure{});

    return QueueSubmissionReceipt{ submissionToken, submission->timelineWaitCount };
}

b8 Device::waitForSubmissionTokenInternal(const QueueSubmissionToken& token, const DeviceLossDiagnosticPolicy::Enum deviceLossDiagnosticPolicy){
    if(!token.valid() || !matchesPhysicalQueueIdentity(token.queue, token.physicalQueueIndex, token.deviceGeneration))
        return false;

    FOUNDATION_ASSERT(token.physicalQueueIndex < m_physicalQueues.size());
    const auto producerQueue = m_physicalQueues[token.physicalQueueIndex];
    FOUNDATION_ASSERT(producerQueue->m_queueID == token.queue);

    if(producerQueue->m_trackingSemaphore == VK_NULL_HANDLE)
        return false;
    if(token.value <= producerQueue->lastObservedFinishedID())
        return true;

    VkSemaphore trackingSemaphore = VK_NULL_HANDLE;
    {
        NothrowScopedLock producerLock(producerQueue->m_mutex);
        trackingSemaphore = producerQueue->m_trackingSemaphore;
        if(trackingSemaphore == VK_NULL_HANDLE || token.value > producerQueue->m_lastSubmittedID)
            return false;
        if(token.value <= producerQueue->m_lastFinishedID)
            return true;
    }

    auto waitInfo = VulkanDetail::MakeVkStruct<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
    waitInfo.semaphoreCount = 1u;
    waitInfo.pSemaphores = &trackingSemaphore;
    waitInfo.pValues = &token.value;
    const VkResult result = m_context.deviceDispatch.vkWaitSemaphores(m_context.device, &waitInfo, UINT64_MAX);
    if(result == VK_SUCCESS){
        NothrowScopedLock producerLock(producerQueue->m_mutex);
        producerQueue->m_lastFinishedID = Max(producerQueue->m_lastFinishedID, token.value);
        producerQueue->m_lastObservedFinishedID.store(producerQueue->m_lastFinishedID, MemoryOrder::release);
        producerQueue->collectCompletedCommandBuffers();
    }
    else if(result == VK_ERROR_DEVICE_LOST)
        markDeviceLost();

    if(result == VK_ERROR_DEVICE_LOST && deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
        captureDeviceLoss("submission token wait");
    if(result != VK_SUCCESS && deviceLossDiagnosticPolicy == DeviceLossDiagnosticPolicy::Capture)
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to wait for submission token: {}"), ResultToString(result));
    return result == VK_SUCCESS;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

