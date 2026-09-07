// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_analysis_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_compiler_analysis_topology{

using namespace GpuTaskGraphCompilerDetail;


struct TaskCycleTraversalFrame{
    usize nextAdjacencyIndex = 0u;
    u32 taskIndex = 0u;
};


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


void BuildTaskDependencyAdjacency(
    const GraphicsVector<GpuTaskDependencyEdge>& edges,
    const usize taskCount,
    TaskDependencyAdjacency& outAdjacency,
    Alloc::ScratchArena& scratchArena){
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

bool BuildTopologicalOrder(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GraphicsVector<GpuTaskDependencyEdge>& edges,
    const TaskDependencyAdjacency& adjacency,
    GraphicsVector<GpuTaskId>& outOrder,
    GraphicsVector<GpuTaskId>& outCyclePath,
    GraphicsVector<GpuTaskDependencyEdge>& outCycleEdges,
    Alloc::ScratchArena& scratchArena){
    using namespace __hidden_gpu_task_graph_compiler_analysis_topology;

    const usize taskCount = graph.taskCount();
    Vector<u32, Alloc::ScratchArena> indegrees(taskCount, scratchArena);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
        indegrees[taskIndex] = 0u;
    for(const usize edgeIndex : adjacency.edgeIndices)
        ++indegrees[edges[edgeIndex].consumer.index];

    outOrder.clear();
    outCyclePath.clear();
    outCycleEdges.clear();
    outOrder.reserve(taskCount);
    usize firstReadyCandidate = 0u;
    for(usize emittedCount = 0u; emittedCount < taskCount; ++emittedCount){
        usize nextTask = taskCount;
        for(usize taskIndex = firstReadyCandidate; taskIndex < taskCount; ++taskIndex){
            if(indegrees[taskIndex] == 0u){
                nextTask = taskIndex;
                break;
            }
        }
        if(nextTask == taskCount)
            break;

        indegrees[nextTask] = Limit<u32>::s_Max;
        firstReadyCandidate = nextTask + 1u;
        outOrder.push_back(graph.taskAt(nextTask).id);
        for(
            usize adjacencyIndex = adjacency.offsets[nextTask];
            adjacencyIndex < adjacency.offsets[nextTask + 1u];
            ++adjacencyIndex
        ){
            const GpuTaskDependencyEdge& edge = edges[adjacency.edgeIndices[adjacencyIndex]];
            NWB_ASSERT(indegrees[edge.consumer.index] > 0u);
            if(--indegrees[edge.consumer.index] == 0u)
                firstReadyCandidate = Min(firstReadyCandidate, static_cast<usize>(edge.consumer.index));
        }
    }
    if(outOrder.size() == taskCount)
        return true;

    Vector<u8, Alloc::ScratchArena> visitState(taskCount, scratchArena);
    Vector<TaskCycleTraversalFrame, Alloc::ScratchArena> visitStack(scratchArena);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
        visitState[taskIndex] = 0u;
    visitStack.reserve(taskCount);

    const auto appendCycleEdge = [&](const u32 producerIndex, const u32 consumerIndex){
        for(
            usize adjacencyIndex = adjacency.offsets[producerIndex];
            adjacencyIndex < adjacency.offsets[producerIndex + 1u];
            ++adjacencyIndex
        ){
            const GpuTaskDependencyEdge& edge = edges[adjacency.edgeIndices[adjacencyIndex]];
            if(edge.consumer.index == consumerIndex){
                outCycleEdges.push_back(edge);
                return;
            }
        }
        NWB_ASSERT(false);
    };
    bool foundCycle = false;
    for(u32 taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        if(visitState[taskIndex] != 0u)
            continue;

        visitState[taskIndex] = 1u;
        visitStack.push_back(TaskCycleTraversalFrame{
            .nextAdjacencyIndex = adjacency.offsets[taskIndex],
            .taskIndex = taskIndex,
        });
        while(!visitStack.empty()){
            TaskCycleTraversalFrame& frame = visitStack.back();
            if(frame.nextAdjacencyIndex >= adjacency.offsets[frame.taskIndex + 1u]){
                visitState[frame.taskIndex] = 2u;
                visitStack.pop_back();
                continue;
            }

            const u32 producerIndex = frame.taskIndex;
            const GpuTaskDependencyEdge& edge = edges[adjacency.edgeIndices[frame.nextAdjacencyIndex]];
            ++frame.nextAdjacencyIndex;
            const u32 consumerIndex = edge.consumer.index;
            if(visitState[consumerIndex] == 1u){
                usize cycleStart = 0u;
                while(visitStack[cycleStart].taskIndex != consumerIndex)
                    ++cycleStart;
                for(usize cycleIndex = cycleStart; cycleIndex < visitStack.size(); ++cycleIndex){
                    outCyclePath.push_back(graph.taskAt(visitStack[cycleIndex].taskIndex).id);
                    if(cycleIndex + 1u < visitStack.size()){
                        appendCycleEdge(
                            visitStack[cycleIndex].taskIndex,
                            visitStack[cycleIndex + 1u].taskIndex
                        );
                    }
                }
                outCyclePath.push_back(graph.taskAt(consumerIndex).id);
                appendCycleEdge(producerIndex, consumerIndex);
                foundCycle = true;
                break;
            }
            if(visitState[consumerIndex] == 0u){
                visitState[consumerIndex] = 1u;
                visitStack.push_back(TaskCycleTraversalFrame{
                    .nextAdjacencyIndex = adjacency.offsets[consumerIndex],
                    .taskIndex = consumerIndex,
                });
            }
        }
        if(foundCycle)
            break;
    }
    outOrder.clear();
    return false;
}


void BuildSchedulingEdges(
    const GraphicsVector<GpuTaskDependencyEdge>& rawEdges,
    const TaskDependencyAdjacency& adjacency,
    const GraphicsVector<GpuTaskId>& topologicalOrder,
    GraphicsVector<GpuTaskDependencyEdge>& outSchedulingEdges,
    Alloc::ScratchArena& scratchArena){
    const usize taskCount = topologicalOrder.size();
    Vector<u32, Alloc::ScratchArena> reached(taskCount, Limit<u32>::s_Max, scratchArena);
    Vector<usize, Alloc::ScratchArena> topologicalIndices(taskCount, scratchArena);
    Vector<u8, Alloc::ScratchArena> retained(rawEdges.size(), 0u, scratchArena);
    Vector<usize, Alloc::ScratchArena> candidates(scratchArena);
    Vector<u32, Alloc::ScratchArena> pending(scratchArena);
    usize maximumConsumerCount = 0u;
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        topologicalIndices[topologicalOrder[taskIndex].index] = taskIndex;
        maximumConsumerCount = Max(maximumConsumerCount, adjacency.offsets[taskIndex + 1u] - adjacency.offsets[taskIndex]);
    }
    candidates.reserve(maximumConsumerCount);
    pending.reserve(taskCount);

    // Visit direct consumers in topological order. A consumer already reached through an earlier one is redundant.
    // Each descendant is visited at most once per producer, while retained edges still leave in raw diagnostic order.
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
        Sort(candidates.data(), candidates.data() + candidates.size(), [&](const usize lhs, const usize rhs){
            return topologicalIndices[rawEdges[lhs].consumer.index] < topologicalIndices[rawEdges[rhs].consumer.index];
        });
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
            for(usize pendingIndex = 0u; pendingIndex < pending.size(); ++pendingIndex){
                const u32 taskIndex = pending[pendingIndex];
                for(usize adjacencyIndex = adjacency.offsets[taskIndex]; adjacencyIndex < adjacency.offsets[taskIndex + 1u]; ++adjacencyIndex){
                    const u32 descendantIndex = rawEdges[adjacency.edgeIndices[adjacencyIndex]].consumer.index;
                    if(reached[descendantIndex] == producerIndex)
                        continue;
                    reached[descendantIndex] = producerIndex;
                    pending.push_back(descendantIndex);
                }
            }
        }
    }
    outSchedulingEdges.clear();
    outSchedulingEdges.reserve(rawEdges.size());
    for(usize edgeIndex = 0u; edgeIndex < rawEdges.size(); ++edgeIndex){
        if(retained[edgeIndex])
            outSchedulingEdges.push_back(rawEdges[edgeIndex]);
    }
}

[[nodiscard]] bool BuildSchedulingTaskAdjacency(
    const GraphicsVector<GpuTaskDependencyEdge>& schedulingEdges,
    const usize taskCount,
    const u64 graphGeneration,
    GraphicsVector<usize>& outOutgoingOffsets,
    GraphicsVector<u32>& outOutgoingConsumers,
    GraphicsVector<usize>& outIncomingOffsets,
    GraphicsVector<u32>& outIncomingProducers,
    Alloc::ScratchArena& scratchArena){
    outOutgoingOffsets.clear();
    outOutgoingConsumers.clear();
    outIncomingOffsets.clear();
    outIncomingProducers.clear();
    outOutgoingOffsets.resize(taskCount + 1u, 0u);
    outIncomingOffsets.resize(taskCount + 1u, 0u);
    for(const GpuTaskDependencyEdge& edge : schedulingEdges){
        if(
            !edge.producer.valid()
            || !edge.consumer.valid()
            || edge.producer.generation != graphGeneration
            || edge.consumer.generation != graphGeneration
            || edge.producer.index >= taskCount
            || edge.consumer.index >= taskCount
            || edge.producer == edge.consumer
        )
            return false;
        ++outOutgoingOffsets[edge.producer.index + 1u];
        ++outIncomingOffsets[edge.consumer.index + 1u];
    }
    for(usize taskIndex = 1u; taskIndex <= taskCount; ++taskIndex){
        outOutgoingOffsets[taskIndex] += outOutgoingOffsets[taskIndex - 1u];
        outIncomingOffsets[taskIndex] += outIncomingOffsets[taskIndex - 1u];
    }

    outOutgoingConsumers.resize(schedulingEdges.size());
    outIncomingProducers.resize(schedulingEdges.size());
    Vector<usize, Alloc::ScratchArena> writeOffsets(taskCount, scratchArena);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
        writeOffsets[taskIndex] = outOutgoingOffsets[taskIndex];
    for(const GpuTaskDependencyEdge& edge : schedulingEdges)
        outOutgoingConsumers[writeOffsets[edge.producer.index]++] = edge.consumer.index;

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
        writeOffsets[taskIndex] = outIncomingOffsets[taskIndex];
    for(const GpuTaskDependencyEdge& edge : schedulingEdges)
        outIncomingProducers[writeOffsets[edge.consumer.index]++] = edge.producer.index;
    return true;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

