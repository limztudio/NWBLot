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
    NWB_ASSERT(!m_valid || (m_assignments.size() == m_taskCount && m_assignmentIndicesByTask.size() == m_taskCount));
    return m_valid && m_generation == graph.generation() && m_declarationRevision == graph.declarationRevision() && m_taskCount == graph.taskCount();
}

bool GpuTaskGraphQueueAssignments::validFor(const GpuTaskGraph::DeclarationReadView& graph, const GpuCompiledGraph::ReadView& compiledPlan)const noexcept{
    return validFor(graph) && compiledPlan.validFor(graph) && m_compiledPlanGeneration != 0u && m_compiledPlanGeneration == compiledPlan.planGeneration();
}

const GpuTaskQueueAssignment* GpuTaskGraphQueueAssignments::find(const GpuTaskId& task)const noexcept{
    if(!m_valid)
        return nullptr;
    return GpuTaskGraphCompilerDetail::FindQueueAssignment(m_assignments, m_assignmentIndicesByTask, task);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraphCompiler::assignQueues(
    GpuTaskGraphQueueAssignments& outAssignments,
    Alloc::ScratchArena& scratchArena,
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphQueueAssignmentOptions& options
)const{
    using namespace GpuTaskGraphCompilerDetail;

    if(!graph.valid())
        return false;
    outAssignments.reset();

    const auto fail = [&](const GpuTaskGraphQueueAssignmentStatus::Enum status)noexcept{
        outAssignments.m_diagnostic.status = status;
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

    outAssignments.m_generation = graph.generation();
    outAssignments.m_declarationRevision = graph.declarationRevision();
    outAssignments.m_taskCount = graph.taskCount();
    outAssignments.m_assignmentIndicesByTask.resize(graph.taskCount(), Limit<u32>::s_Max);
    outAssignments.m_assignments.reserve(graph.taskCount());

    Vector<GpuTaskQueuePlacementGroup, Alloc::ScratchArena> groups(scratchArena);
    const auto placementGroups = BuildQueuePlacementGroups(graph, analysis, topology, scratchArena, groups);
    if(!placementGroups){
        outAssignments.m_diagnostic = placementGroups.error();
        return false;
    }
    bool needsSchedulingReachability = false;
    if(topology.queueCount > 1u && graph.taskCount() > 1u && groups.size() > 1u){
        for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
            const GpuTaskSchedulingHint& scheduling = graph.taskAt(taskIndex).scheduling;
            if(scheduling.overlapPreferred && !scheduling.avoidQueueCrossing){
                needsSchedulingReachability = true;
                break;
            }
        }
    }
    if(needsSchedulingReachability && !analysis.schedulingEdges().empty()){
        constexpr usize s_OverlapQueueMinimum = 2u;
        usize capableQueueCount = 0u;
        for(usize queueIndex = 0u; queueIndex < topology.queueCount && capableQueueCount < s_OverlapQueueMinimum; ++queueIndex){
            for(const GpuTaskQueuePlacementGroup& group : groups){
                if(HasCapabilities(topology.queues[queueIndex].capabilities, group.requiredCapabilities)){
                    ++capableQueueCount;
                    break;
                }
            }
        }
        // Capability matches overestimate every legal static, balancing, and timing route across the complete graph.
        needsSchedulingReachability = capableQueueCount >= s_OverlapQueueMinimum;
    }
    GpuTaskSchedulingReachability schedulingReachability(scratchArena);
    if(needsSchedulingReachability && !BuildGpuTaskSchedulingReachability(graph, analysis, schedulingReachability))
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidGraphAnalysis);
    GpuTaskQueueScoringData scoringData(graph, analysis, schedulingReachability, options, scratchArena);

    // Establish every legal route before scoring.
    // Explicit compatible merge chains share one provisional queue; their command union and external facts constrain the whole chain, while packetization retains its frontier checks.
    for(const GpuTaskQueuePlacementGroup& group : groups){
        auto& selectedQueue = *MakeNotNull(FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, group));
        for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
            const GpuTaskId taskID = analysis.topologicalOrder()[group.assignmentOffset + taskOffset];
            // Groups partition the unique topological order of current graph tasks.
            NWB_ASSERT(
                taskID.valid()
                && taskID.generation == outAssignments.m_generation
                && taskID.index < outAssignments.m_assignmentIndicesByTask.size()
                && outAssignments.m_assignmentIndicesByTask[taskID.index] == Limit<u32>::s_Max
                && outAssignments.m_assignments.size() < Limit<u32>::s_Max
            );
            const u32 assignmentIndex = static_cast<u32>(outAssignments.m_assignments.size());
            outAssignments.m_assignments.push_back(GpuTaskQueueAssignment{
                .task = taskID,
                .initialQueue = selectedQueue.id,
                .queue = selectedQueue.id,
                .score = {},
                .queueClass = selectedQueue.queueClass,
                .reason = HasCapabilities(group.requiredCapabilities, GpuQueueCapability::Graphics) ? GpuTaskQueueAssignmentReason::RequiredGraphics : GpuTaskQueueAssignmentReason::Scored,
                .dedicated = selectedQueue.dedicated,
            }
            );
            outAssignments.m_assignmentIndicesByTask[taskID.index] = assignmentIndex;
        }
    }
    NWB_ASSERT(outAssignments.m_assignments.size() == outAssignments.m_assignmentIndicesByTask.size());
    scoringData.rebuildAssignmentLoads(outAssignments.m_assignments, topology);

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
        usize candidateCount = 0u;
        for(u8 queueClassValue = 0u; queueClassValue < CommandQueue::kCount; ++queueClassValue){
            Candidate& candidate = candidates[queueClassValue];
            candidate.queue = FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, group, static_cast<CommandQueue::Enum>(queueClassValue));
            if(candidate.queue){
                selectedQueue = candidate.queue;
                ++candidateCount;
            }
        }
        // A single legal class needs no choice score; final per-task diagnostics are still computed below.
        if(candidateCount > 1u){
            selectedQueue = nullptr;
            bool hasIndependentOverlap = false;
            for(Candidate& candidate : candidates){
                if(!candidate.queue)
                    continue;
                candidate.score = BuildQueuePlacementGroupScore(graph, analysis, outAssignments.m_assignments, outAssignments.m_assignmentIndicesByTask, topology, schedulingReachability, scoringData, group, *candidate.queue);
                hasIndependentOverlap = hasIndependentOverlap || candidate.score.overlap > 0;
                if(conservative){
                    candidate.score.overlap = 0;
                    candidate.score.queueLoad = 0;
                }
            }
            // Finite crossing costs permit useful async overlap.
            // Without independent work, keep serial chains on one queue instead of scattering them by graph load.
            const bool compareTotalScore = !conservative && hasIndependentOverlap;
            for(const Candidate& candidate : candidates){
                if(!candidate.queue)
                    continue;
                bool better = IsBetterAutomaticQueueAssignmentCandidate(candidate.score, *candidate.queue, selectedScore, selectedQueue, compareTotalScore);
                if(conservative && selectedQueue){
                    const i64 candidateCrossings = static_cast<i64>(candidate.score.incomingCrossings) + candidate.score.outgoingCrossings;
                    const i64 selectedCrossings = static_cast<i64>(selectedScore.incomingCrossings) + selectedScore.outgoingCrossings;
                    if(candidateCrossings == selectedCrossings && candidate.score.ownershipTransfers == selectedScore.ownershipTransfers && candidate.queue->queueClass != selectedQueue->queueClass){
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
        }
        NWB_ASSERT(selectedQueue);
        scoredQueues[groupIndex] = selectedQueue->id;
        scoredReasons[groupIndex] = HasCapabilities(group.requiredCapabilities, GpuQueueCapability::Graphics) ? GpuTaskQueueAssignmentReason::RequiredGraphics : conservative ? GpuTaskQueueAssignmentReason::Conservative : GpuTaskQueueAssignmentReason::Scored;
    }
    for(usize groupIndex = 0u; groupIndex < groups.size(); ++groupIndex){
        const GpuTaskQueuePlacementGroup& group = groups[groupIndex];
        auto& selectedQueue = *MakeNotNull(FindPhysicalQueueInfo(topology, scoredQueues[groupIndex]));
        for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
            GpuTaskQueueAssignment& assignment = outAssignments.m_assignments[group.assignmentOffset + taskOffset];
            assignment.initialQueue = selectedQueue.id;
            assignment.queue = selectedQueue.id;
            assignment.queueClass = selectedQueue.queueClass;
            assignment.reason = scoredReasons[groupIndex];
            assignment.dedicated = selectedQueue.dedicated;
        }
    }

    // Optional physical balancing applies to independent singleton placements. It cannot separate an explicit merge chain.
    bool needsSameClassBalancing = false;
    for(const GpuTaskQueuePlacementGroup& group : groups){
        if(group.assignmentCount != 1u || group.initialOwnershipQueue.valid())
            continue;
        const GpuTaskGraphTaskView task = graph.taskAt(outAssignments.m_assignments[group.assignmentOffset].task.index);
        if(task.scheduling.allowSameClassQueueRouting && task.scheduling.overlapPreferred && !task.scheduling.avoidQueueCrossing){
            needsSameClassBalancing = true;
            break;
        }
    }
    if(needsSameClassBalancing){
        Vector<u64, Alloc::ScratchArena> prefixQueueCosts(topology.queueCount, 0u, scratchArena);
        for(const GpuTaskQueuePlacementGroup& group : groups){
            const usize assignmentIndex = group.assignmentOffset;
            GpuTaskQueueAssignment& assignment = outAssignments.m_assignments[assignmentIndex];
            auto selectedQueue = MakeNotNull(FindPhysicalQueueInfo(topology, assignment.queue));
            if(group.assignmentCount == 1u && !group.initialOwnershipQueue.valid()){
                const GpuTaskGraphTaskView task = graph.taskAt(assignment.task.index);
                if(task.scheduling.allowSameClassQueueRouting && task.scheduling.overlapPreferred && !task.scheduling.avoidQueueCrossing){
                    const GpuPhysicalQueueInfo* const dependencyQueue = task.scheduling.preserveSameClassQueueWithDirectDependency
                        ? FindDirectDependencySameClassQueue(graph, analysis, task, outAssignments.m_assignments, outAssignments.m_assignmentIndicesByTask, topology, *selectedQueue, assignmentIndex, task.scheduling.allowCrossFamilySameClassQueueRouting)
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
                        prefixQueueCosts,
                        scoringData,
                        topology,
                        task,
                        *selectedQueue,
                        task.scheduling.allowCrossFamilySameClassQueueRouting,
                        task.scheduling.preferNonPrimarySameClassQueue
                    )){
                        if(leastLoadedQueue->id != selectedQueue->id){
                            selectedQueue = leastLoadedQueue;
                            assignment.modifiers |= GpuTaskQueueAssignmentModifier::SameClassLoadBalance;
                            if(task.scheduling.preferNonPrimarySameClassQueue)
                                assignment.modifiers |= GpuTaskQueueAssignmentModifier::NonPrimaryRouting;
                        }
                    }
                }
                assignment.queue = selectedQueue->id;
                assignment.queueClass = selectedQueue->queueClass;
                assignment.dedicated = selectedQueue->dedicated;
                assignment.initialQueue = selectedQueue->id;
            }
            // Fixed and merged groups contribute the same prefix cost as routed singletons.
            u64& prefixQueueCost = prefixQueueCosts[static_cast<usize>(selectedQueue.get() - topology.queues)];
            for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
                const GpuTaskId task = outAssignments.m_assignments[group.assignmentOffset + taskOffset].task;
                const u64 cost = scoringData.m_taskCosts[task.index];
                prefixQueueCost = prefixQueueCost > Limit<u64>::s_Max - cost ? Limit<u64>::s_Max : prefixQueueCost + cost;
            }
        }
    }

    if(HasUsableTimingFeedback(options, topology.queues[0u].id.deviceGeneration)){
        scoringData.rebuildAssignmentLoads(outAssignments.m_assignments, topology);
        for(const GpuTaskQueuePlacementGroup& group : groups){
            if(group.assignmentCount != 1u || group.initialOwnershipQueue.valid())
                continue;

            GpuTaskQueueAssignment& assignment = outAssignments.m_assignments[group.assignmentOffset];
            auto& staticQueue = *MakeNotNull(FindPhysicalQueueInfo(topology, assignment.queue));
            const GpuTaskGraphTaskView task = graph.taskAt(assignment.task.index);
            const GpuTaskTimingAssignmentKey key{ .task = task.identity, .variant = task.timing.variant, .resolutionClass = task.timing.resolutionClass };
            auto selectedQueue = MakeNotNull(&staticQueue);
            const GpuPhysicalQueueInfo& incumbent = FindTimingFeedbackIncumbent(graph, topology, task, staticQueue, key, *options.timingHistory);
            const GpuPhysicalQueueInfo* const calibrationQueue = FindTimingFeedbackCalibrationQueue(graph, topology, task, incumbent, key, *options.timingHistory, options.timingFeedbackPolicy, options.timingFrameIndex);
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
                incumbent,
                key,
                *options.timingHistory,
                options.timingFeedbackPolicy,
                options.timingFrameIndex
            )){
                selectedQueue = timingQueue;
                assignment.modifiers |= GpuTaskQueueAssignmentModifier::TimingFeedback;
            }
            else if(&incumbent != &staticQueue){
                selectedQueue = &incumbent;
                assignment.modifiers |= GpuTaskQueueAssignmentModifier::TimingFeedback;
            }
            scoringData.updateAssignmentLoads(assignment.task, assignment.queue, selectedQueue->id);
            assignment.queue = selectedQueue->id;
            assignment.queueClass = selectedQueue->queueClass;
            assignment.dedicated = selectedQueue->dedicated;
        }
    }

    scoringData.rebuildAssignmentLoads(outAssignments.m_assignments, topology);
    for(GpuTaskQueueAssignment& assignment : outAssignments.m_assignments){
        const GpuTaskGraphTaskView task = graph.taskAt(assignment.task.index);
        auto& selectedQueue = *MakeNotNull(FindPhysicalQueueInfo(topology, assignment.queue));
        assignment.score = BuildQueueAssignmentScore(graph, analysis, outAssignments.m_assignments, outAssignments.m_assignmentIndicesByTask, topology, schedulingReachability, scoringData, task, selectedQueue);
    }
    outAssignments.m_diagnostic.status = GpuTaskGraphQueueAssignmentStatus::Success;
    outAssignments.m_valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

