// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/vulkan/buffer_resource_detail.h>
#include <core/graphics/vulkan/texture_resource_detail.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_preflight{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static GpuGraphResourceType::Enum BarrierResourceType(
    const GpuCompiledBarrierType::Enum type
)noexcept{
    switch(type){
    case GpuCompiledBarrierType::TextureTransition:
    case GpuCompiledBarrierType::TextureUav:
    case GpuCompiledBarrierType::TextureOwnershipRelease:
    case GpuCompiledBarrierType::TextureOwnershipAcquire:
    case GpuCompiledBarrierType::TextureStateExport:
        return GpuGraphResourceType::Texture;
    case GpuCompiledBarrierType::BufferTransition:
    case GpuCompiledBarrierType::BufferUav:
    case GpuCompiledBarrierType::BufferOwnershipRelease:
    case GpuCompiledBarrierType::BufferOwnershipAcquire:
    case GpuCompiledBarrierType::BufferStateExport:
        return GpuGraphResourceType::Buffer;
    case GpuCompiledBarrierType::AccelStructTransition:
    case GpuCompiledBarrierType::AccelStructUav:
    case GpuCompiledBarrierType::AccelStructOwnershipRelease:
    case GpuCompiledBarrierType::AccelStructOwnershipAcquire:
    case GpuCompiledBarrierType::AccelStructStateExport:
        return GpuGraphResourceType::AccelStruct;
    default:
        return GpuGraphResourceType::HazardDomain;
    }
}

[[nodiscard]] static bool IsOwnershipRelease(const GpuCompiledBarrierType::Enum type)noexcept{
    return type == GpuCompiledBarrierType::TextureOwnershipRelease
        || type == GpuCompiledBarrierType::BufferOwnershipRelease
        || type == GpuCompiledBarrierType::AccelStructOwnershipRelease
    ;
}

[[nodiscard]] static bool IsOwnershipAcquire(const GpuCompiledBarrierType::Enum type)noexcept{
    return type == GpuCompiledBarrierType::TextureOwnershipAcquire
        || type == GpuCompiledBarrierType::BufferOwnershipAcquire
        || type == GpuCompiledBarrierType::AccelStructOwnershipAcquire
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuNativePacketRecorder::preflightPacketResources(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packetID,
    const CommandListResourceStateHandoff* const initialStates
)const noexcept{
    if(!compiledGraph.validFor(graph) || !compiledGraph.validPacket(packetID))
        return false;

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    if(
        !tasks
        || packet.taskCount == 0u
        || !m_device.matchesPhysicalQueueIdentity(packet.queue)
        || (initialStates && !initialStates->validForDeviceGeneration(compiledGraph.deviceGeneration()))
    )
        return false;

    const auto validateOwnership = [&](const ResourceQueueSharing::Mask sharing,
                                       const ResourceQueueSharing::Mask resourceSharing,
                                       const GpuPhysicalQueueId ownerQueue,
                                       const GpuPhysicalQueueId releaseDestinationQueue){
        if(sharing != resourceSharing)
            return false;
        if(m_device.usesConcurrentQueueSharing(sharing))
            return !ownerQueue.valid() && !releaseDestinationQueue.valid();
        if(
            (ownerQueue.valid() && !m_device.matchesPhysicalQueueIdentity(ownerQueue))
            || (
                releaseDestinationQueue.valid()
                && !m_device.matchesPhysicalQueueIdentity(releaseDestinationQueue)
            )
        )
            return false;

        const u32 destinationFamily = m_device.getQueueFamilyIndex(packet.queue);
        const u32 sourceFamily = ownerQueue.valid()
            ? m_device.getQueueFamilyIndex(ownerQueue)
            : VK_QUEUE_FAMILY_IGNORED
        ;
        if(destinationFamily == VK_QUEUE_FAMILY_IGNORED)
            return false;
        if(!releaseDestinationQueue.valid())
            return !ownerQueue.valid() || sourceFamily == destinationFamily;

        return ownerQueue.valid()
            && sourceFamily != VK_QUEUE_FAMILY_IGNORED
            && m_device.getQueueFamilyIndex(releaseDestinationQueue) == destinationFamily
        ;
    };
    const auto permanentTextureState = [&](Texture* const texture, ResourceStates::Mask& outState){
        outState = ResourceStates::Unknown;
        if(!initialStates)
            return true;
        for(const CommandListResourceStateHandoff::PermanentTextureState& state
            : initialStates->m_permanentTextureStates
        ){
            if(state.texture != texture)
                continue;
            if(state.state == ResourceStates::Unknown)
                return false;
            if(outState != ResourceStates::Unknown && outState != state.state)
                return false;
            outState = state.state;
        }
        return true;
    };
    const auto permanentBufferState = [&](Buffer* const buffer, ResourceStates::Mask& outState){
        outState = ResourceStates::Unknown;
        if(!initialStates)
            return true;
        for(const CommandListResourceStateHandoff::BufferState& state : initialStates->m_permanentBufferStates){
            if(state.buffer != buffer)
                continue;
            if(state.state == ResourceStates::Unknown)
                return false;
            if(outState != ResourceStates::Unknown && outState != state.state)
                return false;
            outState = state.state;
        }
        return true;
    };
    const auto validateBufferForState = [&](Buffer* const buffer, const ResourceStates::Mask state){
        if(!buffer)
            return false;
        const BufferDesc& description = buffer->getCreationDescription();
        if(
            !GraphicsBackend::VulkanBufferDetail::IsBufferResourceStateMaskValid(state)
            || !GraphicsBackend::VulkanBufferDetail::IsBufferDescriptionCompatibleWithResourceStates(
                description,
                state
            )
        )
            return false;
        return m_device.isBufferReadyForGpuUse(
            buffer,
            GraphicsBackend::VulkanBufferDetail::RequiredBufferUsageForResourceStates(description, state)
        );
    };

    if(initialStates){
        for(usize stateIndex = 0u; stateIndex < initialStates->m_textureStates.size(); ++stateIndex){
            const CommandListResourceStateHandoff::TextureState& state = initialStates->m_textureStates[stateIndex];
            if(!state.texture)
                continue;
            const TextureDesc& description = state.texture->getCreationDescription();
            if(
                !GraphicsBackend::VulkanTextureDetail::IsTextureResourceStateMaskValid(state.state)
                || !m_device.isTextureReadyForGpuUse(
                    state.texture,
                    GraphicsBackend::VulkanTextureDetail::RequiredImageUsageForResourceStates(state.state)
                )
                || state.mipLevel >= description.mipLevels
                || state.arraySlice >= description.arraySize
                || !validateOwnership(
                    state.queueSharing,
                    description.queueSharing,
                    state.ownerQueue,
                    state.releaseDestinationQueue
                )
            )
                return false;
            for(usize otherIndex = 0u; otherIndex < stateIndex; ++otherIndex){
                const CommandListResourceStateHandoff::TextureState& other =
                    initialStates->m_textureStates[otherIndex]
                ;
                if(
                    other.texture == state.texture
                    && other.mipLevel == state.mipLevel
                    && other.arraySlice == state.arraySlice
                    && other.state != state.state
                )
                    return false;
            }
            ResourceStates::Mask permanentState = ResourceStates::Unknown;
            if(!permanentTextureState(state.texture, permanentState))
                return false;
            if(permanentState != ResourceStates::Unknown && permanentState != state.state)
                return false;
        }
        for(const CommandListResourceStateHandoff::PermanentTextureState& state
            : initialStates->m_permanentTextureStates
        ){
            if(
                state.texture
                && (
                    !GraphicsBackend::VulkanTextureDetail::IsTextureResourceStateMaskValid(state.state)
                    || !m_device.isTextureReadyForGpuUse(
                        state.texture,
                        GraphicsBackend::VulkanTextureDetail::RequiredImageUsageForResourceStates(state.state)
                    )
                    || !validateOwnership(
                        state.queueSharing,
                        state.texture->getCreationDescription().queueSharing,
                        state.ownerQueue,
                        state.releaseDestinationQueue
                    )
                )
            )
                return false;
            ResourceStates::Mask permanentState = ResourceStates::Unknown;
            if(state.texture && !permanentTextureState(state.texture, permanentState))
                return false;
        }
        for(usize stateIndex = 0u; stateIndex < initialStates->m_bufferStates.size(); ++stateIndex){
            const CommandListResourceStateHandoff::BufferState& state = initialStates->m_bufferStates[stateIndex];
            if(!state.buffer)
                continue;
            const BufferDesc& description = state.buffer->getCreationDescription();
            if(
                !validateBufferForState(state.buffer, state.state)
                || !validateOwnership(
                    state.queueSharing,
                    description.queueSharing,
                    state.ownerQueue,
                    state.releaseDestinationQueue
                )
            )
                return false;
            for(usize otherIndex = 0u; otherIndex < stateIndex; ++otherIndex){
                const CommandListResourceStateHandoff::BufferState& other =
                    initialStates->m_bufferStates[otherIndex]
                ;
                if(other.buffer == state.buffer && other.state != state.state)
                    return false;
            }
            ResourceStates::Mask permanentState = ResourceStates::Unknown;
            if(!permanentBufferState(state.buffer, permanentState))
                return false;
            if(permanentState != ResourceStates::Unknown && permanentState != state.state)
                return false;
        }
        for(const CommandListResourceStateHandoff::BufferState& state
            : initialStates->m_permanentBufferStates
        ){
            if(
                state.buffer
                && (
                    !validateBufferForState(state.buffer, state.state)
                    || !validateOwnership(
                        state.queueSharing,
                        state.buffer->getCreationDescription().queueSharing,
                        state.ownerQueue,
                        state.releaseDestinationQueue
                    )
                )
            )
                return false;
            ResourceStates::Mask permanentState = ResourceStates::Unknown;
            if(state.buffer && !permanentBufferState(state.buffer, permanentState))
                return false;
        }
    }

    const auto validateTextureRange = [](Texture* const texture, const TextureSubresourceSet& sourceRange){
        if(!texture)
            return false;
        const TextureDesc& description = texture->getCreationDescription();
        const TextureSubresourceSet range = sourceRange.resolve(description, TextureSubresourceMipResolve::Range);
        return range.numMipLevels != 0u
            && range.numArraySlices != 0u
            && static_cast<u64>(range.baseMipLevel) + range.numMipLevels <= description.mipLevels
            && static_cast<u64>(range.baseArraySlice) + range.numArraySlices <= description.arraySize
        ;
    };
    const auto validateBufferRange = [](Buffer* const buffer, const BufferRange& range){
        if(!buffer || range.byteSize == 0u)
            return false;
        const u64 bufferSize = buffer->getCreationDescription().byteSize;
        return range.byteOffset < bufferSize
            && (
                range.byteSize == BufferRange::AllBytes
                || range.byteSize <= bufferSize - range.byteOffset
            )
        ;
    };
    const auto validateResourceReady = [&](const GpuGraphResourceId resourceID,
                                           ResourceStates::Mask& outPermanentState,
                                           const ResourceStates::Mask requiredState = ResourceStates::Unknown){
        outPermanentState = ResourceStates::Unknown;
        if(!graph.validResource(resourceID))
            return false;
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceID.index);
        if(resource.id != resourceID)
            return false;
        if(resource.type == GpuGraphResourceType::HazardDomain)
            return !resource.hasBackendResource;
        if(!resource.hasBackendResource)
            return false;

        switch(resource.type){
        case GpuGraphResourceType::Texture:{
            Texture* const texture = graph.textureForResource(resourceID);
            return texture
                && (
                    requiredState == ResourceStates::Unknown
                    || GraphicsBackend::VulkanTextureDetail::IsTextureResourceStateMaskValid(requiredState)
                )
                && texture->getDeviceGeneration() == compiledGraph.deviceGeneration()
                && texture->getCreationDescription().queueSharing == resource.queueSharing
                && m_device.isTextureReadyForGpuUse(
                    texture,
                    GraphicsBackend::VulkanTextureDetail::RequiredImageUsageForResourceStates(requiredState)
                )
                && permanentTextureState(texture, outPermanentState)
            ;
        }
        case GpuGraphResourceType::Buffer:{
            Buffer* const buffer = graph.bufferForResource(resourceID);
            return buffer
                && buffer->getDeviceGeneration() == compiledGraph.deviceGeneration()
                && buffer->getCreationDescription().queueSharing == resource.queueSharing
                && (
                    requiredState == ResourceStates::Unknown
                    ? m_device.isBufferReadyForGpuUse(buffer)
                    : validateBufferForState(buffer, requiredState)
                )
                && permanentBufferState(buffer, outPermanentState)
            ;
        }
        case GpuGraphResourceType::AccelStruct:{
            RayTracingAccelStruct* const accelStruct = graph.accelStructForResource(resourceID);
            Buffer* const backingBuffer = accelStruct ? accelStruct->getBackingBuffer() : nullptr;
            const ResourceQueueSharing::Mask creationQueueSharing = accelStruct
                ? accelStruct->getCreationQueueSharing()
                : ResourceQueueSharing::Exclusive
            ;
            return accelStruct
                && backingBuffer
                && accelStruct->getDeviceGeneration() == compiledGraph.deviceGeneration()
                && accelStruct->queueSharingMatchesCreation()
                && creationQueueSharing == resource.queueSharing
                && backingBuffer->descriptionMatchesCreation()
                && backingBuffer->getCreationDescription().queueSharing == creationQueueSharing
                && m_device.isAccelStructReadyForGpuUse(accelStruct)
                && (
                    requiredState == ResourceStates::Unknown
                    ? m_device.isBufferReadyForGpuUse(backingBuffer)
                    : validateBufferForState(backingBuffer, requiredState)
                )
                && permanentBufferState(backingBuffer, outPermanentState)
            ;
        }
        default:
            return false;
        }
    };
    const auto validateResourceState = [&](const GpuGraphResourceId resourceID,
                                           const ResourceStates::Mask requiredState){
        if(!graph.validResource(resourceID))
            return false;
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceID.index);
        if(resource.type == GpuGraphResourceType::HazardDomain)
            return resource.id == resourceID
                && !resource.hasBackendResource
            ;
        if(requiredState == ResourceStates::Unknown)
            return false;

        ResourceStates::Mask permanentState = ResourceStates::Unknown;
        if(!validateResourceReady(resourceID, permanentState, requiredState))
            return false;
        return permanentState == ResourceStates::Unknown || permanentState == requiredState;
    };
    const auto resourcePermanentState = [&](const GpuGraphResourceId resourceID,
                                            ResourceStates::Mask& outState){
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceID.index);
        if(resource.type == GpuGraphResourceType::Texture)
            return permanentTextureState(graph.textureForResource(resourceID), outState);
        if(resource.type == GpuGraphResourceType::Buffer)
            return permanentBufferState(graph.bufferForResource(resourceID), outState);
        if(resource.type == GpuGraphResourceType::AccelStruct){
            RayTracingAccelStruct* const accelStruct = graph.accelStructForResource(resourceID);
            return permanentBufferState(accelStruct ? accelStruct->getBackingBuffer() : nullptr, outState);
        }
        return false;
    };
    const auto hasExplicitInitialState = [&](const GpuCompiledBarrier& barrier){
        if(!initialStates || !graph.validResource(barrier.resource))
            return false;
        const GpuTaskGraphResourceView resource = graph.resourceAt(barrier.resource.index);
        if(resource.type == GpuGraphResourceType::Texture){
            Texture* const texture = graph.textureForResource(barrier.resource);
            if(!validateTextureRange(texture, barrier.range.textureSubresources))
                return false;
            ResourceStates::Mask permanentState = ResourceStates::Unknown;
            if(!permanentTextureState(texture, permanentState))
                return false;
            if(permanentState != ResourceStates::Unknown)
                return true;
            const TextureSubresourceSet range = barrier.range.textureSubresources.resolve(
                texture->getCreationDescription(),
                TextureSubresourceMipResolve::Range
            );
            for(ArraySlice arraySlice = range.baseArraySlice;
                arraySlice < range.baseArraySlice + range.numArraySlices;
                ++arraySlice
            ){
                for(MipLevel mipLevel = range.baseMipLevel;
                    mipLevel < range.baseMipLevel + range.numMipLevels;
                    ++mipLevel
                ){
                    bool found = false;
                    for(const CommandListResourceStateHandoff::TextureState& state : initialStates->m_textureStates){
                        if(
                            state.texture == texture
                            && state.arraySlice == arraySlice
                            && state.mipLevel == mipLevel
                            && state.state != ResourceStates::Unknown
                        ){
                            found = true;
                            break;
                        }
                    }
                    if(!found)
                        return false;
                }
            }
            return true;
        }

        Buffer* buffer = graph.bufferForResource(barrier.resource);
        if(resource.type == GpuGraphResourceType::AccelStruct){
            RayTracingAccelStruct* const accelStruct = graph.accelStructForResource(barrier.resource);
            buffer = accelStruct ? accelStruct->getBackingBuffer() : nullptr;
        }
        ResourceStates::Mask permanentState = ResourceStates::Unknown;
        if(!buffer || !permanentBufferState(buffer, permanentState))
            return false;
        if(permanentState != ResourceStates::Unknown)
            return true;
        for(const CommandListResourceStateHandoff::BufferState& state : initialStates->m_bufferStates){
            if(state.buffer == buffer && state.state != ResourceStates::Unknown)
                return true;
        }
        return false;
    };

    const auto validateBarrierList = [&](const GpuCompiledBarrier* const barriers,
                                         const u32 barrierCount){
        if(barrierCount != 0u && !barriers)
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < barrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type >= GpuCompiledBarrierType::kCount
                || !graph.validResource(barrier.resource)
                || graph.resourceAt(barrier.resource.index).type
                    != __hidden_gpu_packet_runtime_preflight::BarrierResourceType(barrier.type)
            )
                return false;
            const GpuTaskGraphResourceView barrierResource = graph.resourceAt(barrier.resource.index);
            if(
                (
                    barrierResource.type == GpuGraphResourceType::Texture
                    && !validateTextureRange(
                        graph.textureForResource(barrier.resource),
                        barrier.range.textureSubresources
                    )
                )
                || (
                    barrierResource.type == GpuGraphResourceType::Buffer
                    && !validateBufferRange(
                        graph.bufferForResource(barrier.resource),
                        barrier.range.bufferRange
                    )
                )
            )
                return false;
            const bool ownershipRelease =
                __hidden_gpu_packet_runtime_preflight::IsOwnershipRelease(barrier.type)
            ;
            const bool ownershipAcquire =
                __hidden_gpu_packet_runtime_preflight::IsOwnershipAcquire(barrier.type)
            ;
            if(
                !compiledGraph.queueInfo(barrier.sourceQueue)
                || !compiledGraph.queueInfo(barrier.destinationQueue)
                || (ownershipRelease && barrier.sourceQueue != packet.queue)
                || (ownershipAcquire && barrier.destinationQueue != packet.queue)
                || (!ownershipRelease && !ownershipAcquire && barrier.destinationQueue != packet.queue)
            )
                return false;
            if(barrier.before != ResourceStates::Unknown){
                ResourceStates::Mask ignoredPermanentState = ResourceStates::Unknown;
                if(!validateResourceReady(barrier.resource, ignoredPermanentState, barrier.before))
                    return false;
            }
            if(
                barrier.after != ResourceStates::Unknown
                && !validateResourceState(barrier.resource, barrier.after)
            )
                return false;
            if(!ownershipAcquire && barrier.after == ResourceStates::Unknown)
                return false;
            if(ownershipRelease){
                ResourceStates::Mask permanentState = ResourceStates::Unknown;
                if(
                    !resourcePermanentState(barrier.resource, permanentState)
                    || permanentState != ResourceStates::Unknown
                )
                    return false;
            }
            if(
                (
                    barrier.isGraphInitialState
                    && barrier.before == ResourceStates::Unknown
                )
                || (ownershipAcquire && barrier.after == ResourceStates::Unknown)
            ){
                if(!hasExplicitInitialState(barrier))
                    return false;
            }
        }
        return true;
    };

    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        const GpuTaskId task = tasks[taskIndex];
        if(!graph.validTask(task))
            return false;
        const GpuCompiledTask* const compiledTask = compiledGraph.findTask(task);
        const GpuTaskGraphTaskView taskView = graph.taskAt(task.index);
        if(
            !compiledTask
            || compiledTask->packet != packetID
            || taskView.id != task
            || (taskView.resourceUseCount != 0u && !taskView.resourceUses)
        )
            return false;
        for(usize useIndex = 0u; useIndex < taskView.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = taskView.resourceUses[useIndex];
            if(
                use.access >= GpuTaskResourceAccess::kCount
                || !validateResourceState(use.resource, use.requiredState)
            )
                return false;
            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            if(
                resource.type == GpuGraphResourceType::Texture
                && !validateTextureRange(
                    graph.textureForResource(use.resource),
                    use.range.textureSubresources
                )
            )
                return false;
            if(
                resource.type == GpuGraphResourceType::Buffer
                && !validateBufferRange(graph.bufferForResource(use.resource), use.range.bufferRange)
            )
                return false;
        }
        const GpuPacketStateSeed* const stateSeeds = compiledGraph.taskPrologueStateSeeds(task);
        if(compiledTask->prologueStateSeedCount != 0u && !stateSeeds)
            return false;
        for(u32 seedIndex = 0u; seedIndex < compiledTask->prologueStateSeedCount; ++seedIndex){
            const GpuPacketStateSeed& seed = stateSeeds[seedIndex];
            ResourceStates::Mask permanentState = ResourceStates::Unknown;
            if(
                !compiledGraph.validPacket(seed.sourcePacket)
                || seed.sourcePacket == packetID
                || seed.sourcePacket.index >= packetID.index
                || !validateResourceReady(seed.resource, permanentState)
            )
                return false;
            const GpuTaskGraphResourceView resource = graph.resourceAt(seed.resource.index);
            if(
                (
                    resource.type == GpuGraphResourceType::Texture
                    && !validateTextureRange(
                        graph.textureForResource(seed.resource),
                        seed.range.textureSubresources
                    )
                )
                || (
                    resource.type == GpuGraphResourceType::Buffer
                    && !validateBufferRange(
                        graph.bufferForResource(seed.resource),
                        seed.range.bufferRange
                    )
                )
            )
                return false;
        }
        if(!validateBarrierList(
            compiledGraph.taskPrologueBarriers(task),
            compiledTask->prologueBarrierCount
        ))
            return false;
        if(!validateBarrierList(
            compiledGraph.taskEpilogueBarriers(task),
            compiledTask->epilogueBarrierCount
        ))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

