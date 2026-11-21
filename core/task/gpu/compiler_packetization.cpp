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
    b8 allTasksAllowMerge = false;
    b8 hasCrossQueueConsumerFrontier = false;
};

[[nodiscard]] b8 SchedulingConsumersContain(const GpuTaskGraphAnalysis& analysis, const GpuTaskId& producer, const GpuTaskId& consumer)noexcept{
    const GpuTaskGraphSchedulingTaskIndexView consumers = GpuTaskGraphCompilerDetail::CompilerGraphAccess::SchedulingConsumers(analysis, producer.index);
    for(usize consumerIndex = 0u; consumerIndex < consumers.taskCount; ++consumerIndex){
        if(consumers[consumerIndex] == consumer.index)
            return true;
    }
    return false;
}

[[nodiscard]] b8 HasCrossQueueConsumerFrontier(const GpuTaskGraphAnalysis& analysis, const GpuTaskGraphQueueAssignments& assignments, const GpuTaskId& task, const GpuPhysicalQueueId& packetQueue)noexcept{
    const GpuTaskGraphSchedulingTaskIndexView consumers = GpuTaskGraphCompilerDetail::CompilerGraphAccess::SchedulingConsumers(analysis, task.index);
    for(usize consumerIndex = 0u; consumerIndex < consumers.taskCount; ++consumerIndex){
        auto& consumerAssignment = *MakeNotNull(GpuTaskGraphCompilerDetail::CompilerGraphAccess::AssignmentForTaskIndex(assignments, consumers[consumerIndex]));
        FOUNDATION_ASSERT(consumerAssignment.queue.valid());
        if(consumerAssignment.queue != packetQueue)
            return true;
    }
    return false;
}

static void InitializePacketMergeSummary(
    const GpuTaskGraphDetail::GpuTaskNode& task,
    const GpuTaskId taskId,
    const GpuPhysicalQueueId& packetQueue,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueAssignments& assignments,
    const b8 trackConsumerFrontiers,
    PacketMergeSummary& outSummary
)noexcept{
    outSummary.uniformFrontierScoredMergeDomain = task.scheduling.frontierScoredMergeDomain;
    outSummary.allTasksAllowMerge = GpuTaskGraphCompilerDetail::TaskNodeAllowsPacketMerge(task);
    outSummary.hasCrossQueueConsumerFrontier = trackConsumerFrontiers && HasCrossQueueConsumerFrontier(analysis, assignments, taskId, packetQueue);
}

static void AccumulatePacketMergeSummary(
    const GpuTaskGraphDetail::GpuTaskNode& task,
    const GpuTaskId taskId,
    const GpuPhysicalQueueId& packetQueue,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueAssignments& assignments,
    const b8 trackConsumerFrontiers,
    PacketMergeSummary& inOutSummary
)noexcept{
    inOutSummary.allTasksAllowMerge = inOutSummary.allTasksAllowMerge && GpuTaskGraphCompilerDetail::TaskNodeAllowsPacketMerge(task);
    if(!inOutSummary.uniformFrontierScoredMergeDomain || !task.scheduling.frontierScoredMergeDomain || inOutSummary.uniformFrontierScoredMergeDomain != task.scheduling.frontierScoredMergeDomain)
        inOutSummary.uniformFrontierScoredMergeDomain = Name{};
    if(trackConsumerFrontiers && !inOutSummary.hasCrossQueueConsumerFrontier && HasCrossQueueConsumerFrontier(analysis, assignments, taskId, packetQueue))
        inOutSummary.hasCrossQueueConsumerFrontier = true;
}


[[nodiscard]] Expected<GpuSubmissionPacketRange> ResolvePacketTimingEnvelope(const GpuTaskGraphPacketTimingEnvelopeOptions& options, GpuTaskGraphCompilerDetail::GpuTaskGraphCompiledPlanStorage& compiledPlan)noexcept{
    const b8 hasFirstTask = options.firstTask.valid();
    const b8 hasLastTask = options.lastTask.valid();
    if(hasFirstTask != hasLastTask)
        return MakeUnexpected(Failure{});
    if(!hasFirstTask)
        return GpuSubmissionPacketRange{};

    const u32 firstTaskIndex = GpuTaskGraphCompilerDetail::FindCompiledTaskIndex(compiledPlan, options.firstTask);
    const u32 lastTaskIndex = GpuTaskGraphCompilerDetail::FindCompiledTaskIndex(compiledPlan, options.lastTask);
    if(firstTaskIndex == Limit<u32>::s_Max || lastTaskIndex == Limit<u32>::s_Max || lastTaskIndex < firstTaskIndex)
        return MakeUnexpected(Failure{});

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
        return MakeUnexpected(Failure{});

    for(usize packetIndex = 0u; packetIndex <= lastPacket.index; ++packetIndex){
        if(compiledPlan.packets[packetIndex].joinsAcceptedQueueFrontier)
            return MakeUnexpected(Failure{});
    }

    const usize packetCount = static_cast<usize>(lastPacket.index) - firstPacket.index + 1u;
    const GpuSubmissionPacketRange range{ .first = firstPacket, .packetCount = packetCount };
    for(usize packetOffset = 0u; packetOffset < packetCount; ++packetOffset){
        GpuSubmissionPacket& packet = compiledPlan.packets[firstPacket.index + packetOffset];
        packet.recordsPacketEnvelopeTiming = true;
        packet.recordsTiming = true;
    }
    return range;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<GpuSubmissionPacketRange> BuildSubmissionPackets(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueAssignments& assignments,
    const GpuTaskGraphPacketizationPolicy::Enum policy,
    const GpuTaskGraphPacketTimingEnvelopeOptions& timingEnvelope,
    GpuTaskGraphCompiledPlanStorage& compiledPlan
){
    FOUNDATION_ASSERT(analysis.validFor(graph) && assignments.validFor(graph) && compiledPlan.compiledTaskIndexByTask.size() == graph.taskCount() && compiledPlan.tasks.empty());

    __hidden_gpu_task_graph_compiler_packetization::PacketMergeSummary precedingPacketSummary;
    const b8 trackConsumerFrontiers = policy == GpuTaskGraphPacketizationPolicy::FrontierSafe || policy == GpuTaskGraphPacketizationPolicy::FrontierScored;
    for(const GpuTaskId taskID : analysis.topologicalOrder()){
        FOUNDATION_ASSERT(
            taskID.valid()
            && taskID.generation == compiledPlan.graphGeneration
            && taskID.index < compiledPlan.compiledTaskIndexByTask.size()
            && compiledPlan.compiledTaskIndexByTask[taskID.index] == Limit<u32>::s_Max
            && compiledPlan.tasks.size() < Limit<u32>::s_Max
        );
        const u32 compiledTaskIndex = static_cast<u32>(compiledPlan.tasks.size());

        auto& assignment = *MakeNotNull(CompilerGraphAccess::AssignmentForTaskIndex(assignments, taskID.index));
        FOUNDATION_ASSERT(assignment.queue.valid());

        const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, taskID.index);
        const b8 taskRecordsTiming = task.timing.policy != GpuTaskTimingPolicy::None;
        GpuSubmissionPacketId packetID;
        GpuTaskPacketizationDecision::Enum packetizationDecision = compiledPlan.packets.empty() ? GpuTaskPacketizationDecision::FirstTask : GpuTaskPacketizationDecision::MergeNotRequested;
        const b8 scoredMergeRequested = policy == GpuTaskGraphPacketizationPolicy::FrontierScored && !task.scheduling.mergeWithPrevious;
        const b8 taskAllowsMerge = TaskNodeAllowsPacketMerge(task);
        const b8 mergeRequested = (task.scheduling.mergeWithPrevious || scoredMergeRequested) && taskAllowsMerge && !compiledPlan.packets.empty();
        if(!compiledPlan.packets.empty() && !taskAllowsMerge)
            packetizationDecision = GpuTaskPacketizationDecision::TaskForcesBoundary;
        if(mergeRequested){
            GpuSubmissionPacket& precedingPacket = compiledPlan.packets.back();
            b8 precedingPacketAllowsMerge = precedingPacket.queue == assignment.queue;
            if(!precedingPacketAllowsMerge)
                packetizationDecision = GpuTaskPacketizationDecision::QueueChanged;
            if(precedingPacketAllowsMerge && !precedingPacketSummary.allTasksAllowMerge){
                packetizationDecision = GpuTaskPacketizationDecision::PrecedingTaskForcesBoundary;
                precedingPacketAllowsMerge = false;
            }
            if(precedingPacketAllowsMerge && scoredMergeRequested){
                const Name& frontierScoredMergeDomain = task.scheduling.frontierScoredMergeDomain;
                const b8 precedingPacketMatchesScoredMergeDomain = static_cast<b8>(frontierScoredMergeDomain)
                    && static_cast<b8>(precedingPacketSummary.uniformFrontierScoredMergeDomain)
                    && precedingPacketSummary.uniformFrontierScoredMergeDomain == frontierScoredMergeDomain
                ;
                if(!precedingPacketMatchesScoredMergeDomain)
                    packetizationDecision = GpuTaskPacketizationDecision::ScoredMergeDomainMismatch;
                precedingPacketAllowsMerge = precedingPacketMatchesScoredMergeDomain;
            }
            if(precedingPacketAllowsMerge && scoredMergeRequested){
                const GpuTaskId precedingTask = compiledPlan.packetTasks[precedingPacket.taskOffset + precedingPacket.taskCount - 1u];
                const b8 directlyDependsOnPrecedingTask = __hidden_gpu_task_graph_compiler_packetization::SchedulingConsumersContain(analysis, precedingTask, taskID);
                const b8 eligibleScoredMerge = directlyDependsOnPrecedingTask && task.scheduling.cost <= GpuTaskCostHint::Small && !task.scheduling.allowMergeAcrossConsumerFrontier;
                if(!eligibleScoredMerge)
                    packetizationDecision = GpuTaskPacketizationDecision::ScoredMergeIneligible;
                precedingPacketAllowsMerge = eligibleScoredMerge;
            }
            if(precedingPacketAllowsMerge && task.scheduling.allowMergeAcrossConsumerFrontier){
                const GpuTaskId precedingTask = compiledPlan.packetTasks[precedingPacket.taskOffset + precedingPacket.taskCount - 1u];
                b8 explicitlyDependsOnPrecedingTask = false;
                for(const GpuTaskId dependency : CompilerGraphAccess::TaskDependencies(graph, task)){
                    if(dependency == precedingTask){
                        explicitlyDependsOnPrecedingTask = true;
                        break;
                    }
                }
                if(!explicitlyDependsOnPrecedingTask)
                    packetizationDecision = GpuTaskPacketizationDecision::MergeRequiresExplicitImmediateDependency;
                precedingPacketAllowsMerge = explicitlyDependsOnPrecedingTask;
            }
            if(precedingPacketAllowsMerge && trackConsumerFrontiers && !task.scheduling.allowMergeAcrossConsumerFrontier){
                if(precedingPacketSummary.hasCrossQueueConsumerFrontier){
                    packetizationDecision = GpuTaskPacketizationDecision::CrossQueueConsumerFrontier;
                    precedingPacketAllowsMerge = false;
                }
            }
            if(precedingPacketAllowsMerge){
                packetID = GpuSubmissionPacketId{ .generation = compiledPlan.planGeneration, .index = static_cast<u32>(compiledPlan.packets.size() - 1u), };
                ++precedingPacket.taskCount;
                precedingPacket.recordsTiming = precedingPacket.recordsTiming || taskRecordsTiming;
                packetizationDecision = scoredMergeRequested ? GpuTaskPacketizationDecision::MergedFrontierScored : GpuTaskPacketizationDecision::MergedExplicit;
            }
        }

        if(!packetID.valid()){
            packetID = GpuSubmissionPacketId{ .generation = compiledPlan.planGeneration, .index = static_cast<u32>(compiledPlan.packets.size()), };
            compiledPlan.packets.push_back(GpuSubmissionPacket{
                .taskOffset = static_cast<u32>(compiledPlan.packetTasks.size()),
                .taskCount = 1u,
                .queue = assignment.queue,
                .joinsAcceptedQueueFrontier = task.scheduling.joinsAcceptedQueueFrontier,
                .isRecoverySubmission = task.scheduling.isRecoverySubmission,
                .recordsTiming = taskRecordsTiming,
            });
            __hidden_gpu_task_graph_compiler_packetization::InitializePacketMergeSummary(task, taskID, assignment.queue, analysis, assignments, trackConsumerFrontiers, precedingPacketSummary);
        }
        else{
            __hidden_gpu_task_graph_compiler_packetization::AccumulatePacketMergeSummary(task, taskID, assignment.queue, analysis, assignments, trackConsumerFrontiers, precedingPacketSummary);
        }
        compiledPlan.packetTasks.push_back(taskID);
        compiledPlan.tasks.push_back(GpuCompiledTask{
            .task = taskID,
            .packet = packetID,
            .queue = assignment.queue,
            .packetizationDecision = packetizationDecision,
            .timingPolicy = task.timing.policy,
            .recordsNonCommittingTimingSample = static_cast<b8>(assignment.modifiers & GpuTaskQueueAssignmentModifier::TimingCalibration),
        });
        compiledPlan.compiledTaskIndexByTask[taskID.index] = compiledTaskIndex;
    }
    FOUNDATION_ASSERT(compiledPlan.tasks.size() == compiledPlan.compiledTaskIndexByTask.size());
    return __hidden_gpu_task_graph_compiler_packetization::ResolvePacketTimingEnvelope(timingEnvelope, compiledPlan);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

