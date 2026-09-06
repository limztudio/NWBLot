// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


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

bool Queue::coversTimerQueryPrerequisite(
    const QueueSubmissionToken& prerequisite,
    const bool prerequisiteObservedComplete,
    const SubmissionWait* const localWaits,
    const usize localWaitCount
)const noexcept{
    if(!prerequisite.valid() || prerequisiteObservedComplete)
        return true;
    if(prerequisite.matchesPhysicalQueue(m_physicalQueue.index, m_physicalQueue.deviceGeneration))
        return true;
    if(
        !prerequisite.hasPhysicalQueueIdentity()
        || prerequisite.deviceGeneration != m_device.m_deviceGeneration
        || prerequisite.physicalQueueIndex >= m_device.m_physicalQueues.size()
    )
        return false;

    const Queue* const producerQueue = m_device.m_physicalQueues[prerequisite.physicalQueueIndex];
    if(
        !producerQueue
        || producerQueue->m_physicalQueue.deviceGeneration != prerequisite.deviceGeneration
        || producerQueue->m_queueID != prerequisite.queue
        || producerQueue->m_trackingSemaphore == VK_NULL_HANDLE
    )
        return false;

    const auto coveredBy = [&](const VkSemaphore semaphore, const u64 value) -> bool {
        return semaphore == producerQueue->m_trackingSemaphore && value >= prerequisite.value;
    };
    for(usize waitIndex = 0u; waitIndex < localWaitCount; ++waitIndex){
        if(coveredBy(localWaits[waitIndex].semaphore, localWaits[waitIndex].value))
            return true;
    }
    for(usize waitIndex = 0u; waitIndex < m_waitSemaphores.size(); ++waitIndex){
        if(coveredBy(m_waitSemaphores[waitIndex], m_waitSemaphoreValues[waitIndex]))
            return true;
    }
    return false;
}

u64 Queue::submit(
    CommandList* const* ppCmd,
    const usize numCmd,
    const SubmissionCommandListIdentity* const expectedCommandLists,
    const SubmissionWait* const localWaits,
    const usize localWaitCount,
    bool* const outSubmissionAccepted,
    VkResult* const outNativeResult,
    const SubmissionSignal* const localSignals,
    const usize localSignalCount,
    const bool forceNativeSubmission
){
    ScopedLock lock(m_mutex);
    DescriptorBufferManager* const descriptorBufferManager = m_context.descriptorBufferManager;
    GpuDescriptorHeap* submissionDescriptorHeap = nullptr;
    UniqueLock<Futex> descriptorHeapLock;
    UniqueLock<Futex> descriptorBufferLifecycleLock;
    static_assert(IsTriviallyCopyable_V<DescriptorHeapUseCommitTicket>, "accepted descriptor-use tickets must remain scalar-only");
    static_assert(IsTriviallyCopyable_V<SubmissionCommandListIdentity>, "accepted submission identities must remain scalar-only");
    auto& descriptorHeapUseCommitTickets = m_submitDescriptorHeapUseCommitTickets;
    auto& validatedTimerQueryCommandBuffers = m_submitValidatedTimerQueryCommandBuffers;
    auto& waitInfos = m_submitWaitInfos;
    auto& signalInfos = m_submitSignalInfos;
    auto& cmdBufInfos = m_submitCommandBufferInfos;
    auto& preparedCommandBuffers = m_submitPreparedCommandBuffers;
    descriptorHeapUseCommitTickets.clear();
    validatedTimerQueryCommandBuffers.clear();
    waitInfos.clear();
    signalInfos.clear();
    cmdBufInfos.clear();
    preparedCommandBuffers.clear();
    if(outSubmissionAccepted)
        *outSubmissionAccepted = false;
    if(outNativeResult)
        *outNativeResult = VK_SUCCESS;
    if(m_device.submissionsBlocked())
        return m_lastSubmittedID;

    if(numCmd > 0u && !ppCmd){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list array is null"));
        return m_lastSubmittedID;
    }
    const bool hasCommands = numCmd > 0u;
    if(hasCommands && !expectedCommandLists){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: expected command-list identity array is null"));
        return m_lastSubmittedID;
    }
    // Queue-global synchronization belongs to the next accepted native submission. Validation and injected
    // pre-driver rejection must leave it pending, especially when it contains the acquired swap-chain semaphore.
    if(localWaitCount > 0u && !localWaits){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: local wait array is null"));
        return m_lastSubmittedID;
    }
    if(localSignalCount > 0u && !localSignals){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: local signal array is null"));
        return m_lastSubmittedID;
    }

    const bool hasPendingSemaphores = localWaitCount > 0u
        || localSignalCount > 0u
        || !m_waitSemaphores.empty()
        || !m_signalSemaphores.empty()
    ;
    const bool requiresNativeSubmission = forceNativeSubmission || hasCommands || hasPendingSemaphores;

    if(hasCommands && numCmd > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: command list count exceeds Vulkan limit"));
        return m_lastSubmittedID;
    }
    if(
        localWaitCount > static_cast<usize>(Limit<u32>::s_Max)
        || m_waitSemaphores.size() > static_cast<usize>(Limit<u32>::s_Max) - localWaitCount
        || localSignalCount >= static_cast<usize>(Limit<u32>::s_Max)
        || m_signalSemaphores.size() >= static_cast<usize>(Limit<u32>::s_Max) - localSignalCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: queued semaphore count exceeds Vulkan limit"));
        return m_lastSubmittedID;
    }
    if(requiresNativeSubmission && m_lastSubmittedID == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: queue submission ID exhausted"));
        return m_lastSubmittedID;
    }
    if(hasCommands){
        for(usize i = 0; i < numCmd; ++i){
            auto* cmdList = ppCmd[i];
            const SubmissionCommandListIdentity& expected = expectedCommandLists[i];
            for(usize previous = 0u; previous < i; ++previous){
                if(ppCmd[previous] == cmdList){
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} is duplicated"), i);
                    return m_lastSubmittedID;
                }
            }
            if(!cmdList || &cmdList->m_device != &m_device){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} is null or foreign"), i);
                return m_lastSubmittedID;
            }
            if(!cmdList->matchesSubmissionLease(m_physicalQueue, m_queueID, expected.graphSubmissionAuthorized)){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Command-list lease provenance does not match execution queue"));
                return m_lastSubmittedID;
            }
            if(!cmdList->m_currentCmdBuf || cmdList->m_currentCmdBuf->m_cmdBuf == VK_NULL_HANDLE){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} has no native command buffer"), i);
                return m_lastSubmittedID;
            }
            if(cmdList->m_commandRecordingFailed){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: a command list has a sticky native recording failure"));
                return m_lastSubmittedID;
            }
            if(cmdList->m_isRecording){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} is still recording"), i);
                return m_lastSubmittedID;
            }
            if(
                !expected.owner
                || expected.owner != cmdList->m_currentCmdBuf.get()
                || expected.recordingLeaseSerial == 0u
                || expected.recordingLeaseSerial != cmdList->m_recordingLeaseSerial
                || expected.nativeRecordingID == 0u
                || expected.nativeRecordingID != cmdList->m_nativeRecordingID
                || expected.nativeRecordingID != cmdList->m_currentCmdBuf->m_recordingID
                || expected.recordingWorkerDomain != cmdList->m_creationDesc.recordingWorkerDomain
                || expected.recordingWorkerDomain != cmdList->m_currentCmdBuf->m_recordingWorkerDomain
                || expected.graphRecordingOwnershipSerial
                    != cmdList->m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire)
                || expected.recordingWorkerIndex != cmdList->m_creationDesc.recordingWorkerIndex
                || expected.recordingWorkerIndex != cmdList->m_currentCmdBuf->m_recordingWorkerIndex
            ){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} replaced its validated native recording lease"), i);
                return m_lastSubmittedID;
            }
            if(!cmdList->validateTrackedResourcesReadyForSubmission())
                return m_lastSubmittedID;
        }
    }

    if(hasCommands){
        validatedTimerQueryCommandBuffers.reserve(numCmd);
        for(usize i = 0u; i < numCmd; ++i){
            TrackedCommandBuffer* const tracked = ppCmd[i]->m_currentCmdBuf.get();
            if(!tracked->validateTimerQueryRecordingClaims(
                *this,
                localWaits,
                localWaitCount,
                validatedTimerQueryCommandBuffers.empty() ? nullptr : validatedTimerQueryCommandBuffers.data(),
                validatedTimerQueryCommandBuffers.size()
            )){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: timer-query recording order is stale or unresolved"));
                return m_lastSubmittedID;
            }
            validatedTimerQueryCommandBuffers.push_back(tracked);
        }
    }

    bool requiresDescriptorBufferLifecycle = false;
    if(hasCommands){
        for(usize i = 0u; i < numCmd; ++i){
            TrackedCommandBuffer* const tracked = ppCmd[i]->m_currentCmdBuf.get();
            requiresDescriptorBufferLifecycle |= tracked->m_descriptorBufferManager != nullptr;
            for(GpuDescriptorHeap* const heap : tracked->m_referencedDescriptorHeaps){
                if(!heap)
                    continue;
                if(heap != &m_device.m_gpuDescriptorHeap){
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Command buffer references a foreign descriptor heap."));
                    return m_lastSubmittedID;
                }
                submissionDescriptorHeap = heap;
            }
        }
    }

    // Global hierarchy: queue -> descriptor heap -> descriptor-buffer lifecycle.
    if(submissionDescriptorHeap)
        descriptorHeapLock = UniqueLock<Futex>(submissionDescriptorHeap->m_mutex);
    if(requiresDescriptorBufferLifecycle && descriptorBufferManager)
        descriptorBufferLifecycleLock = UniqueLock<Futex>(descriptorBufferManager->m_lifecycleMutex);

    if(hasCommands){
        for(usize i = 0; i < numCmd; ++i){
            CommandList* const cmdList = ppCmd[i];
            TrackedCommandBuffer* const tracked = cmdList->m_currentCmdBuf.get();
            if(
                (cmdList->m_descriptorBuffersBound && !tracked->m_descriptorBufferManager)
                || (!tracked->m_descriptorBufferManager && tracked->m_descriptorBufferGeneration != 0u)
                || (tracked->m_descriptorBufferManager && tracked->m_descriptorBufferGeneration == 0u)
                || (
                    tracked->m_descriptorBufferManager
                    && (
                        tracked->m_descriptorBufferManager != descriptorBufferManager
                        || descriptorBufferManager != &m_device.m_descriptorBufferManager
                        || !descriptorBufferManager->m_enabled
                        || descriptorBufferManager->m_lifecycleTransitioning
                        || descriptorBufferManager->m_bindingGeneration == 0u
                        || tracked->m_descriptorBufferGeneration != descriptorBufferManager->m_bindingGeneration
                    )
                )
            ){
                NWB_LOGGER_CRITICAL_WARNING(
                    NWB_TEXT("Vulkan: Failed to submit command lists: descriptor-buffer binding generation is stale")
                );
                return m_lastSubmittedID;
            }
        }
    }
    for(usize i = 0u; i < localSignalCount; ++i){
        if(localSignals[i].semaphore == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: local signal semaphore is null"));
            return m_lastSubmittedID;
        }
    }

    if(!requiresNativeSubmission)
        return m_lastSubmittedID;

    if(m_trackingSemaphore == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Queue submission skipped because timeline semaphore is unavailable."));
        return m_lastSubmittedID;
    }

    const u64 submissionID = m_lastSubmittedID + 1u;
    const QueueSubmissionToken submissionToken{
        .queue = m_queueID,
        .value = submissionID,
        .physicalQueueIndex = m_physicalQueue.index,
        .deviceGeneration = m_physicalQueue.deviceGeneration,
    };

    auto timelineSignal = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    timelineSignal.semaphore = m_trackingSemaphore;
    timelineSignal.value = submissionID;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    waitInfos.reserve(localWaitCount + m_waitSemaphores.size());
    for(usize i = 0; i < localWaitCount; ++i){
        if(localWaits[i].semaphore == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: local wait semaphore is null"));
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

    if(hasCommands){
        descriptorHeapUseCommitTickets.reserve(numCmd);
        cmdBufInfos.reserve(numCmd);
        for(usize i = 0u; i < numCmd; ++i){
            CommandList* const commandList = ppCmd[i];
            auto commandBufferInfo = VulkanDetail::MakeVkStruct<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
            commandBufferInfo.commandBuffer = commandList->m_currentCmdBuf->m_cmdBuf;
            cmdBufInfos.push_back(commandBufferInfo);
        }
    }
    NWB_ASSERT(preparedCommandBuffers.get_allocator() == m_commandBuffersInFlight.get_allocator());
    for(usize i = 0u; i < numCmd; ++i){
        TrackedCommandBuffer* const tracked = ppCmd[i]->m_currentCmdBuf.get();
        if(!tracked || !validateCommandBufferSubmissionState(*tracked))
            return m_lastSubmittedID;
        if(!tracked->validatePendingAccelStructBuildCommits()){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Vulkan: Native submission cannot publish acceleration-structure signatures across allocator domains")
            );
            return m_lastSubmittedID;
        }
        for(GpuDescriptorHeap* const heap : tracked->m_referencedDescriptorHeaps){
            if(!heap)
                continue;

            usize heapUseIndex = Limit<usize>::s_Max;
            if(!heap->validateCommandBufferUseSubmissionLocked(*tracked, submissionToken, heapUseIndex))
                return m_lastSubmittedID;
            for(const DescriptorHeapUseCommitTicket& ticket : descriptorHeapUseCommitTickets){
                if(ticket.heap == heap && ticket.heapUseIndex == heapUseIndex){
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Native submission contains a duplicate descriptor-heap use."));
                    return m_lastSubmittedID;
                }
            }
            descriptorHeapUseCommitTickets.push_back(DescriptorHeapUseCommitTicket{
                .heap = heap,
                .commandBuffer = tracked,
                .heapUseIndex = heapUseIndex,
            });
        }
    }

    auto submitInfo = VulkanDetail::MakeVkStruct<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
    submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
    submitInfo.pWaitSemaphoreInfos = waitInfos.data();
    submitInfo.commandBufferInfoCount = static_cast<uint32_t>(cmdBufInfos.size());
    submitInfo.pCommandBufferInfos = cmdBufInfos.data();
    submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
    submitInfo.pSignalSemaphoreInfos = signalInfos.data();

    const auto finalizeDetachedRecordingAttempts = [&](const bool accepted)noexcept{
        for(usize i = 0u; i < numCmd; ++i){
            CommandList* const commandList = ppCmd[i];
            if(!commandList || commandList->m_recordingLeaseSerial != expectedCommandLists[i].recordingLeaseSerial)
                TerminateInvariant();

            if(accepted)
                commandList->m_stateTracker.commitRecordingAttempt();
            else
                commandList->m_stateTracker.rollbackRecordingAttempt();
        }
    };
    const auto releaseDescriptorBufferLifecycle = [&]()noexcept{
        if(!descriptorBufferLifecycleLock.owns_lock())
            return;

        Futex* const lifecycleMutex = descriptorBufferLifecycleLock.release();
        if(lifecycleMutex)
            lifecycleMutex->unlock();
    };
    const auto releaseDescriptorHeap = [&]()noexcept{
        if(!descriptorHeapLock.owns_lock())
            return;

        Futex* const heapMutex = descriptorHeapLock.release();
        if(heapMutex)
            heapMutex->unlock();
    };

    auto submittedBatchBegin = m_commandBuffersInFlight.end();
    if(hasCommands){
        for(usize i = 0u; i < numCmd; ++i)
            preparedCommandBuffers.push_back(ppCmd[i]->m_currentCmdBuf);
        submittedBatchBegin = preparedCommandBuffers.begin();
        m_commandBuffersInFlight.splice(m_commandBuffersInFlight.end(), preparedCommandBuffers);
    }
    auto detachedCommandBuffer = submittedBatchBegin;
    for(usize i = 0u; i < numCmd; ++i, ++detachedCommandBuffer){
        CommandList* const commandList = ppCmd[i];
        TrackedCommandBufferPtr& tracked = *detachedCommandBuffer;
        tracked->m_submissionID = submissionID;
        commandList->m_currentCmdBuf = nullptr;
        commandList->m_nativeRecordingID = 0u;
    }

    VkResult res = VK_SUCCESS;
    bool submissionSuppressed = false;
    {
        ScopedLock hostLock(m_nativeQueue.hostMutex);
        submissionSuppressed = m_device.submissionsBlocked();
        if(!submissionSuppressed)
            res = m_context.deviceDispatch.vkQueueSubmit2(m_nativeQueue.queue, 1, &submitInfo, VK_NULL_HANDLE);
    }
    if(outNativeResult)
        *outNativeResult = res;

    if(submissionSuppressed || res != VK_SUCCESS){
        releaseDescriptorBufferLifecycle();
        releaseDescriptorHeap();
        if(res == VK_ERROR_DEVICE_LOST){
            m_device.markDeviceLost();
            clearPendingSemaphores();
        }
        finalizeDetachedRecordingAttempts(false);
        auto rejectedCommandBuffer = submittedBatchBegin;
        for(usize i = 0u; i < numCmd; ++i)
            rejectedCommandBuffer = recycleCommandBuffer(m_commandBuffersInFlight, rejectedCommandBuffer);

        if(submissionSuppressed){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Queue submission was suppressed because the device requires recreation."));
        }
        else if(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Queue submission was rejected: {}"), ResultToString(res));
        }
        else if(res != VK_ERROR_DEVICE_LOST){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command buffers to queue: {}"), ResultToString(res));
        }

        return m_lastSubmittedID;
    }

    m_lastSubmittedID = submissionID;

    for(const DescriptorHeapUseCommitTicket& ticket : descriptorHeapUseCommitTickets)
        ticket.heap->commitCommandBufferUseSubmissionLocked(*ticket.commandBuffer, submissionToken, ticket.heapUseIndex);
    releaseDescriptorBufferLifecycle();
    releaseDescriptorHeap();
    clearPendingSemaphores();
    finalizeDetachedRecordingAttempts(true);

    auto acceptedCommandBuffer = submittedBatchBegin;
    for(usize i = 0u; i < numCmd; ++i, ++acceptedCommandBuffer){
        TrackedCommandBufferPtr& tracked = *acceptedCommandBuffer;
        tracked->commitTimerQueryRecordingClaims(submissionToken);
        tracked->commitRetainedBufferStateCommits();
        tracked->commitRetainedTextureStateCommits();
        tracked->commitPendingAccelStructBuildCommits();
        tracked->commitPendingOpacityMicromapBuildCommits();
        commitCommandBufferStateTransition(*tracked, TrackedCommandBufferArenaState::Pending);
    }
    if(outSubmissionAccepted)
        *outSubmissionAccepted = true;

    return submissionID;
}

VkResult Queue::updateLastFinishedID(){
    if(!m_trackingSemaphore){
        m_lastFinishedID = m_lastSubmittedID;
        return VK_SUCCESS;
    }

    u64 completedValue = 0;
    const VkResult res = m_context.deviceDispatch.vkGetSemaphoreCounterValue(m_context.device, m_trackingSemaphore, &completedValue);
    if(res == VK_SUCCESS)
        // vkQueueWaitIdle() establishes a stronger completion fact than a later timeline query. Never let a stale
        // driver value make already-retired command buffers or descriptor uses appear in flight again.
        m_lastFinishedID = Max(m_lastFinishedID, completedValue);
    else if(res == VK_ERROR_DEVICE_LOST)
        m_device.markDeviceLost();
    return res;
}

void Queue::waitForIdle(){
    UniqueLock<Futex> lock(m_mutex);

    VkResult res = VK_SUCCESS;
    {
        ScopedLock hostLock(m_nativeQueue.hostMutex);
        res = m_device.isDeviceLost() ? VK_ERROR_DEVICE_LOST : m_context.deviceDispatch.vkQueueWaitIdle(m_nativeQueue.queue);
    }
    if(res == VK_ERROR_DEVICE_LOST)
        m_device.markDeviceLost();
    if(res == VK_SUCCESS){
        m_lastFinishedID = m_lastSubmittedID;
        collectCompletedCommandBuffers();
    }
    lock.unlock();

    if(res == VK_ERROR_DEVICE_LOST)
        m_device.captureDeviceLoss("queue wait idle");
    if(res != VK_SUCCESS)
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Queue wait-for-idle failed: {}"), ResultToString(res));
}

void Queue::clearPendingSemaphores()noexcept{
    static_assert(noexcept(m_waitSemaphores.clear()), "pending wait-semaphore release must be non-throwing");
    static_assert(noexcept(m_waitSemaphoreValues.clear()), "pending wait-value release must be non-throwing");
    static_assert(noexcept(m_signalSemaphores.clear()), "pending signal-semaphore release must be non-throwing");
    static_assert(noexcept(m_signalSemaphoreValues.clear()), "pending signal-value release must be non-throwing");
    m_waitSemaphores.clear();
    m_waitSemaphoreValues.clear();
    m_signalSemaphores.clear();
    m_signalSemaphoreValues.clear();
}

Queue::CommandBufferList::iterator Queue::recycleCommandBuffer(
    CommandBufferList& source,
    const CommandBufferList::iterator commandBuffer
){
    auto next = commandBuffer;
    ++next;

    TrackedCommandBufferPtr& cmdBuf = *commandBuffer;
    if(!cmdBuf)
        return source.erase(commandBuffer);
    if(&cmdBuf->m_queue != this || &cmdBuf->m_context != &m_context){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Cannot recycle a command buffer through a foreign queue"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Command buffer recycle owner mismatch"));
        return next;
    }

    cmdBuf->releasePendingAccelStructBuildCommits();
    cmdBuf->clearTrackedReferences();
    transitionCommandBufferState(*cmdBuf, TrackedCommandBufferArenaState::Reusable);
    if(cmdBuf->m_recordingWorkerIndex == 0u){
        NWB_ASSERT(source.get_allocator() == m_commandBuffersPool.get_allocator());
        m_commandBuffersPool.splice(m_commandBuffersPool.end(), source, commandBuffer);
        return next;
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
        NWB_ASSERT(source.get_allocator() == m_commandBuffersPool.get_allocator());
        m_commandBuffersPool.splice(m_commandBuffersPool.end(), source, commandBuffer);
        return next;
    }

    // Queue submission/timeline retirement holds m_mutex before arriving here. It may take a worker arena lock,
    // but worker recording never takes m_mutex, so the lock order cannot form a cycle.
    ScopedLock lock(workerArena->mutex);

    NWB_ASSERT(source.get_allocator() == workerArena->commandBuffersPool.get_allocator());
    workerArena->commandBuffersPool.splice(workerArena->commandBuffersPool.end(), source, commandBuffer);
    return next;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Device::queueWaitForSemaphore(CommandQueue::Enum waitQueue, VkSemaphore semaphore, u64 value){
    SubmissionOperationLease submissionOperation(*this);
    if(!submissionOperation.valid())
        return;

    Queue* q = getQueue(waitQueue);
    if(q)
        q->addWaitSemaphore(semaphore, value);
}

void Device::queueSignalSemaphore(CommandQueue::Enum executionQueue, VkSemaphore semaphore, u64 value){
    SubmissionOperationLease submissionOperation(*this);
    if(!submissionOperation.valid())
        return;

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
        VkResult result = VK_SUCCESS;
        u64 completedInstance = 0u;
        {
            ScopedLock lock(q->m_mutex);
            result = q->updateLastFinishedID();
            completedInstance = q->m_lastFinishedID;
        }
        if(result == VK_ERROR_DEVICE_LOST)
            captureDeviceLoss("queue timeline query");
        if(result != VK_SUCCESS)
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to query queue timeline semaphore value: {}"), ResultToString(result));
        return completedInstance;
    }
    return 0;
}

void Device::queueWaitForCommandList(CommandQueue::Enum waitQueue, CommandQueue::Enum executionQueue, u64 instance){
    SubmissionOperationLease submissionOperation(*this);
    if(!submissionOperation.valid())
        return;

    Queue* wait = getQueue(waitQueue);
    Queue* exec = getQueue(executionQueue);

    if(wait && exec)
        wait->addWaitSemaphore(exec->m_trackingSemaphore, instance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

