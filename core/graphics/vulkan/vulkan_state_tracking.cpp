// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resource state transitions and barriers


namespace __hidden_vulkan_state_tracking{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkImageMemoryBarrier2 BuildTextureStateBarrier(
    const VkImage image,
    const Format::Enum format,
    const TextureSubresourceSet& subresources,
    const ResourceStates::Mask oldState,
    const ResourceStates::Mask stateBits
){
    auto barrier = VulkanDetail::MakeVkStruct<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(stateBits);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(stateBits);
    barrier.oldLayout = oldState != ResourceStates::Unknown ? VulkanDetail::GetVkImageLayout(oldState) : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VulkanDetail::GetVkImageLayout(stateBits);
    barrier.image = image;

    const FormatInfo& formatInfo = GetFormatInfo(format);
    barrier.subresourceRange.aspectMask = VulkanDetail::GetImageAspectMask(formatInfo);
    barrier.subresourceRange.baseMipLevel = subresources.baseMipLevel;
    barrier.subresourceRange.levelCount = subresources.numMipLevels;
    barrier.subresourceRange.baseArrayLayer = subresources.baseArraySlice;
    barrier.subresourceRange.layerCount = subresources.numArraySlices;

    return barrier;
}

template<typename ContainerT>
void ReserveAdditionalCapacity(ContainerT& container, usize additionalCount){
    if(additionalCount <= 1u)
        return;

    const usize currentSize = container.size();
    if(additionalCount > Limit<usize>::s_Max - currentSize)
        return;

    const usize requiredCapacity = currentSize + additionalCount;
    if constexpr(requires{ container.capacity(); }){
        if(requiredCapacity <= container.capacity())
            return;

        usize nextCapacity = Max<usize>(container.capacity(), 1u);
        while(nextCapacity < requiredCapacity){
            if(nextCapacity > Limit<usize>::s_Max / 2u){
                nextCapacity = requiredCapacity;
                break;
            }
            nextCapacity *= 2u;
        }
        container.reserve(nextCapacity);
    }
    else
        container.reserve(requiredCapacity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

void CommandList::setResourceStatesForBindingSets(const BindingSetVector& bindings){
    for(IBindingSet* bindingSet : bindings)
        setResourceStatesForBindingSet(bindingSet);
}

void CommandList::setResourceStatesForGraphicsBuffers(const GraphicsState& state){
    for(const VertexBufferBinding& binding : state.vertexBuffers){
        if(binding.buffer)
            setBufferState(binding.buffer, ResourceStates::VertexBuffer);
    }

    if(state.indexBuffer.buffer)
        setBufferState(state.indexBuffer.buffer, ResourceStates::IndexBuffer);

    if(state.indirectParams)
        setBufferState(state.indirectParams, ResourceStates::IndirectArgument);
}

void CommandList::executePipelineBarrier(const VkDependencyInfo& depInfo){
    IFramebuffer* resumeFramebuffer = nullptr;
    if(m_renderPassActive){
        resumeFramebuffer = m_renderPassFramebuffer;
        endDynamicRendering();
        m_renderPassActive = false;
        m_renderPassFramebuffer = nullptr;
    }

    vkCmdPipelineBarrier2(m_currentCmdBuf->m_cmdBuf, &depInfo);

    if(resumeFramebuffer){
        RenderPassParameters params = {};
        if(beginDynamicRendering(resumeFramebuffer, params)){
            m_renderPassActive = true;
            m_renderPassFramebuffer = resumeFramebuffer;
        }
    }
}

void CommandList::commitBarriers(){
    if(m_pendingImageBarriers.empty() && m_pendingBufferBarriers.empty())
        return;

    auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.imageMemoryBarrierCount = static_cast<u32>(m_pendingImageBarriers.size());
    depInfo.pImageMemoryBarriers = m_pendingImageBarriers.data();
    depInfo.bufferMemoryBarrierCount = static_cast<u32>(m_pendingBufferBarriers.size());
    depInfo.pBufferMemoryBarriers = m_pendingBufferBarriers.data();

    executePipelineBarrier(depInfo);

    m_pendingImageBarriers.clear();
    m_pendingBufferBarriers.clear();
}

void CommandList::setTextureState(ITexture* textureResource, TextureSubresourceSet subresources, ResourceStates::Mask stateBits){
    if(!textureResource)
        return;

    if(m_stateTracker->isPermanentTexture(textureResource))
        return;

    auto* texture = checked_cast<Texture*>(textureResource);
    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture->m_desc, TextureSubresourceMipResolve::Range);
    if(resolvedSubresources.numMipLevels == 0 || resolvedSubresources.numArraySlices == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to set texture state: invalid subresource range"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to set texture state: invalid subresource range"));
        return;
    }

    ResourceStates::Mask oldState = ResourceStates::Unknown;
    bool firstSubresource = true;
    bool uniformOldState = true;
    bool needsBarrier = false;
    const bool uavBarrierEnabled = stateBits == ResourceStates::UnorderedAccess && m_stateTracker->isUavBarrierEnabledForTexture(textureResource);
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
    const usize subresourceCount = static_cast<usize>(resolvedSubresources.numMipLevels) * static_cast<usize>(resolvedSubresources.numArraySlices);

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            ResourceStates::Mask subresourceOldState = ResourceStates::Unknown;
            if(!m_stateTracker->getTransientTextureState(textureResource, arraySlice, mipLevel, subresourceOldState))
                return;

            if(firstSubresource){
                oldState = subresourceOldState;
                firstSubresource = false;
            }
            else if(subresourceOldState != oldState)
                uniformOldState = false;

            if(subresourceOldState != stateBits || (subresourceOldState == ResourceStates::UnorderedAccess && uavBarrierEnabled))
                needsBarrier = true;
        }
    }

    if(!needsBarrier)
        return;

    if(uniformOldState){
        const VkImageMemoryBarrier2 barrier = __hidden_vulkan_state_tracking::BuildTextureStateBarrier(
            texture->m_image,
            texture->m_desc.format,
            resolvedSubresources,
            oldState,
            stateBits
        );

        m_stateTracker->beginTrackingTransientTexture(textureResource, resolvedSubresources, stateBits);

        if(!m_enableAutomaticBarriers){
            m_pendingImageBarriers.push_back(barrier);
            return;
        }

        auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        executePipelineBarrier(depInfo);
        return;
    }

    const usize firstBarrierIndex = m_pendingImageBarriers.size();
    __hidden_vulkan_state_tracking::ReserveAdditionalCapacity(m_pendingImageBarriers, subresourceCount);
    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            ResourceStates::Mask subresourceOldState = ResourceStates::Unknown;
            if(!m_stateTracker->getTransientTextureState(textureResource, arraySlice, mipLevel, subresourceOldState))
                return;
            if(subresourceOldState == stateBits && (subresourceOldState != ResourceStates::UnorderedAccess || !uavBarrierEnabled))
                continue;

            m_pendingImageBarriers.push_back(__hidden_vulkan_state_tracking::BuildTextureStateBarrier(
                texture->m_image,
                texture->m_desc.format,
                TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u),
                subresourceOldState,
                stateBits
            ));
        }
    }

    m_stateTracker->beginTrackingTransientTexture(textureResource, resolvedSubresources, stateBits);

    if(!m_enableAutomaticBarriers)
        return;

    const usize newBarrierCount = m_pendingImageBarriers.size() - firstBarrierIndex;
    if(newBarrierCount == 0)
        return;

    auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.imageMemoryBarrierCount = static_cast<u32>(newBarrierCount);
    depInfo.pImageMemoryBarriers = m_pendingImageBarriers.data() + firstBarrierIndex;

    executePipelineBarrier(depInfo);
    m_pendingImageBarriers.resize(firstBarrierIndex);
}

void CommandList::setBufferState(IBuffer* bufferResource, ResourceStates::Mask stateBits){
    if(!bufferResource)
        return;

    if(m_stateTracker->isPermanentBuffer(bufferResource))
        return;

    ResourceStates::Mask oldState = ResourceStates::Unknown;
    if(!m_stateTracker->getTransientBufferState(bufferResource, oldState))
        return;

    auto* buffer = checked_cast<Buffer*>(bufferResource);
    const bool needsUavBarrier =
        oldState == ResourceStates::UnorderedAccess
        && stateBits == ResourceStates::UnorderedAccess
        && m_stateTracker->isUavBarrierEnabledForBuffer(bufferResource)
    ;

    if(oldState == stateBits && !needsUavBarrier)
        return;

    auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(stateBits);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(stateBits);
    barrier.buffer = buffer->m_buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    m_stateTracker->beginTrackingTransientBuffer(bufferResource, stateBits);

    if(!m_enableAutomaticBarriers){
        m_pendingBufferBarriers.push_back(barrier);
        return;
    }

    auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.bufferMemoryBarrierCount = 1;
    depInfo.pBufferMemoryBarriers = &barrier;

    executePipelineBarrier(depInfo);
}

void CommandList::setAccelStructState(IRayTracingAccelStruct* accelStructResource, ResourceStates::Mask stateBits){
    if(!accelStructResource)
        return;

    auto* as = checked_cast<AccelStruct*>(accelStructResource);
    if(as->m_buffer){
        setBufferState(as->m_buffer.get(), stateBits);
    }
}

void CommandList::setPermanentTextureState(ITexture* texture, ResourceStates::Mask stateBits){
    setTextureState(texture, s_AllSubresources, stateBits);
    m_stateTracker->setPermanentTextureState(texture, stateBits);
}

void CommandList::setPermanentBufferState(IBuffer* buffer, ResourceStates::Mask stateBits){
    setBufferState(buffer, stateBits);
    m_stateTracker->setPermanentBufferState(buffer, stateBits);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StateTracker::StateTracker(const VulkanContext& context)
    : m_permanentTextureStates(0, Hasher<ITexture*>(), EqualTo<ITexture*>(), Alloc::CustomAllocator<Pair<const ITexture*, ResourceStates::Mask>>(context.objectArena))
    , m_permanentBufferStates(0, Hasher<IBuffer*>(), EqualTo<IBuffer*>(), Alloc::CustomAllocator<Pair<const IBuffer*, ResourceStates::Mask>>(context.objectArena))
    , m_textureStates(0, TextureSubresourceStateKeyHasher(), TextureSubresourceStateKeyEqualTo(), Alloc::CustomAllocator<Pair<const TextureSubresourceStateKey, ResourceStates::Mask>>(context.objectArena))
    , m_bufferStates(0, Hasher<IBuffer*>(), EqualTo<IBuffer*>(), Alloc::CustomAllocator<Pair<const IBuffer*, ResourceStates::Mask>>(context.objectArena))
    , m_textureUavBarriers(0, Hasher<ITexture*>(), EqualTo<ITexture*>(), Alloc::CustomAllocator<Pair<const ITexture*, bool>>(context.objectArena))
    , m_bufferUavBarriers(0, Hasher<IBuffer*>(), EqualTo<IBuffer*>(), Alloc::CustomAllocator<Pair<const IBuffer*, bool>>(context.objectArena))
    , m_context(context)
{}
StateTracker::~StateTracker(){}

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

    m_permanentTextureStates.insert_or_assign(texture, state);
}

void StateTracker::setPermanentBufferState(IBuffer* buffer, ResourceStates::Mask state){
    if(!buffer)
        return;

    m_permanentBufferStates.insert_or_assign(buffer, state);
}

bool StateTracker::isPermanentTexture(ITexture* texture)const{
    if(!texture)
        return false;

    return m_permanentTextureStates.find(texture) != m_permanentTextureStates.end();
}

bool StateTracker::isPermanentBuffer(IBuffer* buffer)const{
    if(!buffer)
        return false;

    return m_permanentBufferStates.find(buffer) != m_permanentBufferStates.end();
}

ResourceStates::Mask StateTracker::getTextureState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel)const{
    if(!texture)
        return ResourceStates::Unknown;

    auto permIt = m_permanentTextureStates.find(texture);
    if(permIt != m_permanentTextureStates.end())
        return permIt.value();

    ResourceStates::Mask state = ResourceStates::Unknown;
    return getTransientTextureState(texture, arraySlice, mipLevel, state) ? state : ResourceStates::Unknown;
}

ResourceStates::Mask StateTracker::getBufferState(IBuffer* buffer)const{
    if(!buffer)
        return ResourceStates::Unknown;

    auto permIt = m_permanentBufferStates.find(buffer);
    if(permIt != m_permanentBufferStates.end())
        return permIt.value();

    ResourceStates::Mask state = ResourceStates::Unknown;
    return getTransientBufferState(buffer, state) ? state : ResourceStates::Unknown;
}

bool StateTracker::getTransientTextureState(ITexture* texture, ArraySlice arraySlice, MipLevel mipLevel, ResourceStates::Mask& outState)const{
    outState = ResourceStates::Unknown;
    if(!texture)
        return false;

    const TextureDesc& desc = texture->getDescription();
    if(mipLevel >= desc.mipLevels || arraySlice >= desc.arraySize)
        return false;

    const TextureSubresourceStateKey key{ texture, mipLevel, arraySlice };
    auto it = m_textureStates.find(key);
    if(it != m_textureStates.end()){
        outState = it.value();
        return true;
    }

    if(desc.keepInitialState && checked_cast<Texture*>(texture)->m_keepInitialStateKnown)
        outState = desc.initialState;

    return true;
}

bool StateTracker::getTransientBufferState(IBuffer* buffer, ResourceStates::Mask& outState)const{
    outState = ResourceStates::Unknown;
    if(!buffer)
        return false;

    auto it = m_bufferStates.find(buffer);
    if(it != m_bufferStates.end()){
        outState = it.value();
        return true;
    }

    const BufferDesc& desc = buffer->getDescription();
    if(desc.keepInitialState)
        outState = desc.initialState;

    return true;
}

void StateTracker::beginTrackingTexture(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask state){
    if(!texture)
        return;

    if(m_permanentTextureStates.find(texture) != m_permanentTextureStates.end())
        return;

    beginTrackingTransientTexture(texture, subresources, state);
}

void StateTracker::beginTrackingBuffer(IBuffer* buffer, ResourceStates::Mask state){
    if(!buffer)
        return;

    if(m_permanentBufferStates.find(buffer) != m_permanentBufferStates.end())
        return;

    beginTrackingTransientBuffer(buffer, state);
}

void StateTracker::appendKeepInitialStateBarriers(
    Vector<VkImageMemoryBarrier2, Alloc::CustomAllocator<VkImageMemoryBarrier2>>& imageBarriers,
    Vector<VkBufferMemoryBarrier2, Alloc::CustomAllocator<VkBufferMemoryBarrier2>>& bufferBarriers
){
    for(auto it = m_textureStates.begin(); it != m_textureStates.end(); ++it){
        const TextureSubresourceStateKey& key = it->first;
        if(!key.texture)
            continue;

        const TextureDesc& desc = key.texture->getDescription();
        const ResourceStates::Mask currentState = it.value();
        if(!desc.keepInitialState)
            continue;

        auto* texture = checked_cast<Texture*>(key.texture);
        if(currentState == desc.initialState){
            texture->m_keepInitialStateKnown = true;
            continue;
        }

        imageBarriers.push_back(__hidden_vulkan_state_tracking::BuildTextureStateBarrier(
            texture->m_image,
            texture->m_desc.format,
            TextureSubresourceSet(key.mipLevel, 1u, key.arraySlice, 1u),
            currentState,
            desc.initialState
        ));
        it.value() = desc.initialState;
        texture->m_keepInitialStateKnown = true;
    }

    for(auto it = m_bufferStates.begin(); it != m_bufferStates.end(); ++it){
        IBuffer* bufferResource = it->first;
        if(!bufferResource)
            continue;

        const BufferDesc& desc = bufferResource->getDescription();
        const ResourceStates::Mask currentState = it.value();
        if(!desc.keepInitialState || currentState == desc.initialState)
            continue;

        auto* buffer = checked_cast<Buffer*>(bufferResource);
        auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
        barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(currentState != ResourceStates::Unknown ? currentState : ResourceStates::Common);
        barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(currentState != ResourceStates::Unknown ? currentState : ResourceStates::Common);
        barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(desc.initialState);
        barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(desc.initialState);
        barrier.buffer = buffer->m_buffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        bufferBarriers.push_back(barrier);
        it.value() = desc.initialState;
    }
}

bool StateTracker::isUavBarrierEnabledForTexture(ITexture* texture)const{
    if(!texture)
        return false;

    const auto found = m_textureUavBarriers.find(texture);
    return found == m_textureUavBarriers.end() || found.value();
}

bool StateTracker::isUavBarrierEnabledForBuffer(IBuffer* buffer)const{
    if(!buffer)
        return false;

    const auto found = m_bufferUavBarriers.find(buffer);
    return found == m_bufferUavBarriers.end() || found.value();
}

void StateTracker::beginTrackingTransientTexture(ITexture* texture, TextureSubresourceSet subresources, ResourceStates::Mask state){
    if(!texture)
        return;

    const TextureDesc& desc = texture->getDescription();
    const TextureSubresourceSet resolvedSubresources = subresources.resolve(desc, TextureSubresourceMipResolve::Range);
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
    const usize subresourceCount = static_cast<usize>(resolvedSubresources.numMipLevels) * static_cast<usize>(resolvedSubresources.numArraySlices);

    __hidden_vulkan_state_tracking::ReserveAdditionalCapacity(m_textureStates, subresourceCount);

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const TextureSubresourceStateKey key{ texture, mipLevel, arraySlice };
            m_textureStates.insert_or_assign(key, state);
        }
    }
}

void StateTracker::beginTrackingTransientBuffer(IBuffer* buffer, ResourceStates::Mask state){
    m_bufferStates.insert_or_assign(buffer, state);
}

void StateTracker::setEnableUavBarriersForTexture(ITexture* texture, bool enableBarriers){
    if(!texture)
        return;

    m_textureUavBarriers.insert_or_assign(texture, enableBarriers);
}

void StateTracker::setEnableUavBarriersForBuffer(IBuffer* buffer, bool enableBarriers){
    if(!buffer)
        return;

    m_bufferUavBarriers.insert_or_assign(buffer, enableBarriers);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

