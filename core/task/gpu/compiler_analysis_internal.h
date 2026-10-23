// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiler.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TaskDependencyAdjacency{
    Vector<usize, Alloc::ScratchArena> offsets;
    Vector<usize, Alloc::ScratchArena> edgeIndices;


public:
    explicit TaskDependencyAdjacency(Alloc::ScratchArena& scratchArena)noexcept
        : offsets(scratchArena)
        , edgeIndices(scratchArena)
    {}
};

void BuildTaskDependencyAdjacency(const GraphicsVector<GpuTaskDependencyEdge>& edges, usize taskCount, Alloc::ScratchArena& scratchArena, TaskDependencyAdjacency& outAdjacency);

[[nodiscard]] bool BuildTopologicalOrder(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GraphicsVector<GpuTaskDependencyEdge>& edges,
    const TaskDependencyAdjacency& adjacency,
    Alloc::ScratchArena& scratchArena,
    GraphicsVector<GpuTaskId>& outOrder,
    GraphicsVector<GpuTaskId>& outCyclePath,
    GraphicsVector<GpuTaskDependencyEdge>& outCycleEdges
);

void BuildSchedulingEdges(
    const GraphicsVector<GpuTaskDependencyEdge>& rawEdges,
    const TaskDependencyAdjacency& adjacency,
    const GraphicsVector<GpuTaskId>& topologicalOrder,
    Alloc::ScratchArena& scratchArena,
    GraphicsVector<GpuTaskDependencyEdge>& outSchedulingEdges
);

void BuildSchedulingTaskAdjacency(
    const GraphicsVector<GpuTaskDependencyEdge>& schedulingEdges,
    usize taskCount,
    u64 graphGeneration,
    Alloc::ScratchArena& scratchArena,
    GraphicsVector<usize>& outOutgoingOffsets,
    GraphicsVector<u32>& outOutgoingConsumers,
    GraphicsVector<usize>& outIncomingOffsets,
    GraphicsVector<u32>& outIncomingProducers
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

