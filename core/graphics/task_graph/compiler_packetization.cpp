// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_compiler_packetization{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

    usize firstTaskIndex = Limit<usize>::s_Max;
    usize lastTaskIndex = Limit<usize>::s_Max;
    for(usize taskIndex = 0u; taskIndex < compiledPlan.tasks.size(); ++taskIndex){
        const GpuCompiledTask& task = compiledPlan.tasks[taskIndex];
        if(task.task == options.firstTask)
            firstTaskIndex = taskIndex;
        if(task.task == options.lastTask)
            lastTaskIndex = taskIndex;
    }
    if(
        firstTaskIndex == Limit<usize>::s_Max
        || lastTaskIndex == Limit<usize>::s_Max
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


[[nodiscard]] const GpuCompiledTask* FindCompiledTask(
    const GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuTaskId& task
)noexcept{
    if(!task.valid() || task.generation != compiledPlan.graphGeneration)
        return nullptr;
    for(const GpuCompiledTask& compiledTask : compiledPlan.tasks){
        if(compiledTask.task == task)
            return &compiledTask;
    }
    return nullptr;
}

[[nodiscard]] GpuCompiledTask* FindCompiledTask(
    GpuTaskGraphCompiledPlanStorage& compiledPlan,
    const GpuTaskId& task
)noexcept{
    if(!task.valid() || task.generation != compiledPlan.graphGeneration)
        return nullptr;
    for(GpuCompiledTask& compiledTask : compiledPlan.tasks){
        if(compiledTask.task == task)
            return &compiledTask;
    }
    return nullptr;
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
    // Tasks retain one exact acceptance and synchronization point by default. An explicitly opted-in successor may
    // share its immediately preceding compatible packet, preserving task order while retaining one submission for a
    // temporary imported/native recording bridge. The separate FrontierScored policy is deliberately opt-in too:
    // it only absorbs a cheap immediate successor after proving that the preceding packet has no cross-queue signal
    // frontier. This keeps current renderer packet boundaries stable while the generic compiler can reduce safe
    // one-task submission overhead for new callers.
    for(const GpuTaskId taskID : analysis.topologicalOrder()){
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
            for(u32 precedingTaskIndex = 0u;
                precedingPacketAllowsMerge && precedingTaskIndex < precedingPacket.taskCount;
                ++precedingTaskIndex
            ){
                const GpuTaskId precedingTask = compiledPlan.packetTasks[
                    precedingPacket.taskOffset + precedingTaskIndex
                ];
                const GpuTaskGraphTaskView preceding = graph.taskAt(precedingTask.index);
                const bool precedingTaskAllowsMerge = preceding.scheduling.allowPacketMerge
                    && !preceding.scheduling.forceSubmissionBoundary
                    && !preceding.scheduling.joinsAcceptedQueueFrontier
                ;
                if(!precedingTaskAllowsMerge)
                    packetizationDecision = GpuTaskPacketizationDecision::PrecedingTaskForcesBoundary;
                precedingPacketAllowsMerge = precedingTaskAllowsMerge;
            }
            if(precedingPacketAllowsMerge && scoredMergeRequested){
                const Name& frontierScoredMergeDomain = task.scheduling.frontierScoredMergeDomain;
                bool precedingPacketMatchesScoredMergeDomain = static_cast<bool>(frontierScoredMergeDomain);
                for(u32 precedingTaskIndex = 0u;
                    precedingPacketMatchesScoredMergeDomain && precedingTaskIndex < precedingPacket.taskCount;
                    ++precedingTaskIndex
                ){
                    const GpuTaskId precedingTask = compiledPlan.packetTasks[
                        precedingPacket.taskOffset + precedingTaskIndex
                    ];
                    const GpuTaskGraphTaskView preceding = graph.taskAt(precedingTask.index);
                    precedingPacketMatchesScoredMergeDomain =
                        static_cast<bool>(preceding.scheduling.frontierScoredMergeDomain)
                        && preceding.scheduling.frontierScoredMergeDomain == frontierScoredMergeDomain
                    ;
                }
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
                bool directlyDependsOnPrecedingTask = false;
                for(const GpuTaskDependencyEdge& edge : analysis.schedulingEdges()){
                    if(edge.producer == precedingTask && edge.consumer == taskID){
                        directlyDependsOnPrecedingTask = true;
                        break;
                    }
                }
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
                && (
                    policy == GpuTaskGraphPacketizationPolicy::FrontierSafe
                    || policy == GpuTaskGraphPacketizationPolicy::FrontierScored
                )
                && !task.scheduling.allowMergeAcrossConsumerFrontier
            ){
                for(u32 precedingTaskIndex = 0u;
                    precedingPacketAllowsMerge && precedingTaskIndex < precedingPacket.taskCount;
                    ++precedingTaskIndex
                ){
                    const GpuTaskId precedingTask = compiledPlan.packetTasks[
                        precedingPacket.taskOffset + precedingTaskIndex
                    ];
                    for(const GpuTaskDependencyEdge& edge : analysis.schedulingEdges()){
                        if(edge.producer != precedingTask)
                            continue;

                        const GpuTaskQueueAssignment* const consumerAssignment = assignments.find(edge.consumer);
                        // Queue assignment covers every analyzed task. Treat a broken assignment conservatively so a
                        // missing consumer identity cannot hide a required cross-queue signal frontier.
                        if(
                            !consumerAssignment
                            || !consumerAssignment->queue.valid()
                            || consumerAssignment->queue != precedingPacket.queue
                        ){
                            packetizationDecision = GpuTaskPacketizationDecision::CrossQueueConsumerFrontier;
                            precedingPacketAllowsMerge = false;
                            break;
                        }
                    }
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
    }
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

