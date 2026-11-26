// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"
#include "timing_decision.h"

#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 AllowsTimingFeedbackRouting(const GpuTaskGraphTaskView& task)noexcept{
    return task.scheduling.allowTimingFeedbackRouting && (task.scheduling.allowSameClassQueueRouting || task.scheduling.allowCrossClassTimingFeedbackRouting) && task.scheduling.overlapPreferred && !task.scheduling.avoidQueueCrossing;
}

[[nodiscard]] b8 IsLegalTimingFeedbackRoute(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology, const GpuTaskGraphTaskView& task, const GpuPhysicalQueueInfo& incumbent, const GpuPhysicalQueueInfo& candidate)noexcept{
    if(!AllowsTimingFeedbackRouting(task) || !IsLegalQueueAssignmentCandidate(graph, topology, task, candidate))
        return false;
    if(candidate.familyIndex != incumbent.familyIndex && !task.scheduling.allowCrossFamilySameClassQueueRouting)
        return false;

    if(candidate.queueClass == incumbent.queueClass)
        return task.scheduling.allowSameClassQueueRouting;

    return task.scheduling.allowCrossClassTimingFeedbackRouting;
}

[[nodiscard]] GpuTaskTimingKey TimingHistoryKeyForQueue(const GpuTaskTimingAssignmentKey& assignmentKey, const CommandQueue::Enum queueClass)noexcept{
    return GpuTaskTimingKey{ .task = assignmentKey.task, .variant = assignmentKey.variant, .resolutionClass = assignmentKey.resolutionClass, .queue = queueClass, };
}

[[nodiscard]] b8 HasUsableTimingFeedback(const GpuTaskGraphQueueAssignmentOptions& options, const u16 deviceGeneration)noexcept{
    FOUNDATION_ASSERT(!options.timingFeedbackPolicy.enabled || options.timingFeedbackPolicy.valid());
    return options.timingFeedbackPolicy.enabled && options.timingHistory && options.timingHistory->valid() && options.timingHistory->deviceGeneration() == deviceGeneration;
}

[[nodiscard]] const GpuPhysicalQueueInfo* FindLeastLoadedSameClassQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const Span<const u64> prefixQueueCosts,
    const GpuTaskQueueScoringData& scoringData,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskId taskId,
    const GpuPhysicalQueueInfo& baseQueue,
    const b8 allowCrossFamilyRouting,
    const b8 preferNonPrimaryQueue,
    const GpuTaskQueueLegalityCache* const legalityCache
)noexcept{
    const GpuPhysicalQueueInfo* result = nullptr;
    u64 resultLoad = Limit<u64>::s_Max;
    const Optional<GpuTaskGraphTaskView> taskView = legalityCache ? Optional<GpuTaskGraphTaskView>{} : Optional<GpuTaskGraphTaskView>{ graph.taskAt(taskId.index) };
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        const b8 legal = legalityCache
            ? legalityCache->isLegal(graph, topology, taskId, queueIndex)
            : IsLegalQueueAssignmentCandidate(graph, topology, *taskView, candidate);
        if(candidate.queueClass != baseQueue.queueClass || (!allowCrossFamilyRouting && candidate.familyIndex != baseQueue.familyIndex) || !legal)
            continue;

        u64 load = prefixQueueCosts[queueIndex];
        const u64 externalQueueLoad = !scoringData.externalQueueLoadsByIndex.empty()
            ? scoringData.externalQueueLoadByIndex(queueIndex)
            : scoringData.externalQueueLoad(candidate.id)
        ;
        load = AddSaturating<u64>(load, externalQueueLoad);
        const b8 candidateIsNonPrimary = candidate.id != baseQueue.id;
        const b8 resultIsNonPrimary = result && result->id != baseQueue.id;
        b8 winsEqualLoad = IsBetterQueue(candidate, result);
        if(preferNonPrimaryQueue && candidateIsNonPrimary != resultIsNonPrimary)
            winsEqualLoad = candidateIsNonPrimary;
        if(!result || load < resultLoad || (load == resultLoad && winsEqualLoad)){
            result = &candidate;
            resultLoad = load;
        }
    }
    return result;
}

[[nodiscard]] const GpuPhysicalQueueInfo* FindDirectDependencySameClassQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskId taskId,
    const Span<const GpuTaskQueueAssignment> assignments,
    const Span<const u32> assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuPhysicalQueueInfo& baseQueue,
    const usize assignedPrefixCount,
    const b8 allowCrossFamilyRouting,
    const GpuTaskQueueLegalityCache* const legalityCache
)noexcept{
    FOUNDATION_ASSERT(assignedPrefixCount <= assignments.size());
    const GpuPhysicalQueueInfo* result = nullptr;
    usize resultAssignmentIndex = 0u;
    const Optional<GpuTaskGraphTaskView> taskView = legalityCache ? Optional<GpuTaskGraphTaskView>{} : Optional<GpuTaskGraphTaskView>{ graph.taskAt(taskId.index) };
    const GpuTaskGraphSchedulingTaskIndexView producerIndices = analysis.schedulingProducers(taskId);
    for(usize producerIndex = 0u; producerIndex < producerIndices.taskCount; ++producerIndex){
        const u32 producerTaskIndex = producerIndices[producerIndex];
        FOUNDATION_ASSERT(producerTaskIndex < assignmentIndicesByTask.size());

        const u32 producerAssignmentIndex = assignmentIndicesByTask[producerTaskIndex];
        if(producerAssignmentIndex >= assignedPrefixCount)
            continue;
        const GpuTaskQueueAssignment& assignment = assignments[producerAssignmentIndex];
        FOUNDATION_ASSERT(assignment.task.generation == taskId.generation && assignment.task.index == producerTaskIndex);
        auto& candidate = *MakeNotNull(FindPhysicalQueueInfo(topology, assignment.queue));
        const usize queueIndex = static_cast<usize>(&candidate - topology.queues);
        const b8 legal = legalityCache
            ? legalityCache->isLegal(graph, topology, taskId, queueIndex)
            : IsLegalQueueAssignmentCandidate(graph, topology, *taskView, candidate);
        if(candidate.queueClass != baseQueue.queueClass || (!allowCrossFamilyRouting && candidate.familyIndex != baseQueue.familyIndex) || !legal)
            continue;
        if(!result || producerAssignmentIndex > resultAssignmentIndex){
            result = &candidate;
            resultAssignmentIndex = producerAssignmentIndex;
        }
    }
    return result;
}

[[nodiscard]] const GpuPhysicalQueueInfo& FindTimingFeedbackIncumbent(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& staticQueue,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskQueueLegalityCache* const legalityCache
)noexcept{
    const GpuTaskTimingAssignmentState* const assignmentState = historySnapshot.findAssignment(key);
    if(!assignmentState || assignmentState->lastAcceptedQueue == staticQueue.id)
        return staticQueue;

    const GpuPhysicalQueueInfo* const acceptedQueue = FindPhysicalQueueInfo(topology, assignmentState->lastAcceptedQueue);
    if(!acceptedQueue)
        return staticQueue;
    const usize queueIndex = static_cast<usize>(acceptedQueue - topology.queues);
    const b8 legal = AllowsTimingFeedbackRouting(task)
        && (legalityCache ? legalityCache->isLegal(graph, topology, task, queueIndex) : IsLegalQueueAssignmentCandidate(graph, topology, task, *acceptedQueue))
        && (acceptedQueue->familyIndex == staticQueue.familyIndex || task.scheduling.allowCrossFamilySameClassQueueRouting)
        && (acceptedQueue->queueClass == staticQueue.queueClass ? task.scheduling.allowSameClassQueueRouting : task.scheduling.allowCrossClassTimingFeedbackRouting);
    if(!legal)
        return staticQueue;
    return *acceptedQueue;
}

[[nodiscard]] static b8 IsEligibleTimingFeedbackCandidate(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuPhysicalQueueInfo& candidate,
    const usize queueIndex,
    const GpuTaskQueueLegalityCache* const legalityCache
)noexcept{
    if(candidate.id == incumbent.id)
        return false;
    if(candidate.familyIndex != incumbent.familyIndex && !task.scheduling.allowCrossFamilySameClassQueueRouting)
        return false;
    if(candidate.queueClass == incumbent.queueClass ? !task.scheduling.allowSameClassQueueRouting : !task.scheduling.allowCrossClassTimingFeedbackRouting)
        return false;
    return legalityCache
        ? legalityCache->isLegal(graph, topology, task, queueIndex)
        : IsLegalQueueAssignmentCandidate(graph, topology, task, candidate);
}

[[nodiscard]] const GpuPhysicalQueueInfo* FindTimingFeedbackQueueCached(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const Span<const GpuTaskQueueAssignment> assignments,
    const Span<const u32> assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskTimingFeedbackPolicy& policy,
    const u64 frameIndex,
    const GpuTaskQueueLegalityCache* const legalityCache
)noexcept{
    if(!AllowsTimingFeedbackRouting(task))
        return nullptr;

    const GpuTaskTimingHistory* const incumbentHistory = historySnapshot.find(TimingHistoryKeyForQueue(key, incumbent.queueClass), incumbent.id);
    const GpuTaskTimingAssignmentState* const assignmentState = historySnapshot.findAssignment(key);
    if(!incumbentHistory || !GpuTaskTimingDecision::HistoryMeetsMinimumSamples(*incumbentHistory, policy))
        return nullptr;
    if(assignmentState && !GpuTaskTimingDecision::SwitchDwellElapsed(*assignmentState, frameIndex, policy))
        return nullptr;

    const GpuPhysicalQueueInfo* result = nullptr;
    const GpuTaskTimingHistory* resultHistory = nullptr;
    GpuQueueAssignmentScore resultScore;
    b8 hasResultScore = false;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(!IsEligibleTimingFeedbackCandidate(graph, topology, task, incumbent, candidate, queueIndex, legalityCache))
            continue;

        const GpuTaskTimingHistory* const candidateHistory = historySnapshot.find(TimingHistoryKeyForQueue(key, candidate.queueClass), candidate.id);
        if(!candidateHistory)
            continue;

        const b8 canSwitch = assignmentState
            ? GpuTaskTimingDecision::CanSwitch(*incumbentHistory, *candidateHistory, *assignmentState, incumbent.id, candidate.id, frameIndex, policy)
            : GpuTaskTimingDecision::HistoryMeetsMinimumSamples(*candidateHistory, policy)
                && GpuTaskTimingDecision::BenefitExceedsHysteresis(*incumbentHistory, *candidateHistory, policy)
        ;
        if(!canSwitch)
            continue;

        if(!result || candidateHistory->averageSeconds < resultHistory->averageSeconds){
            result = &candidate;
            resultHistory = candidateHistory;
            hasResultScore = false;
            continue;
        }
        if(candidateHistory->averageSeconds != resultHistory->averageSeconds)
            continue;

        const GpuQueueAssignmentScore candidateScore = BuildQueueAssignmentScore(graph, analysis, assignments, assignmentIndicesByTask, topology, schedulingReachability, scoringData, task, candidate);
        if(!hasResultScore){
            resultScore = BuildQueueAssignmentScore(graph, analysis, assignments, assignmentIndicesByTask, topology, schedulingReachability, scoringData, task, *result);
            hasResultScore = true;
        }
        const i32 candidateTotal = candidateScore.total();
        const i32 resultTotal = resultScore.total();
        if(candidateTotal > resultTotal || (candidateTotal == resultTotal && IsBetterQueue(candidate, result))){
            result = &candidate;
            resultHistory = candidateHistory;
            resultScore = candidateScore;
        }
    }
    return result;
}

[[nodiscard]] const GpuPhysicalQueueInfo* FindTimingFeedbackQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const Span<const GpuTaskQueueAssignment> assignments,
    const Span<const u32> assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskTimingFeedbackPolicy& policy,
    const u64 frameIndex
)noexcept{
    return FindTimingFeedbackQueueCached(
        graph,
        analysis,
        assignments,
        assignmentIndicesByTask,
        topology,
        schedulingReachability,
        scoringData,
        task,
        incumbent,
        key,
        historySnapshot,
        policy,
        frameIndex,
        nullptr
    );
}

[[nodiscard]] const GpuPhysicalQueueInfo* FindTimingFeedbackCalibrationQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskTimingFeedbackPolicy& policy,
    const u64 frameIndex,
    const GpuTaskQueueLegalityCache* const legalityCache
)noexcept{
    if(policy.calibrationIntervalFrames == 0u || frameIndex % policy.calibrationIntervalFrames != 0u || !AllowsTimingFeedbackRouting(task))
        return nullptr;

    const auto sampleCountFor = [&](const GpuPhysicalQueueInfo& queue)noexcept{
        const GpuTaskTimingHistory* const history = historySnapshot.find(TimingHistoryKeyForQueue(key, queue.queueClass), queue.id);
        return history ? history->sampleCount : 0u;
    };

    auto result = MakeNotNull(&incumbent);
    u32 resultSampleCount = sampleCountFor(incumbent);
    b8 needsCalibration = resultSampleCount < policy.minimumSampleCount;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(!IsEligibleTimingFeedbackCandidate(graph, topology, task, incumbent, candidate, queueIndex, legalityCache))
            continue;

        const u32 candidateSampleCount = sampleCountFor(candidate);
        if(candidateSampleCount >= policy.minimumSampleCount)
            continue;

        needsCalibration = true;
        if(candidateSampleCount < resultSampleCount || (candidateSampleCount == resultSampleCount && result.get() != &incumbent && IsBetterQueue(candidate, result.get()))){
            result = &candidate;
            resultSampleCount = candidateSampleCount;
        }
    }
    return needsCalibration ? result.get() : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

