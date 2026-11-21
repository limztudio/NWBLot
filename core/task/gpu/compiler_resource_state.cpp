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
        FOUNDATION_ASSERT(false);
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
        FOUNDATION_ASSERT(false);
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
        return GpuCompiledBarrierType::Count;
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
        return GpuCompiledBarrierType::Count;
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
        return GpuCompiledBarrierType::Count;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<b8> PlanCrossPacketResourceStateSeed(
    GpuTaskGraphResourceStatePlan& plan,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceUse& use,
    const GpuTaskResourceRange& barrierRange,
    const GpuTaskResourceRange& readerDependencyRange,
    const TrackedCompiledResourceState& previousState,
    const TrackedCompiledResourceState& stateSource,
    const TrackedCompiledResourceState* const readStateSource,
    const GpuCompiledTask& compiledTask,
    const ResourceStates::Mask before,
    const b8 needsSameStateDependency
){
    GpuTaskGraphCompiledPlanStorage& compiledPlan = plan.compiledPlan;
    const GpuPhysicalQueueTopology& topology = plan.topology;
    auto& pendingEpilogueBarriers = plan.pendingEpilogueBarriers;
    const GpuSubmissionPacketId sourcePacket = FindCompiledPacketForTask(compiledPlan, previousState.task);
    if(!sourcePacket.valid())
        return MakeUnexpected(Failure{});
    if(sourcePacket != compiledTask.packet){
        const GpuPhysicalQueueInfo* const sourceQueue = FindCompiledQueueInfo(compiledPlan, previousState.queue);
        const GpuPhysicalQueueInfo* const destinationQueue = FindCompiledQueueInfo(compiledPlan, compiledTask.queue);
        if(!sourceQueue || !destinationQueue)
            return MakeUnexpected(Failure{});
        const b8 differentQueueFamilies = sourceQueue->familyIndex != destinationQueue->familyIndex;
        const b8 resourceUsesConcurrentSharing = ResourceUsesConcurrentQueueSharing(resource, topology);
        const b8 concurrentQueuePair = ResourceSharesQueuePairConcurrently(resource, topology, *sourceQueue, *destinationQueue);
        if(differentQueueFamilies && resourceUsesConcurrentSharing && !concurrentQueuePair){
            return MakeUnexpected(Failure{});
        }

        const b8 requiresExclusiveOwnershipHandoff = !resourceUsesConcurrentSharing && differentQueueFamilies;
        if(requiresExclusiveOwnershipHandoff){
            if(previousState.access == GpuTaskResourceAccess::Read && !AppendOverlappingReaderDependencies(plan, resource, readerDependencyRange, previousState, sourcePacket))
                return MakeUnexpected(Failure{});
            const GpuCompiledBarrierType::Enum releaseType = OwnershipReleaseBarrierType(resource.type);
            const GpuCompiledBarrierType::Enum acquireType = OwnershipAcquireBarrierType(resource.type);
            FOUNDATION_ASSERT(releaseType < GpuCompiledBarrierType::Count && acquireType < GpuCompiledBarrierType::Count);
            if(!AppendCompiledOwnershipTransfer(plan, resource, barrierRange, previousState.task, compiledTask.task, previousState.queue, compiledTask.queue, GpuOwnershipTransferRoute::Internal))
                return MakeUnexpected(Failure{});
            pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                .task = previousState.task,
                .barrier = GpuCompiledBarrier{ .resource = use.resource, .range = barrierRange, .before = before, .after = before, .sourceQueue = previousState.queue, .destinationQueue = compiledTask.queue, .type = releaseType, },
            });
            plan.resourceHasPendingEpilogueBarrier[resource.id.index] = 1u;
            compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                .resource = use.resource,
                .range = barrierRange,
                .before = before,
                .after = before,
                .sourceQueue = previousState.queue,
                .destinationQueue = compiledTask.queue,
                .type = acquireType,
            });
        }
        const b8 readOnlySameState = previousState.access == GpuTaskResourceAccess::Read && use.access == GpuTaskResourceAccess::Read && before == use.requiredState && !needsSameStateDependency;
        const b8 mayOmitInternalStateSeed = use.hasIndependentStateSource && readOnlySameState && concurrentQueuePair;
        if(!mayOmitInternalStateSeed){
            const GpuSubmissionPacketId stateSourcePacket = FindCompiledPacketForTask(compiledPlan, stateSource.task);
            if(!stateSourcePacket.valid())
                return MakeUnexpected(Failure{});
            if(stateSourcePacket != compiledTask.packet){
                compiledPlan.prologueStateSeeds.push_back(GpuPacketStateSeed{ .resource = use.resource, .range = barrierRange, .sourcePacket = stateSourcePacket, });
                return readStateSource != nullptr;
            }
        }
    }
    return false;
}


[[nodiscard]] b8 PlanTaskResourceStates(GpuTaskGraphResourceStatePlan& plan){
    const GpuTaskGraph::DeclarationReadView& graph = plan.graph;
    const GpuPhysicalQueueTopology& topology = plan.topology;
    const Span<const GpuTaskId> topologicalOrder = plan.topologicalOrder;
    GpuTaskGraphCompiledPlanStorage& compiledPlan = plan.compiledPlan;
    Alloc::ScratchArena& scratchArena = plan.scratchArena;
    Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedResourceStates = plan.trackedResourceStates;
    TrackedResourceStateHistory& resourceHistory = plan.resourceHistory;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialOwnershipDependencies = plan.initialOwnershipDependencies;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialAvailabilityDependencies = plan.initialAvailabilityDependencies;
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena>& stateFragments = plan.stateFragments;
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& taskFirstUseRanges = plan.taskFirstUseRanges;

    usize taskUseCapacity = 0u;
    b8 hasNonHazardDomainUse = false;
    for(const GpuCompiledTask& compiledTask : compiledPlan.tasks){
        const GpuTaskGraphDetail::GpuTaskNode& taskNode = CompilerGraphAccess::TaskNode(graph, compiledTask.task.index);
        const usize useCount = taskNode.resourceUseCount;
        if(useCount > taskUseCapacity)
            taskUseCapacity = useCount;
        if(!hasNonHazardDomainUse && useCount != 0u){
            for(const GpuTaskResourceUse& use : CompilerGraphAccess::TaskResourceUses(graph, taskNode)){
                if(CompilerGraphAccess::ResourceNode(graph, use.resource.index).type != GpuGraphResourceType::HazardDomain){
                    hasNonHazardDomainUse = true;
                    break;
                }
            }
        }
    }
    if(!hasNonHazardDomainUse)
        return true;

    struct ResourceShareCacheEntry{
        u64 admittedQueueMask = 0u;
        b8 initialized = false;
        b8 isConcurrent = false;
    };
    Vector<ResourceShareCacheEntry, Alloc::ScratchArena> shareCache(CompilerGraphAccess::ResourceCount(graph), scratchArena);
    const b8 useShareBitmask = topology.queueCount <= s_BitsPerWord;
    const auto resolveResourceShare = [&](const u32 resourceIndex, const GpuTaskGraphResourceView& resource)noexcept -> const ResourceShareCacheEntry&{
        ResourceShareCacheEntry& entry = shareCache[resourceIndex];
        if(!entry.initialized){
            entry.initialized = true;
            entry.isConcurrent = ResourceUsesConcurrentQueueSharing(resource, topology);
            if(entry.isConcurrent && useShareBitmask){
                u64 mask = 0u;
                for(usize q = 0u; q < topology.queueCount; ++q){
                    if(ResourceSharingAdmitsQueue(resource, topology, topology.queues[q]))
                        mask |= BitMask<u64>(static_cast<u32>(q));
                }
                entry.admittedQueueMask = mask;
            }
        }
        return entry;
    };

    TaskResourceUseIndex taskUseHistory(CompilerGraphAccess::ResourceCount(graph), CompilerGraphAccess::Generation(graph), taskUseCapacity, scratchArena);

    for(usize taskIndex = 0u; taskIndex < topologicalOrder.size(); ++taskIndex){
        const GpuTaskId taskID = topologicalOrder[taskIndex];
        FOUNDATION_ASSERT(taskIndex < compiledPlan.tasks.size() && compiledPlan.tasks[taskIndex].task == taskID);
        GpuCompiledTask& compiledTask = compiledPlan.tasks[taskIndex];

        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        auto& taskQueue = *MakeNotNull(FindCompiledQueueInfo(compiledPlan, compiledTask.queue));
        const usize taskQueueIndex = static_cast<usize>(&taskQueue - compiledPlan.queueTopology.data());
        const b8 multipleUses = task.resourceUseCount > 1u;
        if(multipleUses && !taskUseHistory.build(task))
            return false;
        constexpr usize s_OffsetMax = static_cast<usize>(Limit<u32>::s_Max);
        if(compiledPlan.prologueStateSeeds.size() > s_OffsetMax || compiledPlan.prologueBarriers.size() > s_OffsetMax)
            return false;
        compiledTask.prologueStateSeedOffset = static_cast<u32>(compiledPlan.prologueStateSeeds.size());
        compiledTask.prologueBarrierOffset = static_cast<u32>(compiledPlan.prologueBarriers.size());
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            if(CompilerGraphAccess::ResourceNode(graph, use.resource.index).type == GpuGraphResourceType::HazardDomain)
                continue;
            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            FOUNDATION_ASSERT(use.requiredState != ResourceStates::Unknown);
            const ResourceShareCacheEntry& shareInfo = resolveResourceShare(use.resource.index, resource);
            if(
                (resource.type == GpuGraphResourceType::Texture || resource.type == GpuGraphResourceType::Buffer || resource.type == GpuGraphResourceType::AccelStruct)
                && shareInfo.isConcurrent
                && !(useShareBitmask && taskQueueIndex < compiledPlan.queueTopology.size() ? ((shareInfo.admittedQueueMask & BitMask<u64>(static_cast<u32>(taskQueueIndex))) != 0u) : ResourceSharingAdmitsQueue(resource, topology, taskQueue))
            ){
                return false;
            }

            const auto plannedRange = ResolveResourceRangeForPlanning(graph, resource, use.range);
            if(!plannedRange)
                return false;

            const b8 alreadyPlannedByTask = multipleUses && resource.type == GpuGraphResourceType::AccelStruct && taskUseHistory.first(use.resource) < useIndex;
            if(alreadyPlannedByTask){
                if(!resourceHistory.append(TrackedCompiledResourceState{ .resource = use.resource, .range = (*plannedRange), .task = taskID, .state = use.requiredState, .access = use.access, .queue = compiledTask.queue, }))
                    return false;
                continue;
            }

            if(resource.type == GpuGraphResourceType::Texture || resource.type == GpuGraphResourceType::Buffer){
                if(multipleUses){
                    if(!CollectResourceFirstUseRangesWithinTaskWithScratch(graph, task, taskUseHistory, useIndex, resource, (*plannedRange), plan.rangeScratch, taskFirstUseRanges))
                        return false;
                }
                else{
                    taskFirstUseRanges.clear();
                    taskFirstUseRanges.push_back(*plannedRange);
                }
                stateFragments.clear();
                if(!CollectLatestResourceStateFragmentsWithScratch(trackedResourceStates, resourceHistory, resource, taskFirstUseRanges, plan.rangeScratch, stateFragments))
                    return false;

                usize readStateSourceIndex = trackedResourceStates.size();
                for(const TrackedResourceStateFragment& fragment : stateFragments){
                    const TrackedCompiledResourceState* const previousState = fragment.state;
                    const b8 joinsReadersForInternalStateChange = previousState
                        && previousState->access == GpuTaskResourceAccess::Read
                        && use.access == GpuTaskResourceAccess::Read
                        && shareInfo.isConcurrent
                        && multipleUses
                        && !TaskPreservesReadState(graph, task, taskUseHistory, resource, fragment.range, useIndex)
                    ;
                    if(joinsReadersForInternalStateChange && !AppendOverlappingReaderDependencies(plan, resource, fragment.range, *previousState, compiledTask.packet))
                        return false;
                    const TrackedCompiledResourceState* const readStateSource = previousState && !joinsReadersForInternalStateChange ? FindConcurrentReadStateSource(plan, resource, fragment.range, *previousState, use, taskQueue) : nullptr;
                    const TrackedCompiledResourceState* const stateSource = readStateSource ? readStateSource : previousState;
                    if(readStateSource && stateFragments.size() == 1u && RangeContains(resource, fragment.range, (*plannedRange)))
                        readStateSourceIndex = static_cast<usize>(readStateSource - trackedResourceStates.data());
                    const GpuTaskGraphInitialOwnerHandoffSourceView* initialOwnerHandoffSource = nullptr;
                    GpuCompiledBarrierType::Enum initialOwnerAcquireType = GpuCompiledBarrierType::Count;
                    if(!previousState){
                        if(resource.initialAvailabilityCompletion.valid()){
                            initialAvailabilityDependencies.push_back(GpuTaskExternalDependencyEdge{ .completion = resource.initialAvailabilityCompletion, .consumer = taskID, });
                        }
                        if(resource.initialOwnerHandoffSourceCount != 0u){
                            initialOwnerHandoffSource = FindInitialOwnerHandoffSource(resource, fragment.range, compiledTask.queue, nullptr, &graph);
                            if(!initialOwnerHandoffSource)
                                return false;
                            initialOwnerAcquireType = OwnershipAcquireBarrierType(resource.type);
                            FOUNDATION_ASSERT(initialOwnerAcquireType < GpuCompiledBarrierType::Count);
                            initialOwnershipDependencies.push_back(GpuTaskExternalDependencyEdge{ .completion = initialOwnerHandoffSource->completion, .consumer = taskID, });
                        }
                        else if(resource.initialOwnerQueue.valid() && resource.initialOwnerQueue != compiledTask.queue)
                            return false;
                    }
                    const ResourceStates::Mask before = previousState ? previousState->state : resource.initialState;
                    const b8 hasInitialOwnerStateSeed = !previousState && initialOwnerHandoffSource != nullptr;
                    const b8 materializesGraphInitialState = !previousState && resource.initialState != ResourceStates::Unknown;
                    const b8 requiresExplicitInitialStateSource = !previousState && !hasInitialOwnerStateSeed && resource.hasBackendResource && resource.initialState == ResourceStates::Unknown && IsReadAccess(use.access);
                    if(!previousState && initialOwnerHandoffSource){
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
                        });
                    }

                    const b8 needsSameStateDependency = previousState && before == use.requiredState && (IsWriteAccess(previousState->access) || IsWriteAccess(use.access));
                    const b8 needsUavDependency = needsSameStateDependency && ResourceStates::HasUnorderedAccess(before);
                    const b8 compatibleReadStates = previousState
                        && previousState->access == GpuTaskResourceAccess::Read
                        && use.access == GpuTaskResourceAccess::Read
                        && (before == use.requiredState || (resource.type == GpuGraphResourceType::Texture && GraphicsBackend::AreTextureReadStatesCompatible(before, use.requiredState)))
                    ;
                    if(previousState && previousState->access == GpuTaskResourceAccess::Read && before != use.requiredState && !compatibleReadStates && !AppendOverlappingReaderDependencies(plan, resource, fragment.range, *previousState, compiledTask.packet))
                        return false;
                    const Expected<b8> stateSeed = previousState
                        ? PlanCrossPacketResourceStateSeed(plan, resource, use, fragment.range, fragment.range, *previousState, *stateSource, readStateSource, compiledTask, before, needsSameStateDependency)
                        : Expected<b8>(false)
                    ;
                    if(!stateSeed)
                        return false;
                    const b8 importsReadStateSeed = *stateSeed;
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
                        });
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
                }))
                    return false;
                continue;
            }

            FOUNDATION_ASSERT(resource.type == GpuGraphResourceType::AccelStruct);
            const usize previousStateIndex = resourceHistory.last(use.resource);
            const TrackedCompiledResourceState* previousState = previousStateIndex != Limit<usize>::s_Max ? &trackedResourceStates[previousStateIndex] : nullptr;

            const b8 joinsReadersForInternalStateChange = previousState
                && previousState->access == GpuTaskResourceAccess::Read
                && use.access == GpuTaskResourceAccess::Read
                && shareInfo.isConcurrent
                && multipleUses
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
                initialOwnerHandoffSource = FindInitialOwnerHandoffSource(resource, (*plannedRange), compiledTask.queue, nullptr, &graph);
                if(!initialOwnerHandoffSource)
                    return false;
            }
            else if(!previousState && resource.initialOwnerQueue.valid() && resource.initialOwnerQueue != compiledTask.queue)
                return false;

            const b8 hasInitialOwnerStateSeed = initialOwnerHandoffSource != nullptr;
            const b8 materializesGraphInitialState = !previousState && resource.initialState != ResourceStates::Unknown;
            const b8 requiresExplicitInitialStateSource = !previousState && !hasInitialOwnerStateSeed && resource.hasBackendResource && resource.initialState == ResourceStates::Unknown && IsReadAccess(use.access);
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
                });
                initialOwnershipDependencies.push_back(GpuTaskExternalDependencyEdge{ .completion = initialOwnerHandoffSource->completion, .consumer = taskID, });
            }
            const b8 needsSameStateDependency = previousState && before == use.requiredState && (IsWriteAccess(previousState->access) || IsWriteAccess(use.access));
            const b8 needsUavDependency = needsSameStateDependency && ResourceStates::HasUnorderedAccess(before);
            if(previousState && previousState->access == GpuTaskResourceAccess::Read && before != use.requiredState && !AppendOverlappingReaderDependencies(plan, resource, (*plannedRange), *previousState, compiledTask.packet))
                return false;
            const Expected<b8> stateSeed = previousState ? PlanCrossPacketResourceStateSeed(plan, resource, use, use.range, (*plannedRange), *previousState, *stateSource, readStateSource, compiledTask, before, needsSameStateDependency) : Expected<b8>(false);
            if(!stateSeed)
                return false;
            const b8 importsReadStateSeed = *stateSeed;
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
                });
            }

            if(!resourceHistory.append(TrackedCompiledResourceState{ .resource = use.resource, .range = use.range, .task = taskID, .state = use.requiredState, .access = use.access, .queue = compiledTask.queue, .readStateSourceIndex = readStateSourceIndex, }))
                return false;
        }
        if(compiledPlan.prologueStateSeeds.size() > s_OffsetMax || compiledPlan.prologueBarriers.size() > s_OffsetMax)
            return false;
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

