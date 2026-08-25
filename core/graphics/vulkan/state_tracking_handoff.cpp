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
    struct BufferStateEntry{
        Buffer* buffer = nullptr;
        ResourceStates::Mask state = ResourceStates::Unknown;
    };
    BufferStateEntry entries[s_MaxVertexAttributes + 2u]{};
    u32 entryCount = 0u;

    const auto addRequiredState = [&entries, &entryCount](
        Buffer* const buffer,
        const ResourceStates::Mask requiredState
    )noexcept{
        if(!buffer)
            return;
        for(u32 entryIndex = 0u; entryIndex < entryCount; ++entryIndex){
            if(entries[entryIndex].buffer == buffer){
                entries[entryIndex].state |= requiredState;
                return;
            }
        }
        NWB_ASSERT(entryCount < LengthOf(entries));
        entries[entryCount].buffer = buffer;
        entries[entryCount].state = requiredState;
        ++entryCount;
    };

    for(const VertexBufferBinding& binding : state.vertexBuffers)
        addRequiredState(binding.buffer, ResourceStates::VertexBuffer);
    addRequiredState(state.indexBuffer.buffer, ResourceStates::IndexBuffer);
    addRequiredState(state.indirectParams, ResourceStates::IndirectArgument);

    for(u32 entryIndex = 0u; entryIndex < entryCount; ++entryIndex){
        setBufferState(entries[entryIndex].buffer, entries[entryIndex].state);
        if(m_commandRecordingFailed)
            return;
    }
}

bool CommandList::importResourceStateHandoff(const CommandListResourceStateHandoff& states){
    if(!states.validForDeviceGeneration(m_context.deviceGeneration)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Resource-state handoff belongs to a retired device generation"));
        return false;
    }
    NWB_ASSERT(states.m_valid);

    for(const CommandListResourceStateHandoff::BufferState& state : states.m_bufferStates){
        if(
            state.buffer
            && !m_device.isBufferReadyForGpuUse(state.buffer)
        )
            return false;
    }
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_permanentBufferStates){
        if(
            state.buffer
            && !m_device.isBufferReadyForGpuUse(state.buffer)
        )
            return false;
    }

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

    // Validate the complete handoff before publishing any state. Permanent state survives clearState(), so a
    // progressive import would otherwise poison later recording attempts when a later entry is incompatible.
    for(const CommandListResourceStateHandoff::TextureState& state : states.m_textureStates){
        if(!state.texture)
            continue;

        const ResourceStates::Mask existingPermanentState = m_stateTracker.getPermanentTextureState(state.texture);
        if(existingPermanentState != ResourceStates::Unknown && existingPermanentState != state.state){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Imported texture state conflicts with an existing permanent state"));
            return false;
        }

        if(!appendTextureAcquire(
            *state.texture,
            TextureSubresourceSet(state.mipLevel, 1u, state.arraySlice, 1u),
            state.state,
            state.queueSharing,
            state.ownerQueue,
            state.releaseDestinationQueue
        ))
            return false;
    }

    for(const CommandListResourceStateHandoff::BufferState& state : states.m_bufferStates){
        if(!state.buffer)
            continue;

        const ResourceStates::Mask existingPermanentState = m_stateTracker.getPermanentBufferState(state.buffer);
        if(existingPermanentState != ResourceStates::Unknown && existingPermanentState != state.state){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Imported buffer state conflicts with an existing permanent state"));
            return false;
        }

        if(!appendBufferAcquire(
            *state.buffer,
            state.state,
            state.queueSharing,
            state.ownerQueue,
            state.releaseDestinationQueue
        ))
            return false;
    }

    for(const CommandListResourceStateHandoff::PermanentTextureState& state : states.m_permanentTextureStates){
        if(!state.texture)
            continue;

        const ResourceStates::Mask existingPermanentState = m_stateTracker.getPermanentTextureState(state.texture);
        if(existingPermanentState != ResourceStates::Unknown && existingPermanentState != state.state){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Imported permanent texture state conflicts with the command-list contract"));
            return false;
        }
        for(const CommandListResourceStateHandoff::TextureState& transientState : states.m_textureStates){
            if(transientState.texture == state.texture && transientState.state != state.state){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Imported transient texture state conflicts with its permanent state"));
                return false;
            }
        }
        for(const CommandListResourceStateHandoff::PermanentTextureState& otherState : states.m_permanentTextureStates){
            if(otherState.texture == state.texture && otherState.state != state.state){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Resource-state handoff contains conflicting permanent texture states"));
                return false;
            }
        }

        if(!appendTextureAcquire(
            *state.texture,
            s_AllSubresources,
            state.state,
            state.queueSharing,
            state.ownerQueue,
            state.releaseDestinationQueue
        ))
            return false;
    }

    for(const CommandListResourceStateHandoff::BufferState& state : states.m_permanentBufferStates){
        if(!state.buffer)
            continue;

        const ResourceStates::Mask existingPermanentState = m_stateTracker.getPermanentBufferState(state.buffer);
        if(existingPermanentState != ResourceStates::Unknown && existingPermanentState != state.state){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Imported permanent buffer state conflicts with the command-list contract"));
            return false;
        }
        for(const CommandListResourceStateHandoff::BufferState& transientState : states.m_bufferStates){
            if(transientState.buffer == state.buffer && transientState.state != state.state){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Imported transient buffer state conflicts with its permanent state"));
                return false;
            }
        }
        for(const CommandListResourceStateHandoff::BufferState& otherState : states.m_permanentBufferStates){
            if(otherState.buffer == state.buffer && otherState.state != state.state){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Resource-state handoff contains conflicting permanent buffer states"));
                return false;
            }
        }

        if(!appendBufferAcquire(
            *state.buffer,
            state.state,
            state.queueSharing,
            state.ownerQueue,
            state.releaseDestinationQueue
        ))
            return false;
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_textureStates, states.m_textureStates.size());
    for(const CommandListResourceStateHandoff::TextureState& state : states.m_textureStates){
        if(!state.texture)
            continue;

        retainResource(state.texture);
        const TextureSubresourceStateKey key{ state.texture, state.mipLevel, state.arraySlice };
        m_stateTracker.m_textureStates.insert_or_assign(key, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_bufferStates, states.m_bufferStates.size());
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_bufferStates){
        if(!state.buffer)
            continue;

        retainResource(state.buffer);
        m_stateTracker.m_bufferStates.insert_or_assign(state.buffer, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_permanentTextureStates, states.m_permanentTextureStates.size());
    for(const CommandListResourceStateHandoff::PermanentTextureState& state : states.m_permanentTextureStates){
        if(!state.texture)
            continue;

        retainResource(state.texture);
        m_stateTracker.m_permanentTextureStates.insert_or_assign(state.texture, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_permanentBufferStates, states.m_permanentBufferStates.size());
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_permanentBufferStates){
        if(!state.buffer)
            continue;

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
        if(m_commandRecordingFailed)
            return false;
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
    if(!validateCommandRecordingScope(NWB_TEXT("append ownership-release barriers")))
        return;

    if(m_textureOwnershipReleaseDestinations.empty() && m_bufferOwnershipReleaseDestinations.empty())
        return;

    const u32 sourceQueueFamily = m_device.getQueueFamilyIndex(m_desc.physicalQueue);
    if(sourceQueueFamily == VK_QUEUE_FAMILY_IGNORED){
        rejectCommandRecording(NWB_TEXT("append ownership-release barriers"), NWB_TEXT("source queue family is unavailable"));
        return;
    }

    for(auto it = m_textureOwnershipReleaseDestinations.begin(); it != m_textureOwnershipReleaseDestinations.end(); ++it){
        const TextureSubresourceStateKey& key = it->first;
        Texture* const texture = key.texture;
        if(!texture){
            rejectCommandRecording(
                NWB_TEXT("append ownership-release barriers"),
                NWB_TEXT("pending texture release has no resource")
            );
            return;
        }
        if(m_device.usesConcurrentQueueSharing(texture->m_desc.queueSharing)){
            rejectCommandRecording(
                NWB_TEXT("append ownership-release barriers"),
                NWB_TEXT("concurrent texture has a pending exclusive release")
            );
            return;
        }
        if(m_stateTracker.isPermanentTexture(*texture)){
            rejectCommandRecording(
                NWB_TEXT("append ownership-release barriers"),
                NWB_TEXT("permanent texture has a pending ownership release")
            );
            return;
        }

        const GpuPhysicalQueueId destinationQueue = it.value();
        const u32 destinationQueueFamily = m_device.getQueueFamilyIndex(destinationQueue);
        if(destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED){
            rejectCommandRecording(
                NWB_TEXT("append ownership-release barriers"),
                NWB_TEXT("texture destination queue family is unavailable")
            );
            return;
        }

        const ResourceStates::Mask state = m_stateTracker.getTextureState(texture, key.arraySlice, key.mipLevel);
        if(state == ResourceStates::Unknown){
            rejectCommandRecording(NWB_TEXT("append ownership-release barriers"), NWB_TEXT("texture final state is unknown"));
            return;
        }
        if(destinationQueueFamily == sourceQueueFamily)
            continue;

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
        if(!buffer){
            rejectCommandRecording(
                NWB_TEXT("append ownership-release barriers"),
                NWB_TEXT("pending buffer release has no resource")
            );
            return;
        }
        if(m_device.usesConcurrentQueueSharing(buffer->m_desc.queueSharing)){
            rejectCommandRecording(
                NWB_TEXT("append ownership-release barriers"),
                NWB_TEXT("concurrent buffer has a pending exclusive release")
            );
            return;
        }
        if(m_stateTracker.isPermanentBuffer(*buffer)){
            rejectCommandRecording(
                NWB_TEXT("append ownership-release barriers"),
                NWB_TEXT("permanent buffer has a pending ownership release")
            );
            return;
        }

        const GpuPhysicalQueueId destinationQueue = it.value();
        const u32 destinationQueueFamily = m_device.getQueueFamilyIndex(destinationQueue);
        if(destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED){
            rejectCommandRecording(
                NWB_TEXT("append ownership-release barriers"),
                NWB_TEXT("buffer destination queue family is unavailable")
            );
            return;
        }

        const ResourceStates::Mask state = m_stateTracker.getBufferState(buffer);
        if(state == ResourceStates::Unknown){
            rejectCommandRecording(NWB_TEXT("append ownership-release barriers"), NWB_TEXT("buffer final state is unknown"));
            return;
        }
        if(destinationQueueFamily == sourceQueueFamily)
            continue;

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

