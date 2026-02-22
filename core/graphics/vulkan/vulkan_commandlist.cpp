// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandList::CommandList(Device& device, const CommandListParameters& params)
    : RefCounter<ICommandList>(*device.getContext().threadPool)
    , m_desc(params)
    , m_stateTracker(MakeCustomUnique<StateTracker>(*device.getContext().objectArena, device.getContext()))
    , m_device(device)
    , m_context(device.getContext())
    , m_aftermathMarkerTracker()
    , m_pendingImageBarriers(Alloc::CustomAllocator<VkImageMemoryBarrier2>(*device.getContext().objectArena))
    , m_pendingBufferBarriers(Alloc::CustomAllocator<VkBufferMemoryBarrier2>(*device.getContext().objectArena))
    , m_pendingCompactions(Alloc::CustomAllocator<RefCountPtr<AccelStruct, ArenaRefDeleter<AccelStruct>>>(*device.getContext().objectArena))
{
    if(m_device.isAftermathEnabled())
        m_device.getAftermathCrashDumpHelper().registerAftermathMarkerTracker(&m_aftermathMarkerTracker);
}
CommandList::~CommandList(){
    if(m_device.isAftermathEnabled())
        m_device.getAftermathCrashDumpHelper().unRegisterAftermathMarkerTracker(&m_aftermathMarkerTracker);
}

void CommandList::open(){
    m_currentCmdBuf = m_device.getQueue(m_desc.queueType)->getOrCreateCommandBuffer();

    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_currentCmdBuf->m_cmdBuf, &beginInfo);

    m_stateTracker->reset();
}

void CommandList::close(){
    commitBarriers();

    if(m_currentCmdBuf)
        vkEndCommandBuffer(m_currentCmdBuf->m_cmdBuf);

    clearState();
}

void CommandList::clearState(){
    m_stateTracker->reset();

    m_currentGraphicsState = {};
    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = {};

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

