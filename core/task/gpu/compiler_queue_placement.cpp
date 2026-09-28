// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_compiler_queue_placement{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace GpuTaskGraphCompilerDetail;


[[nodiscard]] bool AccumulateRequiredQueue(const GpuPhysicalQueueId queue, GpuPhysicalQueueId& inOutQueue)noexcept{
    if(!queue.valid())
        return true;
    if(inOutQueue.valid() && inOutQueue != queue)
        return false;
    inOutQueue = queue;
    return true;
}


[[nodiscard]] bool FindFirstUseOwnerQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    GpuPhysicalQueueId& inOutQueue)noexcept{
    if(resource.initialOwnerHandoffSourceCount == 0u){
        const GpuPhysicalQueueId destination = resource.initialOwnerReleaseDestinationQueue.valid()
            ? resource.initialOwnerReleaseDestinationQueue
            : resource.initialOwnerQueue
        ;
        return AccumulateRequiredQueue(destination, inOutQueue);
    }

    const GpuTaskGraphInitialOwnerHandoffSourceView* selectedSource = nullptr;
    for(usize sourceIndex = 0u; sourceIndex < resource.initialOwnerHandoffSourceCount; ++sourceIndex){
        const GpuTaskGraphInitialOwnerHandoffSourceView& source = resource.initialOwnerHandoffSources[sourceIndex];
        GpuTaskResourceRange sourceRange;
        if(!ResolveResourceRangeForPlanning(graph, resource, source.range, sourceRange))
            return false;
        if(!RangeContains(resource, sourceRange, range))
            continue;
        if(selectedSource)
            return false;
        selectedSource = &source;
    }
    return selectedSource && AccumulateRequiredQueue(selectedSource->destinationQueue, inOutQueue);
}


[[nodiscard]] bool BuildInitialOwnerQueueConstraints(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    Vector<GpuPhysicalQueueId, Alloc::ScratchArena>& outQueues,
    GpuTaskId& outFailedTask,
    Alloc::ScratchArena& scratchArena){
    usize taskUseCapacity = 0u;
    usize ownedResourceUseCount = 0u;
    for(const GpuTaskId taskID : analysis.topologicalOrder()){
        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        taskUseCapacity = Max(taskUseCapacity, task.resourceUseCount);
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskGraphResourceView resource = graph.resourceAt(task.resourceUses[useIndex].resource.index);
            if(
                resource.initialOwnerQueue.valid()
                || resource.initialOwnerReleaseDestinationQueue.valid()
                || resource.initialOwnerHandoffSourceCount != 0u
            )
                ++ownedResourceUseCount;
        }
    }
    if(ownedResourceUseCount == 0u)
        return true;

    Vector<TrackedCompiledResourceState, Alloc::ScratchArena> states(scratchArena);
    states.reserve(ownedResourceUseCount);
    TrackedResourceStateHistory history(states, graph.resourceCount(), graph.generation(), scratchArena);
    TaskResourceUseIndex taskUses(graph.resourceCount(), graph.generation(), taskUseCapacity, scratchArena);
    Vector<GpuTaskResourceRange, Alloc::ScratchArena> firstUseRanges(scratchArena);
    Vector<TrackedResourceStateFragment, Alloc::ScratchArena> fragments(scratchArena);
    for(const GpuTaskId taskID : analysis.topologicalOrder()){
        outFailedTask = taskID;
        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        if(!taskUses.build(task))
            return false;
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            if(
                resource.type == GpuGraphResourceType::HazardDomain
                || use.requiredState == ResourceStates::Unknown
                || (
                    !resource.initialOwnerQueue.valid()
                    && !resource.initialOwnerReleaseDestinationQueue.valid()
                    && resource.initialOwnerHandoffSourceCount == 0u
                )
            )
                continue;

            GpuTaskResourceRange plannedRange;
            if(!ResolveResourceRangeForPlanning(graph, resource, use.range, plannedRange))
                return false;
            if(resource.type == GpuGraphResourceType::Texture || resource.type == GpuGraphResourceType::Buffer){
                if(!CollectResourceFirstUseRangesWithinTask(
                    graph,
                    task,
                    taskUses,
                    useIndex,
                    resource,
                    plannedRange,
                    scratchArena,
                    firstUseRanges
                ))
                    return false;
                if(!CollectLatestResourceStateFragments(states, history, resource, firstUseRanges, scratchArena, fragments))
                    return false;
                for(const TrackedResourceStateFragment& fragment : fragments){
                    if(!fragment.state && !FindFirstUseOwnerQueue(graph, resource, fragment.range, outQueues[taskID.index]))
                        return false;
                }
            }
            else if(history.last(use.resource) == Limit<usize>::s_Max){
                if(!FindFirstUseOwnerQueue(graph, resource, plannedRange, outQueues[taskID.index]))
                    return false;
            }
            if(!history.append(TrackedCompiledResourceState{
                .resource = use.resource,
                .range = plannedRange,
                .state = use.requiredState,
                .access = use.access,
                .task = taskID,
                .queue = {},
            }))
                return false;
        }
    }
    return true;
}


[[nodiscard]] bool TaskAllowsMerge(const GpuTaskGraphTaskView& task)noexcept{
    return task.scheduling.allowPacketMerge
        && !task.scheduling.forceSubmissionBoundary
        && !task.scheduling.joinsAcceptedQueueFrontier
    ;
}


[[nodiscard]] bool RequestsCompatiblePrecedingMerge(
    const GpuTaskGraphTaskView& preceding,
    const GpuTaskGraphTaskView& task)noexcept{
    if(!task.scheduling.mergeWithPrevious || !TaskAllowsMerge(preceding) || !TaskAllowsMerge(task))
        return false;
    if(!task.scheduling.allowMergeAcrossConsumerFrontier)
        return true;
    for(usize dependencyIndex = 0u; dependencyIndex < task.dependencyCount; ++dependencyIndex){
        if(task.dependencies[dependencyIndex] == preceding.id)
            return true;
    }
    return false;
}


static void AccumulateScore(const GpuQueueAssignmentScore& score, GpuQueueAssignmentScore& inOutScore)noexcept{
    const auto add = [](const i32 lhs, const i32 rhs){
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
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskQueuePlacementGroup& group,
    const CommandQueue::Enum requiredClass)noexcept{
    const GpuPhysicalQueueInfo* result = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            (requiredClass != CommandQueue::kCount && candidate.queueClass != requiredClass)
            || (group.requiredQueue.valid() && candidate.id != group.requiredQueue)
            || (group.overrideQueue.valid() && candidate.id != group.overrideQueue)
            || !HasCapabilities(candidate.capabilities, group.requiredCapabilities)
            || !IsBetterQueue(candidate, result)
        )
            continue;
        bool legal = true;
        for(usize taskOffset = 0u; taskOffset < group.assignmentCount && legal; ++taskOffset){
            const GpuTaskId taskID = analysis.topologicalOrder()[group.assignmentOffset + taskOffset];
            legal = IsLegalQueueAssignmentCandidate(graph, topology, graph.taskAt(taskID.index), candidate);
        }
        if(legal)
            result = &candidate;
    }
    return result;
}


[[nodiscard]] bool BuildQueuePlacementGroups(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphQueueAssignmentOptions& options,
    Vector<GpuTaskQueuePlacementGroup, Alloc::ScratchArena>& outGroups,
    GpuTaskQueueAssignmentDiagnostic& outDiagnostic,
    Alloc::ScratchArena& scratchArena){
    using namespace __hidden_gpu_task_graph_compiler_queue_placement;

    const auto fail = [&](const GpuTaskGraphQueueAssignmentStatus::Enum status, const GpuTaskId task){
        outDiagnostic.status = status;
        outDiagnostic.task = task;
        outDiagnostic.requiredCapabilities = graph.taskAt(task.index).commands.requiredCapabilities;
        return false;
    };
    Vector<GpuPhysicalQueueId, Alloc::ScratchArena> requiredQueues(graph.taskCount(), scratchArena);
    Vector<GpuPhysicalQueueId, Alloc::ScratchArena> overrideQueues(graph.taskCount(), scratchArena);
    GpuTaskId failedTask;
    if(!BuildInitialOwnerQueueConstraints(graph, analysis, requiredQueues, failedTask, scratchArena))
        return fail(GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue, failedTask);

    if(options.queueOverrideCount != 0u && !options.queueOverrides){
        outDiagnostic.status = GpuTaskGraphQueueAssignmentStatus::InvalidQueueOverride;
        return false;
    }
    for(usize overrideIndex = 0u; overrideIndex < options.queueOverrideCount; ++overrideIndex){
        const GpuTaskQueueAssignmentOverride& override = options.queueOverrides[overrideIndex];
        if(!graph.validTask(override.task)){
            outDiagnostic.status = GpuTaskGraphQueueAssignmentStatus::InvalidQueueOverride;
            outDiagnostic.task = override.task;
            return false;
        }
        const GpuPhysicalQueueInfo* const candidate = FindPhysicalQueueInfo(topology, override.queue);
        if(
            overrideQueues[override.task.index].valid()
            || !candidate
            || !IsLegalQueueAssignmentCandidate(graph, topology, graph.taskAt(override.task.index), *candidate)
            || (requiredQueues[override.task.index].valid() && requiredQueues[override.task.index] != override.queue)
        )
            return fail(GpuTaskGraphQueueAssignmentStatus::InvalidQueueOverride, override.task);
        overrideQueues[override.task.index] = override.queue;
    }

    outGroups.clear();
    outGroups.reserve(graph.taskCount());
    for(usize assignmentIndex = 0u; assignmentIndex < analysis.topologicalOrder().size(); ++assignmentIndex){
        const GpuTaskId taskID = analysis.topologicalOrder()[assignmentIndex];
        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        const GpuTaskQueuePlacementGroup singleton{
            .assignmentOffset = assignmentIndex,
            .assignmentCount = 1u,
            .requiredCapabilities = task.commands.requiredCapabilities,
            .requiredQueue = requiredQueues[taskID.index],
            .overrideQueue = overrideQueues[taskID.index],
        };
        if(!FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, singleton))
            return fail(GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue, taskID);

        if(assignmentIndex != 0u && RequestsCompatiblePrecedingMerge(
            graph.taskAt(analysis.topologicalOrder()[assignmentIndex - 1u].index),
            task
        )){
            GpuTaskQueuePlacementGroup combined = outGroups.back();
            ++combined.assignmentCount;
            combined.requiredCapabilities |= singleton.requiredCapabilities;
            combined.overrideQueue = {};
            if(
                AccumulateRequiredQueue(singleton.requiredQueue, combined.requiredQueue)
                && FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, combined)
            ){
                combined.overrideQueue = outGroups.back().overrideQueue;
                if(
                    !AccumulateRequiredQueue(singleton.overrideQueue, combined.overrideQueue)
                    || !FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, combined)
                )
                    return fail(GpuTaskGraphQueueAssignmentStatus::InvalidQueueOverride, taskID);
                outGroups.back() = combined;
                continue;
            }
        }
        outGroups.push_back(singleton);
    }
    return true;
}


[[nodiscard]] GpuQueueAssignmentScore BuildQueuePlacementGroupScore(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskQueuePlacementGroup& group,
    const GpuPhysicalQueueInfo& candidate)noexcept{
    GpuQueueAssignmentScore score;
    for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
        const GpuTaskId taskID = analysis.topologicalOrder()[group.assignmentOffset + taskOffset];
        __hidden_gpu_task_graph_compiler_queue_placement::AccumulateScore(BuildQueueAssignmentScore(
            graph,
            analysis,
            assignments,
            assignmentIndicesByTask,
            topology,
            schedulingReachability,
            scoringData,
            graph.taskAt(taskID.index),
            candidate,
            group.assignmentOffset,
            group.assignmentCount
        ), score);
    }
    if(group.assignmentCount == 1u)
        return score;

    // A shared packet can overlap another task only when every member is independent of that task.
    // Per-member sums would reward work that waits at the same packet's entrance for a different member.
    u64 overlap = 0u;
    for(usize assignmentIndex = 0u; assignmentIndex < assignments.size(); ++assignmentIndex){
        if(
            assignmentIndex >= group.assignmentOffset
            && assignmentIndex - group.assignmentOffset < group.assignmentCount
        )
            continue;
        const GpuTaskQueueAssignment& other = assignments[assignmentIndex];
        if(other.queue == candidate.id)
            continue;
        bool independent = true;
        for(usize taskOffset = 0u; taskOffset < group.assignmentCount && independent; ++taskOffset){
            const GpuTaskId taskID = analysis.topologicalOrder()[group.assignmentOffset + taskOffset];
            const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
            independent = task.scheduling.overlapPreferred
                && !task.scheduling.avoidQueueCrossing
                && schedulingReachability.transitivelyIndependent(taskID, other.task)
            ;
        }
        if(independent){
            const u64 cost = scoringData.taskCosts[other.task.index];
            overlap = overlap > Limit<u64>::s_Max - cost ? Limit<u64>::s_Max : overlap + cost;
        }
    }
    score.overlap = overlap > static_cast<u64>(Limit<i32>::s_Max)
        ? Limit<i32>::s_Max
        : static_cast<i32>(overlap)
    ;
    return score;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

