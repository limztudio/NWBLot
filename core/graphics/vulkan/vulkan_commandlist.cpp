// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandList::CommandList(Device& device, const CommandListParameters& params)
    : desc(params)
    , stateTracker(new StateTracker())
    , m_device(device)
    , m_context(device.getContext())
    , m_aftermathMarkerTracker()
{
    if(m_device.isAftermathEnabled())
        m_device.getAftermathCrashDumpHelper().registerAftermathMarkerTracker(&m_aftermathMarkerTracker);
}

CommandList::~CommandList(){
    if(m_device.isAftermathEnabled())
        m_device.getAftermathCrashDumpHelper().unRegisterAftermathMarkerTracker(&m_aftermathMarkerTracker);
}

void CommandList::open(){
    currentCmdBuf = m_device.getQueue(desc.queueType)->getOrCreateCommandBuffer();
    
    VkCommandBufferBeginInfo beginInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    vkBeginCommandBuffer(currentCmdBuf->cmdBuf, &beginInfo);
    
    stateTracker->reset();
}

void CommandList::close(){
    if(currentCmdBuf)
        vkEndCommandBuffer(currentCmdBuf->cmdBuf);
}

void CommandList::clearState(){
    stateTracker->reset();
    
    currentGraphicsState = {};
    currentComputeState = {};
    currentMeshletState = {};
    currentRayTracingState = {};
    
    m_pendingImageBarriers.clear();
    m_pendingBufferBarriers.clear();
    m_pendingCompactions.clear();
}

void CommandList::copyTextureToBuffer(IBuffer* _dest, u64 destOffsetBytes, u32 destRowPitch, ITexture* _src, const TextureSlice& srcSlice){
    auto* dest = checked_cast<Buffer*>(_dest);
    auto* src = checked_cast<Texture*>(_src);
    
    VkBufferImageCopy region{};
    region.bufferOffset = destOffsetBytes;
    region.bufferRowLength = 0; // tightly packed
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = srcSlice.mipLevel;
    region.imageSubresource.baseArrayLayer = srcSlice.arraySlice;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { srcSlice.x, srcSlice.y, srcSlice.z };
    region.imageExtent = { srcSlice.width, srcSlice.height, srcSlice.depth };
    
    vkCmdCopyImageToBuffer(currentCmdBuf->cmdBuf, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest->buffer, 1, &region);
    
    currentCmdBuf->referencedResources.push_back(_src);
    currentCmdBuf->referencedResources.push_back(_dest);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

