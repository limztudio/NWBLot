// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandList::GraphPublicationReadOwnership::GraphPublicationReadOwnership(const CommandList& commandList)noexcept
    : m_commandList(commandList)
{
    AtomicBackOff backoff;
    for(;;){
        const u8 publicationState = m_commandList.m_graphPublicationState.load(MemoryOrder::acquire);
        if(publicationState == s_GraphPublicationUnowned){
            m_readable = true;
            return;
        }
        if(publicationState == s_GraphPublicationRecording){
            m_readable = GraphRecordingOwnership::hasCapability(
                m_commandList,
                m_commandList.m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire)
            );
            return;
        }
        if(publicationState == s_GraphPublicationReading){
            backoff.pause();
            continue;
        }
        if(publicationState != s_GraphPublicationRecorded)
            return;

        u8 expectedState = s_GraphPublicationRecorded;
        if(m_commandList.m_graphPublicationState.compare_exchange_strong(
            expectedState,
            s_GraphPublicationReading,
            MemoryOrder::acq_rel,
            MemoryOrder::acquire
        )){
            m_readable = true;
            m_acquired = true;
            return;
        }
    }
}
CommandList::GraphPublicationReadOwnership::~GraphPublicationReadOwnership()noexcept{
    if(!m_acquired)
        return;

    const bool ownershipMatches = m_commandList.m_graphPublicationState.load(MemoryOrder::acquire)
        == s_GraphPublicationReading
    ;
    NWB_FATAL_ASSERT_MSG(ownershipMatches, "command-list diagnostic read lost its exact publication capability");
    if(!ownershipMatches)
        TerminateInvariant();
    m_commandList.m_graphPublicationState.store(s_GraphPublicationRecorded, MemoryOrder::release);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandList::GraphRecordingOwnership::Capability*& CommandList::GraphRecordingOwnership::currentCapability()noexcept{
    thread_local Capability* capability = nullptr;
    return capability;
}

bool CommandList::GraphRecordingOwnership::hasCapability(
    const CommandList& commandList,
    const u64 recordingLeaseSerial
)noexcept{
    for(const Capability* capability = currentCapability(); capability; capability = capability->previous){
        if(
            capability->commandList == &commandList
            && capability->recordingLeaseSerial == recordingLeaseSerial
        )
            return true;
    }
    return false;
}

CommandList::GraphRecordingOwnership::GraphRecordingOwnership(
    CommandList& commandList,
    const u64 recordingLeaseSerial
)
    : m_commandList(commandList)
    , m_recordingLeaseSerial(recordingLeaseSerial)
    , m_acquired(commandList.beginGraphRecordingOwnership(recordingLeaseSerial))
{
    if(m_acquired)
        attachCapability();
}
CommandList::GraphRecordingOwnership::~GraphRecordingOwnership()noexcept{
    release();
}

bool CommandList::GraphRecordingOwnership::finish(
    const bool semanticSuccess,
    CommandListResourceStateHandoff* const finalStates
){
    if(!m_acquired || !semanticSuccess){
        if(finalStates)
            finalStates->reset();
        release();
        return false;
    }
    m_commandList.closeInternal(finalStates);
    if(
        m_commandList.commandRecordingFailedUnchecked()
        || m_commandList.isRecordingUnchecked()
        || !m_commandList.hasCommandBufferUnchecked()
    ){
        if(finalStates)
            finalStates->reset();
        release();
        return false;
    }
    return true;
}

void CommandList::GraphRecordingOwnership::publish()noexcept{
    NWB_FATAL_ASSERT_MSG(m_acquired, "task-graph command recording publication requires its exact capability");
    if(!m_acquired)
        TerminateInvariant();
    m_commandList.publishGraphRecordingOwnership(m_recordingLeaseSerial);
    detachCapability();
    m_acquired = false;
}

void CommandList::GraphRecordingOwnership::release()noexcept{
    if(!m_acquired)
        return;
    m_commandList.abortRecordingAttemptWithoutCallbacks();
    detachCapability();
    m_commandList.cancelGraphRecordingOwnership(m_recordingLeaseSerial);
    m_acquired = false;
}

void CommandList::GraphRecordingOwnership::attachCapability()noexcept{
    NWB_FATAL_ASSERT_MSG(!m_capabilityAttached, "task-graph recording capability cannot be attached twice");
    if(m_capabilityAttached)
        TerminateInvariant();

    Capability*& capability = currentCapability();
    m_capability.commandList = &m_commandList;
    m_capability.recordingLeaseSerial = m_recordingLeaseSerial;
    m_capability.previous = capability;
    capability = &m_capability;
    m_capabilityAttached = true;
}

void CommandList::GraphRecordingOwnership::detachCapability()noexcept{
    if(!m_capabilityAttached)
        return;

    Capability** capability = &currentCapability();
    while(*capability && *capability != &m_capability)
        capability = &(*capability)->previous;
    const bool capabilityFound = *capability == &m_capability;
    NWB_FATAL_ASSERT_MSG(capabilityFound, "task-graph recording capability left its owning thread-local stack");
    if(!capabilityFound)
        TerminateInvariant();
    *capability = m_capability.previous;

    m_capability = {};
    m_capabilityAttached = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandList::GraphSubmissionOwnership::GraphSubmissionOwnership(CommandList& commandList)noexcept
    : m_commandList(commandList)
    , m_acquired(commandList.beginGraphSubmissionOwnership(m_recordingLeaseSerial))
{}
CommandList::GraphSubmissionOwnership::~GraphSubmissionOwnership()noexcept{
    release();
}

void CommandList::GraphSubmissionOwnership::accept()noexcept{
    if(!m_acquired)
        return;
    m_commandList.acceptGraphSubmissionOwnership(m_recordingLeaseSerial);
    m_acquired = false;
}

void CommandList::GraphSubmissionOwnership::release()noexcept{
    if(!m_acquired)
        return;
    m_commandList.endGraphSubmissionOwnership(m_recordingLeaseSerial);
    m_acquired = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

