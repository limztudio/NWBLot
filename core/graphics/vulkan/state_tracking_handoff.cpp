// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_image_policy.h"
#include "backend.h"
#include "buffer_resource_detail.h"
#include "state_tracking_detail.h"
#include "texture_resource_detail.h"

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
    GraphicsBufferStateTable requiredBufferStates{};
    if(!CollectGraphicsBufferStates(state, requiredBufferStates))
        return;

    for(u32 entryIndex = 0u; entryIndex < requiredBufferStates.count; ++entryIndex){
        auto& buffer = *MakeNotNull(requiredBufferStates.entries[entryIndex].buffer);
        setBufferStateValidated(buffer, requiredBufferStates.entries[entryIndex].state, false, BufferRange(0u, buffer.m_creationDesc.byteSize));
        if(m_commandRecordingFailed)
            return;
    }
}

b8 CommandList::importResourceStateHandoff(Alloc::ScratchArena& scratchArena, const CommandListResourceStateHandoff& states){
    if(!states.validForDeviceGeneration(m_context.deviceGeneration)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Resource-state handoff belongs to a retired device generation"));
        return false;
    }
    FOUNDATION_ASSERT(states.m_valid);
    if(states.empty())
        return true;

    for(const CommandListResourceStateHandoff::TextureState& state : states.m_textureStates){
        if(state.texture && (!VulkanTextureDetail::IsTextureResourceStateMaskValid(state.state) || !isTextureReadyForCommandQueue(state.texture, VulkanTextureDetail::TextureImagePolicy::RequiredImageUsageForResourceStates(state.state))))
            return false;
    }
    for(const CommandListResourceStateHandoff::PermanentTextureState& state : states.m_permanentTextureStates){
        if(state.texture && (!VulkanTextureDetail::IsTextureResourceStateMaskValid(state.state) || !isTextureReadyForCommandQueue(state.texture, VulkanTextureDetail::TextureImagePolicy::RequiredImageUsageForResourceStates(state.state))))
            return false;
    }
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_bufferStates){
        const BufferDesc* const description = state.buffer ? &state.buffer->getCreationDescription() : nullptr;
        if(
            state.buffer
            && (
                !VulkanBufferDetail::IsBufferResourceStateMaskValid(state.state)
                || !VulkanBufferDetail::IsBufferDescriptionCompatibleWithResourceStates(*description, state.state)
                || !isBufferReadyForCommandQueue(state.buffer, VulkanBufferDetail::RequiredBufferUsageForResourceStates(*description, state.state))
            )
        )
            return false;
    }
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_permanentBufferStates){
        const BufferDesc* const description = state.buffer ? &state.buffer->getCreationDescription() : nullptr;
        if(
            state.buffer
            && (
                !VulkanBufferDetail::IsBufferResourceStateMaskValid(state.state)
                || !VulkanBufferDetail::IsBufferDescriptionCompatibleWithResourceStates(*description, state.state)
                || !isBufferReadyForCommandQueue(state.buffer, VulkanBufferDetail::RequiredBufferUsageForResourceStates(*description, state.state))
            )
        )
            return false;
    }

    b8 bufferStatesSorted = true;
    const CommandListResourceStateHandoff::BufferState* previousBufferState = nullptr;
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_bufferStates){
        if(!state.buffer)
            continue;
        if(!VulkanStateTrackingDetail::IsBufferStateRangeValid(state.range, state.buffer->m_creationDesc)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Imported buffer byte range is empty or invalid"));
            return false;
        }
        if(previousBufferState){
            if(previousBufferState->buffer == state.buffer){
                if(state.range.byteOffset < previousBufferState->range.byteOffset)
                    bufferStatesSorted = false;
                else if(previousBufferState->range.overlaps(state.range)){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Imported buffer state contains overlapping byte intervals"));
                    return false;
                }
            }
            else if(!LessThan<Buffer*>()(previousBufferState->buffer, state.buffer))
                bufferStatesSorted = false;
        }
        previousBufferState = &state;
    }
    if(!bufferStatesSorted){
        Vector<NotNull<const CommandListResourceStateHandoff::BufferState*>, Alloc::ScratchArena> orderedBufferStates{scratchArena};
        orderedBufferStates.reserve(states.m_bufferStates.size());
        for(const CommandListResourceStateHandoff::BufferState& state : states.m_bufferStates){
            if(state.buffer)
                orderedBufferStates.push_back(MakeNotNull(&state));
        }
        Sort(orderedBufferStates.begin(), orderedBufferStates.end(), [](const auto lhs, const auto rhs)noexcept{
            return lhs->buffer != rhs->buffer ? LessThan<Buffer*>()(lhs->buffer, rhs->buffer) : lhs->range.byteOffset < rhs->range.byteOffset;
        });
        for(usize index = 1u; index < orderedBufferStates.size(); ++index){
            const auto& previous = *orderedBufferStates[index - 1u];
            const auto& current = *orderedBufferStates[index];
            if(previous.buffer == current.buffer && previous.range.overlaps(current.range)){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Imported buffer state contains overlapping byte intervals"));
                return false;
            }
        }
    }
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_permanentBufferStates){
        if(state.buffer && !state.range.isEntireBuffer(state.buffer->m_creationDesc)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Permanent buffer state must cover the entire buffer"));
            return false;
        }
    }

    Vector<VkImageMemoryBarrier2, Alloc::ScratchArena> acquireImageBarriers{scratchArena};
    Vector<VkBufferMemoryBarrier2, Alloc::ScratchArena> acquireBufferBarriers{scratchArena};

    const auto appendTextureAcquire = [&](
        Texture& texture,
        const TextureSubresourceSet& subresources,
        const ResourceStates::Mask state,
        const ResourceQueueSharing::Mask sharing,
        const GpuPhysicalQueueId ownerQueue,
        const GpuPhysicalQueueId releaseDestinationQueue
    ) -> b8 {
        if(sharing != texture.m_creationDesc.queueSharing){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Resource-state handoff texture sharing contract does not match the resource description"));
            return false;
        }

        if(texture.m_imageInfo.sharingMode == VK_SHARING_MODE_CONCURRENT){
            if(ownerQueue.valid() || releaseDestinationQueue.valid()){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Concurrent texture state handoff unexpectedly carries exclusive ownership"));
                return false;
            }
            return true;
        }

        if(ownerQueue.valid() && !m_device.matchesPhysicalQueueIdentity(ownerQueue)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Texture state handoff has an invalid owner queue"));
            return false;
        }
        if(releaseDestinationQueue.valid() && !m_device.matchesPhysicalQueueIdentity(releaseDestinationQueue)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Texture state handoff has an invalid ownership destination"));
            return false;
        }

        const u32 sourceQueueFamily = ownerQueue.valid() ? m_device.getQueueFamilyIndex(ownerQueue) : VK_QUEUE_FAMILY_IGNORED;
        const u32 destinationQueueFamily = m_hasCachedPhysicalQueueInfo ? m_cachedPhysicalQueueInfo.familyIndex : VK_QUEUE_FAMILY_IGNORED;
        if(ownerQueue.valid() && (sourceQueueFamily == VK_QUEUE_FAMILY_IGNORED || destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Exclusive texture handoff references an unavailable queue family"));
            return false;
        }

        if(!releaseDestinationQueue.valid()){
            if(ownerQueue.valid() && sourceQueueFamily != destinationQueueFamily){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Exclusive texture handoff changes queue family without a release/acquire transfer"));
                return false;
            }
            return true;
        }

        const u32 releaseDestinationFamily = m_device.getQueueFamilyIndex(releaseDestinationQueue);
        if(!ownerQueue.valid() || releaseDestinationFamily == VK_QUEUE_FAMILY_IGNORED || releaseDestinationFamily != destinationQueueFamily){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Exclusive texture handoff is not imported by its declared destination family"));
            return false;
        }

        if(sourceQueueFamily != destinationQueueFamily){
            acquireImageBarriers.push_back(VulkanStateTrackingDetail::BuildTextureOwnershipAcquireBarrier(texture.m_image, texture.m_aspectMask, subresources, state, sourceQueueFamily, destinationQueueFamily, m_context.extensions.khrRayTracingPipeline));
        }
        return true;
    };
    const auto appendBufferAcquire = [&](Buffer& buffer, const BufferRange range, const ResourceStates::Mask state, const ResourceQueueSharing::Mask sharing, const GpuPhysicalQueueId ownerQueue, const GpuPhysicalQueueId releaseDestinationQueue) -> b8 {
        if(sharing != buffer.m_creationDesc.queueSharing){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Resource-state handoff buffer sharing contract does not match the resource description"));
            return false;
        }

        if(buffer.m_bufferInfo.sharingMode == VK_SHARING_MODE_CONCURRENT){
            if(ownerQueue.valid() || releaseDestinationQueue.valid()){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Concurrent buffer state handoff unexpectedly carries exclusive ownership"));
                return false;
            }
            return true;
        }

        if(ownerQueue.valid() && !m_device.matchesPhysicalQueueIdentity(ownerQueue)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Buffer state handoff has an invalid owner queue"));
            return false;
        }
        if(releaseDestinationQueue.valid() && !m_device.matchesPhysicalQueueIdentity(releaseDestinationQueue)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Buffer state handoff has an invalid ownership destination"));
            return false;
        }

        const u32 sourceQueueFamily = ownerQueue.valid() ? m_device.getQueueFamilyIndex(ownerQueue) : VK_QUEUE_FAMILY_IGNORED;
        const u32 destinationQueueFamily = m_hasCachedPhysicalQueueInfo ? m_cachedPhysicalQueueInfo.familyIndex : VK_QUEUE_FAMILY_IGNORED;
        if(ownerQueue.valid() && (sourceQueueFamily == VK_QUEUE_FAMILY_IGNORED || destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Exclusive buffer handoff references an unavailable queue family"));
            return false;
        }

        if(!releaseDestinationQueue.valid()){
            if(ownerQueue.valid() && sourceQueueFamily != destinationQueueFamily){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Exclusive buffer handoff changes queue family without a release/acquire transfer"));
                return false;
            }
            return true;
        }

        const u32 releaseDestinationFamily = m_device.getQueueFamilyIndex(releaseDestinationQueue);
        if(!ownerQueue.valid() || releaseDestinationFamily == VK_QUEUE_FAMILY_IGNORED || releaseDestinationFamily != destinationQueueFamily){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Exclusive buffer handoff is not imported by its declared destination family"));
            return false;
        }

        if(sourceQueueFamily != destinationQueueFamily){
            acquireBufferBarriers.push_back(VulkanStateTrackingDetail::BuildBufferOwnershipAcquireBarrier(buffer.m_buffer, state, sourceQueueFamily, destinationQueueFamily, m_context.extensions.khrRayTracingPipeline, range));
        }
        return true;
    };

    HashMap<NotNull<Texture*>, ResourceStates::Mask, Alloc::ScratchArena, Hasher<NotNull<Texture*>>, EqualTo<NotNull<Texture*>>> importedPermanentTextures(
        0u,
        Hasher<NotNull<Texture*>>(),
        EqualTo<NotNull<Texture*>>(),
        scratchArena
    );
    Texture* singlePermanentTexture = nullptr;
    ResourceStates::Mask singlePermanentTextureState = ResourceStates::Unknown;
    if(states.m_permanentTextureStates.size() > 1u){
        ::ContainerDetail::ReserveAdditionalCapacity(importedPermanentTextures, states.m_permanentTextureStates.size());
        for(const CommandListResourceStateHandoff::PermanentTextureState& state : states.m_permanentTextureStates){
            if(!state.texture)
                continue;
            const auto [it, inserted] = importedPermanentTextures.emplace(MakeNotNull(state.texture), state.state);
            if(!inserted && it.value() != state.state){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Resource-state handoff contains conflicting permanent texture states"));
                return false;
            }
        }
    }
    else if(states.m_permanentTextureStates.size() == 1u && states.m_permanentTextureStates[0u].texture){
        singlePermanentTexture = states.m_permanentTextureStates[0u].texture;
        singlePermanentTextureState = states.m_permanentTextureStates[0u].state;
    }

    HashMap<NotNull<Buffer*>, ResourceStates::Mask, Alloc::ScratchArena, Hasher<NotNull<Buffer*>>, EqualTo<NotNull<Buffer*>>> importedPermanentBuffers(
        0u,
        Hasher<NotNull<Buffer*>>(),
        EqualTo<NotNull<Buffer*>>(),
        scratchArena
    );
    Buffer* singlePermanentBuffer = nullptr;
    ResourceStates::Mask singlePermanentBufferState = ResourceStates::Unknown;
    if(states.m_permanentBufferStates.size() > 1u){
        ::ContainerDetail::ReserveAdditionalCapacity(importedPermanentBuffers, states.m_permanentBufferStates.size());
        for(const CommandListResourceStateHandoff::BufferState& state : states.m_permanentBufferStates){
            if(!state.buffer)
                continue;
            const auto [it, inserted] = importedPermanentBuffers.emplace(MakeNotNull(state.buffer), state.state);
            if(!inserted && it.value() != state.state){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Resource-state handoff contains conflicting permanent buffer states"));
                return false;
            }
        }
    }
    else if(states.m_permanentBufferStates.size() == 1u && states.m_permanentBufferStates[0u].buffer){
        singlePermanentBuffer = states.m_permanentBufferStates[0u].buffer;
        singlePermanentBufferState = states.m_permanentBufferStates[0u].state;
    }

    for(const CommandListResourceStateHandoff::TextureState& state : states.m_textureStates){
        if(!state.texture)
            continue;

        const ResourceStates::Mask existingPermanentState = m_stateTracker.getPermanentTextureState(state.texture);
        if(existingPermanentState != ResourceStates::Unknown && existingPermanentState != state.state){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Imported texture state conflicts with an existing permanent state"));
            return false;
        }
        if(singlePermanentTexture == state.texture){
            if(singlePermanentTextureState != state.state){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Imported transient texture state conflicts with its permanent state"));
                return false;
            }
        }
        else if(!importedPermanentTextures.empty()){
            const auto permFound = importedPermanentTextures.find(MakeNotNull(state.texture));
            if(permFound != importedPermanentTextures.end() && permFound.value() != state.state){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Imported transient texture state conflicts with its permanent state"));
                return false;
            }
        }

        if(!appendTextureAcquire(*state.texture, TextureSubresourceSet(state.mipLevel, 1u, state.arraySlice, 1u), state.state, state.queueSharing, state.ownerQueue, state.releaseDestinationQueue))
            return false;
    }

    for(const CommandListResourceStateHandoff::BufferState& state : states.m_bufferStates){
        if(!state.buffer)
            continue;

        const ResourceStates::Mask existingPermanentState = m_stateTracker.getPermanentBufferState(state.buffer);
        if(existingPermanentState != ResourceStates::Unknown && existingPermanentState != state.state){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Imported buffer state conflicts with an existing permanent state"));
            return false;
        }
        if(singlePermanentBuffer == state.buffer){
            if(singlePermanentBufferState != state.state){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Imported transient buffer state conflicts with its permanent state"));
                return false;
            }
        }
        else if(!importedPermanentBuffers.empty()){
            const auto permFound = importedPermanentBuffers.find(MakeNotNull(state.buffer));
            if(permFound != importedPermanentBuffers.end() && permFound.value() != state.state){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Imported transient buffer state conflicts with its permanent state"));
                return false;
            }
        }

        if(!appendBufferAcquire(*state.buffer, state.range.resolve(state.buffer->m_creationDesc), state.state, state.queueSharing, state.ownerQueue, state.releaseDestinationQueue))
            return false;
    }

    for(const CommandListResourceStateHandoff::PermanentTextureState& state : states.m_permanentTextureStates){
        if(!state.texture)
            continue;

        const ResourceStates::Mask existingPermanentState = m_stateTracker.getPermanentTextureState(state.texture);
        if(existingPermanentState != ResourceStates::Unknown && existingPermanentState != state.state){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Imported permanent texture state conflicts with the command-list contract"));
            return false;
        }

        if(!appendTextureAcquire(*state.texture, s_AllSubresources, state.state, state.queueSharing, state.ownerQueue, state.releaseDestinationQueue))
            return false;
    }

    for(const CommandListResourceStateHandoff::BufferState& state : states.m_permanentBufferStates){
        if(!state.buffer)
            continue;

        const ResourceStates::Mask existingPermanentState = m_stateTracker.getPermanentBufferState(state.buffer);
        if(existingPermanentState != ResourceStates::Unknown && existingPermanentState != state.state){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Vulkan: Imported permanent buffer state conflicts with the command-list contract"));
            return false;
        }

        if(!appendBufferAcquire(*state.buffer, state.range.resolve(state.buffer->m_creationDesc), state.state, state.queueSharing, state.ownerQueue, state.releaseDestinationQueue))
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

    m_stateTracker.invalidateBufferCache();
    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_bufferStates, states.m_bufferStates.size());
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_bufferStates){
        if(!state.buffer)
            continue;

        retainResource(state.buffer);
        m_stateTracker.beginTrackingTransientBuffer(*state.buffer, state.state, state.range);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_permanentTextureStates, states.m_permanentTextureStates.size());
    for(const CommandListResourceStateHandoff::PermanentTextureState& state : states.m_permanentTextureStates){
        if(!state.texture)
            continue;

        retainResource(state.texture);
        m_stateTracker.setPermanentTextureState(*state.texture, state.state);
    }

    ::ContainerDetail::ReserveAdditionalCapacity(m_stateTracker.m_permanentBufferStates, states.m_permanentBufferStates.size());
    for(const CommandListResourceStateHandoff::BufferState& state : states.m_permanentBufferStates){
        if(!state.buffer)
            continue;

        retainResource(state.buffer);
        m_stateTracker.setPermanentBufferState(*state.buffer, state.state);
    }

    if(!acquireImageBarriers.empty() || !acquireBufferBarriers.empty()){
        executePipelineBarrier({}, { acquireImageBarriers.data(), acquireImageBarriers.size() }, { acquireBufferBarriers.data(), acquireBufferBarriers.size() });
        if(m_commandRecordingFailed)
            return false;
    }

    return true;
}

void CommandList::exportResourceStateHandoff(CommandListResourceStateHandoff& states)const{
    states.reset();
    struct QueueOwnership{
        GpuPhysicalQueueId owner;
        GpuPhysicalQueueId releaseDestination;
    };
    const auto getTextureOwnership = [&](const TextureSubresourceStateKey& key)noexcept -> QueueOwnership{
        if(key.texture->m_imageInfo.sharingMode == VK_SHARING_MODE_CONCURRENT)
            return {};

        const auto release = m_textureOwnershipReleaseDestinations.find(key);
        return QueueOwnership{ m_creationDesc.physicalQueue, release != m_textureOwnershipReleaseDestinations.end() ? release.value() : GpuPhysicalQueueId{} };
    };
    const auto getBufferOwnership = [&](const Buffer& buffer)noexcept -> GpuPhysicalQueueId{
        return buffer.m_bufferInfo.sharingMode != VK_SHARING_MODE_CONCURRENT ? m_creationDesc.physicalQueue : GpuPhysicalQueueId{};
    };

    states.m_textureStates.reserve(m_stateTracker.m_textureStates.size());
    for(auto it = m_stateTracker.m_textureStates.begin(); it != m_stateTracker.m_textureStates.end(); ++it){
        const TextureSubresourceStateKey& key = it->first;
        FOUNDATION_ASSERT(key.texture);

        const auto [ownerQueue, releaseDestinationQueue] = getTextureOwnership(key);
        states.m_textureStates.push_back(CommandListResourceStateHandoff::TextureState{ key.texture, ownerQueue, releaseDestinationQueue, it.value(), key.texture->m_creationDesc.queueSharing, key.mipLevel, key.arraySlice });
    }

    states.m_bufferStates.reserve(m_stateTracker.m_bufferStates.size());
    for(auto it = m_stateTracker.m_bufferStates.begin(); it != m_stateTracker.m_bufferStates.end(); ++it){
        auto& buffer = *MakeNotNull(it->first.get());
        const GpuPhysicalQueueId ownerQueue = getBufferOwnership(buffer);
        const auto releases = m_bufferOwnershipReleaseDestinations.find(MakeNotNull(&buffer));
        const auto append = [&](const BufferRange range, const ResourceStates::Mask state, const GpuPhysicalQueueId destination){
            if(range.hasExtent()){
                states.m_bufferStates.push_back(CommandListResourceStateHandoff::BufferState{ &buffer, range, ownerQueue, destination, state, buffer.m_creationDesc.queueSharing });
            }
        };
        for(const StateTracker::BufferRangeState& state : it.value()){
            u64 cursor = state.range.byteOffset;
            if(releases != m_bufferOwnershipReleaseDestinations.end()){
                for(const BufferOwnershipRelease& release : releases.value()){
                    const BufferRange overlap = state.range.intersect(release.range);
                    if(!overlap.hasExtent())
                        continue;
                    if(cursor < overlap.byteOffset)
                        append(BufferRange(cursor, overlap.byteOffset - cursor), state.state, {});
                    append(overlap, state.state, release.destinationQueue);
                    cursor = overlap.end();
                }
            }
            if(cursor < state.range.end())
                append(BufferRange(cursor, state.range.end() - cursor), state.state, {});
        }
    }

    states.m_permanentTextureStates.reserve(m_stateTracker.m_permanentTextureStates.size());
    for(auto it = m_stateTracker.m_permanentTextureStates.begin(); it != m_stateTracker.m_permanentTextureStates.end(); ++it){
        auto& texture = *MakeNotNull(it.value().texture.get());

        const TextureSubresourceStateKey key{ &texture, 0u, 0u };
        const auto [ownerQueue, releaseDestinationQueue] = getTextureOwnership(key);
        states.m_permanentTextureStates.push_back(CommandListResourceStateHandoff::PermanentTextureState{ &texture, ownerQueue, releaseDestinationQueue, it.value().state, texture.m_creationDesc.queueSharing });
    }

    states.m_permanentBufferStates.reserve(m_stateTracker.m_permanentBufferStates.size());
    for(auto it = m_stateTracker.m_permanentBufferStates.begin(); it != m_stateTracker.m_permanentBufferStates.end(); ++it){
        auto& buffer = *MakeNotNull(it.value().buffer.get());

        const GpuPhysicalQueueId ownerQueue = getBufferOwnership(buffer);
        states.m_permanentBufferStates.push_back(CommandListResourceStateHandoff::BufferState{ &buffer, s_EntireBuffer, ownerQueue, {}, it.value().state, buffer.m_creationDesc.queueSharing });
    }

    states.m_deviceGeneration = m_context.deviceGeneration;
    states.m_valid = true;
}

void CommandList::appendPendingOwnershipReleaseBarriers(){
    if(!validateCommandRecordingScope(s_OwnershipReleaseBarriersOperation))
        return;

    if(m_textureOwnershipReleaseDestinations.empty() && m_bufferOwnershipReleaseDestinations.empty())
        return;

    const u32 sourceQueueFamily = m_hasCachedPhysicalQueueInfo ? m_cachedPhysicalQueueInfo.familyIndex : VK_QUEUE_FAMILY_IGNORED;
    if(sourceQueueFamily == VK_QUEUE_FAMILY_IGNORED){
        rejectCommandRecording(s_OwnershipReleaseBarriersOperation, FOUNDATION_TEXT("source queue family is unavailable"));
        return;
    }

    for(auto it = m_bufferOwnershipReleaseDestinations.begin(); it != m_bufferOwnershipReleaseDestinations.end(); ++it){
        auto& buffer = *MakeNotNull(it->first.get());
        if(buffer.m_bufferInfo.sharingMode == VK_SHARING_MODE_CONCURRENT || m_stateTracker.isPermanentBuffer(buffer)){
            rejectCommandRecording(s_OwnershipReleaseBarriersOperation, FOUNDATION_TEXT("pending buffer release has an invalid resource contract"));
            return;
        }
        const auto tracked = m_stateTracker.m_bufferStates.find(MakeNotNull(&buffer));
        for(const BufferOwnershipRelease& release : it.value()){
            if(m_device.getQueueFamilyIndex(release.destinationQueue) == VK_QUEUE_FAMILY_IGNORED){
                rejectCommandRecording(s_OwnershipReleaseBarriersOperation, FOUNDATION_TEXT("buffer destination queue family is unavailable"));
                return;
            }
            if(!m_stateTracker.hasExplicitBufferState(&buffer, release.range)){
                rejectCommandRecording(s_OwnershipReleaseBarriersOperation, FOUNDATION_TEXT("buffer final byte range state is unknown"));
                return;
            }
            for(const StateTracker::BufferRangeState& state : tracked.value()){
                if(!state.range.overlaps(release.range))
                    continue;
                if(state.state == ResourceStates::Unknown || !validateBufferForGpuState(&buffer, state.state, s_OwnershipReleaseBarriersOperation)){
                    rejectCommandRecording(s_OwnershipReleaseBarriersOperation, FOUNDATION_TEXT("buffer byte range state is incompatible with its creation contract"));
                    return;
                }
            }
        }
    }

    for(auto it = m_textureOwnershipReleaseDestinations.begin(); it != m_textureOwnershipReleaseDestinations.end(); ++it){
        const TextureSubresourceStateKey& key = it->first;
        auto& texture = *MakeNotNull(key.texture);
        if(texture.m_imageInfo.sharingMode == VK_SHARING_MODE_CONCURRENT){
            rejectCommandRecording(s_OwnershipReleaseBarriersOperation, FOUNDATION_TEXT("concurrent texture has a pending exclusive release"));
            return;
        }
        if(m_stateTracker.isPermanentTexture(texture)){
            rejectCommandRecording(s_OwnershipReleaseBarriersOperation, FOUNDATION_TEXT("permanent texture has a pending ownership release"));
            return;
        }

        const GpuPhysicalQueueId destinationQueue = it.value();
        const u32 destinationQueueFamily = m_device.getQueueFamilyIndex(destinationQueue);
        if(destinationQueueFamily == VK_QUEUE_FAMILY_IGNORED){
            rejectCommandRecording(s_OwnershipReleaseBarriersOperation, FOUNDATION_TEXT("texture destination queue family is unavailable"));
            return;
        }

        const ResourceStates::Mask state = m_stateTracker.getTextureState(&texture, key.arraySlice, key.mipLevel);
        if(state == ResourceStates::Unknown){
            rejectCommandRecording(s_OwnershipReleaseBarriersOperation, FOUNDATION_TEXT("texture final state is unknown"));
            return;
        }
        if(destinationQueueFamily == sourceQueueFamily)
            continue;

        m_pendingImageBarriers.push_back(VulkanStateTrackingDetail::BuildTextureOwnershipReleaseBarrier(
            texture.m_image,
            texture.m_aspectMask,
            TextureSubresourceSet(key.mipLevel, 1u, key.arraySlice, 1u),
            state,
            sourceQueueFamily,
            destinationQueueFamily,
            m_context.extensions.khrRayTracingPipeline
        ));
        retainResource(&texture);
    }

    for(auto it = m_bufferOwnershipReleaseDestinations.begin(); it != m_bufferOwnershipReleaseDestinations.end(); ++it){
        auto& buffer = *MakeNotNull(it->first.get());
        const auto tracked = m_stateTracker.m_bufferStates.find(MakeNotNull(&buffer));
        for(const BufferOwnershipRelease& release : it.value()){
            const u32 destinationQueueFamily = m_device.getQueueFamilyIndex(release.destinationQueue);
            if(destinationQueueFamily == sourceQueueFamily)
                continue;
            for(const StateTracker::BufferRangeState& state : tracked.value()){
                const BufferRange overlap = state.range.intersect(release.range);
                if(!overlap.hasExtent())
                    continue;
                m_pendingBufferBarriers.push_back(VulkanStateTrackingDetail::BuildBufferOwnershipReleaseBarrier(buffer.m_buffer, state.state, sourceQueueFamily, destinationQueueFamily, m_context.extensions.khrRayTracingPipeline, overlap));
            }
        }
        retainResource(&buffer);
    }

    commitBarriers();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

