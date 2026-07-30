// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

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
    const auto sameTextureState = [](const TextureState& lhs, const TextureState& rhs){
        return
            lhs.state == rhs.state
            && lhs.queueSharing == rhs.queueSharing
            && lhs.ownerQueue == rhs.ownerQueue
            && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue
        ;
    };
    const auto sameBufferState = [](const BufferState& lhs, const BufferState& rhs){
        return
            lhs.state == rhs.state
            && lhs.queueSharing == rhs.queueSharing
            && lhs.ownerQueue == rhs.ownerQueue
            && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue
        ;
    };
    const auto samePermanentTextureState = [](const PermanentTextureState& lhs, const PermanentTextureState& rhs){
        return
            lhs.state == rhs.state
            && lhs.queueSharing == rhs.queueSharing
            && lhs.ownerQueue == rhs.ownerQueue
            && lhs.releaseDestinationQueue == rhs.releaseDestinationQueue
        ;
    };

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
        const TextureState* const baseState = baseIt != baseTextureIndices.end()
            ? &base.m_textureStates[baseIt.value()]
            : nullptr
        ;
        if(baseState && sameTextureState(state, *baseState))
            return true;

        const auto resultIt = resultTextureIndices.find(key);
        if(resultIt == resultTextureIndices.end()){
            const usize index = m_textureStates.size();
            m_textureStates.push_back(state);
            resultTextureIndices.insert_or_assign(key, index);
            return true;
        }

        TextureState& resultState = m_textureStates[resultIt.value()];
        if((!baseState || !sameTextureState(resultState, *baseState)) && !sameTextureState(resultState, state))
            return false;

        resultState = state;
        return true;
    };
    const auto mergeBufferState = [&](const BufferState& state){
        const auto baseIt = baseBufferIndices.find(state.buffer);
        const BufferState* const baseState = baseIt != baseBufferIndices.end()
            ? &base.m_bufferStates[baseIt.value()]
            : nullptr
        ;
        if(baseState && sameBufferState(state, *baseState))
            return true;

        const auto resultIt = resultBufferIndices.find(state.buffer);
        if(resultIt == resultBufferIndices.end()){
            const usize index = m_bufferStates.size();
            m_bufferStates.push_back(state);
            resultBufferIndices.insert_or_assign(state.buffer, index);
            return true;
        }

        BufferState& resultState = m_bufferStates[resultIt.value()];
        if((!baseState || !sameBufferState(resultState, *baseState)) && !sameBufferState(resultState, state))
            return false;

        resultState = state;
        return true;
    };
    const auto mergePermanentTextureState = [&](const PermanentTextureState& state){
        const auto baseIt = basePermanentTextureIndices.find(state.texture);
        const PermanentTextureState* const baseState = baseIt != basePermanentTextureIndices.end()
            ? &base.m_permanentTextureStates[baseIt.value()]
            : nullptr
        ;
        if(baseState && samePermanentTextureState(state, *baseState))
            return true;

        const auto resultIt = resultPermanentTextureIndices.find(state.texture);
        if(resultIt == resultPermanentTextureIndices.end()){
            const usize index = m_permanentTextureStates.size();
            m_permanentTextureStates.push_back(state);
            resultPermanentTextureIndices.insert_or_assign(state.texture, index);
            return true;
        }

        PermanentTextureState& resultState = m_permanentTextureStates[resultIt.value()];
        if((!baseState || !samePermanentTextureState(resultState, *baseState)) && !samePermanentTextureState(resultState, state))
            return false;

        resultState = state;
        return true;
    };
    const auto mergePermanentBufferState = [&](const BufferState& state){
        const auto baseIt = basePermanentBufferIndices.find(state.buffer);
        const BufferState* const baseState = baseIt != basePermanentBufferIndices.end()
            ? &base.m_permanentBufferStates[baseIt.value()]
            : nullptr
        ;
        if(baseState && sameBufferState(state, *baseState))
            return true;

        const auto resultIt = resultPermanentBufferIndices.find(state.buffer);
        if(resultIt == resultPermanentBufferIndices.end()){
            const usize index = m_permanentBufferStates.size();
            m_permanentBufferStates.push_back(state);
            resultPermanentBufferIndices.insert_or_assign(state.buffer, index);
            return true;
        }

        BufferState& resultState = m_permanentBufferStates[resultIt.value()];
        if((!baseState || !sameBufferState(resultState, *baseState)) && !sameBufferState(resultState, state))
            return false;

        resultState = state;
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

bool CommandListResourceStateHandoff::buildResourceSubset(
    const CommandListResourceStateHandoff& source,
    Texture* const* textures,
    const usize textureCount,
    Buffer* const* buffers,
    const usize bufferCount
){
    if(
        this == &source
        || !source.valid()
        || (textureCount != 0u && !textures)
        || (bufferCount != 0u && !buffers)
    ){
        reset();
        return false;
    }

    const auto containsTexture = [&](Texture* texture){
        if(!texture)
            return false;
        for(usize i = 0u; i < textureCount; ++i){
            if(textures[i] == texture)
                return true;
        }
        return false;
    };
    const auto containsBuffer = [&](Buffer* buffer){
        if(!buffer)
            return false;
        for(usize i = 0u; i < bufferCount; ++i){
            if(buffers[i] == buffer)
                return true;
        }
        return false;
    };

    reset();
    for(const TextureState& state : source.m_textureStates){
        if(containsTexture(state.texture))
            m_textureStates.push_back(state);
    }
    for(const BufferState& state : source.m_bufferStates){
        if(containsBuffer(state.buffer))
            m_bufferStates.push_back(state);
    }
    for(const PermanentTextureState& state : source.m_permanentTextureStates){
        if(containsTexture(state.texture))
            m_permanentTextureStates.push_back(state);
    }
    for(const BufferState& state : source.m_permanentBufferStates){
        if(containsBuffer(state.buffer))
            m_permanentBufferStates.push_back(state);
    }

    m_valid = true;
    return true;
}

bool CommandListResourceStateHandoff::buildTextureSubset(
    const CommandListResourceStateHandoff& source,
    Texture* const texture
){
    Texture* const textures[] = { texture };
    return buildResourceSubset(source, textures, 1u, nullptr, 0u);
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
    // Ordinary state transitions never imply a queue-family transfer. Vulkan zero-initializes these fields to
    // family 0, so set the required ignored sentinel explicitly before using the barrier on a nonzero family.
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    barrier.subresourceRange = VulkanDetail::BuildImageSubresourceRange(subresources, aspectMask);

    return barrier;
}

ResourceStates::Mask NormalizeOwnershipState(const ResourceStates::Mask state){
    return state != ResourceStates::Unknown ? state : ResourceStates::Common;
}

VkImageMemoryBarrier2 BuildTextureOwnershipReleaseBarrier(
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable
){
    const ResourceStates::Mask resolvedState = NormalizeOwnershipState(state);
    auto barrier = BuildTextureStateBarrier(image, aspectMask, subresources, resolvedState, resolvedState, rayTracingStageAvailable);
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = 0u;
    return barrier;
}

VkImageMemoryBarrier2 BuildTextureOwnershipAcquireBarrier(
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable
){
    const ResourceStates::Mask resolvedState = NormalizeOwnershipState(state);
    auto barrier = BuildTextureStateBarrier(image, aspectMask, subresources, resolvedState, resolvedState, rayTracingStageAvailable);
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask = 0u;
    return barrier;
}

VkBufferMemoryBarrier2 BuildBufferOwnershipReleaseBarrier(
    const VkBuffer buffer,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable
){
    const ResourceStates::Mask resolvedState = NormalizeOwnershipState(state);
    auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(resolvedState, rayTracingStageAvailable);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(resolvedState);
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = 0u;
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.buffer = buffer;
    barrier.offset = 0u;
    barrier.size = VK_WHOLE_SIZE;
    return barrier;
}

VkBufferMemoryBarrier2 BuildBufferOwnershipAcquireBarrier(
    const VkBuffer buffer,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable
){
    const ResourceStates::Mask resolvedState = NormalizeOwnershipState(state);
    auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask = 0u;
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(resolvedState, rayTracingStageAvailable);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(resolvedState);
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.buffer = buffer;
    barrier.offset = 0u;
    barrier.size = VK_WHOLE_SIZE;
    return barrier;
}

bool IsKnownQueue(const CommandQueue::Enum queue){
    return static_cast<u32>(queue) < static_cast<u32>(CommandQueue::kCount);
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

bool CommandList::importResourceStateHandoff(const CommandListResourceStateHandoff& states){
    NWB_ASSERT(states.m_valid);

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_StateHandoffArena);
    Vector<VkImageMemoryBarrier2, Alloc::ScratchArena> acquireImageBarriers{scratchArena};
    Vector<VkBufferMemoryBarrier2, Alloc::ScratchArena> acquireBufferBarriers{scratchArena};

    const auto appendTextureAcquire = [&](Texture& texture, const TextureSubresourceSet& subresources, const ResourceStates::Mask state, const ResourceQueueSharing::Mask sharing, const CommandQueue::Enum ownerQueue, const CommandQueue::Enum releaseDestinationQueue) -> bool {
        if(sharing != texture.m_desc.queueSharing){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Resource-state handoff texture sharing contract does not match the resource description"));
            return false;
        }

        if(m_device.usesConcurrentQueueSharing(sharing)){
            if(ownerQueue != CommandQueue::kCount || releaseDestinationQueue != CommandQueue::kCount){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Concurrent texture state handoff unexpectedly carries exclusive ownership"));
                return false;
            }
            return true;
        }

        if(!__hidden_vulkan_state_tracking::IsKnownQueue(ownerQueue) && ownerQueue != CommandQueue::kCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Texture state handoff has an invalid owner queue"));
            return false;
        }
        if(!__hidden_vulkan_state_tracking::IsKnownQueue(releaseDestinationQueue) && releaseDestinationQueue != CommandQueue::kCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Texture state handoff has an invalid ownership destination"));
            return false;
        }

        if(releaseDestinationQueue == CommandQueue::kCount){
            if(ownerQueue != CommandQueue::kCount && ownerQueue != m_desc.queueType){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive texture handoff changes queue without a release/acquire transfer"));
                return false;
            }
            return true;
        }

        if(ownerQueue == CommandQueue::kCount || releaseDestinationQueue != m_desc.queueType){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive texture handoff is not imported by its declared destination queue"));
            return false;
        }

        const u32 sourceQueueFamily = m_device.getQueueFamilyIndex(ownerQueue);
        const u32 destinationQueueFamily = m_device.getQueueFamilyIndex(m_desc.queueType);
        if(sourceQueueFamily == VK_QUEUE_FAMILY_IGNORED || destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive texture handoff references an unavailable queue family"));
            return false;
        }
        if(sourceQueueFamily != destinationQueueFamily){
            acquireImageBarriers.push_back(__hidden_vulkan_state_tracking::BuildTextureOwnershipAcquireBarrier(
                texture.m_image,
                texture.m_aspectMask,
                subresources,
                state,
                sourceQueueFamily,
                destinationQueueFamily,
                m_context.extensions.KHR_ray_tracing_pipeline
            ));
        }
        return true;
    };
    const auto appendBufferAcquire = [&](Buffer& buffer, const ResourceStates::Mask state, const ResourceQueueSharing::Mask sharing, const CommandQueue::Enum ownerQueue, const CommandQueue::Enum releaseDestinationQueue) -> bool {
        if(sharing != buffer.m_desc.queueSharing){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Resource-state handoff buffer sharing contract does not match the resource description"));
            return false;
        }

        if(m_device.usesConcurrentQueueSharing(sharing)){
            if(ownerQueue != CommandQueue::kCount || releaseDestinationQueue != CommandQueue::kCount){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Concurrent buffer state handoff unexpectedly carries exclusive ownership"));
                return false;
            }
            return true;
        }

        if(!__hidden_vulkan_state_tracking::IsKnownQueue(ownerQueue) && ownerQueue != CommandQueue::kCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Buffer state handoff has an invalid owner queue"));
            return false;
        }
        if(!__hidden_vulkan_state_tracking::IsKnownQueue(releaseDestinationQueue) && releaseDestinationQueue != CommandQueue::kCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Buffer state handoff has an invalid ownership destination"));
            return false;
        }

        if(releaseDestinationQueue == CommandQueue::kCount){
            if(ownerQueue != CommandQueue::kCount && ownerQueue != m_desc.queueType){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive buffer handoff changes queue without a release/acquire transfer"));
                return false;
            }
            return true;
        }

        if(ownerQueue == CommandQueue::kCount || releaseDestinationQueue != m_desc.queueType){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive buffer handoff is not imported by its declared destination queue"));
            return false;
        }

        const u32 sourceQueueFamily = m_device.getQueueFamilyIndex(ownerQueue);
        const u32 destinationQueueFamily = m_device.getQueueFamilyIndex(m_desc.queueType);
        if(sourceQueueFamily == VK_QUEUE_FAMILY_IGNORED || destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive buffer handoff references an unavailable queue family"));
            return false;
        }
        if(sourceQueueFamily != destinationQueueFamily){
            acquireBufferBarriers.push_back(__hidden_vulkan_state_tracking::BuildBufferOwnershipAcquireBarrier(
                buffer.m_buffer,
                state,
                sourceQueueFamily,
                destinationQueueFamily,
                m_context.extensions.KHR_ray_tracing_pipeline
            ));
        }
        return true;
    };

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_textureStates, states.m_textureStates.size());
    for(const CommandListResourceStateHandoff::TextureState& state : states.m_textureStates){
        if(!state.texture)
            continue;

        if(!appendTextureAcquire(
            *state.texture,
            TextureSubresourceSet(state.mipLevel, 1u, state.arraySlice, 1u),
            state.state,
            state.queueSharing,
            state.ownerQueue,
            state.releaseDestinationQueue
        ))
            return false;

        const TextureSubresourceStateKey key{ state.texture, state.mipLevel, state.arraySlice };
        m_stateTracker.m_textureStates.insert_or_assign(key, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_bufferStates, states.m_bufferStates.size());
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_bufferStates){
        if(!state.buffer)
            continue;

        if(!appendBufferAcquire(*state.buffer, state.state, state.queueSharing, state.ownerQueue, state.releaseDestinationQueue))
            return false;

        m_stateTracker.m_bufferStates.insert_or_assign(state.buffer, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_permanentTextureStates, states.m_permanentTextureStates.size());
    for(const CommandListResourceStateHandoff::PermanentTextureState& state : states.m_permanentTextureStates){
        if(!state.texture)
            continue;

        if(!appendTextureAcquire(*state.texture, s_AllSubresources, state.state, state.queueSharing, state.ownerQueue, state.releaseDestinationQueue))
            return false;

        m_stateTracker.m_permanentTextureStates.insert_or_assign(state.texture, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_permanentBufferStates, states.m_permanentBufferStates.size());
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_permanentBufferStates){
        if(!state.buffer)
            continue;

        if(!appendBufferAcquire(*state.buffer, state.state, state.queueSharing, state.ownerQueue, state.releaseDestinationQueue))
            return false;

        m_stateTracker.m_permanentBufferStates.insert_or_assign(state.buffer, state.state);
    }

    if(!acquireImageBarriers.empty() || !acquireBufferBarriers.empty()){
        auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        depInfo.imageMemoryBarrierCount = static_cast<u32>(acquireImageBarriers.size());
        depInfo.pImageMemoryBarriers = acquireImageBarriers.data();
        depInfo.bufferMemoryBarrierCount = static_cast<u32>(acquireBufferBarriers.size());
        depInfo.pBufferMemoryBarriers = acquireBufferBarriers.data();
        executePipelineBarrier(depInfo);
    }

    return true;
}

void CommandList::exportResourceStateHandoff(CommandListResourceStateHandoff& states)const{
    states.reset();
    const auto getTextureOwnership = [&](const TextureSubresourceStateKey& key, const ResourceQueueSharing::Mask sharing, CommandQueue::Enum& outOwner, CommandQueue::Enum& outReleaseDestination){
        outOwner = CommandQueue::kCount;
        outReleaseDestination = CommandQueue::kCount;
        if(m_device.usesConcurrentQueueSharing(sharing))
            return;

        outOwner = m_desc.queueType;
        const auto releaseIt = m_textureOwnershipReleaseDestinations.find(key);
        if(releaseIt != m_textureOwnershipReleaseDestinations.end())
            outReleaseDestination = releaseIt.value();
    };
    const auto getBufferOwnership = [&](Buffer* buffer, const ResourceQueueSharing::Mask sharing, CommandQueue::Enum& outOwner, CommandQueue::Enum& outReleaseDestination){
        outOwner = CommandQueue::kCount;
        outReleaseDestination = CommandQueue::kCount;
        if(m_device.usesConcurrentQueueSharing(sharing))
            return;

        outOwner = m_desc.queueType;
        const auto releaseIt = m_bufferOwnershipReleaseDestinations.find(buffer);
        if(releaseIt != m_bufferOwnershipReleaseDestinations.end())
            outReleaseDestination = releaseIt.value();
    };

    states.m_textureStates.reserve(m_stateTracker.m_textureStates.size());
    for(auto it = m_stateTracker.m_textureStates.begin(); it != m_stateTracker.m_textureStates.end(); ++it){
        const TextureSubresourceStateKey& key = it->first;
        if(!key.texture)
            continue;

        CommandQueue::Enum ownerQueue = CommandQueue::kCount;
        CommandQueue::Enum releaseDestinationQueue = CommandQueue::kCount;
        getTextureOwnership(key, key.texture->m_desc.queueSharing, ownerQueue, releaseDestinationQueue);
        states.m_textureStates.push_back(CommandListResourceStateHandoff::TextureState{
            key.texture,
            key.mipLevel,
            key.arraySlice,
            it.value(),
            key.texture->m_desc.queueSharing,
            ownerQueue,
            releaseDestinationQueue
        });
    }

    states.m_bufferStates.reserve(m_stateTracker.m_bufferStates.size());
    for(auto it = m_stateTracker.m_bufferStates.begin(); it != m_stateTracker.m_bufferStates.end(); ++it){
        if(!it->first)
            continue;

        CommandQueue::Enum ownerQueue = CommandQueue::kCount;
        CommandQueue::Enum releaseDestinationQueue = CommandQueue::kCount;
        getBufferOwnership(it->first, it->first->m_desc.queueSharing, ownerQueue, releaseDestinationQueue);
        states.m_bufferStates.push_back(CommandListResourceStateHandoff::BufferState{
            it->first,
            it.value(),
            it->first->m_desc.queueSharing,
            ownerQueue,
            releaseDestinationQueue
        });
    }

    states.m_permanentTextureStates.reserve(m_stateTracker.m_permanentTextureStates.size());
    for(auto it = m_stateTracker.m_permanentTextureStates.begin(); it != m_stateTracker.m_permanentTextureStates.end(); ++it){
        if(!it->first)
            continue;

        CommandQueue::Enum ownerQueue = CommandQueue::kCount;
        CommandQueue::Enum releaseDestinationQueue = CommandQueue::kCount;
        const TextureSubresourceStateKey key{ it->first, 0u, 0u };
        getTextureOwnership(key, it->first->m_desc.queueSharing, ownerQueue, releaseDestinationQueue);
        states.m_permanentTextureStates.push_back(CommandListResourceStateHandoff::PermanentTextureState{
            it->first,
            it.value(),
            it->first->m_desc.queueSharing,
            ownerQueue,
            releaseDestinationQueue
        });
    }

    states.m_permanentBufferStates.reserve(m_stateTracker.m_permanentBufferStates.size());
    for(auto it = m_stateTracker.m_permanentBufferStates.begin(); it != m_stateTracker.m_permanentBufferStates.end(); ++it){
        if(!it->first)
            continue;

        CommandQueue::Enum ownerQueue = CommandQueue::kCount;
        CommandQueue::Enum releaseDestinationQueue = CommandQueue::kCount;
        getBufferOwnership(it->first, it->first->m_desc.queueSharing, ownerQueue, releaseDestinationQueue);
        states.m_permanentBufferStates.push_back(CommandListResourceStateHandoff::BufferState{
            it->first,
            it.value(),
            it->first->m_desc.queueSharing,
            ownerQueue,
            releaseDestinationQueue
        });
    }

    states.m_valid = true;
}

void CommandList::appendPendingOwnershipReleaseBarriers(){
    if(m_textureOwnershipReleaseDestinations.empty() && m_bufferOwnershipReleaseDestinations.empty())
        return;

    const u32 sourceQueueFamily = m_device.getQueueFamilyIndex(m_desc.queueType);
    if(sourceQueueFamily == VK_QUEUE_FAMILY_IGNORED){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release resource ownership from an unavailable source queue family"));
        return;
    }

    for(auto it = m_textureOwnershipReleaseDestinations.begin(); it != m_textureOwnershipReleaseDestinations.end(); ++it){
        const TextureSubresourceStateKey& key = it->first;
        Texture* const texture = key.texture;
        if(!texture)
            continue;
        if(m_device.usesConcurrentQueueSharing(texture->m_desc.queueSharing)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Concurrent texture unexpectedly has a pending ownership release"));
            continue;
        }

        const CommandQueue::Enum destinationQueue = it.value();
        const u32 destinationQueueFamily = m_device.getQueueFamilyIndex(destinationQueue);
        if(destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release texture ownership to an unavailable destination queue family"));
            continue;
        }
        if(destinationQueueFamily == sourceQueueFamily)
            continue;

        const ResourceStates::Mask state = m_stateTracker.getTextureState(texture, key.arraySlice, key.mipLevel);
        m_pendingImageBarriers.push_back(__hidden_vulkan_state_tracking::BuildTextureOwnershipReleaseBarrier(
            texture->m_image,
            texture->m_aspectMask,
            TextureSubresourceSet(key.mipLevel, 1u, key.arraySlice, 1u),
            state,
            sourceQueueFamily,
            destinationQueueFamily,
            m_context.extensions.KHR_ray_tracing_pipeline
        ));
    }

    for(auto it = m_bufferOwnershipReleaseDestinations.begin(); it != m_bufferOwnershipReleaseDestinations.end(); ++it){
        Buffer* const buffer = it->first;
        if(!buffer)
            continue;
        if(m_device.usesConcurrentQueueSharing(buffer->m_desc.queueSharing)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Concurrent buffer unexpectedly has a pending ownership release"));
            continue;
        }

        const CommandQueue::Enum destinationQueue = it.value();
        const u32 destinationQueueFamily = m_device.getQueueFamilyIndex(destinationQueue);
        if(destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release buffer ownership to an unavailable destination queue family"));
            continue;
        }
        if(destinationQueueFamily == sourceQueueFamily)
            continue;

        const ResourceStates::Mask state = m_stateTracker.getBufferState(buffer);
        m_pendingBufferBarriers.push_back(__hidden_vulkan_state_tracking::BuildBufferOwnershipReleaseBarrier(
            buffer->m_buffer,
            state,
            sourceQueueFamily,
            destinationQueueFamily,
            m_context.extensions.KHR_ray_tracing_pipeline
        ));
    }

    commitBarriers();
}

void CommandList::executePipelineBarrier(const VkDependencyInfo& depInfo){
    Framebuffer* resumeFramebuffer = nullptr;
    if(m_renderPassActive){
        resumeFramebuffer = m_renderPassFramebuffer;
        endDynamicRendering();
        m_renderPassActive = false;
        m_renderPassFramebuffer = nullptr;
    }

    // ResourceStates intentionally describe shader visibility without committing to a graphics, compute, or ray
    // pipeline. The normal stage mapping therefore includes ALL_GRAPHICS alongside compute/ray stages. That is valid
    // for the Graphics command pool, but not for a compute-only (or copy-only) pool. ALL_COMMANDS is valid on every
    // queue family and scopes only the commands supported by that queue, so use it for each non-empty side when the
    // generic barrier is emitted outside the Graphics lane. Preserve NONE for the ignored side of a queue-ownership
    // release/acquire pair.
    VkDependencyInfo queueCompatibleDepInfo = depInfo;
    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_StateHandoffArena);
    Vector<VkMemoryBarrier2, Alloc::ScratchArena> queueCompatibleMemoryBarriers{scratchArena};
    Vector<VkImageMemoryBarrier2, Alloc::ScratchArena> queueCompatibleImageBarriers{scratchArena};
    Vector<VkBufferMemoryBarrier2, Alloc::ScratchArena> queueCompatibleBufferBarriers{scratchArena};
    if(m_desc.queueType != CommandQueue::Graphics){
        const auto queueCompatibleStageMask = [](const VkPipelineStageFlags2 stageMask){
            return stageMask == VK_PIPELINE_STAGE_2_NONE
                ? VK_PIPELINE_STAGE_2_NONE
                : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
            ;
        };

        queueCompatibleMemoryBarriers.reserve(depInfo.memoryBarrierCount);
        for(u32 index = 0u; index < depInfo.memoryBarrierCount; ++index){
            VkMemoryBarrier2 barrier = depInfo.pMemoryBarriers[index];
            barrier.srcStageMask = queueCompatibleStageMask(barrier.srcStageMask);
            barrier.dstStageMask = queueCompatibleStageMask(barrier.dstStageMask);
            queueCompatibleMemoryBarriers.push_back(barrier);
        }
        queueCompatibleDepInfo.pMemoryBarriers = queueCompatibleMemoryBarriers.data();

        queueCompatibleImageBarriers.reserve(depInfo.imageMemoryBarrierCount);
        for(u32 index = 0u; index < depInfo.imageMemoryBarrierCount; ++index){
            VkImageMemoryBarrier2 barrier = depInfo.pImageMemoryBarriers[index];
            barrier.srcStageMask = queueCompatibleStageMask(barrier.srcStageMask);
            barrier.dstStageMask = queueCompatibleStageMask(barrier.dstStageMask);
            queueCompatibleImageBarriers.push_back(barrier);
        }
        queueCompatibleDepInfo.pImageMemoryBarriers = queueCompatibleImageBarriers.data();

        queueCompatibleBufferBarriers.reserve(depInfo.bufferMemoryBarrierCount);
        for(u32 index = 0u; index < depInfo.bufferMemoryBarrierCount; ++index){
            VkBufferMemoryBarrier2 barrier = depInfo.pBufferMemoryBarriers[index];
            barrier.srcStageMask = queueCompatibleStageMask(barrier.srcStageMask);
            barrier.dstStageMask = queueCompatibleStageMask(barrier.dstStageMask);
            queueCompatibleBufferBarriers.push_back(barrier);
        }
        queueCompatibleDepInfo.pBufferMemoryBarriers = queueCompatibleBufferBarriers.data();
    }

    vkCmdPipelineBarrier2(m_currentCmdBuf->m_cmdBuf, &queueCompatibleDepInfo);

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
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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

void CommandList::releaseTextureOwnership(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const RenderLane::Enum destinationLane
){
    if(!textureResource || !m_currentCmdBuf)
        return;

    Texture& texture = *textureResource;
    if(m_stateTracker.isPermanentTexture(texture)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release ownership of a permanently tracked texture"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot release ownership of a permanently tracked texture"));
        return;
    }
    if(m_device.usesConcurrentQueueSharing(texture.m_desc.queueSharing)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release ownership of a concurrently shared texture"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot release ownership of a concurrently shared texture"));
        return;
    }

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture.m_desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::DebugValidateTextureSubresourceRange(resolvedSubresources, NWB_TEXT("release texture ownership")))
        return;

    const CommandQueue::Enum destinationQueue = m_device.resolveRenderLane(destinationLane);
    if(!m_device.getQueue(destinationQueue)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release texture ownership to an unavailable destination queue"));
        return;
    }

    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;

    // A release must also be exported in the handoff. Every image subresource therefore needs a concrete tracked
    // layout first: unlike a buffer, an untouched image may still be VK_IMAGE_LAYOUT_UNDEFINED even when its RHI
    // descriptor names a preferred initial state. Do not invent a layout here; make the producer transition it
    // explicitly before releasing ownership.
    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const ResourceStates::Mask state = m_stateTracker.getTextureState(&texture, arraySlice, mipLevel);
            if(state == ResourceStates::Unknown){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release texture ownership without a known final resource state"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot release texture ownership without a known final resource state"));
                return;
            }
            m_stateTracker.beginTrackingTexture(&texture, TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u), state);
        }
    }

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const TextureSubresourceStateKey key{ &texture, mipLevel, arraySlice };
            const auto existing = m_textureOwnershipReleaseDestinations.find(key);
            if(existing != m_textureOwnershipReleaseDestinations.end() && existing.value() != destinationQueue){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Texture ownership was released to conflicting destination queues"));
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Texture ownership was released to conflicting destination queues"));
                return;
            }
        }
    }

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel)
            m_textureOwnershipReleaseDestinations.insert_or_assign(TextureSubresourceStateKey{ &texture, mipLevel, arraySlice }, destinationQueue);
    }
}

void CommandList::releaseBufferOwnership(Buffer* bufferResource, const RenderLane::Enum destinationLane){
    if(!bufferResource || !m_currentCmdBuf)
        return;

    Buffer& buffer = *bufferResource;
    if(m_stateTracker.isPermanentBuffer(buffer)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release ownership of a permanently tracked buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot release ownership of a permanently tracked buffer"));
        return;
    }
    if(m_device.usesConcurrentQueueSharing(buffer.m_desc.queueSharing)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release ownership of a concurrently shared buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot release ownership of a concurrently shared buffer"));
        return;
    }

    const CommandQueue::Enum destinationQueue = m_device.resolveRenderLane(destinationLane);
    if(!m_device.getQueue(destinationQueue)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release buffer ownership to an unavailable destination queue"));
        return;
    }

    // Match the texture path above: even an untouched buffer must have an exported concrete state so the matching
    // acquire can be emitted by the consumer. Existing state tracking has precedence; the descriptor initial state
    // only seeds a resource that this command list has not otherwise touched.
    ResourceStates::Mask state = m_stateTracker.getBufferState(&buffer);
    if(state == ResourceStates::Unknown)
        state = buffer.m_desc.initialState;
    if(state == ResourceStates::Unknown){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release buffer ownership without a known final resource state"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Cannot release buffer ownership without a known final resource state"));
        return;
    }
    m_stateTracker.beginTrackingBuffer(&buffer, state);

    const auto existing = m_bufferOwnershipReleaseDestinations.find(&buffer);
    if(existing != m_bufferOwnershipReleaseDestinations.end() && existing.value() != destinationQueue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Buffer ownership was released to conflicting destination queues"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Buffer ownership was released to conflicting destination queues"));
        return;
    }

    m_bufferOwnershipReleaseDestinations.insert_or_assign(&buffer, destinationQueue);
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
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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

