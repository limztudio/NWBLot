// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "resource_bindings_detail.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_resource_bindings_commands{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 PipelineMatchesHeapLayouts(
    const PipelineBindingState& pipelineBindings,
    const BindingLayout* const resourceLayout,
    const BindingLayout* const samplerLayout,
    const BindingLayout* const accelStructLayout,
    const GpuDescriptorHeapAbi& abi,
    const b8 bindAccelStruct
)noexcept{
    if(!resourceLayout || !samplerLayout)
        return false;
    if(bindAccelStruct && !accelStructLayout)
        return false;

    u32 resourceCount = 0u;
    u32 samplerCount = 0u;
    u32 accelStructCount = 0u;
    const usize layoutCount = pipelineBindings.bindingLayoutsAtCreation.size();
    for(usize layoutIndex = 0u; layoutIndex < layoutCount; ++layoutIndex){
        const BindingLayout* const layout = pipelineBindings.bindingLayoutsAtCreation[layoutIndex].get();
        const u32 setIndex = pipelineBindings.bindingLayoutSetIndicesAtCreation[layoutIndex];
        if(layout == resourceLayout){
            if(setIndex != abi.resourceSetIndex)
                return false;
            ++resourceCount;
        }
        else if(layout == samplerLayout){
            if(setIndex != abi.samplerSetIndex)
                return false;
            ++samplerCount;
        }
        else if(accelStructLayout && layout == accelStructLayout){
            if(setIndex != abi.accelStructSetIndex)
                return false;
            ++accelStructCount;
        }
    }
    return resourceCount == 1u && samplerCount == 1u && accelStructCount == (bindAccelStruct ? 1u : 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setPushConstants(const void* data, usize byteSize){
    if(byteSize == 0)
        return;
    if(m_activePipelineLayout != VK_NULL_HANDLE && data != nullptr && byteSize <= m_activePipelinePushConstantBytes){
        const u32 pushConstantByteSize = static_cast<u32>(byteSize);
        if(VulkanDetail::IsPushConstantByteSizeValid(pushConstantByteSize, m_context.physicalDeviceProperties.limits.maxPushConstantsSize)){
            if(!recordAndValidateCommandCapability(m_activePipelineRequiredCapability, s_SetPushConstantsOperation))
                return;
            m_context.deviceDispatch.vkCmdPushConstants(m_currentCmdBuf->m_cmdBuf, m_activePipelineLayout, VK_SHADER_STAGE_ALL, 0, pushConstantByteSize, data);
            return;
        }
    }

    if(!publicCommandStateAccessible())
        return;
    if(data == nullptr){
        rejectCommandRecording(s_SetPushConstantsOperation, s_DataIsNullReason);
        return;
    }
    if(byteSize > UINT32_MAX){
        rejectCommandRecording(s_SetPushConstantsOperation, FOUNDATION_TEXT("byte size exceeds uint32 range"));
        return;
    }

    const u32 pushConstantByteSize = static_cast<u32>(byteSize);
    if(!VulkanDetail::IsPushConstantByteSizeValid(pushConstantByteSize, m_context.physicalDeviceProperties.limits.maxPushConstantsSize)){
        rejectCommandRecording(s_SetPushConstantsOperation, FOUNDATION_TEXT("byte size is unaligned or exceeds the device limit"));
        return;
    }

    VkPipelineLayout layout = VK_NULL_HANDLE;
    GpuQueueCapability::Mask requiredCapabilities = GpuQueueCapability::None;
    u32 activePipelineCount = 0u;
    u32 pipelinePushConstantByteSize = 0;

    const auto selectPipeline = [&](const VkPipelineLayout candidateLayout, const u32 candidatePushConstantByteSize, const GpuQueueCapability::Mask candidateCapabilities)noexcept{
        ++activePipelineCount;
        layout = candidateLayout;
        requiredCapabilities = candidateCapabilities;
        pipelinePushConstantByteSize = candidatePushConstantByteSize;
    };

    if(m_currentGraphicsState.pipeline){
        auto& gp = *MakeNotNull(m_currentGraphicsState.pipeline);
        selectPipeline(gp.pipelineLayout, gp.pushConstantByteSize, GpuQueueCapability::Graphics);
    }
    if(m_currentComputeState.pipeline){
        auto& cp = *MakeNotNull(m_currentComputeState.pipeline);
        selectPipeline(cp.pipelineLayout, cp.pushConstantByteSize, GpuQueueCapability::Compute);
    }
    if(m_currentMeshletState.pipeline){
        auto& mp = *MakeNotNull(m_currentMeshletState.pipeline);
        selectPipeline(mp.pipelineLayout, mp.pushConstantByteSize, GpuQueueCapability::Graphics);
    }
    if(m_currentRayTracingState.shaderTable){
        auto& rtp = m_currentRayTracingState.shaderTable->getPipeline();
        selectPipeline(rtp.pipelineLayout, rtp.pushConstantByteSize, GpuQueueCapability::Compute);
    }

    if(activePipelineCount != 1u || layout == VK_NULL_HANDLE){
        rejectCommandRecording(s_SetPushConstantsOperation, FOUNDATION_TEXT("exactly one active valid pipeline layout is required"));
        return;
    }
    if(!recordAndValidateCommandCapability(requiredCapabilities, s_SetPushConstantsOperation))
        return;

    if(pipelinePushConstantByteSize == 0){
        rejectCommandRecording(s_SetPushConstantsOperation, FOUNDATION_TEXT("active pipeline layout has no push constant range"));
        return;
    }
    if(pushConstantByteSize > pipelinePushConstantByteSize){
        rejectCommandRecording(s_SetPushConstantsOperation, FOUNDATION_TEXT("byte size exceeds the active pipeline push constant range"));
        return;
    }

    m_context.deviceDispatch.vkCmdPushConstants(m_currentCmdBuf->m_cmdBuf, layout, VK_SHADER_STAGE_ALL, 0, pushConstantByteSize, data);
}

void CommandList::bindDescriptorBufferHeap(GpuDescriptorHeap& heap, const ComputePipeline& pipeline, const GpuDescriptorHandle accelStructHandle){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("bind compute descriptor-buffer heap");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, s_OperationName))
        return;
    if(&pipeline.m_context != &m_context || pipeline.m_pipeline == VK_NULL_HANDLE || pipeline.pipelineLayout == VK_NULL_HANDLE){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("compute pipeline is foreign or has no native pipeline layout"));
        return;
    }
    if(m_currentComputeState.pipeline != &pipeline){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("compute pipeline is not the current command-list pipeline"));
        return;
    }
    if(m_renderPassActive || m_renderPassFramebuffer){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("compute heap binding requires no active render scope"));
        return;
    }

    bindDescriptorBufferHeapNative(heap, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline, accelStructHandle, s_OperationName);
}

void CommandList::bindDescriptorBufferHeap(GpuDescriptorHeap& heap, const GraphicsPipeline& pipeline){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("bind graphics descriptor-buffer heap");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return;
    if(&pipeline.m_context != &m_context || pipeline.m_pipeline == VK_NULL_HANDLE || pipeline.pipelineLayout == VK_NULL_HANDLE){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("graphics pipeline is foreign or has no native pipeline layout"));
        return;
    }
    if(m_currentGraphicsState.pipeline != &pipeline){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("graphics pipeline is not the current command-list pipeline"));
        return;
    }
    if(!m_renderPassActive || !m_renderPassFramebuffer || m_renderPassFramebuffer != m_currentGraphicsState.framebuffer || pipeline.m_framebufferInfo != m_renderPassFramebuffer->m_framebufferInfo){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("graphics heap binding requires the matching active render scope"));
        return;
    }

    bindDescriptorBufferHeapNative(heap, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline, GpuDescriptorHandle::Invalid(), s_OperationName);
}

void CommandList::bindDescriptorBufferHeap(GpuDescriptorHeap& heap, const MeshletPipeline& pipeline){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("bind meshlet descriptor-buffer heap");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Graphics, s_OperationName))
        return;
    if(&pipeline.m_context != &m_context || pipeline.m_pipeline == VK_NULL_HANDLE || pipeline.pipelineLayout == VK_NULL_HANDLE){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("meshlet pipeline is foreign or has no native pipeline layout"));
        return;
    }
    if(m_currentMeshletState.pipeline != &pipeline){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("meshlet pipeline is not the current command-list pipeline"));
        return;
    }
    if(!m_renderPassActive || !m_renderPassFramebuffer || m_renderPassFramebuffer != m_currentMeshletState.framebuffer || pipeline.m_framebufferInfo != m_renderPassFramebuffer->m_framebufferInfo){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("meshlet heap binding requires the matching active render scope"));
        return;
    }

    bindDescriptorBufferHeapNative(heap, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline, GpuDescriptorHandle::Invalid(), s_OperationName);
}

void CommandList::bindDescriptorBufferHeap(GpuDescriptorHeap& heap, const RayTracingPipeline& pipeline, const GpuDescriptorHandle accelStructHandle){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("bind ray-tracing descriptor-buffer heap");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, s_OperationName))
        return;
    if(&pipeline.m_context != &m_context || &pipeline.m_device != &m_device || pipeline.m_pipeline == VK_NULL_HANDLE || pipeline.pipelineLayout == VK_NULL_HANDLE){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("ray-tracing pipeline is foreign or has no native pipeline layout"));
        return;
    }
    ShaderTable* const shaderTable = m_currentRayTracingState.shaderTable;
    if(!shaderTable || shaderTable->m_pipeline.get() != &pipeline){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("ray-tracing pipeline is not the current shader-table pipeline"));
        return;
    }
    if(m_renderPassActive || m_renderPassFramebuffer){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("ray-tracing heap binding requires no active render scope"));
        return;
    }

    bindDescriptorBufferHeapNative(heap, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline, accelStructHandle, s_OperationName);
}

void CommandList::bindDescriptorBufferHeapNative(GpuDescriptorHeap& heap, const VkPipelineBindPoint bindPoint, const PipelineBindingState& pipelineBindings, const GpuDescriptorHandle accelStructHandle, const TStringView operationName){
    DescriptorBufferManager* const manager = m_context.descriptorBufferManager;
    const GpuPhysicalQueueInfo* const exactQueueInfo = cachedPhysicalQueueInfo();
    FOUNDATION_ASSERT(!m_commandRecordingFailed && exactQueueInfo && matchesActiveNativeLeaseIdentity());
    FOUNDATION_ASSERT(pipelineBindings.pipelineLayout != VK_NULL_HANDLE);
    if(
        &heap.m_device != &m_device
        || &heap.m_context != &m_context
        || &heap != &m_device.m_gpuDescriptorHeap
        || !manager
        || manager != &m_device.m_descriptorBufferManager
        || manager != heap.m_context.descriptorBufferManager
        || !m_context.deviceDispatch.vkCmdBindDescriptorBuffersEXT
        || !m_context.deviceDispatch.vkCmdSetDescriptorBufferOffsetsEXT
        || !VulkanDetail::IsDescriptorBufferBackendReady(m_context)
    ){
        rejectCommandRecording(operationName, FOUNDATION_TEXT("heap, device, context, or manager is not exact"));
        return;
    }

    const b8 bindAccelStruct = accelStructHandle.valid();
    u32 bindPointIndex = 0u;
    if(bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE)
        bindPointIndex = 1u;
    else if(bindPoint == VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
        bindPointIndex = 2u;
    CachedDescriptorBufferOffsets& cachedOffsets = m_boundDescriptorBufferOffsets[bindPointIndex];
    if(
        !bindAccelStruct
        && m_descriptorBuffersBound
        && cachedOffsets.valid
        && cachedOffsets.heap == &heap
        && !cachedOffsets.hasAccelStruct
        && cachedOffsets.pipelineLayout == pipelineBindings.pipelineLayout
        && cachedOffsets.descriptorGeneration != 0u
        && cachedOffsets.heapBindingVersion != 0u
        && m_currentCmdBuf->m_descriptorBufferManager == manager
        && m_currentCmdBuf->m_descriptorBufferGeneration == cachedOffsets.descriptorGeneration
        && manager->m_fastBindingGeneration.load(MemoryOrder::acquire) == cachedOffsets.descriptorGeneration
        && heap.m_bindingFastVersion.load(MemoryOrder::acquire) == cachedOffsets.heapBindingVersion
        && !heap.m_hasDynamicReadinessSlots.load(MemoryOrder::acquire)
    )
        return;

    ScopedLock heapLock(heap.m_mutex);
    const GpuDescriptorHeapAbi abi = heap.m_desc.bindlessHeapAbi;
    BindingLayout* const resourceLayout = heap.m_resourceLayout.get();
    BindingLayout* const samplerLayout = heap.m_samplerLayout.get();
    BindingLayout* const accelStructLayout = heap.m_accelStructLayout.get();
    if(heap.m_initialized && heap.m_staticBindingContractValid && heap.m_descriptorBufferGeneration != 0u){
        if(
            (bindAccelStruct && !accelStructLayout)
            || !__hidden_vulkan_resource_bindings_commands::PipelineMatchesHeapLayouts(pipelineBindings, resourceLayout, samplerLayout, accelStructLayout, abi, bindAccelStruct)
        ){
            rejectCommandRecording(operationName, FOUNDATION_TEXT("pipeline creation layouts do not exactly match the heap ABI"));
            return;
        }
    }
    else{
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
            rejectCommandRecording(operationName, FOUNDATION_TEXT("heap descriptor-set ABI is not the dense set-0/1/2 contract or exceeds device limits"));
            return;
        }

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
            || (accelStructLayout && accelStructLayout->m_descriptorBufferSegmentKind != DescriptorBufferSegmentKind::Resource)
        ){
            rejectCommandRecording(operationName, FOUNDATION_TEXT("heap layouts are unavailable, foreign, or incompatible"));
            return;
        }
        const BindlessLayoutDesc* const resourceDesc = resourceLayout->getBindlessDesc();
        const BindlessLayoutDesc* const samplerDesc = samplerLayout->getBindlessDesc();
        const BindlessLayoutDesc* const accelStructDesc = accelStructLayout ? accelStructLayout->getBindlessDesc() : nullptr;
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
            || !__hidden_vulkan_resource_bindings_commands::PipelineMatchesHeapLayouts(pipelineBindings, resourceLayout, samplerLayout, accelStructLayout, abi, bindAccelStruct)
        ){
            rejectCommandRecording(operationName, FOUNDATION_TEXT("pipeline creation layouts do not exactly match the heap ABI"));
            return;
        }
    }

    if(!heap.retainedResourcesReadyForQueueLocked(*exactQueueInfo)){
        rejectCommandRecording(operationName, FOUNDATION_TEXT("heap retains a resource unavailable to this exact command queue"));
        return;
    }
#if defined(NWB_DEBUG)
    for(u32 slot = 0u; slot < heap.m_samplerSlots.nextFresh; ++slot){
        const Sampler* const sampler = heap.m_samplerDescriptorResources[slot].get();
        FOUNDATION_ASSERT(!sampler || (&sampler->m_context == &m_context && sampler->m_sampler != VK_NULL_HANDLE));
    }
#endif

    ScopedLock lifecycleLock(manager->m_lifecycleMutex);
    const auto managerSnapshot = manager->captureBindingSnapshotLocked();
    if(!managerSnapshot || managerSnapshot->generation != heap.m_descriptorBufferGeneration){
        rejectCommandRecording(operationName, FOUNDATION_TEXT("descriptor-buffer manager generation is unavailable or stale"));
        return;
    }

    const DescriptorBufferSegment resourceBlock = heap.m_resourceBufferBlock;
    const DescriptorBufferSegment samplerBlock = heap.m_samplerBufferBlock;
    if(
        resourceBlock.sizeBytes != resourceLayout->m_descriptorBufferSetSizeBytes
        || samplerBlock.sizeBytes != samplerLayout->m_descriptorBufferSetSizeBytes
    ){
        rejectCommandRecording(operationName, FOUNDATION_TEXT("persistent heap blocks are not exact live manager allocations"));
        return;
    }
#if defined(NWB_DEBUG)
    {
        ScopedLock resourceStorageLock(manager->m_resourceSegment.mutex);
        ScopedLock samplerStorageLock(manager->m_samplerSegment.mutex);
        if(
            !manager->isLiveSegmentLocked(manager->m_resourceSegment, resourceBlock, DescriptorBufferSegmentKind::Resource)
            || !manager->isLiveSegmentLocked(manager->m_samplerSegment, samplerBlock, DescriptorBufferSegmentKind::Sampler)
        ){
            rejectCommandRecording(operationName, FOUNDATION_TEXT("persistent heap blocks are not exact live manager allocations"));
            return;
        }
    }
#endif

    DescriptorBufferSegment accelStructBlock{};
    if(bindAccelStruct){
        const u32 slot = accelStructHandle.slot();
        const GpuDescriptorHeap::SlotState::Enum slotState = slot < heap.m_accelStructSlots.slotStates.size() ? heap.m_accelStructSlots.slotStates[slot] : GpuDescriptorHeap::SlotState::Free;
        if(
            accelStructHandle.descriptorClass() != GpuDescriptorClass::AccelStruct
            || slot >= heap.m_accelStructSlots.slotStates.size()
            || slot >= heap.m_accelStructSlots.allocatedClasses.size()
            || slot >= heap.m_accelStructBufferBlocks.size()
            || slot >= heap.m_accelStructResources.size()
            || (slotState != GpuDescriptorHeap::SlotState::Live && slotState != GpuDescriptorHeap::SlotState::PendingRecording)
            || heap.m_accelStructSlots.allocatedClasses[slot] != static_cast<u8>(GpuDescriptorClass::AccelStruct)
        ){
            rejectCommandRecording(operationName, FOUNDATION_TEXT("TLAS handle is stale, retagged, or outside the recordable heap"));
            return;
        }

        accelStructBlock = heap.m_accelStructBufferBlocks[slot];
        AccelStruct* const accelStruct = heap.m_accelStructResources[slot].get();
        Buffer* const backingBuffer = accelStruct ? accelStruct->getBackingBuffer() : nullptr;
        b8 accelStructBlockLive = false;
        {
            ScopedLock resourceStorageLock(manager->m_resourceSegment.mutex);
            accelStructBlockLive = manager->isLiveSegmentLocked(manager->m_resourceSegment, accelStructBlock, DescriptorBufferSegmentKind::Resource);
        }
        if(
            !accelStructBlockLive
            || accelStructBlock.sizeBytes != accelStructLayout->m_descriptorBufferSetSizeBytes
            || !m_device.isAccelStructReadyForGpuUse(accelStruct)
            || !isBufferAdmittedToCommandQueue(*backingBuffer)
            || !accelStruct->m_isTopLevelAtCreation
        ){
            rejectCommandRecording(operationName, FOUNDATION_TEXT("TLAS block or retained top-level acceleration structure is not live"));
            return;
        }
    }

    if((resourceBlock.offsetBytes % managerSnapshot->offsetAlignmentBytes) != 0u || (samplerBlock.offsetBytes % managerSnapshot->offsetAlignmentBytes) != 0u || (bindAccelStruct && (accelStructBlock.offsetBytes % managerSnapshot->offsetAlignmentBytes) != 0u)){
        rejectCommandRecording(operationName, FOUNDATION_TEXT("a descriptor-buffer set block offset is misaligned"));
        return;
    }

    TrackedCommandBuffer& trackedCommandBuffer = *m_currentCmdBuf;
    if(
        (trackedCommandBuffer.m_descriptorBufferManager && trackedCommandBuffer.m_descriptorBufferManager != manager)
        || (trackedCommandBuffer.m_descriptorBufferGeneration != 0u && trackedCommandBuffer.m_descriptorBufferGeneration != managerSnapshot->generation)
        || (m_descriptorBuffersBound && (trackedCommandBuffer.m_descriptorBufferManager != manager || trackedCommandBuffer.m_descriptorBufferGeneration != managerSnapshot->generation))
    ){
        rejectCommandRecording(operationName, FOUNDATION_TEXT("command buffer already references another descriptor generation"));
        return;
    }
    if(!heap.trackCommandBufferUseLocked(trackedCommandBuffer, m_creationDesc.physicalQueue)){
        rejectCommandRecording(operationName, FOUNDATION_TEXT("descriptor-heap command-buffer use identity is exhausted"));
        return;
    }

    if(bindAccelStruct)
        trackedCommandBuffer.retainTopLevelAccelStruct(*heap.m_accelStructResources[accelStructHandle.slot()]);

    ensureDescriptorBuffersBound(*manager, *managerSnapshot);

    const u64 heapBindingVersion = heap.m_bindingFastVersion.load(MemoryOrder::relaxed);
    if(
        cachedOffsets.valid
        && cachedOffsets.pipelineLayout == pipelineBindings.pipelineLayout
        && cachedOffsets.descriptorGeneration == managerSnapshot->generation
        && cachedOffsets.hasAccelStruct == bindAccelStruct
        && cachedOffsets.resourceOffset == resourceBlock.offsetBytes
        && cachedOffsets.samplerOffset == samplerBlock.offsetBytes
        && (!bindAccelStruct || cachedOffsets.accelStructOffset == accelStructBlock.offsetBytes)
    ){
        cachedOffsets.heap = &heap;
        cachedOffsets.heapBindingVersion = heapBindingVersion;
        return;
    }

    const Array<u32, DescriptorBufferManager::s_DescriptorBufferCountWithAccelStruct> bufferIndices = {{
        DescriptorBufferManager::s_ResourceDescriptorBufferIndex,
        DescriptorBufferManager::s_SamplerDescriptorBufferIndex,
        DescriptorBufferManager::s_ResourceDescriptorBufferIndex,
    }};
    const Array<VkDeviceSize, DescriptorBufferManager::s_DescriptorBufferCountWithAccelStruct> offsets = {{ resourceBlock.offsetBytes, samplerBlock.offsetBytes, accelStructBlock.offsetBytes }};

    m_context.deviceDispatch.vkCmdSetDescriptorBufferOffsetsEXT(
        m_currentCmdBuf->m_cmdBuf,
        bindPoint,
        pipelineBindings.pipelineLayout,
        abi.resourceSetIndex,
        bindAccelStruct ? DescriptorBufferManager::s_DescriptorBufferCountWithAccelStruct : DescriptorBufferManager::s_PersistentDescriptorBufferCount,
        bufferIndices.data(),
        offsets.data()
    );

    cachedOffsets.heap = &heap;
    cachedOffsets.pipelineLayout = pipelineBindings.pipelineLayout;
    cachedOffsets.heapBindingVersion = heapBindingVersion;
    cachedOffsets.descriptorGeneration = managerSnapshot->generation;
    cachedOffsets.resourceOffset = resourceBlock.offsetBytes;
    cachedOffsets.samplerOffset = samplerBlock.offsetBytes;
    cachedOffsets.accelStructOffset = accelStructBlock.offsetBytes;
    cachedOffsets.hasAccelStruct = bindAccelStruct;
    cachedOffsets.valid = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::ensureDescriptorBuffersBound(DescriptorBufferManager& manager, const DescriptorBufferManager::BindingSnapshot& snapshot){
    if(m_descriptorBuffersBound)
        return;

    m_context.deviceDispatch.vkCmdBindDescriptorBuffersEXT(m_currentCmdBuf->m_cmdBuf, DescriptorBufferManager::s_PersistentDescriptorBufferCount, snapshot.bindingInfos.data());
    m_currentCmdBuf->m_descriptorBufferManager = &manager;
    m_currentCmdBuf->m_descriptorBufferGeneration = snapshot.generation;
    m_descriptorBuffersBound = true;
    m_boundDescriptorBufferOffsets = {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

