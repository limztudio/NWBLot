// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"
#include "compiler_read_state_sources.h"

#include <core/graphics/backend_selection/resource_validation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] GpuCompiledBarrierType::Enum TransitionBarrierType(const GpuGraphResourceType::Enum resourceType)noexcept{
    switch(resourceType){
    case GpuGraphResourceType::Texture:
        return GpuCompiledBarrierType::TextureTransition;
    case GpuGraphResourceType::Buffer:
        return GpuCompiledBarrierType::BufferTransition;
    case GpuGraphResourceType::AccelStruct:
        return GpuCompiledBarrierType::AccelStructTransition;
    default:
        NWB_ASSERT(false);
        return GpuCompiledBarrierType::TextureTransition;
    }
}

[[nodiscard]] GpuCompiledBarrierType::Enum UavBarrierType(const GpuGraphResourceType::Enum resourceType)noexcept{
    switch(resourceType){
    case GpuGraphResourceType::Texture:
        return GpuCompiledBarrierType::TextureUav;
    case GpuGraphResourceType::Buffer:
        return GpuCompiledBarrierType::BufferUav;
    case GpuGraphResourceType::AccelStruct:
        return GpuCompiledBarrierType::AccelStructUav;
    default:
        NWB_ASSERT(false);
        return GpuCompiledBarrierType::TextureUav;
    }
}

[[nodiscard]] GpuCompiledBarrierType::Enum StateExportBarrierType(const GpuGraphResourceType::Enum resourceType)noexcept{
    switch(resourceType){
    case GpuGraphResourceType::Texture:
        return GpuCompiledBarrierType::TextureStateExport;
    case GpuGraphResourceType::Buffer:
        return GpuCompiledBarrierType::BufferStateExport;
    case GpuGraphResourceType::AccelStruct:
        return GpuCompiledBarrierType::AccelStructStateExport;
    default:
        return GpuCompiledBarrierType::kCount;
    }
}

[[nodiscard]] GpuCompiledBarrierType::Enum OwnershipReleaseBarrierType(const GpuGraphResourceType::Enum resourceType)noexcept{
    switch(resourceType){
    case GpuGraphResourceType::Texture:
        return GpuCompiledBarrierType::TextureOwnershipRelease;
    case GpuGraphResourceType::Buffer:
        return GpuCompiledBarrierType::BufferOwnershipRelease;
    case GpuGraphResourceType::AccelStruct:
        return GpuCompiledBarrierType::AccelStructOwnershipRelease;
    default:
        return GpuCompiledBarrierType::kCount;
    }
}

[[nodiscard]] GpuCompiledBarrierType::Enum OwnershipAcquireBarrierType(const GpuGraphResourceType::Enum resourceType)noexcept{
    switch(resourceType){
    case GpuGraphResourceType::Texture:
        return GpuCompiledBarrierType::TextureOwnershipAcquire;
    case GpuGraphResourceType::Buffer:
        return GpuCompiledBarrierType::BufferOwnershipAcquire;
    case GpuGraphResourceType::AccelStruct:
        return GpuCompiledBarrierType::AccelStructOwnershipAcquire;
    default:
        return GpuCompiledBarrierType::kCount;
    }
}

[[nodiscard]] static const GpuTaskGraphInitialOwnerHandoffSourceView* FindInitialOwnerHandoffSource(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& firstUseRange,
    const GpuPhysicalQueueId& destinationQueue
)noexcept{
    // Callers pass only resources with handoff sources, and compile() already rejected a missing source span.
    NWB_ASSERT(resource.initialOwnerHandoffSourceCount != 0u && resource.initialOwnerHandoffSources);
    const GpuTaskGraphInitialOwnerHandoffSourceView* result = nullptr;
    for(usize sourceIndex = 0u; sourceIndex < resource.initialOwnerHandoffSourceCount; ++sourceIndex){
        const GpuTaskGraphInitialOwnerHandoffSourceView& source = resource.initialOwnerHandoffSources[sourceIndex];
        Expected<GpuTaskResourceRange> plannedSourceRange = GpuTaskResourceRange{};
        if(source.destinationQueue != destinationQueue || !(plannedSourceRange = ResolveResourceRangeForPlanning(graph, resource, source.range)) || !RangeContains(resource, (*plannedSourceRange), firstUseRange))
            continue;
        // One first-use fragment must have one exact external owner and one matching completion token.
        if(result)
            return nullptr;
        result = &source;
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<bool> PlanCrossPacketResourceStateSeed(
    GpuTaskGraphResourceStatePlan& plan,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceUse& use,
    const GpuTaskResourceRange& barrierRange,
    const GpuTaskResourceRange& readerDependencyRange,
    const TrackedCompiledResourceState* const previousState,
    const TrackedCompiledResourceState* const stateSource,
    const TrackedCompiledResourceState* const readStateSource,
    const GpuCompiledTask& compiledTask,
    const ResourceStates::Mask before,
    const bool needsSameStateDependency
){
    GpuTaskGraphCompiledPlanStorage& compiledPlan = plan.compiledPlan;
    const GpuPhysicalQueueTopology& topology = plan.topology;
    auto& pendingEpilogueBarriers = plan.pendingEpilogueBarriers;
    if(previousState){
        const GpuSubmissionPacketId sourcePacket = FindCompiledPacketForTask(compiledPlan, previousState->task);
        if(!sourcePacket.valid())
            return MakeUnexpected(Failure{});
        if(sourcePacket != compiledTask.packet){
            const GpuPhysicalQueueInfo* const sourceQueue = FindCompiledQueueInfo(compiledPlan, previousState->queue);
            const GpuPhysicalQueueInfo* const destinationQueue = FindCompiledQueueInfo(compiledPlan, compiledTask.queue);
            if(!sourceQueue || !destinationQueue)
                return MakeUnexpected(Failure{});
            const bool differentQueueFamilies = sourceQueue->familyIndex != destinationQueue->familyIndex;
            const bool resourceUsesConcurrentSharing = ResourceUsesConcurrentQueueSharing(resource, topology);
            const bool concurrentQueuePair = ResourceSharesQueuePairConcurrently(resource, topology, *sourceQueue, *destinationQueue);
            if(differentQueueFamilies && resourceUsesConcurrentSharing && !concurrentQueuePair){
                // The resource was created concurrent for another family set.
                // Vulkan cannot transfer ownership to an omitted family, so fail compilation instead of recording invalid work.
                return MakeUnexpected(Failure{});
            }

            const bool requiresExclusiveOwnershipHandoff = !resourceUsesConcurrentSharing && differentQueueFamilies;
            if(requiresExclusiveOwnershipHandoff){
                if(previousState->access == GpuTaskResourceAccess::Read && !AppendOverlappingReaderDependencies(plan, resource, readerDependencyRange, *previousState, sourcePacket))
                    return MakeUnexpected(Failure{});
                // HazardDomain uses never reach state planning, so the type is texture, buffer or acceleration structure.
                const GpuCompiledBarrierType::Enum releaseType = OwnershipReleaseBarrierType(resource.type);
                const GpuCompiledBarrierType::Enum acquireType = OwnershipAcquireBarrierType(resource.type);
                NWB_ASSERT(releaseType < GpuCompiledBarrierType::kCount && acquireType < GpuCompiledBarrierType::kCount);
                if(!AppendCompiledOwnershipTransfer(plan, resource, barrierRange, previousState->task, compiledTask.task, previousState->queue, compiledTask.queue, GpuOwnershipTransferRoute::Internal))
                    return MakeUnexpected(Failure{});
                pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                    .task = previousState->task,
                    .barrier = GpuCompiledBarrier{ .resource = use.resource, .range = barrierRange, .before = before, .after = before, .sourceQueue = previousState->queue, .destinationQueue = compiledTask.queue, .type = releaseType, },
                }
                );
                compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                    .resource = use.resource,
                    .range = barrierRange,
                    .before = before,
                    .after = before,
                    .sourceQueue = previousState->queue,
                    .destinationQueue = compiledTask.queue,
                    .type = acquireType,
                }
                );
            }
            const bool readOnlySameState = previousState->access == GpuTaskResourceAccess::Read && use.access == GpuTaskResourceAccess::Read && before == use.requiredState && !needsSameStateDependency;
            const bool mayOmitInternalStateSeed = use.hasIndependentStateSource && readOnlySameState && concurrentQueuePair;
            if(!mayOmitInternalStateSeed){
                const GpuSubmissionPacketId stateSourcePacket = FindCompiledPacketForTask(compiledPlan, stateSource->task);
                if(!stateSourcePacket.valid())
                    return MakeUnexpected(Failure{});
                if(stateSourcePacket != compiledTask.packet){
                    compiledPlan.prologueStateSeeds.push_back(GpuPacketStateSeed{ .resource = use.resource, .range = barrierRange, .sourcePacket = stateSourcePacket, });
                    return readStateSource != nullptr;
                }
            }
        }
    }
    return false;
}


[[nodiscard]] bool PlanTaskResourceStates(GpuTaskGraphResourceStatePlan& plan){
    const GpuTaskGraph::DeclarationReadView& graph = plan.graph;
    const GpuPhysicalQueueTopology& topology = plan.topology;
    const GraphicsVector<GpuTaskId>& topologicalOrder = plan.topologicalOrder;
    GpuTaskGraphCompiledPlanStorage& compiledPlan = plan.compiledPlan;
    Alloc::ScratchArena& scratchArena = plan.scratchArena;
    Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedResourceStates = plan.trackedResourceStates;
    TrackedResourceStateHistory& resourceHistory = plan.resourceHistory;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialOwnershipDependencies = plan.initialOwnershipDependencies;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialAvailabilityDependencies = plan.initialAvailabilityDependencies;
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& stateFragments = plan.stateFragments;
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& taskFirstUseRanges = plan.taskFirstUseRanges;

    usize taskUseCapacity = 0u;
    for(const GpuCompiledTask& compiledTask : compiledPlan.tasks){
        const usize useCount = graph.taskAt(compiledTask.task.index).resourceUseCount;
        if(useCount > taskUseCapacity)
            taskUseCapacity = useCount;
    }
    TaskResourceUseIndex taskUseHistory(graph.resourceCount(), graph.generation(), taskUseCapacity, scratchArena);

    // The construction pass appended one compiled task for each entry in this stable order, and no later phase mutates that vector.
    for(usize taskIndex = 0u; taskIndex < topologicalOrder.size(); ++taskIndex){
        const GpuTaskId taskID = topologicalOrder[taskIndex];
        NWB_ASSERT(taskIndex < compiledPlan.tasks.size() && compiledPlan.tasks[taskIndex].task == taskID);
        GpuCompiledTask& compiledTask = compiledPlan.tasks[taskIndex];

        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        auto& taskQueue = *MakeNotNull(FindCompiledQueueInfo(compiledPlan, compiledTask.queue));
        if(!taskUseHistory.build(task))
            return false;
        compiledTask.prologueStateSeedOffset = static_cast<u32>(compiledPlan.prologueStateSeeds.size());
        compiledTask.prologueBarrierOffset = static_cast<u32>(compiledPlan.prologueBarriers.size());
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            if(resource.type == GpuGraphResourceType::HazardDomain)
                continue;
            // Analysis rejects an unknown required state on every non-HazardDomain use.
            NWB_ASSERT(use.requiredState != ResourceStates::Unknown);
            if(
                (resource.type == GpuGraphResourceType::Texture || resource.type == GpuGraphResourceType::Buffer || resource.type == GpuGraphResourceType::AccelStruct)
                && ResourceUsesConcurrentQueueSharing(resource, topology)
                && !ResourceSharingAdmitsQueue(resource, topology, taskQueue)
            ){
                // Typed resources preserve their exact native family and logical-class admission.
                // Metadata resources retain the logical family resolver.
                // Neither can gain an omitted family through ownership transfer.
                return false;
            }

            const auto plannedRange = ResolveResourceRangeForPlanning(graph, resource, use.range);
            if(!plannedRange)
                return false;

            // A task owns its internal resource ordering.
            // Newly introduced subresources or bytes get graph seeds and transitions; previously declared portions stay local CommandList work.
            // Acceleration structures retain their whole-resource path.
            const bool alreadyPlannedByTask = resource.type == GpuGraphResourceType::AccelStruct && taskUseHistory.first(use.resource) < useIndex;
            if(alreadyPlannedByTask){
                // The task thunk owns this local transition, but the declared final state/access must still become the source for later tasks, packet seeds, ownership handoffs, and terminal exports.
                // Do not emit another packet-boundary barrier for it here.
                if(!resourceHistory.append(TrackedCompiledResourceState{ .resource = use.resource, .range = (*plannedRange), .task = taskID, .state = use.requiredState, .access = use.access, .queue = compiledTask.queue, }))
                    return false;
                continue;
            }

            if(resource.type == GpuGraphResourceType::Texture || resource.type == GpuGraphResourceType::Buffer){
                if(!CollectResourceFirstUseRangesWithinTask(graph, task, taskUseHistory, useIndex, resource, (*plannedRange), scratchArena, taskFirstUseRanges))
                    return false;
                stateFragments.clear();
                if(!CollectLatestResourceStateFragments(trackedResourceStates, resourceHistory, resource, taskFirstUseRanges, scratchArena, stateFragments))
                    return false;

                usize readStateSourceIndex = trackedResourceStates.size();
                for(const TrackedResourceStateFragment& fragment : stateFragments){
                    const TrackedCompiledResourceState* const previousState = fragment.state;
                    const bool joinsReadersForInternalStateChange = previousState
                        && previousState->access == GpuTaskResourceAccess::Read
                        && use.access == GpuTaskResourceAccess::Read
                        && ResourceUsesConcurrentQueueSharing(resource, topology)
                        && !TaskPreservesReadState(graph, task, taskUseHistory, resource, fragment.range, useIndex)
                    ;
                    // Covered later uses transition inside the task, so their outstanding readers must join at entry.
                    if(joinsReadersForInternalStateChange && !AppendOverlappingReaderDependencies(plan, resource, fragment.range, *previousState, compiledTask.packet))
                        return false;
                    const TrackedCompiledResourceState* const readStateSource = previousState && !joinsReadersForInternalStateChange ? FindConcurrentReadStateSource(plan, resource, fragment.range, *previousState, use, taskQueue) : nullptr;
                    const TrackedCompiledResourceState* const stateSource = readStateSource ? readStateSource : previousState;
                    if(readStateSource && stateFragments.size() == 1u && RangeContains(resource, fragment.range, (*plannedRange)))
                        readStateSourceIndex = static_cast<usize>(readStateSource - trackedResourceStates.data());
                    const GpuTaskGraphInitialOwnerHandoffSourceView* initialOwnerHandoffSource = nullptr;
                    GpuCompiledBarrierType::Enum initialOwnerAcquireType = GpuCompiledBarrierType::kCount;
                    if(!previousState){
                        if(resource.initialAvailabilityCompletion.valid()){
                            initialAvailabilityDependencies.push_back(GpuTaskExternalDependencyEdge{ .completion = resource.initialAvailabilityCompletion, .consumer = taskID, });
                        }
                        if(resource.initialOwnerHandoffSourceCount != 0u){
                            initialOwnerHandoffSource = FindInitialOwnerHandoffSource(graph, resource, fragment.range, compiledTask.queue);
                            if(!initialOwnerHandoffSource)
                                return false;
                            initialOwnerAcquireType = OwnershipAcquireBarrierType(resource.type);
                            NWB_ASSERT(initialOwnerAcquireType < GpuCompiledBarrierType::kCount);
                            initialOwnershipDependencies.push_back(GpuTaskExternalDependencyEdge{ .completion = initialOwnerHandoffSource->completion, .consumer = taskID, });
                        }
                        else if(resource.initialOwnerQueue.valid() && resource.initialOwnerQueue != compiledTask.queue)
                            return false;
                    }
                    const ResourceStates::Mask before = previousState ? previousState->state : resource.initialState;
                    const bool hasInitialOwnerStateSeed = !previousState && initialOwnerHandoffSource != nullptr;
                    // An initial-owner handoff opens the packet with its immutable producer snapshot, which materializes the authoritative native starting state.
                    // A known graph initial state still needs an explicit marker after the acquire: it can differ from that snapshot and can be a no-op transition whose declared state must survive.
                    const bool materializesGraphInitialState = !previousState && resource.initialState != ResourceStates::Unknown;
                    const bool requiresExplicitInitialStateSource = !previousState && !hasInitialOwnerStateSeed && resource.hasBackendResource && resource.initialState == ResourceStates::Unknown && IsReadAccess(use.access);
                    if(!previousState && initialOwnerHandoffSource){
                        // The descriptor's one selected source has already been proven to cover this fragment.
                        // Emit only the unseeded fragment range so its immutable snapshot does not overwrite a graph-internal producer's adjacent subresources during packet fan-in.
                        if(!AppendCompiledOwnershipTransfer(plan, resource, fragment.range, GpuTaskId{}, taskID, initialOwnerHandoffSource->sourceQueue, compiledTask.queue, GpuOwnershipTransferRoute::ExternalImport))
                            return false;
                        compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                            .resource = use.resource,
                            .range = fragment.range,
                            .before = before,
                            .after = before,
                            .sourceQueue = initialOwnerHandoffSource->sourceQueue,
                            .destinationQueue = compiledTask.queue,
                            .type = initialOwnerAcquireType,
                            .isInitialOwnerHandoff = true,
                        }
                        );
                    }

                    const bool needsSameStateDependency = previousState && before == use.requiredState && (IsWriteAccess(previousState->access) || IsWriteAccess(use.access));
                    const bool needsUavDependency = needsSameStateDependency && ResourceStates::HasUnorderedAccess(before);
                    const bool compatibleReadStates = previousState
                        && previousState->access == GpuTaskResourceAccess::Read
                        && use.access == GpuTaskResourceAccess::Read
                        && (before == use.requiredState || (resource.type == GpuGraphResourceType::Texture && GraphicsBackend::AreTextureReadStatesCompatible(before, use.requiredState)))
                    ;
                    if(previousState && previousState->access == GpuTaskResourceAccess::Read && before != use.requiredState && !compatibleReadStates && !AppendOverlappingReaderDependencies(plan, resource, fragment.range, *previousState, compiledTask.packet))
                        return false;
                    const auto stateSeed = PlanCrossPacketResourceStateSeed(plan, resource, use, fragment.range, fragment.range, previousState, stateSource, readStateSource, compiledTask, before, needsSameStateDependency);
                    if(!stateSeed)
                        return false;
                    const bool importsReadStateSeed = *stateSeed;
                    const ResourceStates::Mask seededState = importsReadStateSeed ? ReadStateSourceSnapshotState(graph, resource, readStateSource->state) : before;
                    if(materializesGraphInitialState || before != use.requiredState || seededState != use.requiredState || needsSameStateDependency){
                        compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                            .resource = use.resource,
                            .range = fragment.range,
                            .before = seededState,
                            .after = use.requiredState,
                            .sourceQueue = previousState ? previousState->queue : compiledTask.queue,
                            .destinationQueue = compiledTask.queue,
                            .type = needsUavDependency ? UavBarrierType(resource.type) : TransitionBarrierType(resource.type),
                            .isGraphInitialState = materializesGraphInitialState || requiresExplicitInitialStateSource,
                            .forceMemoryDependency = needsSameStateDependency,
                        }
                        );
                    }
                }

                if(!resourceHistory.append(TrackedCompiledResourceState{
                    .resource = use.resource,
                    .range = (*plannedRange),
                    .task = taskID,
                    .state = use.requiredState,
                    .access = use.access,
                    .queue = compiledTask.queue,
                    .readStateSourceIndex = readStateSourceIndex,
                }
                ))
                    return false;
                continue;
            }

            NWB_ASSERT(resource.type == GpuGraphResourceType::AccelStruct);
            const usize previousStateIndex = resourceHistory.last(use.resource);
            const TrackedCompiledResourceState* previousState = previousStateIndex != Limit<usize>::s_Max ? &trackedResourceStates[previousStateIndex] : nullptr;

            const bool joinsReadersForInternalStateChange = previousState
                && previousState->access == GpuTaskResourceAccess::Read
                && use.access == GpuTaskResourceAccess::Read
                && ResourceUsesConcurrentQueueSharing(resource, topology)
                && !TaskPreservesReadState(graph, task, taskUseHistory, resource, (*plannedRange), useIndex)
            ;
            if(joinsReadersForInternalStateChange && !AppendOverlappingReaderDependencies(plan, resource, (*plannedRange), *previousState, compiledTask.packet))
                return false;
            const TrackedCompiledResourceState* const readStateSource = previousState && !joinsReadersForInternalStateChange ? FindConcurrentReadStateSource(plan, resource, (*plannedRange), *previousState, use, taskQueue) : nullptr;
            const TrackedCompiledResourceState* const stateSource = readStateSource ? readStateSource : previousState;
            const usize readStateSourceIndex = readStateSource ? static_cast<usize>(readStateSource - trackedResourceStates.data()) : trackedResourceStates.size();
            const ResourceStates::Mask before = previousState ? previousState->state : resource.initialState;
            if(!previousState && resource.initialAvailabilityCompletion.valid()){
                initialAvailabilityDependencies.push_back(GpuTaskExternalDependencyEdge{ .completion = resource.initialAvailabilityCompletion, .consumer = taskID, });
            }
            const GpuTaskGraphInitialOwnerHandoffSourceView* initialOwnerHandoffSource = nullptr;
            if(!previousState && resource.initialOwnerHandoffSourceCount != 0u){
                initialOwnerHandoffSource = FindInitialOwnerHandoffSource(graph, resource, (*plannedRange), compiledTask.queue);
                if(!initialOwnerHandoffSource)
                    return false;
            }
            else if(!previousState && resource.initialOwnerQueue.valid() && resource.initialOwnerQueue != compiledTask.queue)
                return false;

            const bool hasInitialOwnerStateSeed = initialOwnerHandoffSource != nullptr;
            const bool materializesGraphInitialState = !previousState && resource.initialState != ResourceStates::Unknown;
            const bool requiresExplicitInitialStateSource = !previousState && !hasInitialOwnerStateSeed && resource.hasBackendResource && resource.initialState == ResourceStates::Unknown && IsReadAccess(use.access);
            if(initialOwnerHandoffSource){
                if(!AppendCompiledOwnershipTransfer(plan, resource, (*plannedRange), GpuTaskId{}, taskID, initialOwnerHandoffSource->sourceQueue, compiledTask.queue, GpuOwnershipTransferRoute::ExternalImport))
                    return false;
                compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                    .resource = use.resource,
                    .range = (*plannedRange),
                    .before = before,
                    .after = before,
                    .sourceQueue = initialOwnerHandoffSource->sourceQueue,
                    .destinationQueue = compiledTask.queue,
                    .type = GpuCompiledBarrierType::AccelStructOwnershipAcquire,
                    .isInitialOwnerHandoff = true,
                }
                );
                initialOwnershipDependencies.push_back(GpuTaskExternalDependencyEdge{ .completion = initialOwnerHandoffSource->completion, .consumer = taskID, });
            }
            const bool needsSameStateDependency = previousState && before == use.requiredState && (IsWriteAccess(previousState->access) || IsWriteAccess(use.access));
            const bool needsUavDependency = needsSameStateDependency && ResourceStates::HasUnorderedAccess(before);
            if(previousState && previousState->access == GpuTaskResourceAccess::Read && before != use.requiredState && !AppendOverlappingReaderDependencies(plan, resource, (*plannedRange), *previousState, compiledTask.packet))
                return false;
            const auto stateSeed = PlanCrossPacketResourceStateSeed(plan, resource, use, use.range, (*plannedRange), previousState, stateSource, readStateSource, compiledTask, before, needsSameStateDependency);
            if(!stateSeed)
                return false;
            const bool importsReadStateSeed = *stateSeed;
            const ResourceStates::Mask seededState = importsReadStateSeed ? ReadStateSourceSnapshotState(graph, resource, readStateSource->state) : before;
            if(materializesGraphInitialState || before != use.requiredState || seededState != use.requiredState || needsSameStateDependency){
                compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                    .resource = use.resource,
                    .range = use.range,
                    .before = seededState,
                    .after = use.requiredState,
                    .sourceQueue = previousState ? previousState->queue : compiledTask.queue,
                    .destinationQueue = compiledTask.queue,
                    .type = needsUavDependency ? UavBarrierType(resource.type) : TransitionBarrierType(resource.type),
                    .isGraphInitialState = materializesGraphInitialState || requiresExplicitInitialStateSource,
                    .forceMemoryDependency = needsSameStateDependency,
                }
                );
            }

            if(!resourceHistory.append(TrackedCompiledResourceState{ .resource = use.resource, .range = use.range, .task = taskID, .state = use.requiredState, .access = use.access, .queue = compiledTask.queue, .readStateSourceIndex = readStateSourceIndex, }))
                return false;
        }
        compiledTask.prologueStateSeedCount = static_cast<u32>(compiledPlan.prologueStateSeeds.size()) - compiledTask.prologueStateSeedOffset;
        compiledTask.prologueBarrierCount = static_cast<u32>(compiledPlan.prologueBarriers.size()) - compiledTask.prologueBarrierOffset;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

