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
    Alloc::ScratchArena& scratchArena
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
        // Recovery joins accepted work only after normal execution fails, so it supplies no expected normal overlap or load.
        taskCosts[taskIndex] = scheduling.isRecoverySubmission ? 0u : costGroupWeights[costGroup];
    }

    // Hazard kinds and ranges remain available in analysis for diagnostics.
    // Queue ownership scoring counts each producer/consumer/resource only once, so build that immutable index once for all candidate evaluations.
    Vector<NotNull<const GpuTaskDependencyEdge*>, Alloc::ScratchArena> uniqueEdges(scratchArena);
    uniqueEdges.reserve(analysis.inferredEdges().size());
    for(const GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        if(edge.hazard == GpuTaskHazardType::VersionDependency || edge.hazard == GpuTaskHazardType::VersionLifetime || !edge.resource.valid())
            continue;
        uniqueEdges.push_back(MakeNotNull(&edge));
    }
    if(uniqueEdges.empty())
        return;
    Sort(uniqueEdges.begin(), uniqueEdges.end(), [](NotNull<const GpuTaskDependencyEdge*> lhs, NotNull<const GpuTaskDependencyEdge*> rhs)noexcept{
        if(lhs->producer.index != rhs->producer.index)
            return lhs->producer.index < rhs->producer.index;
        if(lhs->consumer.index != rhs->consumer.index)
            return lhs->consumer.index < rhs->consumer.index;
        return lhs->resource.index < rhs->resource.index;
    });

    usize uniqueEdgeCount = 0u;
    for(const auto edge : uniqueEdges){
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
    uniqueEdges.erase(uniqueEdges.begin() + uniqueEdgeCount, uniqueEdges.end());
    for(usize taskIndex = 1u; taskIndex < ownershipEdgeOffsets.size(); ++taskIndex)
        ownershipEdgeOffsets[taskIndex] += ownershipEdgeOffsets[taskIndex - 1u];

    ownershipEdges.resize(ownershipEdgeOffsets.back(), uniqueEdges.front());
    Vector<usize, Alloc::ScratchArena> writeOffsets(graph.taskCount(), scratchArena);
    for(usize taskIndex = 0u; taskIndex < writeOffsets.size(); ++taskIndex)
        writeOffsets[taskIndex] = ownershipEdgeOffsets[taskIndex];
    for(const auto edge : uniqueEdges){
        ownershipEdges[writeOffsets[edge->producer.index]] = edge;
        ownershipEdges[writeOffsets[edge->consumer.index]] = edge;
        ++writeOffsets[edge->producer.index];
        ++writeOffsets[edge->consumer.index];
    }
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

    // Edge-free, totally ordered, and unused reachability paths never query relation masks.
    if(taskCostGroups.empty()){
        assignedCostWords.clear();
        costWordsPerGroup = 0u;
        totalAssignedCost = 0u;
        for(const GpuTaskQueueAssignment& assignment : assignments){
            const u64 cost = taskCosts[assignment.task.index];
            if(cost == 0u)
                continue;
            totalAssignedCost += cost;
            for(GpuTaskQueueLoad& load : assignedQueueLoads){
                if(load.queue == assignment.queue){
                    load.estimatedCost += cost;
                    break;
                }
            }
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

    totalAssignedCost = 0u;
    // Valid graph indices are u32 and each task cost is at most eight, so graph-only totals fit in u64.
    for(const GpuTaskQueueAssignment& assignment : assignments){
        const u64 cost = taskCosts[assignment.task.index];
        if(cost == 0u)
            continue;
        totalAssignedCost += cost;
        const usize wordIndex = taskCostGroups[assignment.task.index] * costWordsPerGroup + assignment.task.index / s_BitsPerWord;
        const u64 mask = static_cast<u64>(1u) << (assignment.task.index % s_BitsPerWord);
        assignedCostWords[wordIndex] |= mask;
        for(usize queueIndex = 0u; queueIndex < assignedQueueLoads.size(); ++queueIndex){
            GpuTaskQueueLoad& load = assignedQueueLoads[queueIndex];
            if(load.queue == assignment.queue){
                load.estimatedCost += cost;
                assignedCostWords[(queueIndex + 1u) * *wordsPerQueue + wordIndex] |= mask;
                break;
            }
        }
    }
}

void GpuTaskQueueScoringData::updateAssignmentLoads(const GpuTaskId& task, const GpuPhysicalQueueId& previousQueue, const GpuPhysicalQueueId& selectedQueue)noexcept{
    if(previousQueue == selectedQueue)
        return;
    const u64 cost = taskCosts[task.index];
    if(cost == 0u)
        return;
    if(assignedCostWords.empty()){
        for(GpuTaskQueueLoad& load : assignedQueueLoads){
            if(load.queue == previousQueue){
                FOUNDATION_ASSERT(load.estimatedCost >= cost);
                load.estimatedCost -= cost;
            }
            if(load.queue == selectedQueue)
                load.estimatedCost += cost;
        }
        return;
    }
    const usize wordsPerQueue = costWordsPerGroup * GpuTaskCostHint::Count;
    const usize wordIndex = taskCostGroups[task.index] * costWordsPerGroup + task.index / s_BitsPerWord;
    const u64 mask = static_cast<u64>(1u) << (task.index % s_BitsPerWord);
    for(usize queueIndex = 0u; queueIndex < assignedQueueLoads.size(); ++queueIndex){
        GpuTaskQueueLoad& load = assignedQueueLoads[queueIndex];
        u64& word = assignedCostWords[(queueIndex + 1u) * wordsPerQueue + wordIndex];
        if(load.queue == previousQueue){
            FOUNDATION_ASSERT(load.estimatedCost >= cost);
            load.estimatedCost -= cost;
            word &= ~mask;
        }
        if(load.queue == selectedQueue){
            load.estimatedCost += cost;
            word |= mask;
        }
    }
}

u64 GpuTaskQueueScoringData::assignedQueueLoad(const GpuPhysicalQueueId& queue)const noexcept{
    for(const GpuTaskQueueLoad& load : assignedQueueLoads){
        if(load.queue == queue)
            return load.estimatedCost;
    }
    return 0u;
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
    usize queueIndex = 0u;
    while(queueIndex < assignedQueueLoads.size() && assignedQueueLoads[queueIndex].queue != queue)
        ++queueIndex;
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
    FOUNDATION_ASSERT(costWordsPerGroup == reachability.m_wordsPerRow);
    usize firstWord = costWordsPerGroup;
    usize lastWord = 0u;
    for(const GpuTaskQueueAssignment& member : members){
        const auto& range = reachability.m_relatedWordRanges[member.task.index];
        firstWord = Min(firstWord, range.begin);
        lastWord = Max(lastWord, range.end);
    }
    const usize queueOffset = (queueIndex + 1u) * GpuTaskCostHint::Count * costWordsPerGroup;
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
        for(usize costGroup = 0u; costGroup < GpuTaskCostHint::Count; ++costGroup){
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

