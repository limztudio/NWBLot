// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <global/atomic_identity.h>
#include <global/bit.h>
#include <global/overflow.h>


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
    m_assignmentRevision = 0u;
    m_taskCount = 0u;
    m_valid = false;
}

b8 GpuTaskGraphQueueAssignments::validFor(const GpuTaskGraph::DeclarationReadView& graph)const noexcept{
    FOUNDATION_ASSERT(!m_valid || (m_assignments.size() == m_taskCount && m_assignmentIndicesByTask.size() == m_taskCount));
    return m_valid && m_generation == graph.generation() && m_declarationRevision == graph.declarationRevision() && m_taskCount == graph.taskCount();
}

b8 GpuTaskGraphQueueAssignments::validFor(const GpuTaskGraph::DeclarationReadView& graph, const GpuCompiledGraph::ReadView& compiledPlan)const noexcept{
    return validFor(graph) && compiledPlan.validFor(graph) && m_compiledPlanGeneration != 0u && m_compiledPlanGeneration == compiledPlan.planGeneration();
}

const GpuTaskQueueAssignment* GpuTaskGraphQueueAssignments::find(const GpuTaskId& task)const noexcept{
    if(!m_valid)
        return nullptr;
    return GpuTaskGraphCompilerDetail::FindQueueAssignment(m_assignments, m_assignmentIndicesByTask, task);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuTaskGraphCompiler::assignQueues(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphQueueAssignmentOptions& options,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraphQueueAssignments& outAssignments
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
    if(topology.queueCount <= s_BitsPerWord){
        u64 seenQueueLoadMask = 0u;
        for(usize queueLoadIndex = 0u; queueLoadIndex < options.queueLoadCount; ++queueLoadIndex){
            const GpuPhysicalQueueInfo* const info = FindPhysicalQueueInfo(topology, options.queueLoads[queueLoadIndex].queue);
            if(!info)
                return fail(GpuTaskGraphQueueAssignmentStatus::InvalidQueueLoad);
            const u64 bit = BitMask<u64>(static_cast<u32>(static_cast<usize>(info - topology.queues)));
            if((seenQueueLoadMask & bit) != 0u)
                return fail(GpuTaskGraphQueueAssignmentStatus::InvalidQueueLoad);
            seenQueueLoadMask |= bit;
        }
    }
    else{
        for(usize queueLoadIndex = 0u; queueLoadIndex < options.queueLoadCount; ++queueLoadIndex){
            const GpuTaskQueueLoad& queueLoad = options.queueLoads[queueLoadIndex];
            if(!FindPhysicalQueueInfo(topology, queueLoad.queue))
                return fail(GpuTaskGraphQueueAssignmentStatus::InvalidQueueLoad);
            for(usize previousIndex = 0u; previousIndex < queueLoadIndex; ++previousIndex){
                if(options.queueLoads[previousIndex].queue == queueLoad.queue)
                    return fail(GpuTaskGraphQueueAssignmentStatus::InvalidQueueLoad);
            }
        }
    }
    if(options.timingFeedbackPolicy.enabled && !options.timingFeedbackPolicy.valid())
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback);

    const usize taskCount = CompilerGraphAccess::TaskCount(graph);
    outAssignments.m_generation = CompilerGraphAccess::Generation(graph);
    outAssignments.m_declarationRevision = CompilerGraphAccess::DeclarationRevision(graph);
    outAssignments.m_taskCount = taskCount;
    outAssignments.m_assignmentIndicesByTask.resize(taskCount, Limit<u32>::s_Max);
    outAssignments.m_assignments.reserve(taskCount);

    Alloc::ScratchArena::Scope scratchScope(scratchArena);
    GpuTaskQueueLegalityCache legalityCache(scratchArena);
    legalityCache.build(graph, topology);

    Vector<GpuTaskQueuePlacementGroup, Alloc::ScratchArena> groups(scratchArena);
    const auto placementGroups = BuildQueuePlacementGroups(graph, analysis, topology, scratchArena, groups, &legalityCache);
    if(!placementGroups){
        outAssignments.m_diagnostic = placementGroups.error();
        return false;
    }
    b8 needsSchedulingReachability = false;
    if(topology.queueCount > 1u && taskCount > 1u && groups.size() > 1u){
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            const GpuTaskSchedulingHint& scheduling = CompilerGraphAccess::TaskNode(graph, taskIndex).scheduling;
            if(scheduling.overlapPreferred && !scheduling.avoidQueueCrossing){
                needsSchedulingReachability = true;
                break;
            }
        }
    }
    if(needsSchedulingReachability && !analysis.schedulingEdges().empty()){
        if(legalityCache.active){
            u64 unionLegalMask = 0u;
            for(const GpuTaskQueuePlacementGroup& group : groups){
                u64 groupMask = topology.queueCount == s_BitsPerWord ? ~static_cast<u64>(0u) : ((static_cast<u64>(1u) << topology.queueCount) - 1u);
                for(usize taskOffset = 0u; taskOffset < group.assignmentCount && groupMask != 0u; ++taskOffset){
                    const GpuTaskId taskID = analysis.topologicalOrder()[group.assignmentOffset + taskOffset];
                    groupMask &= legalityCache.taskMask(taskID.index);
                }
                if(group.initialOwnershipQueue.valid()){
                    u64 ownerMask = 0u;
                    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
                        if(topology.queues[queueIndex].id == group.initialOwnershipQueue){
                            ownerMask = BitMask<u64>(static_cast<u32>(queueIndex));
                            break;
                        }
                    }
                    groupMask &= ownerMask;
                }
                unionLegalMask |= groupMask;
                if(static_cast<u32>(CountSetBits(unionLegalMask)) >= 2u)
                    break;
            }
            needsSchedulingReachability = static_cast<u32>(CountSetBits(unionLegalMask)) >= 2u;
        }
        else{
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
            needsSchedulingReachability = capableQueueCount >= s_OverlapQueueMinimum;
        }
    }
    GpuTaskSchedulingReachability schedulingReachability(scratchArena);
    if(needsSchedulingReachability && !BuildGpuTaskSchedulingReachability(graph, analysis, schedulingReachability))
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidGraphAnalysis);
    GpuTaskQueueScoringData scoringData(graph, analysis, schedulingReachability, options, scratchArena, &topology);

    for(const GpuTaskQueuePlacementGroup& group : groups){
        auto& selectedQueue = *MakeNotNull(FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, group, CommandQueue::kCount, &legalityCache));
        for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
            const GpuTaskId taskID = analysis.topologicalOrder()[group.assignmentOffset + taskOffset];
            FOUNDATION_ASSERT(
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
            });
            outAssignments.m_assignmentIndicesByTask[taskID.index] = assignmentIndex;
        }
    }
    FOUNDATION_ASSERT(outAssignments.m_assignments.size() == outAssignments.m_assignmentIndicesByTask.size());
    scoringData.rebuildAssignmentLoads(outAssignments.m_assignments, topology);

    Vector<GpuPhysicalQueueId, Alloc::ScratchArena> scoredQueues(groups.size(), scratchArena);
    Vector<GpuTaskQueueAssignmentReason::Enum, Alloc::ScratchArena> scoredReasons(groups.size(), scratchArena);
    for(usize groupIndex = 0u; groupIndex < groups.size(); ++groupIndex){
        const GpuTaskQueuePlacementGroup& group = groups[groupIndex];
        const GpuPhysicalQueueInfo* selectedQueue = nullptr;
        GpuQueueAssignmentScore selectedScore;
        b8 allTasksTiny = true;
        b8 conservative = false;
        for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
            const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, analysis.topologicalOrder()[group.assignmentOffset + taskOffset].index);
            allTasksTiny = allTasksTiny && task.scheduling.cost == GpuTaskCostHint::Tiny;
            conservative = conservative || !task.scheduling.overlapPreferred || task.scheduling.avoidQueueCrossing;
        }
        conservative = conservative || allTasksTiny;
        struct Candidate{
            const GpuPhysicalQueueInfo* queue = nullptr;
            GpuQueueAssignmentScore score;
        };
        Array<Candidate, CommandQueue::kCount> candidates{};
        usize candidateCount = 0u;
        for(u8 queueClassValue = 0u; queueClassValue < CommandQueue::kCount; ++queueClassValue){
            Candidate& candidate = candidates[queueClassValue];
            candidate.queue = FindBestLegalQueuePlacementGroupCandidate(graph, analysis, topology, group, static_cast<CommandQueue::Enum>(queueClassValue), &legalityCache);
            if(candidate.queue){
                selectedQueue = candidate.queue;
                ++candidateCount;
            }
        }
        if(candidateCount > 1u){
            selectedQueue = nullptr;
            b8 hasIndependentOverlap = false;
            const b8 computeOverlap = !conservative;
            for(Candidate& candidate : candidates){
                if(!candidate.queue)
                    continue;
                candidate.score = BuildQueuePlacementGroupScore(graph, analysis, outAssignments.m_assignments, outAssignments.m_assignmentIndicesByTask, topology, schedulingReachability, scoringData, group, *candidate.queue, computeOverlap);
                hasIndependentOverlap = hasIndependentOverlap || candidate.score.overlap > 0;
                if(conservative){
                    candidate.score.overlap = 0;
                    candidate.score.queueLoad = 0;
                }
            }
            const b8 compareTotalScore = !conservative && hasIndependentOverlap;
            for(const Candidate& candidate : candidates){
                if(!candidate.queue)
                    continue;
                b8 better = IsBetterAutomaticQueueAssignmentCandidate(candidate.score, *candidate.queue, selectedScore, selectedQueue, compareTotalScore);
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
        FOUNDATION_ASSERT(selectedQueue);
        scoredQueues[groupIndex] = selectedQueue->id;
        scoredReasons[groupIndex] = HasCapabilities(group.requiredCapabilities, GpuQueueCapability::Graphics) ? GpuTaskQueueAssignmentReason::RequiredGraphics : conservative ? GpuTaskQueueAssignmentReason::Conservative : GpuTaskQueueAssignmentReason::Scored;
    }
    b8 anyAssignmentChanged = false;
    for(usize groupIndex = 0u; groupIndex < groups.size(); ++groupIndex){
        const GpuTaskQueuePlacementGroup& group = groups[groupIndex];
        auto& selectedQueue = *MakeNotNull(FindPhysicalQueueInfo(topology, scoredQueues[groupIndex]));
        for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
            GpuTaskQueueAssignment& assignment = outAssignments.m_assignments[group.assignmentOffset + taskOffset];
            if(assignment.queue != selectedQueue.id)
                anyAssignmentChanged = true;
            assignment.initialQueue = selectedQueue.id;
            assignment.queue = selectedQueue.id;
            assignment.queueClass = selectedQueue.queueClass;
            assignment.reason = scoredReasons[groupIndex];
            assignment.dedicated = selectedQueue.dedicated;
        }
    }

    b8 needsSameClassBalancing = false;
    for(const GpuTaskQueuePlacementGroup& group : groups){
        if(group.assignmentCount != 1u || group.initialOwnershipQueue.valid())
            continue;
        const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, outAssignments.m_assignments[group.assignmentOffset].task.index);
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
                const GpuTaskGraphDetail::GpuTaskNode& taskNode = CompilerGraphAccess::TaskNode(graph, assignment.task.index);
                if(taskNode.scheduling.allowSameClassQueueRouting && taskNode.scheduling.overlapPreferred && !taskNode.scheduling.avoidQueueCrossing){
                    const GpuTaskGraphTaskView taskView = graph.taskAt(assignment.task.index);
                    const GpuPhysicalQueueInfo* const dependencyQueue = taskNode.scheduling.preserveSameClassQueueWithDirectDependency
                        ? FindDirectDependencySameClassQueue(graph, analysis, taskView, outAssignments.m_assignments, outAssignments.m_assignmentIndicesByTask, topology, *selectedQueue, assignmentIndex, taskNode.scheduling.allowCrossFamilySameClassQueueRouting, &legalityCache)
                        : nullptr
                    ;
                    if(dependencyQueue){
                        if(dependencyQueue->id != selectedQueue->id){
                            selectedQueue = dependencyQueue;
                            assignment.modifiers |= GpuTaskQueueAssignmentModifier::DirectDependencyAffinity;
                            anyAssignmentChanged = true;
                        }
                    }
                    else if(const GpuPhysicalQueueInfo* const leastLoadedQueue = FindLeastLoadedSameClassQueue(
                        graph,
                        prefixQueueCosts,
                        scoringData,
                        topology,
                        taskView,
                        *selectedQueue,
                        taskNode.scheduling.allowCrossFamilySameClassQueueRouting,
                        taskNode.scheduling.preferNonPrimarySameClassQueue,
                        &legalityCache
                    )){
                        if(leastLoadedQueue->id != selectedQueue->id){
                            selectedQueue = leastLoadedQueue;
                            assignment.modifiers |= GpuTaskQueueAssignmentModifier::SameClassLoadBalance;
                            if(taskNode.scheduling.preferNonPrimarySameClassQueue)
                                assignment.modifiers |= GpuTaskQueueAssignmentModifier::NonPrimaryRouting;
                            anyAssignmentChanged = true;
                        }
                    }
                }
                assignment.queue = selectedQueue->id;
                assignment.queueClass = selectedQueue->queueClass;
                assignment.dedicated = selectedQueue->dedicated;
                assignment.initialQueue = selectedQueue->id;
            }
            u64& prefixQueueCost = prefixQueueCosts[static_cast<usize>(selectedQueue.get() - topology.queues)];
            for(usize taskOffset = 0u; taskOffset < group.assignmentCount; ++taskOffset){
                const GpuTaskId task = outAssignments.m_assignments[group.assignmentOffset + taskOffset].task;
                const u64 cost = scoringData.taskCosts[task.index];
                prefixQueueCost = AddSaturating<u64>(prefixQueueCost, cost);
            }
        }
    }

    b8 scoringLoadsCurrent = !anyAssignmentChanged;
    if(HasUsableTimingFeedback(options, topology.queues[0u].id.deviceGeneration)){
        if(!scoringLoadsCurrent)
            scoringData.rebuildAssignmentLoads(outAssignments.m_assignments, topology);
        scoringLoadsCurrent = true;
        for(const GpuTaskQueuePlacementGroup& group : groups){
            if(group.assignmentCount != 1u || group.initialOwnershipQueue.valid())
                continue;

            GpuTaskQueueAssignment& assignment = outAssignments.m_assignments[group.assignmentOffset];
            auto& staticQueue = *MakeNotNull(FindPhysicalQueueInfo(topology, assignment.queue));
            const GpuTaskGraphTaskView task = graph.taskAt(assignment.task.index);
            const GpuTaskTimingAssignmentKey key{ .task = task.identity, .variant = task.timing.variant, .resolutionClass = task.timing.resolutionClass };
            auto selectedQueue = MakeNotNull(&staticQueue);
            const GpuPhysicalQueueInfo& incumbent = FindTimingFeedbackIncumbent(graph, topology, task, staticQueue, key, *options.timingHistory, &legalityCache);
            const GpuPhysicalQueueInfo* const calibrationQueue = FindTimingFeedbackCalibrationQueue(graph, topology, task, incumbent, key, *options.timingHistory, options.timingFeedbackPolicy, options.timingFrameIndex, &legalityCache);
            if(calibrationQueue){
                selectedQueue = calibrationQueue;
                assignment.modifiers |= GpuTaskQueueAssignmentModifier::TimingCalibration;
            }
            else if(const GpuPhysicalQueueInfo* const timingQueue = FindTimingFeedbackQueueCached(
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
                options.timingFrameIndex,
                &legalityCache
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

    if(!scoringLoadsCurrent)
        scoringData.rebuildAssignmentLoads(outAssignments.m_assignments, topology);
    for(GpuTaskQueueAssignment& assignment : outAssignments.m_assignments){
        const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, assignment.task.index);
        auto& selectedQueue = *MakeNotNull(FindPhysicalQueueInfo(topology, assignment.queue));
        assignment.score = BuildTaskQueueAssignmentScore(graph, analysis, outAssignments.m_assignments, outAssignments.m_assignmentIndicesByTask, topology, schedulingReachability, scoringData, assignment.task, task.scheduling, selectedQueue);
    }
    static Atomic<u64> s_NextAssignmentRevision{ 0u };
    outAssignments.m_diagnostic.status = GpuTaskGraphQueueAssignmentStatus::Success;
    outAssignments.m_assignmentRevision = NextAtomicWrappingNonzero(s_NextAssignmentRevision);
    outAssignments.m_valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

