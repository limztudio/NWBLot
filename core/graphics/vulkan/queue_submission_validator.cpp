// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "queue_submission_validator.h"

#include <core/common/log.h>


NWB_VULKAN_BEGIN


b8 Queue::SubmissionValidator::validateCommandList(CommandList* const cmdList, const SubmissionCommandListIdentity& expected, const usize cmdIndex, const u64 batchStamp)const{
    if(!cmdList || &cmdList->m_device != &m_queue.m_device){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: command list {} is null or foreign"), cmdIndex);
        return false;
    }
    if(
        !m_queue.m_physicalQueue.valid()
        || cmdList->m_creationDesc.physicalQueue != m_queue.m_physicalQueue
        || cmdList->m_creationDesc.queueType != m_queue.m_queueID
        || !cmdList->descriptionMatchesCreation()
        || !cmdList->m_cachedQueueContractValid
        || cmdList->m_cachedQueue != &m_queue
    ){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Command-list lease provenance does not match execution queue"));
        return false;
    }
    if(cmdList->m_lastSubmitBatchStamp == batchStamp){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: command list {} is duplicated"), cmdIndex);
        return false;
    }
    cmdList->m_lastSubmitBatchStamp = batchStamp;

    const u8 requiredPublicationState = expected.graphSubmissionAuthorized ? CommandList::s_GraphPublicationSubmitting : CommandList::s_GraphPublicationUnowned;
    const u8 publicationState = cmdList->m_graphPublicationState.load(MemoryOrder::acquire);
    const u64 graphOwnershipSerial = cmdList->m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire);
    if(
        publicationState != requiredPublicationState
        || (expected.graphSubmissionAuthorized == (graphOwnershipSerial == 0u))
    ){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Command-list lease provenance does not match execution queue"));
        return false;
    }
    if(cmdList->m_commandRecordingFailed){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: a command list has a sticky native recording failure"));
        return false;
    }
    if(cmdList->m_isRecording){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: command list {} is still recording"), cmdIndex);
        return false;
    }

    const TrackedCommandBuffer* const tracked = cmdList->m_currentCmdBuf.get();
    if(!tracked || tracked->m_cmdBuf == VK_NULL_HANDLE){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: command list {} has no native command buffer"), cmdIndex);
        return false;
    }
    if(
        tracked->m_arenaState != TrackedCommandBufferArenaState::Leased
        || &tracked->m_queue != &m_queue
        || &tracked->m_context != &m_queue.m_context
    ){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Native submission requires a leased command buffer"));
        return false;
    }
    if(
        !expected.owner
        || expected.owner != tracked
        || expected.recordingLeaseSerial == 0u
        || expected.recordingLeaseSerial != cmdList->m_recordingLeaseSerial
        || expected.nativeRecordingID == 0u
        || expected.nativeRecordingID != cmdList->m_nativeRecordingID
        || expected.nativeRecordingID != tracked->m_recordingID
        || expected.recordingWorkerDomain != cmdList->m_creationDesc.recordingWorkerDomain
        || expected.recordingWorkerDomain != tracked->m_recordingWorkerDomain
        || expected.graphRecordingOwnershipSerial != graphOwnershipSerial
        || expected.recordingWorkerIndex != cmdList->m_creationDesc.recordingWorkerIndex
        || expected.recordingWorkerIndex != tracked->m_recordingWorkerIndex
    ){
        NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: command list {} replaced its validated native recording lease"), cmdIndex);
        return false;
    }
    return cmdList->validateTrackedResourcesReadyForSubmissionFast();
}

b8 Queue::SubmissionValidator::validate(CommandList* const* ppCmd, const usize numCmd, const SubmissionCommandListIdentity* const expectedCommandLists, const u64 batchStamp)const{
    for(usize i = 0u; i < numCmd; ++i){
        if(!validateCommandList(ppCmd[i], expectedCommandLists[i], i, batchStamp))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

