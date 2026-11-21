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


void GpuTaskQueueLegalityCache::build(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology){
    taskLegalQueueMasks.clear();
    classQueueMasks.fill(0u);
    orderedQueueIndicesByClassCount.fill(0u);
    orderedAllQueueCount = 0u;
    primaryGraphicsMask = 0u;
    if(topology.queueCount == 0u || topology.queueCount > s_BitsPerWord){
        active = false;
        return;
    }

    active = true;
    orderedAllQueueCount = static_cast<u8>(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const u8 key = static_cast<u8>(queueIndex);
        const GpuPhysicalQueueInfo& keyInfo = topology.queues[key];
        u8 j = static_cast<u8>(queueIndex);
        while(j > 0u && IsBetterQueue(keyInfo, &topology.queues[orderedAllQueueIndices[j - 1u]])){
            orderedAllQueueIndices[j] = orderedAllQueueIndices[j - 1u];
            --j;
        }
        orderedAllQueueIndices[j] = key;
    }
    const u64 allMask = topology.queueCount == s_BitsPerWord ? ~static_cast<u64>(0u) : ((static_cast<u64>(1u) << topology.queueCount) - 1u);
    Array<u64, 8u> capRequiredMask{};
    Array<u64, 8u> capAnyMask{};
    for(u8 req = 0u; req < 8u; ++req){
        u64 rMask = 0u;
        u64 aMask = req == 0u ? allMask : 0u;
        for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
            const u8 caps = static_cast<u8>(topology.queues[queueIndex].capabilities);
            if((caps & req) == req)
                rMask |= BitMask<u64>(static_cast<u32>(queueIndex));
            if(req != 0u && (caps & req) != 0u)
                aMask |= BitMask<u64>(static_cast<u32>(queueIndex));
        }
        capRequiredMask[req] = rMask;
        capAnyMask[req] = aMask;
    }

    for(u8 cls = 0u; cls < CommandQueue::kCount; ++cls){
        u8& count = orderedQueueIndicesByClassCount[cls];
        auto& ordered = orderedQueueIndicesByClass[cls];
        u64 clsMask = 0u;
        for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
            if(topology.queues[queueIndex].queueClass != static_cast<CommandQueue::Enum>(cls))
                continue;
            clsMask |= BitMask<u64>(static_cast<u32>(queueIndex));
            ordered[count++] = static_cast<u8>(queueIndex);
        }
        classQueueMasks[cls] = clsMask;
        for(u8 i = 1u; i < count; ++i){
            const u8 key = ordered[i];
            const GpuPhysicalQueueInfo& keyInfo = topology.queues[key];
            u8 j = i;
            while(j > 0u && IsBetterQueue(keyInfo, &topology.queues[ordered[j - 1u]])){
                ordered[j] = ordered[j - 1u];
                --j;
            }
            ordered[j] = key;
        }
    }

    const GpuPresentEndpoint* const presentEndpoint = CompilerGraphAccess::PresentEndpoint(graph);
    const GpuPhysicalQueueInfo* const primaryGraphics = FindDefaultGraphicsQueue(topology);
    primaryGraphicsMask = primaryGraphics
        ? BitMask<u64>(static_cast<u32>(static_cast<usize>(primaryGraphics - topology.queues)))
        : 0u
    ;

    const usize taskCount = CompilerGraphAccess::TaskCount(graph);
    const usize resourceCount = CompilerGraphAccess::ResourceCount(graph);
    taskLegalQueueMasks.resize(taskCount, 0u);
    Array<u64, 64u> sameFamilyMaskByQueue{};
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const u32 familyIndex = topology.queues[queueIndex].familyIndex;
        u64 familyMask = 0u;
        for(usize otherQueueIndex = 0u; otherQueueIndex < topology.queueCount; ++otherQueueIndex){
            if(topology.queues[otherQueueIndex].familyIndex == familyIndex)
                familyMask |= BitMask<u64>(static_cast<u32>(otherQueueIndex));
        }
        sameFamilyMaskByQueue[queueIndex] = familyMask;
    }
    const TopologySharingLut sharingLut = TopologySharingLut::Build(topology);
    Alloc::ScratchArena& scratchArena = taskLegalQueueMasks.get_allocator().arena();
    Vector<u64, Alloc::ScratchArena> resourceLegalMasks(resourceCount, allMask, scratchArena);
    for(usize resourceIndex = 0u; resourceIndex < resourceCount; ++resourceIndex){
        const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, resourceIndex);
        if(resource.type == GpuGraphResourceType::HazardDomain)
            continue;

        u64 rMask = allMask;
        if(!resource.hasQueueAdmission){
            const b8 usesConcurrent = sharingLut.logicalUsesConcurrent(resource.queueSharing);
            const u64 admittedMask = sharingLut.logicalAdmittedMask(resource.queueSharing);
            if(resource.directConsumerQueue.valid()){
                const GpuPhysicalQueueInfo* const consumerQueue = FindPhysicalQueueInfo(topology, resource.directConsumerQueue);
                if(!consumerQueue)
                    rMask = 0u;
                else{
                    const usize consumerIndex = static_cast<usize>(consumerQueue - topology.queues);
                    if((admittedMask & BitMask<u64>(static_cast<u32>(consumerIndex))) == 0u)
                        rMask = 0u;
                    else if(!usesConcurrent)
                        rMask &= sameFamilyMaskByQueue[consumerIndex];
                }
            }
            if(rMask != 0u && usesConcurrent)
                rMask &= admittedMask;
        }
        else{
            if(resource.directConsumerQueue.valid()){
                const GpuPhysicalQueueInfo* const consumerQueue = FindPhysicalQueueInfo(topology, resource.directConsumerQueue);
                if(!consumerQueue || !ResourceNodeSharingAdmitsQueue(graph, resource, topology, *consumerQueue))
                    rMask = 0u;
                else if(!ResourceNodeUsesConcurrentQueueSharing(graph, resource, topology))
                    rMask &= sameFamilyMaskByQueue[static_cast<usize>(consumerQueue - topology.queues)];
            }
            if(rMask != 0u && ResourceNodeUsesConcurrentQueueSharing(graph, resource, topology)){
                u64 concurrentMask = 0u;
                for(u64 remaining = rMask; remaining != 0u; remaining &= remaining - 1u){
                    const u32 queueIndex = static_cast<u32>(CountTrailingZeros(remaining));
                    if(ResourceNodeSharingAdmitsQueue(graph, resource, topology, topology.queues[queueIndex]))
                        concurrentMask |= BitMask<u64>(queueIndex);
                }
                rMask = concurrentMask;
            }
        }
        resourceLegalMasks[resourceIndex] = rMask;
    }

    const u64 generation = CompilerGraphAccess::Generation(graph);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, taskIndex);
        u64 mask = allMask;
        if(task.commands.externalQueue.valid()){
            const GpuPhysicalQueueInfo* const extQueue = FindPhysicalQueueInfo(topology, task.commands.externalQueue);
            mask &= extQueue ? BitMask<u64>(static_cast<u32>(static_cast<usize>(extQueue - topology.queues))) : 0u;
        }
        if(task.commands.requiresPrimaryGraphicsQueue || (presentEndpoint && presentEndpoint->producer.generation == generation && presentEndpoint->producer.index == taskIndex))
            mask &= primaryGraphicsMask;

        mask &= capRequiredMask[static_cast<u8>(task.commands.requiredCapabilities) & 7u];
        mask &= capAnyMask[static_cast<u8>(task.commands.alternativeCapabilities) & 7u];
        mask &= capAnyMask[static_cast<u8>(task.commands.additionalAlternativeCapabilities) & 7u];

        if(mask != 0u){
            for(const GpuTaskResourceUse& use : CompilerGraphAccess::TaskResourceUses(graph, task)){
                const GpuGraphResourceId resourceId = use.resource;
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

b8 GpuTaskQueueLegalityCache::isLegal(const GpuTaskGraph::DeclarationReadView& graph, const GpuPhysicalQueueTopology& topology, const GpuTaskId taskId, const usize queueIndex)const noexcept{
    if(active)
        return (taskLegalQueueMasks[taskId.index] & BitMask<u64>(static_cast<u32>(queueIndex))) != 0u;
    return IsLegalQueueAssignmentCandidate(graph, topology, graph.taskAt(taskId.index), topology.queues[queueIndex]);
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
        const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, task.resourceUses[useIndex].resource.index);
        if(resource.type == GpuGraphResourceType::HazardDomain)
            continue;
        if(resource.directConsumerQueue.valid()){
            const GpuPhysicalQueueInfo* const consumerQueue = FindPhysicalQueueInfo(topology, resource.directConsumerQueue);
            if(!consumerQueue || !ResourceNodeSharingAdmitsQueue(graph, resource, topology, *consumerQueue) || (!ResourceNodeUsesConcurrentQueueSharing(graph, resource, topology) && consumerQueue->familyIndex != candidate.familyIndex))
                return false;
        }
        if(ResourceNodeUsesConcurrentQueueSharing(graph, resource, topology) && !ResourceNodeSharingAdmitsQueue(graph, resource, topology, candidate))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuQueueAssignmentScore BuildTaskQueueAssignmentScore(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const Span<const GpuTaskQueueAssignment> assignments,
    const Span<const u32> assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskId taskId,
    const GpuTaskSchedulingHint& taskScheduling,
    const GpuPhysicalQueueInfo& candidate,
    const GpuTaskQueueScoreExclusions& exclusions,
    const b8 computeOverlap
)noexcept{
    GpuQueueAssignmentScore score;
    const usize ignoredAssignmentOffset = exclusions.assignmentOffset;
    const usize ignoredAssignmentCount = exclusions.assignmentCount;
    const b8 hasTopologyCandidateIndex = topology.queues && &candidate >= topology.queues && &candidate < topology.queues + topology.queueCount;
    const usize candidateQueueIndex = hasTopologyCandidateIndex
        ? static_cast<usize>(&candidate - topology.queues)
        : scoringData.findAssignedQueueIndex(candidate.id)
    ;
    const usize assignedQueueIndex = hasTopologyCandidateIndex && scoringData.assignedQueueLoads.size() == topology.queueCount
        ? candidateQueueIndex
        : scoringData.findAssignedQueueIndex(candidate.id)
    ;
    const u64 assignedQueueCost = assignedQueueIndex < scoringData.assignedQueueLoads.size()
        ? scoringData.assignedQueueLoads[assignedQueueIndex].estimatedCost
        : 0u
    ;
    FOUNDATION_ASSERT(assignedQueueCost >= exclusions.candidateQueueCost);
    u64 queueLoad = assignedQueueCost - exclusions.candidateQueueCost;
    const u32 taskAssignmentIndex = assignmentIndicesByTask[taskId.index];
    const b8 taskExcluded = taskAssignmentIndex >= ignoredAssignmentOffset && taskAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount;
    const GpuTaskQueueAssignment* const ownAssignment = FindQueueAssignment(assignments, assignmentIndicesByTask, taskId);
    const u64 ownCost = ownAssignment && !taskExcluded ? scoringData.taskCosts[taskId.index] : 0u;
    if(ownAssignment && ownAssignment->queue == candidate.id)
        queueLoad -= ownCost;

    u64 overlap = 0u;
    if(computeOverlap && ignoredAssignmentCount <= 1u && taskScheduling.overlapPreferred && !taskScheduling.avoidQueueCrossing && schedulingReachability.mayContainIndependentTasks()){
        u64 otherQueueCost = scoringData.totalAssignedCost - assignedQueueCost - (exclusions.totalCost - exclusions.candidateQueueCost);
        if(ownAssignment && ownAssignment->queue != candidate.id)
            otherQueueCost -= ownCost;
        if(analysis.schedulingEdges().empty())
            overlap = otherQueueCost;
        else if(otherQueueCost != 0u){
            const GpuTaskQueueAssignment unassignedTask{ .task = taskId, .initialQueue = {}, .queue = candidate.id, .score = {} };
            overlap = scoringData.independentQueueCostByIndex(schedulingReachability, assignedQueueIndex, Span<const GpuTaskQueueAssignment>(ownAssignment ? ownAssignment : &unassignedTask, 1u));
            if(ignoredAssignmentCount == 1u){
                const GpuTaskQueueAssignment& excluded = assignments[ignoredAssignmentOffset];
                if(excluded.queue != candidate.id && schedulingReachability.transitivelyIndependent(taskId, excluded.task))
                    overlap -= scoringData.taskCosts[excluded.task.index];
            }
        }
    }
    const u64 externalQueueLoad = hasTopologyCandidateIndex && !scoringData.externalQueueLoadsByIndex.empty()
        ? scoringData.externalQueueLoadByIndex(candidateQueueIndex)
        : scoringData.externalQueueLoad(candidate.id)
    ;
    queueLoad = AddSaturating<u64>(queueLoad, externalQueueLoad);
    score.overlap = SaturateU64ToI32(overlap);
    score.queueLoad = SaturateU64ToI32(queueLoad);

    u64 incomingCrossings = 0u;
    u64 outgoingCrossings = 0u;
    const GpuTaskGraphSchedulingTaskIndexView producerIndices = CompilerGraphAccess::SchedulingProducers(analysis, taskId.index);
    for(usize producerIndex = 0u; producerIndex < producerIndices.taskCount; ++producerIndex){
        const u32 producerTaskIndex = producerIndices[producerIndex];
        const u32 producerAssignmentIndex = assignmentIndicesByTask[producerTaskIndex];
        if(producerAssignmentIndex >= ignoredAssignmentOffset && producerAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount)
            continue;
        if(producerAssignmentIndex < assignments.size() && assignments[producerAssignmentIndex].queue != candidate.id)
            ++incomingCrossings;
    }
    const GpuTaskGraphSchedulingTaskIndexView consumerIndices = CompilerGraphAccess::SchedulingConsumers(analysis, taskId.index);
    for(usize consumerIndex = 0u; consumerIndex < consumerIndices.taskCount; ++consumerIndex){
        const u32 consumerTaskIndex = consumerIndices[consumerIndex];
        const u32 consumerAssignmentIndex = assignmentIndicesByTask[consumerTaskIndex];
        if(consumerAssignmentIndex >= ignoredAssignmentOffset && consumerAssignmentIndex - ignoredAssignmentOffset < ignoredAssignmentCount)
            continue;
        if(consumerAssignmentIndex < assignments.size() && assignments[consumerAssignmentIndex].queue != candidate.id)
            ++outgoingCrossings;
    }
    score.incomingCrossings = SaturateU64ToI32(incomingCrossings);
    score.outgoingCrossings = SaturateU64ToI32(outgoingCrossings);

    u64 ownershipTransfers = 0u;
    const b8 useFastMask = hasTopologyCandidateIndex && scoringData.hasResourceConcurrentMasks && scoringData.assignedQueueIndexByTask.size() == CompilerGraphAccess::TaskCount(graph);
    for(usize edgeIndex = scoringData.ownershipEdgeOffsets[taskId.index]; edgeIndex < scoringData.ownershipEdgeOffsets[taskId.index + 1u]; ++edgeIndex){
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
        if(useFastMask){
            const usize producerQ = edge.producerIndex == taskId.index ? candidateQueueIndex : scoringData.assignedQueueIndexByTask[edge.producerIndex];
            const usize consumerQ = edge.consumerIndex == taskId.index ? candidateQueueIndex : scoringData.assignedQueueIndexByTask[edge.consumerIndex];
            if(producerQ >= topology.queueCount || consumerQ >= topology.queueCount)
                continue;
            if(scoringData.queueFamilyByIndex[producerQ] == scoringData.queueFamilyByIndex[consumerQ])
                continue;
            const u64 pairMask = BitMask<u64>(static_cast<u32>(producerQ)) | BitMask<u64>(static_cast<u32>(consumerQ));
            if((scoringData.resourceConcurrentAdmittedMasks[edge.resourceIndex] & pairMask) != pairMask)
                ++ownershipTransfers;
            continue;
        }
        const GpuPhysicalQueueInfo* const producerQueue = edge.producerIndex == taskId.index
            ? &candidate
            : producerAssignmentIndex < assignments.size()
                ? FindPhysicalQueueInfo(topology, assignments[producerAssignmentIndex].queue)
                : nullptr
        ;
        const GpuPhysicalQueueInfo* const consumerQueue = edge.consumerIndex == taskId.index
            ? &candidate
            : consumerAssignmentIndex < assignments.size()
                ? FindPhysicalQueueInfo(topology, assignments[consumerAssignmentIndex].queue)
                : nullptr
        ;
        if(!producerQueue || !consumerQueue || producerQueue->familyIndex == consumerQueue->familyIndex)
            continue;

        const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, edge.resourceIndex);
        if(resource.type == GpuGraphResourceType::HazardDomain)
            continue;

        const b8 concurrentQueuePair = ResourceNodeSharesQueuePairConcurrently(graph, resource, topology, *producerQueue, *consumerQueue);
        if(!concurrentQueuePair)
            ++ownershipTransfers;
    }
    score.ownershipTransfers = SaturateU64ToI32(ownershipTransfers);
    return score;
}

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
    return BuildTaskQueueAssignmentScore(
        graph,
        analysis,
        assignments,
        assignmentIndicesByTask,
        topology,
        schedulingReachability,
        scoringData,
        task.id,
        task.scheduling,
        candidate,
        exclusions,
        computeOverlap
    );
}

void BuildSingleTaskQueueCandidateScores(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const Span<const GpuTaskQueueAssignment> assignments,
    const Span<const u32> assignmentIndicesByTask,
    const GpuPhysicalQueueTopology& topology,
    const GpuTaskSchedulingReachability& schedulingReachability,
    const GpuTaskQueueScoringData& scoringData,
    const GpuTaskId taskId,
    const GpuTaskSchedulingHint& taskScheduling,
    const GpuPhysicalQueueInfo* const* const candidates,
    GpuQueueAssignmentScore* const outScores,
    const usize candidateCount,
    const b8 computeOverlap
)noexcept{
    if(candidateCount == 0u || candidateCount > CommandQueue::kCount)
        return;

    const u32 taskAssignmentIndex = assignmentIndicesByTask[taskId.index];
    const GpuTaskQueueAssignment* const ownAssignment = FindQueueAssignment(assignments, assignmentIndicesByTask, taskId);
    const u64 ownCost = ownAssignment ? scoringData.taskCosts[taskId.index] : 0u;
    const b8 mayComputeOverlap = computeOverlap && taskScheduling.overlapPreferred && !taskScheduling.avoidQueueCrossing && schedulingReachability.mayContainIndependentTasks();
    const b8 emptySchedulingEdges = analysis.schedulingEdges().empty();
    const GpuTaskQueueAssignment unassignedTask{ .task = taskId, .initialQueue = {}, .queue = {}, .score = {} };
    const Span<const GpuTaskQueueAssignment> memberSpan(ownAssignment ? ownAssignment : &unassignedTask, 1u);

    Array<usize, CommandQueue::kCount> candidateQueueIndices{};
    Array<usize, CommandQueue::kCount> assignedQueueIndices{};
    Array<u64, CommandQueue::kCount> overlapByCandidate{};
    Array<u64, CommandQueue::kCount> queueLoadByCandidate{};
    b8 allHaveTopologyIndex = true;
    for(usize c = 0u; c < candidateCount; ++c){
        const GpuPhysicalQueueInfo& candidate = *candidates[c];
        const b8 hasTopologyCandidateIndex = topology.queues && &candidate >= topology.queues && &candidate < topology.queues + topology.queueCount;
        allHaveTopologyIndex = allHaveTopologyIndex && hasTopologyCandidateIndex;
        const usize candidateQueueIndex = hasTopologyCandidateIndex
            ? static_cast<usize>(&candidate - topology.queues)
            : scoringData.findAssignedQueueIndex(candidate.id)
        ;
        const usize assignedQueueIndex = hasTopologyCandidateIndex && scoringData.assignedQueueLoads.size() == topology.queueCount
            ? candidateQueueIndex
            : scoringData.findAssignedQueueIndex(candidate.id)
        ;
        candidateQueueIndices[c] = candidateQueueIndex;
        assignedQueueIndices[c] = assignedQueueIndex;

        const u64 assignedQueueCost = assignedQueueIndex < scoringData.assignedQueueLoads.size()
            ? scoringData.assignedQueueLoads[assignedQueueIndex].estimatedCost
            : 0u
        ;
        const u64 excludedCandidateCost = ownAssignment && ownAssignment->queue == candidate.id ? ownCost : 0u;
        FOUNDATION_ASSERT(assignedQueueCost >= excludedCandidateCost);
        u64 queueLoad = assignedQueueCost - excludedCandidateCost;
        const u64 externalQueueLoad = hasTopologyCandidateIndex && !scoringData.externalQueueLoadsByIndex.empty()
            ? scoringData.externalQueueLoadByIndex(candidateQueueIndex)
            : scoringData.externalQueueLoad(candidate.id)
        ;
        queueLoadByCandidate[c] = AddSaturating<u64>(queueLoad, externalQueueLoad);

        u64 overlap = 0u;
        if(mayComputeOverlap){
            u64 otherQueueCost = scoringData.totalAssignedCost - assignedQueueCost - (ownCost - excludedCandidateCost);
            if(emptySchedulingEdges)
                overlap = otherQueueCost;
            else if(otherQueueCost != 0u){
                overlap = scoringData.independentQueueCostByIndex(schedulingReachability, assignedQueueIndex, memberSpan);
                if(ownAssignment && ownAssignment->queue != candidate.id && schedulingReachability.transitivelyIndependent(taskId, ownAssignment->task))
                    overlap -= ownCost;
            }
        }
        overlapByCandidate[c] = overlap;
    }

    u64 totalExternalProducers = 0u;
    Array<u64, CommandQueue::kCount> matchingProducers{};
    const GpuTaskGraphSchedulingTaskIndexView producerIndices = CompilerGraphAccess::SchedulingProducers(analysis, taskId.index);
    for(usize producerIndex = 0u; producerIndex < producerIndices.taskCount; ++producerIndex){
        const u32 producerTaskIndex = producerIndices[producerIndex];
        const u32 producerAssignmentIndex = assignmentIndicesByTask[producerTaskIndex];
        if(producerAssignmentIndex == taskAssignmentIndex || producerAssignmentIndex >= assignments.size())
            continue;
        ++totalExternalProducers;
        const GpuPhysicalQueueId& producerQueue = assignments[producerAssignmentIndex].queue;
        for(usize c = 0u; c < candidateCount; ++c){
            if(producerQueue == candidates[c]->id)
                ++matchingProducers[c];
        }
    }

    u64 totalExternalConsumers = 0u;
    Array<u64, CommandQueue::kCount> matchingConsumers{};
    const GpuTaskGraphSchedulingTaskIndexView consumerIndices = CompilerGraphAccess::SchedulingConsumers(analysis, taskId.index);
    for(usize consumerIndex = 0u; consumerIndex < consumerIndices.taskCount; ++consumerIndex){
        const u32 consumerTaskIndex = consumerIndices[consumerIndex];
        const u32 consumerAssignmentIndex = assignmentIndicesByTask[consumerTaskIndex];
        if(consumerAssignmentIndex == taskAssignmentIndex || consumerAssignmentIndex >= assignments.size())
            continue;
        ++totalExternalConsumers;
        const GpuPhysicalQueueId& consumerQueue = assignments[consumerAssignmentIndex].queue;
        for(usize c = 0u; c < candidateCount; ++c){
            if(consumerQueue == candidates[c]->id)
                ++matchingConsumers[c];
        }
    }

    Array<u64, CommandQueue::kCount> ownershipTransfers{};
    const b8 useFastMask = allHaveTopologyIndex && scoringData.hasResourceConcurrentMasks && scoringData.assignedQueueIndexByTask.size() == CompilerGraphAccess::TaskCount(graph);
    for(usize edgeIndex = scoringData.ownershipEdgeOffsets[taskId.index]; edgeIndex < scoringData.ownershipEdgeOffsets[taskId.index + 1u]; ++edgeIndex){
        const GpuTaskQueueScoringData::OwnershipEdge& edge = scoringData.ownershipEdges[edgeIndex];
        if(edge.producerIndex == taskId.index && edge.consumerIndex == taskId.index)
            continue;
        if(useFastMask){
            const u32 otherTaskIndex = edge.producerIndex == taskId.index ? edge.consumerIndex : edge.producerIndex;
            const usize otherQ = scoringData.assignedQueueIndexByTask[otherTaskIndex];
            if(otherQ >= topology.queueCount)
                continue;
            const u32 otherFamily = scoringData.queueFamilyByIndex[otherQ];
            const u64 admittedMask = scoringData.resourceConcurrentAdmittedMasks[edge.resourceIndex];
            const b8 otherAdmitted = (admittedMask & BitMask<u64>(static_cast<u32>(otherQ))) != 0u;
            for(usize c = 0u; c < candidateCount; ++c){
                const usize candidateQ = candidateQueueIndices[c];
                if(candidateQ >= topology.queueCount || scoringData.queueFamilyByIndex[candidateQ] == otherFamily)
                    continue;
                if(!otherAdmitted || (admittedMask & BitMask<u64>(static_cast<u32>(candidateQ))) == 0u)
                    ++ownershipTransfers[c];
            }
            continue;
        }
        const u32 producerAssignmentIndex = assignmentIndicesByTask[edge.producerIndex];
        const u32 consumerAssignmentIndex = assignmentIndicesByTask[edge.consumerIndex];
        const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, edge.resourceIndex);
        if(resource.type == GpuGraphResourceType::HazardDomain)
            continue;
        const GpuPhysicalQueueInfo* const fixedProducer = edge.producerIndex != taskId.index && producerAssignmentIndex < assignments.size()
            ? FindPhysicalQueueInfo(topology, assignments[producerAssignmentIndex].queue)
            : nullptr
        ;
        const GpuPhysicalQueueInfo* const fixedConsumer = edge.consumerIndex != taskId.index && consumerAssignmentIndex < assignments.size()
            ? FindPhysicalQueueInfo(topology, assignments[consumerAssignmentIndex].queue)
            : nullptr
        ;
        for(usize c = 0u; c < candidateCount; ++c){
            const GpuPhysicalQueueInfo* const producerQueue = edge.producerIndex == taskId.index ? candidates[c] : fixedProducer;
            const GpuPhysicalQueueInfo* const consumerQueue = edge.consumerIndex == taskId.index ? candidates[c] : fixedConsumer;
            if(!producerQueue || !consumerQueue || producerQueue->familyIndex == consumerQueue->familyIndex)
                continue;
            if(!ResourceNodeSharesQueuePairConcurrently(graph, resource, topology, *producerQueue, *consumerQueue))
                ++ownershipTransfers[c];
        }
    }

    for(usize c = 0u; c < candidateCount; ++c){
        outScores[c].overlap = SaturateU64ToI32(overlapByCandidate[c]);
        outScores[c].queueLoad = SaturateU64ToI32(queueLoadByCandidate[c]);
        outScores[c].incomingCrossings = SaturateU64ToI32(totalExternalProducers - matchingProducers[c]);
        outScores[c].outgoingCrossings = SaturateU64ToI32(totalExternalConsumers - matchingConsumers[c]);
        outScores[c].ownershipTransfers = SaturateU64ToI32(ownershipTransfers[c]);
    }
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

