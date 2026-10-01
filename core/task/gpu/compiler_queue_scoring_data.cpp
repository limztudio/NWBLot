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
    Alloc::ScratchArena& scratchArena)
    : m_taskCosts(graph.taskCount(), scratchArena)
    , m_taskCostGroups(reachability.m_words.empty() ? 0u : graph.taskCount(), scratchArena)
    , m_ownershipEdgeOffsets(graph.taskCount() + 1u, 0u, scratchArena)
    , m_ownershipEdges(scratchArena)
    , m_assignedQueueLoads(scratchArena)
    , m_assignedCostWords(scratchArena)
    , m_externalQueueLoads(options.queueLoads)
    , m_externalQueueLoadCount(options.queueLoadCount)
{
    for(usize costGroup = 0u; costGroup < GpuTaskCostHint::kCount; ++costGroup)
        m_costGroupWeights[costGroup] = QueueCostWeight(static_cast<GpuTaskCostHint::Enum>(costGroup));
    for(usize taskIndex = 0u; taskIndex < m_taskCosts.size(); ++taskIndex){
        const auto costGroup = graph.taskAt(taskIndex).scheduling.cost;
        if(!m_taskCostGroups.empty())
            m_taskCostGroups[taskIndex] = static_cast<u8>(costGroup);
        m_taskCosts[taskIndex] = m_costGroupWeights[costGroup];
    }

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
    if(uniqueEdges.empty())
        return;
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
        ++m_ownershipEdgeOffsets[edge->producer.index + 1u];
        ++m_ownershipEdgeOffsets[edge->consumer.index + 1u];
    }
    uniqueEdges.resize(uniqueEdgeCount);
    for(usize taskIndex = 1u; taskIndex < m_ownershipEdgeOffsets.size(); ++taskIndex)
        m_ownershipEdgeOffsets[taskIndex] += m_ownershipEdgeOffsets[taskIndex - 1u];

    m_ownershipEdges.resize(m_ownershipEdgeOffsets.back());
    Vector<usize, Alloc::ScratchArena> writeOffsets(graph.taskCount(), scratchArena);
    for(usize taskIndex = 0u; taskIndex < writeOffsets.size(); ++taskIndex)
        writeOffsets[taskIndex] = m_ownershipEdgeOffsets[taskIndex];
    for(const GpuTaskDependencyEdge* const edge : uniqueEdges){
        m_ownershipEdges[writeOffsets[edge->producer.index]] = edge;
        m_ownershipEdges[writeOffsets[edge->consumer.index]] = edge;
        ++writeOffsets[edge->producer.index];
        ++writeOffsets[edge->consumer.index];
    }
}

u64 GpuTaskQueueScoringData::externalQueueLoad(const GpuPhysicalQueueId& queue)const noexcept{
    if(!m_externalQueueLoads)
        return 0u;
    for(usize queueLoadIndex = 0u; queueLoadIndex < m_externalQueueLoadCount; ++queueLoadIndex){
        const GpuTaskQueueLoad& externalLoad = m_externalQueueLoads[queueLoadIndex];
        if(externalLoad.queue == queue)
            return externalLoad.estimatedCost;
    }
    return 0u;
}

void GpuTaskQueueScoringData::rebuildAssignmentLoads(
    const GraphicsVector<GpuTaskQueueAssignment>& assignments,
    const GpuPhysicalQueueTopology& topology){
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;

    m_assignedQueueLoads.resize(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex)
        m_assignedQueueLoads[queueIndex] = { .queue = topology.queues[queueIndex].id, .estimatedCost = 0u };

    // Edge-free, totally ordered, and unused reachability paths never query relation masks.
    if(m_taskCostGroups.empty()){
        m_assignedCostWords.clear();
        m_costWordsPerGroup = 0u;
        m_totalAssignedCost = 0u;
        for(const GpuTaskQueueAssignment& assignment : assignments){
            const u64 cost = m_taskCosts[assignment.task.index];
            m_totalAssignedCost += cost;
            for(GpuTaskQueueLoad& load : m_assignedQueueLoads){
                if(load.queue == assignment.queue){
                    load.estimatedCost += cost;
                    break;
                }
            }
        }
        return;
    }

    m_costWordsPerGroup = m_taskCosts.empty() ? 0u : (m_taskCosts.size() - 1u) / s_BitsPerWord + 1u;
    usize wordsPerQueue = 0u;
    usize wordCount = 0u;
    if(
        !TryMultiply<usize>(m_costWordsPerGroup, GpuTaskCostHint::kCount, wordsPerQueue)
        || !TryMultiply<usize>(AddSize(topology.queueCount, 1u), wordsPerQueue, wordCount)
    )
        throw AllocationSizeException{};
    m_assignedCostWords.resize(wordCount);
    for(u64& word : m_assignedCostWords)
        word = 0u;

    m_totalAssignedCost = 0u;
    // Valid graph indices are u32 and each task cost is at most eight, so graph-only totals fit in u64.
    for(const GpuTaskQueueAssignment& assignment : assignments){
        const u64 cost = m_taskCosts[assignment.task.index];
        m_totalAssignedCost += cost;
        const usize wordIndex = m_taskCostGroups[assignment.task.index] * m_costWordsPerGroup + assignment.task.index / s_BitsPerWord;
        const u64 mask = static_cast<u64>(1u) << (assignment.task.index % s_BitsPerWord);
        m_assignedCostWords[wordIndex] |= mask;
        for(usize queueIndex = 0u; queueIndex < m_assignedQueueLoads.size(); ++queueIndex){
            GpuTaskQueueLoad& load = m_assignedQueueLoads[queueIndex];
            if(load.queue == assignment.queue){
                load.estimatedCost += cost;
                m_assignedCostWords[(queueIndex + 1u) * wordsPerQueue + wordIndex] |= mask;
                break;
            }
        }
    }
}

void GpuTaskQueueScoringData::updateAssignmentLoads(
    const GpuTaskId& task,
    const GpuPhysicalQueueId& previousQueue,
    const GpuPhysicalQueueId& selectedQueue)noexcept{
    constexpr usize s_BitsPerWord = sizeof(u64) * 8u;

    if(previousQueue == selectedQueue)
        return;
    const u64 cost = m_taskCosts[task.index];
    if(m_assignedCostWords.empty()){
        for(GpuTaskQueueLoad& load : m_assignedQueueLoads){
            if(load.queue == previousQueue){
                NWB_ASSERT(load.estimatedCost >= cost);
                load.estimatedCost -= cost;
            }
            if(load.queue == selectedQueue)
                load.estimatedCost += cost;
        }
        return;
    }
    const usize wordsPerQueue = m_costWordsPerGroup * GpuTaskCostHint::kCount;
    const usize wordIndex = m_taskCostGroups[task.index] * m_costWordsPerGroup + task.index / s_BitsPerWord;
    const u64 mask = static_cast<u64>(1u) << (task.index % s_BitsPerWord);
    for(usize queueIndex = 0u; queueIndex < m_assignedQueueLoads.size(); ++queueIndex){
        GpuTaskQueueLoad& load = m_assignedQueueLoads[queueIndex];
        u64& word = m_assignedCostWords[(queueIndex + 1u) * wordsPerQueue + wordIndex];
        if(load.queue == previousQueue){
            NWB_ASSERT(load.estimatedCost >= cost);
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
    for(const GpuTaskQueueLoad& load : m_assignedQueueLoads){
        if(load.queue == queue)
            return load.estimatedCost;
    }
    return 0u;
}


u64 GpuTaskQueueScoringData::independentQueueCost(
    const GpuTaskSchedulingReachability& reachability,
    const GpuPhysicalQueueId& queue,
    const NotNull<const GpuTaskQueueAssignment*> members,
    const usize memberCount)const noexcept{
    if(!reachability.m_valid || reachability.m_totalOrder || memberCount == 0u)
        return 0u;
#if defined(NWB_DEBUG)
    for(usize memberIndex = 0u; memberIndex < memberCount; ++memberIndex){
        const GpuTaskId task = members.get()[memberIndex].task;
        NWB_ASSERT(task.valid() && task.generation == reachability.m_graphGeneration && task.index < reachability.m_taskCount);
    }
#endif
    usize queueIndex = 0u;
    while(queueIndex < m_assignedQueueLoads.size() && m_assignedQueueLoads[queueIndex].queue != queue)
        ++queueIndex;
    if(queueIndex == m_assignedQueueLoads.size())
        return 0u;
    u64 independentCost = m_totalAssignedCost - m_assignedQueueLoads[queueIndex].estimatedCost;
    if(independentCost == 0u)
        return 0u;
    if(reachability.m_words.empty()){
        for(usize memberIndex = 0u; memberIndex < memberCount; ++memberIndex){
            const GpuTaskQueueAssignment& member = members.get()[memberIndex];
            if(member.queue != queue)
                independentCost -= m_taskCosts[member.task.index];
        }
        return independentCost;
    }
    NWB_ASSERT(m_costWordsPerGroup == reachability.m_wordsPerRow);
    usize firstWord = m_costWordsPerGroup;
    usize lastWord = 0u;
    for(usize memberIndex = 0u; memberIndex < memberCount; ++memberIndex){
        const auto& range = reachability.m_relatedWordRanges[members.get()[memberIndex].task.index];
        firstWord = Min(firstWord, range.m_begin);
        lastWord = Max(lastWord, range.m_end);
    }
    const usize queueOffset = (queueIndex + 1u) * GpuTaskCostHint::kCount * m_costWordsPerGroup;
    for(usize wordIndex = firstWord; wordIndex < lastWord; ++wordIndex){
        u64 related = 0u;
        if(reachability.m_rowOffsets.empty()){
            for(usize memberIndex = 0u; memberIndex < memberCount; ++memberIndex)
                related |= reachability.m_words[members.get()[memberIndex].task.index * m_costWordsPerGroup + wordIndex];
        }
        else{
            for(usize memberIndex = 0u; memberIndex < memberCount; ++memberIndex){
                const usize taskIndex = members.get()[memberIndex].task.index;
                const auto& range = reachability.m_relatedWordRanges[taskIndex];
                if(wordIndex >= range.m_begin && wordIndex < range.m_end)
                    related |= reachability.m_words[reachability.m_rowOffsets[taskIndex] + wordIndex - range.m_begin];
            }
        }
        if(related == 0u)
            continue;
        for(usize costGroup = 0u; costGroup < GpuTaskCostHint::kCount; ++costGroup){
            const usize costWord = costGroup * m_costWordsPerGroup + wordIndex;
            const u64 offQueue = m_assignedCostWords[costWord] & ~m_assignedCostWords[queueOffset + costWord];
            independentCost -= static_cast<u64>(CountSetBits(related & offQueue)) * m_costGroupWeights[costGroup];
        }
    }
    return independentCost;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

