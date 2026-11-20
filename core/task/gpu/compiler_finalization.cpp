// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_compiler_finalization{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TerminalPacketDependencyHasher{
    usize operator()(const GpuPacketDependency& dependency)const noexcept{
        usize hash = Hasher<u64>{}(dependency.producer.generation);
        HashCombine(hash, dependency.producer.index);
        HashCombine(hash, dependency.consumer.generation);
        HashCombine(hash, dependency.consumer.index);
        return hash;
    }
};

struct TerminalPacketDependencyEqual{
    b8 operator()(const GpuPacketDependency& lhs, const GpuPacketDependency& rhs)const noexcept{
        return lhs.producer == rhs.producer && lhs.consumer == rhs.consumer;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 PlanExternalResourceExports(GpuTaskGraphResourceStatePlan& plan){
    const GpuTaskGraph::DeclarationReadView& graph = plan.graph;
    GpuTaskGraphCompiledPlanStorage& compiledPlan = plan.compiledPlan;
    Alloc::ScratchArena& scratchArena = plan.scratchArena;
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedResourceStates = plan.trackedResourceStates;
    const TrackedResourceStateHistory& resourceHistory = plan.resourceHistory;
    Vector<PendingCompiledEpilogueBarrier, Alloc::ScratchArena>& pendingEpilogueBarriers = plan.pendingEpilogueBarriers;
    Vector<GpuPacketDependency, Alloc::ScratchArena>& resourceStateDependencies = plan.resourceStateDependencies;
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& stateFragments = plan.stateFragments;
    using namespace __hidden_gpu_task_graph_compiler_finalization;
    using TerminalDependencyIndex = HashSet<GpuPacketDependency, Alloc::ScratchArena, TerminalPacketDependencyHasher, TerminalPacketDependencyEqual>;
    Optional<TerminalDependencyIndex> indexedTerminalDependencies;
    const auto appendTerminalFinalizationDependency = [&](const GpuPacketDependency& dependency){
        constexpr usize s_InlineTerminalDependencyCount = 8u;
        if(!indexedTerminalDependencies && resourceStateDependencies.size() <= s_InlineTerminalDependencyCount){
            for(const GpuPacketDependency& previous : resourceStateDependencies){
                if(TerminalPacketDependencyEqual{}(previous, dependency))
                    return;
            }
            if(resourceStateDependencies.size() < s_InlineTerminalDependencyCount){
                resourceStateDependencies.push_back(dependency);
                return;
            }
        }
        if(!indexedTerminalDependencies){
            indexedTerminalDependencies.emplace(Max(graph.taskCount(), resourceStateDependencies.size()), scratchArena);
            indexedTerminalDependencies->insert(resourceStateDependencies.begin(), resourceStateDependencies.end());
        }
        if(!indexedTerminalDependencies->insert(dependency).second)
            return;
        resourceStateDependencies.push_back(dependency);
    };

    for(usize resourceIndex = 0u; resourceIndex < graph.resourceCount(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceIndex);
        if(resource.externalFinalState == ResourceStates::Unknown)
            continue;

        const b8 hasExternalFinalRelease = resource.externalFinalReleaseDestinationQueue.valid();

        const GpuCompiledBarrierType::Enum exportType = StateExportBarrierType(resource.type);
        FOUNDATION_ASSERT(exportType < GpuCompiledBarrierType::Count);

        b8 hasTerminalDeclaredRange = false;
        GpuTaskId externalExportTask;
        GpuSubmissionPacketId externalExportPacket;
        GpuPhysicalQueueId externalExportSourceQueue;
        b8 externalExportUsesOnePacket = true;
        constexpr usize s_SourceMax = static_cast<usize>(Limit<u32>::s_Max);
        if(compiledPlan.externalResourceExportSources.size() > s_SourceMax)
            return false;
        const u32 externalExportSourceOffset = static_cast<u32>(compiledPlan.externalResourceExportSources.size());
        u32 externalExportSourceCount = 0u;
        const auto appendTerminalState = [&](const TrackedCompiledResourceState& state, const usize terminalStateIndex, const GpuTaskResourceRange& terminalRange){
            FOUNDATION_ASSERT(terminalStateIndex < trackedResourceStates.size() && &trackedResourceStates[terminalStateIndex] == &state);

            const GpuSubmissionPacketId terminalPacket = FindCompiledPacketForTask(compiledPlan, state.task);
            if(!terminalPacket.valid() || terminalPacket.generation != compiledPlan.planGeneration || terminalPacket.index >= compiledPlan.packets.size())
                return false;

            const b8 performsFinalization = state.state != resource.externalFinalState || (hasExternalFinalRelease && state.queue != resource.externalFinalReleaseDestinationQueue);
            if(performsFinalization && !IsWriteAccess(state.access)){
                for(usize earlierStateIndex = resourceHistory.previous(terminalStateIndex); earlierStateIndex != Limit<usize>::s_Max; earlierStateIndex = resourceHistory.previous(earlierStateIndex)){
                    const TrackedCompiledResourceState& earlierState = trackedResourceStates[earlierStateIndex];
                    if(!RangesOverlap(resource, earlierState.range, terminalRange))
                        continue;

                    const GpuSubmissionPacketId earlierPacket = FindCompiledPacketForTask(compiledPlan, earlierState.task);
                    if(!earlierPacket.valid() || earlierPacket.generation != compiledPlan.planGeneration || earlierPacket.index >= compiledPlan.packets.size())
                        return false;
                    if(earlierPacket != terminalPacket){
                        if(earlierPacket.index >= terminalPacket.index)
                            return false;

                        appendTerminalFinalizationDependency({ .producer = earlierPacket, .consumer = terminalPacket, });
                    }
                    if(IsWriteAccess(earlierState.access) && RangeContains(resource, earlierState.range, terminalRange))
                        break;
                }
            }

            if(hasExternalFinalRelease){
                if(!externalExportTask.valid()){
                    externalExportTask = state.task;
                    externalExportPacket = terminalPacket;
                    externalExportSourceQueue = state.queue;
                }
                else if(externalExportPacket != terminalPacket || externalExportSourceQueue != state.queue){
                    externalExportUsesOnePacket = false;
                }
                if(resource.type != GpuGraphResourceType::Texture && resource.type != GpuGraphResourceType::Buffer && (externalExportPacket != terminalPacket || externalExportSourceQueue != state.queue))
                    return false;
                if(compiledPlan.externalResourceExportSources.size() >= s_SourceMax)
                    return false;
                compiledPlan.externalResourceExportSources.push_back(GpuCompiledExternalResourceExportSource{ .producerTask = state.task, .sourceQueue = state.queue, .range = terminalRange, });
                ++externalExportSourceCount;
            }

            pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                .task = state.task,
                .barrier = GpuCompiledBarrier{ .resource = state.resource, .range = terminalRange, .before = state.state, .after = resource.externalFinalState, .sourceQueue = state.queue, .destinationQueue = state.queue, .type = exportType, },
            });
            plan.resourceHasPendingEpilogueBarrier[resource.id.index] = 1u;
            if(hasExternalFinalRelease && state.queue != resource.externalFinalReleaseDestinationQueue){
                const GpuCompiledBarrierType::Enum releaseType = OwnershipReleaseBarrierType(resource.type);
                FOUNDATION_ASSERT(releaseType < GpuCompiledBarrierType::Count);
                if(!AppendCompiledOwnershipTransfer(plan, resource, terminalRange, state.task, GpuTaskId{}, state.queue, resource.externalFinalReleaseDestinationQueue, GpuOwnershipTransferRoute::ExternalExport))
                    return false;
                pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                    .task = state.task,
                    .barrier = GpuCompiledBarrier{
                        .resource = state.resource,
                        .range = terminalRange,
                        .before = resource.externalFinalState,
                        .after = resource.externalFinalState,
                        .sourceQueue = state.queue,
                        .destinationQueue = resource.externalFinalReleaseDestinationQueue,
                        .type = releaseType,
                    },
                });
                plan.resourceHasPendingEpilogueBarrier[resource.id.index] = 1u;
            }
            hasTerminalDeclaredRange = true;
            return true;
        };

        if(resource.type == GpuGraphResourceType::Texture || resource.type == GpuGraphResourceType::Buffer){
            stateFragments.clear();
            const auto fullResolvedRange = ResolveResourceRangeForPlanning(graph, resource, GpuTaskResourceRange{});
            if(!CollectTerminalResourceStateFragmentsWithScratch(
                trackedResourceStates,
                resourceHistory,
                resource,
                plan.rangeScratch,
                stateFragments,
                fullResolvedRange ? &(*fullResolvedRange) : nullptr
            ))
                return false;
            for(const TrackedResourceStateFragment& fragment : stateFragments){
                FOUNDATION_ASSERT(fragment.state);
                if(!appendTerminalState(*fragment.state, fragment.stateIndex, fragment.range))
                    return false;
            }
        }
        else{
            const usize lastStateIndex = resourceHistory.last(resource.id);
            if(lastStateIndex != Limit<usize>::s_Max){
                const TrackedCompiledResourceState& state = trackedResourceStates[lastStateIndex];
                if(!appendTerminalState(state, lastStateIndex, state.range))
                    return false;
            }
        }
        if(!hasTerminalDeclaredRange){
            return false;
        }
        if(hasExternalFinalRelease){
            if(!externalExportTask.valid() || !externalExportPacket.valid() || !externalExportSourceQueue.valid() || externalExportSourceCount == 0u)
                return false;
            compiledPlan.externalResourceExports.push_back(GpuCompiledExternalResourceExport{
                .resource = resource.id,
                .producerTask = externalExportUsesOnePacket ? externalExportTask : GpuTaskId{},
                .sourceQueue = externalExportUsesOnePacket ? externalExportSourceQueue : GpuPhysicalQueueId{},
                .sourceOffset = externalExportSourceOffset,
                .sourceCount = externalExportSourceCount,
                .destinationQueue = resource.externalFinalReleaseDestinationQueue,
                .finalState = resource.externalFinalState,
            });
        }
    }
    return true;
}

[[nodiscard]] b8 AppendPendingEpilogueBarriers(GpuTaskGraphResourceStatePlan& plan){
    GpuTaskGraphCompiledPlanStorage& compiledPlan = plan.compiledPlan;
    constexpr usize s_BarrierMax = static_cast<usize>(Limit<u32>::s_Max);
    if(compiledPlan.epilogueBarriers.size() > s_BarrierMax || plan.pendingEpilogueBarriers.size() > s_BarrierMax - compiledPlan.epilogueBarriers.size())
        return false;
    if(plan.pendingEpilogueBarriers.empty()){
        const u32 barrierOffset = static_cast<u32>(compiledPlan.epilogueBarriers.size());
        for(GpuCompiledTask& compiledTask : compiledPlan.tasks){
            compiledTask.epilogueBarrierOffset = barrierOffset;
            compiledTask.epilogueBarrierCount = 0u;
        }
        return true;
    }

    for(GpuCompiledTask& compiledTask : compiledPlan.tasks)
        compiledTask.epilogueBarrierCount = 0u;
    for(const PendingCompiledEpilogueBarrier& pending : plan.pendingEpilogueBarriers){
        auto& compiledTask = *MakeNotNull(FindCompiledTask(compiledPlan, pending.task));
        ++compiledTask.epilogueBarrierCount;
    }

    usize barrierCount = compiledPlan.epilogueBarriers.size();
    for(GpuCompiledTask& compiledTask : compiledPlan.tasks){
        compiledTask.epilogueBarrierOffset = static_cast<u32>(barrierCount);
        barrierCount += compiledTask.epilogueBarrierCount;
        compiledTask.epilogueBarrierCount = 0u;
    }
    compiledPlan.epilogueBarriers.resize(barrierCount);

    for(const PendingCompiledEpilogueBarrier& pending : plan.pendingEpilogueBarriers){
        auto& compiledTask = *MakeNotNull(FindCompiledTask(compiledPlan, pending.task));
        const usize barrierIndex = static_cast<usize>(compiledTask.epilogueBarrierOffset) + compiledTask.epilogueBarrierCount;
        compiledPlan.epilogueBarriers[barrierIndex] = pending.barrier;
        ++compiledTask.epilogueBarrierCount;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

