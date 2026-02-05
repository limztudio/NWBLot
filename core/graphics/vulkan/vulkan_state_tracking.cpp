// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// CommandList - Resource state transitions and barriers


void CommandList::setResourceStatesForBindingSet(IBindingSet* bindingSet){
    if(!bindingSet || !enableAutomaticBarriers)
        return;
    
    // TODO: Full implementation requires iterating BindingSet items
    // For now, this is a placeholder for automatic barrier insertion
    // based on descriptor set bindings
}

void CommandList::setEnableAutomaticBarriers(bool enable){
    enableAutomaticBarriers = enable;
}

void CommandList::commitBarriers(){
    // Flush any pending barriers to command buffer
    // Currently barriers are inserted immediately in state transition methods
    // This would be used if barriers were deferred to batch them
}

void CommandList::setTextureState(ITexture* _texture, TextureSubresourceSet subresources, ResourceStates stateBits){
    if(!_texture)
        return;
    
    Texture* texture = checked_cast<Texture*>(_texture);
    const VulkanContext& vk = *m_Context;
    
    VkImageMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    barrier.srcStageMask = getVkPipelineStageFlags(ResourceStates::Common);
    barrier.srcAccessMask = getVkAccessFlags(ResourceStates::Common);
    barrier.dstStageMask = getVkPipelineStageFlags(stateBits);
    barrier.dstAccessMask = getVkAccessFlags(stateBits);
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = getVkImageLayout(stateBits);
    barrier.image = texture->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = subresources.baseMipLevel;
    barrier.subresourceRange.levelCount = subresources.numMipLevels;
    barrier.subresourceRange.baseArrayLayer = subresources.baseArraySlice;
    barrier.subresourceRange.layerCount = subresources.numArraySlices;
    
    VkDependencyInfo depInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    
    vk.vkCmdPipelineBarrier2(currentCmdBuf->cmdBuf, &depInfo);
}

void CommandList::setBufferState(IBuffer* _buffer, ResourceStates stateBits){
    if(!_buffer)
        return;
    
    Buffer* buffer = checked_cast<Buffer*>(_buffer);
    const VulkanContext& vk = *m_Context;
    
    VkBufferMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
    barrier.srcStageMask = getVkPipelineStageFlags(ResourceStates::Common);
    barrier.srcAccessMask = getVkAccessFlags(ResourceStates::Common);
    barrier.dstStageMask = getVkPipelineStageFlags(stateBits);
    barrier.dstAccessMask = getVkAccessFlags(stateBits);
    barrier.buffer = buffer->buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    
    VkDependencyInfo depInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    depInfo.bufferMemoryBarrierCount = 1;
    depInfo.pBufferMemoryBarriers = &barrier;
    
    vk.vkCmdPipelineBarrier2(currentCmdBuf->cmdBuf, &depInfo);
}

void CommandList::setAccelStructState(IRayTracingAccelStruct* _as, ResourceStates stateBits){
    if(!_as)
        return;
    
    // Acceleration structures use buffer barriers for the underlying buffer
    // Full implementation would cast to AccelStruct and barrier the backing buffer
}

void CommandList::setPermanentTextureState(ITexture* texture, ResourceStates stateBits){
    // Marks texture as permanently in this state (no further transitions)
    stateTracker->setPermanentTextureState(texture, stateBits);
}

void CommandList::setPermanentBufferState(IBuffer* buffer, ResourceStates stateBits){
    // Marks buffer as permanently in this state
    stateTracker->setPermanentBufferState(buffer, stateBits);
}

//-----------------------------------------------------------------------------
// State Tracker - Internal helper for tracking resource states and bindings
//-----------------------------------------------------------------------------

StateTracker::StateTracker(){
}

StateTracker::~StateTracker(){
}

void StateTracker::reset(){
    // Clear all tracked state
    graphicsState = {};
    computeState = {};
    meshletState = {};
    rayTracingState = {};
}

void StateTracker::setPermanentTextureState(ITexture* texture, ResourceStates state){
    // Mark texture as always in this state
    // Used for swapchain images or external resources
    // TODO: Store permanent state in hash map for automatic barrier handling
}

void StateTracker::setPermanentBufferState(IBuffer* buffer, ResourceStates state){
    // Mark buffer as always in this state
    // TODO: Store permanent state in hash map for automatic barrier handling
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
