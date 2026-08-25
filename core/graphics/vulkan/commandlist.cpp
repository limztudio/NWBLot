// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandList::CommandList(Device& device, const CommandListParameters& params)
    : RefCounter<GraphicsResource>(device.m_context.threadPool)
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
    if(m_currentCmdBuf)
        m_currentCmdBuf->discardRetainedTextureStateCommits();
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
            vkCmdEndDebugUtilsLabelEXT(m_currentCmdBuf->m_cmdBuf);
    }

    m_gpuCrashMarkerTracker.resetEventStack();
    m_markerDepth = 0u;
}

void CommandList::discardUnsubmittedUploadChunks(){
    if(!m_currentCmdBuf)
        return;

    TrackedCommandBuffer* owner = m_currentCmdBuf.get();
    const u64 reusableVersion = m_device.queueGetCompletedInstance(m_desc.physicalQueue);

    m_device.m_uploadManager.discardChunks(m_desc.physicalQueue, owner, reusableVersion);
    m_device.m_scratchManager.discardChunks(m_desc.physicalQueue, owner, reusableVersion);
}

void CommandList::open(const CommandListResourceStateHandoff* initialStates){
    ++m_recordingLeaseSerial;
    if(m_recordingLeaseSerial == 0u)
        ++m_recordingLeaseSerial;

    NWB_ASSERT_MSG(m_markerDepth == 0u, NWB_TEXT("Vulkan: Command list reopened with unterminated marker scopes"));
    m_stateTracker.rollbackRecordingAttempt();
    clearState();
    m_hostReadbackBarrierTracker.clear();
    if(m_currentCmdBuf)
        m_currentCmdBuf->discardRetainedTextureStateCommits();
    discardUnsubmittedUploadChunks();
    m_currentCmdBuf.reset();
    m_isRecording = false;
    m_commandRecordingFailed = false;
    m_descriptorBuffersBound = false;

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

    Queue* queue = m_device.getQueue(m_desc.physicalQueue);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Requested queue is not available"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Requested queue is not available"));
        m_currentCmdBuf = nullptr;
        return;
    }

    m_currentCmdBuf = queue->getOrCreateCommandBuffer(m_desc.recordingWorkerDomain, m_desc.recordingWorkerIndex);
    if(!m_currentCmdBuf || m_currentCmdBuf->m_cmdBuf == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to acquire command buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to acquire command buffer"));
        m_currentCmdBuf = nullptr;
        return;
    }

    auto beginInfo = VulkanDetail::MakeVkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    const VkResult res = vkBeginCommandBuffer(m_currentCmdBuf->m_cmdBuf, &beginInfo);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to begin command buffer recording: {}"), ResultToString(res));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to begin command buffer recording"));
        discardUnsubmittedUploadChunks();
        m_currentCmdBuf = nullptr;
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
        clearState();
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

    if(!validateTrackedBuffersReadyForClose()){
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

    const VkResult res = vkEndCommandBuffer(m_currentCmdBuf->m_cmdBuf);
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
    clearState();
}

void CommandList::clearState(){
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

bool CommandList::validateTrackedBuffersReadyForClose()noexcept{
    for(auto it = m_stateTracker.m_bufferStates.begin(); it != m_stateTracker.m_bufferStates.end(); ++it){
        Buffer* const buffer = it->first;
        if(!m_device.isBufferReadyForGpuUse(buffer)){
            rejectCommandRecording(NWB_TEXT("close command list"), NWB_TEXT("tracked buffer is not ready for GPU access"));
            return false;
        }
    }
    for(
        auto it = m_stateTracker.m_permanentBufferStates.begin();
        it != m_stateTracker.m_permanentBufferStates.end();
        ++it
    ){
        Buffer* const buffer = it->first;
        if(!m_device.isBufferReadyForGpuUse(buffer)){
            rejectCommandRecording(NWB_TEXT("close command list"), NWB_TEXT("permanent buffer is not ready for GPU access"));
            return false;
        }
    }
    for(
        auto it = m_bufferOwnershipReleaseDestinations.begin();
        it != m_bufferOwnershipReleaseDestinations.end();
        ++it
    ){
        Buffer* const buffer = it->first;
        if(!m_device.isBufferReadyForGpuUse(buffer)){
            rejectCommandRecording(NWB_TEXT("close command list"), NWB_TEXT("released buffer is not ready for GPU access"));
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
        if(it->first && VulkanDetail::HasBufferDeviceWriteState(it.value()))
            registerHostReadbackBuffer(*it->first);
    }
}

void CommandList::appendHostReadbackBarriers(){
    m_hostReadbackBarrierTracker.appendBarriers(m_pendingBufferBarriers);
    commitBarriers();
}

void CommandList::registerHostReadbackBuffer(Buffer& buffer){
    if(
        buffer.m_desc.cpuAccess == CpuAccessMode::Read
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

void CommandList::retainResource(GraphicsResource* resource){
    if(resource)
        m_currentCmdBuf->retainResource(*resource);
}

void CommandList::retainStagingBuffer(Buffer& buffer){
    m_currentCmdBuf->m_referencedStagingBuffers.emplace_back(&buffer, BufferHandle::deleter_type(&m_context.objectArena));
}

bool CommandList::validateCommandRecordingScope(const tchar* const operationName)noexcept{
    if(m_commandRecordingFailed)
        return false;

    constexpr u8 s_KnownCapabilityBits = static_cast<u8>(GpuQueueCapability::Transfer)
        | static_cast<u8>(GpuQueueCapability::Compute)
        | static_cast<u8>(GpuQueueCapability::Graphics)
    ;
    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_desc.physicalQueue);
    const CommandQueue::Enum exactQueueClass = queueInfo ? queueInfo->queueClass : CommandQueue::kCount;
    const u8 exactCapabilityBits = queueInfo ? static_cast<u8>(queueInfo->capabilities) : 0u;
    const bool validRecordingScope = m_isRecording
        && m_currentCmdBuf
        && m_currentCmdBuf->m_cmdBuf != VK_NULL_HANDLE
    ;
    const bool validExactQueue = m_desc.physicalQueue.valid()
        && queueInfo
        && queueInfo->id == m_desc.physicalQueue
        && queueInfo->queueClass == m_desc.queueType
        && (exactCapabilityBits & s_KnownCapabilityBits) != 0u
    ;
    if(validRecordingScope && validExactQueue)
        return true;

    NWB_LOGGER_CRITICAL_WARNING(
        NWB_TEXT("Vulkan: Cannot record {} on exact physical queue {}:{} (descriptor class {}, exact class {}, recording {})"),
        operationName ? operationName : NWB_TEXT("unnamed command"),
        m_desc.physicalQueue.index,
        m_desc.physicalQueue.deviceGeneration,
        static_cast<u32>(m_desc.queueType),
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
    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_desc.physicalQueue);
    const u8 availableBits = queueInfo ? static_cast<u8>(queueInfo->capabilities) : 0u;
    const CommandQueue::Enum exactQueueClass = queueInfo ? queueInfo->queueClass : CommandQueue::kCount;
    const bool validRequiredMask = requiredBits != 0u && (requiredBits & ~s_KnownCapabilityBits) == 0u;
    const bool validRecordingScope = m_isRecording
        && m_currentCmdBuf
        && m_currentCmdBuf->m_cmdBuf != VK_NULL_HANDLE
    ;
    const bool validExactQueue = m_desc.physicalQueue.valid()
        && queueInfo
        && queueInfo->id == m_desc.physicalQueue
        && queueInfo->queueClass == m_desc.queueType
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
        m_desc.physicalQueue.index,
        m_desc.physicalQueue.deviceGeneration,
        static_cast<u32>(m_desc.queueType),
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
    const GpuPhysicalQueueInfo* const queueInfo = m_device.getPhysicalQueueInfo(m_desc.physicalQueue);
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

        const VkResult invalidEndResult = vkEndCommandBuffer(m_currentCmdBuf->m_cmdBuf);
        m_isRecording = false;
        if(invalidEndResult != VK_SUCCESS){
            NWB_LOGGER_WARNING(
                NWB_TEXT("Vulkan: Failed to end an invalidated command buffer before discarding it: {}"),
                ResultToString(invalidEndResult)
            );
        }
    }

    m_currentCmdBuf->discardRetainedTextureStateCommits();
    discardUnsubmittedUploadChunks();
    m_currentCmdBuf.reset();
    m_hostReadbackBarrierTracker.clear();
    clearState();
}

#if defined(NWB_DEBUG)
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
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

