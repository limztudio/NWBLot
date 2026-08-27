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
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
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
            const u64 cost = QueueCostWeight(graph.taskAt(assignment.task.index).scheduling.cost);
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
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphTaskView& task,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskGraphQueueTopology& topology,
    const GpuPhysicalQueueInfo& baseQueue,
    const usize assignedPrefixCount,
    const bool allowCrossFamilyRouting
)noexcept{
    const GpuPhysicalQueueInfo* result = nullptr;
    for(usize assignmentIndex = 0u; assignmentIndex < assignedPrefixCount; ++assignmentIndex){
        const GpuTaskQueueAssignment& assignment = assignments[assignmentIndex];
        bool directDependency = false;
        for(const GpuTaskDependencyEdge& edge : analysis.schedulingEdges()){
            if(edge.producer == assignment.task && edge.consumer == task.id){
                directDependency = true;
                break;
            }
        }
        if(!directDependency)
            continue;

        const GpuPhysicalQueueInfo* const candidate = FindPhysicalQueueInfo(topology, assignment.queue);
        if(
            !candidate
            || candidate->queueClass != baseQueue.queueClass
            || (!allowCrossFamilyRouting && candidate->familyIndex != baseQueue.familyIndex)
            || !IsLegalQueueAssignmentCandidate(graph, topology, task, *candidate)
        )
            continue;
        result = candidate;
    }
    return result;
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
    const GpuTaskGraph& graph,
    const GpuTaskGraphQueueTopology& topology,
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
        && IsLegalQueueAssignmentCandidate(graph, topology, task, candidate)
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

[[nodiscard]] static const GpuPhysicalQueueInfo* FindTimingFeedbackQueue(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskGraphQueueTopology& topology,
    const Vector<u8, Alloc::ScratchArena>& schedulingReachability,
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
        if(
            candidate.id == incumbent.id
            || !IsLegalTimingFeedbackRoute(graph, topology, task, incumbent, candidate)
        )
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

        const GpuQueueAssignmentScore candidateScore = BuildQueueAssignmentScore(
            graph,
            analysis,
            assignments,
            topology,
            schedulingReachability,
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

// Calibration is deliberately bounded and narrower than adaptive selection. It only visits already-legal
// same-class routes until each has enough accepted samples, then ordinary hysteresis resumes. Returning the
// incumbent is meaningful: it reserves this frame for a baseline sample instead of switching on incomplete data.
[[nodiscard]] static const GpuPhysicalQueueInfo* FindTimingFeedbackCalibrationQueue(
    const GpuTaskGraph& graph,
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
    m_compiledPlanGeneration = 0u;
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

bool GpuTaskGraphQueueAssignments::validFor(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph
)const noexcept{
    return validFor(graph)
        && compiledGraph.validFor(graph)
        && m_compiledPlanGeneration != 0u
        && m_compiledPlanGeneration == compiledGraph.planGeneration()
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
    Alloc::ScratchArena& scratchArena,
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

    Vector<u8, Alloc::ScratchArena> schedulingReachability(scratchArena);
    if(!BuildGpuTaskSchedulingReachability(analysis, graph.taskCount(), schedulingReachability))
        return fail(GpuTaskGraphQueueAssignmentStatus::InvalidGraphAnalysis);

    // Establish a legal route for every task before scoring. Outgoing crossings and ownership costs must see a
    // complete provisional plan instead of treating later consumers as if they did not exist.
    for(const GpuTaskId taskID : analysis.topologicalOrder()){
        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        const GpuPhysicalQueueInfo* const graphicsQueue = FindBestLegalQueueAssignmentCandidate(
            graph,
            topology,
            task,
            CommandQueue::Graphics
        );
        const GpuPhysicalQueueInfo* const computeQueue = FindBestLegalQueueAssignmentCandidate(
            graph,
            topology,
            task,
            CommandQueue::Compute
        );
        const GpuPhysicalQueueInfo* const transferQueue = FindBestLegalQueueAssignmentCandidate(
            graph,
            topology,
            task,
            CommandQueue::Transfer
        );
        const GpuPhysicalQueueInfo* const fallbackQueue = FindBestLegalQueueAssignmentCandidate(graph, topology, task);

        const GpuPhysicalQueueInfo* preferredQueue = nullptr;
        switch(task.queue.preferredQueue){
        case GpuQueuePreference::Graphics: preferredQueue = graphicsQueue; break;
        case GpuQueuePreference::Compute: preferredQueue = computeQueue; break;
        case GpuQueuePreference::Transfer: preferredQueue = transferQueue; break;
        case GpuQueuePreference::Any: preferredQueue = nullptr; break;
        default: NWB_ASSERT(false); break;
        }

        const bool hasConcretePreference = task.queue.preferredQueue != GpuQueuePreference::Any;
        const bool strictConcreteClass = hasConcretePreference
            && (!task.queue.compilerMayOverridePreference || !task.queue.allowFallback)
        ;
        const GpuPhysicalQueueInfo* selectedQueue = nullptr;
        GpuTaskQueueAssignmentReason::Enum reason = GpuTaskQueueAssignmentReason::Unknown;
        if(hasConcretePreference && !preferredQueue){
            if(task.queue.allowFallback){
                selectedQueue = fallbackQueue;
                reason = GpuTaskQueueAssignmentReason::Fallback;
            }
        }
        else if(RequiresGraphics(task.queue.requiredCapabilities)){
            selectedQueue = hasConcretePreference ? preferredQueue : fallbackQueue;
            if(!hasConcretePreference || (selectedQueue && selectedQueue->queueClass == CommandQueue::Graphics))
                reason = GpuTaskQueueAssignmentReason::RequiredGraphics;
            else if(selectedQueue && selectedQueue->queueClass == CommandQueue::Compute && selectedQueue->dedicated)
                reason = GpuTaskQueueAssignmentReason::DedicatedCompute;
            else if(selectedQueue && selectedQueue->queueClass == CommandQueue::Transfer && selectedQueue->dedicated)
                reason = GpuTaskQueueAssignmentReason::DedicatedTransfer;
            else if(selectedQueue)
                reason = GpuTaskQueueAssignmentReason::PreferredQueue;
        }
        else if(strictConcreteClass && preferredQueue){
            selectedQueue = preferredQueue;
            if(selectedQueue && selectedQueue->queueClass == CommandQueue::Compute && selectedQueue->dedicated)
                reason = GpuTaskQueueAssignmentReason::DedicatedCompute;
            else if(selectedQueue && selectedQueue->queueClass == CommandQueue::Transfer && selectedQueue->dedicated)
                reason = GpuTaskQueueAssignmentReason::DedicatedTransfer;
            else if(selectedQueue)
                reason = GpuTaskQueueAssignmentReason::PreferredQueue;
        }
        else if(task.queue.preferredQueue == GpuQueuePreference::Any){
            selectedQueue = fallbackQueue;
            reason = GpuTaskQueueAssignmentReason::ScoredAny;
        }
        else{
            switch(task.queue.preferredQueue){
            case GpuQueuePreference::Graphics:
                selectedQueue = graphicsQueue;
                reason = GpuTaskQueueAssignmentReason::PreferredQueue;
                break;
            case GpuQueuePreference::Compute:
                selectedQueue = graphicsQueue ? graphicsQueue : computeQueue;
                reason = graphicsQueue
                    ? GpuTaskQueueAssignmentReason::CompilerOverride
                    : computeQueue->dedicated
                        ? GpuTaskQueueAssignmentReason::DedicatedCompute
                        : GpuTaskQueueAssignmentReason::PreferredQueue
                ;
                break;
            case GpuQueuePreference::Transfer:
                if(transferQueue->dedicated && ShouldUseDedicatedTransfer(task.scheduling)){
                    selectedQueue = transferQueue;
                    reason = GpuTaskQueueAssignmentReason::DedicatedTransfer;
                }
                else if(computeQueue && computeQueue->dedicated && ShouldUseDedicatedCompute(task.scheduling)){
                    selectedQueue = computeQueue;
                    reason = GpuTaskQueueAssignmentReason::CompilerOverride;
                }
                else if(graphicsQueue){
                    selectedQueue = graphicsQueue;
                    reason = GpuTaskQueueAssignmentReason::CompilerOverride;
                }
                else if(computeQueue){
                    selectedQueue = computeQueue;
                    reason = GpuTaskQueueAssignmentReason::CompilerOverride;
                }
                else{
                    selectedQueue = transferQueue;
                    reason = GpuTaskQueueAssignmentReason::PreferredQueue;
                }
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

        outAssignments.m_assignments.push_back(GpuTaskQueueAssignment{
            .task = task.id,
            .initialQueue = selectedQueue->id,
            .queue = selectedQueue->id,
            .score = {},
            .queueClass = selectedQueue->queueClass,
            .reason = reason,
            .modifiers = GpuTaskQueueAssignmentModifier::None,
            .dedicated = selectedQueue->dedicated,
        });
    }

    // Evaluate movable Compute and Any tasks against the same complete provisional plan, then publish their class
    // decisions together. This keeps topology iteration order and partially-updated future routes out of scoring.
    Vector<GpuPhysicalQueueId, Alloc::ScratchArena> scoredQueues(outAssignments.m_assignments.size(), scratchArena);
    Vector<GpuTaskQueueAssignmentReason::Enum, Alloc::ScratchArena> scoredReasons(outAssignments.m_assignments.size(), scratchArena);
    for(usize assignmentIndex = 0u; assignmentIndex < outAssignments.m_assignments.size(); ++assignmentIndex){
        const GpuTaskQueueAssignment& assignment = outAssignments.m_assignments[assignmentIndex];
        const GpuTaskGraphTaskView task = graph.taskAt(assignment.task.index);
        scoredQueues[assignmentIndex] = assignment.queue;
        scoredReasons[assignmentIndex] = assignment.reason;

        if(task.queue.preferredQueue == GpuQueuePreference::Any){
            const GpuPhysicalQueueInfo* selectedQueue = nullptr;
            GpuQueueAssignmentScore selectedScore;
            for(u8 queueClassValue = 0u; queueClassValue < CommandQueue::kCount; ++queueClassValue){
                const GpuPhysicalQueueInfo* const candidate = FindBestLegalQueueAssignmentCandidate(
                    graph,
                    topology,
                    task,
                    static_cast<CommandQueue::Enum>(queueClassValue)
                );
                if(!candidate)
                    continue;

                const GpuQueueAssignmentScore candidateScore = BuildQueueAssignmentScore(
                    graph,
                    analysis,
                    outAssignments.m_assignments,
                    topology,
                    schedulingReachability,
                    task,
                    *candidate
                );
                if(IsBetterAnyQueueAssignmentCandidate(candidateScore, *candidate, selectedScore, selectedQueue)){
                    selectedQueue = candidate;
                    selectedScore = candidateScore;
                }
            }
            NWB_ASSERT(selectedQueue);
            scoredQueues[assignmentIndex] = selectedQueue->id;
            scoredReasons[assignmentIndex] = GpuTaskQueueAssignmentReason::ScoredAny;
            continue;
        }

        if(
            task.queue.preferredQueue != GpuQueuePreference::Compute
            || !task.queue.compilerMayOverridePreference
            || !task.queue.allowFallback
            || RequiresGraphics(task.queue.requiredCapabilities)
        )
            continue;

        const GpuPhysicalQueueInfo* const computeQueue = FindBestLegalQueueAssignmentCandidate(
            graph,
            topology,
            task,
            CommandQueue::Compute
        );
        const GpuPhysicalQueueInfo* const dedicatedComputeQueue = FindBestLegalQueueAssignmentCandidate(
            graph,
            topology,
            task,
            CommandQueue::Compute,
            true
        );
        const GpuPhysicalQueueInfo* const graphicsQueue = FindBestLegalQueueAssignmentCandidate(
            graph,
            topology,
            task,
            CommandQueue::Graphics
        );
        if(!computeQueue || !graphicsQueue)
            continue;

        scoredQueues[assignmentIndex] = graphicsQueue->id;
        scoredReasons[assignmentIndex] = GpuTaskQueueAssignmentReason::CompilerOverride;
        if(
            !dedicatedComputeQueue
            || !ShouldUseDedicatedCompute(task.scheduling)
            || !HasTransitivelyIndependentRequiredGraphicsTask(graph, analysis, schedulingReachability, task)
        )
            continue;

        const GpuQueueAssignmentScore computeScore = BuildQueueAssignmentScore(
            graph,
            analysis,
            outAssignments.m_assignments,
            topology,
            schedulingReachability,
            task,
            *dedicatedComputeQueue
        );
        const GpuQueueAssignmentScore graphicsScore = BuildQueueAssignmentScore(
            graph,
            analysis,
            outAssignments.m_assignments,
            topology,
            schedulingReachability,
            task,
            *graphicsQueue
        );
        if(computeScore.total() > graphicsScore.total()){
            scoredQueues[assignmentIndex] = dedicatedComputeQueue->id;
            scoredReasons[assignmentIndex] = GpuTaskQueueAssignmentReason::DedicatedCompute;
        }
    }

    for(usize assignmentIndex = 0u; assignmentIndex < outAssignments.m_assignments.size(); ++assignmentIndex){
        GpuTaskQueueAssignment& assignment = outAssignments.m_assignments[assignmentIndex];
        const GpuPhysicalQueueInfo* const selectedQueue = FindPhysicalQueueInfo(topology, scoredQueues[assignmentIndex]);
        NWB_ASSERT(selectedQueue);
        assignment.initialQueue = selectedQueue->id;
        assignment.queue = selectedQueue->id;
        assignment.queueClass = selectedQueue->queueClass;
        assignment.reason = scoredReasons[assignmentIndex];
        assignment.dedicated = selectedQueue->dedicated;
    }

    for(usize assignmentIndex = 0u; assignmentIndex < outAssignments.m_assignments.size(); ++assignmentIndex){
        GpuTaskQueueAssignment& assignment = outAssignments.m_assignments[assignmentIndex];
        const GpuTaskGraphTaskView task = graph.taskAt(assignment.task.index);
        const GpuPhysicalQueueInfo* selectedQueue = FindPhysicalQueueInfo(topology, assignment.queue);
        NWB_ASSERT(selectedQueue);

        if(
            task.scheduling.allowSameClassQueueRouting
            && task.scheduling.overlapPreferred
            && !task.scheduling.avoidQueueCrossing
        ){
            const GpuPhysicalQueueInfo* const dependencyQueue = task.scheduling.preserveSameClassQueueWithDirectDependency
                ? FindDirectDependencySameClassQueue(
                    graph,
                    analysis,
                    task,
                    outAssignments.m_assignments,
                    topology,
                    *selectedQueue,
                    assignmentIndex,
                    task.scheduling.allowCrossFamilySameClassQueueRouting
                )
                : nullptr
            ;
            if(dependencyQueue){
                if(dependencyQueue != selectedQueue)
                    assignment.modifiers |= GpuTaskQueueAssignmentModifier::DirectDependencyAffinity;
                selectedQueue = dependencyQueue;
            }
            else if(const GpuPhysicalQueueInfo* const balancedQueue = FindLeastLoadedSameClassQueue(
                graph,
                outAssignments.m_assignments,
                topology,
                task,
                *selectedQueue,
                assignmentIndex,
                task.scheduling.allowCrossFamilySameClassQueueRouting,
                task.scheduling.preferNonPrimarySameClassQueue
            )){
                if(balancedQueue != selectedQueue){
                    assignment.modifiers |= GpuTaskQueueAssignmentModifier::SameClassLoadBalance;
                    if(task.scheduling.preferNonPrimarySameClassQueue)
                        assignment.modifiers |= GpuTaskQueueAssignmentModifier::NonPrimaryPreference;
                }
                selectedQueue = balancedQueue;
            }
        }

        assignment.queue = selectedQueue->id;
        assignment.queueClass = selectedQueue->queueClass;
        assignment.dedicated = selectedQueue->dedicated;
        assignment.initialQueue = selectedQueue->id;
    }

    for(GpuTaskQueueAssignment& assignment : outAssignments.m_assignments){
        const GpuTaskGraphTaskView task = graph.taskAt(assignment.task.index);
        const GpuPhysicalQueueInfo* selectedQueue = FindPhysicalQueueInfo(topology, assignment.queue);
        NWB_ASSERT(selectedQueue);

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
            const GpuPhysicalQueueInfo* const forcedQueue = FindPhysicalQueueInfo(topology, timingOverride->queue);
            if(
                !forcedQueue
                || !IsLegalTimingFeedbackRoute(graph, topology, task, *selectedQueue, *forcedQueue)
            ){
                return fail(
                    GpuTaskGraphQueueAssignmentStatus::InvalidTimingFeedback,
                    task.id,
                    task.queue.requiredCapabilities
                );
            }
            selectedQueue = forcedQueue;
            assignment.modifiers |= GpuTaskQueueAssignmentModifier::DebugTimingOverride;
        }
        else if(HasUsableTimingFeedback(options, topology.queues[0u].id.deviceGeneration)){
            const GpuPhysicalQueueInfo* const calibrationQueue = FindTimingFeedbackCalibrationQueue(
                graph,
                topology,
                task,
                *selectedQueue,
                timingKey,
                *options.timingHistory,
                *options.timingFeedbackPolicy,
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
                topology,
                schedulingReachability,
                task,
                *selectedQueue,
                timingKey,
                *options.timingHistory,
                *options.timingFeedbackPolicy,
                options.timingFrameIndex
            )){
                selectedQueue = timingQueue;
                assignment.modifiers |= GpuTaskQueueAssignmentModifier::TimingFeedback;
            }
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
            topology,
            schedulingReachability,
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

