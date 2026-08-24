// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "state_tracking_detail.h"

#include <core/common/log.h>
#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


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
    if(!states.validForDeviceGeneration(m_context.deviceGeneration)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Resource-state handoff belongs to a retired device generation"));
        return false;
    }
    NWB_ASSERT(states.m_valid);

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_StateHandoffArena);
    Vector<VkImageMemoryBarrier2, Alloc::ScratchArena> acquireImageBarriers{scratchArena};
    Vector<VkBufferMemoryBarrier2, Alloc::ScratchArena> acquireBufferBarriers{scratchArena};

    const auto appendTextureAcquire = [&](Texture& texture, const TextureSubresourceSet& subresources, const ResourceStates::Mask state, const ResourceQueueSharing::Mask sharing, const GpuPhysicalQueueId ownerQueue, const GpuPhysicalQueueId releaseDestinationQueue) -> bool {
        if(sharing != texture.m_desc.queueSharing){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Resource-state handoff texture sharing contract does not match the resource description"));
            return false;
        }

        if(m_device.usesConcurrentQueueSharing(sharing)){
            if(ownerQueue.valid() || releaseDestinationQueue.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Concurrent texture state handoff unexpectedly carries exclusive ownership"));
                return false;
            }
            return true;
        }

        if(ownerQueue.valid() && !m_device.matchesPhysicalQueueIdentity(ownerQueue)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Texture state handoff has an invalid owner queue"));
            return false;
        }
        if(releaseDestinationQueue.valid() && !m_device.matchesPhysicalQueueIdentity(releaseDestinationQueue)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Texture state handoff has an invalid ownership destination"));
            return false;
        }

        const u32 sourceQueueFamily = ownerQueue.valid()
            ? m_device.getQueueFamilyIndex(ownerQueue)
            : VK_QUEUE_FAMILY_IGNORED
        ;
        const u32 destinationQueueFamily = m_device.getQueueFamilyIndex(m_desc.physicalQueue);
        if(ownerQueue.valid() && (sourceQueueFamily == VK_QUEUE_FAMILY_IGNORED || destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive texture handoff references an unavailable queue family"));
            return false;
        }

        if(!releaseDestinationQueue.valid()){
            // Vulkan ownership belongs to a queue family, not one VkQueue. A state handoff from another physical
            // queue in this family is valid once its timeline token is waited; reject only a true family crossing
            // that omitted the compiler-planned release/acquire pair.
            if(ownerQueue.valid() && sourceQueueFamily != destinationQueueFamily){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive texture handoff changes queue family without a release/acquire transfer"));
                return false;
            }
            return true;
        }

        const u32 releaseDestinationFamily = m_device.getQueueFamilyIndex(releaseDestinationQueue);
        if(
            !ownerQueue.valid()
            || releaseDestinationFamily == VK_QUEUE_FAMILY_IGNORED
            || releaseDestinationFamily != destinationQueueFamily
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive texture handoff is not imported by its declared destination family"));
            return false;
        }

        if(sourceQueueFamily == VK_QUEUE_FAMILY_IGNORED || destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive texture handoff references an unavailable queue family"));
            return false;
        }
        if(sourceQueueFamily != destinationQueueFamily){
            acquireImageBarriers.push_back(VulkanStateTrackingDetail::BuildTextureOwnershipAcquireBarrier(
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
    const auto appendBufferAcquire = [&](Buffer& buffer, const ResourceStates::Mask state, const ResourceQueueSharing::Mask sharing, const GpuPhysicalQueueId ownerQueue, const GpuPhysicalQueueId releaseDestinationQueue) -> bool {
        if(sharing != buffer.m_desc.queueSharing){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Resource-state handoff buffer sharing contract does not match the resource description"));
            return false;
        }

        if(m_device.usesConcurrentQueueSharing(sharing)){
            if(ownerQueue.valid() || releaseDestinationQueue.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Concurrent buffer state handoff unexpectedly carries exclusive ownership"));
                return false;
            }
            return true;
        }

        if(ownerQueue.valid() && !m_device.matchesPhysicalQueueIdentity(ownerQueue)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Buffer state handoff has an invalid owner queue"));
            return false;
        }
        if(releaseDestinationQueue.valid() && !m_device.matchesPhysicalQueueIdentity(releaseDestinationQueue)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Buffer state handoff has an invalid ownership destination"));
            return false;
        }

        const u32 sourceQueueFamily = ownerQueue.valid()
            ? m_device.getQueueFamilyIndex(ownerQueue)
            : VK_QUEUE_FAMILY_IGNORED
        ;
        const u32 destinationQueueFamily = m_device.getQueueFamilyIndex(m_desc.physicalQueue);
        if(ownerQueue.valid() && (sourceQueueFamily == VK_QUEUE_FAMILY_IGNORED || destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive buffer handoff references an unavailable queue family"));
            return false;
        }

        if(!releaseDestinationQueue.valid()){
            // See the texture path above: one family may contain several physical queues, so a timeline wait is
            // sufficient for this state handoff and no queue-family ownership transfer is required.
            if(ownerQueue.valid() && sourceQueueFamily != destinationQueueFamily){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive buffer handoff changes queue family without a release/acquire transfer"));
                return false;
            }
            return true;
        }

        const u32 releaseDestinationFamily = m_device.getQueueFamilyIndex(releaseDestinationQueue);
        if(
            !ownerQueue.valid()
            || releaseDestinationFamily == VK_QUEUE_FAMILY_IGNORED
            || releaseDestinationFamily != destinationQueueFamily
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive buffer handoff is not imported by its declared destination family"));
            return false;
        }

        if(sourceQueueFamily == VK_QUEUE_FAMILY_IGNORED || destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Exclusive buffer handoff references an unavailable queue family"));
            return false;
        }
        if(sourceQueueFamily != destinationQueueFamily){
            acquireBufferBarriers.push_back(VulkanStateTrackingDetail::BuildBufferOwnershipAcquireBarrier(
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

        retainResource(state.texture);
        const TextureSubresourceStateKey key{ state.texture, state.mipLevel, state.arraySlice };
        m_stateTracker.m_textureStates.insert_or_assign(key, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_bufferStates, states.m_bufferStates.size());
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_bufferStates){
        if(!state.buffer)
            continue;

        if(!appendBufferAcquire(*state.buffer, state.state, state.queueSharing, state.ownerQueue, state.releaseDestinationQueue))
            return false;

        retainResource(state.buffer);
        m_stateTracker.m_bufferStates.insert_or_assign(state.buffer, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_permanentTextureStates, states.m_permanentTextureStates.size());
    for(const CommandListResourceStateHandoff::PermanentTextureState& state : states.m_permanentTextureStates){
        if(!state.texture)
            continue;

        if(!appendTextureAcquire(*state.texture, s_AllSubresources, state.state, state.queueSharing, state.ownerQueue, state.releaseDestinationQueue))
            return false;

        retainResource(state.texture);
        m_stateTracker.m_permanentTextureStates.insert_or_assign(state.texture, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_permanentBufferStates, states.m_permanentBufferStates.size());
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_permanentBufferStates){
        if(!state.buffer)
            continue;

        if(!appendBufferAcquire(*state.buffer, state.state, state.queueSharing, state.ownerQueue, state.releaseDestinationQueue))
            return false;

        retainResource(state.buffer);
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
    const auto getTextureOwnership = [&](const TextureSubresourceStateKey& key, const ResourceQueueSharing::Mask sharing, GpuPhysicalQueueId& outOwner, GpuPhysicalQueueId& outReleaseDestination){
        outOwner = {};
        outReleaseDestination = {};
        if(m_device.usesConcurrentQueueSharing(sharing))
            return;

        outOwner = m_desc.physicalQueue;
        const auto releaseIt = m_textureOwnershipReleaseDestinations.find(key);
        if(releaseIt != m_textureOwnershipReleaseDestinations.end())
            outReleaseDestination = releaseIt.value();
    };
    const auto getBufferOwnership = [&](Buffer* buffer, const ResourceQueueSharing::Mask sharing, GpuPhysicalQueueId& outOwner, GpuPhysicalQueueId& outReleaseDestination){
        outOwner = {};
        outReleaseDestination = {};
        if(m_device.usesConcurrentQueueSharing(sharing))
            return;

        outOwner = m_desc.physicalQueue;
        const auto releaseIt = m_bufferOwnershipReleaseDestinations.find(buffer);
        if(releaseIt != m_bufferOwnershipReleaseDestinations.end())
            outReleaseDestination = releaseIt.value();
    };

    states.m_textureStates.reserve(m_stateTracker.m_textureStates.size());
    for(auto it = m_stateTracker.m_textureStates.begin(); it != m_stateTracker.m_textureStates.end(); ++it){
        const TextureSubresourceStateKey& key = it->first;
        if(!key.texture)
            continue;

        GpuPhysicalQueueId ownerQueue;
        GpuPhysicalQueueId releaseDestinationQueue;
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

        GpuPhysicalQueueId ownerQueue;
        GpuPhysicalQueueId releaseDestinationQueue;
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

        GpuPhysicalQueueId ownerQueue;
        GpuPhysicalQueueId releaseDestinationQueue;
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

        GpuPhysicalQueueId ownerQueue;
        GpuPhysicalQueueId releaseDestinationQueue;
        getBufferOwnership(it->first, it->first->m_desc.queueSharing, ownerQueue, releaseDestinationQueue);
        states.m_permanentBufferStates.push_back(CommandListResourceStateHandoff::BufferState{
            it->first,
            it.value(),
            it->first->m_desc.queueSharing,
            ownerQueue,
            releaseDestinationQueue
        });
    }

    states.m_deviceGeneration = m_context.deviceGeneration;
    states.m_valid = true;
}

void CommandList::appendPendingOwnershipReleaseBarriers(){
    if(m_textureOwnershipReleaseDestinations.empty() && m_bufferOwnershipReleaseDestinations.empty())
        return;

    const u32 sourceQueueFamily = m_device.getQueueFamilyIndex(m_desc.physicalQueue);
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

        const GpuPhysicalQueueId destinationQueue = it.value();
        const u32 destinationQueueFamily = m_device.getQueueFamilyIndex(destinationQueue);
        if(destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release texture ownership to an unavailable destination queue family"));
            continue;
        }
        if(destinationQueueFamily == sourceQueueFamily)
            continue;

        const ResourceStates::Mask state = m_stateTracker.getTextureState(texture, key.arraySlice, key.mipLevel);
        m_pendingImageBarriers.push_back(VulkanStateTrackingDetail::BuildTextureOwnershipReleaseBarrier(
            texture->m_image,
            texture->m_aspectMask,
            TextureSubresourceSet(key.mipLevel, 1u, key.arraySlice, 1u),
            state,
            sourceQueueFamily,
            destinationQueueFamily,
            m_context.extensions.KHR_ray_tracing_pipeline
        ));
        retainResource(texture);
    }

    for(auto it = m_bufferOwnershipReleaseDestinations.begin(); it != m_bufferOwnershipReleaseDestinations.end(); ++it){
        Buffer* const buffer = it->first;
        if(!buffer)
            continue;
        if(m_device.usesConcurrentQueueSharing(buffer->m_desc.queueSharing)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Concurrent buffer unexpectedly has a pending ownership release"));
            continue;
        }

        const GpuPhysicalQueueId destinationQueue = it.value();
        const u32 destinationQueueFamily = m_device.getQueueFamilyIndex(destinationQueue);
        if(destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot release buffer ownership to an unavailable destination queue family"));
            continue;
        }
        if(destinationQueueFamily == sourceQueueFamily)
            continue;

        const ResourceStates::Mask state = m_stateTracker.getBufferState(buffer);
        m_pendingBufferBarriers.push_back(VulkanStateTrackingDetail::BuildBufferOwnershipReleaseBarrier(
            buffer->m_buffer,
            state,
            sourceQueueFamily,
            destinationQueueFamily,
            m_context.extensions.KHR_ray_tracing_pipeline
        ));
        retainResource(buffer);
    }

    commitBarriers();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

