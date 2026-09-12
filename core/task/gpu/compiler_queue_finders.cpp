// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Physical queues in one Vulkan family can exchange work through a timeline semaphore without a queue-family
// ownership transfer. Cross-family balancing is deliberately separate: opted-in tasks may use it, and resource
// planning below then emits the paired exclusive ownership handoff when required.
[[nodiscard]] const GpuPhysicalQueueInfo* FindLeastLoadedSameClassQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& baseQueue,
    const usize assignedPrefixCount,
    const bool allowCrossFamilyRouting,
    const bool preferNonPrimaryQueue
)noexcept{
    const GpuPhysicalQueueInfo* result = nullptr;
    u64 resultLoad = Limit<u64>::s_Max;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.queueClass != baseQueue.queueClass
            || (!allowCrossFamilyRouting && candidate.familyIndex != baseQueue.familyIndex)
            || !IsLegalQueueAssignmentCandidate(graph, topology, task, candidate)
        )
            continue;

        u64 load = 0u;
        for(usize assignmentIndex = 0u; assignmentIndex < assignedPrefixCount; ++assignmentIndex){
            const GpuTaskQueueAssignment& assignment = assignments[assignmentIndex];
            if(assignment.queue != candidate.id)
                continue;
            const u64 cost = scoringData.taskCosts[assignment.task.index];
            load = load > Limit<u64>::s_Max - cost ? Limit<u64>::s_Max : load + cost;
        }
        const bool candidateIsNonPrimary = candidate.id != baseQueue.id;
        const bool resultIsNonPrimary = result && result->id != baseQueue.id;
        bool winsEqualLoad = IsBetterQueue(candidate, result);
        if(preferNonPrimaryQueue && candidateIsNonPrimary != resultIsNonPrimary)
            winsEqualLoad = candidateIsNonPrimary;
        if(
            !result
            || load < resultLoad
            || (load == resultLoad && winsEqualLoad)
        ){
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
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueInfo& baseQueue,
    const usize assignedPrefixCount,
    const bool allowCrossFamilyRouting
)noexcept{
    const GpuPhysicalQueueInfo* result = nullptr;
    usize resultAssignmentIndex = 0u;
    const GpuTaskGraphSchedulingTaskIndexView producerIndices = analysis.schedulingProducers(task.id);
    for(usize producerIndex = 0u; producerIndex < producerIndices.taskCount; ++producerIndex){
        const u32 producerTaskIndex = producerIndices[producerIndex];
        if(producerTaskIndex >= assignmentIndicesByTask.size())
            continue;

        const u32 producerAssignmentIndex = assignmentIndicesByTask[producerTaskIndex];
        if(producerAssignmentIndex >= assignedPrefixCount || producerAssignmentIndex >= assignments.size())
            continue;
        const GpuTaskId producerTask{ producerTaskIndex, task.id.generation };
        const GpuTaskQueueAssignment& assignment = assignments[producerAssignmentIndex];
        if(assignment.task != producerTask)
            continue;
        const GpuPhysicalQueueInfo* const candidate = FindPhysicalQueueInfo(topology, assignment.queue);
        if(
            !candidate
            || candidate->queueClass != baseQueue.queueClass
            || (!allowCrossFamilyRouting && candidate->familyIndex != baseQueue.familyIndex)
            || !IsLegalQueueAssignmentCandidate(graph, topology, task, *candidate)
        )
            continue;
        if(!result || producerAssignmentIndex > resultAssignmentIndex){
            result = candidate;
            resultAssignmentIndex = producerAssignmentIndex;
        }
    }
    return result;
}

[[nodiscard]] bool AllowsTimingFeedbackRouting(const GpuTaskGraphTaskView& task)noexcept{
    return task.scheduling.allowTimingFeedbackRouting
        && (
            task.scheduling.allowSameClassQueueRouting
            || task.scheduling.allowCrossClassTimingFeedbackRouting
        )
        && task.scheduling.overlapPreferred
        && !task.scheduling.avoidQueueCrossing
    ;
}

// Same-class routing retains its independent physical-queue opt-in. Every route into another Vulkan family keeps
// the separate family opt-in. Cross-class timing is a stronger explicit opt-in and can only use classes already
// admitted by a flexible queue request; candidate validation still owns capability and resource-sharing checks.
[[nodiscard]] bool IsLegalTimingFeedbackRoute(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuPhysicalQueueInfo& candidate
)noexcept{
    if(!AllowsTimingFeedbackRouting(task) || !IsLegalQueueAssignmentCandidate(graph, topology, task, candidate))
        return false;
    if(
        candidate.familyIndex != incumbent.familyIndex
        && !task.scheduling.allowCrossFamilySameClassQueueRouting
    )
        return false;

    if(candidate.queueClass == incumbent.queueClass)
        return task.scheduling.allowSameClassQueueRouting;

    return task.scheduling.allowCrossClassTimingFeedbackRouting
        && (
            task.queue.preferredQueue == GpuQueuePreference::Any
            || (task.queue.allowFallback && task.queue.compilerMayOverridePreference)
        )
    ;
}

[[nodiscard]] GpuTaskTimingKey TimingHistoryKeyForQueue(
    const GpuTaskTimingAssignmentKey& assignmentKey,
    const CommandQueue::Enum queueClass
)noexcept{
    return GpuTaskTimingKey{
        .task = assignmentKey.task,
        .variant = assignmentKey.variant,
        .resolutionClass = assignmentKey.resolutionClass,
        .queue = queueClass,
    };
}

[[nodiscard]] const GpuPhysicalQueueInfo* FindTimingFeedbackIncumbent(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& staticQueue,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot
)noexcept{
    const GpuTaskTimingAssignmentState* const assignmentState = historySnapshot.findAssignment(key);
    if(!assignmentState || assignmentState->lastAcceptedQueue == staticQueue.id)
        return &staticQueue;

    const GpuPhysicalQueueInfo* const acceptedQueue = FindPhysicalQueueInfo(
        topology,
        assignmentState->lastAcceptedQueue
    );
    if(
        !acceptedQueue
        || !IsLegalTimingFeedbackRoute(graph, topology, task, staticQueue, *acceptedQueue)
    )
        return &staticQueue;
    return acceptedQueue;
}

[[nodiscard]] bool HasUsableTimingFeedback(
    const GpuTaskGraphQueueAssignmentOptions& options,
    const u16 deviceGeneration
)noexcept{
    return options.timingFeedbackPolicy.enabled
        && options.timingFeedbackPolicy.valid()
        && options.timingHistory
        && options.timingHistory->valid()
        && options.timingHistory->deviceGeneration() == deviceGeneration
    ;
}

[[nodiscard]] const GpuPhysicalQueueInfo* FindTimingFeedbackQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskTimingFeedbackPolicy& policy,
    const u64 frameIndex
)noexcept{
    const GpuTaskTimingHistory* const incumbentHistory = historySnapshot.find(
        TimingHistoryKeyForQueue(key, incumbent.queueClass),
        incumbent.id
    );
    const GpuTaskTimingAssignmentState* const assignmentState = historySnapshot.findAssignment(key);
    if(!incumbentHistory)
        return nullptr;

    const GpuPhysicalQueueInfo* result = nullptr;
    const GpuTaskTimingHistory* resultHistory = nullptr;
    GpuQueueAssignmentScore resultScore;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.id == incumbent.id
            || !IsLegalTimingFeedbackRoute(graph, topology, task, incumbent, candidate)
        )
            continue;

        const GpuTaskTimingHistory* const candidateHistory = historySnapshot.find(
            TimingHistoryKeyForQueue(key, candidate.queueClass),
            candidate.id
        );
        if(!candidateHistory)
            continue;

        // A fresh store can finish bounded calibration without committing any probe as an incumbent. In that
        // state the deterministic static route is the baseline and the first evidence-backed choice has no prior
        // switch whose dwell must elapse.
        const bool canSwitch = assignmentState
            ? GpuTaskTimingFeedbackCanSwitch(
                *incumbentHistory,
                *candidateHistory,
                *assignmentState,
                incumbent.id,
                candidate.id,
                frameIndex,
                policy
            )
            : GpuTaskTimingHistoryMeetsMinimumSamples(*incumbentHistory, policy)
                && GpuTaskTimingHistoryMeetsMinimumSamples(*candidateHistory, policy)
                && GpuTaskTimingBenefitExceedsHysteresis(*incumbentHistory, *candidateHistory, policy)
        ;
        if(!canSwitch)
            continue;

        const GpuQueueAssignmentScore candidateScore = BuildQueueAssignmentScore(
            graph,
            analysis,
            assignments,
            assignmentIndicesByTask,
            topology,
            schedulingReachability,
            scoringData,
            task,
            candidate
        );
        if(
            !result
            || candidateHistory->averageSeconds < resultHistory->averageSeconds
            || (
                candidateHistory->averageSeconds == resultHistory->averageSeconds
                && (
                    candidateScore.total() > resultScore.total()
                    || (
                        candidateScore.total() == resultScore.total()
                        && IsBetterQueue(candidate, result)
                    )
                )
            )
        ){
            result = &candidate;
            resultHistory = candidateHistory;
            resultScore = candidateScore;
        }
    }
    return result;
}

// Calibration is deliberately bounded and narrower than adaptive selection. It only visits already-legal opted-in
// routes until each has enough accepted samples, then ordinary hysteresis resumes. Returning the incumbent is
// meaningful: it reserves this frame for a baseline sample instead of switching on incomplete data.
[[nodiscard]] const GpuPhysicalQueueInfo* FindTimingFeedbackCalibrationQueue(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuTaskTimingAssignmentKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskTimingFeedbackPolicy& policy,
    const u64 frameIndex
)noexcept{
    if(
        policy.calibrationIntervalFrames == 0u
        || frameIndex % policy.calibrationIntervalFrames != 0u
        || !AllowsTimingFeedbackRouting(task)
    )
        return nullptr;

    const auto sampleCountFor = [&](const GpuPhysicalQueueInfo& queue){
        const GpuTaskTimingHistory* const history = historySnapshot.find(
            TimingHistoryKeyForQueue(key, queue.queueClass),
            queue.id
        );
        return history ? history->sampleCount : 0u;
    };

    const GpuPhysicalQueueInfo* result = &incumbent;
    u32 resultSampleCount = sampleCountFor(incumbent);
    bool needsCalibration = resultSampleCount < policy.minimumSampleCount;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.id == incumbent.id
            || !IsLegalTimingFeedbackRoute(graph, topology, task, incumbent, candidate)
        )
            continue;

        const u32 candidateSampleCount = sampleCountFor(candidate);
        if(candidateSampleCount >= policy.minimumSampleCount)
            continue;

        needsCalibration = true;
        if(
            candidateSampleCount < resultSampleCount
            || (
                candidateSampleCount == resultSampleCount
                && result != &incumbent
                && IsBetterQueue(candidate, result)
            )
        ){
            result = &candidate;
            resultSampleCount = candidateSampleCount;
        }
    }
    return needsCalibration ? result : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

