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


    explicit TaskDependencyAdjacency(Alloc::ScratchArena& scratchArena)
        : offsets(scratchArena)
        , edgeIndices(scratchArena)
    {}
};

void BuildTaskDependencyAdjacency(
    const GraphicsVector<GpuTaskDependencyEdge>& edges,
    usize taskCount,
    TaskDependencyAdjacency& outAdjacency,
    Alloc::ScratchArena& scratchArena
);

[[nodiscard]] bool BuildTopologicalOrder(
    const GpuTaskGraph::DeclarationReadView& graph,
    const GraphicsVector<GpuTaskDependencyEdge>& edges,
    const TaskDependencyAdjacency& adjacency,
    GraphicsVector<GpuTaskId>& outOrder,
    GraphicsVector<GpuTaskId>& outCyclePath,
    GraphicsVector<GpuTaskDependencyEdge>& outCycleEdges,
    Alloc::ScratchArena& scratchArena
);

void BuildSchedulingEdges(
    const GraphicsVector<GpuTaskDependencyEdge>& rawEdges,
    const TaskDependencyAdjacency& adjacency,
    const GraphicsVector<GpuTaskId>& topologicalOrder,
    GraphicsVector<GpuTaskDependencyEdge>& outSchedulingEdges,
    Alloc::ScratchArena& scratchArena
);

[[nodiscard]] bool BuildSchedulingTaskAdjacency(
    const GraphicsVector<GpuTaskDependencyEdge>& schedulingEdges,
    usize taskCount,
    u64 graphGeneration,
    GraphicsVector<usize>& outOutgoingOffsets,
    GraphicsVector<u32>& outOutgoingConsumers,
    GraphicsVector<usize>& outIncomingOffsets,
    GraphicsVector<u32>& outIncomingProducers,
    Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

