// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskSchedulingReachability::GpuTaskSchedulingReachability(Alloc::ScratchArena& scratchArena)
    : m_words(scratchArena)
{}

bool GpuTaskSchedulingReachability::reaches(
    const GpuTaskId& source,
    const GpuTaskId& destination
)const noexcept{
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;

    if(
        !m_valid
        || !source.valid()
        || !destination.valid()
        || source.generation != m_graphGeneration
        || destination.generation != m_graphGeneration
        || source.index >= m_taskCount
        || destination.index >= m_taskCount
        || source == destination
    )
        return false;
    const usize wordIndex = source.index * m_wordsPerRow + destination.index / s_BitsPerWord;
    const u64 mask = static_cast<u64>(1u) << (destination.index % s_BitsPerWord);
    return (m_words[wordIndex] & mask) != 0u;
}

bool GpuTaskSchedulingReachability::transitivelyIndependent(
    const GpuTaskId& lhs,
    const GpuTaskId& rhs
)const noexcept{
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;

    if(
        !m_valid
        || !lhs.valid()
        || !rhs.valid()
        || lhs.generation != m_graphGeneration
        || rhs.generation != m_graphGeneration
        || lhs.index >= m_taskCount
        || rhs.index >= m_taskCount
        || lhs == rhs
    )
        return false;
    const usize lhsToRhsWord = lhs.index * m_wordsPerRow + rhs.index / s_BitsPerWord;
    const usize rhsToLhsWord = rhs.index * m_wordsPerRow + lhs.index / s_BitsPerWord;
    const u64 rhsMask = static_cast<u64>(1u) << (rhs.index % s_BitsPerWord);
    const u64 lhsMask = static_cast<u64>(1u) << (lhs.index % s_BitsPerWord);
    return (m_words[lhsToRhsWord] & rhsMask) == 0u
        && (m_words[rhsToLhsWord] & lhsMask) == 0u
    ;
}


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
            ResourceUsesConcurrentQueueSharing(resource, topology)
            && !ResourceSharingAdmitsQueue(resource, topology, candidate)
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
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    GpuTaskSchedulingReachability& outReachability
){
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;

    outReachability.m_words.clear();
    outReachability.m_graphGeneration = 0u;
    outReachability.m_taskCount = 0u;
    outReachability.m_wordsPerRow = 0u;
    outReachability.m_valid = false;
    const auto fail = [&outReachability](){
        outReachability.m_words.clear();
        outReachability.m_graphGeneration = 0u;
        outReachability.m_taskCount = 0u;
        outReachability.m_wordsPerRow = 0u;
        outReachability.m_valid = false;
        return false;
    };
    if(!analysis.validFor(graph))
        return fail();

    const usize taskCount = graph.taskCount();
    if(taskCount > static_cast<usize>(Limit<u32>::s_Max))
        return fail();
    const usize wordsPerRow = taskCount == 0u ? 0u : (taskCount - 1u) / s_BitsPerWord + 1u;
    usize totalWordCount = 0u;
    if(
        !TryMultiply<usize>(taskCount, wordsPerRow, totalWordCount)
        || totalWordCount > Limit<usize>::s_Max / sizeof(u64)
        || totalWordCount > outReachability.m_words.max_size()
    )
        return fail();

    outReachability.m_graphGeneration = graph.generation();
    outReachability.m_taskCount = taskCount;
    outReachability.m_wordsPerRow = wordsPerRow;
    outReachability.m_words.resize(totalWordCount, 0u);
    for(usize orderIndex = analysis.topologicalOrder().size(); orderIndex > 0u; --orderIndex){
        const GpuTaskId source = analysis.topologicalOrder()[orderIndex - 1u];
        if(
            !source.valid()
            || source.generation != graph.generation()
            || source.index >= taskCount
        )
            return fail();

        const usize sourceRowOffset = source.index * wordsPerRow;
        const GpuTaskGraphSchedulingTaskIndexView consumers = analysis.schedulingConsumers(source);
        for(usize consumerOffset = 0u; consumerOffset < consumers.taskCount; ++consumerOffset){
            const usize consumerIndex = consumers[consumerOffset];
            if(consumerIndex >= taskCount || consumerIndex == source.index)
                return fail();
            const usize consumerRowOffset = consumerIndex * wordsPerRow;
            for(usize wordIndex = 0u; wordIndex < wordsPerRow; ++wordIndex){
                outReachability.m_words[sourceRowOffset + wordIndex] |=
                    outReachability.m_words[consumerRowOffset + wordIndex]
                ;
            }
            const usize consumerWord = sourceRowOffset + consumerIndex / s_BitsPerWord;
            outReachability.m_words[consumerWord] |= static_cast<u64>(1u) << (consumerIndex % s_BitsPerWord);
        }
    }
    outReachability.m_valid = true;
    return true;
}


bool HasTransitivelyIndependentRequiredGraphicsTask(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskGraphTaskView& task
)noexcept{
    for(const GpuTaskId otherTaskID : analysis.topologicalOrder()){
        if(otherTaskID == task.id)
            continue;

        const GpuTaskGraphTaskView otherTask = graph.taskAt(otherTaskID.index);
        if(
            RequiresGraphics(otherTask.queue.requiredCapabilities)
            && schedulingReachability.transitivelyIndependent(task.id, otherTask.id)
        )
            return true;
    }
    return false;
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


GpuQueueAssignmentScore BuildQueueAssignmentScore(
    const GpuTaskGraph& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
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
            && schedulingReachability.transitivelyIndependent(task.id, assignment.task)
        )
            overlap = overlap > Limit<u64>::s_Max - cost ? Limit<u64>::s_Max : overlap + cost;
    }
    score.overlap = SaturateQueueScoreTerm(overlap);
    score.queueLoad = SaturateQueueScoreTerm(queueLoad);

    u64 incomingCrossings = 0u;
    u64 outgoingCrossings = 0u;
    const GpuTaskGraphSchedulingTaskIndexView producerIndices = analysis.schedulingProducers(task.id);
    for(usize producerIndex = 0u; producerIndex < producerIndices.taskCount; ++producerIndex){
        const GpuTaskId producerTask{ producerIndices[producerIndex], task.id.generation };
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
        const GpuTaskId consumerTask{ consumerIndices[consumerIndex], task.id.generation };
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
    for(usize edgeIndex = 0u; edgeIndex < analysis.inferredEdges().size(); ++edgeIndex){
        const GpuTaskDependencyEdge& edge = analysis.inferredEdges()[edgeIndex];
        if(
            (
                edge.hazard == GpuTaskHazardType::VersionDependency
                || edge.hazard == GpuTaskHazardType::VersionLifetime
            )
            || (edge.producer != task.id && edge.consumer != task.id)
            || !edge.resource.valid()
        )
            continue;

        bool alreadyCounted = false;
        for(usize previousIndex = 0u; previousIndex < edgeIndex; ++previousIndex){
            const GpuTaskDependencyEdge& previous = analysis.inferredEdges()[previousIndex];
            if(
                previous.hazard != GpuTaskHazardType::VersionDependency
                && previous.hazard != GpuTaskHazardType::VersionLifetime
                && previous.producer == edge.producer
                && previous.consumer == edge.consumer
                && previous.resource == edge.resource
            ){
                alreadyCounted = true;
                break;
            }
        }
        if(alreadyCounted)
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

