// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandList::CommandList(Device& device, const CommandListParameters& params)
    : RefCounter<ICommandList>(device.m_context.threadPool)
    , m_desc(params)
    , m_stateTracker(MakeCustomUnique<StateTracker>(device.m_context.objectArena, device.m_context))
    , m_device(device)
    , m_context(device.m_context)
    , m_aftermathMarkerTracker()
    , m_pendingImageBarriers(Alloc::CustomAllocator<VkImageMemoryBarrier2>(device.m_context.objectArena))
    , m_pendingBufferBarriers(Alloc::CustomAllocator<VkBufferMemoryBarrier2>(device.m_context.objectArena))
    , m_pendingCompactions(Alloc::CustomAllocator<RefCountPtr<AccelStruct, ArenaRefDeleter<AccelStruct>>>(device.m_context.objectArena))
{
    if(m_device.isAftermathEnabled())
        m_device.getAftermathCrashDumpHelper().registerAftermathMarkerTracker(m_aftermathMarkerTracker);
}
CommandList::~CommandList(){
    if(m_device.isAftermathEnabled())
        m_device.getAftermathCrashDumpHelper().unRegisterAftermathMarkerTracker(m_aftermathMarkerTracker);
}

void CommandList::open(){
    VkResult res = VK_SUCCESS;

    Queue* queue = m_device.getQueue(m_desc.queueType);
    if(!queue){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Requested queue is not available"));
        m_currentCmdBuf = nullptr;
        return;
    }

    m_currentCmdBuf = queue->getOrCreateCommandBuffer();
    if(!m_currentCmdBuf || m_currentCmdBuf->m_cmdBuf == VK_NULL_HANDLE){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to acquire command buffer"));
        m_currentCmdBuf = nullptr;
        return;
    }

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    res = vkBeginCommandBuffer(m_currentCmdBuf->m_cmdBuf, &beginInfo);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to begin command buffer recording"));
        m_currentCmdBuf = nullptr;
        return;
    }

    m_stateTracker->reset();
}

void CommandList::close(){
    VkResult res = VK_SUCCESS;

    if(!m_currentCmdBuf){
        clearState();
        return;
    }

    endActiveRenderPass();
    commitBarriers();

    res = vkEndCommandBuffer(m_currentCmdBuf->m_cmdBuf);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to end command buffer recording"));
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
    m_pendingCompactions.clear();
}

void CommandList::copyTextureToBuffer(IBuffer* _dest, u64 destOffsetBytes, u32 destRowPitch, ITexture* _src, const TextureSlice& srcSlice){
    auto* dest = checked_cast<Buffer*>(_dest);
    auto* src = checked_cast<Texture*>(_src);

    const FormatInfo& formatInfo = GetFormatInfo(src->m_desc.format);
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if(formatInfo.hasDepth)
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if(formatInfo.hasStencil)
        aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

    VkBufferImageCopy region{};
    region.bufferOffset = destOffsetBytes;
    region.bufferRowLength = destRowPitch > 0 ? (destRowPitch / formatInfo.bytesPerBlock) : 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = aspectMask;
    region.imageSubresource.mipLevel = srcSlice.mipLevel;
    region.imageSubresource.baseArrayLayer = srcSlice.arraySlice;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { static_cast<int32_t>(srcSlice.x), static_cast<int32_t>(srcSlice.y), static_cast<int32_t>(srcSlice.z) };
    region.imageExtent = { srcSlice.width, srcSlice.height, srcSlice.depth };

    vkCmdCopyImageToBuffer(m_currentCmdBuf->m_cmdBuf, src->m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest->m_buffer, 1, &region);

    m_currentCmdBuf->m_referencedResources.push_back(_src);
    m_currentCmdBuf->m_referencedResources.push_back(_dest);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

