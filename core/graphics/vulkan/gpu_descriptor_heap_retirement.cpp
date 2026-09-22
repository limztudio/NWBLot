// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuDescriptorHeap::isResourceAdmittedToActiveUsesLocked(const ResourceQueueAdmissionSnapshot& admission)const noexcept{
    for(const HeapUse& heapUse : m_heapUses){
        if(!heapUse.commandBuffer)
            continue;

        const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(heapUse.physicalQueue);
        if(!queueInfo || !ResourceQueueAdmissionAdmitsQueue(admission, *queueInfo))
            return false;
    }
    return true;
}

bool GpuDescriptorHeap::retainedResourcesReadyForQueueLocked(const GpuPhysicalQueueInfo& queue)const noexcept{
    for(const BufferHandle& retainedBuffer : m_resourceDescriptorBuffers){
        if(
            retainedBuffer
            && (
                !m_device.isBufferReadyForGpuUse(retainedBuffer.get())
                || !ResourceQueueAdmissionAdmitsQueue(retainedBuffer->getQueueAdmissionSnapshot(), queue)
            )
        )
            return false;
    }
    for(const TextureHandle& retainedTexture : m_resourceDescriptorTextures){
        if(
            retainedTexture
            && (
                !m_device.isTextureReadyForGpuUse(retainedTexture.get())
                || !ResourceQueueAdmissionAdmitsQueue(retainedTexture->getQueueAdmissionSnapshot(), queue)
            )
        )
            return false;
    }
    for(const RayTracingAccelStructHandle& retainedAccelStruct : m_accelStructResources){
        if(!retainedAccelStruct)
            continue;

        Buffer* const backingBuffer = retainedAccelStruct->getBackingBuffer();
        if(
            !m_device.isAccelStructReadyForGpuUse(retainedAccelStruct.get())
            || !backingBuffer
            || !ResourceQueueAdmissionAdmitsQueue(backingBuffer->getQueueAdmissionSnapshot(), queue)
        )
            return false;
    }
    return true;
}

bool GpuDescriptorHeap::retainedResourcesReadyForQueue(const GpuPhysicalQueueId& queue)const noexcept{
    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(queue);
    if(!queueInfo)
        return false;

    NothrowScopedLock lock(m_mutex);
    return m_initialized && retainedResourcesReadyForQueueLocked(*queueInfo);
}

bool GpuDescriptorHeap::trackCommandBufferUseLocked(
    TrackedCommandBuffer& commandBuffer,
    const GpuPhysicalQueueId& physicalQueue
){
    if(!m_initialized || !m_device.getPhysicalQueueInfo(physicalQueue))
        return false;

    for(GpuDescriptorHeap* trackedHeap : commandBuffer.m_referencedDescriptorHeaps){
        if(trackedHeap != this)
            continue;

        for(const HeapUse& heapUse : m_heapUses){
            if(heapUse.commandBuffer == &commandBuffer)
                return heapUse.physicalQueue == physicalQueue;
        }
        return false;
    }
    if(m_lastHeapUseID == UINT64_MAX)
        return false;

    commandBuffer.m_referencedDescriptorHeaps.push_back(this);
    m_heapUses.push_back(HeapUse{
        .commandBuffer = &commandBuffer,
        .submissionToken = {},
        .id = ++m_lastHeapUseID,
        .physicalQueue = physicalQueue,
    });
    return true;
}

bool GpuDescriptorHeap::validateCommandBufferUseSubmissionLocked(
    TrackedCommandBuffer& commandBuffer,
    const QueueSubmissionToken& submissionToken,
    usize& outHeapUseIndex
){
    outHeapUseIndex = Limit<usize>::s_Max;
    if(
        !submissionToken.valid()
        || !submissionToken.hasPhysicalQueueIdentity()
        || !m_device.matchesPhysicalQueueIdentity(
            GpuPhysicalQueueId{ .index = submissionToken.physicalQueueIndex, .deviceGeneration = submissionToken.deviceGeneration }
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap received an invalid physical command-buffer submission token."));
        return false;
    }

    for(usize heapUseIndex = 0u; heapUseIndex < m_heapUses.size(); ++heapUseIndex){
        HeapUse& heapUse = m_heapUses[heapUseIndex];
        if(heapUse.commandBuffer != &commandBuffer || heapUse.submissionToken.valid())
            continue;
        if(!submissionToken.matchesPhysicalQueue(
            heapUse.physicalQueue.index,
            heapUse.physicalQueue.deviceGeneration
        )){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: GpuDescriptorHeap command-buffer submission changed its exact physical queue.")
            );
            return false;
        }

        outHeapUseIndex = heapUseIndex;
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: GpuDescriptorHeap could not resolve command-buffer heap use for accepted submission {}."), submissionToken.value);
    return false;
}

void GpuDescriptorHeap::commitCommandBufferUseSubmissionLocked(
    TrackedCommandBuffer& commandBuffer,
    const QueueSubmissionToken& submissionToken,
    const usize heapUseIndex
)noexcept{
    static_assert(IsTriviallyCopyable_V<QueueSubmissionToken>, "accepted descriptor-use publication must remain scalar-only");
    if(heapUseIndex >= m_heapUses.size())
        TerminateInvariant();

    HeapUse& heapUse = m_heapUses[heapUseIndex];
    if(
        heapUse.commandBuffer != &commandBuffer
        || heapUse.submissionToken.valid()
        || !submissionToken.matchesPhysicalQueue(
            heapUse.physicalQueue.index,
            heapUse.physicalQueue.deviceGeneration
        )
    )
        TerminateInvariant();

    heapUse.submissionToken = submissionToken;
}

void GpuDescriptorHeap::discardCommandBufferUse(TrackedCommandBuffer& commandBuffer)noexcept{
    NothrowScopedLock lock(m_mutex);
    for(HeapUse& heapUse : m_heapUses){
        if(heapUse.commandBuffer == &commandBuffer)
            heapUse.commandBuffer = nullptr;
    }
}

void GpuDescriptorHeap::releasePendingRecordingLease(const u64 descriptorBufferGeneration)noexcept{
    NothrowScopedLock lock(m_mutex);
    if(
        !m_initialized
        || descriptorBufferGeneration == 0u
        || descriptorBufferGeneration != m_descriptorBufferGeneration
        || m_activePendingRecordingLeaseCount == 0u
    )
        TerminateInvariant();

    if(m_activePendingRecordingLeaseCount == 1u){
        if(
            m_pendingRecordingCount > m_pendingRecording.size()
            || m_retiredCount > m_retired.size()
            || m_pendingRecordingCount > m_retired.size() - m_retiredCount
        )
            TerminateInvariant();

        for(usize pendingIndex = 0u; pendingIndex < m_pendingRecordingCount; ++pendingIndex){
            const GpuDescriptorHandle handle = m_pendingRecording[pendingIndex];
            if(!handle.valid() || handle.descriptorClass() >= GpuDescriptorClass::kCount)
                TerminateInvariant();

            SlotAllocator& allocator = allocatorForClass(handle.descriptorClass());
            const u32 slot = handle.slot();
            if(
                slot >= allocator.slotStates.size()
                || slot >= allocator.allocatedClasses.size()
                || allocator.slotStates[slot] != SlotState::PendingRecording
                || allocator.allocatedClasses[slot] != static_cast<u8>(handle.descriptorClass())
            )
                TerminateInvariant();
        }
    }

    --m_activePendingRecordingLeaseCount;
    if(m_activePendingRecordingLeaseCount != 0u)
        return;

    for(usize pendingIndex = 0u; pendingIndex < m_pendingRecordingCount; ++pendingIndex){
        const GpuDescriptorHandle handle = m_pendingRecording[pendingIndex];
        SlotAllocator& allocator = allocatorForClass(handle.descriptorClass());
        allocator.slotStates[handle.slot()] = SlotState::Retired;
        m_retired[m_retiredCount] = RetiredSlot{ m_lastHeapUseID, handle };
        ++m_retiredCount;
    }
    m_pendingRecordingCount = 0u;
}

void GpuDescriptorHeap::collectRetired(){
    struct QueueCompletion{
        GpuPhysicalQueueId queue;
        u64 value = 0u;
    };
    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_DescriptorBindingArena);
    Vector<QueueCompletion, Alloc::ScratchArena> completions(scratchArena);

    {
        ScopedLock lock(m_mutex);
        completions.reserve(m_heapUses.size());
        for(const HeapUse& heapUse : m_heapUses){
            const QueueSubmissionToken& token = heapUse.submissionToken;
            if(!token.valid() || !token.hasPhysicalQueueIdentity())
                continue;

            const GpuPhysicalQueueId queue{ .index = token.physicalQueueIndex, .deviceGeneration = token.deviceGeneration };
            bool alreadyTracked = false;
            for(const QueueCompletion& completion : completions){
                if(completion.queue == queue){
                    alreadyTracked = true;
                    break;
                }
            }
            if(!alreadyTracked)
                completions.push_back(QueueCompletion{
                    .queue = queue,
                    .value = 0u,
                });
        }
    }

    for(QueueCompletion& completion : completions)
        completion.value = m_device.queueGetCompletedInstance(completion.queue);

    ScopedLock lock(m_mutex);
    const auto heapUseComplete = [&](const HeapUse& heapUse) -> bool {
        const QueueSubmissionToken& token = heapUse.submissionToken;
        if(!token.valid())
            return heapUse.commandBuffer == nullptr;

        if(!token.hasPhysicalQueueIdentity())
            return false;

        const GpuPhysicalQueueId queue{ .index = token.physicalQueueIndex, .deviceGeneration = token.deviceGeneration };
        for(const QueueCompletion& completion : completions){
            if(completion.queue == queue)
                return completion.value >= token.value;
        }
        return false;
    };

    usize keptRetired = 0u;
    for(usize retiredIndex = 0u; retiredIndex < m_retiredCount; ++retiredIndex){
        const RetiredSlot& retired = m_retired[retiredIndex];
        bool canRetire = true;
        for(const HeapUse& heapUse : m_heapUses){
            if(heapUse.id > retired.lastRequiredHeapUseID)
                continue;
            if(!heapUseComplete(heapUse)){
                canRetire = false;
                break;
            }
        }

        if(canRetire){
            SlotAllocator& allocator = allocatorForClass(retired.handle.descriptorClass());
            const u32 slot = retired.handle.slot();
            if(
                slot < allocator.slotStates.size()
                && slot < allocator.allocatedClasses.size()
                && allocator.slotStates[slot] == SlotState::Retired
                && allocator.allocatedClasses[slot] == static_cast<u8>(retired.handle.descriptorClass())
                && allocator.freeCount < allocator.freeList.size()
            ){
                releaseRetainedDescriptorResource(retired.handle);
                allocator.slotStates[slot] = SlotState::Free;
                allocator.allocatedClasses[slot] = static_cast<u8>(GpuDescriptorClass::kCount);
                allocator.freeList[allocator.freeCount] = retired.handle.slot();
                ++allocator.freeCount;
            }
            else{
                TerminateInvariant();
            }
        }
        else{
            m_retired[keptRetired] = retired;
            ++keptRetired;
        }
    }
    m_retiredCount = keptRetired;

    usize keptHeapUses = 0u;
    for(usize heapUseIndex = 0u; heapUseIndex < m_heapUses.size(); ++heapUseIndex){
        const HeapUse& heapUse = m_heapUses[heapUseIndex];
        if(heapUseComplete(heapUse))
            continue;

        m_heapUses[keptHeapUses] = heapUse;
        ++keptHeapUses;
    }
    m_heapUses.resize(keptHeapUses);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

