// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <global/bit.h>
#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const GpuPhysicalQueueInfo* FindPhysicalQueueInfo(const GpuPhysicalQueueTopology& topology, const GpuPhysicalQueueId& queueID)noexcept{
    if(queueID.index < topology.queueCount && topology.queues[queueID.index].id == queueID)
        return &topology.queues[queueID.index];
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(queue.id == queueID)
            return &queue;
    }
    return nullptr;
}


void GpuTaskQueueLegalityCache::build(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology){
    taskLegalQueueMasks.clear();
    if(topology.queueCount == 0u || topology.queueCount > s_BitsPerWord){
        active = false;
        return;
    }

    active = true;
    const u64 allMask = topology.queueCount == s_BitsPerWord ? ~static_cast<u64>(0u) : ((static_cast<u64>(1u) << topology.queueCount) - 1u);
    Alloc::ScratchArena& scratchArena = taskLegalQueueMasks.get_allocator().arena();
    Vector<u64, Alloc::ScratchArena> resourceLegalMasks(graph.resourceCount(), allMask, scratchArena);
    for(usize resourceIndex = 0u; resourceIndex < graph.resourceCount(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceIndex);
        if(resource.type == GpuGraphResourceType::HazardDomain)
            continue;

        u64 rMask = allMask;
        if(resource.directConsumerQueue.valid()){
            const GpuPhysicalQueueInfo* const consumerQueue = FindPhysicalQueueInfo(topology, resource.directConsumerQueue);
            if(!consumerQueue || !ResourceSharingAdmitsQueue(resource, topology, *consumerQueue))
                rMask = 0u;
            else if(!ResourceUsesConcurrentQueueSharing(resource, topology)){
                u64 familyMask = 0u;
                for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
                    if(topology.queues[queueIndex].familyIndex == consumerQueue->familyIndex)
                        familyMask |= BitMask<u64>(static_cast<u32>(queueIndex));
                }
                rMask &= familyMask;
            }
        }
        if(rMask != 0u && ResourceUsesConcurrentQueueSharing(resource, topology)){
            u64 concurrentMask = 0u;
            for(u64 remaining = rMask; remaining != 0u; remaining &= remaining - 1u){
                const u32 queueIndex = static_cast<u32>(CountTrailingZeros(remaining));
                if(ResourceSharingAdmitsQueue(resource, topology, topology.queues[queueIndex]))
                    concurrentMask |= BitMask<u64>(queueIndex);
            }
            rMask = concurrentMask;
        }
        resourceLegalMasks[resourceIndex] = rMask;
    }

    const GpuPresentEndpoint* const presentEndpoint = graph.presentEndpoint();
    const GpuPhysicalQueueInfo* const primaryGraphics = FindDefaultGraphicsQueue(topology);
    const u64 primaryGraphicsMask = primaryGraphics
        ? BitMask<u64>(static_cast<u32>(static_cast<usize>(primaryGraphics - topology.queues)))
        : 0u
    ;

    taskLegalQueueMasks.resize(graph.taskCount(), 0u);
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        u64 mask = allMask;
        if(task.commands.externalQueue.valid()){
            u64 extMask = 0u;
            for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
                if(topology.queues[queueIndex].id == task.commands.externalQueue){
                    extMask = BitMask<u64>(static_cast<u32>(queueIndex));
                    break;
                }
            }
            mask &= extMask;
        }
        if(task.commands.requiresPrimaryGraphicsQueue || (presentEndpoint && presentEndpoint->producer == task.id))
            mask &= primaryGraphicsMask;

        if(mask != 0u){
            u64 capMask = 0u;
            for(u64 remaining = mask; remaining != 0u; remaining &= remaining - 1u){
                const u32 queueIndex = static_cast<u32>(CountTrailingZeros(remaining));
                const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
                if(
                    HasCapabilities(candidate.capabilities, task.commands.requiredCapabilities)
                    && (task.commands.alternativeCapabilities == GpuQueueCapability::None || (candidate.capabilities & task.commands.alternativeCapabilities) != GpuQueueCapability::None)
                    && (task.commands.additionalAlternativeCapabilities == GpuQueueCapability::None || (candidate.capabilities & task.commands.additionalAlternativeCapabilities) != GpuQueueCapability::None)
                )
                    capMask |= BitMask<u64>(queueIndex);
            }
            mask = capMask;
        }

        if(mask != 0u){
            for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
                const GpuGraphResourceId resourceId = task.resourceUses[useIndex].resource;
                if(presentEndpoint && resourceId == presentEndpoint->backBuffer)
                    mask &= primaryGraphicsMask;
                mask &= resourceLegalMasks[resourceId.index];
                if(mask == 0u)
                    break;
            }
        }
        taskLegalQueueMasks[taskIndex] = mask;
    }
}

b8 GpuTaskQueueLegalityCache::isLegal(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology, const GpuTaskGraphTaskView& task, const usize queueIndex)const noexcept{
    if(active)
        return (taskLegalQueueMasks[task.id.index] & BitMask<u64>(static_cast<u32>(queueIndex))) != 0u;
    return IsLegalQueueAssignmentCandidate(graph, topology, task, topology.queues[queueIndex]);
}


b8 IsLegalQueueAssignmentCandidate(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology, const GpuTaskGraphTaskView& task, const GpuPhysicalQueueInfo& candidate)noexcept{
    if(
        !HasCapabilities(candidate.capabilities, task.commands.requiredCapabilities)
        || (task.commands.externalQueue.valid() && task.commands.externalQueue != candidate.id)
        || (task.commands.alternativeCapabilities != GpuQueueCapability::None && (candidate.capabilities & task.commands.alternativeCapabilities) == GpuQueueCapability::None)
        || (task.commands.additionalAlternativeCapabilities != GpuQueueCapability::None && (candidate.capabilities & task.commands.additionalAlternativeCapabilities) == GpuQueueCapability::None)
    )
        return false;

    const GpuPresentEndpoint* const presentEndpoint = graph.presentEndpoint();
    b8 requiresPrimaryGraphicsQueue = task.commands.requiresPrimaryGraphicsQueue || (presentEndpoint && presentEndpoint->producer == task.id);
    if(presentEndpoint){
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex)
            requiresPrimaryGraphicsQueue = requiresPrimaryGraphicsQueue || task.resourceUses[useIndex].resource == presentEndpoint->backBuffer;
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
            if(!consumerQueue || !ResourceSharingAdmitsQueue(resource, topology, *consumerQueue) || (!ResourceUsesConcurrentQueueSharing(resource, topology) && consumerQueue->familyIndex != candidate.familyIndex))
                return false;
        }
        if(ResourceUsesConcurrentQueueSharing(resource, topology) && !ResourceSharingAdmitsQueue(resource, topology, candidate))
            return false;
    }
    return true;
}


const GpuTaskQueueAssignment* FindQueueAssignment(const Span<const GpuTaskQueueAssignment> assignments, const Span<const u32> assignmentIndicesByTask, const GpuTaskId& task)noexcept{
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
    const Span<const GpuTaskQueueAssignment> assignments,
    const Span<const u32> assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskGraphTaskView& task,
    const GpuPhysicalQueueInfo& candidate,
    const GpuTaskQueueScoreExclusions& exclusions,
    const b8 computeOverlap
)noexcept{
    GpuQueueAssignmentScore score;
    const usize ignoredAssignmentOffset = exclusions.assignmentOffset;
    const usize ignoredAssignmentCount = exclusions.assignmentCount;
    const u64 assignedQueueCost = scoringData.assignedQueueLoad(candidate.id);
    FOUNDATION_ASSERT(assignedQueueCost >= exclusions.candidateQueueCost);
    u64 queueLoad = assignedQueueCost - exclusions.candidateQueueCost;
    const u32 taskAssignmentIndex = assignmentIndicesByTask[task.id.index];
    const b8 taskExcluded = taskAssignmentIndex >= ignoredAssignmentOffset && taskAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount;
    const GpuTaskQueueAssignment* const ownAssignment = FindQueueAssignment(assignments, assignmentIndicesByTask, task.id);
    const u64 ownCost = ownAssignment && !taskExcluded ? scoringData.taskCosts[task.id.index] : 0u;
    if(ownAssignment && ownAssignment->queue == candidate.id)
        queueLoad -= ownCost;

    u64 overlap = 0u;
    if(computeOverlap && ignoredAssignmentCount <= 1u && task.scheduling.overlapPreferred && !task.scheduling.avoidQueueCrossing && schedulingReachability.mayContainIndependentTasks()){
        u64 otherQueueCost = scoringData.totalAssignedCost - assignedQueueCost - (exclusions.totalCost - exclusions.candidateQueueCost);
        if(ownAssignment && ownAssignment->queue != candidate.id)
            otherQueueCost -= ownCost;
        if(analysis.schedulingEdges().empty())
            overlap = otherQueueCost;
        else if(otherQueueCost != 0u){
            const GpuTaskQueueAssignment unassignedTask{ .task = task.id, .initialQueue = {}, .queue = candidate.id, .score = {} };
            overlap = scoringData.independentQueueCost(schedulingReachability, candidate.id, Span<const GpuTaskQueueAssignment>(ownAssignment ? ownAssignment : &unassignedTask, 1u));
            if(ignoredAssignmentCount == 1u){
                const GpuTaskQueueAssignment& excluded = assignments[ignoredAssignmentOffset];
                if(excluded.queue != candidate.id && schedulingReachability.transitivelyIndependent(task.id, excluded.task))
                    overlap -= scoringData.taskCosts[excluded.task.index];
            }
        }
    }
    const u64 externalQueueLoad = scoringData.externalQueueLoad(candidate.id);
    queueLoad = AddSaturating<u64>(queueLoad, externalQueueLoad);
    score.overlap = SaturateU64ToI32(overlap);
    score.queueLoad = SaturateU64ToI32(queueLoad);

    u64 incomingCrossings = 0u;
    u64 outgoingCrossings = 0u;
    const GpuTaskGraphSchedulingTaskIndexView producerIndices = analysis.schedulingProducers(task.id);
    for(usize producerIndex = 0u; producerIndex < producerIndices.taskCount; ++producerIndex){
        const GpuTaskId producerTask{ .generation = task.id.generation, .index = static_cast<u32>(producerIndices[producerIndex]) };
        const u32 producerAssignmentIndex = assignmentIndicesByTask[producerTask.index];
        if(producerAssignmentIndex >= ignoredAssignmentOffset && producerAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount)
            continue;
        const GpuTaskQueueAssignment* const producer = FindQueueAssignment(assignments, assignmentIndicesByTask, producerTask);
        if(producer && producer->queue != candidate.id)
            ++incomingCrossings;
    }
    const GpuTaskGraphSchedulingTaskIndexView consumerIndices = analysis.schedulingConsumers(task.id);
    for(usize consumerIndex = 0u; consumerIndex < consumerIndices.taskCount; ++consumerIndex){
        const GpuTaskId consumerTask{ .generation = task.id.generation, .index = static_cast<u32>(consumerIndices[consumerIndex]) };
        const u32 consumerAssignmentIndex = assignmentIndicesByTask[consumerTask.index];
        if(consumerAssignmentIndex >= ignoredAssignmentOffset && consumerAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount)
            continue;
        const GpuTaskQueueAssignment* const consumer = FindQueueAssignment(assignments, assignmentIndicesByTask, consumerTask);
        if(consumer && consumer->queue != candidate.id)
            ++outgoingCrossings;
    }
    score.incomingCrossings = SaturateU64ToI32(incomingCrossings);
    score.outgoingCrossings = SaturateU64ToI32(outgoingCrossings);

    u64 ownershipTransfers = 0u;
    for(usize edgeIndex = scoringData.ownershipEdgeOffsets[task.id.index]; edgeIndex < scoringData.ownershipEdgeOffsets[task.id.index + 1u]; ++edgeIndex){
        const GpuTaskQueueScoringData::OwnershipEdge& edge = scoringData.ownershipEdges[edgeIndex];
        const u32 producerAssignmentIndex = assignmentIndicesByTask[edge.producerIndex];
        const u32 consumerAssignmentIndex = assignmentIndicesByTask[edge.consumerIndex];
        if(
            producerAssignmentIndex >= ignoredAssignmentOffset
            && producerAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount
            && consumerAssignmentIndex >= ignoredAssignmentOffset
            && consumerAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount
        )
            continue;
        const GpuPhysicalQueueInfo* const producerQueue = edge.producerIndex == task.id.index
            ? &candidate
            : producerAssignmentIndex < assignments.size()
                ? FindPhysicalQueueInfo(topology, assignments[producerAssignmentIndex].queue)
                : nullptr
        ;
        const GpuPhysicalQueueInfo* const consumerQueue = edge.consumerIndex == task.id.index
            ? &candidate
            : consumerAssignmentIndex < assignments.size()
                ? FindPhysicalQueueInfo(topology, assignments[consumerAssignmentIndex].queue)
                : nullptr
        ;
        if(!producerQueue || !consumerQueue || producerQueue->familyIndex == consumerQueue->familyIndex)
            continue;

        const GpuTaskGraphResourceView resource = graph.resourceAt(edge.resourceIndex);
        if(resource.type == GpuGraphResourceType::HazardDomain)
            continue;

        const b8 concurrentQueuePair = ResourceSharesQueuePairConcurrently(resource, topology, *producerQueue, *consumerQueue);
        if(!concurrentQueuePair)
            ++ownershipTransfers;
    }
    score.ownershipTransfers = SaturateU64ToI32(ownershipTransfers);
    return score;
}


b8 IsBetterAutomaticQueueAssignmentCandidate(const GpuQueueAssignmentScore& candidateScore, const GpuPhysicalQueueInfo& candidate, const GpuQueueAssignmentScore& currentScore, const GpuPhysicalQueueInfo* const current, const b8 compareTotalScore)noexcept{
    if(!current)
        return true;

    if(compareTotalScore){
        const auto total = [](const GpuQueueAssignmentScore& score)noexcept{
            return static_cast<i64>(score.overlap) - static_cast<i64>(score.queueLoad) - static_cast<i64>(score.incomingCrossings) - static_cast<i64>(score.outgoingCrossings) - static_cast<i64>(score.ownershipTransfers);
        };
        const i64 candidateTotal = total(candidateScore);
        const i64 currentTotal = total(currentScore);
        if(candidateTotal != currentTotal)
            return candidateTotal > currentTotal;
    }

    const i64 candidateCrossings = static_cast<i64>(candidateScore.incomingCrossings) + static_cast<i64>(candidateScore.outgoingCrossings);
    const i64 currentCrossings = static_cast<i64>(currentScore.incomingCrossings) + static_cast<i64>(currentScore.outgoingCrossings);
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

