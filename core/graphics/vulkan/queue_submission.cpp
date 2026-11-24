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

Queue::BatchSubmissionResult Queue::submitBatch(const Span<const BatchSubmitEntry> entries, Span<SubmissionReceipt> outReceipts, const b8 callerPreValidated){
    ScopedLock lock(m_mutex);
    DescriptorSubmission descriptorSubmission(*this);
    static_assert(IsTriviallyCopyable_V<DescriptorHeapUseCommitTicket>, "accepted descriptor-use tickets must remain scalar-only");
    static_assert(IsTriviallyCopyable_V<SubmissionCommandListIdentity>, "accepted submission identities must remain scalar-only");
    auto& descriptorHeapUseCommitTickets = m_submitDescriptorHeapUseCommitTickets;
    auto& validatedTimerQueryCommandBuffers = m_submitValidatedTimerQueryCommandBuffers;
    auto& batchEntries = m_submitBatchEntries;
    auto& submitInfos = m_submitBatchInfos;
    auto& waitInfos = m_submitWaitInfos;
    auto& signalInfos = m_submitSignalInfos;
    auto& cmdBufInfos = m_submitCommandBufferInfos;
    descriptorHeapUseCommitTickets.clear();
    validatedTimerQueryCommandBuffers.clear();
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
        if(entry.localSignalCount > 0u){
            if(!entry.localSignals){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: local signal array is null"));
                return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
            }
            for(usize i = 0u; i < entry.localSignalCount; ++i){
                if(entry.localSignals[i].semaphore == VK_NULL_HANDLE){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: local signal semaphore is null"));
                    return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
                }
            }
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

    if(nativeEntryCount == 0u){
        for(SubmissionReceipt& receipt : outReceipts)
            receipt = SubmissionReceipt{ m_lastSubmittedID, 0u, false };
        return BatchSubmissionResult{ BatchSubmissionResult::Status::Accepted, m_lastSubmittedID, VK_SUCCESS };
    }
    if(m_trackingSemaphore == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Queue submission skipped because timeline semaphore is unavailable."));
        return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
    }

    u64 batchStamp = m_nextSubmitBatchStamp++;
    if(batchStamp == 0u)
        batchStamp = m_nextSubmitBatchStamp++;
    if(totalCmdCount > 0u)
        validatedTimerQueryCommandBuffers.reserve(totalCmdCount);
    waitInfos.reserve(totalWaitCount);
    signalInfos.reserve(totalSignalCount);
    cmdBufInfos.reserve(totalCmdCount);
    batchEntries.reserve(entries.size());
    submitInfos.reserve(entries.size());

    if(totalWaitCount > 0u)
        m_submitBatchAcceptedTimelineWaits.assign(m_acceptedTimelineWaits.begin(), m_acceptedTimelineWaits.end());

    const SubmissionValidator validator(*this);
    u64 batchDirectCmdCount = 0u;
    GpuDescriptorHeap* submissionDescriptorHeap = nullptr;
    b8 requiresDescriptorBufferLifecycle = false;
    b8 requiresDescriptorBufferValidation = false;
    b8 hasRetainedTlas = false;
    for(usize entryIndex = 0u; entryIndex < entries.size(); ++entryIndex){
        const BatchSubmitEntry& entry = entries[entryIndex];

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

        const usize cmdOffset = cmdBufInfos.size();
        b8 hasTimerQueries = false;
        const Span<const SubmissionWait> entryWaits = entry.localWaits ? Span<const SubmissionWait>(entry.localWaits, entry.localWaitCount) : Span<const SubmissionWait>{};
        for(usize i = 0u; i < entry.numCmd; ++i){
            CommandList* const commandList = entry.ppCmd[i];
            if(callerPreValidated){
                const SubmissionCommandListIdentity& expected = entry.expectedCommandLists[i];
                const b8 fastValid =
                    commandList
                    && commandList->m_graphPublicationState.load(MemoryOrder::relaxed) == CommandList::s_GraphPublicationSubmitting
                    && commandList->m_graphRecordingOwnershipSerial.load(MemoryOrder::relaxed) == expected.graphRecordingOwnershipSerial
                    && commandList->m_recordingLeaseSerial == expected.recordingLeaseSerial
                    && commandList->m_currentCmdBuf.get() == expected.owner
                    && commandList->validateTrackedResourcesReadyForSubmissionFast()
                ;
                if(!fastValid && !validator.validateCommandList(commandList, expected, i, batchStamp))
                    return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
            }
            else if(!validator.validateCommandList(commandList, entry.expectedCommandLists[i], i, batchStamp))
                return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
            TrackedCommandBuffer& tracked = *MakeNotNull(commandList->m_currentCmdBuf.get());
            if(tracked.m_recordingWorkerIndex == 0u)
                ++batchDirectCmdCount;
            else{
                const WorkerCommandArena* const workerArena = tracked.m_workerCommandArena
                    ? static_cast<const WorkerCommandArena*>(tracked.m_workerCommandArena)
                    : findWorkerCommandArena(tracked.m_recordingWorkerDomain, tracked.m_recordingWorkerIndex);
                if(!workerArena || workerArena->leasedCommandBufferCount.load(MemoryOrder::relaxed) == 0u){
                    NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Native submission could not validate its worker command-buffer arena"));
                    return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
                }
            }
            const u8 commitFlags = tracked.m_pendingCommitFlags;
            if((commitFlags & TrackedCommandBuffer::s_CommitTimerQueries) != 0u){
                if(!tracked.validateTimerQueryRecordingClaims(*this, entryWaits, validatedTimerQueryCommandBuffers)){
                    NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: timer-query recording order is stale or unresolved"));
                    return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
                }
                validatedTimerQueryCommandBuffers.push_back(MakeNotNull(&tracked));
                hasTimerQueries = true;
            }
            if((commitFlags & TrackedCommandBuffer::s_CommitAccelStructBuilds) != 0u && !tracked.validatePendingAccelStructBuildCommits()){
                NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Native submission cannot publish acceleration-structure signatures across allocator domains"));
                return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
            }
            if(tracked.hasRetainedTopLevelAccelStructs())
                hasRetainedTlas = true;

            requiresDescriptorBufferLifecycle |= tracked.m_descriptorBufferManager != nullptr;
            requiresDescriptorBufferValidation |= (tracked.m_descriptorBufferManager != nullptr || commandList->m_descriptorBuffersBound || tracked.m_descriptorBufferGeneration != 0u);
            for(const auto heap : tracked.m_referencedDescriptorHeaps){
                if(heap.get() != &m_device.m_gpuDescriptorHeap){
                    NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Command buffer references a foreign descriptor heap."));
                    return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
                }
                submissionDescriptorHeap = heap.get();
            }

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
            .signalOffset = 0u,
            .signalCount = 0u,
            .timelineWaitCount = emittedTimelineWaitCount,
            .submissionID = 0u,
            .hasTimerQueries = hasTimerQueries,
        });
    }

    if(totalCmdCount > 0u){
        if(m_leasedCommandBufferCount.load(MemoryOrder::relaxed) < totalCmdCount){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Native submission observed an empty leased command-buffer count"));
            return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
        }
        if(batchDirectCmdCount != 0u && m_directLeasedCommandBufferCount.load(MemoryOrder::relaxed) < batchDirectCmdCount){
            NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Native submission observed an empty direct leased command-buffer count"));
            return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
        }
    }

    if(!descriptorSubmission.lockAndValidate(entries, submissionDescriptorHeap, requiresDescriptorBufferLifecycle, requiresDescriptorBufferValidation))
        return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };

    if(hasRetainedTlas){
        for(usize entryIndex = 0u; entryIndex < entries.size(); ++entryIndex){
            const BatchSubmitEntry& entry = entries[entryIndex];
            for(usize i = 0u; i < entry.numCmd; ++i){
                TrackedCommandBuffer& tracked = *MakeNotNull(entry.ppCmd[i]->m_currentCmdBuf.get());
                if(!tracked.hasRetainedTopLevelAccelStructs())
                    continue;
                tracked.retainAcceptedTopLevelAccelStructReferences();
                for(usize buildEntry = 0u; buildEntry <= entryIndex; ++buildEntry){
                    for(usize buildIndex = 0u; buildIndex < entries[buildEntry].numCmd; ++buildIndex)
                        tracked.retainPendingTopLevelAccelStructReferences(*entries[buildEntry].ppCmd[buildIndex]->m_currentCmdBuf);
                }
            }
        }
    }

    if(descriptorSubmission.hasReferencedHeaps())
        descriptorHeapUseCommitTickets.reserve(totalCmdCount);

    usize groupStart = 0u;
    u64 groupIndex = 0u;
    while(groupStart < entries.size()){
        usize groupEnd = groupStart;
        while(
            groupEnd + 1u < entries.size()
            && entries[groupEnd].allowSignalCoalescing
            && entries[groupEnd].localSignalCount == 0u
            && batchEntries[groupEnd + 1u].waitCount == 0u
            && batchEntries[groupEnd + 1u].cmdOffset == batchEntries[groupEnd].cmdOffset + batchEntries[groupEnd].cmdCount
        ){
            ++groupEnd;
        }

        const u64 groupSubmissionID = m_lastSubmittedID + 1u + groupIndex;
        const QueueSubmissionToken groupToken{ .value = groupSubmissionID, .physicalQueueIndex = m_physicalQueue.index, .deviceGeneration = m_physicalQueue.deviceGeneration, .queue = m_queueID, };

        const b8 validateDescriptorHeaps = descriptorSubmission.hasReferencedHeaps();
        for(usize k = groupStart; k <= groupEnd; ++k){
            batchEntries[k].submissionID = groupSubmissionID;
            if(!validateDescriptorHeaps)
                continue;
            const BatchSubmitEntry& groupEntry = entries[k];
            for(usize i = 0u; i < groupEntry.numCmd; ++i){
                auto& tracked = *MakeNotNull(groupEntry.ppCmd[i]->m_currentCmdBuf.get());
                if(!descriptorSubmission.validateHeapUses(tracked, groupToken))
                    return BatchSubmissionResult{ BatchSubmissionResult::Status::ValidationFailed, m_lastSubmittedID, VK_SUCCESS };
            }
        }

        const usize signalOffset = signalInfos.size();
        auto timelineSignal = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        timelineSignal.semaphore = m_trackingSemaphore;
        timelineSignal.value = groupSubmissionID;
        timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalInfos.push_back(timelineSignal);

        const BatchSubmitEntry& lastGroupEntry = entries[groupEnd];
        for(usize i = 0u; i < lastGroupEntry.localSignalCount; ++i){
            VkSemaphoreSubmitInfo signalInfo = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
            signalInfo.semaphore = lastGroupEntry.localSignals[i].semaphore;
            signalInfo.value = lastGroupEntry.localSignals[i].value;
            signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            signalInfos.push_back(signalInfo);
        }
        const usize signalCount = signalInfos.size() - signalOffset;

        const BatchPreparedEntry& firstPrepared = batchEntries[groupStart];
        const BatchPreparedEntry& lastPrepared = batchEntries[groupEnd];
        const usize groupCmdCount = (lastPrepared.cmdOffset + lastPrepared.cmdCount) - firstPrepared.cmdOffset;

        auto submitInfo = VulkanDetail::MakeVkStruct<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
        submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(firstPrepared.waitCount);
        submitInfo.pWaitSemaphoreInfos = firstPrepared.waitCount > 0u ? waitInfos.data() + firstPrepared.waitOffset : nullptr;
        submitInfo.commandBufferInfoCount = static_cast<uint32_t>(groupCmdCount);
        submitInfo.pCommandBufferInfos = groupCmdCount > 0u ? cmdBufInfos.data() + firstPrepared.cmdOffset : nullptr;
        submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalCount);
        submitInfo.pSignalSemaphoreInfos = signalInfos.data() + signalOffset;
        submitInfos.push_back(submitInfo);

        groupStart = groupEnd + 1u;
        ++groupIndex;
    }

    const usize batchStartIndex = m_commandBuffersInFlight.size();
    if(totalCmdCount > 0u){
        m_commandBuffersInFlight.reserve(batchStartIndex + totalCmdCount);
        for(usize entryIndex = 0u; entryIndex < entries.size(); ++entryIndex){
            const BatchSubmitEntry& entry = entries[entryIndex];
            const u64 entrySubmissionID = batchEntries[entryIndex].submissionID;
            for(usize i = 0u; i < entry.numCmd; ++i){
                auto& commandList = *MakeNotNull(entry.ppCmd[i]);
                m_commandBuffersInFlight.pushBackReserved(Move(commandList.m_currentCmdBuf));
                m_commandBuffersInFlight.back()->m_submissionID = entrySubmissionID;
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
        for(const BatchSubmitEntry& entry : entries){
            for(usize i = 0u; i < entry.numCmd; ++i){
                CommandList* const commandList = entry.ppCmd[i];
                if(!commandList || commandList->m_recordingLeaseSerial != entry.expectedCommandLists[i].recordingLeaseSerial)
                    TerminateInvariant();
                commandList->m_stateTracker.rollbackRecordingAttempt();
            }
        }
        for(usize i = 0u; i < totalCmdCount; ++i){
            TrackedCommandBufferPtr rejected = Move(m_commandBuffersInFlight.back());
            m_commandBuffersInFlight.popBack();
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
    m_publishedSubmittedID.store(m_lastSubmittedID, MemoryOrder::release);
    if(totalWaitCount > 0u)
        Swap(m_acceptedTimelineWaits, m_submitBatchAcceptedTimelineWaits);

    descriptorSubmission.commitAndRelease();

    struct WorkerTransitionBatch{
        WorkerCommandArena* arena = nullptr;
        u64 count = 0u;
    };
    Array<WorkerTransitionBatch, 16u> workerBatches{};
    u32 workerBatchCount = 0u;
    u64 totalWorkerTransitionCount = 0u;
    usize cmdCursor = batchStartIndex;
    u64 directTransitionCount = 0u;
    for(usize entryIndex = 0u; entryIndex < entries.size(); ++entryIndex){
        const BatchSubmitEntry& entry = entries[entryIndex];
        const BatchPreparedEntry& prepared = batchEntries[entryIndex];
        const QueueSubmissionToken entryToken{ .value = prepared.submissionID, .physicalQueueIndex = m_physicalQueue.index, .deviceGeneration = m_physicalQueue.deviceGeneration, .queue = m_queueID, };
        for(usize i = 0u; i < entry.numCmd; ++i, ++cmdCursor){
            CommandList* const commandList = entry.ppCmd[i];
            if(!commandList || commandList->m_recordingLeaseSerial != entry.expectedCommandLists[i].recordingLeaseSerial)
                TerminateInvariant();
            commandList->m_stateTracker.commitRecordingAttemptFast();

            TrackedCommandBufferPtr& tracked = m_commandBuffersInFlight[cmdCursor];
            const u8 commitFlags = tracked->m_pendingCommitFlags;
            if((commitFlags & TrackedCommandBuffer::s_CommitTimerQueries) != 0u)
                tracked->commitTimerQueryRecordingClaims(entryToken);
            if((commitFlags & TrackedCommandBuffer::s_CommitBufferStates) != 0u)
                tracked->commitRetainedBufferStateCommits();
            if((commitFlags & TrackedCommandBuffer::s_CommitTextureStates) != 0u)
                tracked->commitRetainedTextureStateCommits();
            if((commitFlags & TrackedCommandBuffer::s_CommitAccelStructBuilds) != 0u)
                tracked->commitPendingAccelStructBuildCommits();
            if((commitFlags & TrackedCommandBuffer::s_CommitOpacityMicromapBuilds) != 0u)
                tracked->commitPendingOpacityMicromapBuildCommits();
            if(&tracked->m_queue != this || &tracked->m_context != &m_context)
                TerminateInvariant();
            if(tracked->m_recordingWorkerIndex == 0u){
                if(tracked->m_arenaState == TrackedCommandBufferArenaState::Leased){
                    tracked->m_arenaState = TrackedCommandBufferArenaState::Pending;
                    ++directTransitionCount;
                }
                else
                    commitCommandBufferStateTransition(*tracked, TrackedCommandBufferArenaState::Pending);
            }
            else if(tracked->m_arenaState == TrackedCommandBufferArenaState::Leased){
                WorkerCommandArena* const workerArena = tracked->m_workerCommandArena
                    ? static_cast<WorkerCommandArena*>(tracked->m_workerCommandArena)
                    : findWorkerCommandArena(tracked->m_recordingWorkerDomain, tracked->m_recordingWorkerIndex);
                if(!workerArena)
                    TerminateInvariant();
                u32 batchIdx = 0u;
                while(batchIdx < workerBatchCount && workerBatches[batchIdx].arena != workerArena)
                    ++batchIdx;
                if(batchIdx < workerBatchCount){
                    tracked->m_arenaState = TrackedCommandBufferArenaState::Pending;
                    ++workerBatches[batchIdx].count;
                    ++totalWorkerTransitionCount;
                }
                else if(workerBatchCount < workerBatches.size()){
                    tracked->m_arenaState = TrackedCommandBufferArenaState::Pending;
                    workerBatches[workerBatchCount++] = WorkerTransitionBatch{ workerArena, 1u };
                    ++totalWorkerTransitionCount;
                }
                else
                    commitCommandBufferStateTransition(*tracked, TrackedCommandBufferArenaState::Pending);
            }
            else
                commitCommandBufferStateTransition(*tracked, TrackedCommandBufferArenaState::Pending);
        }
        outReceipts[entryIndex] = SubmissionReceipt{ prepared.submissionID, prepared.timelineWaitCount, true };
    }
    const u64 totalTransitionCount = directTransitionCount + totalWorkerTransitionCount;
    if(totalTransitionCount != 0u){
        if(m_leasedCommandBufferCount.fetch_sub(totalTransitionCount, MemoryOrder::relaxed) < totalTransitionCount)
            TerminateInvariant();
        m_pendingCommandBufferCount.fetch_add(totalTransitionCount, MemoryOrder::relaxed);
    }
    if(directTransitionCount != 0u){
        if(m_directLeasedCommandBufferCount.fetch_sub(directTransitionCount, MemoryOrder::relaxed) < directTransitionCount)
            TerminateInvariant();
        m_pendingDirectCommandBufferCount.fetch_add(directTransitionCount, MemoryOrder::relaxed);
    }
    if(workerBatchCount != 0u){
        u64 newPendingWorkerEpochCount = 0u;
        for(u32 batchIdx = 0u; batchIdx < workerBatchCount; ++batchIdx){
            const WorkerTransitionBatch& batch = workerBatches[batchIdx];
            if(batch.arena->leasedCommandBufferCount.fetch_sub(batch.count, MemoryOrder::relaxed) < batch.count)
                TerminateInvariant();
            if(batch.arena->pendingCommandBufferCount.fetch_add(batch.count, MemoryOrder::relaxed) == 0u)
                ++newPendingWorkerEpochCount;
        }
        if(newPendingWorkerEpochCount != 0u)
            m_pendingWorkerEpochCount.fetch_add(newPendingWorkerEpochCount, MemoryOrder::relaxed);
    }
    return BatchSubmissionResult{ BatchSubmissionResult::Status::Accepted, m_lastSubmittedID, VK_SUCCESS };
}

VkResult Queue::updateLastFinishedID(){
    if(!m_trackingSemaphore){
        m_lastFinishedID = m_lastSubmittedID;
        m_lastObservedFinishedID.store(m_lastFinishedID, MemoryOrder::release);
        return VK_SUCCESS;
    }

    u64 completedValue = 0;
    const VkResult res = m_context.deviceDispatch.vkGetSemaphoreCounterValue(m_context.device, m_trackingSemaphore, &completedValue);
    if(res == VK_SUCCESS){
        m_lastFinishedID = Max(m_lastFinishedID, completedValue);
        m_lastObservedFinishedID.store(m_lastFinishedID, MemoryOrder::release);
    }
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
        m_lastObservedFinishedID.store(m_lastFinishedID, MemoryOrder::release);
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

    WorkerCommandArena* const workerArena = cmdBuf->m_workerCommandArena
        ? static_cast<WorkerCommandArena*>(cmdBuf->m_workerCommandArena)
        : findWorkerCommandArena(cmdBuf->m_recordingWorkerDomain, cmdBuf->m_recordingWorkerIndex);
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


b8 Device::isQueueSubmissionObservedComplete(const QueueSubmissionToken& token)const noexcept{
    if(!token.valid())
        return true;
    if(!token.hasPhysicalQueueIdentity() || token.deviceGeneration != m_deviceGeneration || token.physicalQueueIndex >= m_physicalQueues.size())
        return false;

    const Queue* const q = m_physicalQueues[token.physicalQueueIndex].get();
    if(!q || q->m_queueID != token.queue)
        return false;
    return q->lastObservedFinishedID() >= token.value;
}

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

