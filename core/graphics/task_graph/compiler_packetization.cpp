// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_compiler_packetization{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PacketMergeSummary{
    Name uniformFrontierScoredMergeDomain;
    bool allTasksAllowMerge = false;
    bool hasCrossQueueConsumerFrontier = false;
};

[[nodiscard]] bool TaskAllowsPacketMerge(const GpuTaskGraphTaskView& task)noexcept{
    return task.scheduling.allowPacketMerge
        && !task.scheduling.forceSubmissionBoundary
        && !task.scheduling.joinsAcceptedQueueFrontier
    ;
}

[[nodiscard]] bool SchedulingConsumersContain(
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskId& producer,
    const GpuTaskId& consumer
)noexcept{
    const GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(producer);
    for(usize consumerIndex = 0u; consumerIndex < consumers.taskCount; ++consumerIndex){
        if(consumers[consumerIndex] == consumer.index)
            return true;
    }
    return false;
}

[[nodiscard]] bool HasCrossQueueConsumerFrontier(
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueAssignments& assignments,
    const GpuTaskId& task,
    const GpuPhysicalQueueId& packetQueue
)noexcept{
    const GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(task);
    for(usize consumerIndex = 0u; consumerIndex < consumers.taskCount; ++consumerIndex){
        const GpuTaskId consumer{ consumers[consumerIndex], task.generation };
        const GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumer);
        if(
            !consumerAssignment
            || !consumerAssignment->queue.valid()
            || consumerAssignment->queue != packetQueue
        )
            return true;
    }
    return false;
}

static void InitializePacketMergeSummary(
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueId& packetQueue,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueAssignments& assignments,
    const bool trackConsumerFrontiers,
    PacketMergeSummary& outSummary
){
    outSummary.uniformFrontierScoredMergeDomain = task.scheduling.frontierScoredMergeDomain;
    outSummary.allTasksAllowMerge = TaskAllowsPacketMerge(task);
    outSummary.hasCrossQueueConsumerFrontier = trackConsumerFrontiers
        && HasCrossQueueConsumerFrontier(analysis, assignments, task.id, packetQueue)
    ;
}

static void AccumulatePacketMergeSummary(
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueId& packetQueue,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueAssignments& assignments,
    const bool trackConsumerFrontiers,
    PacketMergeSummary& inOutSummary
){
    inOutSummary.allTasksAllowMerge = inOutSummary.allTasksAllowMerge && TaskAllowsPacketMerge(task);
    if(
        !inOutSummary.uniformFrontierScoredMergeDomain
        || !task.scheduling.frontierScoredMergeDomain
        || inOutSummary.uniformFrontierScoredMergeDomain != task.scheduling.frontierScoredMergeDomain
    )
        inOutSummary.uniformFrontierScoredMergeDomain = Name{};
    if(
        trackConsumerFrontiers
        && !inOutSummary.hasCrossQueueConsumerFrontier
        && HasCrossQueueConsumerFrontier(analysis, assignments, task.id, packetQueue)
    )
        inOutSummary.hasCrossQueueConsumerFrontier = true;
}


[[nodiscard]] bool ResolvePacketTimingEnvelope(
    const GpuTaskGraphPacketTimingEnvelopeOptions& options,
    GpuTaskGraphCompilerDetail::GpuTaskGraphCompiledPlanStorage& compiledPlan,
    GpuSubmissionPacketRange& outRange
){
    outRange = {};
    const bool hasFirstTask = options.firstTask.valid();
    const bool hasLastTask = options.lastTask.valid();
    if(hasFirstTask != hasLastTask)
        return false;
    if(!hasFirstTask)
        return true;

    const u32 firstTaskIndex = GpuTaskGraphCompilerDetail::FindCompiledTaskIndex(compiledPlan, options.firstTask);
    const u32 lastTaskIndex = GpuTaskGraphCompilerDetail::FindCompiledTaskIndex(compiledPlan, options.lastTask);
    if(
        firstTaskIndex == Limit<u32>::s_Max
        || lastTaskIndex == Limit<u32>::s_Max
        || lastTaskIndex < firstTaskIndex
    )
        return false;

    const GpuSubmissionPacketId firstPacket = compiledPlan.tasks[firstTaskIndex].packet;
    const GpuSubmissionPacketId lastPacket = compiledPlan.tasks[lastTaskIndex].packet;
    if(
        !firstPacket.valid()
        || !lastPacket.valid()
        || firstPacket.generation != compiledPlan.planGeneration
        || lastPacket.generation != compiledPlan.planGeneration
        || firstPacket.index >= compiledPlan.packets.size()
        || lastPacket.index >= compiledPlan.packets.size()
        || lastPacket.index < firstPacket.index
    )
        return false;

    for(usize packetIndex = 0u; packetIndex <= lastPacket.index; ++packetIndex){
        if(compiledPlan.packets[packetIndex].joinsAcceptedQueueFrontier)
            return false;
    }

    const usize packetCount = static_cast<usize>(lastPacket.index) - firstPacket.index + 1u;
    outRange = GpuSubmissionPacketRange{ .first = firstPacket, .packetCount = packetCount };
    for(usize packetOffset = 0u; packetOffset < packetCount; ++packetOffset){
        GpuSubmissionPacket& packet = compiledPlan.packets[firstPacket.index + packetOffset];
        packet.recordsPacketEnvelopeTiming = true;
        packet.recordsTiming = true;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] u32 FindCompiledTaskIndex(
    const GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuTaskId& task
)noexcept{
    if(
        !task.valid()
        || task.generation != compiledPlan.graphGeneration
        || task.index >= compiledPlan.compiledTaskIndexByTask.size()
    )
        return Limit<u32>::s_Max;
    const u32 compiledTaskIndex = compiledPlan.compiledTaskIndexByTask[task.index];
    if(
        compiledTaskIndex >= compiledPlan.tasks.size()
        || compiledPlan.tasks[compiledTaskIndex].task != task
    )
        return Limit<u32>::s_Max;
    return compiledTaskIndex;
}

[[nodiscard]] const GpuCompiledTask* FindCompiledTask(
    const GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuTaskId& task
)noexcept{
    const u32 compiledTaskIndex = FindCompiledTaskIndex(compiledPlan, task);
    return compiledTaskIndex != Limit<u32>::s_Max ? &compiledPlan.tasks[compiledTaskIndex] : nullptr;
}

[[nodiscard]] GpuCompiledTask* FindCompiledTask(
    GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuTaskId& task
)noexcept{
    const u32 compiledTaskIndex = FindCompiledTaskIndex(compiledPlan, task);
    return compiledTaskIndex != Limit<u32>::s_Max ? &compiledPlan.tasks[compiledTaskIndex] : nullptr;
}

[[nodiscard]] GpuSubmissionPacketId FindCompiledPacketForTask(
    const GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuTaskId& task
)noexcept{
    const GpuCompiledTask* const compiledTask = FindCompiledTask(compiledPlan, task);
    return compiledTask ? compiledTask->packet : GpuSubmissionPacketId{};
}

[[nodiscard]] const GpuPhysicalQueueInfo* FindCompiledQueueInfo(
    const GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuPhysicalQueueId& queue
)noexcept{
    if(!queue.valid() || queue.deviceGeneration != compiledPlan.deviceGeneration)
        return nullptr;
    for(const GpuPhysicalQueueInfo& info : compiledPlan.queueTopology){
        if(info.id == queue)
            return &info;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BuildSubmissionPackets(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueAssignments& assignments,
    const GpuTaskGraphPacketizationPolicy::Enum policy,
    const GpuTaskGraphPacketTimingEnvelopeOptions& timingEnvelope,
    GpuTaskGraphCompiledPlanStorage& compiledPlan,
    GpuSubmissionPacketRange& outTimingEnvelopeRange
){
    if(
        !analysis.validFor(graph)
        || !assignments.validFor(graph)
        || compiledPlan.compiledTaskIndexByTask.size() != graph.taskCount()
        || !compiledPlan.tasks.empty()
    )
        return false;

    // Tasks retain one exact acceptance and synchronization point by default. An explicitly opted-in successor may
    // share its immediately preceding compatible packet, preserving task order while retaining one submission for a
    // temporary imported/native recording bridge. The separate FrontierScored policy is deliberately opt-in too:
    // it only absorbs a cheap immediate successor after proving that the preceding packet has no cross-queue signal
    // frontier. This keeps current renderer packet boundaries stable while the generic compiler can reduce safe
    // one-task submission overhead for new callers.
    __hidden_gpu_task_graph_compiler_packetization::PacketMergeSummary precedingPacketSummary;
    const bool trackConsumerFrontiers = policy == GpuTaskGraphPacketizationPolicy::FrontierSafe
        || policy == GpuTaskGraphPacketizationPolicy::FrontierScored
    ;
    for(const GpuTaskId taskID : analysis.topologicalOrder()){
        if(
            !taskID.valid()
            || taskID.generation != compiledPlan.graphGeneration
            || taskID.index >= compiledPlan.compiledTaskIndexByTask.size()
            || compiledPlan.compiledTaskIndexByTask[taskID.index] != Limit<u32>::s_Max
            || compiledPlan.tasks.size() >= Limit<u32>::s_Max
        )
            return false;
        const u32 compiledTaskIndex = static_cast<u32>(compiledPlan.tasks.size());

        const GpuTaskQueueAssignment* const assignment = assignments.find(taskID);
        if(!assignment || !assignment->queue.valid())
            return false;

        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        const bool taskRecordsTiming = task.timing.policy != GpuTaskTimingPolicy::None;
        GpuSubmissionPacketId packetID;
        GpuTaskPacketizationDecision::Enum packetizationDecision = compiledPlan.packets.empty()
            ? GpuTaskPacketizationDecision::FirstTask
            : GpuTaskPacketizationDecision::MergeNotRequested
        ;
        const bool scoredMergeRequested =
            policy == GpuTaskGraphPacketizationPolicy::FrontierScored
            && !task.scheduling.mergeWithPrevious
        ;
        const bool mergeRequested =
            (task.scheduling.mergeWithPrevious || scoredMergeRequested)
            && task.scheduling.allowPacketMerge
            && !task.scheduling.forceSubmissionBoundary
            && !task.scheduling.joinsAcceptedQueueFrontier
            && !compiledPlan.packets.empty()
        ;
        if(
            !compiledPlan.packets.empty()
            && (
                !task.scheduling.allowPacketMerge
                || task.scheduling.forceSubmissionBoundary
                || task.scheduling.joinsAcceptedQueueFrontier
            )
        )
            packetizationDecision = GpuTaskPacketizationDecision::TaskForcesBoundary;
        if(mergeRequested){
            GpuSubmissionPacket& precedingPacket = compiledPlan.packets.back();
            bool precedingPacketAllowsMerge = precedingPacket.queue == assignment->queue;
            if(!precedingPacketAllowsMerge)
                packetizationDecision = GpuTaskPacketizationDecision::QueueChanged;
            if(precedingPacketAllowsMerge && !precedingPacketSummary.allTasksAllowMerge){
                packetizationDecision = GpuTaskPacketizationDecision::PrecedingTaskForcesBoundary;
                precedingPacketAllowsMerge = false;
            }
            if(precedingPacketAllowsMerge && scoredMergeRequested){
                const Name& frontierScoredMergeDomain = task.scheduling.frontierScoredMergeDomain;
                const bool precedingPacketMatchesScoredMergeDomain = static_cast<bool>(frontierScoredMergeDomain)
                    && static_cast<bool>(precedingPacketSummary.uniformFrontierScoredMergeDomain)
                    && precedingPacketSummary.uniformFrontierScoredMergeDomain == frontierScoredMergeDomain
                ;
                if(!precedingPacketMatchesScoredMergeDomain)
                    packetizationDecision = GpuTaskPacketizationDecision::ScoredMergeDomainMismatch;
                precedingPacketAllowsMerge = precedingPacketMatchesScoredMergeDomain;
            }
            if(precedingPacketAllowsMerge && scoredMergeRequested){
                // A score is useful only for an actual immediate serial chain. Never use packet coalescing to
                // manufacture an order between unrelated work, and keep Medium/Large work independently
                // accept/recoverable unless its owner deliberately asks for an explicit merge.
                const GpuTaskId precedingTask = compiledPlan.packetTasks[
                    precedingPacket.taskOffset + precedingPacket.taskCount - 1u
                ];
                const bool directlyDependsOnPrecedingTask =
                    __hidden_gpu_task_graph_compiler_packetization::SchedulingConsumersContain(
                        analysis,
                        precedingTask,
                        taskID
                    )
                ;
                const bool eligibleScoredMerge = directlyDependsOnPrecedingTask
                    && task.scheduling.cost <= GpuTaskCostHint::Small
                    && !task.scheduling.allowMergeAcrossConsumerFrontier
                ;
                if(!eligibleScoredMerge)
                    packetizationDecision = GpuTaskPacketizationDecision::ScoredMergeIneligible;
                precedingPacketAllowsMerge = eligibleScoredMerge;
            }
            if(precedingPacketAllowsMerge && task.scheduling.allowMergeAcrossConsumerFrontier){
                // This narrow opt-in is only valid for an explicit immediate successor. It must not let an
                // unrelated same-queue task quietly absorb an already-signallable cross-queue frontier.
                const GpuTaskId precedingTask = compiledPlan.packetTasks[
                    precedingPacket.taskOffset + precedingPacket.taskCount - 1u
                ];
                bool explicitlyDependsOnPrecedingTask = false;
                for(usize dependencyIndex = 0u;
                    dependencyIndex < task.dependencyCount;
                    ++dependencyIndex
                ){
                    explicitlyDependsOnPrecedingTask = explicitlyDependsOnPrecedingTask
                        || task.dependencies[dependencyIndex] == precedingTask
                    ;
                }
                if(!explicitlyDependsOnPrecedingTask)
                    packetizationDecision = GpuTaskPacketizationDecision::MergeRequiresExplicitImmediateDependency;
                precedingPacketAllowsMerge = explicitlyDependsOnPrecedingTask;
            }
            if(
                precedingPacketAllowsMerge
                && trackConsumerFrontiers
                && !task.scheduling.allowMergeAcrossConsumerFrontier
            ){
                if(precedingPacketSummary.hasCrossQueueConsumerFrontier){
                    packetizationDecision = GpuTaskPacketizationDecision::CrossQueueConsumerFrontier;
                    precedingPacketAllowsMerge = false;
                }
            }
            if(precedingPacketAllowsMerge){
                packetID = GpuSubmissionPacketId{
                    static_cast<u32>(compiledPlan.packets.size() - 1u),
                    compiledPlan.planGeneration,
                };
                ++precedingPacket.taskCount;
                precedingPacket.recordsTiming = precedingPacket.recordsTiming || taskRecordsTiming;
                packetizationDecision = scoredMergeRequested
                    ? GpuTaskPacketizationDecision::MergedFrontierScored
                    : GpuTaskPacketizationDecision::MergedExplicit
                ;
            }
        }

        if(!packetID.valid()){
            packetID = GpuSubmissionPacketId{
                static_cast<u32>(compiledPlan.packets.size()),
                compiledPlan.planGeneration,
            };
            compiledPlan.packets.push_back(GpuSubmissionPacket{
                .queue = assignment->queue,
                .taskOffset = static_cast<u32>(compiledPlan.packetTasks.size()),
                .taskCount = 1u,
                .joinsAcceptedQueueFrontier = task.scheduling.joinsAcceptedQueueFrontier,
                .isRecoverySubmission = task.scheduling.isRecoverySubmission,
                .recordsTiming = taskRecordsTiming,
            });
            __hidden_gpu_task_graph_compiler_packetization::InitializePacketMergeSummary(
                task,
                assignment->queue,
                analysis,
                assignments,
                trackConsumerFrontiers,
                precedingPacketSummary
            );
        }
        else{
            __hidden_gpu_task_graph_compiler_packetization::AccumulatePacketMergeSummary(
                task,
                assignment->queue,
                analysis,
                assignments,
                trackConsumerFrontiers,
                precedingPacketSummary
            );
        }
        compiledPlan.packetTasks.push_back(taskID);
        compiledPlan.tasks.push_back(GpuCompiledTask{
            .task = taskID,
            .queue = assignment->queue,
            .packet = packetID,
            .packetizationDecision = packetizationDecision,
            .timingPolicy = task.timing.policy,
            .recordsNonCommittingTimingSample = static_cast<bool>(
                assignment->modifiers
                & (
                    GpuTaskQueueAssignmentModifier::TimingCalibration
                    | GpuTaskQueueAssignmentModifier::DebugTimingOverride
                )
            ),
        });
        compiledPlan.compiledTaskIndexByTask[taskID.index] = compiledTaskIndex;
    }
    if(compiledPlan.tasks.size() != compiledPlan.compiledTaskIndexByTask.size())
        return false;
    return __hidden_gpu_task_graph_compiler_packetization::ResolvePacketTimingEnvelope(
        timingEnvelope,
        compiledPlan,
        outTimingEnvelopeRange
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

