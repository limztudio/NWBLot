// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "queue_descriptor_submission.h"

#include <core/common/log.h>


NWB_VULKAN_BEGIN


Queue::DescriptorSubmission::DescriptorSubmission(Queue& queue)noexcept
    : m_queue(queue)
    , m_descriptorBufferManager(queue.m_context.descriptorBufferManager)
{}


b8 Queue::DescriptorSubmission::prepare(CommandList* const* ppCmd, const usize numCmd){
    const b8 hasCommands = numCmd > 0u;
    b8 requiresDescriptorBufferLifecycle = false;
    if(hasCommands){
        for(usize i = 0u; i < numCmd; ++i){
            auto& tracked = *MakeNotNull(ppCmd[i]->m_currentCmdBuf.get());
            requiresDescriptorBufferLifecycle |= tracked.m_descriptorBufferManager != nullptr;
            for(const auto heap : tracked.m_referencedDescriptorHeaps){

                if(heap.get() != &m_queue.m_device.m_gpuDescriptorHeap){
                    NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Command buffer references a foreign descriptor heap."));
                    return false;
                }
                m_submissionDescriptorHeap = heap.get();
            }
        }
    }

    // Global hierarchy: queue -> descriptor heap -> descriptor-buffer lifecycle.
    if(m_submissionDescriptorHeap)
        m_descriptorHeapLock = UniqueLock<Futex>(m_submissionDescriptorHeap->m_mutex);
    if(requiresDescriptorBufferLifecycle && m_descriptorBufferManager)
        m_descriptorBufferLifecycleLock = UniqueLock<Futex>(m_descriptorBufferManager->m_lifecycleMutex);

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
                        tracked.m_descriptorBufferManager != m_descriptorBufferManager
                        || m_descriptorBufferManager != &m_queue.m_device.m_descriptorBufferManager
                        || !m_descriptorBufferManager->m_enabled
                        || m_descriptorBufferManager->m_lifecycleTransitioning
                        || m_descriptorBufferManager->m_bindingGeneration == 0u
                        || tracked.m_descriptorBufferGeneration != m_descriptorBufferManager->m_bindingGeneration
                    )
                )
            ){
                NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Failed to submit command lists: descriptor-buffer binding generation is stale"));
                return false;
            }
        }
    }
    return true;
}


b8 Queue::DescriptorSubmission::validateHeapUses(TrackedCommandBuffer& tracked, const QueueSubmissionToken& submissionToken){
    auto& descriptorHeapUseCommitTickets = m_queue.m_submitDescriptorHeapUseCommitTickets;
    for(const auto heap : tracked.m_referencedDescriptorHeaps){
        const auto heapUseIndex = heap->validateCommandBufferUseSubmissionLocked(tracked, submissionToken);
        if(!heapUseIndex)
            return false;
        for(const DescriptorHeapUseCommitTicket& ticket : descriptorHeapUseCommitTickets){
            if(ticket.heap == heap && ticket.heapUseIndex == *heapUseIndex){
                NWB_LOGGER_CRITICAL_WARNING(FOUNDATION_TEXT("Vulkan: Native submission contains a duplicate descriptor-heap use."));
                return false;
            }
        }
        descriptorHeapUseCommitTickets.push_back(DescriptorHeapUseCommitTicket{ .heap = heap, .commandBuffer = MakeNotNull(&tracked), .heapUseIndex = *heapUseIndex, });
    }
    return true;
}


void Queue::DescriptorSubmission::releaseLocks()noexcept{
    if(m_descriptorBufferLifecycleLock.owns_lock()){
        auto& lifecycleMutex = *MakeNotNull(m_descriptorBufferLifecycleLock.release());
        lifecycleMutex.unlock();
    }
    if(m_descriptorHeapLock.owns_lock()){
        auto& heapMutex = *MakeNotNull(m_descriptorHeapLock.release());
        heapMutex.unlock();
    }
}

void Queue::DescriptorSubmission::commitAndRelease(const QueueSubmissionToken& submissionToken)noexcept{
    for(const DescriptorHeapUseCommitTicket& ticket : m_queue.m_submitDescriptorHeapUseCommitTickets)
        ticket.heap->commitCommandBufferUseSubmissionLocked(*ticket.commandBuffer, submissionToken, ticket.heapUseIndex);
    releaseLocks();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
