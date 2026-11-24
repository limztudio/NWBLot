// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_resource_context.h"
#include "backend_core_context.h"
#include "backend_command_tracking.h"
#include "backend_queue.h"
#include "vulkan_allocator.h"
#include "upload_manager.h"
#include "backend_memory_upload.h"
#include "backend_heap.h"
#include "backend_sampler.h"
#include "backend_shader.h"
#include "backend_input_layout.h"
#include "backend_framebuffer.h"
#include "backend_descriptor_heap.h"
#include "backend_pipeline_state.h"
#include "backend_raytracing_pipeline.h"
#include "backend_resource_management.h"
#include "backend_query.h"
#include "backend_command_list.h"
#include "backend_device.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Expected<BufferSuballocation> UploadManager::TrySuballocateFromChunk(BufferChunk& chunk, const u64 size, const u32 alignment)noexcept{
    u64 alignedOffset = chunk.allocated;
    if(alignment > 1u){
        const u64 align64 = static_cast<u64>(alignment);
        if((align64 & (align64 - 1u)) == 0u){
            const u64 mask = align64 - 1u;
            if(alignedOffset > Limit<u64>::s_Max - mask)
                return MakeUnexpected(Failure{});
            alignedOffset = (alignedOffset + mask) & ~mask;
        }
        else{
            const auto checked = AlignUpChecked<u64>(alignedOffset, align64);
            if(!checked)
                return MakeUnexpected(Failure{});
            alignedOffset = *checked;
        }
    }
    if(alignedOffset > chunk.size || size > chunk.size - alignedOffset)
        return MakeUnexpected(Failure{});

    Buffer& buffer = *chunk.buffer;
    void* const cpuAddress = buffer.m_mappedMemory ? static_cast<u8*>(buffer.m_mappedMemory) + alignedOffset : nullptr;

    chunk.allocated = alignedOffset + size;
    return BufferSuballocation{ MakeNotNull(&buffer), alignedOffset, cpuAddress };
}

inline Expected<BufferSuballocation> UploadManager::suballocateBuffer(const u64 size, TrackedCommandBuffer* const owner, const u64 nativeRecordingID, const GpuPhysicalQueueId queue, const u32 alignment){
    if(!owner || nativeRecordingID == 0u)
        return MakeUnexpected(Failure{});
    const usize chunkSlot = static_cast<usize>(m_chunkKind);
    const u8 slotBit = static_cast<u8>(1u << chunkSlot);
    FOUNDATION_ASSERT(chunkSlot < owner->m_activeUploadChunks.size());

    BufferChunk* const cachedChunk = owner->m_activeUploadChunks[chunkSlot];
    if(cachedChunk && cachedChunk->manager == this && cachedChunk->owner == owner && cachedChunk->nativeRecordingID == nativeRecordingID && cachedChunk->physicalQueue == queue){
        const auto fastResult = TrySuballocateFromChunk(*cachedChunk, size, alignment);
        if(fastResult){
            FOUNDATION_ASSERT((owner->m_usedUploadChunkMask & slotBit) != 0u);
            return fastResult;
        }
    }

    return suballocateBufferSlow(size, owner, nativeRecordingID, queue, alignment, chunkSlot, slotBit, cachedChunk);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline b8 CommandList::validateTrackedResourcesReadyForSubmissionFast()const{
    if(!m_currentCmdBuf)
        return validateTrackedResourcesReadyForSubmission();
    if(!m_currentCmdBuf->m_resourceReferences.m_hasDynamicReadinessResources && !m_stateTracker.hasPermanentTextureStates() && !m_stateTracker.hasPermanentBufferStates())
        return true;
    return validateTrackedResourcesReadyForSubmission();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

