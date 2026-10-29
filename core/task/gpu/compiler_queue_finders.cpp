// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Same-family queues share work via timeline semaphore, no ownership transfer. Cross-family stays opt-in.
[[nodiscard]] const GpuPhysicalQueueInfo* FindLeastLoadedSameClassQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const Span<const u64> prefixQueueCosts,
    const GpuTaskQueueScoringData& scoringData,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& baseQueue,
    const b8 allowCrossFamilyRouting,
    const b8 preferNonPrimaryQueue
)noexcept{
    const GpuPhysicalQueueInfo* result = nullptr;
    u64 resultLoad = Limit<u64>::s_Max;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(candidate.queueClass != baseQueue.queueClass || (!allowCrossFamilyRouting && candidate.familyIndex != baseQueue.familyIndex) || !IsLegalQueueAssignmentCandidate(graph, topology, task, candidate))
            continue;

        u64 load = prefixQueueCosts[queueIndex];
        const u64 externalQueueLoad = scoringData.externalQueueLoad(candidate.id);
        load = load > Limit<u64>::s_Max - externalQueueLoad ? Limit<u64>::s_Max : load + externalQueueLoad;
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
    const GpuTaskGraphTaskView& task,
    const Span<const GpuTaskQueueAssignment> assignments,
    const Span<const u32> assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuPhysicalQueueInfo& baseQueue,
    const usize assignedPrefixCount,
    const b8 allowCrossFamilyRouting
)noexcept{
    // Balancing runs after every task has a dense assignment from the same topology; the prefix ends at the current group.
    NWB_ASSERT(assignedPrefixCount <= assignments.size());
    const GpuPhysicalQueueInfo* result = nullptr;
    usize resultAssignmentIndex = 0u;
    const GpuTaskGraphSchedulingTaskIndexView producerIndices = analysis.schedulingProducers(task.id);
    for(usize producerIndex = 0u; producerIndex < producerIndices.taskCount; ++producerIndex){
        const u32 producerTaskIndex = producerIndices[producerIndex];
        NWB_ASSERT(producerTaskIndex < assignmentIndicesByTask.size());

        const u32 producerAssignmentIndex = assignmentIndicesByTask[producerTaskIndex];
        if(producerAssignmentIndex >= assignedPrefixCount)
            continue;
        const GpuTaskQueueAssignment& assignment = assignments[producerAssignmentIndex];
        NWB_ASSERT(assignment.task.generation == task.id.generation && assignment.task.index == producerTaskIndex);
        auto& candidate = *MakeNotNull(FindPhysicalQueueInfo(topology, assignment.queue));
        if(candidate.queueClass != baseQueue.queueClass || (!allowCrossFamilyRouting && candidate.familyIndex != baseQueue.familyIndex) || !IsLegalQueueAssignmentCandidate(graph, topology, task, candidate))
            continue;
        if(!result || producerAssignmentIndex > resultAssignmentIndex){
            result = &candidate;
            resultAssignmentIndex = producerAssignmentIndex;
        }
    }
    return result;
}

[[nodiscard]] b8 AllowsTimingFeedbackRouting(const GpuTaskGraphTaskView& task)noexcept{
    return task.scheduling.allowTimingFeedbackRouting && (task.scheduling.allowSameClassQueueRouting || task.scheduling.allowCrossClassTimingFeedbackRouting) && task.scheduling.overlapPreferred && !task.scheduling.avoidQueueCrossing;
}

// Same-class routing retains its independent physical-queue opt-in.
// Every route into another Vulkan family keeps the separate family opt-in.
// Cross-class timing is a stronger explicit opt-in and only uses command-compatible classes.
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

[[nodiscard]] const GpuPhysicalQueueInfo& FindTimingFeedbackIncumbent(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& staticQueue,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot
)noexcept{
    const GpuTaskTimingAssignmentState* const assignmentState = historySnapshot.findAssignment(key);
    if(!assignmentState || assignmentState->lastAcceptedQueue == staticQueue.id)
        return staticQueue;

    const GpuPhysicalQueueInfo* const acceptedQueue = FindPhysicalQueueInfo(topology, assignmentState->lastAcceptedQueue);
    if(!acceptedQueue || !IsLegalTimingFeedbackRoute(graph, topology, task, staticQueue, *acceptedQueue))
        return staticQueue;
    return *acceptedQueue;
}

[[nodiscard]] b8 HasUsableTimingFeedback(const GpuTaskGraphQueueAssignmentOptions& options, const u16 deviceGeneration)noexcept{
    // assignQueues() rejects an enabled invalid policy before this query.
    NWB_ASSERT(!options.timingFeedbackPolicy.enabled || options.timingFeedbackPolicy.valid());
    return options.timingFeedbackPolicy.enabled && options.timingHistory && options.timingHistory->valid() && options.timingHistory->deviceGeneration() == deviceGeneration;
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
    const GpuTaskTimingHistory* const incumbentHistory = historySnapshot.find(TimingHistoryKeyForQueue(key, incumbent.queueClass), incumbent.id);
    const GpuTaskTimingAssignmentState* const assignmentState = historySnapshot.findAssignment(key);
    if(!incumbentHistory)
        return nullptr;

    const GpuPhysicalQueueInfo* result = nullptr;
    const GpuTaskTimingHistory* resultHistory = nullptr;
    GpuQueueAssignmentScore resultScore;
    b8 hasResultScore = false;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(candidate.id == incumbent.id || !IsLegalTimingFeedbackRoute(graph, topology, task, incumbent, candidate))
            continue;

        const GpuTaskTimingHistory* const candidateHistory = historySnapshot.find(TimingHistoryKeyForQueue(key, candidate.queueClass), candidate.id);
        if(!candidateHistory)
            continue;

        // A fresh store may finish calibration with no probe as incumbent; the static route is then the baseline.
        const b8 canSwitch = assignmentState ? GpuTaskTimingFeedbackCanSwitch(*incumbentHistory, *candidateHistory, *assignmentState, incumbent.id, candidate.id, frameIndex, policy) : GpuTaskTimingHistoryMeetsMinimumSamples(*incumbentHistory, policy)
                && GpuTaskTimingHistoryMeetsMinimumSamples(*candidateHistory, policy)
                && GpuTaskTimingBenefitExceedsHysteresis(*incumbentHistory, *candidateHistory, policy)
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

        // Queue scores break exact duration ties; measured differences need no graph-scoring scan.
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

// Calibration is narrower than selection: visits only legal opted-in routes until each has enough samples. Incumbent keeps this frame.
[[nodiscard]] const GpuPhysicalQueueInfo* FindTimingFeedbackCalibrationQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskTimingFeedbackPolicy& policy,
    const u64 frameIndex
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
        if(candidate.id == incumbent.id || !IsLegalTimingFeedbackRoute(graph, topology, task, incumbent, candidate))
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

