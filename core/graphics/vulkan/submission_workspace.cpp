// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "submission_workspace.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Device::SubmissionWorkspace::prepareBatch(const Span<const Device::GraphCommandListBatchEntry> entries, const b8 graphSubmissionAuthorized){
    auto& expectedCommandLists = m_queue.m_executeExpectedCommandLists;
    auto& submittedOwners = m_queue.m_executeSubmittedOwners;
    expectedCommandLists.clear();

    usize totalCommandLists = 0u;
    for(const Device::GraphCommandListBatchEntry& entry : entries){
        if(entry.commandLists)
            totalCommandLists += entry.numCommandLists;
    }

    if(totalCommandLists > 0u){
        expectedCommandLists.reserve(totalCommandLists);
        for(const Device::GraphCommandListBatchEntry& entry : entries){
            if(!entry.commandLists)
                continue;
            for(usize i = 0u; i < entry.numCommandLists; ++i){
                auto& commandList = *MakeNotNull(entry.commandLists[i]);
                TrackedCommandBuffer& owner = *commandList.m_currentCmdBuf;
                expectedCommandLists.push_back(Queue::SubmissionCommandListIdentity{
                    .owner = &owner,
                    .recordingLeaseSerial = commandList.m_recordingLeaseSerial,
                    .nativeRecordingID = commandList.m_nativeRecordingID,
                    .recordingWorkerDomain = commandList.m_creationDesc.recordingWorkerDomain,
                    .graphRecordingOwnershipSerial = graphSubmissionAuthorized ? commandList.m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire) : 0u,
                    .recordingWorkerIndex = commandList.m_creationDesc.recordingWorkerIndex,
                    .graphSubmissionAuthorized = graphSubmissionAuthorized,
                });
            }
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

void Device::SubmissionWorkspace::finalizeBatch(const Span<const Device::GraphCommandListBatchEntry> entries, const GpuPhysicalQueueId& executionQueue, const u64 submittedID, const b8 submissionAccepted){
    const auto& expectedCommandLists = m_queue.m_executeExpectedCommandLists;
    if(expectedCommandLists.empty())
        return;

    if(submissionAccepted){
        m_device.m_uploadManager.submitChunks(executionQueue, submittedID, expectedCommandLists, m_queue.m_executeSubmittedOwners);
        m_device.m_scratchManager.submitChunks(executionQueue, submittedID, expectedCommandLists, m_queue.m_executeSubmittedOwners);
        m_device.m_accelStructInputManager.submitChunks(executionQueue, submittedID, expectedCommandLists, m_queue.m_executeSubmittedOwners);
        return;
    }

    const auto ownerStillRecorded = [&](const Queue::SubmissionCommandListIdentity& expected)noexcept -> b8 {
        if(!expected.owner || expected.nativeRecordingID == 0u)
            return false;
        for(const Device::GraphCommandListBatchEntry& entry : entries){
            if(!entry.commandLists)
                continue;
            for(usize i = 0u; i < entry.numCommandLists; ++i){
                CommandList* const commandList = entry.commandLists[i];
                if(commandList && commandList->m_currentCmdBuf.get() == expected.owner && commandList->m_nativeRecordingID == expected.nativeRecordingID)
                    return true;
            }
        }
        return false;
    };

    const Expected<u64> reusableVersion = m_device.queueGetCompletedInstance(executionQueue);
    for(const Queue::SubmissionCommandListIdentity& expected : expectedCommandLists){
        if(ownerStillRecorded(expected))
            continue;
        if(!reusableVersion){
            m_device.m_uploadManager.abandonChunks(executionQueue, expected.owner, expected.nativeRecordingID);
            m_device.m_scratchManager.abandonChunks(executionQueue, expected.owner, expected.nativeRecordingID);
            m_device.m_accelStructInputManager.abandonChunks(executionQueue, expected.owner, expected.nativeRecordingID);
            continue;
        }
        m_device.m_uploadManager.discardChunks(executionQueue, expected.owner, expected.nativeRecordingID, *reusableVersion);
        m_device.m_scratchManager.discardChunks(executionQueue, expected.owner, expected.nativeRecordingID, *reusableVersion);
        m_device.m_accelStructInputManager.discardChunks(executionQueue, expected.owner, expected.nativeRecordingID, *reusableVersion);
    }
}

void Device::SubmissionWorkspace::prepare(CommandList* const* commandLists, const usize numCommandLists, const b8 graphSubmissionAuthorized){
    const Device::GraphCommandListBatchEntry entry{ commandLists, numCommandLists, {} };
    prepareBatch(Span<const Device::GraphCommandListBatchEntry>(&entry, 1u), graphSubmissionAuthorized);
}

void Device::SubmissionWorkspace::finalize(CommandList* const* commandLists, const usize numCommandLists, const GpuPhysicalQueueId& executionQueue, const u64 submittedID, const b8 submissionAccepted){
    const Device::GraphCommandListBatchEntry entry{ commandLists, numCommandLists, {} };
    finalizeBatch(Span<const Device::GraphCommandListBatchEntry>(&entry, 1u), executionQueue, submittedID, submissionAccepted);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

