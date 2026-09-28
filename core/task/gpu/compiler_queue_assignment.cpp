// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTaskGraphQueueAssignments::reset()noexcept{
    static_assert(noexcept(m_assignments.clear()));
    static_assert(noexcept(m_assignmentIndicesByTask.clear()));

    m_assignments.clear();
    m_assignmentIndicesByTask.clear();
    m_diagnostic = GpuTaskQueueAssignmentDiagnostic{};
    m_generation = 0u;
    m_declarationRevision = 0u;
    m_compiledPlanGeneration = 0u;
    m_taskCount = 0u;
    m_valid = false;
}

bool GpuTaskGraphQueueAssignments::validFor(const GpuTaskGraph::DeclarationReadView& graph)const noexcept{
    return m_valid
        && m_generation == graph.generation()
        && m_declarationRevision == graph.declarationRevision()
        && m_taskCount == graph.taskCount()
        && m_assignments.size() == m_taskCount
        && m_assignmentIndicesByTask.size() == m_taskCount
    ;
}

bool GpuTaskGraphQueueAssignments::validFor(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuCompiledGraph::ReadView& compiledPlan
)const noexcept{
    return validFor(graph)
        && compiledPlan.validFor(graph)
        && m_compiledPlanGeneration != 0u
        && m_compiledPlanGeneration == compiledPlan.planGeneration()
    ;
}

const GpuTaskQueueAssignment* GpuTaskGraphQueueAssignments::find(const GpuTaskId& task)const noexcept{
    if(!m_valid)
        return nullptr;
    return GpuTaskGraphCompilerDetail::FindQueueAssignment(m_assignments, m_assignmentIndicesByTask, task);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraphCompiler::assignQueues(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueTopology& topology,
    GpuTaskGraphQueueAssignments& outAssignments,
    Alloc::ScratchArena& scratchArena,
    const GpuTaskGraphQueueAssignmentOptions& options
)const{
    using namespace GpuTaskGraphCompilerDetail;

    if(!graph.valid())
        return false;
    outAssignments.reset();

    const auto fail = [&](const GpuTaskGraphQueueAssignmentStatus::Enum status, const GpuTaskId task = {}, const GpuQueueCapability::Mask requiredCapabilities = GpuQueueCapability::None){
        outAssignments.m_diagnostic.status = status;
        outAssignments.m_diagnostic.task = task;
        outAssignments.m_diagnostic.requiredCapabilities = requiredCapabilities;
        return false;
    };
    if(!analysis.validFor(graph))
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidGraphAnalysis);
    if(!IsValidQueueTopology(topology))
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology);
    if(options.queueLoadCount != 0u && !options.queueLoads)
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidQueueLoad);
    for(usize queueLoadIndex = 0u; queueLoadIndex < options.queueLoadCount; ++queueLoadIndex){
        const GpuTaskQueueLoad& queueLoad = options.queueLoads[queueLoadIndex];
        if(!FindPhysicalQueueInfo(topology, queueLoad.queue))
            return fail(GpuTaskGraphQueueAssignmentStatus::InvalidQueueLoad);
        for(usize previousIndex = 0u; previousIndex < queueLoadIndex; ++previousIndex){
            if(options.queueLoads[previousIndex].queue == queueLoad.queue)
                return fail(GpuTaskGraphQueueAssignmentStatus::InvalidQueueLoad);
        }
    }
    if(options.timingFeedbackPolicy.enabled && !options.timingFeedbackPolicy.valid())
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback);
    if(ValidateGpuTaskTimingQueueOverrides(
        options.timingQueueOverrides,
        options.timingQueueOverrideCount,
        topology.queues[0u].id.deviceGeneration
    ) != GpuTaskTimingQueueOverrideStatus::Success)
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback);

    outAssignments.m_generation = graph.generation();
    outAssignments.m_declarationRevision = graph.declarationRevision();
    outAssignments.m_taskCount = graph.taskCount();
    outAssignments.m_assignmentIndicesByTask.resize(graph.taskCount(), Limit<u32>::s_Max);
    outAssignments.m_assignments.reserve(graph.taskCount());

    GpuTaskSchedulingReachability schedulingReachability(scratchArena);
    if(!BuildGpuTaskSchedulingReachability(graph, analysis, schedulingReachability))
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidGraphAnalysis);
    const GpuTaskQueueScoringData scoringData(graph, analysis, options, scratchArena);
    Vector<GpuTaskQueuePlacementGroup, Alloc::ScratchArena> groups(scratchArena);
    if(!BuildQueuePlacementGroups(graph, analysis, topology, options, groups, outAssignments.m_diagnostic, scratchArena))
        return false;

    // Establish every legal route before scoring. Explicit compatible merge chains share one provisional queue;
    // their command union and external ownership facts constrain the whole chain, while packetization retains its frontier checks.
    for(const GpuTaskQueuePlacementGroup& group : groups){
        const GpuPhysicalQueueInfo* const selectedQueue = FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, group);
        NWB_ASSERT(selectedQueue);
        for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
            const GpuTaskId taskID = analysis.topologicalOrder()[group.assignmentOffset + taskOffset];
            if(
                !taskID.valid()
                || taskID.generation != outAssignments.m_generation
                || taskID.index >= outAssignments.m_assignmentIndicesByTask.size()
                || outAssignments.m_assignmentIndicesByTask[taskID.index] != Limit<u32>::s_Max
                || outAssignments.m_assignments.size() >= Limit<u32>::s_Max
            )
                return fail(GpuTaskGraphQueueAssignmentStatus::InvalidGraphAnalysis, taskID);
            const u32 assignmentIndex = static_cast<u32>(outAssignments.m_assignments.size());
            outAssignments.m_assignments.push_back(GpuTaskQueueAssignment{
                .task = taskID,
                .initialQueue = selectedQueue->id,
                .queue = selectedQueue->id,
                .score = {},
                .queueClass = selectedQueue->queueClass,
                .reason = RequiresGraphics(group.requiredCapabilities)
                    ? GpuTaskQueueAssignmentReason::RequiredGraphics
                    : GpuTaskQueueAssignmentReason::ScoredAny,
                .dedicated = selectedQueue->dedicated,
                .modifiers = group.overrideQueue.valid()
                    ? GpuTaskQueueAssignmentModifier::QueueOverride
                    : GpuTaskQueueAssignmentModifier::None,
            });
            outAssignments.m_assignmentIndicesByTask[taskID.index] = assignmentIndex;
        }
    }
    if(outAssignments.m_assignments.size() != outAssignments.m_assignmentIndicesByTask.size())
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidGraphAnalysis);

    // Score complete groups against the same provisional plan, omitting group-local work from crossing/overlap costs.
    // This bounds automatic placement to the available queue classes and keeps physical-topology iteration order irrelevant.
    Vector<GpuPhysicalQueueId, Alloc::ScratchArena> scoredQueues(groups.size(), scratchArena);
    Vector<GpuTaskQueueAssignmentReason::Enum, Alloc::ScratchArena> scoredReasons(groups.size(), scratchArena);
    for(usize groupIndex = 0u; groupIndex < groups.size(); ++groupIndex){
        const GpuTaskQueuePlacementGroup& group = groups[groupIndex];
        const GpuPhysicalQueueInfo* selectedQueue = nullptr;
        GpuQueueAssignmentScore selectedScore;
        bool allTasksTiny = true;
        bool conservative = false;
        for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
            const GpuTaskGraphTaskView task = graph.taskAt(analysis.topologicalOrder()[group.assignmentOffset + taskOffset].index);
            allTasksTiny = allTasksTiny && task.scheduling.cost == GpuTaskCostHint::Tiny;
            conservative = conservative || !task.scheduling.overlapPreferred || task.scheduling.avoidQueueCrossing;
        }
        conservative = conservative || allTasksTiny;
        struct Candidate{
            const GpuPhysicalQueueInfo* queue = nullptr;
            GpuQueueAssignmentScore score;
        };
        Candidate candidates[CommandQueue::kCount] = {};
        bool hasIndependentOverlap = false;
        for(u8 queueClassValue = 0u; queueClassValue < CommandQueue::kCount; ++queueClassValue){
            Candidate& candidate = candidates[queueClassValue];
            candidate.queue = FindBestLegalQueuePlacementGroupCandidate(
                graph,
                analysis,
                topology,
                group,
                static_cast<CommandQueue::Enum>(queueClassValue)
            );
            if(!candidate.queue)
                continue;
            candidate.score = BuildQueuePlacementGroupScore(
                graph,
                analysis,
                outAssignments.m_assignments,
                outAssignments.m_assignmentIndicesByTask,
                topology,
                schedulingReachability,
                scoringData,
                group,
                *candidate.queue
            );
            hasIndependentOverlap = hasIndependentOverlap || candidate.score.overlap > 0;
            if(conservative){
                candidate.score.overlap = 0;
                candidate.score.queueLoad = 0;
            }
        }
        // Finite crossing costs permit useful async overlap. Without independent work, graph-load estimates must
        // not scatter a serial chain merely because its ancestors and descendants occupy the same queue.
        const bool compareTotalScore = !conservative && hasIndependentOverlap;
        for(const Candidate& candidate : candidates){
            if(!candidate.queue)
                continue;
            bool better = IsBetterAnyQueueAssignmentCandidate(
                candidate.score,
                *candidate.queue,
                selectedScore,
                selectedQueue,
                compareTotalScore
            );
            if(conservative && selectedQueue){
                const i64 candidateCrossings = static_cast<i64>(candidate.score.incomingCrossings) + candidate.score.outgoingCrossings;
                const i64 selectedCrossings = static_cast<i64>(selectedScore.incomingCrossings) + selectedScore.outgoingCrossings;
                if(
                    candidateCrossings == selectedCrossings
                    && candidate.score.ownershipTransfers == selectedScore.ownershipTransfers
                    && candidate.queue->queueClass != selectedQueue->queueClass
                ){
                    if(candidate.queue->queueClass == CommandQueue::Graphics)
                        better = true;
                    else if(selectedQueue->queueClass == CommandQueue::Graphics)
                        better = false;
                }
            }
            if(better){
                selectedQueue = candidate.queue;
                selectedScore = candidate.score;
            }
        }
        NWB_ASSERT(selectedQueue);
        scoredQueues[groupIndex] = selectedQueue->id;
        scoredReasons[groupIndex] = RequiresGraphics(group.requiredCapabilities)
            ? GpuTaskQueueAssignmentReason::RequiredGraphics
            : conservative
                ? GpuTaskQueueAssignmentReason::ConservativeAny
                : GpuTaskQueueAssignmentReason::ScoredAny
        ;
    }
    for(usize groupIndex = 0u; groupIndex < groups.size(); ++groupIndex){
        const GpuTaskQueuePlacementGroup& group = groups[groupIndex];
        const GpuPhysicalQueueInfo* const selectedQueue = FindPhysicalQueueInfo(topology, scoredQueues[groupIndex]);
        NWB_ASSERT(selectedQueue);
        for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
            GpuTaskQueueAssignment& assignment = outAssignments.m_assignments[group.assignmentOffset + taskOffset];
            assignment.initialQueue = selectedQueue->id;
            assignment.queue = selectedQueue->id;
            assignment.queueClass = selectedQueue->queueClass;
            assignment.reason = scoredReasons[groupIndex];
            assignment.dedicated = selectedQueue->dedicated;
        }
    }

    // Optional physical balancing applies to independent singleton placements. It cannot separate an explicit merge chain.
    for(const GpuTaskQueuePlacementGroup& group : groups){
        if(group.assignmentCount != 1u || group.requiredQueue.valid() || group.overrideQueue.valid())
            continue;
        const usize assignmentIndex = group.assignmentOffset;
        GpuTaskQueueAssignment& assignment = outAssignments.m_assignments[assignmentIndex];
        const GpuTaskGraphTaskView task = graph.taskAt(assignment.task.index);
        const GpuPhysicalQueueInfo* selectedQueue = FindPhysicalQueueInfo(topology, assignment.queue);
        NWB_ASSERT(selectedQueue);
        if(task.scheduling.allowSameClassQueueRouting && task.scheduling.overlapPreferred && !task.scheduling.avoidQueueCrossing){
            const GpuPhysicalQueueInfo* const dependencyQueue = task.scheduling.preserveSameClassQueueWithDirectDependency
                ? FindDirectDependencySameClassQueue(
                    graph,
                    analysis,
                    task,
                    outAssignments.m_assignments,
                    outAssignments.m_assignmentIndicesByTask,
                    topology,
                    *selectedQueue,
                    assignmentIndex,
                    task.scheduling.allowCrossFamilySameClassQueueRouting
                )
                : nullptr
            ;
            if(dependencyQueue){
                if(dependencyQueue->id != selectedQueue->id){
                    selectedQueue = dependencyQueue;
                    assignment.modifiers |= GpuTaskQueueAssignmentModifier::DirectDependencyAffinity;
                }
            }
            else if(const GpuPhysicalQueueInfo* const leastLoadedQueue = FindLeastLoadedSameClassQueue(
                graph,
                outAssignments.m_assignments,
                scoringData,
                topology,
                task,
                *selectedQueue,
                assignmentIndex,
                task.scheduling.allowCrossFamilySameClassQueueRouting,
                task.scheduling.preferNonPrimarySameClassQueue
            )){
                if(leastLoadedQueue->id != selectedQueue->id){
                    selectedQueue = leastLoadedQueue;
                    assignment.modifiers |= GpuTaskQueueAssignmentModifier::SameClassLoadBalance;
                    if(task.scheduling.preferNonPrimarySameClassQueue)
                        assignment.modifiers |= GpuTaskQueueAssignmentModifier::NonPrimaryPreference;
                }
            }
        }
        assignment.queue = selectedQueue->id;
        assignment.queueClass = selectedQueue->queueClass;
        assignment.dedicated = selectedQueue->dedicated;
        assignment.initialQueue = selectedQueue->id;
    }

    for(const GpuTaskQueuePlacementGroup& group : groups){
        const GpuPhysicalQueueInfo* const staticQueue = FindPhysicalQueueInfo(
            topology,
            outAssignments.m_assignments[group.assignmentOffset].queue
        );
        NWB_ASSERT(staticQueue);
        GpuPhysicalQueueId timingOverrideQueue;
        GpuTaskId timingOverrideTask;
        for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
            const GpuTaskGraphTaskView task = graph.taskAt(outAssignments.m_assignments[group.assignmentOffset + taskOffset].task.index);
            const GpuTaskTimingAssignmentKey key{ .task = task.identity, .variant = task.timing.variant, .resolutionClass = task.timing.resolutionClass };
            const GpuTaskTimingQueueOverride* const override = FindGpuTaskTimingQueueOverride(
                options.timingQueueOverrides,
                options.timingQueueOverrideCount,
                TimingHistoryKeyForQueue(key, staticQueue->queueClass)
            );
            if(!override)
                continue;
            if(timingOverrideQueue.valid() && timingOverrideQueue != override->queue)
                return fail(GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback, task.id, task.commands.requiredCapabilities);
            timingOverrideQueue = override->queue;
            timingOverrideTask = task.id;
        }
        if(timingOverrideQueue.valid()){
            const GpuQueueCapability::Mask requiredCapabilities = graph.taskAt(timingOverrideTask.index).commands.requiredCapabilities;
            GpuTaskQueuePlacementGroup forcedGroup = group;
            if(forcedGroup.overrideQueue.valid() && forcedGroup.overrideQueue != timingOverrideQueue)
                return fail(GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback, timingOverrideTask, requiredCapabilities);
            forcedGroup.overrideQueue = timingOverrideQueue;
            const GpuPhysicalQueueInfo* const selectedQueue = FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, forcedGroup);
            if(!selectedQueue)
                return fail(GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback, timingOverrideTask, requiredCapabilities);
            for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
                GpuTaskQueueAssignment& assignment = outAssignments.m_assignments[group.assignmentOffset + taskOffset];
                const GpuTaskGraphTaskView task = graph.taskAt(assignment.task.index);
                if(!IsLegalTimingFeedbackRoute(graph, topology, task, *staticQueue, *selectedQueue))
                    return fail(GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback, task.id, task.commands.requiredCapabilities);
                assignment.queue = selectedQueue->id;
                assignment.queueClass = selectedQueue->queueClass;
                assignment.dedicated = selectedQueue->dedicated;
                assignment.modifiers |= GpuTaskQueueAssignmentModifier::DebugTimingOverride;
            }
            continue;
        }
        if(
            group.assignmentCount != 1u
            || group.requiredQueue.valid()
            || group.overrideQueue.valid()
            || !HasUsableTimingFeedback(options, topology.queues[0u].id.deviceGeneration)
        )
            continue;

        GpuTaskQueueAssignment& assignment = outAssignments.m_assignments[group.assignmentOffset];
        const GpuTaskGraphTaskView task = graph.taskAt(assignment.task.index);
        const GpuTaskTimingAssignmentKey key{ .task = task.identity, .variant = task.timing.variant, .resolutionClass = task.timing.resolutionClass };
        const GpuPhysicalQueueInfo* selectedQueue = staticQueue;
        const GpuPhysicalQueueInfo* const incumbent = FindTimingFeedbackIncumbent(graph, topology, task, *staticQueue, key, *options.timingHistory);
        const GpuPhysicalQueueInfo* const calibrationQueue = FindTimingFeedbackCalibrationQueue(
            graph,
            topology,
            task,
            *incumbent,
            key,
            *options.timingHistory,
            options.timingFeedbackPolicy,
            options.timingFrameIndex
        );
        if(calibrationQueue){
            selectedQueue = calibrationQueue;
            assignment.modifiers |= GpuTaskQueueAssignmentModifier::TimingCalibration;
        }
        else if(const GpuPhysicalQueueInfo* const timingQueue = FindTimingFeedbackQueue(
            graph,
            analysis,
            outAssignments.m_assignments,
            outAssignments.m_assignmentIndicesByTask,
            topology,
            schedulingReachability,
            scoringData,
            task,
            *incumbent,
            key,
            *options.timingHistory,
            options.timingFeedbackPolicy,
            options.timingFrameIndex
        )){
            selectedQueue = timingQueue;
            assignment.modifiers |= GpuTaskQueueAssignmentModifier::TimingFeedback;
        }
        else if(incumbent != staticQueue){
            selectedQueue = incumbent;
            assignment.modifiers |= GpuTaskQueueAssignmentModifier::TimingFeedback;
        }
        assignment.queue = selectedQueue->id;
        assignment.queueClass = selectedQueue->queueClass;
        assignment.dedicated = selectedQueue->dedicated;
    }

    for(GpuTaskQueueAssignment& assignment : outAssignments.m_assignments){
        const GpuTaskGraphTaskView task = graph.taskAt(assignment.task.index);
        const GpuPhysicalQueueInfo* const selectedQueue = FindPhysicalQueueInfo(topology, assignment.queue);
        NWB_ASSERT(selectedQueue);
        assignment.score = BuildQueueAssignmentScore(
            graph,
            analysis,
            outAssignments.m_assignments,
            outAssignments.m_assignmentIndicesByTask,
            topology,
            schedulingReachability,
            scoringData,
            task,
            *selectedQueue
        );
    }
    outAssignments.m_diagnostic.status = GpuTaskGraphQueueAssignmentStatus::Success;
    outAssignments.m_valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

