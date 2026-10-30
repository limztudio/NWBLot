// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Queue::SubmissionReceipt, Queue::SubmissionFailure> Queue::submit(
    CommandList* const* ppCmd,
    const usize numCmd,
    const SubmissionCommandListIdentity* const expectedCommandLists,
    const SubmissionWait* const localWaits,
    const usize localWaitCount,
    const SubmissionSignal* const localSignals,
    const usize localSignalCount,
    const b8 forceNativeSubmission
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
    if(m_device.submissionsBlocked())
        return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });

    if(numCmd > 0u && !ppCmd){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list array is null"));
        return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
    }
    const b8 hasCommands = numCmd > 0u;
    if(hasCommands && !expectedCommandLists){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: expected command-list identity array is null"));
        return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
    }
    // Queue-global synchronization belongs to the next accepted native submission.
    // Validation and injected pre-driver rejection must leave it pending, especially when it contains the acquired swap-chain semaphore.
    if(localWaitCount > 0u && !localWaits){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: local wait array is null"));
        return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
    }
    if(localSignalCount > 0u && !localSignals){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: local signal array is null"));
        return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
    }

    const b8 hasPendingSemaphores = localWaitCount > 0u || localSignalCount > 0u;
    const b8 requiresNativeSubmission = forceNativeSubmission || hasCommands || hasPendingSemaphores;

    if(hasCommands && numCmd > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: command list count exceeds Vulkan limit"));
        return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
    }
    if(
        localWaitCount > static_cast<usize>(Limit<u32>::s_Max)
        || localSignalCount >= static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: queued semaphore count exceeds Vulkan limit"));
        return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
    }
    if(requiresNativeSubmission && m_lastSubmittedID == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: queue submission ID exhausted"));
        return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
    }
    if(hasCommands){
        for(usize i = 0u; i < numCmd; ++i){
            auto* cmdList = ppCmd[i];
            const SubmissionCommandListIdentity& expected = expectedCommandLists[i];
            for(usize previous = 0u; previous < i; ++previous){
                if(ppCmd[previous] == cmdList){
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} is duplicated"), i);
                    return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
                }
            }
            if(!cmdList || &cmdList->m_device != &m_device){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} is null or foreign"), i);
                return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
            }
            if(!cmdList->matchesSubmissionLease(m_physicalQueue, m_queueID, expected.graphSubmissionAuthorized)){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Command-list lease provenance does not match execution queue"));
                return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
            }
            if(!cmdList->m_currentCmdBuf || cmdList->m_currentCmdBuf->m_cmdBuf == VK_NULL_HANDLE){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} has no native command buffer"), i);
                return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
            }
            if(cmdList->m_commandRecordingFailed){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: a command list has a sticky native recording failure"));
                return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
            }
            if(cmdList->m_isRecording){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} is still recording"), i);
                return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
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
                return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
            }
            if(!cmdList->validateTrackedResourcesReadyForSubmission())
                return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
        }
    }

    if(hasCommands){
        validatedTimerQueryCommandBuffers.reserve(numCmd);
        for(usize i = 0u; i < numCmd; ++i){
            auto& tracked = *MakeNotNull(ppCmd[i]->m_currentCmdBuf.get());
            if(!tracked.validateTimerQueryRecordingClaims(*this, localWaits, localWaitCount, validatedTimerQueryCommandBuffers.empty() ? nullptr : validatedTimerQueryCommandBuffers.data(), validatedTimerQueryCommandBuffers.size())){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: timer-query recording order is stale or unresolved"));
                return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
            }
            validatedTimerQueryCommandBuffers.push_back(MakeNotNull(&tracked));
        }
    }

    b8 requiresDescriptorBufferLifecycle = false;
    if(hasCommands){
        for(usize i = 0u; i < numCmd; ++i){
            auto& tracked = *MakeNotNull(ppCmd[i]->m_currentCmdBuf.get());
            requiresDescriptorBufferLifecycle |= tracked.m_descriptorBufferManager != nullptr;
            for(const auto heap : tracked.m_referencedDescriptorHeaps){

                if(heap.get() != &m_device.m_gpuDescriptorHeap){
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Command buffer references a foreign descriptor heap."));
                    return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
                }
                submissionDescriptorHeap = heap.get();
            }
        }
    }

    // Global hierarchy: queue -> descriptor heap -> descriptor-buffer lifecycle.
    if(submissionDescriptorHeap)
        descriptorHeapLock = UniqueLock<Futex>(submissionDescriptorHeap->m_mutex);
    if(requiresDescriptorBufferLifecycle && descriptorBufferManager)
        descriptorBufferLifecycleLock = UniqueLock<Futex>(descriptorBufferManager->m_lifecycleMutex);

    if(hasCommands){
        for(usize i = 0u; i < numCmd; ++i){
            auto& cmdList = *MakeNotNull(ppCmd[i]);
            auto& tracked = *MakeNotNull(cmdList.m_currentCmdBuf.get());
            if(
                (cmdList.m_descriptorBuffersBound && !tracked.m_descriptorBufferManager)
                || (!tracked.m_descriptorBufferManager && tracked.m_descriptorBufferGeneration != 0u)
                || (tracked.m_descriptorBufferManager && tracked.m_descriptorBufferGeneration == 0u)
                || (
                    tracked.m_descriptorBufferManager
                    && (
                        tracked.m_descriptorBufferManager != descriptorBufferManager
                        || descriptorBufferManager != &m_device.m_descriptorBufferManager
                        || !descriptorBufferManager->m_enabled
                        || descriptorBufferManager->m_lifecycleTransitioning
                        || descriptorBufferManager->m_bindingGeneration == 0u
                        || tracked.m_descriptorBufferGeneration != descriptorBufferManager->m_bindingGeneration
                    )
                )
            ){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: descriptor-buffer binding generation is stale"));
                return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
            }
        }
    }
    for(usize i = 0u; i < localSignalCount; ++i){
        if(localSignals[i].semaphore == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: local signal semaphore is null"));
            return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
        }
    }

    if(!requiresNativeSubmission)
        return SubmissionReceipt{ m_lastSubmittedID, 0u, false };

    if(m_trackingSemaphore == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Queue submission skipped because timeline semaphore is unavailable."));
        return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
    }

    const u64 submissionID = m_lastSubmittedID + 1u;
    const QueueSubmissionToken submissionToken{ .value = submissionID, .physicalQueueIndex = m_physicalQueue.index, .deviceGeneration = m_physicalQueue.deviceGeneration, .queue = m_queueID, };

    auto timelineSignal = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    timelineSignal.semaphore = m_trackingSemaphore;
    timelineSignal.value = submissionID;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    waitInfos.reserve(localWaitCount);
    usize emittedTimelineWaitCount = 0u;
    for(usize i = 0u; i < localWaitCount; ++i){
        const SubmissionWait& wait = localWaits[i];
        if(wait.semaphore == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: local wait semaphore is null"));
            return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
        }

        SubmissionWait* acceptedWait = nullptr;
        if(wait.value != 0u){
            for(SubmissionWait& accepted : m_acceptedTimelineWaits){
                if(accepted.semaphore == wait.semaphore){
                    acceptedWait = &accepted;
                    break;
                }
            }
            if(!acceptedWait){
                for(const auto producerQueue : m_device.m_physicalQueues){

                    if(producerQueue->m_trackingSemaphore == wait.semaphore){
                        m_acceptedTimelineWaits.reserve(m_device.m_physicalQueues.size());
                        m_acceptedTimelineWaits.push_back(SubmissionWait{ wait.semaphore, 0u });
                        acceptedWait = &m_acceptedTimelineWaits.back();
                        break;
                    }
                }
            }
        }
        if(acceptedWait){
            if(wait.value <= acceptedWait->value)
                continue;
            ++emittedTimelineWaitCount;
        }

        VkSemaphoreSubmitInfo waitInfo = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        waitInfo.semaphore = wait.semaphore;
        waitInfo.value = wait.value;
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitInfos.push_back(waitInfo);
    }

    signalInfos.reserve(1u + localSignalCount);
    signalInfos.push_back(timelineSignal);

    for(usize i = 0u; i < localSignalCount; ++i){
        VkSemaphoreSubmitInfo signalInfo = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        signalInfo.semaphore = localSignals[i].semaphore;
        signalInfo.value = localSignals[i].value;
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalInfos.push_back(signalInfo);
    }

    if(hasCommands){
        descriptorHeapUseCommitTickets.reserve(numCmd);
        cmdBufInfos.reserve(numCmd);
        for(usize i = 0u; i < numCmd; ++i){
            auto& commandList = *MakeNotNull(ppCmd[i]);
            auto commandBufferInfo = VulkanDetail::MakeVkStruct<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
            commandBufferInfo.commandBuffer = commandList.m_currentCmdBuf->m_cmdBuf;
            cmdBufInfos.push_back(commandBufferInfo);
        }
    }
    NWB_ASSERT(preparedCommandBuffers.get_allocator() == m_commandBuffersInFlight.get_allocator());
    for(usize i = 0u; i < numCmd; ++i){
        auto& tracked = *MakeNotNull(ppCmd[i]->m_currentCmdBuf.get());
        if(!validateCommandBufferSubmissionState(tracked))
            return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
        if(!tracked.validatePendingAccelStructBuildCommits()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Native submission cannot publish acceleration-structure signatures across allocator domains"));
            return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
        }
        if(!tracked.m_referencedTopLevelAccelStructs.empty()){
            // Trace packets retain the selected BLAS sets until their own queue completion.
            tracked.retainAcceptedTopLevelAccelStructReferences();
            for(usize buildIndex = 0u; buildIndex < numCmd; ++buildIndex)
                tracked.retainPendingTopLevelAccelStructReferences(*ppCmd[buildIndex]->m_currentCmdBuf);
        }
        for(const auto heap : tracked.m_referencedDescriptorHeaps){
            const auto heapUseIndex = heap->validateCommandBufferUseSubmissionLocked(tracked, submissionToken);
            if(!heapUseIndex)
                return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
            for(const DescriptorHeapUseCommitTicket& ticket : descriptorHeapUseCommitTickets){
                if(ticket.heap == heap && ticket.heapUseIndex == *heapUseIndex){
                    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Native submission contains a duplicate descriptor-heap use."));
                    return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, VK_SUCCESS });
                }
            }
            descriptorHeapUseCommitTickets.push_back(DescriptorHeapUseCommitTicket{ .heap = heap, .commandBuffer = MakeNotNull(&tracked), .heapUseIndex = *heapUseIndex, });
        }
    }

    auto submitInfo = VulkanDetail::MakeVkStruct<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
    submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
    submitInfo.pWaitSemaphoreInfos = waitInfos.data();
    submitInfo.commandBufferInfoCount = static_cast<uint32_t>(cmdBufInfos.size());
    submitInfo.pCommandBufferInfos = cmdBufInfos.data();
    submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
    submitInfo.pSignalSemaphoreInfos = signalInfos.data();

    const auto finalizeDetachedRecordingAttempts = [&](const b8 accepted)noexcept{
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

        auto& lifecycleMutex = *MakeNotNull(descriptorBufferLifecycleLock.release());
        lifecycleMutex.unlock();
    };
    const auto releaseDescriptorHeap = [&]()noexcept{
        if(!descriptorHeapLock.owns_lock())
            return;

        auto& heapMutex = *MakeNotNull(descriptorHeapLock.release());
        heapMutex.unlock();
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
        auto& commandList = *MakeNotNull(ppCmd[i]);
        TrackedCommandBufferPtr& tracked = *detachedCommandBuffer;
        tracked->m_submissionID = submissionID;
        commandList.m_currentCmdBuf = nullptr;
        commandList.m_nativeRecordingID = 0u;
    }

    VkResult res = VK_SUCCESS;
    b8 submissionSuppressed = false;
    {
        ScopedLock hostLock(m_nativeQueue.hostMutex);
        submissionSuppressed = m_device.submissionsBlocked();
        if(!submissionSuppressed)
            res = m_context.deviceDispatch.vkQueueSubmit2(m_nativeQueue.queue, 1, &submitInfo, VK_NULL_HANDLE);
    }

    if(submissionSuppressed || res != VK_SUCCESS){
        releaseDescriptorBufferLifecycle();
        releaseDescriptorHeap();
        if(res == VK_ERROR_DEVICE_LOST)
            m_device.markDeviceLost();
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

        return MakeUnexpected(SubmissionFailure{ m_lastSubmittedID, res });
    }

    m_lastSubmittedID = submissionID;
    for(usize i = 0u; i < localWaitCount; ++i){
        const SubmissionWait& wait = localWaits[i];
        if(wait.value == 0u)
            continue;
        for(SubmissionWait& accepted : m_acceptedTimelineWaits){
            if(accepted.semaphore == wait.semaphore){
                accepted.value = Max(accepted.value, wait.value);
                break;
            }
        }
    }

    for(const DescriptorHeapUseCommitTicket& ticket : descriptorHeapUseCommitTickets)
        ticket.heap->commitCommandBufferUseSubmissionLocked(*ticket.commandBuffer, submissionToken, ticket.heapUseIndex);
    releaseDescriptorBufferLifecycle();
    releaseDescriptorHeap();
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
    return SubmissionReceipt{ submissionID, emittedTimelineWaitCount, true };
}

VkResult Queue::updateLastFinishedID(){
    if(!m_trackingSemaphore){
        m_lastFinishedID = m_lastSubmittedID;
        return VK_SUCCESS;
    }

    u64 completedValue = 0;
    const VkResult res = m_context.deviceDispatch.vkGetSemaphoreCounterValue(m_context.device, m_trackingSemaphore, &completedValue);
    if(res == VK_SUCCESS)
        // vkQueueWaitIdle() beats a later timeline query. Never let a stale driver value resurrect retired buffers.
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

Queue::CommandBufferList::iterator Queue::recycleCommandBuffer(CommandBufferList& source, const CommandBufferList::iterator commandBuffer){
    auto next = commandBuffer;
    ++next;

    TrackedCommandBufferPtr& cmdBuf = *commandBuffer;
    NWB_ASSERT(cmdBuf);
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

    WorkerCommandArena* const workerArena = findWorkerCommandArena(cmdBuf->m_recordingWorkerDomain, cmdBuf->m_recordingWorkerIndex);
    if(!workerArena){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot recycle command buffer because physical queue {} worker {}:{} no longer has a command arena"), m_physicalQueue.index, cmdBuf->m_recordingWorkerDomain, cmdBuf->m_recordingWorkerIndex);
        NWB_ASSERT_MSG(false, NWB_TEXT("Worker command buffer lost its owning command arena"));
        NWB_ASSERT(source.get_allocator() == m_commandBuffersPool.get_allocator());
        m_commandBuffersPool.splice(m_commandBuffersPool.end(), source, commandBuffer);
        return next;
    }

    // Submission/retirement holds m_mutex here. Worker recording never takes m_mutex, so no lock cycle.
    ScopedLock lock(workerArena->mutex);

    NWB_ASSERT(source.get_allocator() == workerArena->commandBuffersPool.get_allocator());
    workerArena->commandBuffersPool.splice(workerArena->commandBuffersPool.end(), source, commandBuffer);
    return next;
}

b8 Queue::coversTimerQueryPrerequisite(const QueueSubmissionToken& prerequisite, const b8 prerequisiteObservedComplete, const SubmissionWait* const localWaits, const usize localWaitCount)const noexcept{
    if(!prerequisite.valid() || prerequisiteObservedComplete)
        return true;
    if(prerequisite.matchesPhysicalQueue(m_physicalQueue.index, m_physicalQueue.deviceGeneration))
        return true;
    if(!prerequisite.hasPhysicalQueueIdentity() || prerequisite.deviceGeneration != m_device.m_deviceGeneration || prerequisite.physicalQueueIndex >= m_device.m_physicalQueues.size())
        return false;

    const auto producerQueue = m_device.m_physicalQueues[prerequisite.physicalQueueIndex];
    NWB_ASSERT(producerQueue->m_physicalQueue.deviceGeneration == prerequisite.deviceGeneration);
    if(producerQueue->m_queueID != prerequisite.queue || producerQueue->m_trackingSemaphore == VK_NULL_HANDLE)
        return false;

    const auto coveredBy = [&](const VkSemaphore semaphore, const u64 value)noexcept -> b8 {
        return semaphore == producerQueue->m_trackingSemaphore && value >= prerequisite.value;
    };
    for(usize waitIndex = 0u; waitIndex < localWaitCount; ++waitIndex){
        if(coveredBy(localWaits[waitIndex].semaphore, localWaits[waitIndex].value))
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> Device::queueGetCompletedInstance(CommandQueue::Enum queue){
    return queueGetCompletedInstance(getPrimaryPhysicalQueue(queue));
}

Expected<u64> Device::queueGetCompletedInstance(const GpuPhysicalQueueId& queue){
    // Rejected-submit cleanup retires CPU-side bookkeeping; device-lost already marked the device terminal, so skip the second timeline query here.
    if(isDeviceLost())
        return MakeUnexpected(Failure{});

    Queue* q = getQueue(queue);
    if(!q)
        return MakeUnexpected(Failure{});

    VkResult result = VK_SUCCESS;
    u64 completedInstance = 0u;
    {
        ScopedLock lock(q->m_mutex);
        result = q->updateLastFinishedID();
        completedInstance = q->m_lastFinishedID;
    }
    if(result == VK_ERROR_DEVICE_LOST)
        captureDeviceLoss(VulkanArenaScope::s_QueueTimelineQueryContext);
    if(result != VK_SUCCESS){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to query queue timeline semaphore value: {}"), ResultToString(result));
        return MakeUnexpected(Failure{});
    }
    return completedInstance;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

