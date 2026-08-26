// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool IsBetterQueue(
    const GpuPhysicalQueueInfo& candidate,
    const GpuPhysicalQueueInfo* const current
)noexcept{
    if(!current)
        return true;
    if(candidate.id.index != current->id.index)
        return candidate.id.index < current->id.index;
    return candidate.queueClass < current->queueClass;
}

// Physical queues in one Vulkan family can exchange work through a timeline semaphore without a queue-family
// ownership transfer. Cross-family balancing is deliberately separate: opted-in tasks may use it, and resource
// planning below then emits the paired exclusive ownership handoff when required.
[[nodiscard]] static const GpuPhysicalQueueInfo* FindLeastLoadedSameClassQueue(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueInfo& baseQueue,
    const GpuQueueCapability::Mask requiredCapabilities,
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
            || !HasCapabilities(candidate.capabilities, requiredCapabilities)
        )
            continue;

        u64 load = 0u;
        for(const GpuTaskId assignedTask : analysis.topologicalOrder()){
            const GpuTaskQueueAssignment* assignment = nullptr;
            for(const GpuTaskQueueAssignment& existingAssignment : assignments){
                if(existingAssignment.task == assignedTask){
                    assignment = &existingAssignment;
                    break;
                }
            }
            if(!assignment || assignment->queue != candidate.id)
                continue;
            const u64 cost = QueueCostWeight(graph.taskAt(assignedTask.index).scheduling.cost);
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

[[nodiscard]] static const GpuPhysicalQueueInfo* FindDirectDependencySameClassQueue(
    const GpuTaskGraphTaskView& task,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueInfo& baseQueue,
    const GpuQueueCapability::Mask requiredCapabilities,
    const bool allowCrossFamilyRouting
)noexcept{
    for(usize dependencyIndex = task.dependencyCount; dependencyIndex > 0u; --dependencyIndex){
        const GpuTaskId dependency = task.dependencies[dependencyIndex - 1u];
        const GpuTaskQueueAssignment* assignment = nullptr;
        for(const GpuTaskQueueAssignment& candidateAssignment : assignments){
            if(candidateAssignment.task == dependency){
                assignment = &candidateAssignment;
                break;
            }
        }
        if(!assignment)
            continue;

        for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
            const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
            if(
                candidate.id != assignment->queue
                || candidate.queueClass != baseQueue.queueClass
                || (!allowCrossFamilyRouting && candidate.familyIndex != baseQueue.familyIndex)
                || !HasCapabilities(candidate.capabilities, requiredCapabilities)
            )
                continue;
            return &candidate;
        }
    }
    return nullptr;
}

[[nodiscard]] static const GpuPhysicalQueueInfo* FindPhysicalQueue(
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueId& queueID
)noexcept{
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(queue.id == queueID)
            return &queue;
    }
    return nullptr;
}

[[nodiscard]] static const GpuTaskQueueAssignment* FindQueueAssignment(
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskId& task
)noexcept{
    for(const GpuTaskQueueAssignment& assignment : assignments){
        if(assignment.task == task)
            return &assignment;
    }
    return nullptr;
}

[[nodiscard]] static bool AllowsTimingFeedbackRouting(const GpuTaskGraphTaskView& task)noexcept{
    return task.scheduling.allowTimingFeedbackRouting
        && task.scheduling.allowSameClassQueueRouting
        && task.scheduling.overlapPreferred
        && !task.scheduling.avoidQueueCrossing
    ;
}

// Adaptive routing remains inside the selected queue class. Same-family candidates are always eligible; another
// Vulkan family additionally needs the task's explicit cross-family opt-in, after which resource planning owns the
// required exclusive release/acquire pair or rejects an incompatible concurrent-sharing declaration.
[[nodiscard]] static bool IsLegalTimingFeedbackRoute(
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuPhysicalQueueInfo& candidate
)noexcept{
    return AllowsTimingFeedbackRouting(task)
        && candidate.queueClass == incumbent.queueClass
        && (
            candidate.familyIndex == incumbent.familyIndex
            || task.scheduling.allowCrossFamilySameClassQueueRouting
        )
        && HasCapabilities(candidate.capabilities, task.queue.requiredCapabilities)
    ;
}

[[nodiscard]] static bool HasUsableTimingFeedback(
    const GpuTaskGraphQueueAssignmentOptions& options,
    const u16 deviceGeneration
)noexcept{
    return options.timingFeedbackPolicy
        && options.timingFeedbackPolicy->enabled
        && options.timingFeedbackPolicy->valid()
        && options.timingHistory
        && options.timingHistory->valid()
        && options.timingHistory->deviceGeneration() == deviceGeneration
    ;
}

[[nodiscard]] static i32 SaturateQueueScoreTerm(const u64 value)noexcept{
    return value > static_cast<u64>(Limit<i32>::s_Max)
        ? Limit<i32>::s_Max
        : static_cast<i32>(value)
    ;
}

// Timing history decides whether a switch is worthwhile. This lightweight score only gives deterministic meaning to
// equal-duration candidates: lower existing graph load and fewer already-known incoming queue crossings win, with
// the physical queue ID remaining the final tie-breaker.
[[nodiscard]] static GpuQueueAssignmentScore BuildTimingFeedbackScore(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuPhysicalQueueInfo& candidate
)noexcept{
    GpuQueueAssignmentScore score;
    score.preference = candidate.id == incumbent.id ? 1 : 0;
    score.overlap = candidate.id != incumbent.id && task.scheduling.overlapPreferred ? 1 : 0;

    u64 queueLoad = 0u;
    for(const GpuTaskQueueAssignment& assignment : assignments){
        if(assignment.queue != candidate.id)
            continue;

        const u64 cost = QueueCostWeight(graph.taskAt(assignment.task.index).scheduling.cost);
        queueLoad = queueLoad > Limit<u64>::s_Max - cost ? Limit<u64>::s_Max : queueLoad + cost;
    }
    score.queueLoad = SaturateQueueScoreTerm(queueLoad);

    u64 incomingCrossings = 0u;
    for(const GpuTaskDependencyEdge& edge : analysis.schedulingEdges()){
        if(edge.consumer != task.id)
            continue;

        const GpuTaskQueueAssignment* const producerAssignment = FindQueueAssignment(assignments, edge.producer);
        if(producerAssignment && producerAssignment->queue != candidate.id)
            ++incomingCrossings;
    }
    score.incomingCrossings = SaturateQueueScoreTerm(incomingCrossings);
    // Future consumer assignments are not yet immutable. Exact ownership costs require the complete plan, so both
    // terms remain outside this intentionally local tie-breaker; resource planning stays authoritative.
    score.outgoingCrossings = 0;
    score.ownershipTransfers = 0;
    return score;
}

[[nodiscard]] static const GpuPhysicalQueueInfo* FindTimingFeedbackQueue(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuTaskTimingKey& key,
    const GpuTaskTimingHistorySnapshot& historySnapshot,
    const GpuTaskTimingFeedbackPolicy& policy,
    const u64 frameIndex
)noexcept{
    const GpuTaskTimingHistory* const incumbentHistory = historySnapshot.find(key, incumbent.id);
    const GpuTaskTimingAssignmentState* const assignmentState = historySnapshot.findAssignment(key);
    if(!incumbentHistory || !assignmentState)
        return nullptr;

    const GpuPhysicalQueueInfo* result = nullptr;
    const GpuTaskTimingHistory* resultHistory = nullptr;
    GpuQueueAssignmentScore resultScore;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(candidate.id == incumbent.id || !IsLegalTimingFeedbackRoute(task, incumbent, candidate))
            continue;

        const GpuTaskTimingHistory* const candidateHistory = historySnapshot.find(key, candidate.id);
        if(
            !candidateHistory
            || !GpuTaskTimingFeedbackCanSwitch(
                *incumbentHistory,
                *candidateHistory,
                *assignmentState,
                incumbent.id,
                candidate.id,
                frameIndex,
                policy
            )
        )
            continue;

        const GpuQueueAssignmentScore candidateScore = BuildTimingFeedbackScore(
            graph,
            analysis,
            assignments,
            task,
            incumbent,
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

// Calibration is deliberately bounded and narrower than adaptive selection. It only visits already-legal
// same-class routes until each has enough accepted samples, then ordinary hysteresis resumes. Returning the
// incumbent is meaningful: it reserves this frame for a baseline sample instead of switching on incomplete data.
[[nodiscard]] static const GpuPhysicalQueueInfo* FindTimingFeedbackCalibrationQueue(
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& incumbent,
    const GpuTaskTimingKey& key,
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
        const GpuTaskTimingHistory* const history = historySnapshot.find(key, queue.id);
        return history ? history->sampleCount : 0u;
    };

    const GpuPhysicalQueueInfo* result = &incumbent;
    u32 resultSampleCount = sampleCountFor(incumbent);
    bool needsCalibration = resultSampleCount < policy.minimumSampleCount;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(candidate.id == incumbent.id || !IsLegalTimingFeedbackRoute(task, incumbent, candidate))
            continue;

        const u32 candidateSampleCount = sampleCountFor(candidate);
        if(candidateSampleCount >= policy.minimumSampleCount)
            continue;

        needsCalibration = true;
        if(
            candidateSampleCount < resultSampleCount
            || (
                candidateSampleCount == resultSampleCount
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


void GpuTaskGraphQueueAssignments::reset(){
    m_assignments.clear();
    m_diagnostic = GpuTaskQueueAssignmentDiagnostic{};
    m_generation = 0u;
    m_declarationRevision = 0u;
    m_taskCount = 0u;
    m_valid = false;
}

bool GpuTaskGraphQueueAssignments::validFor(const GpuTaskGraph& graph)const noexcept{
    return m_valid
        && m_generation == graph.generation()
        && m_declarationRevision == graph.declarationRevision()
        && m_taskCount == graph.taskCount()
    ;
}

const GpuTaskQueueAssignment* GpuTaskGraphQueueAssignments::find(const GpuTaskId& task)const noexcept{
    if(!m_valid || task.generation != m_generation)
        return nullptr;

    for(const GpuTaskQueueAssignment& assignment : m_assignments){
        if(assignment.task == task)
            return &assignment;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraphCompiler::assignQueues(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueTopology& topology,
    GpuTaskGraphQueueAssignments& outAssignments,
    const GpuTaskGraphQueueAssignmentOptions& options
)const{
    using namespace GpuTaskGraphCompilerDetail;

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

    if(
        options.timingFeedbackPolicy
        && options.timingFeedbackPolicy->enabled
        && !options.timingFeedbackPolicy->valid()
    )
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
    outAssignments.m_assignments.reserve(graph.taskCount());

    for(const GpuTaskId taskID : analysis.topologicalOrder()){
        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        const GpuPhysicalQueueInfo* const graphicsQueue = FindBestCompatibleQueue(
            topology,
            task.queue.requiredCapabilities,
            CommandQueue::Graphics
        );
        const GpuPhysicalQueueInfo* const computeQueue = FindBestCompatibleQueue(
            topology,
            task.queue.requiredCapabilities,
            CommandQueue::Compute
        );
        const GpuPhysicalQueueInfo* const transferQueue = FindBestCompatibleQueue(
            topology,
            task.queue.requiredCapabilities,
            CommandQueue::Transfer
        );
        const GpuPhysicalQueueInfo* const fallbackQueue = FindBestCompatibleQueue(
            topology,
            task.queue.requiredCapabilities
        );

        const GpuPhysicalQueueInfo* selectedQueue = nullptr;
        GpuTaskQueueAssignmentReason::Enum reason = GpuTaskQueueAssignmentReason::Unknown;
        if(RequiresGraphics(task.queue.requiredCapabilities)){
            // A task that declares Graphics must stay on the physical Graphics transport even if a malformed future
            // topology happens to advertise Graphics capability on another queue class.
            selectedQueue = graphicsQueue;
            reason = GpuTaskQueueAssignmentReason::RequiredGraphics;
        }
        else{
            switch(task.queue.preferredQueue){
            case GpuQueuePreference::Graphics:
                selectedQueue = graphicsQueue;
                reason = GpuTaskQueueAssignmentReason::PreferredQueue;
                if(!selectedQueue && task.queue.allowFallback){
                    selectedQueue = fallbackQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                break;
            case GpuQueuePreference::Compute:
                if(
                    computeQueue
                    && (
                        !task.queue.compilerMayOverridePreference
                        || (computeQueue->dedicated && ShouldUseDedicatedCompute(task.scheduling))
                    )
                ){
                    selectedQueue = computeQueue;
                    reason = computeQueue->dedicated
                        ? GpuTaskQueueAssignmentReason::DedicatedCompute
                        : GpuTaskQueueAssignmentReason::PreferredQueue
                    ;
                }
                else if(task.queue.allowFallback && graphicsQueue){
                    selectedQueue = graphicsQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                else if(computeQueue){
                    selectedQueue = computeQueue;
                    reason = GpuTaskQueueAssignmentReason::PreferredQueue;
                }
                else if(task.queue.allowFallback){
                    selectedQueue = fallbackQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                break;
            case GpuQueuePreference::Transfer:
                if(
                    transferQueue
                    && (
                        !task.queue.compilerMayOverridePreference
                        || (transferQueue->dedicated && ShouldUseDedicatedTransfer(task.scheduling))
                    )
                ){
                    selectedQueue = transferQueue;
                    reason = transferQueue->dedicated
                        ? GpuTaskQueueAssignmentReason::DedicatedTransfer
                        : GpuTaskQueueAssignmentReason::PreferredQueue
                    ;
                }
                else if(task.queue.allowFallback && computeQueue && computeQueue->dedicated && ShouldUseDedicatedCompute(task.scheduling)){
                    selectedQueue = computeQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                else if(task.queue.allowFallback && graphicsQueue){
                    selectedQueue = graphicsQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                else if(transferQueue){
                    selectedQueue = transferQueue;
                    reason = GpuTaskQueueAssignmentReason::PreferredQueue;
                }
                else if(task.queue.allowFallback && computeQueue){
                    selectedQueue = computeQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                else if(task.queue.allowFallback){
                    selectedQueue = fallbackQueue;
                    reason = GpuTaskQueueAssignmentReason::Fallback;
                }
                break;
            case GpuQueuePreference::Any:
                // Keep the initial Any policy conservative and stable. Packet/frontier scoring arrives after this
                // observational assignment has proven parity against the current renderer schedule.
                selectedQueue = graphicsQueue ? graphicsQueue : fallbackQueue;
                reason = GpuTaskQueueAssignmentReason::ConservativeAny;
                break;
            default:
                NWB_ASSERT(false);
                break;
            }
        }
        if(!selectedQueue){
            return fail(
                GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue,
                task.id,
                task.queue.requiredCapabilities
            );
        }

        const GpuPhysicalQueueInfo* const initiallySelectedQueue = selectedQueue;
        if(
            task.scheduling.allowSameClassQueueRouting
            && task.scheduling.overlapPreferred
            && !task.scheduling.avoidQueueCrossing
        ){
            const GpuPhysicalQueueInfo* const dependencyQueue =
                task.scheduling.preserveSameClassQueueWithDirectDependency
                    ? FindDirectDependencySameClassQueue(
                        task,
                        outAssignments.m_assignments,
                        topology,
                        *selectedQueue,
                        task.queue.requiredCapabilities,
                        task.scheduling.allowCrossFamilySameClassQueueRouting
                    )
                    : nullptr
            ;
            if(dependencyQueue){
                selectedQueue = dependencyQueue;
            }
            else if(const GpuPhysicalQueueInfo* const balancedQueue = FindLeastLoadedSameClassQueue(
                graph,
                analysis,
                outAssignments.m_assignments,
                topology,
                *selectedQueue,
                task.queue.requiredCapabilities,
                task.scheduling.allowCrossFamilySameClassQueueRouting,
                task.scheduling.preferNonPrimarySameClassQueue
            ))
                selectedQueue = balancedQueue;
        }
        if(selectedQueue != initiallySelectedQueue)
            reason = GpuTaskQueueAssignmentReason::SameClassRouting;

        const GpuTaskTimingKey timingKey{
            .task = task.identity,
            .variant = task.timing.variant,
            .resolutionClass = task.timing.resolutionClass,
            .queue = selectedQueue->queueClass,
        };
        const GpuTaskTimingQueueOverride* const timingOverride = FindGpuTaskTimingQueueOverride(
            options.timingQueueOverrides,
            options.timingQueueOverrideCount,
            timingKey
        );
        if(timingOverride){
            const GpuPhysicalQueueInfo* const forcedQueue = FindPhysicalQueue(topology, timingOverride->queue);
            if(!forcedQueue || !IsLegalTimingFeedbackRoute(task, *selectedQueue, *forcedQueue)){
                return fail(
                    GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback,
                    task.id,
                    task.queue.requiredCapabilities
                );
            }
            if(forcedQueue != selectedQueue){
                selectedQueue = forcedQueue;
                reason = GpuTaskQueueAssignmentReason::SameClassRouting;
            }
        }
        else if(HasUsableTimingFeedback(options, topology.queues[0u].id.deviceGeneration)){
            const GpuPhysicalQueueInfo* const calibrationQueue = FindTimingFeedbackCalibrationQueue(
                topology,
                task,
                *selectedQueue,
                timingKey,
                *options.timingHistory,
                *options.timingFeedbackPolicy,
                options.timingFrameIndex
            );
            if(calibrationQueue){
                if(calibrationQueue != selectedQueue){
                    selectedQueue = calibrationQueue;
                    reason = GpuTaskQueueAssignmentReason::SameClassRouting;
                }
            }
            else{
                const GpuPhysicalQueueInfo* const timingQueue = FindTimingFeedbackQueue(
                    graph,
                    analysis,
                    outAssignments.m_assignments,
                    topology,
                    task,
                    *selectedQueue,
                    timingKey,
                    *options.timingHistory,
                    *options.timingFeedbackPolicy,
                    options.timingFrameIndex
                );
                if(timingQueue){
                    selectedQueue = timingQueue;
                    reason = GpuTaskQueueAssignmentReason::SameClassRouting;
                }
            }
        }

        outAssignments.m_assignments.push_back(GpuTaskQueueAssignment{
            .task = task.id,
            .queue = selectedQueue->id,
            .queueClass = selectedQueue->queueClass,
            .reason = reason,
            .dedicated = selectedQueue->dedicated,
        });
    }

    outAssignments.m_diagnostic.status = GpuTaskGraphQueueAssignmentStatus::Success;
    outAssignments.m_valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

