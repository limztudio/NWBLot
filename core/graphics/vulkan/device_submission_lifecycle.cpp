// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>
#include <global/atomic.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Device::SubmissionOperationLease::~SubmissionOperationLease()noexcept{
    if(!m_device)
        return;
    NWB_FATAL_ASSERT(activeLease() == this);
    if(activeLease() != this)
        TerminateInvariant();
    activeLease() = m_previousActiveLease;
    m_device->endSubmissionOperation();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Device::beginSubmissionOperation()noexcept{
    u64 state = m_submissionOperationState.load(MemoryOrder::acquire);
    for(;;){
        if(
            (state & s_SubmissionDrainBit) != 0u
            || requiresRecreation()
            || (state & s_SubmissionOperationCountMask) == s_SubmissionOperationCountMask
        )
            return false;
        if(m_submissionOperationState.compare_exchange_weak(
            state,
            state + 1u,
            MemoryOrder::acquire,
            MemoryOrder::relaxed
        ))
            return true;
    }
}

void Device::endSubmissionOperation()noexcept{
    u64 state = m_submissionOperationState.load(MemoryOrder::relaxed);
    for(;;){
        if((state & s_SubmissionOperationCountMask) == 0u)
            TerminateInvariant();
        if(m_submissionOperationState.compare_exchange_weak(
            state,
            state - 1u,
            MemoryOrder::release,
            MemoryOrder::relaxed
        )){
            if((state & s_SubmissionOperationCountMask) == 1u)
                m_submissionOperationState.notify_all();
            return;
        }
    }
}

bool Device::beginLifecycleDrain()noexcept{
    // Submission hooks run while leased; re-entrant teardown must fail before closing the gate.
    if(submissionOperationActiveOnCurrentThread())
        return false;

    u64 state = m_submissionOperationState.load(MemoryOrder::acquire);
    for(;;){
        if((state & s_SubmissionDrainBit) != 0u)
            return false;
        if(m_submissionOperationState.compare_exchange_weak(
            state,
            state | s_SubmissionDrainBit,
            MemoryOrder::acq_rel,
            MemoryOrder::acquire
        ))
            break;
    }

    state |= s_SubmissionDrainBit;
    while((state & s_SubmissionOperationCountMask) != 0u){
        m_submissionOperationState.wait(state, MemoryOrder::acquire);
        state = m_submissionOperationState.load(MemoryOrder::acquire);
        NWB_FATAL_ASSERT((state & s_SubmissionDrainBit) != 0u);
        if((state & s_SubmissionDrainBit) == 0u)
            TerminateInvariant();
    }
    return true;
}

void Device::endLifecycleDrain()noexcept{
    const bool lifecycleDestructionPrepared = m_lifecycleDestructionPrepared.load(MemoryOrder::acquire);
    const u64 submissionOperationState = m_submissionOperationState.load(MemoryOrder::acquire);
    NWB_FATAL_ASSERT(!lifecycleDestructionPrepared);
    NWB_FATAL_ASSERT(submissionOperationState == s_SubmissionDrainBit);
    if(lifecycleDestructionPrepared || submissionOperationState != s_SubmissionDrainBit)
        TerminateInvariant();
    m_submissionOperationState.store(0u, MemoryOrder::release);
}

bool Device::sealLifecycleDrainForDestruction()noexcept{
    if(m_submissionOperationState.load(MemoryOrder::acquire) != s_SubmissionDrainBit)
        return false;
    m_lifecycleDestructionPrepared.store(true, MemoryOrder::release);
    return true;
}

QueueSubmissionToken Device::consumeAcquiredImageSemaphore(const VkSemaphore semaphore){
    SubmissionOperationLease submissionOperation(*this);
    if(!submissionOperation.valid() || semaphore == VK_NULL_HANDLE)
        return {};

    const GpuPhysicalQueueId executionQueue = getPrimaryPhysicalQueue(CommandQueue::Graphics);
    Queue* const queue = getQueue(executionQueue);
    if(!queue)
        return {};

    const Queue::SubmissionWait acquireWait{ semaphore, 0u };
    bool submissionAccepted = false;
    const u64 submittedID = queue->submit(
        nullptr,
        0u,
        nullptr,
        &acquireWait,
        1u,
        &submissionAccepted,
        nullptr,
        nullptr,
        0u,
        true
    );
    if(!submissionAccepted)
        return {};

    return QueueSubmissionToken{
        .value = submittedID,
        .queue = queue->m_queueID,
        .physicalQueueIndex = executionQueue.index,
        .deviceGeneration = executionQueue.deviceGeneration,
    };
}

bool Device::presentNativeQueue(
    const u32 nativeQueueIndex,
    const VkPresentInfoKHR& presentInfo,
    VkResult& outResult
){
    SubmissionOperationLease submissionOperation(*this);
    outResult = VK_ERROR_UNKNOWN;
    if(!submissionOperation.valid())
        return false;
    if(
        nativeQueueIndex >= m_nativeQueueStates.size()
        || !m_nativeQueueStates[nativeQueueIndex]
        || m_nativeQueueStates[nativeQueueIndex]->queue == VK_NULL_HANDLE
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Presentation references an invalid canonical native queue state."));
        return false;
    }

    NativeQueueState& nativeQueue = *m_nativeQueueStates[nativeQueueIndex];
    ScopedLock hostLock(nativeQueue.hostMutex);
    if(submissionsBlocked())
        return false;
    outResult = m_context.deviceDispatch.vkQueuePresentKHR(nativeQueue.queue, &presentInfo);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

