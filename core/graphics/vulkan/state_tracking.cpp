// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>
#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_list_state_handoff{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextureStateKey = GraphicsBackend::TextureSubresourceStateKey;
using TextureStateIndexMap = HashMap<
    TextureStateKey,
    usize,
    GraphicsBackend::TextureSubresourceStateKeyHasher,
    GraphicsBackend::TextureSubresourceStateKeyEqualTo,
    Alloc::GlobalArena
>;
using BufferStateIndexMap = HashMap<Buffer*, usize, Hasher<Buffer*>, EqualTo<Buffer*>, Alloc::GlobalArena>;
using PermanentTextureStateIndexMap = HashMap<Texture*, usize, Hasher<Texture*>, EqualTo<Texture*>, Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CommandListResourceStateHandoff::buildFanIn(
    const CommandListResourceStateHandoff& base,
    const CommandListResourceStateHandoff* const* branches,
    const usize branchCount
){
    if(this == &base)
        return false;

    if(!base.valid() || (branchCount != 0u && !branches)){
        reset();
        return false;
    }

    for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
        const CommandListResourceStateHandoff* branch = branches[branchIndex];
        if(branch == this)
            return false;

        if(!branch || !branch->valid()){
            reset();
            return false;
        }
    }

    reset();

    auto& arena = m_textureStates.get_allocator().arena();
    using namespace __hidden_command_list_state_handoff;

    TextureStateIndexMap baseTextureIndices(
        0u,
        GraphicsBackend::TextureSubresourceStateKeyHasher(),
        GraphicsBackend::TextureSubresourceStateKeyEqualTo(),
        arena
    );
    TextureStateIndexMap resultTextureIndices(
        0u,
        GraphicsBackend::TextureSubresourceStateKeyHasher(),
        GraphicsBackend::TextureSubresourceStateKeyEqualTo(),
        arena
    );
    BufferStateIndexMap baseBufferIndices(0u, Hasher<Buffer*>(), EqualTo<Buffer*>(), arena);
    BufferStateIndexMap resultBufferIndices(0u, Hasher<Buffer*>(), EqualTo<Buffer*>(), arena);
    PermanentTextureStateIndexMap basePermanentTextureIndices(0u, Hasher<Texture*>(), EqualTo<Texture*>(), arena);
    PermanentTextureStateIndexMap resultPermanentTextureIndices(0u, Hasher<Texture*>(), EqualTo<Texture*>(), arena);
    BufferStateIndexMap basePermanentBufferIndices(0u, Hasher<Buffer*>(), EqualTo<Buffer*>(), arena);
    BufferStateIndexMap resultPermanentBufferIndices(0u, Hasher<Buffer*>(), EqualTo<Buffer*>(), arena);

    m_textureStates.reserve(base.m_textureStates.size());
    baseTextureIndices.reserve(base.m_textureStates.size());
    resultTextureIndices.reserve(base.m_textureStates.size());
    for(const TextureState& state : base.m_textureStates){
        const TextureStateKey key{ state.texture, state.mipLevel, state.arraySlice };
        const usize index = m_textureStates.size();
        m_textureStates.push_back(state);
        baseTextureIndices.insert_or_assign(key, index);
        resultTextureIndices.insert_or_assign(key, index);
    }

    m_bufferStates.reserve(base.m_bufferStates.size());
    baseBufferIndices.reserve(base.m_bufferStates.size());
    resultBufferIndices.reserve(base.m_bufferStates.size());
    for(const BufferState& state : base.m_bufferStates){
        const usize index = m_bufferStates.size();
        m_bufferStates.push_back(state);
        baseBufferIndices.insert_or_assign(state.buffer, index);
        resultBufferIndices.insert_or_assign(state.buffer, index);
    }

    m_permanentTextureStates.reserve(base.m_permanentTextureStates.size());
    basePermanentTextureIndices.reserve(base.m_permanentTextureStates.size());
    resultPermanentTextureIndices.reserve(base.m_permanentTextureStates.size());
    for(const PermanentTextureState& state : base.m_permanentTextureStates){
        const usize index = m_permanentTextureStates.size();
        m_permanentTextureStates.push_back(state);
        basePermanentTextureIndices.insert_or_assign(state.texture, index);
        resultPermanentTextureIndices.insert_or_assign(state.texture, index);
    }

    m_permanentBufferStates.reserve(base.m_permanentBufferStates.size());
    basePermanentBufferIndices.reserve(base.m_permanentBufferStates.size());
    resultPermanentBufferIndices.reserve(base.m_permanentBufferStates.size());
    for(const BufferState& state : base.m_permanentBufferStates){
        const usize index = m_permanentBufferStates.size();
        m_permanentBufferStates.push_back(state);
        basePermanentBufferIndices.insert_or_assign(state.buffer, index);
        resultPermanentBufferIndices.insert_or_assign(state.buffer, index);
    }

    const auto mergeTextureState = [&](const TextureState& state){
        const TextureStateKey key{ state.texture, state.mipLevel, state.arraySlice };
        const auto baseIt = baseTextureIndices.find(key);
        const ResourceStates::Mask baseState = baseIt != baseTextureIndices.end()
            ? base.m_textureStates[baseIt.value()].state
            : ResourceStates::Unknown
        ;
        if(state.state == baseState)
            return true;

        const auto resultIt = resultTextureIndices.find(key);
        if(resultIt == resultTextureIndices.end()){
            const usize index = m_textureStates.size();
            m_textureStates.push_back(state);
            resultTextureIndices.insert_or_assign(key, index);
            return true;
        }

        TextureState& resultState = m_textureStates[resultIt.value()];
        if(resultState.state != baseState && resultState.state != state.state)
            return false;

        resultState.state = state.state;
        return true;
    };
    const auto mergeBufferState = [&](const BufferState& state){
        const auto baseIt = baseBufferIndices.find(state.buffer);
        const ResourceStates::Mask baseState = baseIt != baseBufferIndices.end()
            ? base.m_bufferStates[baseIt.value()].state
            : ResourceStates::Unknown
        ;
        if(state.state == baseState)
            return true;

        const auto resultIt = resultBufferIndices.find(state.buffer);
        if(resultIt == resultBufferIndices.end()){
            const usize index = m_bufferStates.size();
            m_bufferStates.push_back(state);
            resultBufferIndices.insert_or_assign(state.buffer, index);
            return true;
        }

        BufferState& resultState = m_bufferStates[resultIt.value()];
        if(resultState.state != baseState && resultState.state != state.state)
            return false;

        resultState.state = state.state;
        return true;
    };
    const auto mergePermanentTextureState = [&](const PermanentTextureState& state){
        const auto baseIt = basePermanentTextureIndices.find(state.texture);
        const ResourceStates::Mask baseState = baseIt != basePermanentTextureIndices.end()
            ? base.m_permanentTextureStates[baseIt.value()].state
            : ResourceStates::Unknown
        ;
        if(state.state == baseState)
            return true;

        const auto resultIt = resultPermanentTextureIndices.find(state.texture);
        if(resultIt == resultPermanentTextureIndices.end()){
            const usize index = m_permanentTextureStates.size();
            m_permanentTextureStates.push_back(state);
            resultPermanentTextureIndices.insert_or_assign(state.texture, index);
            return true;
        }

        PermanentTextureState& resultState = m_permanentTextureStates[resultIt.value()];
        if(resultState.state != baseState && resultState.state != state.state)
            return false;

        resultState.state = state.state;
        return true;
    };
    const auto mergePermanentBufferState = [&](const BufferState& state){
        const auto baseIt = basePermanentBufferIndices.find(state.buffer);
        const ResourceStates::Mask baseState = baseIt != basePermanentBufferIndices.end()
            ? base.m_permanentBufferStates[baseIt.value()].state
            : ResourceStates::Unknown
        ;
        if(state.state == baseState)
            return true;

        const auto resultIt = resultPermanentBufferIndices.find(state.buffer);
        if(resultIt == resultPermanentBufferIndices.end()){
            const usize index = m_permanentBufferStates.size();
            m_permanentBufferStates.push_back(state);
            resultPermanentBufferIndices.insert_or_assign(state.buffer, index);
            return true;
        }

        BufferState& resultState = m_permanentBufferStates[resultIt.value()];
        if(resultState.state != baseState && resultState.state != state.state)
            return false;

        resultState.state = state.state;
        return true;
    };

    for(usize branchIndex = 0u; branchIndex < branchCount; ++branchIndex){
        const CommandListResourceStateHandoff& branch = *branches[branchIndex];
        for(const TextureState& state : branch.m_textureStates){
            if(!mergeTextureState(state)){
                reset();
                return false;
            }
        }
        for(const BufferState& state : branch.m_bufferStates){
            if(!mergeBufferState(state)){
                reset();
                return false;
            }
        }
        for(const PermanentTextureState& state : branch.m_permanentTextureStates){
            if(!mergePermanentTextureState(state)){
                reset();
                return false;
            }
        }
        for(const BufferState& state : branch.m_permanentBufferStates){
            if(!mergePermanentBufferState(state)){
                reset();
                return false;
            }
        }
    }

    m_valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::setResourceStatesForFramebuffer(Framebuffer& framebuffer){
    const FramebufferDesc& desc = framebuffer.getDescription();

    for(const auto& attachment : desc.colorAttachments)
        setTextureState(attachment.texture, attachment.subresources, ResourceStates::RenderTarget);

    if(desc.depthAttachment.valid())
        setTextureState(desc.depthAttachment.texture, desc.depthAttachment.subresources, desc.depthAttachment.isReadOnly ? ResourceStates::DepthRead : ResourceStates::DepthWrite);

    if(desc.shadingRateAttachment.valid())
        setTextureState(desc.shadingRateAttachment.texture, desc.shadingRateAttachment.subresources, ResourceStates::ShadingRateSurface);
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

void CommandList::importResourceStateHandoff(const CommandListResourceStateHandoff& states){
    NWB_ASSERT(states.m_valid);

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_textureStates, states.m_textureStates.size());
    for(const CommandListResourceStateHandoff::TextureState& state : states.m_textureStates){
        if(!state.texture)
            continue;

        const TextureSubresourceStateKey key{ state.texture, state.mipLevel, state.arraySlice };
        m_stateTracker.m_textureStates.insert_or_assign(key, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_bufferStates, states.m_bufferStates.size());
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_bufferStates){
        if(state.buffer)
            m_stateTracker.m_bufferStates.insert_or_assign(state.buffer, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_permanentTextureStates, states.m_permanentTextureStates.size());
    for(const CommandListResourceStateHandoff::PermanentTextureState& state : states.m_permanentTextureStates){
        if(!state.texture)
            continue;

        m_stateTracker.m_permanentTextureStates.insert_or_assign(state.texture, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_permanentBufferStates, states.m_permanentBufferStates.size());
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_permanentBufferStates){
        if(state.buffer)
            m_stateTracker.m_permanentBufferStates.insert_or_assign(state.buffer, state.state);
    }
}

void CommandList::exportResourceStateHandoff(CommandListResourceStateHandoff& states)const{
    states.reset();
    states.m_textureStates.reserve(m_stateTracker.m_textureStates.size());
    for(auto it = m_stateTracker.m_textureStates.begin(); it != m_stateTracker.m_textureStates.end(); ++it){
        const TextureSubresourceStateKey& key = it->first;
        if(!key.texture)
            continue;

        states.m_textureStates.push_back(CommandListResourceStateHandoff::TextureState{
            key.texture,
            key.mipLevel,
            key.arraySlice,
            it.value()
        });
    }

    states.m_bufferStates.reserve(m_stateTracker.m_bufferStates.size());
    for(auto it = m_stateTracker.m_bufferStates.begin(); it != m_stateTracker.m_bufferStates.end(); ++it){
        if(!it->first)
            continue;

        states.m_bufferStates.push_back(CommandListResourceStateHandoff::BufferState{
            it->first,
            it.value()
        });
    }

    states.m_permanentTextureStates.reserve(m_stateTracker.m_permanentTextureStates.size());
    for(auto it = m_stateTracker.m_permanentTextureStates.begin(); it != m_stateTracker.m_permanentTextureStates.end(); ++it){
        if(!it->first)
            continue;

        states.m_permanentTextureStates.push_back(CommandListResourceStateHandoff::PermanentTextureState{
            it->first,
            it.value()
        });
    }

    states.m_permanentBufferStates.reserve(m_stateTracker.m_permanentBufferStates.size());
    for(auto it = m_stateTracker.m_permanentBufferStates.begin(); it != m_stateTracker.m_permanentBufferStates.end(); ++it){
        if(!it->first)
            continue;

        states.m_permanentBufferStates.push_back(CommandListResourceStateHandoff::BufferState{
            it->first,
            it.value()
        });
    }

    states.m_valid = true;
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
                ::ContainerDetail::ReserveAdditionalCapacity(m_pendingImageBarriers, subresourceCount);
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

    ::ContainerDetail::ReserveAdditionalCapacity(m_textureStates, subresourceCount);

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

