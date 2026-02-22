// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resource state transitions and barriers


void CommandList::setResourceStatesForBindingSet(IBindingSet* bindingSet){
    if(!bindingSet || !m_enableAutomaticBarriers)
        return;

    auto* bs = checked_cast<BindingSet*>(bindingSet);
    for(const auto& item : bs->m_desc.bindings){
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
    m_enableAutomaticBarriers = enable;
}

void CommandList::commitBarriers(){
    if(m_pendingImageBarriers.empty() && m_pendingBufferBarriers.empty())
        return;

    VkDependencyInfo depInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    depInfo.imageMemoryBarrierCount = static_cast<u32>(m_pendingImageBarriers.size());
    depInfo.pImageMemoryBarriers = m_pendingImageBarriers.data();
    depInfo.bufferMemoryBarrierCount = static_cast<u32>(m_pendingBufferBarriers.size());
    depInfo.pBufferMemoryBarriers = m_pendingBufferBarriers.data();

    vkCmdPipelineBarrier2(m_currentCmdBuf->m_cmdBuf, &depInfo);

    m_pendingImageBarriers.clear();
    m_pendingBufferBarriers.clear();
}

void CommandList::setTextureState(ITexture* _texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits){
    if(!_texture)
        return;

    if(m_stateTracker->isPermanentTexture(_texture))
        return;

    auto* texture = checked_cast<Texture*>(_texture);

    ResourceStates::Mask oldState = m_stateTracker->getTextureState(_texture, subresources.baseArraySlice, subresources.baseMipLevel);
    if(oldState == stateBits)
        return;

    VkImageMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    barrier.srcStageMask = __hidden_vulkan::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.srcAccessMask = __hidden_vulkan::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = __hidden_vulkan::GetVkPipelineStageFlags(stateBits);
    barrier.dstAccessMask = __hidden_vulkan::GetVkAccessFlags(stateBits);
    barrier.oldLayout = oldState != ResourceStates::Unknown ? __hidden_vulkan::GetVkImageLayout(oldState) : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = __hidden_vulkan::GetVkImageLayout(stateBits);
    barrier.image = texture->m_image;

    const FormatInfo& formatInfo = GetFormatInfo(texture->m_desc.format);
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if(formatInfo.hasDepth) aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if(formatInfo.hasStencil) aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = subresources.baseMipLevel;
    barrier.subresourceRange.levelCount = subresources.numMipLevels;
    barrier.subresourceRange.baseArrayLayer = subresources.baseArraySlice;
    barrier.subresourceRange.layerCount = subresources.numArraySlices;

    m_stateTracker->beginTrackingTexture(_texture, subresources, stateBits);

    if(!m_enableAutomaticBarriers){
        m_pendingImageBarriers.push_back(barrier);
        return;
    }

    VkDependencyInfo depInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(m_currentCmdBuf->m_cmdBuf, &depInfo);
}

void CommandList::setBufferState(IBuffer* _buffer, ResourceStates::Mask stateBits){
    if(!_buffer)
        return;

    if(m_stateTracker->isPermanentBuffer(_buffer))
        return;

    auto* buffer = checked_cast<Buffer*>(_buffer);

    ResourceStates::Mask oldState = m_stateTracker->getBufferState(_buffer);
    if(oldState == stateBits)
        return;

    VkBufferMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
    barrier.srcStageMask = __hidden_vulkan::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.srcAccessMask = __hidden_vulkan::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = __hidden_vulkan::GetVkPipelineStageFlags(stateBits);
    barrier.dstAccessMask = __hidden_vulkan::GetVkAccessFlags(stateBits);
    barrier.buffer = buffer->m_buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    m_stateTracker->beginTrackingBuffer(_buffer, stateBits);

    if(!m_enableAutomaticBarriers){
        m_pendingBufferBarriers.push_back(barrier);
        return;
    }

    VkDependencyInfo depInfo = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    depInfo.bufferMemoryBarrierCount = 1;
    depInfo.pBufferMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(m_currentCmdBuf->m_cmdBuf, &depInfo);
}

void CommandList::setAccelStructState(IRayTracingAccelStruct* _as, ResourceStates::Mask stateBits){
    if(!_as)
        return;

    auto* as = checked_cast<AccelStruct*>(_as);

    if(as->m_buffer){
        setBufferState(as->m_buffer.get(), stateBits);
    }
}

void CommandList::setPermanentTextureState(ITexture* texture, ResourceStates::Mask stateBits){
    m_stateTracker->setPermanentTextureState(texture, stateBits);
}

void CommandList::setPermanentBufferState(IBuffer* buffer, ResourceStates::Mask stateBits){
    m_stateTracker->setPermanentBufferState(buffer, stateBits);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StateTracker::StateTracker(const VulkanContext& context)
    : m_context(context)
    , m_permanentTextureStates(0, Hasher<ITexture*>(), EqualTo<ITexture*>(), Alloc::CustomAllocator<Pair<const ITexture*, ResourceStates::Mask>>(*context.objectArena))
    , m_permanentBufferStates(0, Hasher<IBuffer*>(), EqualTo<IBuffer*>(), Alloc::CustomAllocator<Pair<const IBuffer*, ResourceStates::Mask>>(*context.objectArena))
    , m_textureStates(0, Hasher<ITexture*>(), EqualTo<ITexture*>(), Alloc::CustomAllocator<Pair<const ITexture*, ResourceStates::Mask>>(*context.objectArena))
    , m_bufferStates(0, Hasher<IBuffer*>(), EqualTo<IBuffer*>(), Alloc::CustomAllocator<Pair<const IBuffer*, ResourceStates::Mask>>(*context.objectArena))
    , m_textureUavBarriers(0, Hasher<ITexture*>(), EqualTo<ITexture*>(), Alloc::CustomAllocator<Pair<const ITexture*, bool>>(*context.objectArena))
    , m_bufferUavBarriers(0, Hasher<IBuffer*>(), EqualTo<IBuffer*>(), Alloc::CustomAllocator<Pair<const IBuffer*, bool>>(*context.objectArena))
{
}
StateTracker::~StateTracker(){
}

void StateTracker::reset(){
    m_graphicsState = {};
    m_computeState = {};
    m_meshletState = {};
    m_rayTracingState = {};

    m_textureStates.clear();
    m_bufferStates.clear();
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

    auto permIt = m_permanentTextureStates.find(texture);
    if(permIt != m_permanentTextureStates.end())
        return permIt->second;

    auto it = m_textureStates.find(texture);
    if(it != m_textureStates.end())
        return it->second;

    return ResourceStates::Unknown;
}

ResourceStates::Mask StateTracker::getBufferState(IBuffer* buffer)const{
    if(!buffer)
        return ResourceStates::Unknown;

    auto permIt = m_permanentBufferStates.find(buffer);
    if(permIt != m_permanentBufferStates.end())
        return permIt->second;

    auto it = m_bufferStates.find(buffer);
    if(it != m_bufferStates.end())
        return it->second;

    return ResourceStates::Unknown;
}

void StateTracker::beginTrackingTexture(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask state){
    (void)subresources;

    if(!texture)
        return;

    if(m_permanentTextureStates.count(texture) > 0)
        return;

    m_textureStates[texture] = state;
}

void StateTracker::beginTrackingBuffer(IBuffer* buffer, ResourceStates::Mask state){
    if(!buffer)
        return;

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

