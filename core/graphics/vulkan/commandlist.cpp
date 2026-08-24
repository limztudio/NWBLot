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
    if(m_currentCmdBuf)
        m_currentCmdBuf->discardRetainedTextureStateCommits();
    discardUnsubmittedUploadChunks();

    if(m_device.isAnyGpuMarkerEnabled())
        m_device.getGpuCrashTracker().unRegisterGpuCrashMarkerTracker(m_gpuCrashMarkerTracker);
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
    if(m_currentCmdBuf)
        m_currentCmdBuf->discardRetainedTextureStateCommits();
    discardUnsubmittedUploadChunks();
    m_currentCmdBuf.reset();
    m_isRecording = false;
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
    m_textureOwnershipReleaseDestinations.clear();
    m_bufferOwnershipReleaseDestinations.clear();
    if(initialStates && !importResourceStateHandoff(*initialStates)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot open command list from an incompatible cross-queue resource-state handoff"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot open command list from an incompatible cross-queue resource-state handoff"));
        discardUnsubmittedUploadChunks();
        m_currentCmdBuf.reset();
        m_isRecording = false;
        clearState();
    }
}

void CommandList::close(CommandListResourceStateHandoff* finalStates){
    if(finalStates)
        finalStates->reset();

    if(!m_currentCmdBuf || !m_isRecording){
        m_isRecording = false;
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
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Closing a command list with an ownership release requires a final resource-state handoff"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Closing a command list with an ownership release requires a final resource-state handoff"));
        m_textureOwnershipReleaseDestinations.clear();
        m_bufferOwnershipReleaseDestinations.clear();
    }

    endActiveRenderPass();
    m_stateTracker.appendKeepInitialStateBarriers(*m_currentCmdBuf, m_pendingImageBarriers, m_pendingBufferBarriers);
    commitBarriers();
    appendPendingOwnershipReleaseBarriers();

    const VkResult res = vkEndCommandBuffer(m_currentCmdBuf->m_cmdBuf);
    m_isRecording = false;
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to end command buffer recording: {}"), ResultToString(res));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to end command buffer recording"));
        m_currentCmdBuf->discardRetainedTextureStateCommits();
        discardUnsubmittedUploadChunks();
        m_currentCmdBuf.reset();
        clearState();
        return;
    }

    if(finalStates)
        exportResourceStateHandoff(*finalStates);

    clearState();
}

void CommandList::clearState(){
    if(m_currentCmdBuf && m_renderPassActive)
        endActiveRenderPass();

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
    m_taskCapabilityTracking = false;
#endif

    m_pendingImageBarriers.clear();
    m_pendingBufferBarriers.clear();
    m_textureOwnershipReleaseDestinations.clear();
    m_bufferOwnershipReleaseDestinations.clear();
}

void CommandList::retainResource(GraphicsResource* resource){
    if(resource)
        m_currentCmdBuf->m_referencedResources.emplace_back(resource, Handle<GraphicsResource>::deleter_type(&m_context.objectArena));
}

void CommandList::retainStagingBuffer(Buffer& buffer){
    m_currentCmdBuf->m_referencedStagingBuffers.emplace_back(&buffer, BufferHandle::deleter_type(&m_context.objectArena));
}

bool CommandList::validateNonTransferCommand(const tchar* const operationName)const{
    if(m_desc.queueType != CommandQueue::Transfer)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {} is invalid on a dedicated transfer command list"), operationName);
    NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: graphics, compute, and ray-tracing commands are invalid on a dedicated transfer command list"));
    return false;
}

#if defined(NWB_DEBUG)
void CommandList::beginTaskCapabilityTracking(){
    NWB_ASSERT(!m_taskCapabilityTracking);
    m_taskCapabilitiesUsed = GpuQueueCapability::None;
    m_taskCapabilityTracking = true;
}

GpuQueueCapability::Mask CommandList::endTaskCapabilityTracking(){
    NWB_ASSERT(m_taskCapabilityTracking);
    m_taskCapabilityTracking = false;
    return m_taskCapabilitiesUsed;
}

void CommandList::recordTaskCapability(const GpuQueueCapability::Mask capability)noexcept{
    if(!m_taskCapabilityTracking)
        return;
    m_taskCapabilitiesUsed = static_cast<GpuQueueCapability::Mask>(
        static_cast<u8>(m_taskCapabilitiesUsed) | static_cast<u8>(capability)
    );
}
#endif

bool CommandList::validateIndirectBuffer(Buffer* bufferResource, u64 offsetBytes, u64 commandSizeBytes, u32 commandCount, const tchar* commandName)const{
#if defined(NWB_DEBUG)
    if(!VulkanDetail::DebugValidateNotNull(commandName, NWB_TEXT("no indirect buffer is bound"), bufferResource))
        return false;

    auto* buffer = bufferResource;
    if(!buffer->m_desc.isDrawIndirectArgs){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute {}: buffer was not created with indirect-argument usage"), commandName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to execute indirect command: buffer was not created with indirect-argument usage"));
        return false;
    }
    if((offsetBytes & s_BufferAlignmentMask) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute {}: offset is not 4-byte aligned"), commandName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to execute indirect command: offset is not 4-byte aligned"));
        return false;
    }

    const u64 totalBytes = commandSizeBytes * commandCount;
    if(!VulkanDetail::DebugValidateBufferRange(buffer->m_desc, offsetBytes, totalBytes, commandName, NWB_TEXT("indirect argument")))
        return false;

    return true;
#else
    static_cast<void>(bufferResource);
    static_cast<void>(offsetBytes);
    static_cast<void>(commandSizeBytes);
    static_cast<void>(commandCount);
    static_cast<void>(commandName);
    return true;
#endif
}

bool CommandList::prepareDrawIndirect(
    const u32 offsetBytes,
    const u32 drawCount,
    const u64 commandSizeBytes,
    const tchar* operationLabel,
    const tchar* commandName,
    VulkanDetail::IndirectDrawIndexMode::Enum indexMode,
    Buffer*& outIndirectBuffer
)const{
    outIndirectBuffer = nullptr;
    if(drawCount == 0)
        return false;
#if defined(NWB_DEBUG)
    if(!m_renderPassActive || !m_currentGraphicsState.pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: no graphics pipeline and active render pass are bound"), operationLabel);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: no graphics pipeline and active render pass are bound"), operationLabel);
        return false;
    }
    if(indexMode == VulkanDetail::IndirectDrawIndexMode::Indexed && !m_currentGraphicsState.indexBuffer.buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: no index buffer is bound"), operationLabel);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: no index buffer is bound"), operationLabel);
        return false;
    }
    if(drawCount > m_context.physicalDeviceProperties.limits.maxDrawIndirectCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: draw count exceeds device limit"), operationLabel);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: draw count exceeds device limit"), operationLabel);
        return false;
    }
    if(!validateIndirectBuffer(m_currentGraphicsState.indirectParams, offsetBytes, commandSizeBytes, drawCount, commandName))
        return false;
#else
    static_cast<void>(offsetBytes);
    static_cast<void>(commandSizeBytes);
    static_cast<void>(operationLabel);
    static_cast<void>(commandName);
    static_cast<void>(indexMode);
#endif

    outIndirectBuffer = m_currentGraphicsState.indirectParams;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

