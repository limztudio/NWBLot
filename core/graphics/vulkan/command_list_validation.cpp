// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CommandList::validateTrackedResourcesReadyForSubmission()const{
    if(!m_currentCmdBuf){
        NWB_LOGGER_CRITICAL_WARNING(
            NWB_TEXT("Vulkan: Failed to submit command list: tracked resource readiness ledger is unavailable")
        );
        return false;
    }

    for(Texture* const texture : m_currentCmdBuf->m_resourceReferences.m_textures){
        if(!isTextureReadyForCommandQueue(texture)){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Vulkan: Failed to submit command list: referenced texture is not ready for this exact command queue")
            );
            return false;
        }
    }
    for(
        auto it = m_stateTracker.m_permanentTextureStates.begin();
        it != m_stateTracker.m_permanentTextureStates.end();
        ++it
    ){
        Texture* const texture = it.value().texture.get();
        if(!isTextureReadyForCommandQueue(texture)){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Vulkan: Failed to submit command list: permanent texture is not ready for this exact command queue")
            );
            return false;
        }
    }
    for(GpuDescriptorHeap* const heap : m_currentCmdBuf->m_referencedDescriptorHeaps){
        if(!heap || !heap->retainedResourcesReadyForQueue(m_creationDesc.physicalQueue)){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Vulkan: Failed to submit command list: descriptor heap contains a resource unavailable to this exact command queue")
            );
            return false;
        }
    }
    for(Buffer* const buffer : m_currentCmdBuf->m_resourceReferences.m_buffers){
        if(!isBufferReadyForCommandQueue(buffer)){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Vulkan: Failed to submit command list: referenced buffer is not ready for this exact command queue")
            );
            return false;
        }
    }
    for(
        auto it = m_stateTracker.m_permanentBufferStates.begin();
        it != m_stateTracker.m_permanentBufferStates.end();
        ++it
    ){
        if(!isBufferReadyForCommandQueue(it.value().buffer.get())){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Vulkan: Failed to submit command list: permanent buffer is not ready for this exact command queue")
            );
            return false;
        }
    }

    return true;
}

void CommandList::collectHostReadbackBuffers(){
    for(auto it = m_stateTracker.m_bufferStates.begin(); it != m_stateTracker.m_bufferStates.end(); ++it){
        for(const StateTracker::BufferRangeState& state : it.value()){
            if(it->first && VulkanDetail::HasBufferDeviceWriteState(state.state)){
                registerHostReadbackBuffer(*it->first);
                break;
            }
        }
    }
    for(
        auto it = m_stateTracker.m_permanentBufferStates.begin();
        it != m_stateTracker.m_permanentBufferStates.end();
        ++it
    ){
        Buffer* const buffer = it.value().buffer.get();
        if(buffer && VulkanDetail::HasBufferDeviceWriteState(it.value().state))
            registerHostReadbackBuffer(*buffer);
    }
}

void CommandList::appendHostReadbackBarriers(){
    m_hostReadbackBarrierTracker.appendBarriers(m_pendingBufferBarriers);
    commitBarriers();
}

void CommandList::registerHostReadbackBuffer(Buffer& buffer){
    if(
        buffer.m_creationDesc.cpuAccess == CpuAccessMode::Read
        && m_hostReadbackBarrierTracker.registerBuffer(buffer.m_buffer)
    )
        retainResource(&buffer);
}

void CommandList::registerHostReadbackStagingTexture(StagingTexture& stagingTexture){
    if(
        stagingTexture.m_cpuAccess == CpuAccessMode::Read
        && m_hostReadbackBarrierTracker.registerBuffer(stagingTexture.m_buffer)
    )
        retainResource(&stagingTexture);
}

void CommandList::retainResource(Buffer* resource){
    if(resource)
        m_currentCmdBuf->m_resourceReferences.retainBuffer(*resource);
}

void CommandList::retainResource(Texture* resource){
    if(resource)
        m_currentCmdBuf->m_resourceReferences.retainTexture(*resource);
}

void CommandList::retainResource(Framebuffer* resource){
    if(!resource)
        return;

    m_currentCmdBuf->m_resourceReferences.retainResource(*resource);
    for(const TextureHandle& texture : resource->m_resources){
        if(texture)
            m_currentCmdBuf->m_resourceReferences.trackRetainedTexture(*texture);
    }
}

void CommandList::retainResource(GraphicsResource* resource){
    if(resource)
        m_currentCmdBuf->m_resourceReferences.retainResource(*resource);
}

void CommandList::retainStagingBuffer(Buffer& buffer){
    m_currentCmdBuf->m_referencedStagingBuffers.emplace_back(&buffer, BufferHandle::deleter_type(&m_context.objectArena));
    m_currentCmdBuf->m_resourceReferences.trackRetainedBuffer(buffer);
}

bool CommandList::validateCommandRecordingScope(const tchar* const operationName){
    if(!publicCommandStateAccessible())
        return false;
    if(m_commandRecordingFailed)
        return false;

    constexpr u8 s_KnownCapabilityBits = static_cast<u8>(GpuQueueCapability::Transfer)
        | static_cast<u8>(GpuQueueCapability::Compute)
        | static_cast<u8>(GpuQueueCapability::Graphics)
    ;
    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue);
    const CommandQueue::Enum exactQueueClass = queueInfo ? queueInfo->queueClass : CommandQueue::kCount;
    const u8 exactCapabilityBits = queueInfo ? static_cast<u8>(queueInfo->capabilities) : 0u;
    const bool validRecordingScope = matchesActiveNativeLeaseIdentity();
    const bool validExactQueue = m_creationDesc.physicalQueue.valid()
        && queueInfo
        && queueInfo->id == m_creationDesc.physicalQueue
        && queueInfo->queueClass == m_creationDesc.queueType
        && (exactCapabilityBits & s_KnownCapabilityBits) != 0u
    ;
    if(validRecordingScope && validExactQueue)
        return true;

    NWB_LOGGER_CRITICAL_WARNING(
        NWB_TEXT("Vulkan: Cannot record {} on exact physical queue {}:{} (descriptor class {}, exact class {}, recording {})"),
        operationName ? operationName : NWB_TEXT("unnamed command"),
        m_creationDesc.physicalQueue.index,
        m_creationDesc.physicalQueue.deviceGeneration,
        static_cast<u32>(m_creationDesc.queueType),
        static_cast<u32>(exactQueueClass),
        validRecordingScope
    );
    invalidateCommandRecording();
    return false;
}

bool CommandList::recordAndValidateCommandCapability(
    const GpuQueueCapability::Mask requiredCapabilities,
    const tchar* const operationName
){
    if(!publicCommandStateAccessible())
        return false;
#if defined(NWB_DEBUG)
    // Diagnostics must record the attempted operation even when its physical queue rejects it.
    if(m_taskCapabilityTracking){
        m_taskCapabilitiesUsed = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(m_taskCapabilitiesUsed) | static_cast<u8>(requiredCapabilities)
        );
    }
#endif

    if(m_commandRecordingFailed)
        return false;

    constexpr u8 s_KnownCapabilityBits = static_cast<u8>(GpuQueueCapability::Transfer)
        | static_cast<u8>(GpuQueueCapability::Compute)
        | static_cast<u8>(GpuQueueCapability::Graphics)
    ;
    const u8 requiredBits = static_cast<u8>(requiredCapabilities);
    if(!validateCommandRecordingScope(operationName))
        return false;

    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue);
    const u8 availableBits = queueInfo ? static_cast<u8>(queueInfo->capabilities) : 0u;
    const CommandQueue::Enum exactQueueClass = queueInfo ? queueInfo->queueClass : CommandQueue::kCount;
    const bool validRequiredMask = requiredBits != 0u && (requiredBits & ~s_KnownCapabilityBits) == 0u;
    const bool validRecordingScope = matchesActiveNativeLeaseIdentity();
    const bool validExactQueue = m_creationDesc.physicalQueue.valid()
        && queueInfo
        && queueInfo->id == m_creationDesc.physicalQueue
        && queueInfo->queueClass == m_creationDesc.queueType
    ;
    const bool supported = validRequiredMask
        && validRecordingScope
        && validExactQueue
        && (availableBits & requiredBits) == requiredBits
    ;
    if(supported)
        return true;

    NWB_LOGGER_CRITICAL_WARNING(
        NWB_TEXT("Vulkan: Cannot record {} on exact physical queue {}:{} (descriptor class {}, exact class {}, required mask {}, available mask {}, recording {})"),
        operationName ? operationName : NWB_TEXT("unnamed command"),
        m_creationDesc.physicalQueue.index,
        m_creationDesc.physicalQueue.deviceGeneration,
        static_cast<u32>(m_creationDesc.queueType),
        static_cast<u32>(exactQueueClass),
        static_cast<u32>(requiredBits),
        static_cast<u32>(availableBits),
        validRecordingScope
    );
    invalidateCommandRecording();
    return false;
}

bool CommandList::recordAndValidateAnyCommandCapability(
    const GpuQueueCapability::Mask alternativeCapabilities,
    const tchar* const operationName
){
    if(!publicCommandStateAccessible())
        return false;
    constexpr u8 s_KnownCapabilityBits = static_cast<u8>(GpuQueueCapability::Transfer)
        | static_cast<u8>(GpuQueueCapability::Compute)
        | static_cast<u8>(GpuQueueCapability::Graphics)
    ;
    const u8 alternativeBits = static_cast<u8>(alternativeCapabilities);
    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue);
    const u8 availableBits = queueInfo ? static_cast<u8>(queueInfo->capabilities) : 0u;
    u8 selectableBits = availableBits & alternativeBits;
#if defined(NWB_DEBUG)
    if(m_taskCapabilityTracking){
        const u8 declaredSelectableBits = selectableBits & static_cast<u8>(m_taskDeclaredCapabilities);
        if(declaredSelectableBits != 0u)
            selectableBits = declaredSelectableBits;
    }
#endif

    if(alternativeBits == 0u || (alternativeBits & ~s_KnownCapabilityBits) != 0u)
        return recordAndValidateCommandCapability(alternativeCapabilities, operationName);
    if(selectableBits == 0u)
        selectableBits = alternativeBits;

    const u8 selectedBit = selectableBits & static_cast<u8>(0u - selectableBits);
    return recordAndValidateCommandCapability(static_cast<GpuQueueCapability::Mask>(selectedBit), operationName);
}

void CommandList::rejectCommandRecording(const tchar* const operationName, const tchar* const reason){
    if(!publicCommandStateAccessible())
        return;
    if(!m_commandRecordingFailed){
        NWB_LOGGER_CRITICAL_WARNING(
            NWB_TEXT("Vulkan: Rejecting {} command recording: {}"),
            operationName ? operationName : NWB_TEXT("unnamed command"),
            reason ? reason : NWB_TEXT("invalid command semantics")
        );
    }
    invalidateCommandRecording();
}

void CommandList::invalidateCommandRecording()noexcept{
    if(!publicCommandStateAccessible())
        return;
    m_commandRecordingFailed = true;
    m_hostReadbackBarrierTracker.clear();
}

void CommandList::discardInvalidCommandBuffer(){
    m_stateTracker.rollbackRecordingAttempt();
    if(!m_currentCmdBuf)
        return;

    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Discarding a command list after command recording failed"));
    if(m_isRecording){
        endActiveRenderPass();
        resetMarkerState();

        const VkResult invalidEndResult = m_context.deviceDispatch.vkEndCommandBuffer(m_currentCmdBuf->m_cmdBuf);
        m_isRecording = false;
        if(invalidEndResult != VK_SUCCESS){
            NWB_LOGGER_WARNING(
                NWB_TEXT("Vulkan: Failed to end an invalidated command buffer before discarding it: {}"),
                ResultToString(invalidEndResult)
            );
        }
    }

    m_currentCmdBuf->discardTimerQueryRecordingClaims();
    m_currentCmdBuf->m_resourceReferences.discardBufferStateCommits();
    m_currentCmdBuf->discardRetainedTextureStateCommits();
    m_currentCmdBuf->releasePendingAccelStructBuildCommits();
    m_currentCmdBuf->discardPendingOpacityMicromapBuildCommits();
    discardUnsubmittedUploadChunks();
    m_currentCmdBuf.reset();
    m_nativeRecordingID = 0u;
    m_hostReadbackBarrierTracker.clear();
    clearStateInternal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::beginTaskCapabilityTracking(const GpuQueueCapability::Mask declaredCapabilities){
    if(!publicCommandStateAccessible())
        return;
    NWB_ASSERT(!m_taskCapabilityTracking);
    m_taskCapabilitiesUsed = GpuQueueCapability::None;
    m_taskDeclaredCapabilities = declaredCapabilities;
    m_taskCapabilityTracking = true;
}

GpuQueueCapability::Mask CommandList::endTaskCapabilityTracking(){
    if(!publicCommandStateAccessible())
        return GpuQueueCapability::None;
    NWB_ASSERT(m_taskCapabilityTracking);
    m_taskCapabilityTracking = false;
    m_taskDeclaredCapabilities = GpuQueueCapability::None;
    return m_taskCapabilitiesUsed;
}

void CommandList::cancelTaskCapabilityTracking(){
    if(!publicCommandStateAccessible())
        return;
    m_taskCapabilitiesUsed = GpuQueueCapability::None;
    m_taskDeclaredCapabilities = GpuQueueCapability::None;
    m_taskCapabilityTracking = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

