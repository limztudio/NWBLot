// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandList::CommandList(Device& device, const CommandListParameters& params)
    : RefCounter<GraphicsResource>(device.m_context.threadPool)
    , m_creationDesc(params)
    , m_desc(params)
    , m_stateTracker(device.m_context)
    , m_hostReadbackBarrierTracker(device.m_context.objectArena)
    , m_device(device)
    , m_context(device.m_context)
    , m_gpuCrashMarkerTracker(device.m_context.objectArena)
    , m_pendingImageBarriers(device.m_context.objectArena)
    , m_pendingBufferBarriers(device.m_context.objectArena)
    , m_textureOwnershipReleaseDestinations(0u, TextureSubresourceStateKeyHasher(), TextureSubresourceStateKeyEqualTo(), device.m_context.objectArena)
    , m_bufferOwnershipReleaseDestinations(0u, Hasher<Buffer*>(), EqualTo<Buffer*>(), device.m_context.objectArena)
{
    if(m_device.isAnyGpuMarkerEnabled())
        m_device.getGpuCrashTracker().registerGpuCrashMarkerTracker(m_gpuCrashMarkerTracker);
}
CommandList::~CommandList(){
    m_stateTracker.rollbackRecordingAttempt();
    resetMarkerState();
    if(m_currentCmdBuf){
        m_currentCmdBuf->discardTimerQueryRecordingClaims();
        m_currentCmdBuf->discardRetainedBufferStateCommits();
        m_currentCmdBuf->discardRetainedTextureStateCommits();
        m_currentCmdBuf->discardPendingAccelStructBuildCommits();
        m_currentCmdBuf->discardPendingOpacityMicromapBuildCommits();
    }
    discardUnsubmittedUploadChunks();

    if(m_device.isAnyGpuMarkerEnabled())
        m_device.getGpuCrashTracker().unRegisterGpuCrashMarkerTracker(m_gpuCrashMarkerTracker);
}

void CommandList::resetMarkerState(){
    if(m_markerDepth != 0u)
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Recovering {} unterminated command-list marker scope(s)"), m_markerDepth);

    if(
        m_isRecording
        && m_currentCmdBuf
        && m_currentCmdBuf->m_cmdBuf != VK_NULL_HANDLE
        && m_context.extensions.EXT_debug_utils
    ){
        for(u32 markerIndex = 0u; markerIndex < m_markerDepth; ++markerIndex)
            m_context.instanceDispatch.vkCmdEndDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf);
    }

    m_gpuCrashMarkerTracker.resetEventStack();
    m_markerDepth = 0u;
}

void CommandList::discardUnsubmittedUploadChunks(){
    if(!m_currentCmdBuf)
        return;

    TrackedCommandBuffer* owner = m_currentCmdBuf.get();
    const u64 nativeRecordingID = m_nativeRecordingID;
    const GpuPhysicalQueueId ownerQueue = owner->m_queue.m_physicalQueue;
    const u64 reusableVersion = m_device.queueGetCompletedInstance(ownerQueue);

    m_device.m_uploadManager.discardChunks(ownerQueue, owner, nativeRecordingID, reusableVersion);
    m_device.m_scratchManager.discardChunks(ownerQueue, owner, nativeRecordingID, reusableVersion);
}

void CommandList::open(const CommandListResourceStateHandoff* initialStates){
    ++m_recordingLeaseSerial;
    if(m_recordingLeaseSerial == 0u)
        ++m_recordingLeaseSerial;

    NWB_ASSERT_MSG(m_markerDepth == 0u, NWB_TEXT("Vulkan: Command list reopened with unterminated marker scopes"));
    m_stateTracker.rollbackRecordingAttempt();
    clearStateInternal();
    m_hostReadbackBarrierTracker.clear();
    if(m_currentCmdBuf){
        m_currentCmdBuf->discardTimerQueryRecordingClaims();
        m_currentCmdBuf->discardRetainedBufferStateCommits();
        m_currentCmdBuf->discardRetainedTextureStateCommits();
        m_currentCmdBuf->discardPendingAccelStructBuildCommits();
        m_currentCmdBuf->discardPendingOpacityMicromapBuildCommits();
    }
    discardUnsubmittedUploadChunks();
    m_currentCmdBuf.reset();
    m_nativeRecordingID = 0u;
    m_isRecording = false;
    m_commandRecordingFailed = false;
    m_descriptorBuffersBound = false;

    if(!descriptionMatchesCreation()){
        rejectCommandRecording(
            NWB_TEXT("open command list"),
            NWB_TEXT("public description differs from resolved creation identity")
        );
        return;
    }

    if(initialStates && !initialStates->valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot open command list from an invalid resource-state handoff"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot open command list from an invalid resource-state handoff"));
        return;
    }
    if(initialStates && !initialStates->validForDeviceGeneration(m_context.deviceGeneration)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot open command list from a resource-state handoff from a retired device generation"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot open command list from a resource-state handoff from a retired device generation"));
        return;
    }

    Queue* queue = m_device.getQueue(m_creationDesc.physicalQueue);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Requested queue is not available"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Requested queue is not available"));
        m_currentCmdBuf = nullptr;
        return;
    }

    m_currentCmdBuf = queue->getOrCreateCommandBuffer(
        m_creationDesc.recordingWorkerDomain,
        m_creationDesc.recordingWorkerIndex
    );
    if(!m_currentCmdBuf || m_currentCmdBuf->m_cmdBuf == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to acquire command buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to acquire command buffer"));
        m_currentCmdBuf = nullptr;
        return;
    }
    m_nativeRecordingID = m_currentCmdBuf->m_recordingID;
    if(!matchesNativeLeaseIdentity()){
        rejectCommandRecording(
            NWB_TEXT("open command list"),
            NWB_TEXT("native lease does not match resolved creation identity")
        );
        m_currentCmdBuf.reset();
        m_nativeRecordingID = 0u;
        return;
    }

    auto beginInfo = VulkanDetail::MakeVkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    const VkResult res = m_context.deviceDispatch.vkBeginCommandBuffer(m_currentCmdBuf->m_cmdBuf, &beginInfo);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to begin command buffer recording: {}"), ResultToString(res));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to begin command buffer recording"));
        discardUnsubmittedUploadChunks();
        m_currentCmdBuf = nullptr;
        m_nativeRecordingID = 0u;
        return;
    }

    m_isRecording = true;

    m_stateTracker.reset();
    m_stateTracker.beginRecordingAttempt();
    m_textureOwnershipReleaseDestinations.clear();
    m_bufferOwnershipReleaseDestinations.clear();
    if(initialStates && !importResourceStateHandoff(*initialStates)){
        rejectCommandRecording(NWB_TEXT("open command list"), NWB_TEXT("resource-state handoff is incompatible"));
        discardInvalidCommandBuffer();
    }
}

void CommandList::close(CommandListResourceStateHandoff* finalStates){
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

    if(!validateCommandRecordingScope(NWB_TEXT("close command list"))){
        discardInvalidCommandBuffer();
        return;
    }

    // An ownership release has no useful consumer without the state handoff that names its destination and final
    // layout. Refuse to record an orphaned release rather than leaving an exclusive resource owned by another
    // family while later code has no way to acquire it.
    if(
        !finalStates
        && (!m_textureOwnershipReleaseDestinations.empty() || !m_bufferOwnershipReleaseDestinations.empty())
    ){
        rejectCommandRecording(
            NWB_TEXT("close command list"),
            NWB_TEXT("ownership release requires a final resource-state handoff")
        );
        discardInvalidCommandBuffer();
        return;
    }

    if(!validateTrackedTexturesReadyForClose() || !validateTrackedBuffersReadyForClose()){
        discardInvalidCommandBuffer();
        return;
    }

    endActiveRenderPass();
    collectHostReadbackBuffers();
    m_stateTracker.appendKeepInitialStateBarriers(*m_currentCmdBuf, m_pendingImageBarriers, m_pendingBufferBarriers);
    commitBarriers();
    if(m_commandRecordingFailed){
        discardInvalidCommandBuffer();
        return;
    }
    appendHostReadbackBarriers();
    if(m_commandRecordingFailed){
        discardInvalidCommandBuffer();
        return;
    }
    appendPendingOwnershipReleaseBarriers();
    if(m_commandRecordingFailed){
        discardInvalidCommandBuffer();
        return;
    }
    resetMarkerState();

    const VkResult res = m_context.deviceDispatch.vkEndCommandBuffer(m_currentCmdBuf->m_cmdBuf);
    m_isRecording = false;
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to end command buffer recording: {}"), ResultToString(res));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to end command buffer recording"));
        rejectCommandRecording(NWB_TEXT("close command list"), NWB_TEXT("native command buffer could not be ended"));
        discardInvalidCommandBuffer();
        return;
    }

    if(finalStates)
        exportResourceStateHandoff(*finalStates);

    m_hostReadbackBarrierTracker.clear();
    clearStateInternal();
}

void CommandList::clearState(){
    if(m_isRecording && !validateCommandRecordingScope(NWB_TEXT("clear command-list state")))
        return;
    clearStateInternal();
}

void CommandList::clearStateInternal(){
    NWB_ASSERT_MSG(m_markerDepth == 0u, NWB_TEXT("Vulkan: Command-list logical state cleared with unterminated marker scopes"));
    if(m_currentCmdBuf && m_renderPassActive)
        endActiveRenderPass();
    resetMarkerState();

    if(!m_commandRecordingFailed && isRecording())
        collectHostReadbackBuffers();
    m_stateTracker.reset();

    m_currentGraphicsState = {};
    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = {};
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
    m_textureOwnershipReleaseDestinations.clear();
    m_bufferOwnershipReleaseDestinations.clear();
}

bool CommandList::descriptionMatchesCreation()const noexcept{
    return
        m_desc.queueType == m_creationDesc.queueType
        && m_desc.physicalQueue == m_creationDesc.physicalQueue
        && m_desc.recordingWorkerDomain == m_creationDesc.recordingWorkerDomain
        && m_desc.recordingWorkerIndex == m_creationDesc.recordingWorkerIndex
    ;
}

bool CommandList::matchesNativeLeaseIdentity()const noexcept{
    if(
        !descriptionMatchesCreation()
        || !m_currentCmdBuf
        || m_currentCmdBuf->m_cmdBuf == VK_NULL_HANDLE
        || m_currentCmdBuf->m_arenaState != TrackedCommandBufferArenaState::Leased
        || &m_currentCmdBuf->m_context != &m_context
        || m_recordingLeaseSerial == 0u
        || m_nativeRecordingID == 0u
        || m_currentCmdBuf->m_recordingID != m_nativeRecordingID
        || m_currentCmdBuf->m_recordingWorkerDomain != m_creationDesc.recordingWorkerDomain
        || m_currentCmdBuf->m_recordingWorkerIndex != m_creationDesc.recordingWorkerIndex
    )
        return false;

    Queue* const expectedQueue = m_device.getQueue(m_creationDesc.physicalQueue);
    return
        expectedQueue
        && expectedQueue->m_physicalQueue == m_creationDesc.physicalQueue
        && expectedQueue->m_queueID == m_creationDesc.queueType
        && &m_currentCmdBuf->m_queue == expectedQueue
    ;
}

bool CommandList::matchesActiveNativeLeaseIdentity()const noexcept{
    return m_isRecording && matchesNativeLeaseIdentity();
}

bool CommandList::matchesSubmissionLease(
    const GpuPhysicalQueueId executionQueue,
    const CommandQueue::Enum executionQueueClass
)const noexcept{
    return
        !m_isRecording
        && !m_commandRecordingFailed
        && executionQueue.valid()
        && executionQueue == m_creationDesc.physicalQueue
        && executionQueueClass == m_creationDesc.queueType
        && matchesNativeLeaseIdentity()
    ;
}

bool CommandList::isTextureAdmittedToCommandQueue(const Texture& texture)const noexcept{
    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue);
    return queueInfo && ResourceQueueAdmissionAdmitsQueue(texture.getQueueAdmissionSnapshot(), *queueInfo);
}

bool CommandList::isTextureReadyForCommandQueue(
    Texture* const texture,
    const VkImageUsageFlags requiredUsage
)const noexcept{
    return m_device.isTextureReadyForGpuUse(texture, requiredUsage)
        && isTextureAdmittedToCommandQueue(*texture)
    ;
}

bool CommandList::isBufferAdmittedToCommandQueue(const Buffer& buffer)const noexcept{
    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue);
    return queueInfo && ResourceQueueAdmissionAdmitsQueue(buffer.getQueueAdmissionSnapshot(), *queueInfo);
}

bool CommandList::isBufferReadyForCommandQueue(
    Buffer* const buffer,
    const VkBufferUsageFlags requiredUsage
)const noexcept{
    return m_device.isBufferReadyForGpuUse(buffer, requiredUsage)
        && isBufferAdmittedToCommandQueue(*buffer)
    ;
}

bool CommandList::validateTrackedTexturesReadyForClose()noexcept{
    for(Texture* const texture : m_currentCmdBuf->m_referencedTextures){
        if(!isTextureReadyForCommandQueue(texture)){
            rejectCommandRecording(NWB_TEXT("close command list"), NWB_TEXT("referenced texture is not ready for this exact command queue"));
            return false;
        }
    }
    for(auto it = m_stateTracker.m_textureStates.begin(); it != m_stateTracker.m_textureStates.end(); ++it){
        Texture* const texture = it->first.texture;
        if(!isTextureReadyForCommandQueue(texture)){
            rejectCommandRecording(NWB_TEXT("close command list"), NWB_TEXT("tracked texture is not ready for this exact command queue"));
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
            rejectCommandRecording(NWB_TEXT("close command list"), NWB_TEXT("permanent texture is not ready for this exact command queue"));
            return false;
        }
    }
    for(
        auto it = m_textureOwnershipReleaseDestinations.begin();
        it != m_textureOwnershipReleaseDestinations.end();
        ++it
    ){
        Texture* const texture = it->first.texture;
        if(!isTextureReadyForCommandQueue(texture)){
            rejectCommandRecording(NWB_TEXT("close command list"), NWB_TEXT("released texture is not ready for this exact command queue"));
            return false;
        }
    }
    for(GpuDescriptorHeap* const heap : m_currentCmdBuf->m_referencedDescriptorHeaps){
        if(!heap || !heap->retainedResourcesReadyForQueue(m_creationDesc.physicalQueue)){
            rejectCommandRecording(
                NWB_TEXT("close command list"),
                NWB_TEXT("descriptor heap contains a resource unavailable to this exact command queue")
            );
            return false;
        }
    }

    return true;
}

bool CommandList::validateTrackedBuffersReadyForClose()noexcept{
    for(Buffer* const buffer : m_currentCmdBuf->m_referencedBuffers){
        if(!isBufferReadyForCommandQueue(buffer)){
            rejectCommandRecording(NWB_TEXT("close command list"), NWB_TEXT("referenced buffer is not ready for this exact command queue"));
            return false;
        }
    }
    for(auto it = m_stateTracker.m_bufferStates.begin(); it != m_stateTracker.m_bufferStates.end(); ++it){
        Buffer* const buffer = it->first;
        if(!isBufferReadyForCommandQueue(buffer)){
            rejectCommandRecording(NWB_TEXT("close command list"), NWB_TEXT("tracked buffer is not ready for this exact command queue"));
            return false;
        }
    }
    for(
        auto it = m_stateTracker.m_permanentBufferStates.begin();
        it != m_stateTracker.m_permanentBufferStates.end();
        ++it
    ){
        Buffer* const buffer = it.value().buffer.get();
        if(!isBufferReadyForCommandQueue(buffer)){
            rejectCommandRecording(NWB_TEXT("close command list"), NWB_TEXT("permanent buffer is not ready for this exact command queue"));
            return false;
        }
    }
    for(
        auto it = m_bufferOwnershipReleaseDestinations.begin();
        it != m_bufferOwnershipReleaseDestinations.end();
        ++it
    ){
        Buffer* const buffer = it->first;
        if(!isBufferReadyForCommandQueue(buffer)){
            rejectCommandRecording(NWB_TEXT("close command list"), NWB_TEXT("released buffer is not ready for this exact command queue"));
            return false;
        }
    }

    return true;
}

bool CommandList::validateTrackedResourcesReadyForSubmission()const noexcept{
    if(!m_currentCmdBuf){
        NWB_LOGGER_CRITICAL_WARNING(
            NWB_TEXT("Vulkan: Failed to submit command list: tracked resource readiness ledger is unavailable")
        );
        return false;
    }

    for(Texture* const texture : m_currentCmdBuf->m_referencedTextures){
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
    for(Buffer* const buffer : m_currentCmdBuf->m_referencedBuffers){
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
        if(it->first && VulkanDetail::HasBufferDeviceWriteState(it.value()))
            registerHostReadbackBuffer(*it->first);
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
        m_currentCmdBuf->retainBuffer(*resource);
}

void CommandList::retainResource(Texture* resource){
    if(resource)
        m_currentCmdBuf->retainTexture(*resource);
}

void CommandList::retainResource(Framebuffer* resource){
    if(!resource)
        return;

    m_currentCmdBuf->retainResource(*resource);
    for(const TextureHandle& texture : resource->m_resources){
        if(texture)
            m_currentCmdBuf->trackRetainedTexture(*texture);
    }
}

void CommandList::retainResource(GraphicsResource* resource){
    if(resource)
        m_currentCmdBuf->retainResource(*resource);
}

void CommandList::retainStagingBuffer(Buffer& buffer){
    m_currentCmdBuf->m_referencedStagingBuffers.emplace_back(&buffer, BufferHandle::deleter_type(&m_context.objectArena));
    m_currentCmdBuf->trackRetainedBuffer(buffer);
}

bool CommandList::validateCommandRecordingScope(const tchar* const operationName)noexcept{
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
)noexcept{
#if defined(NWB_DEBUG)
    // Declaration diagnostics must see the attempted native operation even when its exact physical queue rejects it.
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
)noexcept{
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

void CommandList::rejectCommandRecording(const tchar* const operationName, const tchar* const reason)noexcept{
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
    m_commandRecordingFailed = true;
    m_hostReadbackBarrierTracker.clear();
}

void CommandList::discardInvalidCommandBuffer()noexcept{
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
    m_currentCmdBuf->discardRetainedBufferStateCommits();
    m_currentCmdBuf->discardRetainedTextureStateCommits();
    m_currentCmdBuf->discardPendingAccelStructBuildCommits();
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
    NWB_ASSERT(!m_taskCapabilityTracking);
    m_taskCapabilitiesUsed = GpuQueueCapability::None;
    m_taskDeclaredCapabilities = declaredCapabilities;
    m_taskCapabilityTracking = true;
}

GpuQueueCapability::Mask CommandList::endTaskCapabilityTracking(){
    NWB_ASSERT(m_taskCapabilityTracking);
    m_taskCapabilityTracking = false;
    m_taskDeclaredCapabilities = GpuQueueCapability::None;
    return m_taskCapabilitiesUsed;
}

void CommandList::cancelTaskCapabilityTracking(){
    m_taskCapabilitiesUsed = GpuQueueCapability::None;
    m_taskDeclaredCapabilities = GpuQueueCapability::None;
    m_taskCapabilityTracking = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

