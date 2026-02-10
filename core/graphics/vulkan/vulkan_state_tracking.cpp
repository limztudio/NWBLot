// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resource state transitions and barriers


void CommandList::setResourceStatesForBindingSet(IBindingSet* bindingSet){
    if(!bindingSet || !enableAutomaticBarriers)
        return;
    
    BindingSet* bs = checked_cast<BindingSet*>(bindingSet);
    
    for(const auto& item : bs->desc.bindings){
        switch(item.type){
        case ResourceType::Texture_SRV:
            if(item.resourceHandle)
                setTextureState(static_cast<ITexture*>(item.resourceHandle), item.subresources, ResourceStates::ShaderResource);
            break;
        case ResourceType::Texture_UAV:
            if(item.resourceHandle)
                setTextureState(static_cast<ITexture*>(item.resourceHandle), item.subresources, ResourceStates::UnorderedAccess);
            break;
        case ResourceType::StructuredBuffer_SRV:
        case ResourceType::RawBuffer_SRV:
        case ResourceType::TypedBuffer_SRV:
            if(item.resourceHandle)
                setBufferState(static_cast<IBuffer*>(item.resourceHandle), ResourceStates::ShaderResource);
            break;
        case ResourceType::StructuredBuffer_UAV:
        case ResourceType::RawBuffer_UAV:
        case ResourceType::TypedBuffer_UAV:
            if(item.resourceHandle)
                setBufferState(static_cast<IBuffer*>(item.resourceHandle), ResourceStates::UnorderedAccess);
            break;
        case ResourceType::ConstantBuffer:
        case ResourceType::VolatileConstantBuffer:
            if(item.resourceHandle)
                setBufferState(static_cast<IBuffer*>(item.resourceHandle), ResourceStates::ConstantBuffer);
            break;
        case ResourceType::RayTracingAccelStruct:
            if(item.resourceHandle)
                setAccelStructState(static_cast<IRayTracingAccelStruct*>(item.resourceHandle), ResourceStates::AccelStructRead);
            break;
        case ResourceType::Sampler:
        case ResourceType::None:
        case ResourceType::PushConstants:
        case ResourceType::SamplerFeedbackTexture_UAV:
        default:
            break;
        }
    }
}

void CommandList::setEnableAutomaticBarriers(bool enable){
    enableAutomaticBarriers = enable;
}

void CommandList::commitBarriers(){
    if(m_pendingImageBarriers.empty() && m_pendingBufferBarriers.empty())
        return;
    
    VkDependencyInfo depInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    depInfo.imageMemoryBarrierCount = static_cast<u32>(m_pendingImageBarriers.size());
    depInfo.pImageMemoryBarriers = m_pendingImageBarriers.data();
    depInfo.bufferMemoryBarrierCount = static_cast<u32>(m_pendingBufferBarriers.size());
    depInfo.pBufferMemoryBarriers = m_pendingBufferBarriers.data();
    
    vkCmdPipelineBarrier2(currentCmdBuf->cmdBuf, &depInfo);
    
    m_pendingImageBarriers.clear();
    m_pendingBufferBarriers.clear();
}

void CommandList::setTextureState(ITexture* _texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits){
    if(!_texture)
        return;
    
    if(stateTracker->isPermanentTexture(_texture))
        return;
    
    Texture* texture = checked_cast<Texture*>(_texture);
    
    ResourceStates::Mask oldState = stateTracker->getTextureState(_texture, subresources.baseArraySlice, subresources.baseMipLevel);
    if(oldState == stateBits)
        return;
    
    VkImageMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    barrier.srcStageMask = __hidden_vulkan::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.srcAccessMask = __hidden_vulkan::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = __hidden_vulkan::GetVkPipelineStageFlags(stateBits);
    barrier.dstAccessMask = __hidden_vulkan::GetVkAccessFlags(stateBits);
    barrier.oldLayout = oldState != ResourceStates::Unknown ? __hidden_vulkan::GetVkImageLayout(oldState) : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = __hidden_vulkan::GetVkImageLayout(stateBits);
    barrier.image = texture->image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = subresources.baseMipLevel;
    barrier.subresourceRange.levelCount = subresources.numMipLevels;
    barrier.subresourceRange.baseArrayLayer = subresources.baseArraySlice;
    barrier.subresourceRange.layerCount = subresources.numArraySlices;
    
    stateTracker->beginTrackingTexture(_texture, subresources, stateBits);
    
    if(!enableAutomaticBarriers){
        m_pendingImageBarriers.push_back(barrier);
        return;
    }
    
    VkDependencyInfo depInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    
    vkCmdPipelineBarrier2(currentCmdBuf->cmdBuf, &depInfo);
}

void CommandList::setBufferState(IBuffer* _buffer, ResourceStates::Mask stateBits){
    if(!_buffer)
        return;
    
    if(stateTracker->isPermanentBuffer(_buffer))
        return;
    
    Buffer* buffer = checked_cast<Buffer*>(_buffer);
    
    ResourceStates::Mask oldState = stateTracker->getBufferState(_buffer);
    if(oldState == stateBits)
        return;
    
    VkBufferMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
    barrier.srcStageMask = __hidden_vulkan::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.srcAccessMask = __hidden_vulkan::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = __hidden_vulkan::GetVkPipelineStageFlags(stateBits);
    barrier.dstAccessMask = __hidden_vulkan::GetVkAccessFlags(stateBits);
    barrier.buffer = buffer->buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;
    
    stateTracker->beginTrackingBuffer(_buffer, stateBits);
    
    if(!enableAutomaticBarriers){
        m_pendingBufferBarriers.push_back(barrier);
        return;
    }
    
    VkDependencyInfo depInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    depInfo.bufferMemoryBarrierCount = 1;
    depInfo.pBufferMemoryBarriers = &barrier;
    
    vkCmdPipelineBarrier2(currentCmdBuf->cmdBuf, &depInfo);
}

void CommandList::setAccelStructState(IRayTracingAccelStruct* _as, ResourceStates::Mask stateBits){
    if(!_as)
        return;
    
    AccelStruct* as = checked_cast<AccelStruct*>(_as);
    
    if(as->buffer){
        setBufferState(as->buffer.get(), stateBits);
    }
}

void CommandList::setPermanentTextureState(ITexture* texture, ResourceStates::Mask stateBits){
    // Marks texture as permanently in this state (no further transitions)
    stateTracker->setPermanentTextureState(texture, stateBits);
}

void CommandList::setPermanentBufferState(IBuffer* buffer, ResourceStates::Mask stateBits){
    // Marks buffer as permanently in this state
    stateTracker->setPermanentBufferState(buffer, stateBits);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    
    m_textureStates.clear();
    m_bufferStates.clear();
    // Note: permanent states are NOT cleared on reset - they persist across command lists
}

void StateTracker::setPermanentTextureState(ITexture* texture, ResourceStates::Mask state){
    if(!texture)
        return;
    
    m_permanentTextureStates[texture] = state;
}

void StateTracker::setPermanentBufferState(IBuffer* buffer, ResourceStates::Mask state){
    if(!buffer)
        return;
    
    m_permanentBufferStates[buffer] = state;
}

bool StateTracker::isPermanentTexture(ITexture* texture)const{
    if(!texture)
        return false;
    
    return m_permanentTextureStates.count(texture) > 0;
}

bool StateTracker::isPermanentBuffer(IBuffer* buffer)const{
    if(!buffer)
        return false;
    
    return m_permanentBufferStates.count(buffer) > 0;
}

ResourceStates::Mask StateTracker::getTextureState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel)const{
    (void)arraySlice;
    (void)mipLevel;
    
    if(!texture)
        return ResourceStates::Unknown;
    
    // Check permanent state first
    auto permIt = m_permanentTextureStates.find(texture);
    if(permIt != m_permanentTextureStates.end())
        return permIt->second;
    
    // Check tracked state
    auto it = m_textureStates.find(texture);
    if(it != m_textureStates.end())
        return it->second;
    
    return ResourceStates::Unknown;
}

ResourceStates::Mask StateTracker::getBufferState(IBuffer* buffer)const{
    if(!buffer)
        return ResourceStates::Unknown;
    
    // Check permanent state first
    auto permIt = m_permanentBufferStates.find(buffer);
    if(permIt != m_permanentBufferStates.end())
        return permIt->second;
    
    // Check tracked state
    auto it = m_bufferStates.find(buffer);
    if(it != m_bufferStates.end())
        return it->second;
    
    return ResourceStates::Unknown;
}

void StateTracker::beginTrackingTexture(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask state){
    (void)subresources;
    
    if(!texture)
        return;
    
    // Don't override permanent states
    if(m_permanentTextureStates.count(texture) > 0)
        return;
    
    m_textureStates[texture] = state;
}

void StateTracker::beginTrackingBuffer(IBuffer* buffer, ResourceStates::Mask state){
    if(!buffer)
        return;
    
    // Don't override permanent states
    if(m_permanentBufferStates.count(buffer) > 0)
        return;
    
    m_bufferStates[buffer] = state;
}

void StateTracker::setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers){
    if(!texture)
        return;
    
    m_textureUavBarriers[texture] = enableBarriers;
}

void StateTracker::setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers){
    if(!buffer)
        return;
    
    m_bufferUavBarriers[buffer] = enableBarriers;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

