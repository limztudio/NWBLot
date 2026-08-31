// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>
#include <global/atomic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Device::beginSubmissionOperation()noexcept{
    ScopedLock lock(m_submissionOperationMutex);
    if(submissionsBlocked())
        return false;

    ++m_activeSubmissionOperationCount;
    return true;
}

void Device::endSubmissionOperation()noexcept{
    bool drained = false;
    {
        ScopedLock lock(m_submissionOperationMutex);
        NWB_ASSERT(m_activeSubmissionOperationCount > 0u);
        --m_activeSubmissionOperationCount;
        drained = m_activeSubmissionOperationCount == 0u;
    }
    if(drained)
        m_submissionOperationCondition.notify_all();
}

bool Device::beginLifecycleDrain()noexcept{
    // A public submission hook executes while its submit operation remains leased. Re-entering teardown from that
    // callback must fail before closing the gate, otherwise this thread would wait forever for its own lease.
    if(submissionOperationActiveOnCurrentThread())
        return false;

    UniqueLock<Futex> lock(m_submissionOperationMutex);
    if(m_submissionSuspended.load(MemoryOrder::acquire))
        return false;

    m_submissionSuspended.store(true, MemoryOrder::release);
    m_submissionOperationCondition.wait(lock, [this](){ return m_activeSubmissionOperationCount == 0u; });
    return true;
}

void Device::endLifecycleDrain()noexcept{
    ScopedLock lock(m_submissionOperationMutex);
    NWB_ASSERT(!m_lifecycleDestructionPrepared.load(MemoryOrder::acquire));
    NWB_ASSERT(m_activeSubmissionOperationCount == 0u);
    NWB_ASSERT(m_submissionSuspended.load(MemoryOrder::acquire));
    m_submissionSuspended.store(false, MemoryOrder::release);
}

bool Device::sealLifecycleDrainForDestruction()noexcept{
    ScopedLock lock(m_submissionOperationMutex);
    if(!m_submissionSuspended.load(MemoryOrder::acquire))
        return false;
    NWB_ASSERT(m_activeSubmissionOperationCount == 0u);
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
        0u,
        true
    );
    if(!submissionAccepted)
        return {};

    return QueueSubmissionToken{
        .queue = queue->m_queueID,
        .value = submittedID,
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

