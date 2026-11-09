// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "queue_submission_validator.h"

#include <core/common/log.h>


NWB_VULKAN_BEGIN


b8 Queue::SubmissionValidator::validate(CommandList* const* ppCmd, const usize numCmd, const SubmissionCommandListIdentity* const expectedCommandLists)const{
    for(usize i = 0u; i < numCmd; ++i){
        auto* cmdList = ppCmd[i];
        const SubmissionCommandListIdentity& expected = expectedCommandLists[i];
        for(usize previous = 0u; previous < i; ++previous){
            if(ppCmd[previous] == cmdList){
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} is duplicated"), i);
                return false;
            }
        }
        if(!cmdList || &cmdList->m_device != &m_queue.m_device){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} is null or foreign"), i);
            return false;
        }
        if(!cmdList->matchesSubmissionLease(m_queue.m_physicalQueue, m_queue.m_queueID, expected.graphSubmissionAuthorized)){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Command-list lease provenance does not match execution queue"));
            return false;
        }
        if(!cmdList->m_currentCmdBuf || cmdList->m_currentCmdBuf->m_cmdBuf == VK_NULL_HANDLE){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} has no native command buffer"), i);
            return false;
        }
        if(cmdList->m_commandRecordingFailed){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: a command list has a sticky native recording failure"));
            return false;
        }
        if(cmdList->m_isRecording){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to submit command lists: command list {} is still recording"), i);
            return false;
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
            return false;
        }
        if(!cmdList->validateTrackedResourcesReadyForSubmission())
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

