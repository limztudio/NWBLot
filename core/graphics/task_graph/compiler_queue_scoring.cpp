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

[[nodiscard]] static i32 SaturateQueueScoreTerm(const u64 value)noexcept{
    return value > static_cast<u64>(Limit<i32>::s_Max)
        ? Limit<i32>::s_Max
        : static_cast<i32>(value)
    ;
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

[[nodiscard]] static bool IsTransitivelyIndependent(
    const Vector<u8, Alloc::ScratchArena>& schedulingReachability,
    const usize taskCount,
    const GpuTaskId& lhs,
    const GpuTaskId& rhs
)noexcept{
    if(
        lhs.index >= taskCount
        || rhs.index >= taskCount
        || schedulingReachability.size() != taskCount * taskCount
    )
        return false;

    return schedulingReachability[lhs.index * taskCount + rhs.index] == 0u
        && schedulingReachability[rhs.index * taskCount + lhs.index] == 0u
    ;
}

[[nodiscard]] static bool MatchesPreferredQueueClass(
    const GpuQueuePreference::Enum preference,
    const CommandQueue::Enum queueClass
)noexcept{
    switch(preference){
    case GpuQueuePreference::Graphics: return queueClass == CommandQueue::Graphics;
    case GpuQueuePreference::Compute: return queueClass == CommandQueue::Compute;
    case GpuQueuePreference::Transfer: return queueClass == CommandQueue::Transfer;
    case GpuQueuePreference::Any: return false;
    default: return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const GpuPhysicalQueueInfo* FindPhysicalQueueInfo(
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


bool IsLegalQueueAssignmentCandidate(
    const GpuTaskGraph& graph,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& candidate
)noexcept{
    if(!HasCapabilities(candidate.capabilities, task.queue.requiredCapabilities))
        return false;

    for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(task.resourceUses[useIndex].resource.index);
        if(resource.type == GpuGraphResourceType::HazardDomain)
            continue;
        if(
            ResourceUsesConcurrentQueueSharing(resource.queueSharing, topology)
            && !ResourceSharingIncludesQueueFamily(resource.queueSharing, topology, candidate.familyIndex)
        )
            return false;
    }
    return true;
}


const GpuPhysicalQueueInfo* FindBestLegalQueueAssignmentCandidate(
    const GpuTaskGraph& graph,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskGraphTaskView& task,
    const CommandQueue::Enum requiredClass,
    const bool dedicatedOnly
)noexcept{
    const GpuPhysicalQueueInfo* result = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            (requiredClass != CommandQueue::kCount && candidate.queueClass != requiredClass)
            || (dedicatedOnly && !candidate.dedicated)
            || !IsLegalQueueAssignmentCandidate(graph, topology, task, candidate)
            || !IsBetterQueue(candidate, result)
        )
            continue;
        result = &candidate;
    }
    return result;
}


bool BuildGpuTaskSchedulingReachability(
    const GpuTaskGraphAnalysis& analysis,
    const usize taskCount,
    Vector<u8, Alloc::ScratchArena>& outReachability
){
    if(taskCount != 0u && taskCount > Limit<usize>::s_Max / taskCount)
        return false;

    outReachability.clear();
    outReachability.resize(taskCount * taskCount, 0u);
    for(usize orderIndex = analysis.topologicalOrder().size(); orderIndex > 0u; --orderIndex){
        const GpuTaskId source = analysis.topologicalOrder()[orderIndex - 1u];
        for(const GpuTaskDependencyEdge& edge : analysis.schedulingEdges()){
            if(edge.producer != source)
                continue;

            outReachability[source.index * taskCount + edge.consumer.index] = 1u;
            for(usize destinationIndex = 0u; destinationIndex < taskCount; ++destinationIndex){
                if(outReachability[edge.consumer.index * taskCount + destinationIndex] != 0u)
                    outReachability[source.index * taskCount + destinationIndex] = 1u;
            }
        }
    }
    return true;
}


bool HasTransitivelyIndependentRequiredGraphicsTask(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const Vector<u8, Alloc::ScratchArena>& schedulingReachability,
    const GpuTaskGraphTaskView& task
)noexcept{
    for(const GpuTaskId otherTaskID : analysis.topologicalOrder()){
        if(otherTaskID == task.id)
            continue;

        const GpuTaskGraphTaskView otherTask = graph.taskAt(otherTaskID.index);
        if(
            RequiresGraphics(otherTask.queue.requiredCapabilities)
            && IsTransitivelyIndependent(schedulingReachability, graph.taskCount(), task.id, otherTask.id)
        )
            return true;
    }
    return false;
}


GpuQueueAssignmentScore BuildQueueAssignmentScore(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuTaskGraphQueueTopology& topology,
    const Vector<u8, Alloc::ScratchArena>& schedulingReachability,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& candidate
)noexcept{
    GpuQueueAssignmentScore score;
    score.preference = MatchesPreferredQueueClass(task.queue.preferredQueue, candidate.queueClass) ? 1 : 0;

    u64 overlap = 0u;
    u64 queueLoad = 0u;
    for(const GpuTaskQueueAssignment& assignment : assignments){
        if(assignment.task == task.id)
            continue;

        const u64 cost = QueueCostWeight(graph.taskAt(assignment.task.index).scheduling.cost);
        if(assignment.queue == candidate.id)
            queueLoad = queueLoad > Limit<u64>::s_Max - cost ? Limit<u64>::s_Max : queueLoad + cost;
        if(
            task.scheduling.overlapPreferred
            && !task.scheduling.avoidQueueCrossing
            && assignment.queue != candidate.id
            && IsTransitivelyIndependent(
                schedulingReachability,
                graph.taskCount(),
                task.id,
                assignment.task
            )
        )
            overlap = overlap > Limit<u64>::s_Max - cost ? Limit<u64>::s_Max : overlap + cost;
    }
    score.overlap = SaturateQueueScoreTerm(overlap);
    score.queueLoad = SaturateQueueScoreTerm(queueLoad);

    u64 incomingCrossings = 0u;
    u64 outgoingCrossings = 0u;
    for(const GpuTaskDependencyEdge& edge : analysis.schedulingEdges()){
        if(edge.consumer == task.id){
            const GpuTaskQueueAssignment* const producer = FindQueueAssignment(assignments, edge.producer);
            if(producer && producer->queue != candidate.id)
                ++incomingCrossings;
        }
        if(edge.producer == task.id){
            const GpuTaskQueueAssignment* const consumer = FindQueueAssignment(assignments, edge.consumer);
            if(consumer && consumer->queue != candidate.id)
                ++outgoingCrossings;
        }
    }
    score.incomingCrossings = SaturateQueueScoreTerm(incomingCrossings);
    score.outgoingCrossings = SaturateQueueScoreTerm(outgoingCrossings);

    u64 ownershipTransfers = 0u;
    for(usize edgeIndex = 0u; edgeIndex < analysis.inferredEdges().size(); ++edgeIndex){
        const GpuTaskDependencyEdge& edge = analysis.inferredEdges()[edgeIndex];
        if(
            (edge.producer != task.id && edge.consumer != task.id)
            || !edge.resource.valid()
        )
            continue;

        bool alreadyCounted = false;
        for(usize previousIndex = 0u; previousIndex < edgeIndex; ++previousIndex){
            const GpuTaskDependencyEdge& previous = analysis.inferredEdges()[previousIndex];
            if(
                previous.producer == edge.producer
                && previous.consumer == edge.consumer
                && previous.resource == edge.resource
            ){
                alreadyCounted = true;
                break;
            }
        }
        if(alreadyCounted)
            continue;

        const GpuTaskQueueAssignment* const producerAssignment = FindQueueAssignment(assignments, edge.producer);
        const GpuTaskQueueAssignment* const consumerAssignment = FindQueueAssignment(assignments, edge.consumer);
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

        const bool concurrentQueuePair = ResourceUsesConcurrentQueueSharing(resource.queueSharing, topology)
            && ResourceSharingIncludesQueueFamily(resource.queueSharing, topology, producerQueue->familyIndex)
            && ResourceSharingIncludesQueueFamily(resource.queueSharing, topology, consumerQueue->familyIndex)
        ;
        if(!concurrentQueuePair)
            ++ownershipTransfers;
    }
    score.ownershipTransfers = SaturateQueueScoreTerm(ownershipTransfers);
    return score;
}


bool IsBetterAnyQueueAssignmentCandidate(
    const GpuQueueAssignmentScore& candidateScore,
    const GpuPhysicalQueueInfo& candidate,
    const GpuQueueAssignmentScore& currentScore,
    const GpuPhysicalQueueInfo* const current
)noexcept{
    if(!current)
        return true;

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

