// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "queue_submission_validator.h"
#include "queue_descriptor_submission.h"
#include "arena_names.h"

#include <core/common/log.h>

#include <global/scope_exit.h>
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
    const b8 forceNativeSubmission,
    const u64 maxSameQueueWaitValue
){
    const BatchSubmitEntry entry{
        .ppCmd = ppCmd,
        .numCmd = numCmd,
        .expectedCommandLists = expectedCommandLists,
        .localWaits = localWaits,
        .localWaitCount = localWaitCount,
        .localSignals = localSignals,
        .localSignalCount = localSignalCount,
        .maxSameQueueWaitValue = maxSameQueueWaitValue,
        .forceNativeSubmission = forceNativeSubmission,
    };
    SubmissionReceipt receipt;
    const BatchSubmissionResult result = submitBatch(Span<const BatchSubmitEntry>(&entry, 1u), Span<SubmissionReceipt>(&receipt, 1u));
    if(result.status == BatchSubmissionResult::Accepted)
        return receipt;
    return MakeUnexpected(SubmissionFailure{ result.lastSubmittedID, result.nativeResult });
}

Queue::BatchSubmissionResult Queue::submitBatch(const Span<const BatchSubmitEntry> entries, Span<SubmissionReceipt> outReceipts){
    ScopedLock lock(m_mutex);
    DescriptorSubmission descriptorSubmission(*this);
    static_assert(IsTriviallyCopyable_V<DescriptorHeapUseCommitTicket>, "accepted descriptor-use tickets must remain scalar-only");
    static_assert(IsTriviallyCopyable_V<SubmissionCommandListIdentity>, "accepted submission identities must remain scalar-only");
    auto& descriptorHeapUseCommitTickets = m_submitDescriptorHeapUseCommitTickets;
    auto& validatedTimerQueryCommandBuffers = m_submitValidatedTimerQueryCommandBuffers;
    auto& uniqueCommandLists = m_submitBatchUniqueCommandLists;
    auto& batchEntries = m_submitBatchEntries;
    auto& submitInfos = m_submitBatchInfos;
    auto& waitInfos = m_submitWaitInfos;
    auto& signalInfos = m_submitSignalInfos;
    auto& cmdBufInfos = m_submitCommandBufferInfos;
    descriptorHeapUseCommitTickets.clear();
    validatedTimerQueryCommandBuffers.clear();
    uniqueCommandLists.clear();
    batchEntries.clear();
    submitInfos.clear();
    waitInfos.clear();
    signalInfos.clear();
    cmdBufInfos.clear();
    if(m_device.submissionsBlocked() || entries.empty() || outReceipts.size() != entries.size())
        return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };

    usize totalCmdCount = 0u;
    usize totalWaitCount = 0u;
    usize totalSignalCount = 0u;
    usize nativeEntryCount = 0u;
    for(const BatchSubmitEntry& entry : entries){
        if(entry.numCmd > 0u && !entry.ppCmd){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: command list array is null"));
            return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
        }
        const b8 hasCommands = entry.numCmd > 0u;
        if(hasCommands && !entry.expectedCommandLists){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: expected command-list identity array is null"));
            return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
        }
        if(entry.localWaitCount > 0u && !entry.localWaits){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: local wait array is null"));
            return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
        }
        if(entry.localSignalCount > 0u && !entry.localSignals){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: local signal array is null"));
            return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
        }
        if(hasCommands && entry.numCmd > UINT32_MAX){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: command list count exceeds Vulkan limit"));
            return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
        }
        if(entry.localWaitCount > static_cast<usize>(Limit<u32>::s_Max) || entry.localSignalCount >= static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: queued semaphore count exceeds Vulkan limit"));
            return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
        }
        if(entry.maxSameQueueWaitValue != 0u && (m_trackingSemaphore == VK_NULL_HANDLE || entry.maxSameQueueWaitValue > m_lastSubmittedID)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: dependency token is invalid, unavailable, or unsignalled"));
            return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
        }

        const b8 requiresNativeSubmission = entry.forceNativeSubmission || hasCommands || entry.localWaitCount > 0u || entry.localSignalCount > 0u;
        if(requiresNativeSubmission)
            ++nativeEntryCount;

        totalCmdCount += entry.numCmd;
        totalWaitCount += entry.localWaitCount;
        totalSignalCount += 1u + entry.localSignalCount;
    }

    if(nativeEntryCount != 0u && nativeEntryCount != entries.size())
        return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
    if(nativeEntryCount > 0u && nativeEntryCount > static_cast<usize>(Limit<u64>::s_Max - m_lastSubmittedID)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: queue submission ID exhausted"));
        return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
    }
    if(entries.size() > static_cast<usize>(Limit<u32>::s_Max))
        return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };

    uniqueCommandLists.reserve(totalCmdCount);
    validatedTimerQueryCommandBuffers.reserve(totalCmdCount);
    const SubmissionValidator validator(*this);
    for(const BatchSubmitEntry& entry : entries){
        if(entry.numCmd == 0u)
            continue;
        if(!validator.validate(entry.ppCmd, entry.numCmd, entry.expectedCommandLists))
            return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
        const usize priorCount = uniqueCommandLists.size();
        for(usize i = 0u; i < entry.numCmd; ++i){
            CommandList* const cmdList = entry.ppCmd[i];
            for(usize priorIndex = 0u; priorIndex < priorCount; ++priorIndex){
                if(uniqueCommandLists[priorIndex] == cmdList){
                    NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: command list is duplicated in batch"));
                    return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
                }
            }
            uniqueCommandLists.push_back(cmdList);
        }

        const Span<const SubmissionWait> entryWaits = entry.localWaits ? Span<const SubmissionWait>(entry.localWaits, entry.localWaitCount) : Span<const SubmissionWait>{};
        for(usize i = 0u; i < entry.numCmd; ++i){
            auto& tracked = *MakeNotNull(entry.ppCmd[i]->m_currentCmdBuf.get());
            if(!tracked.validateTimerQueryRecordingClaims(*this, entryWaits, validatedTimerQueryCommandBuffers)){
                NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: timer-query recording order is stale or unresolved"));
                return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
            }
            validatedTimerQueryCommandBuffers.push_back(MakeNotNull(&tracked));
        }
    }

    if(!descriptorSubmission.prepareBatch(entries))
        return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };

    for(const BatchSubmitEntry& entry : entries){
        for(usize i = 0u; i < entry.localSignalCount; ++i){
            if(entry.localSignals[i].semaphore == VK_NULL_HANDLE){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: local signal semaphore is null"));
                return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
            }
        }
    }

    if(nativeEntryCount == 0u){
        for(SubmissionReceipt& receipt : outReceipts)
            receipt = SubmissionReceipt{ m_lastSubmittedID, 0u, false };
        return BatchSubmissionResult{ BatchSubmissionResult::Status::Accepted, m_lastSubmittedID, VK_SUCCESS };
    }

    if(m_trackingSemaphore == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Queue submission skipped because timeline semaphore is unavailable."));
        return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
    }

    waitInfos.reserve(totalWaitCount);
    signalInfos.reserve(totalSignalCount);
    cmdBufInfos.reserve(totalCmdCount);
    batchEntries.reserve(entries.size());
    submitInfos.reserve(entries.size());
    descriptorHeapUseCommitTickets.reserve(totalCmdCount);

    m_submitBatchAcceptedTimelineWaits.assign(m_acceptedTimelineWaits.begin(), m_acceptedTimelineWaits.end());

    for(usize entryIndex = 0u; entryIndex < entries.size(); ++entryIndex){
        const BatchSubmitEntry& entry = entries[entryIndex];
        const u64 entrySubmissionID = m_lastSubmittedID + 1u + static_cast<u64>(entryIndex);
        const QueueSubmissionToken entryToken{ .value = entrySubmissionID, .physicalQueueIndex = m_physicalQueue.index, .deviceGeneration = m_physicalQueue.deviceGeneration, .queue = m_queueID, };

        const usize waitOffset = waitInfos.size();
        usize emittedTimelineWaitCount = 0u;
        for(usize i = 0u; i < entry.localWaitCount; ++i){
            const SubmissionWait& wait = entry.localWaits[i];
            if(wait.semaphore == VK_NULL_HANDLE){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: local wait semaphore is null"));
                return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
            }

            SubmissionWait* acceptedWait = nullptr;
            if(wait.value != 0u){
                for(SubmissionWait& accepted : m_submitBatchAcceptedTimelineWaits){
                    if(accepted.semaphore == wait.semaphore){
                        acceptedWait = &accepted;
                        break;
                    }
                }
                if(!acceptedWait){
                    for(const auto producerQueue : m_device.m_physicalQueues){
                        if(producerQueue->m_trackingSemaphore == wait.semaphore){
                            m_submitBatchAcceptedTimelineWaits.reserve(m_device.m_physicalQueues.size());
                            m_submitBatchAcceptedTimelineWaits.push_back(SubmissionWait{ wait.semaphore, 0u });
                            acceptedWait = &m_submitBatchAcceptedTimelineWaits.back();
                            break;
                        }
                    }
                }
            }
            if(acceptedWait){
                if(wait.value <= acceptedWait->value)
                    continue;
                ++emittedTimelineWaitCount;
                acceptedWait->value = wait.value;
            }

            VkSemaphoreSubmitInfo waitInfo = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            waitInfo.semaphore = wait.semaphore;
            waitInfo.value = wait.value;
            waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            waitInfos.push_back(waitInfo);
        }
        const usize waitCount = waitInfos.size() - waitOffset;

        const usize signalOffset = signalInfos.size();
        auto timelineSignal = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        timelineSignal.semaphore = m_trackingSemaphore;
        timelineSignal.value = entrySubmissionID;
        timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalInfos.push_back(timelineSignal);

        for(usize i = 0u; i < entry.localSignalCount; ++i){
            VkSemaphoreSubmitInfo signalInfo = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            signalInfo.semaphore = entry.localSignals[i].semaphore;
            signalInfo.value = entry.localSignals[i].value;
            signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signalInfos.push_back(signalInfo);
        }
        const usize signalCount = signalInfos.size() - signalOffset;

        const usize cmdOffset = cmdBufInfos.size();
        for(usize i = 0u; i < entry.numCmd; ++i){
            auto& commandList = *MakeNotNull(entry.ppCmd[i]);
            auto& tracked = *MakeNotNull(commandList.m_currentCmdBuf.get());
            if(!validateCommandBufferSubmissionState(tracked))
                return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
            if(!tracked.validatePendingAccelStructBuildCommits()){
                NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Native submission cannot publish acceleration-structure signatures across allocator domains"));
                return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
            }
            if(tracked.hasRetainedTopLevelAccelStructs()){
                tracked.retainAcceptedTopLevelAccelStructReferences();
                for(usize buildEntry = 0u; buildEntry <= entryIndex; ++buildEntry){
                    for(usize buildIndex = 0u; buildIndex < entries[buildEntry].numCmd; ++buildIndex)
                        tracked.retainPendingTopLevelAccelStructReferences(*entries[buildEntry].ppCmd[buildIndex]->m_currentCmdBuf);
                }
            }
            if(!descriptorSubmission.validateHeapUses(tracked, entryToken))
                return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };

            auto commandBufferInfo = VulkanDetail::MakeVkStruct<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
            commandBufferInfo.commandBuffer = tracked.m_cmdBuf;
            cmdBufInfos.push_back(commandBufferInfo);
        }
        const usize cmdCount = cmdBufInfos.size() - cmdOffset;

        batchEntries.push_back(BatchPreparedEntry{
            .waitOffset = waitOffset,
            .waitCount = waitCount,
            .cmdOffset = cmdOffset,
            .cmdCount = cmdCount,
            .signalOffset = signalOffset,
            .signalCount = signalCount,
            .timelineWaitCount = emittedTimelineWaitCount,
            .submissionID = entrySubmissionID,
        });
    }

    for(const BatchPreparedEntry& prepared : batchEntries){
        auto submitInfo = VulkanDetail::MakeVkStruct<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
        submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(prepared.waitCount);
        submitInfo.pWaitSemaphoreInfos = prepared.waitCount > 0u ? waitInfos.data() + prepared.waitOffset : nullptr;
        submitInfo.commandBufferInfoCount = static_cast<uint32_t>(prepared.cmdCount);
        submitInfo.pCommandBufferInfos = prepared.cmdCount > 0u ? cmdBufInfos.data() + prepared.cmdOffset : nullptr;
        submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(prepared.signalCount);
        submitInfo.pSignalSemaphoreInfos = signalInfos.data() + prepared.signalOffset;
        submitInfos.push_back(submitInfo);
    }

    const auto finalizeDetachedRecordingAttempts = [&](const b8 accepted)noexcept{
        for(const BatchSubmitEntry& entry : entries){
            for(usize i = 0u; i < entry.numCmd; ++i){
                CommandList* const commandList = entry.ppCmd[i];
                if(!commandList || commandList->m_recordingLeaseSerial != entry.expectedCommandLists[i].recordingLeaseSerial)
                    TerminateInvariant();

                if(accepted)
                    commandList->m_stateTracker.commitRecordingAttempt();
                else
                    commandList->m_stateTracker.rollbackRecordingAttempt();
            }
        }
    };
    const usize batchStartIndex = m_commandBuffersInFlight.size();
    if(totalCmdCount > 0u){
        ScopeExit rollbackInFlightPush([&]()noexcept{ m_commandBuffersInFlight.resize(batchStartIndex); });
        for(const BatchSubmitEntry& entry : entries){
            for(usize i = 0u; i < entry.numCmd; ++i)
                m_commandBuffersInFlight.push_back(entry.ppCmd[i]->m_currentCmdBuf);
        }
        rollbackInFlightPush.release();

        usize cmdCursor = batchStartIndex;
        for(usize entryIndex = 0u; entryIndex < entries.size(); ++entryIndex){
            const BatchSubmitEntry& entry = entries[entryIndex];
            const u64 entrySubmissionID = batchEntries[entryIndex].submissionID;
            for(usize i = 0u; i < entry.numCmd; ++i, ++cmdCursor){
                auto& commandList = *MakeNotNull(entry.ppCmd[i]);
                TrackedCommandBufferPtr& tracked = m_commandBuffersInFlight[cmdCursor];
                tracked->m_submissionID = entrySubmissionID;
                commandList.m_currentCmdBuf = nullptr;
                commandList.m_nativeRecordingID = 0u;
            }
        }
    }

    VkResult res = VK_SUCCESS;
    b8 submissionSuppressed = false;
    {
        ScopedLock hostLock(m_nativeQueue.hostMutex);
        submissionSuppressed = m_device.submissionsBlocked();
        if(!submissionSuppressed)
            res = m_context.deviceDispatch.vkQueueSubmit2(m_nativeQueue.queue, static_cast<uint32_t>(submitInfos.size()), submitInfos.data(), VK_NULL_HANDLE);
    }

    if(submissionSuppressed || res != VK_SUCCESS){
        descriptorSubmission.releaseLocks();
        if(res == VK_ERROR_DEVICE_LOST)
            m_device.markDeviceLost();
        finalizeDetachedRecordingAttempts(false);
        for(usize i = 0u; i < totalCmdCount; ++i){
            TrackedCommandBufferPtr rejected = Move(m_commandBuffersInFlight.back());
            m_commandBuffersInFlight.pop_back();
            recycleCommandBuffer(Move(rejected));
        }

        if(submissionSuppressed){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Queue submission was suppressed because the device requires recreation."));
        }
        else if(res == VK_ERROR_OUT_OF_HOST_MEMORY || res == VK_ERROR_OUT_OF_DEVICE_MEMORY){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Queue submission was rejected: {}"), ResultToString(res));
        }
        else if(res != VK_ERROR_DEVICE_LOST){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to submit command buffers to queue: {}"), ResultToString(res));
        }

        return BatchSubmissionResult{ BatchSubmissionResult::Status::NativeSubmitFailed, m_lastSubmittedID, res };
    }

    m_lastSubmittedID = batchEntries.back().submissionID;
    Swap(m_acceptedTimelineWaits, m_submitBatchAcceptedTimelineWaits);

    descriptorSubmission.commitAndRelease();
    finalizeDetachedRecordingAttempts(true);

    usize cmdCursor = batchStartIndex;
    for(usize entryIndex = 0u; entryIndex < entries.size(); ++entryIndex){
        const BatchSubmitEntry& entry = entries[entryIndex];
        const BatchPreparedEntry& prepared = batchEntries[entryIndex];
        const QueueSubmissionToken entryToken{ .value = prepared.submissionID, .physicalQueueIndex = m_physicalQueue.index, .deviceGeneration = m_physicalQueue.deviceGeneration, .queue = m_queueID, };
        for(usize i = 0u; i < entry.numCmd; ++i, ++cmdCursor){
            TrackedCommandBufferPtr& tracked = m_commandBuffersInFlight[cmdCursor];
            tracked->commitTimerQueryRecordingClaims(entryToken);
            tracked->commitRetainedBufferStateCommits();
            tracked->commitRetainedTextureStateCommits();
            tracked->commitPendingAccelStructBuildCommits();
            tracked->commitPendingOpacityMicromapBuildCommits();
            commitCommandBufferStateTransition(*tracked, TrackedCommandBufferArenaState::Pending);
        }
        outReceipts[entryIndex] = SubmissionReceipt{ prepared.submissionID, prepared.timelineWaitCount, true };
    }
    return BatchSubmissionResult{ BatchSubmissionResult::Status::Accepted, m_lastSubmittedID, VK_SUCCESS };
}

VkResult Queue::updateLastFinishedID(){
    if(!m_trackingSemaphore){
        m_lastFinishedID = m_lastSubmittedID;
        return VK_SUCCESS;
    }

    u64 completedValue = 0;
    const VkResult res = m_context.deviceDispatch.vkGetSemaphoreCounterValue(m_context.device, m_trackingSemaphore, &completedValue);
    if(res == VK_SUCCESS)
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
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Queue wait-for-idle failed: {}"), ResultToString(res));
}

void Queue::recycleCommandBuffer(TrackedCommandBufferPtr cmdBuf){
    FOUNDATION_ASSERT(cmdBuf);
    if(&cmdBuf->m_queue != this || &cmdBuf->m_context != &m_context){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Cannot recycle a command buffer through a foreign queue"));
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Command buffer recycle owner mismatch"));
        return;
    }

    cmdBuf->clearTrackedReferences();
    transitionCommandBufferState(*cmdBuf, TrackedCommandBufferArenaState::Reusable);
    if(cmdBuf->m_recordingWorkerIndex == 0u){
        m_commandBuffersPool.push_back(Move(cmdBuf));
        return;
    }

    WorkerCommandArena* const workerArena = findWorkerCommandArena(cmdBuf->m_recordingWorkerDomain, cmdBuf->m_recordingWorkerIndex);
    if(!workerArena){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Cannot recycle command buffer because physical queue {} worker {}:{} no longer has a command arena"), m_physicalQueue.index, cmdBuf->m_recordingWorkerDomain, cmdBuf->m_recordingWorkerIndex);
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Worker command buffer lost its owning command arena"));
        m_commandBuffersPool.push_back(Move(cmdBuf));
        return;
    }

    ScopedLock lock(workerArena->mutex);
    workerArena->commandBuffersPool.push_back(Move(cmdBuf));
}

b8 Queue::coversTimerQueryPrerequisite(const QueueSubmissionToken& prerequisite, const b8 prerequisiteObservedComplete, const Span<const SubmissionWait> localWaits)const noexcept{
    if(!prerequisite.valid() || prerequisiteObservedComplete)
        return true;
    if(prerequisite.matchesPhysicalQueue(m_physicalQueue.index, m_physicalQueue.deviceGeneration))
        return true;
    if(!prerequisite.hasPhysicalQueueIdentity() || prerequisite.deviceGeneration != m_device.m_deviceGeneration || prerequisite.physicalQueueIndex >= m_device.m_physicalQueues.size())
        return false;

    const auto producerQueue = m_device.m_physicalQueues[prerequisite.physicalQueueIndex];
    FOUNDATION_ASSERT(producerQueue->m_physicalQueue.deviceGeneration == prerequisite.deviceGeneration);
    if(producerQueue->m_queueID != prerequisite.queue || producerQueue->m_trackingSemaphore == VK_NULL_HANDLE)
        return false;

    const auto coveredBy = [&](const VkSemaphore semaphore, const u64 value)noexcept -> b8 {
        return semaphore == producerQueue->m_trackingSemaphore && value >= prerequisite.value;
    };
    for(const SubmissionWait& localWait : localWaits){
        if(coveredBy(localWait.semaphore, localWait.value))
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u64> Device::queueGetCompletedInstance(CommandQueue::Enum queue){
    return queueGetCompletedInstance(getPrimaryPhysicalQueue(queue));
}

Expected<u64> Device::queueGetCompletedInstance(const GpuPhysicalQueueId& queue){
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
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Failed to query queue timeline semaphore value: {}"), ResultToString(result));
        return MakeUnexpected(Failure{});
    }
    return completedInstance;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

