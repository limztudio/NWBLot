// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime_internal.h"
#include "packet_runtime_initial_state_validation.h"
#include "task_graph.h"

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <core/graphics/backend_selection/resource_validation.h>

#include <global/interval.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_preflight{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static GpuGraphResourceType::Enum BarrierResourceType(const GpuCompiledBarrierType::Enum type)noexcept{
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

[[nodiscard]] static b8 IsOwnershipRelease(const GpuCompiledBarrierType::Enum type)noexcept{
    return type == GpuCompiledBarrierType::TextureOwnershipRelease || type == GpuCompiledBarrierType::BufferOwnershipRelease || type == GpuCompiledBarrierType::AccelStructOwnershipRelease;
}

[[nodiscard]] static b8 IsTransition(const GpuCompiledBarrierType::Enum type)noexcept{
    return type == GpuCompiledBarrierType::TextureTransition || type == GpuCompiledBarrierType::BufferTransition || type == GpuCompiledBarrierType::AccelStructTransition;
}

[[nodiscard]] static b8 IsUavDependency(const GpuCompiledBarrierType::Enum type)noexcept{
    return type == GpuCompiledBarrierType::TextureUav || type == GpuCompiledBarrierType::BufferUav || type == GpuCompiledBarrierType::AccelStructUav;
}

[[nodiscard]] static b8 IsOwnershipAcquire(const GpuCompiledBarrierType::Enum type)noexcept{
    return type == GpuCompiledBarrierType::TextureOwnershipAcquire || type == GpuCompiledBarrierType::BufferOwnershipAcquire || type == GpuCompiledBarrierType::AccelStructOwnershipAcquire;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuNativePacketRecorder::preflightPacketResources(
    const GpuTaskGraph& graph,
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuPacketRecordingAccess& recordingAccess,
    const GpuSubmissionPacketId packetID,
    const CommandListResourceStateHandoff* const initialStates,
    Alloc::ScratchArena& stateFanInScratchArena,
    GpuRecordedGraph::PacketRecordingScratch& scratch
)const{
    if(!declarationAccess.validFor(graph) || !planAccess.validFor(declarationAccess) || !recordingAccess.validFor(graph, compiledGraph, planAccess, packetID))
        return false;

    const GpuCompiledPacketView packetView = planAccess.packet(packetID);
    if(!packetView.valid())
        return false;
    const GpuSubmissionPacket& packet = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;
    const GpuPhysicalQueueInfo* const packetQueueInfo = planAccess.queueInfo(packet.queue);
    if(packet.taskCount == 0u || !packetQueueInfo || !m_device.matchesPhysicalQueueIdentity(packet.queue) || (initialStates && !initialStates->validForDeviceGeneration(planAccess.deviceGeneration())))
        return false;

    const u32 destinationDomain = initialStates ? GraphicsBackend::GetQueueOwnershipDomain(m_device, packet.queue) : GraphicsBackend::s_InvalidQueueOwnershipDomain;
    const auto validateOwnership = [&](const ResourceQueueSharing::Mask sharing, const ResourceQueueSharing::Mask resourceSharing, const b8 usesConcurrentSharing, const GpuPhysicalQueueId ownerQueue, const GpuPhysicalQueueId releaseDestinationQueue)noexcept{
        if(sharing != resourceSharing)
            return false;
        if(usesConcurrentSharing)
            return !ownerQueue.valid() && !releaseDestinationQueue.valid();
        if((ownerQueue.valid() && !m_device.matchesPhysicalQueueIdentity(ownerQueue)) || (releaseDestinationQueue.valid() && !m_device.matchesPhysicalQueueIdentity(releaseDestinationQueue)))
            return false;

        const u32 sourceDomain = ownerQueue.valid() ? GraphicsBackend::GetQueueOwnershipDomain(m_device, ownerQueue) : GraphicsBackend::s_InvalidQueueOwnershipDomain;
        if(destinationDomain == GraphicsBackend::s_InvalidQueueOwnershipDomain)
            return false;
        if(!releaseDestinationQueue.valid())
            return !ownerQueue.valid() || sourceDomain == destinationDomain;

        return ownerQueue.valid() && sourceDomain != GraphicsBackend::s_InvalidQueueOwnershipDomain && GraphicsBackend::GetQueueOwnershipDomain(m_device, releaseDestinationQueue) == destinationDomain;
    };
    Optional<GpuInitialStateHandoffValidation> initialStateValidation;
    if(initialStates){
        initialStateValidation.emplace(*initialStates, stateFanInScratchArena);
        if(!initialStateValidation->validate())
            return false;
    }
    const auto permanentTextureState = [&](Texture* const texture)noexcept -> Expected<ResourceStates::Mask>{
        if(initialStateValidation)
            return initialStateValidation->permanentTextureState(texture);
        return ResourceStates::Unknown;
    };
    const auto permanentBufferState = [&](Buffer* const buffer)noexcept -> Expected<ResourceStates::Mask>{
        if(initialStateValidation)
            return initialStateValidation->permanentBufferState(buffer);
        return ResourceStates::Unknown;
    };
    const auto validateTextureForState = [&](Texture* const texture, const ResourceStates::Mask state)noexcept{
        if(!texture || !ResourceQueueAdmissionAdmitsQueue(texture->getQueueAdmissionSnapshot(), *packetQueueInfo))
            return false;
        if(state == ResourceStates::Unknown)
            return m_device.isTextureReadyForGpuUse(texture);
        if(!GraphicsBackend::IsTextureResourceStateMaskValid(state))
            return false;
        return GraphicsBackend::IsTextureReadyForState(m_device, texture, state);
    };
    const auto validateBufferForState = [&](Buffer* const buffer, const ResourceStates::Mask state)noexcept{
        if(!buffer)
            return false;
        if(!ResourceQueueAdmissionAdmitsQueue(buffer->getQueueAdmissionSnapshot(), *packetQueueInfo))
            return false;
        const BufferDesc& description = buffer->getCreationDescription();
        if(!GraphicsBackend::IsBufferResourceStateMaskValid(state) || !GraphicsBackend::IsBufferDescriptionCompatibleWithResourceStates(description, state))
            return false;
        return GraphicsBackend::IsBufferReadyForState(m_device, buffer, state);
    };

    if(initialStates){
        for(usize stateIndex = 0u; stateIndex < initialStates->m_textureStates.size(); ++stateIndex){
            const CommandListResourceStateHandoff::TextureState& state = initialStates->m_textureStates[stateIndex];
            if(!state.texture)
                continue;
            const TextureDesc& description = state.texture->getCreationDescription();
            if(
                !validateTextureForState(state.texture, state.state)
                || state.mipLevel >= description.mipLevels
                || state.arraySlice >= description.arraySize
                || !validateOwnership(state.queueSharing, description.queueSharing, state.texture->getQueueAdmissionSnapshot().usesConcurrentSharing, state.ownerQueue, state.releaseDestinationQueue)
            )
                return false;
        }
        for(const CommandListResourceStateHandoff::PermanentTextureState& state : initialStates->m_permanentTextureStates){
            if(
                state.texture
                && (
                    !validateTextureForState(state.texture, state.state)
                    || !validateOwnership(state.queueSharing, state.texture->getCreationDescription().queueSharing, state.texture->getQueueAdmissionSnapshot().usesConcurrentSharing, state.ownerQueue, state.releaseDestinationQueue)
                )
            )
                return false;
        }
        for(usize stateIndex = 0u; stateIndex < initialStates->m_bufferStates.size(); ++stateIndex){
            const CommandListResourceStateHandoff::BufferState& state = initialStates->m_bufferStates[stateIndex];
            if(!state.buffer)
                continue;
            const BufferDesc& description = state.buffer->getCreationDescription();
            if(!validateBufferForState(state.buffer, state.state) || !validateOwnership(state.queueSharing, description.queueSharing, state.buffer->getQueueAdmissionSnapshot().usesConcurrentSharing, state.ownerQueue, state.releaseDestinationQueue))
                return false;
        }
        for(const CommandListResourceStateHandoff::BufferState& state : initialStates->m_permanentBufferStates){
            if(
                state.buffer
                && (
                    !validateBufferForState(state.buffer, state.state)
                    || !validateOwnership(state.queueSharing, state.buffer->getCreationDescription().queueSharing, state.buffer->getQueueAdmissionSnapshot().usesConcurrentSharing, state.ownerQueue, state.releaseDestinationQueue)
                )
            )
                return false;
        }
    }

    auto& cachedResources = scratch.preflightResourceCache;
    const usize requiredResourceCount = declarationAccess.resourceCount();
    if(cachedResources.size() < requiredResourceCount)
        cachedResources.resize(requiredResourceCount);
    ++scratch.preflightCacheStamp;
    if(scratch.preflightCacheStamp == 0u){
        for(auto& cached : cachedResources)
            cached.stamp = 0u;
        scratch.preflightCacheStamp = 1u;
    }
    const u64 preflightStamp = scratch.preflightCacheStamp;
    const auto getCachedResource = [&](const GpuGraphResourceId resourceID)noexcept -> GpuRecordedGraph::PacketRecordingScratch::PreflightCachedResource&{
        auto& cached = cachedResources[resourceID.index];
        if(cached.stamp != preflightStamp){
            cached.stamp = preflightStamp;
            cached.texture = nullptr;
            cached.buffer = nullptr;
            cached.accelStruct = nullptr;
            cached.permanentState = ResourceStates::Unknown;
            cached.lastValidatedState = ResourceStates::Unknown;
            cached.queueSharing = ResourceQueueSharing::Exclusive;
            cached.type = GpuGraphResourceType::HazardDomain;
            cached.hasBackendResource = false;
            cached.baseValidated = false;
            cached.hasLastValidatedState = false;
            cached.resolved = false;
        }
        if(!cached.resolved){
            const GpuTaskGraphResourceView resource = declarationAccess.resourceAt(resourceID.index);
            cached.type = resource.type;
            cached.hasBackendResource = resource.hasBackendResource;
            cached.queueSharing = resource.queueSharing;
            switch(resource.type){
            case GpuGraphResourceType::Texture:
                cached.texture = declarationAccess.textureForResource(resourceID);
                break;
            case GpuGraphResourceType::Buffer:
                cached.buffer = declarationAccess.bufferForResource(resourceID);
                break;
            case GpuGraphResourceType::AccelStruct:
                cached.accelStruct = declarationAccess.accelStructForResource(resourceID);
                cached.buffer = cached.accelStruct ? cached.accelStruct->getBackingBuffer() : nullptr;
                break;
            default:
                break;
            }
            cached.resolved = true;
        }
        return cached;
    };

    const auto validateTextureRange = [](Texture* const texture, const TextureSubresourceSet& sourceRange)noexcept{
        if(!texture)
            return false;
        const TextureDesc& description = texture->getCreationDescription();
        const TextureSubresourceSet range = sourceRange.resolve(description, TextureSubresourceMipResolve::Range);
        return range.numMipLevels != 0u && range.numArraySlices != 0u && static_cast<u64>(range.baseMipLevel) + range.numMipLevels <= description.mipLevels && static_cast<u64>(range.baseArraySlice) + range.numArraySlices <= description.arraySize;
    };
    const auto validateBufferRange = [](Buffer* const buffer, const BufferRange& range)noexcept{
        return buffer && GpuInitialStateHandoffValidation::ValidBufferRange(range, buffer->getCreationDescription().byteSize);
    };
    const auto validateCachedResourceReady = [&](GpuRecordedGraph::PacketRecordingScratch::PreflightCachedResource& cached, const ResourceStates::Mask requiredState)noexcept -> Expected<ResourceStates::Mask>{
        if(cached.type == GpuGraphResourceType::HazardDomain){
            if(cached.hasBackendResource)
                return MakeUnexpected(Failure{});
            return ResourceStates::Unknown;
        }
        if(!cached.hasBackendResource)
            return MakeUnexpected(Failure{});

        switch(cached.type){
        case GpuGraphResourceType::Texture:{
            Texture* const texture = cached.texture;
            if(!cached.baseValidated){
                if(!(texture && texture->getDeviceGeneration() == planAccess.deviceGeneration() && texture->getCreationDescription().queueSharing == cached.queueSharing && validateTextureForState(texture, ResourceStates::Unknown)))
                    return MakeUnexpected(Failure{});
                const auto permanentState = permanentTextureState(texture);
                if(!permanentState)
                    return MakeUnexpected(Failure{});
                cached.permanentState = *permanentState;
                cached.baseValidated = true;
            }
            if(requiredState == ResourceStates::Unknown)
                return cached.permanentState;
            if(cached.hasLastValidatedState && cached.lastValidatedState == requiredState)
                return cached.permanentState;
            if(!GraphicsBackend::IsTextureResourceStateMaskValid(requiredState) || !GraphicsBackend::IsTextureReadyForState(m_device, texture, requiredState))
                return MakeUnexpected(Failure{});
            cached.lastValidatedState = requiredState;
            cached.hasLastValidatedState = true;
            return cached.permanentState;
        }
        case GpuGraphResourceType::Buffer:{
            Buffer* const buffer = cached.buffer;
            if(!cached.baseValidated){
                if(!(buffer && buffer->getDeviceGeneration() == planAccess.deviceGeneration() && buffer->getCreationDescription().queueSharing == cached.queueSharing && ResourceQueueAdmissionAdmitsQueue(buffer->getQueueAdmissionSnapshot(), *packetQueueInfo) && m_device.isBufferReadyForGpuUse(buffer)))
                    return MakeUnexpected(Failure{});
                const auto permanentState = permanentBufferState(buffer);
                if(!permanentState)
                    return MakeUnexpected(Failure{});
                cached.permanentState = *permanentState;
                cached.baseValidated = true;
            }
            if(requiredState == ResourceStates::Unknown)
                return cached.permanentState;
            if(cached.hasLastValidatedState && cached.lastValidatedState == requiredState)
                return cached.permanentState;
            if(!GraphicsBackend::IsBufferResourceStateMaskValid(requiredState) || !GraphicsBackend::IsBufferDescriptionCompatibleWithResourceStates(buffer->getCreationDescription(), requiredState) || !GraphicsBackend::IsBufferReadyForState(m_device, buffer, requiredState))
                return MakeUnexpected(Failure{});
            cached.lastValidatedState = requiredState;
            cached.hasLastValidatedState = true;
            return cached.permanentState;
        }
        case GpuGraphResourceType::AccelStruct:{
            RayTracingAccelStruct* const accelStruct = cached.accelStruct;
            Buffer* const backingBuffer = cached.buffer;
            if(!cached.baseValidated){
                const ResourceQueueSharing::Mask creationQueueSharing = accelStruct ? accelStruct->getCreationQueueSharing() : ResourceQueueSharing::Exclusive;
                if(!(
                    accelStruct
                    && backingBuffer
                    && accelStruct->getDeviceGeneration() == planAccess.deviceGeneration()
                    && accelStruct->queueSharingMatchesCreation()
                    && creationQueueSharing == cached.queueSharing
                    && backingBuffer->descriptionMatchesCreation()
                    && backingBuffer->getCreationDescription().queueSharing == creationQueueSharing
                    && m_device.isAccelStructReadyForGpuUse(accelStruct)
                    && ResourceQueueAdmissionAdmitsQueue(backingBuffer->getQueueAdmissionSnapshot(), *packetQueueInfo)
                    && m_device.isBufferReadyForGpuUse(backingBuffer)
                ))
                    return MakeUnexpected(Failure{});
                const auto permanentState = permanentBufferState(backingBuffer);
                if(!permanentState)
                    return MakeUnexpected(Failure{});
                cached.permanentState = *permanentState;
                cached.baseValidated = true;
            }
            if(requiredState == ResourceStates::Unknown)
                return cached.permanentState;
            if(cached.hasLastValidatedState && cached.lastValidatedState == requiredState)
                return cached.permanentState;
            if(!GraphicsBackend::IsBufferResourceStateMaskValid(requiredState) || !GraphicsBackend::IsBufferDescriptionCompatibleWithResourceStates(backingBuffer->getCreationDescription(), requiredState) || !GraphicsBackend::IsBufferReadyForState(m_device, backingBuffer, requiredState))
                return MakeUnexpected(Failure{});
            cached.lastValidatedState = requiredState;
            cached.hasLastValidatedState = true;
            return cached.permanentState;
        }
        default:
            return MakeUnexpected(Failure{});
        }
    };
    const auto validateResourceReady = [&](const GpuGraphResourceId resourceID, const ResourceStates::Mask requiredState = ResourceStates::Unknown)noexcept -> Expected<ResourceStates::Mask>{
        if(!declarationAccess.validResource(resourceID))
            return MakeUnexpected(Failure{});
        return validateCachedResourceReady(getCachedResource(resourceID), requiredState);
    };
    const auto validateResourceState = [&](const GpuGraphResourceId resourceID, const ResourceStates::Mask requiredState)noexcept{
        if(!declarationAccess.validResource(resourceID))
            return false;
        auto& cached = getCachedResource(resourceID);
        if(cached.type == GpuGraphResourceType::HazardDomain)
            return !cached.hasBackendResource;
        if(requiredState == ResourceStates::Unknown)
            return false;

        const auto permanentState = validateCachedResourceReady(cached, requiredState);
        if(!permanentState)
            return false;
        return (*permanentState) == ResourceStates::Unknown || (*permanentState) == requiredState;
    };
    const auto resourcePermanentState = [&](const GpuGraphResourceId resourceID)noexcept -> Expected<ResourceStates::Mask>{
        const auto& cached = getCachedResource(resourceID);
        if(cached.baseValidated)
            return cached.permanentState;
        if(cached.type == GpuGraphResourceType::Texture)
            return permanentTextureState(cached.texture);
        if(cached.type == GpuGraphResourceType::Buffer || cached.type == GpuGraphResourceType::AccelStruct)
            return permanentBufferState(cached.buffer);
        return MakeUnexpected(Failure{});
    };
    const auto hasExplicitInitialState = [&](const GpuCompiledBarrier& barrier)noexcept{
        if(!initialStates || !declarationAccess.validResource(barrier.resource))
            return false;
        const auto& cached = getCachedResource(barrier.resource);
        if(cached.type == GpuGraphResourceType::Texture){
            Texture* const texture = cached.texture;
            if(!validateTextureRange(texture, barrier.range.textureSubresources))
                return false;
            const auto permanentState = permanentTextureState(texture);
            if(!permanentState)
                return false;
            if((*permanentState) != ResourceStates::Unknown)
                return true;
            const TextureSubresourceSet range = barrier.range.textureSubresources.resolve(texture->getCreationDescription(), TextureSubresourceMipResolve::Range);
            return initialStateValidation->hasExplicitTextureRange(texture, range);
        }

        Buffer* const buffer = cached.buffer;
        Expected<ResourceStates::Mask> permanentState = ResourceStates::Unknown;
        if(!buffer || !(permanentState = permanentBufferState(buffer)))
            return false;
        if((*permanentState) != ResourceStates::Unknown)
            return true;
        const BufferRange range = barrier.range.bufferRange.resolve(buffer->getCreationDescription());
        if(initialStates->m_bufferStates.size() > 8u)
            return initialStateValidation->hasExplicitBufferRange(buffer, range);
        return CoversHalfOpenRange(
            range.byteOffset, range.end(), Span<const CommandListResourceStateHandoff::BufferState>(initialStates->m_bufferStates),
            [buffer](const auto& state)noexcept{ return state.buffer == buffer && state.state != ResourceStates::Unknown; },
            [](const auto& state)noexcept{ return state.range.byteOffset; },
            [](const auto& state)noexcept{ return state.range.end(); }
        ) && range.hasExtent();
    };

    const auto validateBarrierList = [&](const Span<const GpuCompiledBarrier> barriers)noexcept{
        for(const GpuCompiledBarrier& barrier : barriers){
            if(barrier.type >= GpuCompiledBarrierType::Count || !declarationAccess.validResource(barrier.resource))
                return false;
            const auto& barrierResource = getCachedResource(barrier.resource);
            if(barrierResource.type != __hidden_gpu_packet_runtime_preflight::BarrierResourceType(barrier.type))
                return false;
            if(
                (barrierResource.type == GpuGraphResourceType::Texture && !validateTextureRange(barrierResource.texture, barrier.range.textureSubresources))
                || (barrierResource.type == GpuGraphResourceType::Buffer && !validateBufferRange(barrierResource.buffer, barrier.range.bufferRange))
            )
                return false;
            const b8 ownershipRelease = __hidden_gpu_packet_runtime_preflight::IsOwnershipRelease(barrier.type);
            const b8 ownershipAcquire = __hidden_gpu_packet_runtime_preflight::IsOwnershipAcquire(barrier.type);
            const b8 transition = __hidden_gpu_packet_runtime_preflight::IsTransition(barrier.type);
            const b8 uavDependency = __hidden_gpu_packet_runtime_preflight::IsUavDependency(barrier.type);
            if(
                (
                    barrier.forceMemoryDependency
                    && (
                        (!transition && !uavDependency)
                        || barrier.before == ResourceStates::Unknown
                        || barrier.before != barrier.after
                        || barrier.isGraphInitialState
                        || barrier.isInitialOwnerHandoff
                        || (transition && ResourceStates::HasUnorderedAccess(barrier.before))
                    )
                )
                || (transition && barrier.before == barrier.after && !barrier.isGraphInitialState && !barrier.forceMemoryDependency)
                || (uavDependency && (!barrier.forceMemoryDependency || !ResourceStates::HasUnorderedAccess(barrier.before)))
            )
                return false;
            if(
                !planAccess.queueInfo(barrier.sourceQueue)
                || !planAccess.queueInfo(barrier.destinationQueue)
                || (ownershipRelease && barrier.sourceQueue != packet.queue)
                || (ownershipAcquire && barrier.destinationQueue != packet.queue)
                || (!ownershipRelease && !ownershipAcquire && barrier.destinationQueue != packet.queue)
            )
                return false;
            if(barrier.before != ResourceStates::Unknown){
                if(!validateResourceReady(barrier.resource, barrier.before))
                    return false;
            }
            if(barrier.after != ResourceStates::Unknown && !validateResourceState(barrier.resource, barrier.after))
                return false;
            if(!ownershipAcquire && barrier.after == ResourceStates::Unknown)
                return false;
            if(ownershipRelease){
                Expected<ResourceStates::Mask> permanentState = ResourceStates::Unknown;
                if(!(permanentState = resourcePermanentState(barrier.resource)) || (*permanentState) != ResourceStates::Unknown)
                    return false;
            }
            if((barrier.isGraphInitialState && barrier.before == ResourceStates::Unknown) || (ownershipAcquire && barrier.after == ResourceStates::Unknown)){
                if(!hasExplicitInitialState(barrier))
                    return false;
            }
        }
        return true;
    };

    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        const GpuTaskId task = tasks[taskIndex];
        if(!recordingAccess.validForTask(graph, compiledGraph, planAccess, task))
            return false;
        const GpuCompiledTaskView compiledTaskView = planAccess.findTask(task);
        const GpuCompiledTask* const compiledTask = compiledTaskView.plan;
        const GpuTaskGraphTaskView taskView = declarationAccess.taskAt(task.index);
        FOUNDATION_ASSERT(compiledTaskView.valid() && compiledTask->packet == packetID && taskView.id == task && (taskView.resourceUseCount == 0u || taskView.resourceUses != nullptr));
        for(usize useIndex = 0u; useIndex < taskView.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = taskView.resourceUses[useIndex];
            if(use.access >= GpuTaskResourceAccess::Count || !validateResourceState(use.resource, use.requiredState))
                return false;
            const auto& cached = getCachedResource(use.resource);
            if(cached.type == GpuGraphResourceType::Texture && !validateTextureRange(cached.texture, use.range.textureSubresources))
                return false;
            if(cached.type == GpuGraphResourceType::Buffer && !validateBufferRange(cached.buffer, use.range.bufferRange))
                return false;
        }
        const GpuPacketStateSeed* const stateSeeds = compiledTaskView.prologueStateSeeds;
        FOUNDATION_ASSERT(compiledTask->prologueStateSeedCount == 0u || stateSeeds != nullptr);
        for(u32 seedIndex = 0u; seedIndex < compiledTask->prologueStateSeedCount; ++seedIndex){
            const GpuPacketStateSeed& seed = stateSeeds[seedIndex];
            if(!planAccess.validPacket(seed.sourcePacket) || seed.sourcePacket.index >= packetID.index || !validateResourceReady(seed.resource))
                return false;
            const auto& cached = getCachedResource(seed.resource);
            if(
                (cached.type == GpuGraphResourceType::Texture && !validateTextureRange(cached.texture, seed.range.textureSubresources))
                || (cached.type == GpuGraphResourceType::Buffer && !validateBufferRange(cached.buffer, seed.range.bufferRange))
            )
                return false;
        }
        FOUNDATION_ASSERT(compiledTask->prologueBarrierCount == 0u || compiledTaskView.prologueBarriers != nullptr);
        if(!validateBarrierList(Span<const GpuCompiledBarrier>(compiledTaskView.prologueBarriers, compiledTask->prologueBarrierCount)))
            return false;
        FOUNDATION_ASSERT(compiledTask->epilogueBarrierCount == 0u || compiledTaskView.epilogueBarriers != nullptr);
        if(!validateBarrierList(Span<const GpuCompiledBarrier>(compiledTaskView.epilogueBarriers, compiledTask->epilogueBarrierCount)))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

