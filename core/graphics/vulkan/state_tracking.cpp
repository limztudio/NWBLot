// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resource state transitions and barriers


namespace __hidden_vulkan_state_tracking{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkImageMemoryBarrier2 BuildTextureStateBarrier(
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    const ResourceStates::Mask oldState,
    const ResourceStates::Mask stateBits,
    const bool rayTracingStageAvailable
){
    auto barrier = VulkanDetail::MakeVkStruct<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common, rayTracingStageAvailable);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(stateBits, rayTracingStageAvailable);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(stateBits);
    barrier.oldLayout = oldState != ResourceStates::Unknown ? VulkanDetail::GetVkImageLayout(oldState) : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VulkanDetail::GetVkImageLayout(stateBits);
    barrier.image = image;

    barrier.subresourceRange = VulkanDetail::BuildImageSubresourceRange(subresources, aspectMask);

    return barrier;
}

bool NeedsTextureStateBarrier(const ResourceStates::Mask oldState, const ResourceStates::Mask stateBits, const bool uavBarrierEnabled){
    return oldState != stateBits || (oldState == ResourceStates::UnorderedAccess && uavBarrierEnabled);
}

void AppendTextureStateBarrier(
    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& barriers,
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const ArraySlice arraySlice,
    const MipLevel mipLevel,
    const ResourceStates::Mask oldState,
    const ResourceStates::Mask stateBits,
    const bool rayTracingStageAvailable
){
    barriers.push_back(BuildTextureStateBarrier(
        image,
        aspectMask,
        TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u),
        oldState,
        stateBits,
        rayTracingStageAvailable
    ));
}

void AppendTextureStateBarriersBefore(
    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& barriers,
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    const MipLevel mipEnd,
    const ArraySlice currentArraySlice,
    const MipLevel currentMipLevel,
    const ResourceStates::Mask oldState,
    const ResourceStates::Mask stateBits,
    const bool rayTracingStageAvailable
){
    for(ArraySlice arraySlice = subresources.baseArraySlice; arraySlice <= currentArraySlice; ++arraySlice){
        const MipLevel previousMipEnd = arraySlice == currentArraySlice ? currentMipLevel : mipEnd;
        for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < previousMipEnd; ++mipLevel)
            AppendTextureStateBarrier(barriers, image, aspectMask, arraySlice, mipLevel, oldState, stateBits, rayTracingStageAvailable);
    }
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


void CommandList::setResourceStatesForBindingSet(BindingSet* bindingSet){
    if(!bindingSet || !m_enableAutomaticBarriers)
        return;

    auto* bs = bindingSet;
    for(const auto& item : bs->m_desc.bindings){
        switch(item.type){
        case ResourceType::Texture_SRV:
            if(item.resourceHandle)
                setTextureState(static_cast<Texture*>(item.resourceHandle), item.subresources, ResourceStates::ShaderResource);
            break;
        case ResourceType::Texture_UAV:
            if(item.resourceHandle)
                setTextureState(static_cast<Texture*>(item.resourceHandle), item.subresources, ResourceStates::UnorderedAccess);
            break;
        case ResourceType::StructuredBuffer_SRV:
        case ResourceType::RawBuffer_SRV:
        case ResourceType::TypedBuffer_SRV:
            if(item.resourceHandle)
                setBufferState(static_cast<Buffer*>(item.resourceHandle), ResourceStates::ShaderResource);
            break;
        case ResourceType::StructuredBuffer_UAV:
        case ResourceType::RawBuffer_UAV:
        case ResourceType::TypedBuffer_UAV:
            if(item.resourceHandle)
                setBufferState(static_cast<Buffer*>(item.resourceHandle), ResourceStates::UnorderedAccess);
            break;
        case ResourceType::ConstantBuffer:
        case ResourceType::VolatileConstantBuffer:
            if(item.resourceHandle)
                setBufferState(static_cast<Buffer*>(item.resourceHandle), ResourceStates::ConstantBuffer);
            break;
        case ResourceType::RayTracingAccelStruct:
            if(item.resourceHandle)
                setAccelStructState(static_cast<RayTracingAccelStruct*>(item.resourceHandle), ResourceStates::AccelStructRead);
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

void CommandList::setResourceStatesForFramebuffer(Framebuffer& framebuffer){
    const FramebufferDesc& desc = framebuffer.getDescription();

    for(const auto& attachment : desc.colorAttachments)
        setTextureState(attachment.texture, attachment.subresources, ResourceStates::RenderTarget);

    if(desc.depthAttachment.valid())
        setTextureState(desc.depthAttachment.texture, desc.depthAttachment.subresources, desc.depthAttachment.isReadOnly ? ResourceStates::DepthRead : ResourceStates::DepthWrite);

    if(desc.shadingRateAttachment.valid())
        setTextureState(desc.shadingRateAttachment.texture, desc.shadingRateAttachment.subresources, ResourceStates::ShadingRateSurface);
}

void CommandList::setResourceStatesForBindingSets(const BindingSetVector& bindings){
    for(BindingSet* bindingSet : bindings)
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
    Framebuffer* resumeFramebuffer = nullptr;
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

void CommandList::setTextureState(Texture* textureResource, TextureSubresourceSet subresources, ResourceStates::Mask stateBits){
    if(!textureResource)
        return;

    Texture& texture = *textureResource;
    if(m_stateTracker.isPermanentTexture(texture))
        return;

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture.m_desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::DebugValidateTextureSubresourceRange(resolvedSubresources, NWB_TEXT("set texture state")))
        return;

    ResourceStates::Mask oldState = ResourceStates::Unknown;
    bool firstSubresource = true;
    bool needsBarrier = false;
    bool usePerSubresourceBarriers = false;
    const bool uavBarrierEnabled = stateBits == ResourceStates::UnorderedAccess && m_stateTracker.isUavBarrierEnabledForTexture(texture);
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
    const usize subresourceCount = static_cast<usize>(resolvedSubresources.numMipLevels) * static_cast<usize>(resolvedSubresources.numArraySlices);
    usize firstBarrierIndex = m_pendingImageBarriers.size();

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            ResourceStates::Mask subresourceOldState = ResourceStates::Unknown;
            if(!m_stateTracker.getResolvedTransientTextureState(texture, arraySlice, mipLevel, subresourceOldState))
                return;

            if(firstSubresource){
                oldState = subresourceOldState;
                firstSubresource = false;
            }
            else if(subresourceOldState != oldState && !usePerSubresourceBarriers){
                usePerSubresourceBarriers = true;
                firstBarrierIndex = m_pendingImageBarriers.size();
                __hidden_vulkan_state_tracking::ReserveAdditionalCapacity(m_pendingImageBarriers, subresourceCount);
                if(__hidden_vulkan_state_tracking::NeedsTextureStateBarrier(oldState, stateBits, uavBarrierEnabled)){
                    __hidden_vulkan_state_tracking::AppendTextureStateBarriersBefore(
                        m_pendingImageBarriers,
                        texture.m_image,
                        texture.m_aspectMask,
                        resolvedSubresources,
                        mipEnd,
                        arraySlice,
                        mipLevel,
                        oldState,
                        stateBits,
                        m_context.extensions.KHR_ray_tracing_pipeline
                    );
                }
            }

            const bool subresourceNeedsBarrier = __hidden_vulkan_state_tracking::NeedsTextureStateBarrier(
                subresourceOldState,
                stateBits,
                uavBarrierEnabled
            );
            if(subresourceNeedsBarrier){
                needsBarrier = true;
                if(usePerSubresourceBarriers){
                    __hidden_vulkan_state_tracking::AppendTextureStateBarrier(
                        m_pendingImageBarriers,
                        texture.m_image,
                        texture.m_aspectMask,
                        arraySlice,
                        mipLevel,
                        subresourceOldState,
                        stateBits,
                        m_context.extensions.KHR_ray_tracing_pipeline
                    );
                }
            }
        }
    }

    if(!needsBarrier)
        return;

    if(!usePerSubresourceBarriers){
        const VkImageMemoryBarrier2 barrier = __hidden_vulkan_state_tracking::BuildTextureStateBarrier(
            texture.m_image,
            texture.m_aspectMask,
            resolvedSubresources,
            oldState,
            stateBits,
            m_context.extensions.KHR_ray_tracing_pipeline
        );

        m_stateTracker.beginTrackingResolvedTransientTexture(texture, resolvedSubresources, stateBits);

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

    m_stateTracker.beginTrackingResolvedTransientTexture(texture, resolvedSubresources, stateBits);

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

void CommandList::setBufferState(Buffer* bufferResource, ResourceStates::Mask stateBits){
    if(!bufferResource)
        return;

    Buffer& buffer = *bufferResource;
    if(m_stateTracker.isPermanentBuffer(buffer))
        return;

    ResourceStates::Mask oldState = ResourceStates::Unknown;
    if(!m_stateTracker.getTransientBufferState(buffer, oldState))
        return;

    const bool needsUavBarrier =
        oldState == ResourceStates::UnorderedAccess
        && stateBits == ResourceStates::UnorderedAccess
        && m_stateTracker.isUavBarrierEnabledForBuffer(buffer)
    ;

    if(oldState == stateBits && !needsUavBarrier)
        return;

    auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common, m_context.extensions.KHR_ray_tracing_pipeline);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(stateBits, m_context.extensions.KHR_ray_tracing_pipeline);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(stateBits);
    barrier.buffer = buffer.m_buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    m_stateTracker.beginTrackingTransientBuffer(buffer, stateBits);

    if(!m_enableAutomaticBarriers){
        m_pendingBufferBarriers.push_back(barrier);
        return;
    }

    auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.bufferMemoryBarrierCount = 1;
    depInfo.pBufferMemoryBarriers = &barrier;

    executePipelineBarrier(depInfo);
}

void CommandList::setAccelStructState(RayTracingAccelStruct* accelStructResource, ResourceStates::Mask stateBits){
    if(!accelStructResource)
        return;

    auto* as = accelStructResource;
    if(as->m_buffer)
        setBufferState(as->m_buffer.get(), stateBits);
}

void CommandList::setPermanentTextureState(Texture* texture, ResourceStates::Mask stateBits){
    if(!texture)
        return;

    setTextureState(texture, s_AllSubresources, stateBits);
    m_stateTracker.setPermanentTextureState(*texture, stateBits);
}

void CommandList::setPermanentBufferState(Buffer* buffer, ResourceStates::Mask stateBits){
    if(!buffer)
        return;

    setBufferState(buffer, stateBits);
    m_stateTracker.setPermanentBufferState(*buffer, stateBits);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StateTracker::StateTracker(const VulkanContext& context)
    : m_permanentTextureStates(0, Hasher<Texture*>(), EqualTo<Texture*>(), context.objectArena)
    , m_permanentBufferStates(0, Hasher<Buffer*>(), EqualTo<Buffer*>(), context.objectArena)
    , m_textureStates(0, TextureSubresourceStateKeyHasher(), TextureSubresourceStateKeyEqualTo(), context.objectArena)
    , m_bufferStates(0, Hasher<Buffer*>(), EqualTo<Buffer*>(), context.objectArena)
    , m_textureUavBarriers(0, Hasher<Texture*>(), EqualTo<Texture*>(), context.objectArena)
    , m_bufferUavBarriers(0, Hasher<Buffer*>(), EqualTo<Buffer*>(), context.objectArena)
    , m_context(context)
{}
StateTracker::~StateTracker(){}

void StateTracker::reset(){
    m_textureStates.clear();
    m_bufferStates.clear();
}

void StateTracker::setPermanentTextureState(Texture& texture, ResourceStates::Mask state){
    m_permanentTextureStates.insert_or_assign(&texture, state);
}

void StateTracker::setPermanentBufferState(Buffer& buffer, ResourceStates::Mask state){
    m_permanentBufferStates.insert_or_assign(&buffer, state);
}

bool StateTracker::isPermanentTexture(Texture& texture)const{
    return m_permanentTextureStates.find(&texture) != m_permanentTextureStates.end();
}

bool StateTracker::isPermanentBuffer(Buffer& buffer)const{
    return m_permanentBufferStates.find(&buffer) != m_permanentBufferStates.end();
}

ResourceStates::Mask StateTracker::getTextureState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel)const{
    if(!texture)
        return ResourceStates::Unknown;

    auto permIt = m_permanentTextureStates.find(texture);
    if(permIt != m_permanentTextureStates.end())
        return permIt.value();

    ResourceStates::Mask state = ResourceStates::Unknown;
    return getTransientTextureState(*texture, arraySlice, mipLevel, state) ? state : ResourceStates::Unknown;
}

ResourceStates::Mask StateTracker::getBufferState(Buffer* buffer)const{
    if(!buffer)
        return ResourceStates::Unknown;

    auto permIt = m_permanentBufferStates.find(buffer);
    if(permIt != m_permanentBufferStates.end())
        return permIt.value();

    ResourceStates::Mask state = ResourceStates::Unknown;
    return getTransientBufferState(*buffer, state) ? state : ResourceStates::Unknown;
}

bool StateTracker::getTransientTextureState(Texture& texture, ArraySlice arraySlice, MipLevel mipLevel, ResourceStates::Mask& outState)const{
    outState = ResourceStates::Unknown;

    const TextureDesc& desc = texture.getDescription();
    if(mipLevel >= desc.mipLevels || arraySlice >= desc.arraySize)
        return false;

    return getResolvedTransientTextureState(texture, arraySlice, mipLevel, outState);
}

bool StateTracker::getResolvedTransientTextureState(Texture& texture, ArraySlice arraySlice, MipLevel mipLevel, ResourceStates::Mask& outState)const{
    outState = ResourceStates::Unknown;

    const TextureSubresourceStateKey key{ &texture, mipLevel, arraySlice };
    auto it = m_textureStates.find(key);
    if(it != m_textureStates.end()){
        outState = it.value();
        return true;
    }

    if(texture.m_desc.keepInitialState && texture.m_keepInitialStateKnown)
        outState = texture.m_desc.initialState;

    return true;
}

bool StateTracker::getTransientBufferState(Buffer& buffer, ResourceStates::Mask& outState)const{
    outState = ResourceStates::Unknown;

    auto it = m_bufferStates.find(&buffer);
    if(it != m_bufferStates.end()){
        outState = it.value();
        return true;
    }

    const BufferDesc& desc = buffer.getDescription();
    if(desc.keepInitialState)
        outState = desc.initialState;

    return true;
}

void StateTracker::beginTrackingTexture(Texture* texture, TextureSubresourceSet subresources, ResourceStates::Mask state){
    if(!texture)
        return;

    if(m_permanentTextureStates.find(texture) != m_permanentTextureStates.end())
        return;

    beginTrackingTransientTexture(*texture, subresources, state);
}

void StateTracker::beginTrackingBuffer(Buffer* buffer, ResourceStates::Mask state){
    if(!buffer)
        return;

    if(m_permanentBufferStates.find(buffer) != m_permanentBufferStates.end())
        return;

    beginTrackingTransientBuffer(*buffer, state);
}

void StateTracker::appendKeepInitialStateBarriers(
    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& imageBarriers,
    Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena>& bufferBarriers
){
    for(auto it = m_textureStates.begin(); it != m_textureStates.end(); ++it){
        const TextureSubresourceStateKey& key = it->first;
        if(!key.texture)
            continue;

        const TextureDesc& desc = key.texture->getDescription();
        const ResourceStates::Mask currentState = it.value();
        if(!desc.keepInitialState)
            continue;

        auto* texture = key.texture;
        if(currentState == desc.initialState){
            texture->m_keepInitialStateKnown = true;
            continue;
        }

        imageBarriers.push_back(__hidden_vulkan_state_tracking::BuildTextureStateBarrier(
            texture->m_image,
            texture->m_aspectMask,
            TextureSubresourceSet(key.mipLevel, 1u, key.arraySlice, 1u),
            currentState,
            desc.initialState,
            m_context.extensions.KHR_ray_tracing_pipeline
        ));
        it.value() = desc.initialState;
        texture->m_keepInitialStateKnown = true;
    }

    for(auto it = m_bufferStates.begin(); it != m_bufferStates.end(); ++it){
        Buffer* bufferResource = it->first;
        if(!bufferResource)
            continue;

        const BufferDesc& desc = bufferResource->getDescription();
        const ResourceStates::Mask currentState = it.value();
        if(!desc.keepInitialState || currentState == desc.initialState)
            continue;

        auto* buffer = bufferResource;
        auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
        barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(currentState != ResourceStates::Unknown ? currentState : ResourceStates::Common, m_context.extensions.KHR_ray_tracing_pipeline);
        barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(currentState != ResourceStates::Unknown ? currentState : ResourceStates::Common);
        barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(desc.initialState, m_context.extensions.KHR_ray_tracing_pipeline);
        barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(desc.initialState);
        barrier.buffer = buffer->m_buffer;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
        bufferBarriers.push_back(barrier);
        it.value() = desc.initialState;
    }
}

bool StateTracker::isUavBarrierEnabledForTexture(Texture& texture)const{
    const auto found = m_textureUavBarriers.find(&texture);
    return found == m_textureUavBarriers.end() || found.value();
}

bool StateTracker::isUavBarrierEnabledForBuffer(Buffer& buffer)const{
    const auto found = m_bufferUavBarriers.find(&buffer);
    return found == m_bufferUavBarriers.end() || found.value();
}

void StateTracker::beginTrackingTransientTexture(Texture& texture, TextureSubresourceSet subresources, ResourceStates::Mask state){
    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture.m_desc, TextureSubresourceMipResolve::Range);
    beginTrackingResolvedTransientTexture(texture, resolvedSubresources, state);
}

void StateTracker::beginTrackingResolvedTransientTexture(Texture& texture, const TextureSubresourceSet& resolvedSubresources, ResourceStates::Mask state){
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
    const usize subresourceCount = static_cast<usize>(resolvedSubresources.numMipLevels) * static_cast<usize>(resolvedSubresources.numArraySlices);

    __hidden_vulkan_state_tracking::ReserveAdditionalCapacity(m_textureStates, subresourceCount);

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const TextureSubresourceStateKey key{ &texture, mipLevel, arraySlice };
            m_textureStates.insert_or_assign(key, state);
        }
    }
}

void StateTracker::beginTrackingTransientBuffer(Buffer& buffer, ResourceStates::Mask state){
    m_bufferStates.insert_or_assign(&buffer, state);
}

void StateTracker::setEnableUavBarriersForTexture(Texture& texture, bool enableBarriers){
    m_textureUavBarriers.insert_or_assign(&texture, enableBarriers);
}

void StateTracker::setEnableUavBarriersForBuffer(Buffer& buffer, bool enableBarriers){
    m_bufferUavBarriers.insert_or_assign(&buffer, enableBarriers);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Command List Tracking Accessors


void CommandList::setEnableUavBarriersForTexture(Texture* texture, bool enableBarriers){
    if(!texture)
        return;
    m_stateTracker.setEnableUavBarriersForTexture(*texture, enableBarriers);
}

void CommandList::setEnableUavBarriersForBuffer(Buffer* buffer, bool enableBarriers){
    if(!buffer)
        return;
    m_stateTracker.setEnableUavBarriersForBuffer(*buffer, enableBarriers);
}

void CommandList::beginTrackingTextureState(Texture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits){
    m_stateTracker.beginTrackingTexture(texture, subresources, stateBits);
}

void CommandList::beginTrackingBufferState(Buffer* buffer, ResourceStates::Mask stateBits){
    m_stateTracker.beginTrackingBuffer(buffer, stateBits);
}

ResourceStates::Mask CommandList::getTextureSubresourceState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel){
    return m_stateTracker.getTextureState(texture, arraySlice, mipLevel);
}

ResourceStates::Mask CommandList::getBufferState(Buffer* buffer){
    return m_stateTracker.getBufferState(buffer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

