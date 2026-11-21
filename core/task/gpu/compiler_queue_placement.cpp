// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <global/bit.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_compiler_queue_placement{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace GpuTaskGraphCompilerDetail;


[[nodiscard]] b8 AccumulateExactQueueConstraint(const GpuPhysicalQueueId queue, GpuPhysicalQueueId& inOutQueue)noexcept{
    if(!queue.valid())
        return true;
    if(inOutQueue.valid() && inOutQueue != queue)
        return false;
    inOutQueue = queue;
    return true;
}


[[nodiscard]] b8 AccumulateInitialOwnershipQueue(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphResourceView& resource, const GpuTaskResourceRange& range, GpuPhysicalQueueId& inOutQueue)noexcept{
    if(resource.initialOwnerHandoffSourceCount == 0u)
        return AccumulateExactQueueConstraint(resource.initialOwnerQueue, inOutQueue);

    const GpuTaskGraphInitialOwnerHandoffSourceView* selectedSource = nullptr;
    for(usize sourceIndex = 0u; sourceIndex < resource.initialOwnerHandoffSourceCount; ++sourceIndex){
        const GpuTaskGraphInitialOwnerHandoffSourceView& source = resource.initialOwnerHandoffSources[sourceIndex];
        const auto sourceRange = ResolveResourceRangeForPlanning(graph, resource, source.range);
        if(!sourceRange)
            return false;
        if(!RangeContains(resource, (*sourceRange), range))
            continue;
        if(selectedSource)
            return false;
        selectedSource = &source;
    }
    return selectedSource && AccumulateExactQueueConstraint(selectedSource->destinationQueue, inOutQueue);
}


[[nodiscard]] Expected<void, GpuTaskId> BuildInitialOwnershipQueueConstraints(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphAnalysis& analysis, Alloc::ScratchArena& scratchArena, Vector<GpuPhysicalQueueId, Alloc::ScratchArena>& outQueues){
    const usize resourceCount = CompilerGraphAccess::ResourceCount(graph);
    b8 anyOwnershipConstraint = false;
    for(usize resourceIndex = 0u; resourceIndex < resourceCount; ++resourceIndex){
        const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, resourceIndex);
        if(resource.initialOwnerQueue.valid() || resource.initialOwnerHandoffSourceCount != 0u){
            anyOwnershipConstraint = true;
            break;
        }
    }
    if(!anyOwnershipConstraint)
        return {};

    usize taskUseCapacity = 0u;
    usize ownedResourceUseCount = 0u;
    for(const GpuTaskId taskID : analysis.topologicalOrder()){
        const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, taskID.index);
        taskUseCapacity = Max(taskUseCapacity, static_cast<usize>(task.resourceUseCount));
        for(const GpuTaskResourceUse& use : CompilerGraphAccess::TaskResourceUses(graph, task)){
            const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, use.resource.index);
            if(resource.initialOwnerQueue.valid() || resource.initialOwnerHandoffSourceCount != 0u)
                ++ownedResourceUseCount;
        }
    }
    if(ownedResourceUseCount == 0u)
        return {};

    Vector<TrackedCompiledResourceState, Alloc::ScratchArena> states(scratchArena);
    states.reserve(ownedResourceUseCount);
    TrackedResourceStateHistory history(graph.resourceCount(), graph.generation(), states, scratchArena);
    TaskResourceUseIndex taskUses(graph.resourceCount(), graph.generation(), taskUseCapacity, scratchArena);
    Vector<GpuTaskResourceRange, Alloc::ScratchArena> firstUseRanges(scratchArena);
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena> fragments(scratchArena);
    ResourceRangeScratch rangeScratch(scratchArena);
    for(const GpuTaskId taskID : analysis.topologicalOrder()){
        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        if(!taskUses.build(task))
            return MakeUnexpected(taskID);
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            if(resource.type == GpuGraphResourceType::HazardDomain || (!resource.initialOwnerQueue.valid() && resource.initialOwnerHandoffSourceCount == 0u))
                continue;
            FOUNDATION_ASSERT(use.requiredState != ResourceStates::Unknown);

            const auto plannedRange = ResolveResourceRangeForPlanning(graph, resource, use.range);
            if(!plannedRange)
                return MakeUnexpected(taskID);
            if(resource.type == GpuGraphResourceType::Texture || resource.type == GpuGraphResourceType::Buffer){
                if(!CollectResourceFirstUseRangesWithinTaskWithScratch(graph, task, taskUses, useIndex, resource, (*plannedRange), rangeScratch, firstUseRanges))
                    return MakeUnexpected(taskID);
                if(!CollectLatestResourceStateFragmentsWithScratch(states, history, resource, firstUseRanges, rangeScratch, fragments))
                    return MakeUnexpected(taskID);
                for(const TrackedResourceStateFragment& fragment : fragments){
                    if(!fragment.state && !AccumulateInitialOwnershipQueue(graph, resource, fragment.range, outQueues[taskID.index]))
                        return MakeUnexpected(taskID);
                }
            }
            else if(history.last(use.resource) == Limit<usize>::s_Max){
                if(!AccumulateInitialOwnershipQueue(graph, resource, (*plannedRange), outQueues[taskID.index]))
                    return MakeUnexpected(taskID);
            }
            if(!history.append(TrackedCompiledResourceState{ .resource = use.resource, .range = (*plannedRange), .task = taskID, .state = use.requiredState, .access = use.access, .queue = {}, }))
                return MakeUnexpected(taskID);
        }
    }
    return {};
}


[[nodiscard]] b8 RequestsCompatiblePrecedingMerge(const GpuTaskGraph::DeclarationReadView& graph, const GpuTaskGraphDetail::GpuTaskNode& preceding, const GpuTaskId precedingId, const GpuTaskGraphDetail::GpuTaskNode& task)noexcept{
    if(!task.scheduling.mergeWithPrevious || !GpuTaskGraphCompilerDetail::TaskNodeAllowsPacketMerge(preceding) || !GpuTaskGraphCompilerDetail::TaskNodeAllowsPacketMerge(task))
        return false;
    if(!task.scheduling.allowMergeAcrossConsumerFrontier)
        return true;
    for(const GpuTaskId dependency : CompilerGraphAccess::TaskDependencies(graph, task)){
        if(dependency == precedingId)
            return true;
    }
    return false;
}


static void AccumulateScore(const GpuQueueAssignmentScore& score, GpuQueueAssignmentScore& inOutScore)noexcept{
    const auto add = [](const i32 lhs, const i32 rhs)noexcept{
        const i64 sum = static_cast<i64>(lhs) + static_cast<i64>(rhs);
        return sum > Limit<i32>::s_Max ? Limit<i32>::s_Max : static_cast<i32>(sum);
    };
    inOutScore.overlap = add(inOutScore.overlap, score.overlap);
    inOutScore.queueLoad = add(inOutScore.queueLoad, score.queueLoad);
    inOutScore.incomingCrossings = add(inOutScore.incomingCrossings, score.incomingCrossings);
    inOutScore.outgoingCrossings = add(inOutScore.outgoingCrossings, score.outgoingCrossings);
    inOutScore.ownershipTransfers = add(inOutScore.ownershipTransfers, score.ownershipTransfers);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] const GpuPhysicalQueueInfo* FindBestLegalQueuePlacementGroupCandidate(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskQueuePlacementGroup& group,
    const CommandQueue::Enum requiredClass,
    const GpuTaskQueueLegalityCache* const legalityCache
)noexcept{
    const GpuPhysicalQueueInfo* result = nullptr;
    if(legalityCache && legalityCache->active){
        u64 groupMask = group.legalQueueMask;
        if(groupMask == 0u){
            groupMask = topology.queueCount == s_BitsPerWord ? ~static_cast<u64>(0u) : ((static_cast<u64>(1u) << topology.queueCount) - 1u);
            for(usize taskOffset = 0u; taskOffset < group.assignmentCount && groupMask != 0u; ++taskOffset){
                const GpuTaskId taskID = analysis.topologicalOrder()[group.assignmentOffset + taskOffset];
                groupMask &= legalityCache->taskMask(taskID.index);
            }
        }
        if(requiredClass != CommandQueue::kCount)
            groupMask &= legalityCache->classQueueMasks[requiredClass];
        if(groupMask == 0u)
            return nullptr;
        if(group.initialOwnershipQueue.valid()){
            const GpuPhysicalQueueInfo* const owner = FindPhysicalQueueInfo(topology, group.initialOwnershipQueue);
            if(!owner)
                return nullptr;
            const usize ownerIndex = static_cast<usize>(owner - topology.queues);
            if((groupMask & BitMask<u64>(static_cast<u32>(ownerIndex))) == 0u || !HasCapabilities(owner->capabilities, group.requiredCapabilities))
                return nullptr;
            return owner;
        }
        if(requiredClass != CommandQueue::kCount){
            const u8 count = legalityCache->orderedQueueIndicesByClassCount[requiredClass];
            const auto& ordered = legalityCache->orderedQueueIndicesByClass[requiredClass];
            for(u8 i = 0u; i < count; ++i){
                const u8 queueIndex = ordered[i];
                if((groupMask & BitMask<u64>(queueIndex)) != 0u){
                    const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
                    if(HasCapabilities(candidate.capabilities, group.requiredCapabilities))
                        return &candidate;
                }
            }
            return nullptr;
        }
        for(u8 i = 0u; i < legalityCache->orderedAllQueueCount; ++i){
            const u8 queueIndex = legalityCache->orderedAllQueueIndices[i];
            if((groupMask & BitMask<u64>(queueIndex)) != 0u){
                const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
                if(HasCapabilities(candidate.capabilities, group.requiredCapabilities))
                    return &candidate;
            }
        }
        return nullptr;
    }
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            (requiredClass != CommandQueue::kCount && candidate.queueClass != requiredClass)
            || (group.initialOwnershipQueue.valid() && candidate.id != group.initialOwnershipQueue)
            || !HasCapabilities(candidate.capabilities, group.requiredCapabilities)
            || !IsBetterQueue(candidate, result)
        )
            continue;
        b8 legal = true;
        for(usize taskOffset = 0u; taskOffset < group.assignmentCount && legal; ++taskOffset){
            const GpuTaskId taskID = analysis.topologicalOrder()[group.assignmentOffset + taskOffset];
            legal = IsLegalQueueAssignmentCandidate(graph, topology, graph.taskAt(taskID.index), candidate);
        }
        if(legal)
            result = &candidate;
    }
    return result;
}


[[nodiscard]] Expected<void, GpuTaskQueueAssignmentDiagnostic> BuildQueuePlacementGroups(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuPhysicalQueueTopology& topology,
    Alloc::ScratchArena& scratchArena,
    Vector<GpuTaskQueuePlacementGroup, Alloc::ScratchArena>& outGroups,
    const GpuTaskQueueLegalityCache* const legalityCache
){
    using namespace __hidden_gpu_task_graph_compiler_queue_placement;

    const usize taskCount = CompilerGraphAccess::TaskCount(graph);
    GpuTaskQueueAssignmentDiagnostic diagnostic;
    const auto fail = [&](const GpuTaskGraphQueueAssignmentStatus::Enum status, const GpuTaskId task)noexcept{
        diagnostic.status = status;
        diagnostic.task = task;
        diagnostic.requiredCapabilities = CompilerGraphAccess::TaskNode(graph, task.index).commands.requiredCapabilities;
        return MakeUnexpected(diagnostic);
    };
    Vector<GpuPhysicalQueueId, Alloc::ScratchArena> initialOwnershipQueues(taskCount, scratchArena);
    const auto ownershipConstraints = BuildInitialOwnershipQueueConstraints(graph, analysis, scratchArena, initialOwnershipQueues);
    if(!ownershipConstraints)
        return fail(GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue, ownershipConstraints.error());

    outGroups.clear();
    outGroups.reserve(taskCount);
    const GpuPhysicalQueueInfo* legalityWitness = nullptr;
    usize legalityWitnessIndex = Limit<usize>::s_Max;
    const b8 cacheActive = legalityCache && legalityCache->active;
    for(usize assignmentIndex = 0u; assignmentIndex < analysis.topologicalOrder().size(); ++assignmentIndex){
        const GpuTaskId taskID = analysis.topologicalOrder()[assignmentIndex];
        const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, taskID.index);
        const u64 taskLegalMask = cacheActive ? legalityCache->taskMask(taskID.index) : 0u;
        const GpuTaskQueuePlacementGroup singleton{
            .assignmentOffset = assignmentIndex,
            .assignmentCount = 1u,
            .requiredCapabilities = task.commands.requiredCapabilities,
            .initialOwnershipQueue = initialOwnershipQueues[taskID.index],
            .legalQueueMask = taskLegalMask,
        };
        if(cacheActive && taskLegalMask == 0u)
            return fail(GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue, taskID);
        const GpuPhysicalQueueInfo* const singletonQueue = FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, singleton, CommandQueue::kCount, legalityCache);
        if(!singletonQueue)
            return fail(GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue, taskID);

        if(assignmentIndex != 0u){
            const GpuTaskId precedingID = analysis.topologicalOrder()[assignmentIndex - 1u];
            if(RequestsCompatiblePrecedingMerge(graph, CompilerGraphAccess::TaskNode(graph, precedingID.index), precedingID, task)){
                GpuTaskQueuePlacementGroup combined = outGroups.back();
                ++combined.assignmentCount;
                combined.requiredCapabilities |= singleton.requiredCapabilities;
                if(cacheActive)
                    combined.legalQueueMask &= taskLegalMask;
                const GpuPhysicalQueueInfo* combinedWitness = nullptr;
                if((!cacheActive || combined.legalQueueMask != 0u) && AccumulateExactQueueConstraint(singleton.initialOwnershipQueue, combined.initialOwnershipQueue)){
                    FOUNDATION_ASSERT(legalityWitness);
                    const b8 witnessLegal = (!combined.initialOwnershipQueue.valid() || combined.initialOwnershipQueue == legalityWitness->id)
                        && (
                            cacheActive
                                ? (taskLegalMask & BitMask<u64>(static_cast<u32>(legalityWitnessIndex))) != 0u
                                : IsLegalQueueAssignmentCandidate(graph, topology, graph.taskAt(taskID.index), *legalityWitness)
                        );
                    if(witnessLegal)
                        combinedWitness = legalityWitness;
                    else
                        combinedWitness = FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, combined, CommandQueue::kCount, legalityCache);
                }
                if(combinedWitness){
                    outGroups.back() = combined;
                    legalityWitness = combinedWitness;
                    legalityWitnessIndex = static_cast<usize>(combinedWitness - topology.queues);
                    continue;
                }
            }
        }
        outGroups.push_back(singleton);
        legalityWitness = singletonQueue;
        legalityWitnessIndex = static_cast<usize>(singletonQueue - topology.queues);
    }
    return {};
}


[[nodiscard]] GpuQueueAssignmentScore BuildQueuePlacementGroupScore(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const Span<const GpuTaskQueueAssignment> assignments,
    const Span<const u32> assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskQueuePlacementGroup& group,
    const GpuPhysicalQueueInfo& candidate,
    const b8 computeOverlap
)noexcept{
    GpuTaskQueueScoreExclusions exclusions{ .assignmentOffset = group.assignmentOffset, .assignmentCount = group.assignmentCount, .totalCost = 0u, .candidateQueueCost = 0u, };
    b8 allTasksAllowOverlap = computeOverlap;
    for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
        const GpuTaskQueueAssignment& assignment = assignments[group.assignmentOffset + taskOffset];
        const u64 cost = scoringData.taskCosts[assignment.task.index];
        exclusions.totalCost += cost;
        if(assignment.queue == candidate.id)
            exclusions.candidateQueueCost += cost;
        if(allTasksAllowOverlap){
            const GpuTaskSchedulingHint& scheduling = CompilerGraphAccess::TaskNode(graph, assignment.task.index).scheduling;
            allTasksAllowOverlap = scheduling.overlapPreferred && !scheduling.avoidQueueCrossing;
        }
    }

    GpuQueueAssignmentScore score;
    for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
        const GpuTaskId taskID = analysis.topologicalOrder()[group.assignmentOffset + taskOffset];
        __hidden_gpu_task_graph_compiler_queue_placement::AccumulateScore(BuildTaskQueueAssignmentScore(
            graph,
            analysis,
            assignments,
            assignmentIndicesByTask,
            topology,
            schedulingReachability,
            scoringData,
            taskID,
            CompilerGraphAccess::TaskNode(graph, taskID.index).scheduling,
            candidate,
            exclusions,
            computeOverlap
        ), score
        );
    }
    if(group.assignmentCount == 1u)
        return score;

    u64 overlap = 0u;
    if(allTasksAllowOverlap && analysis.schedulingEdges().empty()){
        const u64 assignedQueueCost = scoringData.assignedQueueLoad(candidate.id);
        FOUNDATION_ASSERT(assignedQueueCost >= exclusions.candidateQueueCost);
        overlap = scoringData.totalAssignedCost - assignedQueueCost - (exclusions.totalCost - exclusions.candidateQueueCost);
    }
    else if(allTasksAllowOverlap && schedulingReachability.mayContainIndependentTasks()){
        overlap = scoringData.independentQueueCost(schedulingReachability, candidate.id, assignments.subspan(group.assignmentOffset, group.assignmentCount));
    }
    score.overlap = SaturateU64ToI32(overlap);
    return score;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

