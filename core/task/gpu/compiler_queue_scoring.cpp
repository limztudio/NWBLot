// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static i32 SaturateQueueScoreTerm(const u64 value)noexcept{
    return value > static_cast<u64>(Limit<i32>::s_Max)
        ? Limit<i32>::s_Max
        : static_cast<i32>(value)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const GpuPhysicalQueueInfo* FindPhysicalQueueInfo(
    const GpuPhysicalQueueTopology& topology,
    const GpuPhysicalQueueId& queueID
)noexcept{
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(queue.id == queueID)
            return &queue;
    }
    return nullptr;
}


bool IsLegalQueueAssignmentCandidate(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& candidate
)noexcept{
    if(
        !HasCapabilities(candidate.capabilities, task.commands.requiredCapabilities)
        || (task.commands.externalQueue.valid() && task.commands.externalQueue != candidate.id)
        || (
            task.commands.alternativeCapabilities != GpuQueueCapability::None
            && (candidate.capabilities & task.commands.alternativeCapabilities) == GpuQueueCapability::None
        )
        || (
            task.commands.additionalAlternativeCapabilities != GpuQueueCapability::None
            && (candidate.capabilities & task.commands.additionalAlternativeCapabilities) == GpuQueueCapability::None
        )
    )
        return false;

    const GpuPresentEndpoint* const presentEndpoint = graph.presentEndpoint();
    bool requiresPrimaryGraphicsQueue = task.commands.requiresPrimaryGraphicsQueue
        || (presentEndpoint && presentEndpoint->producer == task.id)
    ;
    if(presentEndpoint){
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex)
            requiresPrimaryGraphicsQueue = requiresPrimaryGraphicsQueue
                || task.resourceUses[useIndex].resource == presentEndpoint->backBuffer
            ;
    }
    if(requiresPrimaryGraphicsQueue){
        const GpuPhysicalQueueInfo* const primaryGraphics = FindDefaultGraphicsQueue(topology);
        if(!primaryGraphics || primaryGraphics->id != candidate.id)
            return false;
    }

    for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(task.resourceUses[useIndex].resource.index);
        if(resource.type == GpuGraphResourceType::HazardDomain)
            continue;
        if(resource.directConsumerQueue.valid()){
            const GpuPhysicalQueueInfo* const consumerQueue = FindPhysicalQueueInfo(topology, resource.directConsumerQueue);
            if(
                !consumerQueue
                || !ResourceSharingAdmitsQueue(resource, topology, *consumerQueue)
                || (!ResourceUsesConcurrentQueueSharing(resource, topology) && consumerQueue->familyIndex != candidate.familyIndex)
            )
                return false;
        }
        if(
            ResourceUsesConcurrentQueueSharing(resource, topology)
            && !ResourceSharingAdmitsQueue(resource, topology, candidate)
        )
            return false;
    }
    return true;
}


const GpuTaskQueueAssignment* FindQueueAssignment(
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuTaskId& task
)noexcept{
    if(!task.valid() || task.index >= assignmentIndicesByTask.size())
        return nullptr;
    const u32 assignmentIndex = assignmentIndicesByTask[task.index];
    if(assignmentIndex >= assignments.size())
        return nullptr;
    const GpuTaskQueueAssignment& assignment = assignments[assignmentIndex];
    return assignment.task == task ? &assignment : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuQueueAssignmentScore BuildQueueAssignmentScore(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& candidate,
    const GpuTaskQueueScoreExclusions& exclusions)noexcept{
    GpuQueueAssignmentScore score;
    const usize ignoredAssignmentOffset = exclusions.assignmentOffset;
    const usize ignoredAssignmentCount = exclusions.assignmentCount;
    const u64 assignedQueueCost = scoringData.assignedQueueLoad(candidate.id);
    GLOBAL_ASSERT(assignedQueueCost >= exclusions.candidateQueueCost);
    u64 queueLoad = assignedQueueCost - exclusions.candidateQueueCost;
    const u32 taskAssignmentIndex = assignmentIndicesByTask[task.id.index];
    const bool taskExcluded = taskAssignmentIndex >= ignoredAssignmentOffset
        && taskAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount
    ;
    const GpuTaskQueueAssignment* const ownAssignment = FindQueueAssignment(assignments, assignmentIndicesByTask, task.id);
    const u64 ownCost = ownAssignment && !taskExcluded ? scoringData.m_taskCosts[task.id.index] : 0u;
    if(ownAssignment && ownAssignment->queue == candidate.id)
        queueLoad -= ownCost;

    u64 overlap = 0u;
    if(
        ignoredAssignmentCount <= 1u
        && task.scheduling.overlapPreferred
        && !task.scheduling.avoidQueueCrossing
        && schedulingReachability.mayContainIndependentTasks()
    ){
        u64 otherQueueCost = scoringData.m_totalAssignedCost - assignedQueueCost - (exclusions.totalCost - exclusions.candidateQueueCost);
        if(ownAssignment && ownAssignment->queue != candidate.id)
            otherQueueCost -= ownCost;
        if(analysis.schedulingEdges().empty())
            overlap = otherQueueCost;
        else if(otherQueueCost != 0u){
            const GpuTaskQueueAssignment unassignedTask{ .task = task.id, .initialQueue = {}, .queue = candidate.id, .score = {} };
            overlap = scoringData.independentQueueCost(
                schedulingReachability,
                candidate.id,
                NotNull<const GpuTaskQueueAssignment*>(ownAssignment ? ownAssignment : &unassignedTask),
                1u
            );
            if(ignoredAssignmentCount == 1u){
                const GpuTaskQueueAssignment& excluded = assignments[ignoredAssignmentOffset];
                if(excluded.queue != candidate.id && schedulingReachability.transitivelyIndependent(task.id, excluded.task))
                    overlap -= scoringData.m_taskCosts[excluded.task.index];
            }
        }
    }
    const u64 externalQueueLoad = scoringData.externalQueueLoad(candidate.id);
    queueLoad = queueLoad > Limit<u64>::s_Max - externalQueueLoad
        ? Limit<u64>::s_Max
        : queueLoad + externalQueueLoad
    ;
    score.overlap = SaturateQueueScoreTerm(overlap);
    score.queueLoad = SaturateQueueScoreTerm(queueLoad);

    u64 incomingCrossings = 0u;
    u64 outgoingCrossings = 0u;
    const GpuTaskGraphSchedulingTaskIndexView producerIndices = analysis.schedulingProducers(task.id);
    for(usize producerIndex = 0u; producerIndex < producerIndices.taskCount; ++producerIndex){
        const GpuTaskId producerTask{ .generation = task.id.generation, .index = static_cast<u32>(producerIndices[producerIndex]) };
        const u32 producerAssignmentIndex = assignmentIndicesByTask[producerTask.index];
        if(
            producerAssignmentIndex >= ignoredAssignmentOffset
            && producerAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount
        )
            continue;
        const GpuTaskQueueAssignment* const producer = FindQueueAssignment(
            assignments,
            assignmentIndicesByTask,
            producerTask
        );
        if(producer && producer->queue != candidate.id)
            ++incomingCrossings;
    }
    const GpuTaskGraphSchedulingTaskIndexView consumerIndices = analysis.schedulingConsumers(task.id);
    for(usize consumerIndex = 0u; consumerIndex < consumerIndices.taskCount; ++consumerIndex){
        const GpuTaskId consumerTask{ .generation = task.id.generation, .index = static_cast<u32>(consumerIndices[consumerIndex]) };
        const u32 consumerAssignmentIndex = assignmentIndicesByTask[consumerTask.index];
        if(
            consumerAssignmentIndex >= ignoredAssignmentOffset
            && consumerAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount
        )
            continue;
        const GpuTaskQueueAssignment* const consumer = FindQueueAssignment(
            assignments,
            assignmentIndicesByTask,
            consumerTask
        );
        if(consumer && consumer->queue != candidate.id)
            ++outgoingCrossings;
    }
    score.incomingCrossings = SaturateQueueScoreTerm(incomingCrossings);
    score.outgoingCrossings = SaturateQueueScoreTerm(outgoingCrossings);

    u64 ownershipTransfers = 0u;
    for(
        usize edgeIndex = scoringData.m_ownershipEdgeOffsets[task.id.index];
        edgeIndex < scoringData.m_ownershipEdgeOffsets[task.id.index + 1u];
        ++edgeIndex
    ){
        const GpuTaskDependencyEdge& edge = *scoringData.m_ownershipEdges[edgeIndex];
        const u32 producerAssignmentIndex = assignmentIndicesByTask[edge.producer.index];
        const u32 consumerAssignmentIndex = assignmentIndicesByTask[edge.consumer.index];
        if(
            producerAssignmentIndex >= ignoredAssignmentOffset
            && producerAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount
            && consumerAssignmentIndex >= ignoredAssignmentOffset
            && consumerAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount
        )
            continue;
        const GpuTaskQueueAssignment* const producerAssignment = FindQueueAssignment(
            assignments,
            assignmentIndicesByTask,
            edge.producer
        );
        const GpuTaskQueueAssignment* const consumerAssignment = FindQueueAssignment(
            assignments,
            assignmentIndicesByTask,
            edge.consumer
        );
        const GpuPhysicalQueueInfo* const producerQueue = edge.producer == task.id
            ? &candidate
            : producerAssignment ? FindPhysicalQueueInfo(topology, producerAssignment->queue) : nullptr
        ;
        const GpuPhysicalQueueInfo* const consumerQueue = edge.consumer == task.id
            ? &candidate
            : consumerAssignment ? FindPhysicalQueueInfo(topology, consumerAssignment->queue) : nullptr
        ;
        if(!producerQueue || !consumerQueue || producerQueue->familyIndex == consumerQueue->familyIndex)
            continue;

        const GpuTaskGraphResourceView resource = graph.resourceAt(edge.resource.index);
        if(resource.type == GpuGraphResourceType::HazardDomain)
            continue;

        const bool concurrentQueuePair = ResourceSharesQueuePairConcurrently(
            resource,
            topology,
            *producerQueue,
            *consumerQueue
        );
        if(!concurrentQueuePair)
            ++ownershipTransfers;
    }
    score.ownershipTransfers = SaturateQueueScoreTerm(ownershipTransfers);
    return score;
}


bool IsBetterAutomaticQueueAssignmentCandidate(
    const GpuQueueAssignmentScore& candidateScore,
    const GpuPhysicalQueueInfo& candidate,
    const GpuQueueAssignmentScore& currentScore,
    const GpuPhysicalQueueInfo* const current,
    const bool compareTotalScore)noexcept{
    if(!current)
        return true;

    if(compareTotalScore){
        const auto total = [](const GpuQueueAssignmentScore& score){
            return static_cast<i64>(score.overlap)
                - static_cast<i64>(score.queueLoad)
                - static_cast<i64>(score.incomingCrossings)
                - static_cast<i64>(score.outgoingCrossings)
                - static_cast<i64>(score.ownershipTransfers)
            ;
        };
        const i64 candidateTotal = total(candidateScore);
        const i64 currentTotal = total(currentScore);
        if(candidateTotal != currentTotal)
            return candidateTotal > currentTotal;
    }

    const i64 candidateCrossings = static_cast<i64>(candidateScore.incomingCrossings)
        + static_cast<i64>(candidateScore.outgoingCrossings)
    ;
    const i64 currentCrossings = static_cast<i64>(currentScore.incomingCrossings)
        + static_cast<i64>(currentScore.outgoingCrossings)
    ;
    if(candidateCrossings != currentCrossings)
        return candidateCrossings < currentCrossings;
    if(candidateScore.ownershipTransfers != currentScore.ownershipTransfers)
        return candidateScore.ownershipTransfers < currentScore.ownershipTransfers;
    const i64 candidateBenefit = static_cast<i64>(candidateScore.overlap) - static_cast<i64>(candidateScore.queueLoad);
    const i64 currentBenefit = static_cast<i64>(currentScore.overlap) - static_cast<i64>(currentScore.queueLoad);
    if(candidateBenefit != currentBenefit)
        return candidateBenefit > currentBenefit;
    if(candidateScore.overlap != currentScore.overlap)
        return candidateScore.overlap > currentScore.overlap;
    if(candidateScore.queueLoad != currentScore.queueLoad)
        return candidateScore.queueLoad < currentScore.queueLoad;
    return IsBetterQueue(candidate, current);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

