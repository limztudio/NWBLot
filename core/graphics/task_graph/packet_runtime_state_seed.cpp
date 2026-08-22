// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_state_seed{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 TextureRangeEnd(
    const u32 base,
    const u32 count,
    const u32 all
)noexcept{
    return count == all ? Limit<u64>::s_Max : static_cast<u64>(base) + static_cast<u64>(count);
}

[[nodiscard]] static bool TextureRangeContains(
    const TextureSubresourceSet& outer,
    const TextureSubresourceSet& inner
)noexcept{
    return outer.baseMipLevel <= inner.baseMipLevel
        && TextureRangeEnd(outer.baseMipLevel, outer.numMipLevels, TextureSubresourceSet::AllMipLevels)
            >= TextureRangeEnd(inner.baseMipLevel, inner.numMipLevels, TextureSubresourceSet::AllMipLevels)
        && outer.baseArraySlice <= inner.baseArraySlice
        && TextureRangeEnd(outer.baseArraySlice, outer.numArraySlices, TextureSubresourceSet::AllArraySlices)
            >= TextureRangeEnd(inner.baseArraySlice, inner.numArraySlices, TextureSubresourceSet::AllArraySlices)
    ;
}

[[nodiscard]] static const GpuTaskGraphInitialOwnerHandoffSourceView* FindInitialOwnerHandoffSource(
    const GpuTaskGraphResourceView& resource,
    const GpuCompiledBarrier& barrier
)noexcept{
    if(
        resource.initialOwnerHandoffSourceCount == 0u
        || !resource.initialOwnerHandoffSources
        || resource.type != GpuGraphResourceType::Texture
    )
        return nullptr;

    const GpuTaskGraphInitialOwnerHandoffSourceView* result = nullptr;
    for(usize sourceIndex = 0u;
        sourceIndex < resource.initialOwnerHandoffSourceCount;
        ++sourceIndex
    ){
        const GpuTaskGraphInitialOwnerHandoffSourceView& source = resource.initialOwnerHandoffSources[sourceIndex];
        if(
            source.sourceQueue != barrier.sourceQueue
            || source.destinationQueue != barrier.destinationQueue
            || !TextureRangeContains(source.range.textureSubresources, barrier.range.textureSubresources)
        )
            continue;
        if(result)
            return nullptr;
        result = &source;
    }
    return result;
}

[[nodiscard]] bool ValidateTaskPacketStateBindings(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount
){
    if(taskStateBindingCount != 0u && !taskStateBindings)
        return false;

    for(usize bindingIndex = 0u; bindingIndex < taskStateBindingCount; ++bindingIndex){
        const GpuTaskPacketStateBinding& binding = taskStateBindings[bindingIndex];
        if(
            !graph.validTask(binding.task)
            || !compiledGraph.findTask(binding.task)
            || binding.externalStateSourceCount == 0u
            || !binding.externalStateSources
        )
            return false;
        for(usize sourceIndex = 0u; sourceIndex < binding.externalStateSourceCount; ++sourceIndex){
            const CommandListResourceStateHandoff* const states =
                binding.externalStateSources[sourceIndex].states
            ;
            if(!states || !states->validForDeviceGeneration(compiledGraph.deviceGeneration()))
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuRecordedGraph::buildPacketInitialStateSeed(
    PacketRecordingScratch& scratch,
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId& packetID,
    const GpuExternalPacketStateSource* const externalStateSources,
    const usize externalStateSourceCount,
    const GpuTaskPacketStateBinding* const taskStateBindings,
    const usize taskStateBindingCount,
    const CommandListResourceStateHandoff*& outInitialStates
){
    outInitialStates = nullptr;
    if(
        !validFor(compiledGraph)
        || !compiledGraph.validPacket(packetID)
        || (externalStateSourceCount != 0u && !externalStateSources)
        || !__hidden_gpu_packet_runtime_state_seed::ValidateTaskPacketStateBindings(
            graph,
            compiledGraph,
            taskStateBindings,
            taskStateBindingCount
        )
    )
        return false;

    scratch.initialStateSeed.reset();
    scratch.stateSubsetScratch.reset();
    scratch.stateMergeScratch.reset();
    scratch.externalBaseStateSeed.reset();
    scratch.externalMergedStateSeed.reset();

    // State snapshots include inherited state as well as resources a producer changed.  Preserve the established
    // base-plus-branches fan-in rule while deriving every filtered branch from graph declarations: a later source
    // may replace the base, but two independent sources may not leave incompatible final state for one resource.
    const auto appendSourceSubset = [&]{
        if(scratch.stateSubsetScratch.empty())
            return true;

        if(!scratch.stateMergeScratch.valid())
            return scratch.stateMergeScratch.copyFrom(scratch.stateSubsetScratch);

        const CommandListResourceStateHandoff* const branches[] = { &scratch.stateSubsetScratch };
        if(!scratch.initialStateSeed.buildFanIn(scratch.stateMergeScratch, branches, LengthOf(branches)))
            return false;
        return scratch.stateMergeScratch.copyFrom(scratch.initialStateSeed);
    };

    const auto appendMergedExternalSource = [&]{
        if(!scratch.stateMergeScratch.valid())
            return true;

        if(!scratch.externalBaseStateSeed.valid()){
            if(
                !scratch.externalBaseStateSeed.copyFrom(scratch.stateMergeScratch)
                || !scratch.externalMergedStateSeed.copyFrom(scratch.stateMergeScratch)
            )
                return false;
            return true;
        }

        const CommandListResourceStateHandoff* const branches[] = {
            &scratch.externalMergedStateSeed,
            &scratch.stateMergeScratch,
        };
        if(!scratch.initialStateSeed.buildFanIn(scratch.externalBaseStateSeed, branches, LengthOf(branches)))
            return false;
        return scratch.externalMergedStateSeed.copyFrom(scratch.initialStateSeed);
    };

    const GpuSubmissionPacket& packet = compiledGraph.packet(packetID);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packetID);
    if(!tasks || packet.taskCount == 0u)
        return false;

    const auto appendStateSource = [&](
        const CommandListResourceStateHandoff* const sourceStates,
        const GpuTaskId* const sourceTasks,
        const u32 sourceTaskCount
    ){
        if(
            !sourceStates
            || !sourceStates->validForDeviceGeneration(compiledGraph.deviceGeneration())
            || (sourceTaskCount != 0u && !sourceTasks)
        )
            return false;

        scratch.stateMergeScratch.reset();

        for(u32 taskIndex = 0u; taskIndex < sourceTaskCount; ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(sourceTasks[taskIndex].index);
            for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
                const GpuTaskResourceUse& use = task.resourceUses[useIndex];
                const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
                scratch.stateSubsetScratch.reset();

                switch(resource.type){
                case GpuGraphResourceType::Texture:{
                    Texture* const texture = graph.textureForResource(use.resource);
                    if(!texture || !scratch.stateSubsetScratch.buildTextureRangeSubset(
                        *sourceStates,
                        texture,
                        use.range.textureSubresources
                    ))
                        return false;
                    break;
                }
                case GpuGraphResourceType::Buffer:{
                    Buffer* const buffer = graph.bufferForResource(use.resource);
                    if(!buffer)
                        return false;
                    Buffer* const buffers[] = { buffer };
                    if(!scratch.stateSubsetScratch.buildResourceSubset(*sourceStates, nullptr, 0u, buffers, 1u))
                        return false;
                    break;
                }
                case GpuGraphResourceType::AccelStruct:{
                    RayTracingAccelStruct* const accelStruct = graph.accelStructForResource(use.resource);
                    Buffer* const backingBuffer = accelStruct ? accelStruct->getBackingBuffer() : nullptr;
                    if(!backingBuffer)
                        return false;
                    Buffer* const buffers[] = { backingBuffer };
                    if(!scratch.stateSubsetScratch.buildResourceSubset(*sourceStates, nullptr, 0u, buffers, 1u))
                        return false;
                    break;
                }
                case GpuGraphResourceType::HazardDomain:
                    continue;
                default:
                    return false;
                }

                // A given external producer need not have touched every resource declared by the consumer.  Empty
                // subsets deliberately remain absent instead of manufacturing an initial state; a later source or
                // the compiler's transition supplies the state that actually exists.
                if(!appendSourceSubset())
                    return false;
            }
        }

        return appendMergedExternalSource();
    };

    const auto appendInitialOwnerStateSource = [&](const GpuCompiledBarrier& barrier){
        const GpuTaskGraphResourceView resource = graph.resourceAt(barrier.resource.index);
        const GpuTaskGraphInitialOwnerHandoffSourceView* const multiSource =
            __hidden_gpu_packet_runtime_state_seed::FindInitialOwnerHandoffSource(resource, barrier)
        ;
        if(resource.initialOwnerHandoffSourceCount != 0u && !multiSource)
            return false;
        const CommandListResourceStateHandoff* const sourceStates = multiSource
            ? multiSource->stateSource
            : resource.initialOwnerStateSource
        ;
        if(
            !sourceStates
            || !sourceStates->validForDeviceGeneration(compiledGraph.deviceGeneration())
            || (
                multiSource
                    ? (
                        multiSource->sourceQueue != barrier.sourceQueue
                        || multiSource->destinationQueue != barrier.destinationQueue
                    )
                    : (
                        resource.initialOwnerQueue != barrier.sourceQueue
                        || resource.initialOwnerReleaseDestinationQueue != barrier.destinationQueue
                    )
            )
        )
            return false;

        scratch.stateMergeScratch.reset();
        scratch.stateSubsetScratch.reset();
        switch(resource.type){
        case GpuGraphResourceType::Texture:{
            Texture* const texture = graph.textureForResource(barrier.resource);
            if(!texture || !scratch.stateSubsetScratch.buildTextureRangeSubset(
                *sourceStates,
                texture,
                barrier.range.textureSubresources
            ))
                return false;
            break;
        }
        case GpuGraphResourceType::Buffer:{
            Buffer* const buffer = graph.bufferForResource(barrier.resource);
            if(!buffer)
                return false;
            Buffer* const buffers[] = { buffer };
            if(!scratch.stateSubsetScratch.buildResourceSubset(*sourceStates, nullptr, 0u, buffers, 1u))
                return false;
            break;
        }
        case GpuGraphResourceType::AccelStruct:{
            RayTracingAccelStruct* const accelStruct = graph.accelStructForResource(barrier.resource);
            Buffer* const backingBuffer = accelStruct ? accelStruct->getBackingBuffer() : nullptr;
            if(!backingBuffer)
                return false;
            Buffer* const buffers[] = { backingBuffer };
            if(!scratch.stateSubsetScratch.buildResourceSubset(*sourceStates, nullptr, 0u, buffers, 1u))
                return false;
            break;
        }
        default:
            return false;
        }

        // Unlike an optional task state source, an imported ownership handoff must name every first-use range. A
        // missing state would leave the acquire marker without the native source layout/owner it is required to
        // import, so reject recording instead of falling back to descriptor creation state.
        if(scratch.stateSubsetScratch.empty() || !appendSourceSubset())
            return false;
        return appendMergedExternalSource();
    };

    // Resource-owned external handoffs are resolved from compiler markers rather than a renderer packet override.
    // They are imported before ordinary task sources so the native command list opens with the released ownership
    // and emits the paired Vulkan acquire before any graph prologue transition records.
    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        const GpuCompiledTask* const compiledTask = compiledGraph.findTask(tasks[taskIndex]);
        const GpuCompiledBarrier* const barriers = compiledGraph.taskPrologueBarriers(tasks[taskIndex]);
        if(!compiledTask || (compiledTask->prologueBarrierCount != 0u && !barriers))
            return false;
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                !barrier.isInitialOwnerHandoff
                || (
                    barrier.type != GpuCompiledBarrierType::TextureOwnershipAcquire
                    && barrier.type != GpuCompiledBarrierType::BufferOwnershipAcquire
                    && barrier.type != GpuCompiledBarrierType::AccelStructOwnershipAcquire
                )
            )
                continue;
            const GpuTaskGraphResourceView resource = graph.resourceAt(barrier.resource.index);
            const GpuTaskGraphInitialOwnerHandoffSourceView* const multiSource =
                __hidden_gpu_packet_runtime_state_seed::FindInitialOwnerHandoffSource(resource, barrier)
            ;
            if(resource.initialOwnerHandoffSourceCount != 0u && !multiSource)
                return false;
            if(!multiSource && !resource.initialOwnerCompletion.valid())
                continue;
            if(!appendInitialOwnerStateSource(barrier))
                return false;
        }
    }

    // Declaration-owned sources are attached to the task that needs their imported state.  That preserves the
    // source/resource relationship across packet coalescing and lets ordinary record traversal avoid renderer-owned
    // packet-specific overrides.  Sparse record-descriptor sources below remain as a compatibility bridge.
    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(tasks[taskIndex].index);
        if(task.externalStateSourceCount != 0u && !task.externalStateSources)
            return false;
        for(usize sourceIndex = 0u; sourceIndex < task.externalStateSourceCount; ++sourceIndex){
            if(!appendStateSource(
                task.externalStateSources[sourceIndex].states,
                tasks + taskIndex,
                1u
            ))
                return false;
        }
    }

    // Late sources remain a recording-time bridge, but their identity is graph-owned: the caller names the task
    // that semantically receives the imported state and the recorder resolves its current compiled packet.  Apply
    // each source to the complete packet to preserve the established packet-wide external handoff while compiler
    // packetization evolves.
    for(usize bindingIndex = 0u; bindingIndex < taskStateBindingCount; ++bindingIndex){
        const GpuTaskPacketStateBinding& binding = taskStateBindings[bindingIndex];
        if(compiledGraph.packetForTask(binding.task) != packetID)
            continue;
        for(usize sourceIndex = 0u; sourceIndex < binding.externalStateSourceCount; ++sourceIndex){
            if(!appendStateSource(
                binding.externalStateSources[sourceIndex].states,
                tasks,
                packet.taskCount
            ))
                return false;
        }
    }

    for(usize sourceIndex = 0u; sourceIndex < externalStateSourceCount; ++sourceIndex){
        if(!appendStateSource(
            externalStateSources[sourceIndex].states,
            tasks,
            packet.taskCount
        ))
            return false;
    }

    scratch.initialStateSeed.reset();
    if(
        scratch.externalMergedStateSeed.valid()
        && !scratch.initialStateSeed.copyFrom(scratch.externalMergedStateSeed)
    )
        return false;
    scratch.stateMergeScratch.reset();

    const auto appendInitialStateSubset = [&](const bool allowEmpty){
        if(scratch.stateSubsetScratch.empty())
            return allowEmpty;

        if(!scratch.initialStateSeed.valid())
            return scratch.initialStateSeed.copyFrom(scratch.stateSubsetScratch);

        const CommandListResourceStateHandoff* const branches[] = { &scratch.stateSubsetScratch };
        if(!scratch.stateMergeScratch.buildFanIn(scratch.initialStateSeed, branches, LengthOf(branches)))
            return false;
        return scratch.initialStateSeed.copyFrom(scratch.stateMergeScratch);
    };

    for(u32 taskIndex = 0u; taskIndex < packet.taskCount; ++taskIndex){
        const GpuCompiledTask* const compiledTask = compiledGraph.findTask(tasks[taskIndex]);
        const GpuPacketStateSeed* const stateSeeds = compiledGraph.taskPrologueStateSeeds(tasks[taskIndex]);
        if(!compiledTask || (compiledTask->prologueStateSeedCount != 0u && !stateSeeds))
            return false;

        for(u32 seedIndex = 0u; seedIndex < compiledTask->prologueStateSeedCount; ++seedIndex){
            const GpuPacketStateSeed& seed = stateSeeds[seedIndex];
            const CommandListResourceStateHandoff* const sourceStates = packetStateSeed(seed.sourcePacket);
            if(!sourceStates || !sourceStates->validForDeviceGeneration(compiledGraph.deviceGeneration()))
                return false;

            // An empty subset means the producer thunk never tracked a resource it declared as a state source.  Do
            // not silently fall back to the descriptor's creation state: that would reintroduce the stale-state bug
            // this graph-owned seed is meant to eliminate.
            scratch.stateSubsetScratch.reset();
            if(Texture* const texture = graph.textureForResource(seed.resource)){
                if(!scratch.stateSubsetScratch.buildTextureRangeSubset(
                    *sourceStates,
                    texture,
                    seed.range.textureSubresources
                ))
                    return false;
            }
            else if(Buffer* const buffer = graph.bufferForResource(seed.resource)){
                Buffer* const buffers[] = { buffer };
                if(!scratch.stateSubsetScratch.buildResourceSubset(*sourceStates, nullptr, 0u, buffers, 1u))
                    return false;
            }
            else if(RayTracingAccelStruct* const accelStruct = graph.accelStructForResource(seed.resource)){
                Buffer* const backingBuffer = accelStruct->getBackingBuffer();
                if(!backingBuffer)
                    return false;
                Buffer* const buffers[] = { backingBuffer };
                if(!scratch.stateSubsetScratch.buildResourceSubset(*sourceStates, nullptr, 0u, buffers, 1u))
                    return false;
            }
            else
                return false;
            if(!appendInitialStateSubset(false))
                return false;
        }
    }

    outInitialStates = scratch.initialStateSeed.valid() ? &scratch.initialStateSeed : nullptr;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

