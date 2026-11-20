// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskQueueScoringData::GpuTaskQueueScoringData(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GpuTaskGraphAnalysis& analysis,
    const GpuTaskSchedulingReachability& reachability,
    const GpuTaskGraphQueueAssignmentOptions& options,
    Alloc::ScratchArena& scratchArena,
    const GpuPhysicalQueueTopology* const topology
)
    : taskCosts(graph.taskCount(), scratchArena)
    , taskCostGroups(reachability.m_words.empty() ? 0u : graph.taskCount(), scratchArena)
    , ownershipEdgeOffsets(graph.taskCount() + 1u, 0u, scratchArena)
    , ownershipEdges(scratchArena)
    , assignedQueueLoads(scratchArena)
    , assignedCostWords(scratchArena)
    , externalQueueLoads(options.queueLoads)
    , externalQueueLoadCount(options.queueLoadCount)
{
    for(usize costGroup = 0u; costGroup < GpuTaskCostHint::Count; ++costGroup)
        costGroupWeights[costGroup] = QueueCostWeight(static_cast<GpuTaskCostHint::Enum>(costGroup));
    for(usize taskIndex = 0u; taskIndex < taskCosts.size(); ++taskIndex){
        const GpuTaskSchedulingHint& scheduling = graph.taskAt(taskIndex).scheduling;
        const auto costGroup = scheduling.cost;
        if(!taskCostGroups.empty())
            taskCostGroups[taskIndex] = static_cast<u8>(costGroup);
        taskCosts[taskIndex] = scheduling.isRecoverySubmission ? 0u : costGroupWeights[costGroup];
    }

    if(topology && topology->queueCount > 0u){
        b8 singleFamily = true;
        const u32 firstFamily = topology->queues[0u].familyIndex;
        for(usize queueIndex = 1u; queueIndex < topology->queueCount; ++queueIndex){
            if(topology->queues[queueIndex].familyIndex != firstFamily){
                singleFamily = false;
                break;
            }
        }
        if(singleFamily)
            return;
    }

    Vector<OwnershipEdge, Alloc::ScratchArena> uniqueEdges(scratchArena);
    uniqueEdges.reserve(analysis.inferredEdges().size());
    for(const GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        if(edge.hazard == GpuTaskHazardType::VersionDependency || edge.hazard == GpuTaskHazardType::VersionLifetime || !edge.resource.valid())
            continue;
        uniqueEdges.push_back(OwnershipEdge{ .producerIndex = edge.producer.index, .consumerIndex = edge.consumer.index, .resourceIndex = edge.resource.index, });
    }
    if(uniqueEdges.empty())
        return;
    Sort(uniqueEdges.begin(), uniqueEdges.end(), [](const OwnershipEdge& lhs, const OwnershipEdge& rhs)noexcept{
        if(lhs.producerIndex != rhs.producerIndex)
            return lhs.producerIndex < rhs.producerIndex;
        if(lhs.consumerIndex != rhs.consumerIndex)
            return lhs.consumerIndex < rhs.consumerIndex;
        return lhs.resourceIndex < rhs.resourceIndex;
    });

    usize uniqueEdgeCount = 0u;
    for(const OwnershipEdge& edge : uniqueEdges){
        if(uniqueEdgeCount != 0u){
            const OwnershipEdge& previous = uniqueEdges[uniqueEdgeCount - 1u];
            if(previous.producerIndex == edge.producerIndex && previous.consumerIndex == edge.consumerIndex && previous.resourceIndex == edge.resourceIndex)
                continue;
        }
        uniqueEdges[uniqueEdgeCount] = edge;
        ++uniqueEdgeCount;
        ++ownershipEdgeOffsets[edge.producerIndex + 1u];
        ++ownershipEdgeOffsets[edge.consumerIndex + 1u];
    }
    uniqueEdges.erase(uniqueEdges.begin() + uniqueEdgeCount, uniqueEdges.end());
    for(usize taskIndex = 1u; taskIndex < ownershipEdgeOffsets.size(); ++taskIndex)
        ownershipEdgeOffsets[taskIndex] += ownershipEdgeOffsets[taskIndex - 1u];

    ownershipEdges.resize(ownershipEdgeOffsets.back());
    Vector<usize, Alloc::ScratchArena> writeOffsets(graph.taskCount(), scratchArena);
    for(usize taskIndex = 0u; taskIndex < writeOffsets.size(); ++taskIndex)
        writeOffsets[taskIndex] = ownershipEdgeOffsets[taskIndex];
    for(const OwnershipEdge& edge : uniqueEdges){
        ownershipEdges[writeOffsets[edge.producerIndex]] = edge;
        ownershipEdges[writeOffsets[edge.consumerIndex]] = edge;
        ++writeOffsets[edge.producerIndex];
        ++writeOffsets[edge.consumerIndex];
    }
}

usize GpuTaskQueueScoringData::findAssignedQueueIndex(const GpuPhysicalQueueId& queue)const noexcept{
    const usize count = assignedQueueLoads.size();
    if(queue.index < count && assignedQueueLoads[queue.index].queue == queue)
        return queue.index;
    for(usize queueIndex = 0u; queueIndex < count; ++queueIndex){
        if(assignedQueueLoads[queueIndex].queue == queue)
            return queueIndex;
    }
    return count;
}

u64 GpuTaskQueueScoringData::externalQueueLoad(const GpuPhysicalQueueId& queue)const noexcept{
    if(!externalQueueLoads)
        return 0u;
    for(usize queueLoadIndex = 0u; queueLoadIndex < externalQueueLoadCount; ++queueLoadIndex){
        const GpuTaskQueueLoad& externalLoad = externalQueueLoads[queueLoadIndex];
        if(externalLoad.queue == queue)
            return externalLoad.estimatedCost;
    }
    return 0u;
}

void GpuTaskQueueScoringData::rebuildAssignmentLoads(const Span<const GpuTaskQueueAssignment> assignments, const GpuPhysicalQueueTopology& topology){
    assignedQueueLoads.resize(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex)
        assignedQueueLoads[queueIndex] = { .queue = topology.queues[queueIndex].id, .estimatedCost = 0u };

    if(taskCostGroups.empty()){
        assignedCostWords.clear();
        costWordsPerGroup = 0u;
        nonEmptyCostGroupMask = 0u;
        totalAssignedCost = 0u;
        for(const GpuTaskQueueAssignment& assignment : assignments){
            const u64 cost = taskCosts[assignment.task.index];
            if(cost == 0u)
                continue;
            totalAssignedCost += cost;
            const usize queueIndex = findAssignedQueueIndex(assignment.queue);
            if(queueIndex < assignedQueueLoads.size())
                assignedQueueLoads[queueIndex].estimatedCost += cost;
        }
        return;
    }

    costWordsPerGroup = taskCosts.empty() ? 0u : (taskCosts.size() - 1u) / s_BitsPerWord + 1u;
    const auto wordsPerQueue = TryMultiply<usize>(costWordsPerGroup, GpuTaskCostHint::Count);
    if(!wordsPerQueue)
        throw AllocationSizeException{};
    const auto wordCount = TryMultiply<usize>(AddSize(topology.queueCount, 1u), *wordsPerQueue);
    if(!wordCount)
        throw AllocationSizeException{};
    assignedCostWords.resize(*wordCount);
    for(u64& word : assignedCostWords)
        word = 0u;

    nonEmptyCostGroupMask = 0u;
    totalAssignedCost = 0u;
    for(const GpuTaskQueueAssignment& assignment : assignments){
        const u64 cost = taskCosts[assignment.task.index];
        if(cost == 0u)
            continue;
        totalAssignedCost += cost;
        const u8 costGroup = taskCostGroups[assignment.task.index];
        nonEmptyCostGroupMask |= static_cast<u8>(1u << costGroup);
        const usize wordIndex = costGroup * costWordsPerGroup + assignment.task.index / s_BitsPerWord;
        const u64 mask = static_cast<u64>(1u) << (assignment.task.index % s_BitsPerWord);
        assignedCostWords[wordIndex] |= mask;
        const usize queueIndex = findAssignedQueueIndex(assignment.queue);
        if(queueIndex < assignedQueueLoads.size()){
            assignedQueueLoads[queueIndex].estimatedCost += cost;
            assignedCostWords[(queueIndex + 1u) * *wordsPerQueue + wordIndex] |= mask;
        }
    }
}

void GpuTaskQueueScoringData::updateAssignmentLoads(const GpuTaskId& task, const GpuPhysicalQueueId& previousQueue, const GpuPhysicalQueueId& selectedQueue)noexcept{
    if(previousQueue == selectedQueue)
        return;
    const u64 cost = taskCosts[task.index];
    if(cost == 0u)
        return;
    const usize previousQueueIndex = findAssignedQueueIndex(previousQueue);
    const usize selectedQueueIndex = findAssignedQueueIndex(selectedQueue);
    if(assignedCostWords.empty()){
        if(previousQueueIndex < assignedQueueLoads.size()){
            FOUNDATION_ASSERT(assignedQueueLoads[previousQueueIndex].estimatedCost >= cost);
            assignedQueueLoads[previousQueueIndex].estimatedCost -= cost;
        }
        if(selectedQueueIndex < assignedQueueLoads.size())
            assignedQueueLoads[selectedQueueIndex].estimatedCost += cost;
        return;
    }
    const usize wordsPerQueue = costWordsPerGroup * GpuTaskCostHint::Count;
    const usize wordIndex = taskCostGroups[task.index] * costWordsPerGroup + task.index / s_BitsPerWord;
    const u64 mask = static_cast<u64>(1u) << (task.index % s_BitsPerWord);
    if(previousQueueIndex < assignedQueueLoads.size()){
        FOUNDATION_ASSERT(assignedQueueLoads[previousQueueIndex].estimatedCost >= cost);
        assignedQueueLoads[previousQueueIndex].estimatedCost -= cost;
        assignedCostWords[(previousQueueIndex + 1u) * wordsPerQueue + wordIndex] &= ~mask;
    }
    if(selectedQueueIndex < assignedQueueLoads.size()){
        assignedQueueLoads[selectedQueueIndex].estimatedCost += cost;
        assignedCostWords[(selectedQueueIndex + 1u) * wordsPerQueue + wordIndex] |= mask;
    }
}

u64 GpuTaskQueueScoringData::assignedQueueLoad(const GpuPhysicalQueueId& queue)const noexcept{
    const usize queueIndex = findAssignedQueueIndex(queue);
    return queueIndex < assignedQueueLoads.size() ? assignedQueueLoads[queueIndex].estimatedCost : 0u;
}


u64 GpuTaskQueueScoringData::independentQueueCost(const GpuTaskSchedulingReachability& reachability, const GpuPhysicalQueueId& queue, const Span<const GpuTaskQueueAssignment> members)const noexcept{
    if(!reachability.m_valid || reachability.m_totalOrder || members.empty() || !members.data())
        return 0u;
#if defined(NWB_DEBUG)
    for(const GpuTaskQueueAssignment& member : members){
        const GpuTaskId task = member.task;
        FOUNDATION_ASSERT(task.valid() && task.generation == reachability.m_graphGeneration && task.index < reachability.m_taskCount);
    }
#endif
    const usize queueIndex = findAssignedQueueIndex(queue);
    if(queueIndex == assignedQueueLoads.size())
        return 0u;
    u64 independentCost = totalAssignedCost - assignedQueueLoads[queueIndex].estimatedCost;
    if(independentCost == 0u)
        return 0u;
    if(reachability.m_words.empty()){
        for(const GpuTaskQueueAssignment& member : members){
            if(member.queue != queue)
                independentCost -= taskCosts[member.task.index];
        }
        return independentCost;
    }
    if(nonEmptyCostGroupMask == 0u)
        return independentCost;

    FOUNDATION_ASSERT(costWordsPerGroup == reachability.m_wordsPerRow);
    const usize queueOffset = (queueIndex + 1u) * GpuTaskCostHint::Count * costWordsPerGroup;
    if(members.size() == 1u){
        const usize taskIndex = members[0u].task.index;
        const auto& range = reachability.m_relatedWordRanges[taskIndex];
        const usize firstWord = range.begin;
        const usize lastWord = range.end;
        const usize rowOffset = reachability.m_rowOffsets.empty() ? (taskIndex * costWordsPerGroup) : reachability.m_rowOffsets[taskIndex];
        for(usize wordIndex = firstWord; wordIndex < lastWord; ++wordIndex){
            const u64 related = reachability.m_rowOffsets.empty()
                ? reachability.m_words[rowOffset + wordIndex]
                : reachability.m_words[rowOffset + wordIndex - firstWord];
            if(related == 0u)
                continue;
            for(u32 groupMask = nonEmptyCostGroupMask; groupMask != 0u; groupMask &= groupMask - 1u){
                const usize costGroup = static_cast<usize>(CountTrailingZeros(groupMask));
                const usize costWord = costGroup * costWordsPerGroup + wordIndex;
                const u64 offQueue = assignedCostWords[costWord] & ~assignedCostWords[queueOffset + costWord];
                independentCost -= static_cast<u64>(CountSetBits(related & offQueue)) * costGroupWeights[costGroup];
            }
        }
        return independentCost;
    }

    usize firstWord = costWordsPerGroup;
    usize lastWord = 0u;
    for(const GpuTaskQueueAssignment& member : members){
        const auto& range = reachability.m_relatedWordRanges[member.task.index];
        firstWord = Min(firstWord, range.begin);
        lastWord = Max(lastWord, range.end);
    }
    for(usize wordIndex = firstWord; wordIndex < lastWord; ++wordIndex){
        u64 related = 0u;
        if(reachability.m_rowOffsets.empty()){
            for(const GpuTaskQueueAssignment& member : members)
                related |= reachability.m_words[member.task.index * costWordsPerGroup + wordIndex];
        }
        else{
            for(const GpuTaskQueueAssignment& member : members){
                const usize taskIndex = member.task.index;
                const auto& range = reachability.m_relatedWordRanges[taskIndex];
                if(wordIndex >= range.begin && wordIndex < range.end)
                    related |= reachability.m_words[reachability.m_rowOffsets[taskIndex] + wordIndex - range.begin];
            }
        }
        if(related == 0u)
            continue;
        for(u32 groupMask = nonEmptyCostGroupMask; groupMask != 0u; groupMask &= groupMask - 1u){
            const usize costGroup = static_cast<usize>(CountTrailingZeros(groupMask));
            const usize costWord = costGroup * costWordsPerGroup + wordIndex;
            const u64 offQueue = assignedCostWords[costWord] & ~assignedCostWords[queueOffset + costWord];
            independentCost -= static_cast<u64>(CountSetBits(related & offQueue)) * costGroupWeights[costGroup];
        }
    }
    return independentCost;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

