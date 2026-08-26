// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] GpuCompiledBarrierType::Enum TransitionBarrierType(
    const GpuGraphResourceType::Enum resourceType
)noexcept{
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

[[nodiscard]] GpuCompiledBarrierType::Enum UavBarrierType(
    const GpuGraphResourceType::Enum resourceType
)noexcept{
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

[[nodiscard]] GpuCompiledBarrierType::Enum StateExportBarrierType(
    const GpuGraphResourceType::Enum resourceType
)noexcept{
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

[[nodiscard]] GpuCompiledBarrierType::Enum OwnershipReleaseBarrierType(
    const GpuGraphResourceType::Enum resourceType
)noexcept{
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

[[nodiscard]] GpuCompiledBarrierType::Enum OwnershipAcquireBarrierType(
    const GpuGraphResourceType::Enum resourceType
)noexcept{
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
    const GpuTaskGraphResourceView& resource,
    const Texture* const texture,
    const GpuTaskResourceRange& firstUseRange,
    const GpuPhysicalQueueId& destinationQueue
)noexcept{
    if(
        resource.initialOwnerHandoffSourceCount == 0u
        || !resource.initialOwnerHandoffSources
    )
        return nullptr;

    const GpuTaskGraphInitialOwnerHandoffSourceView* result = nullptr;
    for(usize sourceIndex = 0u;
        sourceIndex < resource.initialOwnerHandoffSourceCount;
        ++sourceIndex
    ){
        const GpuTaskGraphInitialOwnerHandoffSourceView& source = resource.initialOwnerHandoffSources[sourceIndex];
        GpuTaskResourceRange plannedSourceRange;
        if(
            source.destinationQueue != destinationQueue
            || !ResolveTextureRangeForPlanning(texture, source.range, plannedSourceRange)
            || !RangeContains(resource, plannedSourceRange, firstUseRange)
        )
            continue;
        // One first graph use must have one exact external owner. A broader range that straddles two released mips
        // cannot safely select one state source or one completion token, so reject it rather than guessing.
        if(result)
            return nullptr;
        result = &source;
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool PlanTaskResourceStates(GpuTaskGraphResourceStatePlan& plan){
    const GpuTaskGraph& graph = plan.graph;
    const GpuTaskGraphQueueTopology& topology = plan.topology;
    const GraphicsVector<GpuTaskId>& topologicalOrder = plan.topologicalOrder;
    GpuTaskGraphCompiledPlanStorage& compiledPlan = plan.compiledPlan;
    Alloc::ScratchArena& scratchArena = plan.scratchArena;
    Vector<TrackedCompiledResourceState, Alloc::ScratchArena>& trackedResourceStates = plan.trackedResourceStates;
    Vector<PendingCompiledEpilogueBarrier, Alloc::ScratchArena>& pendingEpilogueBarriers = plan.pendingEpilogueBarriers;
    Vector<GpuTaskExternalDependencyEdge, Alloc::ScratchArena>& initialOwnershipDependencies = plan.initialOwnershipDependencies;
    Vector<TrackedTextureStateFragment, Alloc::ScratchArena>& stateFragments = plan.stateFragments;
    Vector<GpuTaskResourceRange, Alloc::ScratchArena>& taskFirstUseRanges = plan.taskFirstUseRanges;

    // The construction pass appended one compiled task for each entry in this stable order, and no later phase
    // mutates that vector. Preserve the prior fail-closed contract while avoiding a re-scan for every task.
    for(usize taskIndex = 0u; taskIndex < topologicalOrder.size(); ++taskIndex){
        const GpuTaskId taskID = topologicalOrder[taskIndex];
        if(
            taskIndex >= compiledPlan.tasks.size()
            || compiledPlan.tasks[taskIndex].task != taskID
        )
            return false;
        GpuCompiledTask* const compiledTask = &compiledPlan.tasks[taskIndex];

        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        const GpuPhysicalQueueInfo* const taskQueue = FindCompiledQueueInfo(compiledPlan, compiledTask->queue);
        if(!taskQueue)
            return false;
        compiledTask->prologueStateSeedOffset = static_cast<u32>(compiledPlan.prologueStateSeeds.size());
        compiledTask->prologueBarrierOffset = static_cast<u32>(compiledPlan.prologueBarriers.size());
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            if(resource.type == GpuGraphResourceType::HazardDomain || use.requiredState == ResourceStates::Unknown)
                continue;
            if(
                (
                    resource.type == GpuGraphResourceType::Texture
                    || resource.type == GpuGraphResourceType::Buffer
                    || resource.type == GpuGraphResourceType::AccelStruct
                )
                && ResourceUsesConcurrentQueueSharing(resource.queueSharing, topology)
                && !ResourceSharingIncludesQueueFamily(resource.queueSharing, topology, taskQueue->familyIndex)
            ){
                // Vulkan concurrent sharing admits native families, not graph queue-class labels. A Transfer queue
                // may therefore use a family already admitted through AsyncCompute, while an omitted family remains
                // illegal because concurrent resources cannot gain it through an ownership transfer.
                return false;
            }

            const Texture* const typedTexture = resource.type == GpuGraphResourceType::Texture
                ? graph.textureForResource(use.resource)
                : nullptr
            ;
            GpuTaskResourceRange plannedRange = use.range;
            if(
                resource.type == GpuGraphResourceType::Texture
                && !ResolveTextureRangeForPlanning(typedTexture, use.range, plannedRange)
            )
                return false;

            // A task owns its internal resource ordering. Texture portions already declared earlier in this task
            // remain local CommandList work, while newly introduced subresources still receive normal graph seeds
            // and transitions. Buffers and acceleration structures retain their intentionally whole-resource path.
            bool alreadyPlannedByTask = false;
            if(resource.type != GpuGraphResourceType::Texture){
                for(usize previousUseIndex = 0u; previousUseIndex < useIndex; ++previousUseIndex){
                    const GpuTaskResourceUse& previousUse = task.resourceUses[previousUseIndex];
                    if(previousUse.resource != use.resource)
                        continue;

                    if(RangesOverlap(resource, previousUse.range, plannedRange)){
                        alreadyPlannedByTask = true;
                        break;
                    }
                }
            }
            if(alreadyPlannedByTask){
                // The task thunk owns this local transition, but the declared final state/access must still become
                // the source for later tasks, packet seeds, ownership handoffs, and terminal exports. Do not emit
                // another packet-boundary barrier for it here.
                trackedResourceStates.push_back(TrackedCompiledResourceState{
                    .resource = use.resource,
                    .range = plannedRange,
                    .state = use.requiredState,
                    .access = use.access,
                    .task = taskID,
                    .queue = compiledTask->queue,
                });
                continue;
            }

            if(resource.type == GpuGraphResourceType::Texture){
                if(!CollectTextureFirstUseRangesWithinTask(
                    task,
                    useIndex,
                    use.resource,
                    typedTexture,
                    plannedRange,
                    scratchArena,
                    taskFirstUseRanges
                ))
                    return false;
                stateFragments.clear();
                if(!CollectLatestTextureStateFragments(
                    trackedResourceStates,
                    use.resource,
                    taskFirstUseRanges,
                    scratchArena,
                    stateFragments
                ))
                    return false;

                for(const TrackedTextureStateFragment& fragment : stateFragments){
                    const TrackedCompiledResourceState* const previousState = fragment.state;
                    const GpuTaskGraphInitialOwnerHandoffSourceView* initialOwnerHandoffSource = nullptr;
                    bool usesInitialOwnerOnlyHandoff = false;
                    GpuCompiledBarrierType::Enum initialOwnerAcquireType = GpuCompiledBarrierType::kCount;
                    if(!previousState){
                        if(
                            resource.initialOwnerReleaseDestinationQueue.valid()
                            && resource.initialOwnerReleaseDestinationQueue != compiledTask->queue
                        ){
                            // The descriptor's release destination stays authoritative. Resolve ownership only for
                            // the exact first-use fragment so an earlier local task use cannot broaden the source.
                            return false;
                        }
                        if(resource.initialOwnerHandoffSourceCount != 0u){
                            initialOwnerHandoffSource = FindInitialOwnerHandoffSource(
                                resource,
                                typedTexture,
                                fragment.range,
                                compiledTask->queue
                            );
                            if(!initialOwnerHandoffSource)
                                return false;
                            initialOwnerAcquireType = OwnershipAcquireBarrierType(resource.type);
                            if(initialOwnerAcquireType >= GpuCompiledBarrierType::kCount)
                                return false;
                            initialOwnershipDependencies.push_back(GpuTaskExternalDependencyEdge{
                                .completion = initialOwnerHandoffSource->completion,
                                .consumer = taskID,
                            });
                        }
                        else if(
                            resource.initialOwnerQueue.valid()
                            && resource.initialOwnerQueue != compiledTask->queue
                        ){
                            if(
                                !resource.initialOwnerReleaseDestinationQueue.valid()
                                || resource.initialOwnerReleaseDestinationQueue != compiledTask->queue
                                || !resource.initialOwnerCompletion.valid()
                                || !resource.initialOwnerStateSource
                            ){
                                // Owner-only imports retain the original exact-queue restriction. A different first
                                // consumer needs all three explicit pieces of an external handoff: fixed destination,
                                // completion, and exported native state source.
                                return false;
                            }
                            initialOwnerAcquireType = OwnershipAcquireBarrierType(resource.type);
                            if(initialOwnerAcquireType >= GpuCompiledBarrierType::kCount)
                                return false;
                            usesInitialOwnerOnlyHandoff = true;
                            initialOwnershipDependencies.push_back(GpuTaskExternalDependencyEdge{
                                .completion = resource.initialOwnerCompletion,
                                .consumer = taskID,
                            });
                        }
                    }
                    const ResourceStates::Mask before = previousState ? previousState->state : resource.initialState;
                    const bool hasInitialOwnerStateSeed =
                        !previousState
                        && (initialOwnerHandoffSource != nullptr || usesInitialOwnerOnlyHandoff)
                    ;
                    // An initial-owner handoff opens the packet with its immutable producer snapshot, which
                    // materializes the authoritative native starting state. A known graph initial state still
                    // needs an explicit marker after the acquire: it can differ from that snapshot and can be a
                    // no-op transition whose declared state must survive into the packet snapshot.
                    const bool materializesGraphInitialState =
                        !previousState
                        && resource.initialState != ResourceStates::Unknown
                    ;
                    const bool requiresExplicitInitialStateSource =
                        !previousState
                        && !hasInitialOwnerStateSeed
                        && resource.hasBackendResource
                        && resource.initialState == ResourceStates::Unknown
                        && IsReadAccess(use.access)
                    ;
                    if(!previousState && initialOwnerHandoffSource){
                        // The descriptor's one selected source has already been proven to cover this fragment.
                        // Emit only the unseeded fragment range so its immutable snapshot does not
                        // overwrite a graph-internal producer's adjacent subresources during packet fan-in.
                        compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                            .resource = use.resource,
                            .range = fragment.range,
                            .before = before,
                            .after = before,
                            .sourceQueue = initialOwnerHandoffSource->sourceQueue,
                            .destinationQueue = compiledTask->queue,
                            .type = initialOwnerAcquireType,
                            .isInitialOwnerHandoff = true,
                        });
                    }
                    else if(!previousState && usesInitialOwnerOnlyHandoff){
                        // This marker imports the descriptor-owned snapshot before normal graph state fragments,
                        // allowing CommandList::open to emit the paired acquire only for the uncovered range.
                        compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                            .resource = use.resource,
                            .range = fragment.range,
                            .before = before,
                            .after = before,
                            .sourceQueue = resource.initialOwnerQueue,
                            .destinationQueue = compiledTask->queue,
                            .type = initialOwnerAcquireType,
                            .isInitialOwnerHandoff = true,
                        });
                    }

                    const bool needsUavDependency =
                        previousState
                        && before == use.requiredState
                        && ResourceStates::HasUnorderedAccess(before)
                        && (IsWriteAccess(previousState->access) || IsWriteAccess(use.access))
                    ;
                    if(previousState){
                        const GpuSubmissionPacketId sourcePacket = FindCompiledPacketForTask(
                            compiledPlan,
                            previousState->task
                        );
                        if(!sourcePacket.valid())
                            return false;
                        if(sourcePacket != compiledTask->packet){
                            const GpuPhysicalQueueInfo* const sourceQueue = FindCompiledQueueInfo(
                                compiledPlan,
                                previousState->queue
                            );
                            const GpuPhysicalQueueInfo* const destinationQueue = FindCompiledQueueInfo(
                                compiledPlan,
                                compiledTask->queue
                            );
                            if(!sourceQueue || !destinationQueue)
                                return false;
                            const bool differentQueueFamilies = sourceQueue->familyIndex != destinationQueue->familyIndex;
                            const bool resourceUsesConcurrentSharing = ResourceUsesConcurrentQueueSharing(
                                resource.queueSharing,
                                topology
                            );
                            const bool concurrentQueuePair = ResourceSharesQueuePairConcurrently(
                                resource.queueSharing,
                                topology,
                                *sourceQueue,
                                *destinationQueue
                            );
                            if(differentQueueFamilies && resourceUsesConcurrentSharing && !concurrentQueuePair){
                                // The resource was created concurrent for another family set. Vulkan cannot
                                // transfer ownership to an omitted family, so fail compilation instead of
                                // recording invalid work.
                                return false;
                            }

                            const bool requiresExclusiveOwnershipHandoff =
                                !resourceUsesConcurrentSharing
                                && differentQueueFamilies
                            ;
                            if(requiresExclusiveOwnershipHandoff){
                                const GpuCompiledBarrierType::Enum releaseType = OwnershipReleaseBarrierType(resource.type);
                                const GpuCompiledBarrierType::Enum acquireType = OwnershipAcquireBarrierType(resource.type);
                                if(
                                    releaseType >= GpuCompiledBarrierType::kCount
                                    || acquireType >= GpuCompiledBarrierType::kCount
                                )
                                    return false;
                                pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                                    .task = previousState->task,
                                    .barrier = GpuCompiledBarrier{
                                        .resource = use.resource,
                                        .range = fragment.range,
                                        .before = before,
                                        .after = before,
                                        .sourceQueue = previousState->queue,
                                        .destinationQueue = compiledTask->queue,
                                        .type = releaseType,
                                    },
                                });
                                compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                                    .resource = use.resource,
                                    .range = fragment.range,
                                    .before = before,
                                    .after = before,
                                    .sourceQueue = previousState->queue,
                                    .destinationQueue = compiledTask->queue,
                                    .type = acquireType,
                                });
                            }
                            const bool readOnlySameState =
                                previousState->access == GpuTaskResourceAccess::Read
                                && use.access == GpuTaskResourceAccess::Read
                                && before == use.requiredState
                                && !needsUavDependency
                            ;
                            const bool mayOmitInternalStateSeed =
                                use.hasIndependentStateSource
                                && readOnlySameState
                                && concurrentQueuePair
                            ;
                            if(!mayOmitInternalStateSeed){
                                compiledPlan.prologueStateSeeds.push_back(GpuPacketStateSeed{
                                    .resource = use.resource,
                                    .range = fragment.range,
                                    .sourcePacket = sourcePacket,
                                });
                            }
                        }
                    }
                    if(materializesGraphInitialState || before != use.requiredState || needsUavDependency){
                        compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                            .resource = use.resource,
                            .range = fragment.range,
                            .before = before,
                            .after = use.requiredState,
                            .sourceQueue = previousState ? previousState->queue : compiledTask->queue,
                            .destinationQueue = compiledTask->queue,
                            .type = needsUavDependency
                                ? UavBarrierType(resource.type)
                                : TransitionBarrierType(resource.type),
                            .isGraphInitialState = materializesGraphInitialState || requiresExplicitInitialStateSource,
                        });
                    }
                }

                trackedResourceStates.push_back(TrackedCompiledResourceState{
                    .resource = use.resource,
                    .range = plannedRange,
                    .state = use.requiredState,
                    .access = use.access,
                    .task = taskID,
                    .queue = compiledTask->queue,
                });
                continue;
            }

            const TrackedCompiledResourceState* previousState = nullptr;
            for(usize stateIndex = trackedResourceStates.size(); stateIndex > 0u; --stateIndex){
                const TrackedCompiledResourceState& candidate = trackedResourceStates[stateIndex - 1u];
                if(
                    candidate.resource == use.resource
                    && RangesOverlap(resource, candidate.range, use.range)
                ){
                    previousState = &candidate;
                    break;
                }
            }

            const ResourceStates::Mask before = previousState ? previousState->state : resource.initialState;
            // An initial-owner handoff opens the packet with its immutable producer snapshot, which materializes
            // the authoritative native starting state. A known graph initial state still needs an explicit marker
            // after the acquire: it can differ from that snapshot and can be a no-op transition whose declared
            // state must survive into the packet snapshot.
            const bool hasInitialOwnerStateSeed =
                !previousState
                && (
                    resource.initialOwnerHandoffSourceCount != 0u
                    || (
                        resource.initialOwnerReleaseDestinationQueue.valid()
                        && resource.initialOwnerStateSource != nullptr
                    )
                )
            ;
            const bool materializesGraphInitialState =
                !previousState
                && resource.initialState != ResourceStates::Unknown
            ;
            const bool requiresExplicitInitialStateSource =
                !previousState
                && !hasInitialOwnerStateSeed
                && resource.hasBackendResource
                && resource.initialState == ResourceStates::Unknown
                && IsReadAccess(use.access)
            ;
            if(
                !previousState
                && resource.initialOwnerReleaseDestinationQueue.valid()
                && resource.initialOwnerReleaseDestinationQueue != compiledTask->queue
            ){
                // A producer that already released ownership has relinquished it even when the source happens to
                // be this task's broad queue class. Its fixed release destination remains authoritative.
                return false;
            }
            if(!previousState && resource.initialOwnerHandoffSourceCount != 0u){
                const GpuTaskGraphInitialOwnerHandoffSourceView* const source = FindInitialOwnerHandoffSource(
                    resource,
                    nullptr,
                    use.range,
                    compiledTask->queue
                );
                if(!source)
                    return false;
                const GpuCompiledBarrierType::Enum acquireType = OwnershipAcquireBarrierType(resource.type);
                if(acquireType >= GpuCompiledBarrierType::kCount)
                    return false;
                // The packet recorder imports the exact immutable source range before prologue lowering. This marker
                // keeps one source owner and one completion bound to every first consumer range, including a
                // same-physical-queue source whose timeline wait still proves the external producer accepted.
                compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                    .resource = use.resource,
                    .range = use.range,
                    .before = before,
                    .after = before,
                    .sourceQueue = source->sourceQueue,
                    .destinationQueue = compiledTask->queue,
                    .type = acquireType,
                    .isInitialOwnerHandoff = true,
                });
                initialOwnershipDependencies.push_back(GpuTaskExternalDependencyEdge{
                    .completion = source->completion,
                    .consumer = taskID,
                });
            }
            else if(!previousState && resource.initialOwnerQueue.valid() && resource.initialOwnerQueue != compiledTask->queue){
                if(
                    !resource.initialOwnerReleaseDestinationQueue.valid()
                    || resource.initialOwnerReleaseDestinationQueue != compiledTask->queue
                    || !resource.initialOwnerCompletion.valid()
                    || !resource.initialOwnerStateSource
                ){
                    // Owner-only imports retain the original exact-queue restriction. A different first consumer
                    // needs all three explicit pieces of an external handoff: fixed destination, completion, and
                    // exported native state source.
                    return false;
                }
                const GpuCompiledBarrierType::Enum acquireType = OwnershipAcquireBarrierType(resource.type);
                if(acquireType >= GpuCompiledBarrierType::kCount)
                    return false;
                // The recorder imports the descriptor-owned state snapshot before prologue lowering. That emits
                // the native paired acquire (if families differ); this immutable marker also proves the snapshot
                // and the external completion belong to this first range consumer.
                compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                    .resource = use.resource,
                    .range = use.range,
                    .before = before,
                    .after = before,
                    .sourceQueue = resource.initialOwnerQueue,
                    .destinationQueue = compiledTask->queue,
                    .type = acquireType,
                    .isInitialOwnerHandoff = true,
                });
                initialOwnershipDependencies.push_back(GpuTaskExternalDependencyEdge{
                    .completion = resource.initialOwnerCompletion,
                    .consumer = taskID,
                });
            }
            const bool needsUavDependency =
                previousState
                && before == use.requiredState
                && ResourceStates::HasUnorderedAccess(before)
                && (IsWriteAccess(previousState->access) || IsWriteAccess(use.access))
            ;
            if(
                previousState
                && (
                    resource.type == GpuGraphResourceType::Texture
                    || resource.type == GpuGraphResourceType::Buffer
                    || resource.type == GpuGraphResourceType::AccelStruct
                )
            ){
                const GpuSubmissionPacketId sourcePacket = FindCompiledPacketForTask(
                    compiledPlan,
                    previousState->task
                );
                if(!sourcePacket.valid())
                    return false;
                if(sourcePacket != compiledTask->packet){
                    const GpuPhysicalQueueInfo* const sourceQueue = FindCompiledQueueInfo(
                        compiledPlan,
                        previousState->queue
                    );
                    const GpuPhysicalQueueInfo* const destinationQueue = FindCompiledQueueInfo(
                        compiledPlan,
                        compiledTask->queue
                    );
                    if(!sourceQueue || !destinationQueue)
                        return false;
                    const bool differentQueueFamilies = sourceQueue->familyIndex != destinationQueue->familyIndex;
                    const bool resourceUsesConcurrentSharing = ResourceUsesConcurrentQueueSharing(
                        resource.queueSharing,
                        topology
                    );
                    const bool concurrentQueuePair = ResourceSharesQueuePairConcurrently(
                        resource.queueSharing,
                        topology,
                        *sourceQueue,
                        *destinationQueue
                    );
                    if(differentQueueFamilies && resourceUsesConcurrentSharing && !concurrentQueuePair){
                        // The resource was created concurrent for another family set. Vulkan cannot transfer
                        // ownership to an omitted family, so fail compilation instead of recording invalid work.
                        return false;
                    }

                    const bool requiresExclusiveOwnershipHandoff =
                        !resourceUsesConcurrentSharing
                        && differentQueueFamilies
                    ;
                    if(requiresExclusiveOwnershipHandoff){
                        const GpuCompiledBarrierType::Enum releaseType = OwnershipReleaseBarrierType(resource.type);
                        const GpuCompiledBarrierType::Enum acquireType = OwnershipAcquireBarrierType(resource.type);
                        if(releaseType >= GpuCompiledBarrierType::kCount || acquireType >= GpuCompiledBarrierType::kCount)
                            return false;
                        pendingEpilogueBarriers.push_back(PendingCompiledEpilogueBarrier{
                            .task = previousState->task,
                            .barrier = GpuCompiledBarrier{
                                .resource = use.resource,
                                .range = use.range,
                                .before = before,
                                .after = before,
                                .sourceQueue = previousState->queue,
                                .destinationQueue = compiledTask->queue,
                                .type = releaseType,
                            },
                        });
                        compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                            .resource = use.resource,
                            .range = use.range,
                            .before = before,
                            .after = before,
                            .sourceQueue = previousState->queue,
                            .destinationQueue = compiledTask->queue,
                            .type = acquireType,
                        });
                    }
                    const bool readOnlySameState =
                        previousState->access == GpuTaskResourceAccess::Read
                        && use.access == GpuTaskResourceAccess::Read
                        && before == use.requiredState
                        && !needsUavDependency
                    ;
                    const bool mayOmitInternalStateSeed =
                        use.hasIndependentStateSource
                        && readOnlySameState
                        && concurrentQueuePair
                    ;
                    if(!mayOmitInternalStateSeed){
                        compiledPlan.prologueStateSeeds.push_back(GpuPacketStateSeed{
                            .resource = use.resource,
                            .range = use.range,
                            .sourcePacket = sourcePacket,
                        });
                    }
                }
            }
            if(materializesGraphInitialState || before != use.requiredState || needsUavDependency){
                compiledPlan.prologueBarriers.push_back(GpuCompiledBarrier{
                    .resource = use.resource,
                    .range = use.range,
                    .before = before,
                    .after = use.requiredState,
                    .sourceQueue = previousState ? previousState->queue : compiledTask->queue,
                    .destinationQueue = compiledTask->queue,
                    .type = needsUavDependency
                        ? UavBarrierType(resource.type)
                        : TransitionBarrierType(resource.type),
                    .isGraphInitialState = materializesGraphInitialState || requiresExplicitInitialStateSource,
                });
            }

            trackedResourceStates.push_back(TrackedCompiledResourceState{
                .resource = use.resource,
                .range = use.range,
                .state = use.requiredState,
                .access = use.access,
                .task = taskID,
                .queue = compiledTask->queue,
            });
        }
        compiledTask->prologueStateSeedCount = static_cast<u32>(compiledPlan.prologueStateSeeds.size())
            - compiledTask->prologueStateSeedOffset
        ;
        compiledTask->prologueBarrierCount = static_cast<u32>(compiledPlan.prologueBarriers.size())
            - compiledTask->prologueBarrierOffset
        ;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

