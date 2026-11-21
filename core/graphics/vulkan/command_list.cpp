// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>

#include <global/atomic_identity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_list{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_GraphPublicationOwnedReason = FOUNDATION_TEXT("task-graph recorder retains exclusive publication ownership");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandList::CommandList(Device& device, const CommandListParameters& params)
    : RefCounter<GraphicsResource>(device.m_context.cpuScheduler)
    , m_creationDesc(params)
    , m_desc(params)
    , m_stateTracker(device.m_context)
    , m_hostReadbackBarrierTracker(device.m_context.objectArena)
    , m_markerStack(device.m_context.objectArena)
    , m_device(device)
    , m_context(device.m_context)
    , m_gpuCrashMarkerTracker(device.getGpuCrashTracker(), device.m_context.objectArena)
    , m_pendingImageBarriers(device.m_context.objectArena)
    , m_pendingBufferBarriers(device.m_context.objectArena)
    , m_textureOwnershipReleaseDestinations(0u, TextureSubresourceStateKeyHasher(), TextureSubresourceStateKeyEqualTo(), device.m_context.objectArena)
    , m_bufferOwnershipReleaseDestinations(0u, Hasher<NotNull<Buffer*>>(), EqualTo<NotNull<Buffer*>>(), device.m_context.objectArena)
{
    m_cachedQueue = m_device.getQueue(m_creationDesc.physicalQueue);
    if(const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue)){
        m_cachedPhysicalQueueInfo = *queueInfo;
        m_hasCachedPhysicalQueueInfo = true;
        const u8 capabilityBits = static_cast<u8>(queueInfo->capabilities);
        m_universalGraphicsCompute = (capabilityBits & static_cast<u8>(GpuQueueCapability::Graphics)) != 0u && (capabilityBits & static_cast<u8>(GpuQueueCapability::Compute)) != 0u;
        m_cachedQueueContractValid =
            queueInfo->queueClass == m_creationDesc.queueType
            && (capabilityBits & GpuQueueCapability::s_KnownCapabilityBits) != 0u
            && m_cachedQueue != nullptr
            && m_cachedQueue->m_queueID == m_creationDesc.queueType
        ;
    }
}

CommandList::~CommandList()noexcept{
    abortRecordingAttemptWithoutCallbacks();
    m_graphRecordingOwnershipSerial.store(0u, MemoryOrder::release);
    m_graphPublicationState.store(s_GraphPublicationUnowned, MemoryOrder::release);
}

void CommandList::open(const CommandListResourceStateHandoff* initialStates){
    constexpr TStringView s_OperationName = FOUNDATION_TEXT("open command list");
    const u8 publicationState = m_graphPublicationState.load(MemoryOrder::acquire);
    if(publicationState != s_GraphPublicationUnowned){
        if(publicationState == s_GraphPublicationRecording && GraphRecordingOwnership::HasCapability(*this, m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire))){
            rejectCommandRecording(s_OperationName, __hidden_command_list::s_GraphPublicationOwnedReason);
        }
        return;
    }

    AdvanceWrappingNonzero(m_recordingLeaseSerial);

    FOUNDATION_ASSERT_MSG(m_markerStack.empty(), FOUNDATION_TEXT("Vulkan: Command list reopened with unterminated marker scopes"));
    m_stateTracker.rollbackRecordingAttempt();
    clearStateInternal();
    m_hostReadbackBarrierTracker.clear();
    if(m_currentCmdBuf)
        m_currentCmdBuf->discardPendingRecordingCommits();
    discardUnsubmittedUploadChunks();
    m_currentCmdBuf.reset();
    m_nativeRecordingID = 0u;
    m_isRecording = false;
    m_commandRecordingFailed = false;
    m_descriptorBuffersBound = false;

    if(!descriptionMatchesCreation()){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("public description differs from resolved creation identity"));
        return;
    }

    if(initialStates && !initialStates->valid()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Cannot open command list from an invalid resource-state handoff"));
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Cannot open command list from an invalid resource-state handoff"));
        return;
    }
    if(initialStates && !initialStates->validForDeviceGeneration(m_context.deviceGeneration)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Cannot open command list from a resource-state handoff from a retired device generation"));
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Cannot open command list from a resource-state handoff from a retired device generation"));
        return;
    }

    Queue* queue = m_cachedQueue ? m_cachedQueue : m_device.getQueue(m_creationDesc.physicalQueue);
    if(!queue){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Requested queue is not available"));
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Requested queue is not available"));
        m_currentCmdBuf = nullptr;
        return;
    }

    auto commandBuffer = queue->getOrCreateCommandBuffer(m_creationDesc.recordingWorkerDomain, m_creationDesc.recordingWorkerIndex);
    if(!commandBuffer || !*commandBuffer || (*commandBuffer)->m_cmdBuf == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to acquire command buffer"));
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Failed to acquire command buffer"));
        m_currentCmdBuf = nullptr;
        return;
    }
    m_currentCmdBuf = Move(*commandBuffer);
    m_nativeRecordingID = m_currentCmdBuf->m_recordingID;
    if(!matchesNativeLeaseIdentity()){
        rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("native lease does not match resolved creation identity"));
        m_currentCmdBuf.reset();
        m_nativeRecordingID = 0u;
        return;
    }

    auto beginInfo = VulkanDetail::MakeVkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    const VkResult res = m_context.deviceDispatch.vkBeginCommandBuffer(m_currentCmdBuf->m_cmdBuf, &beginInfo);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to begin command buffer recording: {}"), ResultToString(res));
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Failed to begin command buffer recording"));
        discardUnsubmittedUploadChunks();
        m_currentCmdBuf = nullptr;
        m_nativeRecordingID = 0u;
        return;
    }

    m_isRecording = true;

    m_stateTracker.beginRecordingAttempt();
    if(initialStates){
        if(initialStates->empty()){
            if(!initialStates->validForDeviceGeneration(m_context.deviceGeneration)){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Resource-state handoff belongs to a retired device generation"));
                rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("resource-state handoff is incompatible"));
                discardInvalidCommandBuffer();
            }
        }
        else{
            Alloc::ScratchArena scratchArena(VulkanArenaScope::s_StateHandoffArena);

            if(!importResourceStateHandoff(scratchArena, *initialStates)){
                rejectCommandRecording(s_OperationName, FOUNDATION_TEXT("resource-state handoff is incompatible"));
                discardInvalidCommandBuffer();
            }
        }
    }
}

void CommandList::close(CommandListResourceStateHandoff* finalStates){
    const u8 publicationState = m_graphPublicationState.load(MemoryOrder::acquire);
    if(publicationState != s_GraphPublicationUnowned){
        if(finalStates)
            finalStates->reset();
        if(publicationState == s_GraphPublicationRecording && GraphRecordingOwnership::HasCapability(*this, m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire))){
            rejectCommandRecording(s_CloseCommandListOperation, __hidden_command_list::s_GraphPublicationOwnedReason);
        }
        return;
    }

    closeInternal(finalStates);
}

void CommandList::clearState(){
    if(!publicCommandStateAccessible())
        return;
    if(m_isRecording && !validateCommandRecordingScope(FOUNDATION_TEXT("clear command-list state")))
        return;
    clearStateInternal();
}

b8 CommandList::publicCommandStateAccessible()const noexcept{
    const u8 publicationState = m_graphPublicationState.load(MemoryOrder::acquire);
    return publicationState == s_GraphPublicationUnowned || (publicationState == s_GraphPublicationRecording && GraphRecordingOwnership::HasCapability(*this, m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire)));
}

b8 CommandList::beginGraphRecordingOwnership(const u64 recordingLeaseSerial){
    u8 expectedState = s_GraphPublicationUnowned;
    const b8 ownershipAvailable = matchesRecordingLease(recordingLeaseSerial) && m_graphPublicationState.compare_exchange_strong(expectedState, s_GraphPublicationRecording, MemoryOrder::acq_rel, MemoryOrder::acquire);
    if(!ownershipAvailable){
        rejectCommandRecording(FOUNDATION_TEXT("begin task-graph command recording ownership"), FOUNDATION_TEXT("native recording lease is unavailable or already exclusively owned"));
        return false;
    }

    m_graphRecordingOwnershipSerial.store(recordingLeaseSerial, MemoryOrder::release);
    return true;
}

void CommandList::publishGraphRecordingOwnership(const u64 recordingLeaseSerial)noexcept{
    const b8 ownershipMatches = m_graphPublicationState.load(MemoryOrder::acquire) == s_GraphPublicationRecording && m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire) == recordingLeaseSerial;
    FOUNDATION_FATAL_ASSERT_MSG(ownershipMatches, "task-graph command recording ownership changed without its capability");
    m_graphPublicationState.store(s_GraphPublicationRecorded, MemoryOrder::release);
}

void CommandList::cancelGraphRecordingOwnership(const u64 recordingLeaseSerial)noexcept{
    const u8 publicationState = m_graphPublicationState.load(MemoryOrder::acquire);
    const u64 graphRecordingOwnershipSerial = m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire);
    const b8 ownershipMatches = (publicationState == s_GraphPublicationRecording && graphRecordingOwnershipSerial == recordingLeaseSerial) || (publicationState == s_GraphPublicationUnowned && graphRecordingOwnershipSerial == 0u);
    FOUNDATION_FATAL_ASSERT_MSG(ownershipMatches, "cancelled task-graph recording must retain its exact command-list capability");
    if(publicationState != s_GraphPublicationRecording)
        return;
    m_graphRecordingOwnershipSerial.store(0u, MemoryOrder::release);
    m_graphPublicationState.store(s_GraphPublicationUnowned, MemoryOrder::release);
}

void CommandList::revokeGraphRecordingPublication(const u64 recordingLeaseSerial)noexcept{
    if(recordingLeaseSerial == 0u)
        return;

    AtomicBackOff backoff;
    for(;;){
        u8 expectedState = s_GraphPublicationRecorded;
        if(m_graphPublicationState.compare_exchange_strong(expectedState, s_GraphPublicationRevoking, MemoryOrder::acq_rel, MemoryOrder::acquire))
            break;
        if(expectedState != s_GraphPublicationReading)
            return;
        backoff.pause();
    }

    const b8 ownershipMatches = m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire) == recordingLeaseSerial && m_recordingLeaseSerial == recordingLeaseSerial;
    if(!ownershipMatches){
        m_graphPublicationState.store(s_GraphPublicationRecorded, MemoryOrder::release);
        return;
    }

    abortRecordingAttemptWithoutCallbacks();
    m_graphRecordingOwnershipSerial.store(0u, MemoryOrder::release);
    m_graphPublicationState.store(s_GraphPublicationUnowned, MemoryOrder::release);
}

Expected<u64> CommandList::beginGraphSubmissionOwnership()noexcept{
    AtomicBackOff backoff;
    for(;;){
        u8 expectedState = s_GraphPublicationRecorded;
        if(m_graphPublicationState.compare_exchange_strong(expectedState, s_GraphPublicationSubmitting, MemoryOrder::acq_rel, MemoryOrder::acquire))
            break;
        if(expectedState != s_GraphPublicationReading)
            return MakeUnexpected(Failure{});
        backoff.pause();
    }

    const u64 graphRecordingOwnershipSerial = m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire);
    const b8 ownershipValid = graphRecordingOwnershipSerial != 0u && !m_isRecording && !m_commandRecordingFailed && matchesNativeLeaseIdentity();
    if(!ownershipValid){
        m_graphPublicationState.store(s_GraphPublicationRecorded, MemoryOrder::release);
        return MakeUnexpected(Failure{});
    }

    return graphRecordingOwnershipSerial;
}

void CommandList::acceptGraphSubmissionOwnership(const u64 recordingLeaseSerial)noexcept{
    const b8 ownershipMatches = m_graphPublicationState.load(MemoryOrder::acquire) == s_GraphPublicationSubmitting && m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire) == recordingLeaseSerial;
    FOUNDATION_FATAL_ASSERT_MSG(ownershipMatches, "accepted graph submission must retain its exact command-list capability");
    m_graphRecordingOwnershipSerial.store(0u, MemoryOrder::release);
    m_graphPublicationState.store(s_GraphPublicationUnowned, MemoryOrder::release);
}

void CommandList::endGraphSubmissionOwnership(const u64 recordingLeaseSerial)noexcept{
    const u8 publicationState = m_graphPublicationState.load(MemoryOrder::acquire);
    const u64 graphRecordingOwnershipSerial = m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire);
    const b8 ownershipMatches = (publicationState == s_GraphPublicationSubmitting && graphRecordingOwnershipSerial == recordingLeaseSerial) || (publicationState == s_GraphPublicationUnowned && graphRecordingOwnershipSerial == 0u);
    FOUNDATION_FATAL_ASSERT_MSG(ownershipMatches, "graph submission ownership changed without its exact capability");
    if(publicationState == s_GraphPublicationSubmitting)
        m_graphPublicationState.store(s_GraphPublicationRecorded, MemoryOrder::release);
}

void CommandList::closeInternal(CommandListResourceStateHandoff* finalStates){
    if(finalStates)
        finalStates->reset();

    if(!m_currentCmdBuf){
        m_stateTracker.rollbackRecordingAttempt();
        m_isRecording = false;
        m_hostReadbackBarrierTracker.clear();
        clearState();
        return;
    }

    if(m_commandRecordingFailed){
        discardInvalidCommandBuffer();
        return;
    }

    if(!m_isRecording){
        m_hostReadbackBarrierTracker.clear();
        clearStateInternal();
        return;
    }

    if(!validateCommandRecordingScope(s_CloseCommandListOperation)){
        discardInvalidCommandBuffer();
        return;
    }

    if(!finalStates && (!m_textureOwnershipReleaseDestinations.empty() || !m_bufferOwnershipReleaseDestinations.empty())){
        rejectCommandRecording(s_CloseCommandListOperation, FOUNDATION_TEXT("ownership release requires a final resource-state handoff"));
        discardInvalidCommandBuffer();
        return;
    }

    if(!validateTrackedTexturesReadyForClose() || !validateTrackedBuffersReadyForClose()){
        discardInvalidCommandBuffer();
        return;
    }

    endActiveRenderPass();
    if(m_hasPendingBufferCopies)
        flushPendingBufferCopies();
    collectHostReadbackBuffers();
    m_stateTracker.appendKeepInitialStateBarriers(*m_currentCmdBuf, m_pendingImageBarriers, m_pendingBufferBarriers);
    if(!m_pendingImageBarriers.empty() || !m_pendingBufferBarriers.empty()){
        commitBarriers();
        if(m_commandRecordingFailed){
            discardInvalidCommandBuffer();
            return;
        }
    }
    if(m_hostReadbackBarrierTracker.size() != 0u){
        appendHostReadbackBarriers();
        if(m_commandRecordingFailed){
            discardInvalidCommandBuffer();
            return;
        }
    }
    if(!m_textureOwnershipReleaseDestinations.empty() || !m_bufferOwnershipReleaseDestinations.empty()){
        appendPendingOwnershipReleaseBarriers();
        if(m_commandRecordingFailed){
            discardInvalidCommandBuffer();
            return;
        }
    }
    resetMarkerState();

    const VkResult res = m_context.deviceDispatch.vkEndCommandBuffer(m_currentCmdBuf->m_cmdBuf);
    m_isRecording = false;
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Failed to end command buffer recording: {}"), ResultToString(res));
        FOUNDATION_ASSERT_MSG(false, FOUNDATION_TEXT("Vulkan: Failed to end command buffer recording"));
        rejectCommandRecording(s_CloseCommandListOperation, FOUNDATION_TEXT("native command buffer could not be ended"));
        discardInvalidCommandBuffer();
        return;
    }

    if(finalStates)
        exportResourceStateHandoff(*finalStates);

    m_hostReadbackBarrierTracker.clear();
    clearStateInternal();
}

void CommandList::abortRecordingAttemptWithoutCallbacks()noexcept{
    resetPendingBufferCopies();
    invalidateGraphicsDrawValidationCache();
    clearDirtyPipelineStates();
    m_activePipelineKind = s_PipelineKindNone;
    m_activePipelineLayout = VK_NULL_HANDLE;
    m_activePipelinePushConstantBytes = 0u;
    m_activePipelineRequiredCapability = GpuQueueCapability::None;
    m_boundDescriptorBufferOffsets = {};
    m_stateTracker.rollbackRecordingAttempt();
    resetMarkerStateWithoutCallbacks();
    if(m_currentCmdBuf){
        m_currentCmdBuf->discardPendingRecordingCommits();
        abandonUnsubmittedUploadChunks();
        m_currentCmdBuf.reset();
    }
    m_isRecording = false;
    m_commandRecordingFailed = true;
    m_nativeRecordingID = 0u;
    m_renderPassActive = false;
    m_graphicsBuffersValidatedInPass = false;
    m_renderPassFramebuffer = nullptr;
    m_boundDescriptorBufferOffsets = {};
    m_hostReadbackBarrierTracker.clear();
#if defined(NWB_DEBUG)
    m_taskCapabilitiesUsed = GpuQueueCapability::None;
    m_taskDeclaredCapabilities = GpuQueueCapability::None;
    m_taskCapabilityTracking = false;
#endif
}

void CommandList::clearDirtyPipelineStates(const u8 keepMask)noexcept{
    const u8 clearMask = static_cast<u8>(m_dirtyPipelineStateMask & static_cast<u8>(~keepMask));
    if(clearMask == 0u)
        return;

    if((clearMask & s_DirtyGraphicsState) != 0u){
        m_currentGraphicsState.pipeline = nullptr;
        m_currentGraphicsState.framebuffer = nullptr;
        m_currentGraphicsState.viewport.viewports.clear();
        m_currentGraphicsState.viewport.scissorRects.clear();
        m_currentGraphicsState.shadingRateState = {};
        m_currentGraphicsState.dynamicStencilRefValue = 0u;
        m_currentGraphicsState.blendConstantColor = {};
        m_currentGraphicsState.vertexBuffers.clear();
        m_currentGraphicsState.indexBuffer = {};
        m_currentGraphicsState.indirectParams = nullptr;
        m_hasBoundGraphicsState = false;
        invalidateGraphicsDrawValidationCache();
    }
    if((clearMask & s_DirtyComputeState) != 0u){
        m_currentComputeState.pipeline = nullptr;
        m_currentComputeState.indirectParams = nullptr;
        m_hasBoundComputeState = false;
    }
    if((clearMask & s_DirtyMeshletState) != 0u){
        m_currentMeshletState.pipeline = nullptr;
        m_currentMeshletState.framebuffer = nullptr;
        m_currentMeshletState.viewport.viewports.clear();
        m_currentMeshletState.viewport.scissorRects.clear();
        m_currentMeshletState.blendConstantColor = {};
        m_currentMeshletState.indirectParams = nullptr;
        m_currentMeshletState.dynamicStencilRefValue = 0u;
        m_hasBoundMeshletState = false;
    }
    if((clearMask & s_DirtyRayTracingState) != 0u)
        m_currentRayTracingState.shaderTable = nullptr;

    m_dirtyPipelineStateMask &= keepMask;
}

void CommandList::updateActivePushConstantPipeline(const u8 pipelineKind, const VkPipelineLayout layout, const u32 pushConstantBytes, const GpuQueueCapability::Mask capability)noexcept{
    m_activePipelineKind = pipelineKind;
    m_activePipelineLayout = layout;
    m_activePipelinePushConstantBytes = pushConstantBytes;
    m_activePipelineRequiredCapability = capability;
}

void CommandList::clearStateInternal(){
    FOUNDATION_ASSERT_MSG(m_markerStack.empty(), FOUNDATION_TEXT("Vulkan: Command-list logical state cleared with unterminated marker scopes"));
    resetPendingBufferCopies();
    if(m_currentCmdBuf && m_renderPassActive)
        endActiveRenderPass();
    resetMarkerState();

    if(!m_commandRecordingFailed && isRecording())
        collectHostReadbackBuffers();
    m_stateTracker.reset();

    clearDirtyPipelineStates();
    invalidateGraphicsDrawValidationCache();
    m_activePipelineKind = s_PipelineKindNone;
    m_activePipelineLayout = VK_NULL_HANDLE;
    m_activePipelinePushConstantBytes = 0u;
    m_activePipelineRequiredCapability = GpuQueueCapability::None;
    m_boundDescriptorBufferOffsets = {};
    m_hasBoundGraphicsState = false;
    m_graphicsBuffersValidatedInPass = false;
    m_hasBoundComputeState = false;
    m_hasBoundMeshletState = false;
    m_renderPassActive = false;
    m_descriptorBuffersBound = false;
    m_renderPassFramebuffer = nullptr;
#if defined(NWB_DEBUG)
    m_taskCapabilitiesUsed = GpuQueueCapability::None;
    m_taskDeclaredCapabilities = GpuQueueCapability::None;
    m_taskCapabilityTracking = false;
#endif

    m_pendingImageBarriers.clear();
    m_pendingBufferBarriers.clear();
    if(!m_textureOwnershipReleaseDestinations.empty())
        m_textureOwnershipReleaseDestinations.clear();
    if(!m_bufferOwnershipReleaseDestinations.empty())
        m_bufferOwnershipReleaseDestinations.clear();
}

b8 CommandList::matchesNativeLeaseIdentity()const noexcept{
    return
        descriptionMatchesCreation()
        && m_cachedQueueContractValid
        && m_currentCmdBuf
        && m_currentCmdBuf->m_cmdBuf != VK_NULL_HANDLE
        && m_currentCmdBuf->m_arenaState == TrackedCommandBufferArenaState::Leased
        && &m_currentCmdBuf->m_context == &m_context
        && m_recordingLeaseSerial != 0u
        && m_nativeRecordingID != 0u
        && m_currentCmdBuf->m_recordingID == m_nativeRecordingID
        && m_currentCmdBuf->m_recordingWorkerDomain == m_creationDesc.recordingWorkerDomain
        && m_currentCmdBuf->m_recordingWorkerIndex == m_creationDesc.recordingWorkerIndex
        && &m_currentCmdBuf->m_queue == m_cachedQueue
    ;
}

b8 CommandList::matchesActiveNativeLeaseIdentity()const noexcept{
    return m_isRecording && matchesNativeLeaseIdentity();
}

b8 CommandList::matchesSubmissionLease(const GpuPhysicalQueueId executionQueue, const CommandQueue::Enum executionQueueClass, const b8 graphSubmissionAuthorized)const noexcept{
    const u8 requiredPublicationState = graphSubmissionAuthorized ? s_GraphPublicationSubmitting : s_GraphPublicationUnowned;
    if(m_graphPublicationState.load(MemoryOrder::acquire) != requiredPublicationState)
        return false;

    return
        !m_isRecording
        && !m_commandRecordingFailed
        && (graphSubmissionAuthorized == (m_graphRecordingOwnershipSerial.load(MemoryOrder::acquire) != 0u))
        && executionQueue.valid()
        && executionQueue == m_creationDesc.physicalQueue
        && executionQueueClass == m_creationDesc.queueType
        && matchesNativeLeaseIdentity()
    ;
}

b8 CommandList::isTextureAdmittedToCommandQueue(const Texture& texture)const noexcept{
    const GpuPhysicalQueueInfo* const queueInfo = cachedPhysicalQueueInfo();
    return queueInfo && ResourceQueueAdmissionAdmitsQueue(texture.getQueueAdmissionSnapshot(), *queueInfo);
}

b8 CommandList::isTextureReadyForCommandQueue(Texture* const texture, const VkImageUsageFlags requiredUsage)const noexcept{
    return m_device.isTextureReadyForGpuUse(texture, requiredUsage) && isTextureAdmittedToCommandQueue(*texture);
}

b8 CommandList::isBufferAdmittedToCommandQueue(const Buffer& buffer)const noexcept{
    const GpuPhysicalQueueInfo* const queueInfo = cachedPhysicalQueueInfo();
    return queueInfo && ResourceQueueAdmissionAdmitsQueue(buffer.getQueueAdmissionSnapshot(), *queueInfo);
}

b8 CommandList::isBufferReadyForCommandQueue(Buffer* const buffer, const VkBufferUsageFlags requiredUsage)const noexcept{
    return m_device.isBufferReadyForGpuUse(buffer, requiredUsage) && isBufferAdmittedToCommandQueue(*buffer);
}

b8 CommandList::validateTrackedTexturesReadyForClose(){
    if(m_currentCmdBuf->m_resourceReferences.m_hasCloseValidationTextures || !m_hasCachedPhysicalQueueInfo){
        for(const auto texture : m_currentCmdBuf->m_resourceReferences.m_textures){
            if(!isTextureReadyForCommandQueue(texture.get())){
                rejectCommandRecording(s_CloseCommandListOperation, FOUNDATION_TEXT("referenced texture is not ready for this exact command queue"));
                return false;
            }
        }
    }
    for(auto it = m_stateTracker.m_permanentTextureStates.begin(); it != m_stateTracker.m_permanentTextureStates.end(); ++it){
        auto& texture = *MakeNotNull(it.value().texture.get());
        if(!isTextureReadyForCommandQueue(&texture)){
            rejectCommandRecording(s_CloseCommandListOperation, FOUNDATION_TEXT("permanent texture is not ready for this exact command queue"));
            return false;
        }
    }
    for(const auto heap : m_currentCmdBuf->m_referencedDescriptorHeaps){
        if(!heap->retainedResourcesReadyForQueue(m_creationDesc.physicalQueue)){
            rejectCommandRecording(s_CloseCommandListOperation, FOUNDATION_TEXT("descriptor heap contains a resource unavailable to this exact command queue"));
            return false;
        }
    }

    return true;
}

b8 CommandList::validateTrackedBuffersReadyForClose(){
    if(m_currentCmdBuf->m_resourceReferences.m_hasCloseValidationBuffers || !m_hasCachedPhysicalQueueInfo){
        for(const auto buffer : m_currentCmdBuf->m_resourceReferences.m_buffers){
            if(!isBufferReadyForCommandQueue(buffer.get())){
                rejectCommandRecording(s_CloseCommandListOperation, FOUNDATION_TEXT("referenced buffer is not ready for this exact command queue"));
                return false;
            }
        }
    }
    for(auto it = m_stateTracker.m_permanentBufferStates.begin(); it != m_stateTracker.m_permanentBufferStates.end(); ++it){
        auto& buffer = *MakeNotNull(it.value().buffer.get());
        if(!isBufferReadyForCommandQueue(&buffer)){
            rejectCommandRecording(s_CloseCommandListOperation, FOUNDATION_TEXT("permanent buffer is not ready for this exact command queue"));
            return false;
        }
    }

    return true;
}

void CommandList::resetMarkerState(){
    if(!m_markerStack.empty())
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("Vulkan: Recovering {} unterminated command-list marker scope(s)"), m_markerStack.size());

    resetMarkerStateWithoutCallbacks();
}

void CommandList::resetMarkerStateWithoutCallbacks()noexcept{
    if(m_isRecording && m_currentCmdBuf && m_currentCmdBuf->m_cmdBuf != VK_NULL_HANDLE){
        for(usize markerIndex = m_markerStack.size(); markerIndex > 0u; --markerIndex){
            if(m_markerStack[markerIndex - 1u].usesDebugUtils)
                m_context.instanceDispatch.vkCmdEndDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf);
        }
    }

    m_gpuCrashMarkerTracker.resetEventStack();
    m_markerStack.clear();
}

void CommandList::discardUnsubmittedUploadChunks(){
    if(!m_currentCmdBuf || m_currentCmdBuf->m_usedUploadChunkMask == 0u)
        return;

    auto& owner = *MakeNotNull(m_currentCmdBuf.get());
    const u8 usedMask = owner.m_usedUploadChunkMask;
    const u64 nativeRecordingID = m_nativeRecordingID;
    const GpuPhysicalQueueId ownerQueue = owner.m_queue.m_physicalQueue;
    constexpr u8 s_UploadBit = static_cast<u8>(1u << static_cast<usize>(UploadManager::ChunkKind::Upload));
    constexpr u8 s_ScratchBit = static_cast<u8>(1u << static_cast<usize>(UploadManager::ChunkKind::Scratch));
    constexpr u8 s_AccelInputBit = static_cast<u8>(1u << static_cast<usize>(UploadManager::ChunkKind::AccelStructBuildInput));
    const Expected<u64> reusableVersion = m_device.queueGetCompletedInstance(ownerQueue);
    if(!reusableVersion){
        if((usedMask & s_UploadBit) != 0u)
            m_device.m_uploadManager.abandonChunks(ownerQueue, &owner, nativeRecordingID);
        if((usedMask & s_ScratchBit) != 0u)
            m_device.m_scratchManager.abandonChunks(ownerQueue, &owner, nativeRecordingID);
        if((usedMask & s_AccelInputBit) != 0u)
            m_device.m_accelStructInputManager.abandonChunks(ownerQueue, &owner, nativeRecordingID);
        return;
    }

    if((usedMask & s_UploadBit) != 0u)
        m_device.m_uploadManager.discardChunks(ownerQueue, &owner, nativeRecordingID, *reusableVersion);
    if((usedMask & s_ScratchBit) != 0u)
        m_device.m_scratchManager.discardChunks(ownerQueue, &owner, nativeRecordingID, *reusableVersion);
    if((usedMask & s_AccelInputBit) != 0u)
        m_device.m_accelStructInputManager.discardChunks(ownerQueue, &owner, nativeRecordingID, *reusableVersion);
}

void CommandList::abandonUnsubmittedUploadChunks()noexcept{
    if(!m_currentCmdBuf || m_currentCmdBuf->m_usedUploadChunkMask == 0u)
        return;

    auto& owner = *MakeNotNull(m_currentCmdBuf.get());
    const u8 usedMask = owner.m_usedUploadChunkMask;
    const u64 nativeRecordingID = m_nativeRecordingID;
    const GpuPhysicalQueueId ownerQueue = owner.m_queue.m_physicalQueue;
    constexpr u8 s_UploadBit = static_cast<u8>(1u << static_cast<usize>(UploadManager::ChunkKind::Upload));
    constexpr u8 s_ScratchBit = static_cast<u8>(1u << static_cast<usize>(UploadManager::ChunkKind::Scratch));
    constexpr u8 s_AccelInputBit = static_cast<u8>(1u << static_cast<usize>(UploadManager::ChunkKind::AccelStructBuildInput));
    if((usedMask & s_UploadBit) != 0u)
        m_device.m_uploadManager.abandonChunks(ownerQueue, &owner, nativeRecordingID);
    if((usedMask & s_ScratchBit) != 0u)
        m_device.m_scratchManager.abandonChunks(ownerQueue, &owner, nativeRecordingID);
    if((usedMask & s_AccelInputBit) != 0u)
        m_device.m_accelStructInputManager.abandonChunks(ownerQueue, &owner, nativeRecordingID);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

