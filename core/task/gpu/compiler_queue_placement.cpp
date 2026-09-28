// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_compiler_queue_placement{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace GpuTaskGraphCompilerDetail;


[[nodiscard]] bool AccumulateExactQueueConstraint(const GpuPhysicalQueueId queue, GpuPhysicalQueueId& inOutQueue)noexcept{
    if(!queue.valid())
        return true;
    if(inOutQueue.valid() && inOutQueue != queue)
        return false;
    inOutQueue = queue;
    return true;
}


[[nodiscard]] bool AccumulateInitialOwnershipQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphResourceView& resource,
    const GpuTaskResourceRange& range,
    GpuPhysicalQueueId& inOutQueue)noexcept{
    if(resource.initialOwnerHandoffSourceCount == 0u)
        return AccumulateExactQueueConstraint(resource.initialOwnerQueue, inOutQueue);

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
    return selectedSource && AccumulateExactQueueConstraint(selectedSource->destinationQueue, inOutQueue);
}


[[nodiscard]] bool BuildInitialOwnershipQueueConstraints(
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
                    if(!fragment.state && !AccumulateInitialOwnershipQueue(graph, resource, fragment.range, outQueues[taskID.index]))
                        return false;
                }
            }
            else if(history.last(use.resource) == Limit<usize>::s_Max){
                if(!AccumulateInitialOwnershipQueue(graph, resource, plannedRange, outQueues[taskID.index]))
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
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskQueuePlacementGroup& group,
    const CommandQueue::Enum requiredClass)noexcept{
    const GpuPhysicalQueueInfo* result = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            (requiredClass != CommandQueue::kCount && candidate.queueClass != requiredClass)
            || (group.initialOwnershipQueue.valid() && candidate.id != group.initialOwnershipQueue)
            || (group.diagnosticOverrideQueue.valid() && candidate.id != group.diagnosticOverrideQueue)
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
    const GpuPhysicalQueueTopology& topology,
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
    Vector<GpuPhysicalQueueId, Alloc::ScratchArena> initialOwnershipQueues(graph.taskCount(), scratchArena);
    Vector<GpuPhysicalQueueId, Alloc::ScratchArena> diagnosticOverrideQueues(graph.taskCount(), scratchArena);
    GpuTaskId failedTask;
    if(!BuildInitialOwnershipQueueConstraints(graph, analysis, initialOwnershipQueues, failedTask, scratchArena))
        return fail(GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue, failedTask);

    if(options.diagnosticQueueOverrideCount != 0u && !options.diagnosticQueueOverrides){
        outDiagnostic.status = GpuTaskGraphQueueAssignmentStatus::InvalidDiagnosticQueueOverride;
        return false;
    }
    for(usize overrideIndex = 0u; overrideIndex < options.diagnosticQueueOverrideCount; ++overrideIndex){
        const GpuTaskDiagnosticQueueOverride& override = options.diagnosticQueueOverrides[overrideIndex];
        if(!graph.validTask(override.task)){
            outDiagnostic.status = GpuTaskGraphQueueAssignmentStatus::InvalidDiagnosticQueueOverride;
            outDiagnostic.task = override.task;
            return false;
        }
        const GpuPhysicalQueueInfo* const candidate = FindPhysicalQueueInfo(topology, override.queue);
        if(
            diagnosticOverrideQueues[override.task.index].valid()
            || !candidate
            || !IsLegalQueueAssignmentCandidate(graph, topology, graph.taskAt(override.task.index), *candidate)
            || (initialOwnershipQueues[override.task.index].valid() && initialOwnershipQueues[override.task.index] != override.queue)
        )
            return fail(GpuTaskGraphQueueAssignmentStatus::InvalidDiagnosticQueueOverride, override.task);
        diagnosticOverrideQueues[override.task.index] = override.queue;
    }

    outGroups.clear();
    outGroups.reserve(graph.taskCount());
    const GpuPhysicalQueueInfo* legalityWitness = nullptr;
    for(usize assignmentIndex = 0u; assignmentIndex < analysis.topologicalOrder().size(); ++assignmentIndex){
        const GpuTaskId taskID = analysis.topologicalOrder()[assignmentIndex];
        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        const GpuTaskQueuePlacementGroup singleton{
            .assignmentOffset = assignmentIndex,
            .assignmentCount = 1u,
            .requiredCapabilities = task.commands.requiredCapabilities,
            .initialOwnershipQueue = initialOwnershipQueues[taskID.index],
            .diagnosticOverrideQueue = diagnosticOverrideQueues[taskID.index],
        };
        const GpuPhysicalQueueInfo* const singletonQueue = FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, singleton);
        if(!singletonQueue)
            return fail(GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue, taskID);

        if(assignmentIndex != 0u && RequestsCompatiblePrecedingMerge(
            graph.taskAt(analysis.topologicalOrder()[assignmentIndex - 1u].index),
            task
        )){
            GpuTaskQueuePlacementGroup combined = outGroups.back();
            ++combined.assignmentCount;
            combined.requiredCapabilities |= singleton.requiredCapabilities;
            combined.diagnosticOverrideQueue = {};
            const GpuPhysicalQueueInfo* combinedWitness = nullptr;
            if(AccumulateExactQueueConstraint(singleton.initialOwnershipQueue, combined.initialOwnershipQueue)){
                NWB_ASSERT(legalityWitness);
                // The witness already admits preceding members; only the appended task can invalidate it.
                if(
                    (!combined.initialOwnershipQueue.valid() || combined.initialOwnershipQueue == legalityWitness->id)
                    && IsLegalQueueAssignmentCandidate(graph, topology, task, *legalityWitness)
                )
                    combinedWitness = legalityWitness;
                else
                    combinedWitness = FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, combined);
            }
            if(combinedWitness){
                combined.diagnosticOverrideQueue = outGroups.back().diagnosticOverrideQueue;
                if(
                    !AccumulateExactQueueConstraint(singleton.diagnosticOverrideQueue, combined.diagnosticOverrideQueue)
                    || (
                        combined.diagnosticOverrideQueue.valid()
                        && !FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, combined)
                    )
                )
                    return fail(GpuTaskGraphQueueAssignmentStatus::InvalidDiagnosticQueueOverride, taskID);
                outGroups.back() = combined;
                legalityWitness = combinedWitness;
                continue;
            }
        }
        outGroups.push_back(singleton);
        legalityWitness = singletonQueue;
    }
    return true;
}


[[nodiscard]] GpuQueueAssignmentScore BuildQueuePlacementGroupScore(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskQueuePlacementGroup& group,
    const GpuPhysicalQueueInfo& candidate)noexcept{
    GpuTaskQueueScoreExclusions exclusions{
        .assignmentOffset = group.assignmentOffset,
        .assignmentCount = group.assignmentCount,
        .totalCost = 0u,
        .candidateQueueCost = 0u,
    };
    bool allTasksAllowOverlap = true;
    for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
        const GpuTaskQueueAssignment& assignment = assignments[group.assignmentOffset + taskOffset];
        const u64 cost = scoringData.taskCosts[assignment.task.index];
        exclusions.totalCost += cost;
        if(assignment.queue == candidate.id)
            exclusions.candidateQueueCost += cost;
        const GpuTaskSchedulingHint& scheduling = graph.taskAt(assignment.task.index).scheduling;
        allTasksAllowOverlap = allTasksAllowOverlap && scheduling.overlapPreferred && !scheduling.avoidQueueCrossing;
    }

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
            exclusions
        ), score);
    }
    if(group.assignmentCount == 1u)
        return score;

    // A shared packet can overlap another task only when every member is independent of that task.
    // Per-member sums would reward work that waits at the same packet's entrance for a different member.
    u64 overlap = 0u;
    if(allTasksAllowOverlap && analysis.schedulingEdges().empty()){
        const u64 assignedQueueCost = scoringData.assignedQueueLoad(candidate.id);
        NWB_ASSERT(assignedQueueCost >= exclusions.candidateQueueCost);
        overlap = scoringData.totalAssignedCost - assignedQueueCost - (exclusions.totalCost - exclusions.candidateQueueCost);
    }
    else if(allTasksAllowOverlap && schedulingReachability.mayContainIndependentTasks()){
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
                independent = schedulingReachability.transitivelyIndependent(taskID, other.task);
            }
            if(independent){
                const u64 cost = scoringData.taskCosts[other.task.index];
                overlap = overlap > Limit<u64>::s_Max - cost ? Limit<u64>::s_Max : overlap + cost;
            }
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

