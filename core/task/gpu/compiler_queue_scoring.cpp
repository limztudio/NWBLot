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


[[nodiscard]] static i32 SaturateQueueScoreTerm(const u64 value)noexcept{
    return value > static_cast<u64>(Limit<i32>::s_Max)
        ? Limit<i32>::s_Max
        : static_cast<i32>(value)
    ;
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
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphQueueTopology& topology,
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
        const GpuPhysicalQueueInfo* const primaryGraphics = FindBestCompatibleQueue(
            topology,
            GpuQueueCapability::Graphics,
            CommandQueue::Graphics
        );
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


bool BuildGpuTaskSchedulingReachability(
    const GpuTaskGraph::DeclarationReadView& graph,
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


u64 GpuTaskQueueScoringData::externalQueueLoad(const GpuPhysicalQueueId& queue)const noexcept{
    if(!queueLoads)
        return 0u;
    for(usize queueLoadIndex = 0u; queueLoadIndex < queueLoadCount; ++queueLoadIndex){
        const GpuTaskQueueLoad& externalLoad = queueLoads[queueLoadIndex];
        if(externalLoad.queue == queue)
            return externalLoad.estimatedCost;
    }
    return 0u;
}

GpuTaskQueueScoringData::GpuTaskQueueScoringData(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskGraphQueueAssignmentOptions& options,
    Alloc::ScratchArena& scratchArena)
    : taskCosts(graph.taskCount(), scratchArena)
    , ownershipEdgeOffsets(graph.taskCount() + 1u, 0u, scratchArena)
    , ownershipEdges(scratchArena)
    , queueLoads(options.queueLoads)
    , queueLoadCount(options.queueLoadCount)
{
    for(usize taskIndex = 0u; taskIndex < taskCosts.size(); ++taskIndex)
        taskCosts[taskIndex] = QueueCostWeight(graph.taskAt(taskIndex).scheduling.cost);

    // Hazard kinds and ranges remain available in analysis for diagnostics.
    // Queue ownership scoring counts each producer/consumer/resource only once, so build that immutable index once for all candidate evaluations.
    Vector<const GpuTaskDependencyEdge*, Alloc::ScratchArena> uniqueEdges(scratchArena);
    uniqueEdges.reserve(analysis.inferredEdges().size());
    for(const GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        if(
            edge.hazard == GpuTaskHazardType::VersionDependency
            || edge.hazard == GpuTaskHazardType::VersionLifetime
            || !edge.resource.valid()
        )
            continue;
        uniqueEdges.push_back(&edge);
    }
    Sort(uniqueEdges.begin(), uniqueEdges.end(), [](const GpuTaskDependencyEdge* lhs, const GpuTaskDependencyEdge* rhs){
        if(lhs->producer.index != rhs->producer.index)
            return lhs->producer.index < rhs->producer.index;
        if(lhs->consumer.index != rhs->consumer.index)
            return lhs->consumer.index < rhs->consumer.index;
        return lhs->resource.index < rhs->resource.index;
    });

    usize uniqueEdgeCount = 0u;
    for(const GpuTaskDependencyEdge* const edge : uniqueEdges){
        if(uniqueEdgeCount != 0u){
            const GpuTaskDependencyEdge& previous = *uniqueEdges[uniqueEdgeCount - 1u];
            if(previous.producer == edge->producer && previous.consumer == edge->consumer && previous.resource == edge->resource)
                continue;
        }
        uniqueEdges[uniqueEdgeCount] = edge;
        ++uniqueEdgeCount;
        ++ownershipEdgeOffsets[edge->producer.index + 1u];
        ++ownershipEdgeOffsets[edge->consumer.index + 1u];
    }
    uniqueEdges.resize(uniqueEdgeCount);
    for(usize taskIndex = 1u; taskIndex < ownershipEdgeOffsets.size(); ++taskIndex)
        ownershipEdgeOffsets[taskIndex] += ownershipEdgeOffsets[taskIndex - 1u];

    ownershipEdges.resize(ownershipEdgeOffsets.back());
    Vector<usize, Alloc::ScratchArena> writeOffsets(graph.taskCount(), scratchArena);
    for(usize taskIndex = 0u; taskIndex < writeOffsets.size(); ++taskIndex)
        writeOffsets[taskIndex] = ownershipEdgeOffsets[taskIndex];
    for(const GpuTaskDependencyEdge* const edge : uniqueEdges){
        ownershipEdges[writeOffsets[edge->producer.index]] = edge;
        ownershipEdges[writeOffsets[edge->consumer.index]] = edge;
        ++writeOffsets[edge->producer.index];
        ++writeOffsets[edge->consumer.index];
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuQueueAssignmentScore BuildQueueAssignmentScore(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GraphicsVector<u32>& assignmentIndicesByTask,
    const GpuTaskGraphQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& candidate,
    const usize ignoredAssignmentOffset,
    const usize ignoredAssignmentCount)noexcept{
    GpuQueueAssignmentScore score;

    u64 overlap = 0u;
    u64 queueLoad = 0u;
    for(usize assignmentIndex = 0u; assignmentIndex < assignments.size(); ++assignmentIndex){
        if(
            assignmentIndex >= ignoredAssignmentOffset
            && assignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount
        )
            continue;
        const GpuTaskQueueAssignment& assignment = assignments[assignmentIndex];
        if(assignment.task == task.id)
            continue;

        const u64 cost = scoringData.taskCosts[assignment.task.index];
        if(assignment.queue == candidate.id)
            queueLoad = queueLoad > Limit<u64>::s_Max - cost ? Limit<u64>::s_Max : queueLoad + cost;
        if(
            ignoredAssignmentCount <= 1u
            && task.scheduling.overlapPreferred
            && !task.scheduling.avoidQueueCrossing
            && assignment.queue != candidate.id
            && schedulingReachability.transitivelyIndependent(task.id, assignment.task)
        )
            overlap = overlap > Limit<u64>::s_Max - cost ? Limit<u64>::s_Max : overlap + cost;
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
        usize edgeIndex = scoringData.ownershipEdgeOffsets[task.id.index];
        edgeIndex < scoringData.ownershipEdgeOffsets[task.id.index + 1u];
        ++edgeIndex
    ){
        const GpuTaskDependencyEdge& edge = *scoringData.ownershipEdges[edgeIndex];
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


bool IsBetterAnyQueueAssignmentCandidate(
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

