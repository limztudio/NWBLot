// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "resource_bindings_detail.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_descriptor_buffer_commands{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool HasExactLayoutAtSet(
    const PipelineBindingState& pipelineBindings,
    const BindingLayout* const expectedLayout,
    const u32 expectedSetIndex,
    const u32 expectedCount
){
    if(!expectedLayout)
        return expectedCount == 0u;

    u32 foundCount = 0u;
    for(u32 layoutIndex = 0u;
        layoutIndex < static_cast<u32>(pipelineBindings.m_bindingLayoutsAtCreation.size());
        ++layoutIndex
    ){
        if(pipelineBindings.m_bindingLayoutsAtCreation[layoutIndex].get() != expectedLayout)
            continue;
        if(pipelineBindings.m_bindingLayoutSetIndicesAtCreation[layoutIndex] != expectedSetIndex)
            return false;
        ++foundCount;
    }
    return foundCount == expectedCount;
}

[[nodiscard]] bool PipelineMatchesHeapLayouts(
    const PipelineBindingState& pipelineBindings,
    const BindingLayout* const resourceLayout,
    const BindingLayout* const samplerLayout,
    const BindingLayout* const accelStructLayout,
    const GpuDescriptorHeapAbi& abi,
    const bool bindAccelStruct
){
    return HasExactLayoutAtSet(pipelineBindings, resourceLayout, abi.resourceSetIndex, 1u)
        && HasExactLayoutAtSet(pipelineBindings, samplerLayout, abi.samplerSetIndex, 1u)
        && HasExactLayoutAtSet(
            pipelineBindings,
            accelStructLayout,
            abi.accelStructSetIndex,
            bindAccelStruct ? 1u : 0u
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::bindDescriptorBufferHeap(
    GpuDescriptorHeap& heap,
    const ComputePipeline& pipeline,
    const GpuDescriptorHandle accelStructHandle
){
    constexpr const tchar* s_OperationName = NWB_TEXT("bind compute descriptor-buffer heap");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, s_OperationName))
        return;
    if(
        &pipeline.m_context != &m_context
        || pipeline.m_pipeline == VK_NULL_HANDLE
        || pipeline.m_pipelineLayout == VK_NULL_HANDLE
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("compute pipeline is foreign or has no native pipeline layout"));
        return;
    }
    if(m_currentComputeState.pipeline != &pipeline){
        rejectCommandRecording(s_OperationName, NWB_TEXT("compute pipeline is not the current command-list pipeline"));
        return;
    }
    if(m_renderPassActive || m_renderPassFramebuffer){
        rejectCommandRecording(s_OperationName, NWB_TEXT("compute heap binding requires no active render scope"));
        return;
    }

    bindDescriptorBufferHeapNative(
        heap,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline,
        accelStructHandle,
        s_OperationName
    );
}

void CommandList::bindDescriptorBufferHeap(GpuDescriptorHeap& heap, const GraphicsPipeline& pipeline){
    constexpr const tchar* s_OperationName = NWB_TEXT("bind graphics descriptor-buffer heap");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return;
    if(
        &pipeline.m_context != &m_context
        || pipeline.m_pipeline == VK_NULL_HANDLE
        || pipeline.m_pipelineLayout == VK_NULL_HANDLE
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("graphics pipeline is foreign or has no native pipeline layout"));
        return;
    }
    if(m_currentGraphicsState.pipeline != &pipeline){
        rejectCommandRecording(s_OperationName, NWB_TEXT("graphics pipeline is not the current command-list pipeline"));
        return;
    }
    if(
        !m_renderPassActive
        || !m_renderPassFramebuffer
        || m_renderPassFramebuffer != m_currentGraphicsState.framebuffer
        || pipeline.m_framebufferInfo != m_renderPassFramebuffer->m_framebufferInfo
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("graphics heap binding requires the matching active render scope"));
        return;
    }

    bindDescriptorBufferHeapNative(
        heap,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline,
        GpuDescriptorHandle::invalid(),
        s_OperationName
    );
}

void CommandList::bindDescriptorBufferHeap(GpuDescriptorHeap& heap, const MeshletPipeline& pipeline){
    constexpr const tchar* s_OperationName = NWB_TEXT("bind meshlet descriptor-buffer heap");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return;
    if(
        &pipeline.m_context != &m_context
        || pipeline.m_pipeline == VK_NULL_HANDLE
        || pipeline.m_pipelineLayout == VK_NULL_HANDLE
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("meshlet pipeline is foreign or has no native pipeline layout"));
        return;
    }
    if(m_currentMeshletState.pipeline != &pipeline){
        rejectCommandRecording(s_OperationName, NWB_TEXT("meshlet pipeline is not the current command-list pipeline"));
        return;
    }
    if(
        !m_renderPassActive
        || !m_renderPassFramebuffer
        || m_renderPassFramebuffer != m_currentMeshletState.framebuffer
        || pipeline.m_framebufferInfo != m_renderPassFramebuffer->m_framebufferInfo
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("meshlet heap binding requires the matching active render scope"));
        return;
    }

    bindDescriptorBufferHeapNative(
        heap,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline,
        GpuDescriptorHandle::invalid(),
        s_OperationName
    );
}

void CommandList::bindDescriptorBufferHeap(
    GpuDescriptorHeap& heap,
    const RayTracingPipeline& pipeline,
    const GpuDescriptorHandle accelStructHandle
){
    constexpr const tchar* s_OperationName = NWB_TEXT("bind ray-tracing descriptor-buffer heap");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, s_OperationName))
        return;
    if(
        &pipeline.m_context != &m_context
        || &pipeline.m_device != &m_device
        || pipeline.m_pipeline == VK_NULL_HANDLE
        || pipeline.m_pipelineLayout == VK_NULL_HANDLE
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("ray-tracing pipeline is foreign or has no native pipeline layout"));
        return;
    }
    ShaderTable* const shaderTable = m_currentRayTracingState.shaderTable;
    if(!shaderTable || shaderTable->m_pipeline.get() != &pipeline){
        rejectCommandRecording(s_OperationName, NWB_TEXT("ray-tracing pipeline is not the current shader-table pipeline"));
        return;
    }
    if(m_renderPassActive || m_renderPassFramebuffer){
        rejectCommandRecording(s_OperationName, NWB_TEXT("ray-tracing heap binding requires no active render scope"));
        return;
    }

    bindDescriptorBufferHeapNative(
        heap,
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        pipeline,
        accelStructHandle,
        s_OperationName
    );
}

void CommandList::bindDescriptorBufferHeapNative(
    GpuDescriptorHeap& heap,
    const VkPipelineBindPoint bindPoint,
    const PipelineBindingState& pipelineBindings,
    const GpuDescriptorHandle accelStructHandle,
    const tchar* const operationName
){
    if(!validateCommandRecordingScope(operationName))
        return;

    DescriptorBufferManager* const manager = m_context.descriptorBufferManager;
    Queue* const expectedQueue = m_device.getQueue(m_creationDesc.physicalQueue);
    const GpuPhysicalQueueInfo* const exactQueueInfo = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue);
    TrackedCommandBuffer* const activeCommandBuffer = m_currentCmdBuf.get();
    if(
        !m_isRecording
        || m_commandRecordingFailed
        || !activeCommandBuffer
        || activeCommandBuffer->m_cmdBuf == VK_NULL_HANDLE
        || activeCommandBuffer->m_arenaState != TrackedCommandBufferArenaState::Leased
        || &activeCommandBuffer->m_context != &m_context
        || !expectedQueue
        || !exactQueueInfo
        || &activeCommandBuffer->m_queue != expectedQueue
        || m_recordingLeaseSerial == 0u
        || &heap.m_device != &m_device
        || &heap.m_context != &m_context
        || &heap != &m_device.m_gpuDescriptorHeap
        || !manager
        || manager != &m_device.m_descriptorBufferManager
        || manager != heap.m_context.descriptorBufferManager
        || pipelineBindings.m_pipelineLayout == VK_NULL_HANDLE
        || !vkCmdBindDescriptorBuffersEXT
        || !vkCmdSetDescriptorBufferOffsetsEXT
        || !VulkanDetail::IsDescriptorBufferBackendReady(m_context)
    ){
        rejectCommandRecording(operationName, NWB_TEXT("heap, device, context, manager, or pipeline layout is not exact"));
        return;
    }

    const bool bindAccelStruct = accelStructHandle.valid();
    ScopedLock heapLock(heap.m_mutex);
    const GpuDescriptorHeapAbi abi = heap.m_desc.bindlessHeapAbi;
    const u32 maxBoundDescriptorSets = m_context.physicalDeviceProperties.limits.maxBoundDescriptorSets;
    if(
        !abi.valid()
        || abi.resourceSetIndex != 0u
        || abi.samplerSetIndex != abi.resourceSetIndex + 1u
        || abi.accelStructSetIndex != abi.samplerSetIndex + 1u
        || abi.resourceSetIndex >= maxBoundDescriptorSets
        || abi.samplerSetIndex >= maxBoundDescriptorSets
        || abi.accelStructSetIndex >= maxBoundDescriptorSets
    ){
        rejectCommandRecording(
            operationName,
            NWB_TEXT("heap descriptor-set ABI is not the dense set-0/1/2 contract or exceeds device limits")
        );
        return;
    }

    BindingLayout* const resourceLayout = heap.m_resourceLayout.get();
    BindingLayout* const samplerLayout = heap.m_samplerLayout.get();
    BindingLayout* const accelStructLayout = heap.m_accelStructLayout.get();
    if(
        !heap.m_initialized
        || heap.m_descriptorBufferGeneration == 0u
        || !resourceLayout
        || !samplerLayout
        || &resourceLayout->m_context != &m_context
        || &samplerLayout->m_context != &m_context
        || (accelStructLayout && &accelStructLayout->m_context != &m_context)
        || !resourceLayout->m_descriptorBufferCompatible
        || !samplerLayout->m_descriptorBufferCompatible
        || (accelStructLayout && !accelStructLayout->m_descriptorBufferCompatible)
        || resourceLayout->m_descriptorBufferSegmentKind != DescriptorBufferSegmentKind::Resource
        || samplerLayout->m_descriptorBufferSegmentKind != DescriptorBufferSegmentKind::Sampler
        || (accelStructLayout
            && accelStructLayout->m_descriptorBufferSegmentKind != DescriptorBufferSegmentKind::Resource)
    ){
        rejectCommandRecording(operationName, NWB_TEXT("heap layouts are unavailable, foreign, or incompatible"));
        return;
    }
    const BindlessLayoutDesc* const resourceDesc = resourceLayout->getBindlessDesc();
    const BindlessLayoutDesc* const samplerDesc = samplerLayout->getBindlessDesc();
    const BindlessLayoutDesc* const accelStructDesc = accelStructLayout
        ? accelStructLayout->getBindlessDesc()
        : nullptr
    ;
    if(
        !resourceDesc
        || !samplerDesc
        || resourceDesc->descriptorSetIndex != abi.resourceSetIndex
        || samplerDesc->descriptorSetIndex != abi.samplerSetIndex
        || resourceDesc->maxCapacity != heap.m_resourceSlots.capacity
        || samplerDesc->maxCapacity != heap.m_samplerSlots.capacity
        || (bindAccelStruct && !accelStructDesc)
        || (accelStructDesc && accelStructDesc->descriptorSetIndex != abi.accelStructSetIndex)
        || (accelStructDesc && accelStructDesc->maxCapacity != 1u)
        || !__hidden_vulkan_descriptor_buffer_commands::PipelineMatchesHeapLayouts(
            pipelineBindings,
            resourceLayout,
            samplerLayout,
            accelStructLayout,
            abi,
            bindAccelStruct
        )
    ){
        rejectCommandRecording(operationName, NWB_TEXT("pipeline creation layouts do not exactly match the heap ABI"));
        return;
    }

    if(!heap.retainedResourcesReadyForQueueLocked(*exactQueueInfo)){
        rejectCommandRecording(operationName, NWB_TEXT("heap retains a resource unavailable to this exact command queue"));
        return;
    }
    for(const SamplerHandle& retainedSampler : heap.m_samplerDescriptorResources){
        Sampler* const sampler = retainedSampler.get();
        if(sampler && (&sampler->m_context != &m_context || sampler->m_sampler == VK_NULL_HANDLE)){
            rejectCommandRecording(operationName, NWB_TEXT("heap retains a foreign or unready sampler"));
            return;
        }
    }

    ScopedLock lifecycleLock(manager->m_lifecycleMutex);
    ScopedLock resourceStorageLock(manager->m_resourceSegment.mutex);
    ScopedLock samplerStorageLock(manager->m_samplerSegment.mutex);
    DescriptorBufferManager::BindingSnapshot managerSnapshot;
    if(
        !manager->captureBindingSnapshotLocked(managerSnapshot)
        || managerSnapshot.generation != heap.m_descriptorBufferGeneration
    ){
        rejectCommandRecording(operationName, NWB_TEXT("descriptor-buffer manager generation is unavailable or stale"));
        return;
    }

    const DescriptorBufferSegment resourceBlock = heap.m_resourceBufferBlock;
    const DescriptorBufferSegment samplerBlock = heap.m_samplerBufferBlock;
    if(
        !manager->isLiveSegmentLocked(
            manager->m_resourceSegment,
            resourceBlock,
            DescriptorBufferSegmentKind::Resource
        )
        || !manager->isLiveSegmentLocked(
            manager->m_samplerSegment,
            samplerBlock,
            DescriptorBufferSegmentKind::Sampler
        )
        || resourceBlock.storageIdentity != managerSnapshot.resourceStorageIdentity
        || samplerBlock.storageIdentity != managerSnapshot.samplerStorageIdentity
        || resourceBlock.sizeBytes != resourceLayout->m_descriptorBufferSetSizeBytes
        || samplerBlock.sizeBytes != samplerLayout->m_descriptorBufferSetSizeBytes
    ){
        rejectCommandRecording(operationName, NWB_TEXT("persistent heap blocks are not exact live manager allocations"));
        return;
    }

    DescriptorBufferSegment accelStructBlock{};
    if(bindAccelStruct){
        const u32 slot = accelStructHandle.slot();
        const GpuDescriptorHeap::SlotState slotState = slot < heap.m_accelStructSlots.slotStates.size()
            ? heap.m_accelStructSlots.slotStates[slot]
            : GpuDescriptorHeap::SlotState::Free
        ;
        if(
            accelStructHandle.descriptorClass() != GpuDescriptorClass::AccelStruct
            || slot >= heap.m_accelStructSlots.slotStates.size()
            || slot >= heap.m_accelStructSlots.allocatedClasses.size()
            || slot >= heap.m_accelStructBufferBlocks.size()
            || slot >= heap.m_accelStructResources.size()
            || (
                slotState != GpuDescriptorHeap::SlotState::Live
                && slotState != GpuDescriptorHeap::SlotState::PendingRecording
            )
            || heap.m_accelStructSlots.allocatedClasses[slot] != static_cast<u8>(GpuDescriptorClass::AccelStruct)
        ){
            rejectCommandRecording(operationName, NWB_TEXT("TLAS handle is stale, retagged, or outside the recordable heap"));
            return;
        }

        accelStructBlock = heap.m_accelStructBufferBlocks[slot];
        AccelStruct* const accelStruct = heap.m_accelStructResources[slot].get();
        Buffer* const backingBuffer = accelStruct ? accelStruct->getBackingBuffer() : nullptr;
        if(
            !manager->isLiveSegmentLocked(
                manager->m_resourceSegment,
                accelStructBlock,
                DescriptorBufferSegmentKind::Resource
            )
            || accelStructBlock.storageIdentity != managerSnapshot.resourceStorageIdentity
            || accelStructBlock.sizeBytes != accelStructLayout->m_descriptorBufferSetSizeBytes
            || !m_device.isAccelStructReadyForGpuUse(accelStruct)
            || !backingBuffer
            || !isBufferAdmittedToCommandQueue(*backingBuffer)
            || !accelStruct->m_isTopLevelAtCreation
            || accelStruct->m_deviceAddress == 0u
        ){
            rejectCommandRecording(
                operationName,
                NWB_TEXT("TLAS block or retained top-level acceleration structure is not live")
            );
            return;
        }
    }

    if(
        (resourceBlock.offsetBytes % managerSnapshot.offsetAlignmentBytes) != 0u
        || (samplerBlock.offsetBytes % managerSnapshot.offsetAlignmentBytes) != 0u
        || (bindAccelStruct && (accelStructBlock.offsetBytes % managerSnapshot.offsetAlignmentBytes) != 0u)
    ){
        rejectCommandRecording(operationName, NWB_TEXT("a descriptor-buffer set block offset is misaligned"));
        return;
    }

    TrackedCommandBuffer& trackedCommandBuffer = *m_currentCmdBuf;
    if(
        (trackedCommandBuffer.m_descriptorBufferManager
            && trackedCommandBuffer.m_descriptorBufferManager != manager)
        || (trackedCommandBuffer.m_descriptorBufferGeneration != 0u
            && trackedCommandBuffer.m_descriptorBufferGeneration != managerSnapshot.generation)
        || (m_descriptorBuffersBound
            && (
                trackedCommandBuffer.m_descriptorBufferManager != manager
                || trackedCommandBuffer.m_descriptorBufferGeneration != managerSnapshot.generation
            ))
    ){
        rejectCommandRecording(operationName, NWB_TEXT("command buffer already references another descriptor generation"));
        return;
    }
    if(!heap.trackCommandBufferUseLocked(trackedCommandBuffer, m_creationDesc.physicalQueue)){
        rejectCommandRecording(operationName, NWB_TEXT("descriptor-heap command-buffer use identity is exhausted"));
        return;
    }
    for(const BufferHandle& retainedBuffer : heap.m_resourceDescriptorBuffers){
        if(retainedBuffer)
            trackedCommandBuffer.trackRetainedBuffer(*retainedBuffer);
    }
    for(const TextureHandle& retainedTexture : heap.m_resourceDescriptorTextures){
        if(retainedTexture)
            trackedCommandBuffer.trackRetainedTexture(*retainedTexture);
    }

    ensureDescriptorBuffersBound(*manager, managerSnapshot);

    // Resource, sampler, and TLAS heap sets are contiguous at 0/1/2.
    const u32 bufferIndices[DescriptorBufferManager::s_DescriptorBufferCountWithAccelStruct] = {
        DescriptorBufferManager::s_ResourceDescriptorBufferIndex,
        DescriptorBufferManager::s_SamplerDescriptorBufferIndex,
        DescriptorBufferManager::s_ResourceDescriptorBufferIndex,
    };
    const VkDeviceSize offsets[DescriptorBufferManager::s_DescriptorBufferCountWithAccelStruct] = {
        resourceBlock.offsetBytes,
        samplerBlock.offsetBytes,
        accelStructBlock.offsetBytes
    };

    vkCmdSetDescriptorBufferOffsetsEXT(
        m_currentCmdBuf->m_cmdBuf,
        bindPoint,
        pipelineBindings.m_pipelineLayout,
        abi.resourceSetIndex,
        bindAccelStruct
            ? DescriptorBufferManager::s_DescriptorBufferCountWithAccelStruct
            : DescriptorBufferManager::s_PersistentDescriptorBufferCount,
        bufferIndices,
        offsets
    );
}

void CommandList::ensureDescriptorBuffersBound(
    DescriptorBufferManager& manager,
    const DescriptorBufferManager::BindingSnapshot& snapshot
){
    if(m_descriptorBuffersBound)
        return;

    vkCmdBindDescriptorBuffersEXT(
        m_currentCmdBuf->m_cmdBuf,
        DescriptorBufferManager::s_PersistentDescriptorBufferCount,
        snapshot.bindingInfos.data()
    );
    m_currentCmdBuf->m_descriptorBufferManager = &manager;
    m_currentCmdBuf->m_descriptorBufferGeneration = snapshot.generation;
    m_descriptorBuffersBound = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

