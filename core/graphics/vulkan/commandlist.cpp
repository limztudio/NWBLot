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
    , m_stateTracker(MakeGlobalUnique<StateTracker>(device.m_context.objectArena, device.m_context))
    , m_device(device)
    , m_context(device.m_context)
    , m_aftermathMarkerTracker(device.m_context.objectArena)
    , m_pendingImageBarriers(device.m_context.objectArena)
    , m_pendingBufferBarriers(device.m_context.objectArena)
    , m_pendingCompactions(device.m_context.objectArena)
{
    if(m_device.isAftermathEnabled())
        m_device.getAftermathCrashDumpHelper().registerAftermathMarkerTracker(m_aftermathMarkerTracker);
}
CommandList::~CommandList(){
    discardUnsubmittedUploadChunks();

    if(m_device.isAftermathEnabled())
        m_device.getAftermathCrashDumpHelper().unRegisterAftermathMarkerTracker(m_aftermathMarkerTracker);
}

void CommandList::discardUnsubmittedUploadChunks(){
    if(!m_currentCmdBuf)
        return;

    if(!m_device.m_uploadManager && !m_device.m_scratchManager)
        return;

    TrackedCommandBuffer* owner = m_currentCmdBuf.get();
    const u64 reusableVersion = m_device.queueGetCompletedInstance(m_desc.queueType);

    if(m_device.m_uploadManager)
        m_device.m_uploadManager->discardChunks(m_desc.queueType, owner, reusableVersion);
    if(m_device.m_scratchManager)
        m_device.m_scratchManager->discardChunks(m_desc.queueType, owner, reusableVersion);
}

void CommandList::open(){
    discardUnsubmittedUploadChunks();
    m_currentCmdBuf.reset();

    Queue* queue = m_device.getQueue(m_desc.queueType);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Requested queue is not available"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Requested queue is not available"));
        m_currentCmdBuf = nullptr;
        return;
    }

    m_currentCmdBuf = queue->getOrCreateCommandBuffer();
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

    m_stateTracker->reset();
}

void CommandList::close(){
    if(!m_currentCmdBuf){
        clearState();
        return;
    }

    endActiveRenderPass();
    m_stateTracker->appendKeepInitialStateBarriers(m_pendingImageBarriers, m_pendingBufferBarriers);
    commitBarriers();

    const VkResult res = vkEndCommandBuffer(m_currentCmdBuf->m_cmdBuf);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to end command buffer recording: {}"), ResultToString(res));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to end command buffer recording"));
        discardUnsubmittedUploadChunks();
        m_currentCmdBuf.reset();
        clearState();
        return;
    }

    clearState();
}

void CommandList::clearState(){
    if(m_currentCmdBuf && m_renderPassActive)
        endActiveRenderPass();

    m_stateTracker->reset();

    m_currentGraphicsState = {};
    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = {};
    m_renderPassActive = false;
    m_renderPassFramebuffer = nullptr;

    m_pendingImageBarriers.clear();
    m_pendingBufferBarriers.clear();
}

void CommandList::retainResource(GraphicsResource* resource){
    if(resource)
        m_currentCmdBuf->m_referencedResources.emplace_back(resource, ArenaRefDeleter<GraphicsResource>(&m_context.objectArena));
}

void CommandList::retainStagingBuffer(Buffer& buffer){
    m_currentCmdBuf->m_referencedStagingBuffers.emplace_back(&buffer, BufferHandle::deleter_type(&m_context.objectArena));
}

Device* CommandList::getDevice(){
    return &m_device;
}

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
    if((offsetBytes & 3u) != 0){
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

NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

