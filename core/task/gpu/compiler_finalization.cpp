// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_finalization{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TerminalPacketDependencyHasher{
    usize operator()(const GpuPacketDependency& dependency)const{
        usize hash = Hasher<u64>{}(dependency.producer.generation);
        HashCombine(hash, dependency.producer.index);
        HashCombine(hash, dependency.consumer.generation);
        HashCombine(hash, dependency.consumer.index);
        return hash;
    }
};

struct TerminalPacketDependencyEqual{
    bool operator()(const GpuPacketDependency& lhs, const GpuPacketDependency& rhs)const noexcept{
        return lhs.producer == rhs.producer && lhs.consumer == rhs.consumer;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool PlanExternalResourceExports(GpuTaskGraphResourceStatePlan& plan){
    const GpuTaskGraph::DeclarationReadView& graph = plan.graph;
    GpuTaskGraphCompiledPlanStorage& compiledPlan = plan.compiledPlan;
    Alloc::ScratchArena& scratchArena = plan.scratchArena;
    const Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedResourceStates = plan.trackedResourceStates;
    const TrackedResourceStateHistory& resourceHistory = plan.resourceHistory;
    Vector<PendingCompiledEpilogueBarrier, Alloc::ScratchArena>& pendingEpilogueBarriers = plan.pendingEpilogueBarriers;
    Vector<GpuPacketDependency, Alloc::ScratchArena>& resourceStateDependencies =
        plan.resourceStateDependencies
    ;
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& stateFragments = plan.stateFragments;
    using namespace __hidden_gpu_task_graph_finalization;
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

    // Imported metadata may require a graph-owned terminal state for post-graph resume. Texture/buffer keep every
    // terminal fragment; AS stays whole-allocation; state retained even without a native transition.
    for(usize resourceIndex = 0u; resourceIndex < graph.resourceCount(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceIndex);
        if(resource.externalFinalState == ResourceStates::Unknown)
            continue;

        const bool hasExternalFinalRelease = resource.externalFinalReleaseDestinationQueue.valid();

        const GpuCompiledBarrierType::Enum exportType = StateExportBarrierType(resource.type);
        if(exportType >= GpuCompiledBarrierType::kCount)
            return false;

        bool hasTerminalDeclaredRange = false;
        GpuTaskId externalExportTask;
        GpuSubmissionPacketId externalExportPacket;
        GpuPhysicalQueueId externalExportSourceQueue;
        bool externalExportUsesOnePacket = true;
        const u32 externalExportSourceOffset = static_cast<u32>(
            compiledPlan.externalResourceExportSources.size()
        );
        u32 externalExportSourceCount = 0u;
        const auto appendTerminalState = [&](
            const TrackedCompiledResourceState& state,
            const usize terminalStateIndex,
            const GpuTaskResourceRange& terminalRange
        ){
            if(
                terminalStateIndex >= trackedResourceStates.size()
                || &trackedResourceStates[terminalStateIndex] != &state
            )
                return false;

            const GpuSubmissionPacketId terminalPacket = FindCompiledPacketForTask(compiledPlan, state.task);
            if(
                !terminalPacket.valid()
                || terminalPacket.generation != compiledPlan.planGeneration
                || terminalPacket.index >= compiledPlan.packets.size()
            )
                return false;

            const bool performsFinalization = state.state != resource.externalFinalState
                || (
                    hasExternalFinalRelease
                    && state.queue != resource.externalFinalReleaseDestinationQueue
                )
            ;
            if(performsFinalization){
                // The selected terminal task owns the external transition/release for this exact fragment. Any
                // earlier overlapping terminal user that was shadowed by it must finish before that operation; a
                // matching state alone does not otherwise order concurrent read-only packets.
                for(
                    usize earlierStateIndex = resourceHistory.first(resource.id);
                    earlierStateIndex != Limit<usize>::s_Max && earlierStateIndex < terminalStateIndex;
                    earlierStateIndex = resourceHistory.next(earlierStateIndex)
                ){
                    const TrackedCompiledResourceState& earlierState = trackedResourceStates[earlierStateIndex];
                    if(!RangesOverlap(resource, earlierState.range, terminalRange))
                        continue;

                    const GpuSubmissionPacketId earlierPacket = FindCompiledPacketForTask(
                        compiledPlan,
                        earlierState.task
                    );
                    if(
                        !earlierPacket.valid()
                        || earlierPacket.generation != compiledPlan.planGeneration
                        || earlierPacket.index >= compiledPlan.packets.size()
                    )
                        return false;
                    if(earlierPacket == terminalPacket)
                        continue;
                    if(earlierPacket.index >= terminalPacket.index)
                        return false;

                    appendTerminalFinalizationDependency({
                        .producer = earlierPacket,
                        .consumer = terminalPacket,
                    });
                }
            }

            if(hasExternalFinalRelease){
                if(!externalExportTask.valid()){
                    externalExportTask = state.task;
                    externalExportPacket = terminalPacket;
                    externalExportSourceQueue = state.queue;
                }
                else if(
                    externalExportPacket != terminalPacket
                    || externalExportSourceQueue != state.queue
                ){
                    externalExportUsesOnePacket = false;
                }
                // Texture and buffer ownership snapshots retain disjoint terminal ranges from different queues.
                // Acceleration structures still have one allocation owner and cannot publish conflicting releases.
                if(
                    resource.type != GpuGraphResourceType::Texture
                    && resource.type != GpuGraphResourceType::Buffer
                    && (
                        externalExportPacket != terminalPacket
                        || externalExportSourceQueue != state.queue
                    )
                )
                    return false;
                compiledPlan.externalResourceExportSources.push_back(
                    GpuCompiledExternalResourceExportSource{
                        .producerTask = state.task,
                        .sourceQueue = state.queue,
                        .range = terminalRange,
                    }
                );
                ++externalExportSourceCount;
            }

            pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                .task = state.task,
                .barrier = GpuCompiledBarrier{
                    .resource = state.resource,
                    .range = terminalRange,
                    .before = state.state,
                    .after = resource.externalFinalState,
                    .sourceQueue = state.queue,
                    .destinationQueue = state.queue,
                    .type = exportType,
                },
            });
            if(hasExternalFinalRelease && state.queue != resource.externalFinalReleaseDestinationQueue){
                const GpuCompiledBarrierType::Enum releaseType = OwnershipReleaseBarrierType(resource.type);
                if(releaseType >= GpuCompiledBarrierType::kCount)
                    return false;
                // Export the exact final state before ownership moves. Native lowering therefore captures the same
                // state in the released snapshot and the paired Vulkan queue-family release barrier.
                if(!AppendCompiledOwnershipTransfer(
                    plan,
                    resource,
                    terminalRange,
                    state.task,
                    GpuTaskId{},
                    state.queue,
                    resource.externalFinalReleaseDestinationQueue,
                    GpuOwnershipTransferRoute::ExternalExport
                ))
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
            }
            hasTerminalDeclaredRange = true;
            return true;
        };

        if(resource.type == GpuGraphResourceType::Texture || resource.type == GpuGraphResourceType::Buffer){
            stateFragments.clear();
            if(!CollectTerminalResourceStateFragments(
                trackedResourceStates,
                resourceHistory,
                resource,
                scratchArena,
                stateFragments
            ))
                return false;
            for(const TrackedResourceStateFragment& fragment : stateFragments){
                if(
                    !fragment.state
                    || !appendTerminalState(*fragment.state, fragment.stateIndex, fragment.range)
                )
                    return false;
            }
        }
        else{
            for(
                usize stateIndex = resourceHistory.first(resource.id);
                stateIndex != Limit<usize>::s_Max;
                stateIndex = resourceHistory.next(stateIndex)
            ){
                const TrackedCompiledResourceState& state = trackedResourceStates[stateIndex];
                bool hasLaterOverlappingUse = false;
                for(
                    usize laterStateIndex = resourceHistory.next(stateIndex);
                    laterStateIndex != Limit<usize>::s_Max;
                    laterStateIndex = resourceHistory.next(laterStateIndex)
                ){
                    const TrackedCompiledResourceState& later = trackedResourceStates[laterStateIndex];
                    if(RangesOverlap(resource, state.range, later.range)){
                        hasLaterOverlappingUse = true;
                        break;
                    }
                }
                if(hasLaterOverlappingUse)
                    continue;
                if(!appendTerminalState(state, stateIndex, state.range))
                    return false;
            }
        }
        if(!hasTerminalDeclaredRange){
            // A final-state requirement cannot be published from an untouched or state-unknown resource. Reject
            // compilation rather than leaving a direct renderer bridge to guess whether the requirement held.
            return false;
        }
        if(hasExternalFinalRelease){
            if(
                !externalExportTask.valid()
                || !externalExportPacket.valid()
                || !externalExportSourceQueue.valid()
                || externalExportSourceCount == 0u
            )
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

[[nodiscard]] bool AppendPendingEpilogueBarriers(GpuTaskGraphResourceStatePlan& plan){
    // Consumers are visited after their producer, so ownership releases are discovered late. Group them only after
    // planning completes to keep every task's epilogue span contiguous in the immutable compiled graph.
    GpuTaskGraphCompiledPlanStorage& compiledPlan = plan.compiledPlan;
    constexpr usize s_BarrierMax = static_cast<usize>(Limit<u32>::s_Max);
    if(
        compiledPlan.epilogueBarriers.size() > s_BarrierMax
        || plan.pendingEpilogueBarriers.size() > s_BarrierMax - compiledPlan.epilogueBarriers.size()
    )
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
        GpuCompiledTask* const compiledTask = FindCompiledTask(compiledPlan, pending.task);
        if(compiledTask)
            ++compiledTask->epilogueBarrierCount;
    }

    usize barrierCount = compiledPlan.epilogueBarriers.size();
    for(GpuCompiledTask& compiledTask : compiledPlan.tasks){
        compiledTask.epilogueBarrierOffset = static_cast<u32>(barrierCount);
        barrierCount += compiledTask.epilogueBarrierCount;
        compiledTask.epilogueBarrierCount = 0u;
    }
    compiledPlan.epilogueBarriers.resize(barrierCount);

    // Reuse each task's count as its fill cursor. Traversing discovery order keeps exports before their paired
    // ownership releases without allocating a separate grouping table.
    for(const PendingCompiledEpilogueBarrier& pending : plan.pendingEpilogueBarriers){
        GpuCompiledTask* const compiledTask = FindCompiledTask(compiledPlan, pending.task);
        if(!compiledTask)
            continue;
        const usize barrierIndex = static_cast<usize>(compiledTask->epilogueBarrierOffset) + compiledTask->epilogueBarrierCount;
        compiledPlan.epilogueBarriers[barrierIndex] = pending.barrier;
        ++compiledTask->epilogueBarrierCount;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

