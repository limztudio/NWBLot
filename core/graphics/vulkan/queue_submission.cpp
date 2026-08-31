// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    const SubmissionSignal* const localSignals,
    const usize localSignalCount,
    const bool forceNativeSubmission
){
    ScopedLock lock(m_mutex);
    DescriptorBufferManager* const descriptorBufferManager = m_context.descriptorBufferManager;
    UniqueLock<Futex> descriptorBufferLifecycleLock;
    if(outSubmissionAccepted)
        *outSubmissionAccepted = false;
    if(m_device.submissionsBlocked())
        return m_lastSubmittedID;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_QueueSubmitArena);

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
            for(usize previous = 0u; previous < i; ++previous){
                if(ppCmd[previous] == cmdList){
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} is duplicated"), i);
                    return m_lastSubmittedID;
                }
            }
            if(
                !cmdList
                || &cmdList->m_device != &m_device
                || !cmdList->m_currentCmdBuf
                || cmdList->m_currentCmdBuf->m_cmdBuf == VK_NULL_HANDLE
            ){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} is null, foreign, or has no native command buffer"), i);
                return m_lastSubmittedID;
            }
            if(cmdList->commandRecordingFailed()){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: a command list has a sticky native recording failure"));
                return m_lastSubmittedID;
            }
            if(cmdList->m_isRecording){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} is still recording"), i);
                return m_lastSubmittedID;
            }
            if(!cmdList->matchesSubmissionLease(m_physicalQueue, m_queueID)){
                NWB_LOGGER_CRITICAL_WARNING(
                    NWB_TEXT("Vulkan: Command-list lease provenance does not match execution queue")
                );
                return m_lastSubmittedID;
            }
            const SubmissionCommandListIdentity& expected = expectedCommandLists[i];
            if(
                !expected.owner
                || expected.owner.get() != cmdList->m_currentCmdBuf.get()
                || expected.recordingLeaseSerial == 0u
                || expected.recordingLeaseSerial != cmdList->m_recordingLeaseSerial
                || expected.nativeRecordingID == 0u
                || expected.nativeRecordingID != cmdList->m_nativeRecordingID
                || expected.nativeRecordingID != cmdList->m_currentCmdBuf->m_recordingID
                || expected.recordingWorkerDomain != cmdList->m_creationDesc.recordingWorkerDomain
                || expected.recordingWorkerDomain != cmdList->m_currentCmdBuf->m_recordingWorkerDomain
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

    Vector<TrackedCommandBuffer*, Alloc::ScratchArena> validatedTimerQueryCommandBuffers{scratchArena};
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

    if(descriptorBufferManager)
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

    Vector<TrackedCommandBufferPtr, Alloc::ScratchArena> trackedBuffers{scratchArena};
    Vector<VkCommandBufferSubmitInfo, Alloc::ScratchArena> cmdBufInfos{scratchArena};

    if(hasCommands){
        trackedBuffers.reserve(numCmd);
        cmdBufInfos.reserve(numCmd);

        for(usize i = 0; i < numCmd; ++i){
            auto* cmdList = ppCmd[i];
            auto cmdBufInfo = VulkanDetail::MakeVkStruct<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
            cmdBufInfo.commandBuffer = cmdList->m_currentCmdBuf->m_cmdBuf;
            cmdBufInfos.push_back(cmdBufInfo);

            cmdList->m_currentCmdBuf->m_submissionID = m_lastSubmittedID + 1;
            trackedBuffers.push_back(Move(cmdList->m_currentCmdBuf));
            cmdList->m_nativeRecordingID = 0u;
        }
    }

    const auto finalizeDetachedRecordingAttempts = [&](const bool accepted){
        for(usize i = 0u; i < numCmd; ++i){
            CommandList* const commandList = ppCmd[i];
            if(!commandList || commandList->m_recordingLeaseSerial != expectedCommandLists[i].recordingLeaseSerial)
                continue;

            if(accepted)
                commandList->m_stateTracker.commitRecordingAttempt();
            else
                commandList->m_stateTracker.rollbackRecordingAttempt();
        }
    };

    if(!requiresNativeSubmission)
        return m_lastSubmittedID;

    if(m_trackingSemaphore == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Queue submission skipped because timeline semaphore is unavailable."));

        if(descriptorBufferLifecycleLock.owns_lock())
            descriptorBufferLifecycleLock.unlock();
        finalizeDetachedRecordingAttempts(false);
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
            if(descriptorBufferLifecycleLock.owns_lock())
                descriptorBufferLifecycleLock.unlock();
            finalizeDetachedRecordingAttempts(false);
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

    VkResult res = VK_SUCCESS;
    bool submissionSuppressed = false;
    {
        ScopedLock hostLock(m_nativeQueue.hostMutex);
        submissionSuppressed = m_device.submissionsBlocked();
        if(!submissionSuppressed)
            res = m_context.deviceDispatch.vkQueueSubmit2(m_nativeQueue.queue, 1, &submitInfo, VK_NULL_HANDLE);
    }

    if(submissionSuppressed || res != VK_SUCCESS){
        m_lastSubmittedID = submissionID - 1;

        if(descriptorBufferLifecycleLock.owns_lock())
            descriptorBufferLifecycleLock.unlock();

        if(submissionSuppressed){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Queue submission was suppressed because the device requires recreation."));
        }
        else if(res == VK_ERROR_DEVICE_LOST){
            clearPendingSemaphores();
            m_device.captureDeviceLoss("queue submit");
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Device was lost during queue submission."));
        }
        else if(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Queue submission was rejected: {}"), ResultToString(res));
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command buffers to queue: {}"), ResultToString(res));
        }

        finalizeDetachedRecordingAttempts(false);
        for(auto& tracked : trackedBuffers){
            recycleCommandBuffer(Move(tracked));
        }

        return m_lastSubmittedID;
    }

    if(descriptorBufferLifecycleLock.owns_lock())
        descriptorBufferLifecycleLock.unlock();
    clearPendingSemaphores();
    finalizeDetachedRecordingAttempts(true);

    const QueueSubmissionToken submissionToken{
        .queue = m_queueID,
        .value = submissionID,
        .physicalQueueIndex = m_physicalQueue.index,
        .deviceGeneration = m_physicalQueue.deviceGeneration,
    };
    for(auto& tracked : trackedBuffers){
        tracked->commitTimerQueryRecordingClaims(submissionToken);
        tracked->commitRetainedBufferStateCommits();
        tracked->commitRetainedTextureStateCommits();
        tracked->commitPendingAccelStructBuildCommits();
        tracked->commitPendingOpacityMicromapBuildCommits();
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
    const VkResult res = m_context.deviceDispatch.vkGetSemaphoreCounterValue(m_context.device, m_trackingSemaphore, &completedValue);
    if(res == VK_SUCCESS)
        // vkQueueWaitIdle() establishes a stronger completion fact than a later timeline query. Never let a stale
        // driver value make already-retired command buffers or descriptor uses appear in flight again.
        m_lastFinishedID = Max(m_lastFinishedID, completedValue);
    else{
        if(res == VK_ERROR_DEVICE_LOST)
            m_device.captureDeviceLoss("queue timeline query");
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to query queue timeline semaphore value: {}"), ResultToString(res));
    }
}

void Queue::waitForIdle(){
    ScopedLock lock(m_mutex);

    VkResult res = VK_SUCCESS;
    {
        ScopedLock hostLock(m_nativeQueue.hostMutex);
        res = m_device.isDeviceLost() ? VK_ERROR_DEVICE_LOST : m_context.deviceDispatch.vkQueueWaitIdle(m_nativeQueue.queue);
    }
    if(res != VK_SUCCESS){
        if(res == VK_ERROR_DEVICE_LOST)
            m_device.captureDeviceLoss("queue wait idle");
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
    if(&cmdBuf->m_queue != this || &cmdBuf->m_context != &m_context){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Cannot recycle a command buffer through a foreign queue"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Command buffer recycle owner mismatch"));
        return;
    }

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
        ScopedLock lock(q->m_mutex);
        q->updateLastFinishedID();
        return q->m_lastFinishedID;
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

