// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_analysis_internal.h"
#include "compiler_internal.h"

#include <global/bit.h>
#include <global/hierarchical_index_set.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_compiler_analysis_topology{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace GpuTaskGraphCompilerDetail;


struct TaskCycleTraversalFrame{
    usize nextAdjacencyIndex = 0u;
    u32 taskIndex = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BuildTaskDependencyAdjacency(const Span<const GpuTaskDependencyEdge> edges, const usize taskCount, Alloc::ScratchArena& scratchArena, TaskDependencyAdjacency& outAdjacency){
    outAdjacency.offsets.clear();
    outAdjacency.offsets.resize(taskCount + 1u, 0u);
    for(const GpuTaskDependencyEdge& edge : edges)
        ++outAdjacency.offsets[edge.producer.index + 1u];
    for(usize taskIndex = 1u; taskIndex <= taskCount; ++taskIndex)
        outAdjacency.offsets[taskIndex] += outAdjacency.offsets[taskIndex - 1u];

    outAdjacency.edgeIndices.clear();
    outAdjacency.edgeIndices.resize(outAdjacency.offsets[taskCount]);
    Vector<usize, Alloc::ScratchArena> writeOffsets(taskCount, scratchArena);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
        writeOffsets[taskIndex] = outAdjacency.offsets[taskIndex];

    for(usize edgeIndex = 0u; edgeIndex < edges.size(); ++edgeIndex){
        const GpuTaskDependencyEdge& edge = edges[edgeIndex];
        outAdjacency.edgeIndices[writeOffsets[edge.producer.index]++] = edgeIndex;
    }
}

b8 BuildTopologicalOrder(
    const GpuTaskGraph::DeclarationReadView& graph,
    const Span<const GpuTaskDependencyEdge> edges,
    const TaskDependencyAdjacency& adjacency,
    Alloc::ScratchArena& scratchArena,
    GraphicsVector<GpuTaskId>& outOrder,
    GraphicsVector<GpuTaskId>& outCyclePath,
    GraphicsVector<GpuTaskDependencyEdge>& outCycleEdges
){
    using namespace __hidden_gpu_task_graph_compiler_analysis_topology;

    const usize taskCount = CompilerGraphAccess::TaskCount(graph);
    const u64 generation = CompilerGraphAccess::Generation(graph);
    Vector<u32, Alloc::ScratchArena> indegrees(taskCount, scratchArena);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
        indegrees[taskIndex] = 0u;
    for(const GpuTaskDependencyEdge& edge : edges)
        ++indegrees[edge.consumer.index];

    outOrder.clear();
    outCyclePath.clear();
    outCycleEdges.clear();
    outOrder.reserve(taskCount);
    usize firstReadyCandidate = 0u;
    constexpr usize s_ScanBudgetMultiplier = 2u;
    usize scanBudget = taskCount > Limit<usize>::s_Max / s_ScanBudgetMultiplier ? Limit<usize>::s_Max : taskCount * s_ScanBudgetMultiplier;
    HierarchicalIndexSet<Alloc::ScratchArena> readyIndex(scratchArena);
    for(usize emittedCount = 0u; emittedCount < taskCount; ++emittedCount){
        if(scanBudget == 0u && readyIndex.m_levelCount == 0u)
            readyIndex.initializeZeros(Span<const u32>(indegrees));
        usize nextTask = taskCount;
        if(readyIndex.m_levelCount != 0u)
            nextTask = readyIndex.takeLowest();
        else{
            for(usize taskIndex = firstReadyCandidate; taskIndex < taskCount; ++taskIndex){
                if(indegrees[taskIndex] == 0u){
                    nextTask = taskIndex;
                    break;
                }
            }
            const usize scanned = nextTask - firstReadyCandidate + (nextTask < taskCount ? 1u : 0u);
            scanBudget = scanned >= scanBudget ? 0u : scanBudget - scanned;
        }
        if(nextTask == taskCount)
            break;

        indegrees[nextTask] = Limit<u32>::s_Max;
        firstReadyCandidate = nextTask + 1u;
        outOrder.push_back(GpuTaskId{ .generation = generation, .index = static_cast<u32>(nextTask) });
        for(usize adjacencyIndex = adjacency.offsets[nextTask]; adjacencyIndex < adjacency.offsets[nextTask + 1u]; ++adjacencyIndex){
            const GpuTaskDependencyEdge& edge = edges[adjacency.edgeIndices[adjacencyIndex]];
            FOUNDATION_ASSERT(indegrees[edge.consumer.index] > 0u);
            if(--indegrees[edge.consumer.index] == 0u){
                if(readyIndex.m_levelCount != 0u)
                    readyIndex.add(edge.consumer.index);
                else
                    firstReadyCandidate = Min(firstReadyCandidate, static_cast<usize>(edge.consumer.index));
            }
        }
    }
    if(outOrder.size() == taskCount)
        return true;

    constexpr u8 s_VisitNotStarted = 0u;
    constexpr u8 s_VisitOnStack = 1u;
    constexpr u8 s_VisitFinished = 2u;
    Vector<u8, Alloc::ScratchArena> visitState(taskCount, scratchArena);
    Vector<TaskCycleTraversalFrame, Alloc::ScratchArena> visitStack(scratchArena);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
        visitState[taskIndex] = s_VisitNotStarted;
    visitStack.reserve(taskCount);

    const auto appendCycleEdge = [&](const u32 producerIndex, const u32 consumerIndex){
        for(usize adjacencyIndex = adjacency.offsets[producerIndex]; adjacencyIndex < adjacency.offsets[producerIndex + 1u]; ++adjacencyIndex){
            const GpuTaskDependencyEdge& edge = edges[adjacency.edgeIndices[adjacencyIndex]];
            if(edge.consumer.index == consumerIndex){
                outCycleEdges.push_back(edge);
                return;
            }
        }
        FOUNDATION_ASSERT(false);
    };
    b8 foundCycle = false;
    for(u32 taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        if(visitState[taskIndex] != s_VisitNotStarted)
            continue;

        visitState[taskIndex] = s_VisitOnStack;
        visitStack.push_back(TaskCycleTraversalFrame{ .nextAdjacencyIndex = adjacency.offsets[taskIndex], .taskIndex = taskIndex, });
        while(!visitStack.empty()){
            TaskCycleTraversalFrame& frame = visitStack.back();
            if(frame.nextAdjacencyIndex >= adjacency.offsets[frame.taskIndex + 1u]){
                visitState[frame.taskIndex] = s_VisitFinished;
                visitStack.pop_back();
                continue;
            }

            const u32 producerIndex = frame.taskIndex;
            const GpuTaskDependencyEdge& edge = edges[adjacency.edgeIndices[frame.nextAdjacencyIndex]];
            ++frame.nextAdjacencyIndex;
            const u32 consumerIndex = edge.consumer.index;
            if(visitState[consumerIndex] == s_VisitOnStack){
                usize cycleStart = 0u;
                while(visitStack[cycleStart].taskIndex != consumerIndex)
                    ++cycleStart;
                for(usize cycleIndex = cycleStart; cycleIndex < visitStack.size(); ++cycleIndex){
                    outCyclePath.push_back(graph.taskAt(visitStack[cycleIndex].taskIndex).id);
                    if(cycleIndex + 1u < visitStack.size()){
                        appendCycleEdge(visitStack[cycleIndex].taskIndex, visitStack[cycleIndex + 1u].taskIndex);
                    }
                }
                outCyclePath.push_back(graph.taskAt(consumerIndex).id);
                appendCycleEdge(producerIndex, consumerIndex);
                foundCycle = true;
                break;
            }
            if(visitState[consumerIndex] == s_VisitNotStarted){
                visitState[consumerIndex] = s_VisitOnStack;
                visitStack.push_back(TaskCycleTraversalFrame{ .nextAdjacencyIndex = adjacency.offsets[consumerIndex], .taskIndex = consumerIndex, });
            }
        }
        if(foundCycle)
            break;
    }
    outOrder.clear();
    return false;
}


void BuildSchedulingEdges(
    const Span<const GpuTaskDependencyEdge> rawEdges,
    const TaskDependencyAdjacency& adjacency,
    const Span<const GpuTaskId> topologicalOrder,
    Alloc::ScratchArena& scratchArena,
    GraphicsVector<GpuTaskDependencyEdge>& outSchedulingEdges,
    DenseDescendantBitMatrix* const outDenseDesc
){
    const usize taskCount = topologicalOrder.size();
    Vector<u8, Alloc::ScratchArena> retained(rawEdges.size(), 0u, scratchArena);
    if(taskCount <= DenseDescendantBitMatrix::s_MaxTasks && taskCount != 0u && !rawEdges.empty()){
        const usize wordsPerRow = (taskCount - 1u) / s_BitsPerWord + 1u;
        const b8 usePreallocated = outDenseDesc && outDenseDesc->taskCount == taskCount && outDenseDesc->wordsPerRow == wordsPerRow && outDenseDesc->strictDesc.size() == taskCount * wordsPerRow;
        Vector<u64, Alloc::ScratchArena> localStrictDesc(usePreallocated ? 0u : taskCount * wordsPerRow, 0u, scratchArena);
        u64* const strictDescData = usePreallocated ? outDenseDesc->strictDesc.data() : localStrictDesc.data();
        for(usize orderIndex = taskCount; orderIndex > 0u; --orderIndex){
            const u32 producerIndex = topologicalOrder[orderIndex - 1u].index;
            const usize firstEdge = adjacency.offsets[producerIndex];
            const usize edgeEnd = adjacency.offsets[producerIndex + 1u];
            if(edgeEnd == firstEdge)
                continue;

            u64* const producerRow = strictDescData + static_cast<usize>(producerIndex) * wordsPerRow;
            if(edgeEnd == firstEdge + 1u){
                const usize edgeIdx = adjacency.edgeIndices[firstEdge];
                retained[edgeIdx] = 1u;
                const u32 consumerIndex = rawEdges[edgeIdx].consumer.index;
                const u64* const consumerRow = strictDescData + static_cast<usize>(consumerIndex) * wordsPerRow;
                for(usize wordIndex = 0u; wordIndex < wordsPerRow; ++wordIndex)
                    producerRow[wordIndex] = consumerRow[wordIndex];
                producerRow[consumerIndex / s_BitsPerWord] |= static_cast<u64>(1u) << (consumerIndex % s_BitsPerWord);
                continue;
            }

            for(usize adjacencyIndex = firstEdge; adjacencyIndex < edgeEnd; ++adjacencyIndex){
                const u32 consumerIndex = rawEdges[adjacency.edgeIndices[adjacencyIndex]].consumer.index;
                const u64* const consumerRow = strictDescData + static_cast<usize>(consumerIndex) * wordsPerRow;
                for(usize wordIndex = 0u; wordIndex < wordsPerRow; ++wordIndex)
                    producerRow[wordIndex] |= consumerRow[wordIndex];
            }
            for(usize adjacencyIndex = firstEdge; adjacencyIndex < edgeEnd; ++adjacencyIndex){
                const usize edgeIdx = adjacency.edgeIndices[adjacencyIndex];
                const u32 consumerIndex = rawEdges[edgeIdx].consumer.index;
                const u64 consumerBit = static_cast<u64>(1u) << (consumerIndex % s_BitsPerWord);
                if((producerRow[consumerIndex / s_BitsPerWord] & consumerBit) == 0u)
                    retained[edgeIdx] = 1u;
            }
            for(usize adjacencyIndex = firstEdge; adjacencyIndex < edgeEnd; ++adjacencyIndex){
                const u32 consumerIndex = rawEdges[adjacency.edgeIndices[adjacencyIndex]].consumer.index;
                producerRow[consumerIndex / s_BitsPerWord] |= static_cast<u64>(1u) << (consumerIndex % s_BitsPerWord);
            }
        }
        if(usePreallocated)
            outDenseDesc->valid = true;
        outSchedulingEdges.clear();
        outSchedulingEdges.reserve(rawEdges.size());
        for(usize edgeIndex = 0u; edgeIndex < rawEdges.size(); ++edgeIndex){
            if(retained[edgeIndex])
                outSchedulingEdges.push_back(rawEdges[edgeIndex]);
        }
        return;
    }

    Vector<u32, Alloc::ScratchArena> reached(taskCount, Limit<u32>::s_Max, scratchArena);
    Vector<usize, Alloc::ScratchArena> topologicalIndices(taskCount, scratchArena);
    Vector<usize, Alloc::ScratchArena> candidates(scratchArena);
    Vector<u32, Alloc::ScratchArena> pending(scratchArena);
    usize maximumConsumerCount = 0u;
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        topologicalIndices[topologicalOrder[taskIndex].index] = taskIndex;
        maximumConsumerCount = Max(maximumConsumerCount, adjacency.offsets[taskIndex + 1u] - adjacency.offsets[taskIndex]);
    }
    candidates.reserve(maximumConsumerCount);
    pending.reserve(taskCount);

    for(u32 producerIndex = 0u; producerIndex < taskCount; ++producerIndex){
        const usize firstEdge = adjacency.offsets[producerIndex];
        const usize edgeEnd = adjacency.offsets[producerIndex + 1u];
        if(edgeEnd == firstEdge)
            continue;
        if(edgeEnd == firstEdge + 1u){
            retained[adjacency.edgeIndices[firstEdge]] = 1u;
            continue;
        }
        candidates.clear();
        for(usize adjacencyIndex = firstEdge; adjacencyIndex < edgeEnd; ++adjacencyIndex)
            candidates.push_back(adjacency.edgeIndices[adjacencyIndex]);
        Sort(candidates.data(), candidates.data() + candidates.size(), [&](const usize lhs, const usize rhs)noexcept{
            return topologicalIndices[rawEdges[lhs].consumer.index] < topologicalIndices[rawEdges[rhs].consumer.index];
        });
        const usize lastConsumerRank = topologicalIndices[rawEdges[candidates.back()].consumer.index];
        usize firstUnreachedCandidate = 0u;
        for(usize candidateIndex = 0u; candidateIndex < candidates.size(); ++candidateIndex){
            const usize edgeIndex = candidates[candidateIndex];
            const u32 consumerIndex = rawEdges[edgeIndex].consumer.index;
            if(reached[consumerIndex] == producerIndex)
                continue;
            retained[edgeIndex] = 1u;
            if(candidateIndex + 1u == candidates.size())
                break;

            pending.clear();
            pending.push_back(consumerIndex);
            reached[consumerIndex] = producerIndex;
            while(firstUnreachedCandidate < candidates.size() && reached[rawEdges[candidates[firstUnreachedCandidate]].consumer.index] == producerIndex)
                ++firstUnreachedCandidate;
            if(firstUnreachedCandidate == candidates.size())
                break;
            for(usize pendingIndex = 0u; pendingIndex < pending.size(); ++pendingIndex){
                const u32 taskIndex = pending[pendingIndex];
                for(usize adjacencyIndex = adjacency.offsets[taskIndex]; adjacencyIndex < adjacency.offsets[taskIndex + 1u]; ++adjacencyIndex){
                    const u32 descendantIndex = rawEdges[adjacency.edgeIndices[adjacencyIndex]].consumer.index;
                    const usize descendantRank = topologicalIndices[descendantIndex];
                    if(descendantRank > lastConsumerRank || reached[descendantIndex] == producerIndex)
                        continue;
                    reached[descendantIndex] = producerIndex;
                    if(descendantIndex == rawEdges[candidates[firstUnreachedCandidate]].consumer.index){
                        do{
                            ++firstUnreachedCandidate;
                        }while(firstUnreachedCandidate < candidates.size() && reached[rawEdges[candidates[firstUnreachedCandidate]].consumer.index] == producerIndex);
                        if(firstUnreachedCandidate == candidates.size())
                            break;
                    }
                    if(descendantRank < lastConsumerRank)
                        pending.push_back(descendantIndex);
                }
                if(firstUnreachedCandidate == candidates.size())
                    break;
            }
            if(firstUnreachedCandidate == candidates.size())
                break;
        }
    }
    outSchedulingEdges.clear();
    outSchedulingEdges.reserve(rawEdges.size());
    for(usize edgeIndex = 0u; edgeIndex < rawEdges.size(); ++edgeIndex){
        if(retained[edgeIndex])
            outSchedulingEdges.push_back(rawEdges[edgeIndex]);
    }
}

void BuildSchedulingTaskAdjacency(
    const Span<const GpuTaskDependencyEdge> schedulingEdges,
    const usize taskCount,
    [[maybe_unused]] const u64 graphGeneration,
    Alloc::ScratchArena& scratchArena,
    GraphicsVector<usize>& outOutgoingOffsets,
    GraphicsVector<u32>& outOutgoingConsumers,
    GraphicsVector<usize>& outIncomingOffsets,
    GraphicsVector<u32>& outIncomingProducers
){
    outOutgoingOffsets.clear();
    outOutgoingConsumers.clear();
    outIncomingOffsets.clear();
    outIncomingProducers.clear();
    outOutgoingOffsets.resize(taskCount + 1u, 0u);
    outIncomingOffsets.resize(taskCount + 1u, 0u);
    for(const GpuTaskDependencyEdge& edge : schedulingEdges){
        FOUNDATION_ASSERT(
            edge.producer.valid()
            && edge.consumer.valid()
            && edge.producer.generation == graphGeneration
            && edge.consumer.generation == graphGeneration
            && edge.producer.index < taskCount
            && edge.consumer.index < taskCount
            && edge.producer != edge.consumer
        );
        ++outOutgoingOffsets[edge.producer.index + 1u];
        ++outIncomingOffsets[edge.consumer.index + 1u];
    }
    for(usize taskIndex = 1u; taskIndex <= taskCount; ++taskIndex){
        outOutgoingOffsets[taskIndex] += outOutgoingOffsets[taskIndex - 1u];
        outIncomingOffsets[taskIndex] += outIncomingOffsets[taskIndex - 1u];
    }

    outOutgoingConsumers.resize(schedulingEdges.size());
    outIncomingProducers.resize(schedulingEdges.size());
    Vector<usize, Alloc::ScratchArena> writeOffsets(taskCount * 2u, scratchArena);
    usize* const outgoingWrite = writeOffsets.data();
    usize* const incomingWrite = writeOffsets.data() + taskCount;
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        outgoingWrite[taskIndex] = outOutgoingOffsets[taskIndex];
        incomingWrite[taskIndex] = outIncomingOffsets[taskIndex];
    }
    for(const GpuTaskDependencyEdge& edge : schedulingEdges){
        outOutgoingConsumers[outgoingWrite[edge.producer.index]++] = edge.consumer.index;
        outIncomingProducers[incomingWrite[edge.consumer.index]++] = edge.producer.index;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

