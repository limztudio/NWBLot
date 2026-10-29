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

void BuildTaskDependencyAdjacency(Span<const GpuTaskDependencyEdge> edges, usize taskCount, Alloc::ScratchArena& scratchArena, TaskDependencyAdjacency& outAdjacency);

[[nodiscard]] b8 BuildTopologicalOrder(
    const GpuTaskGraph::DeclarationReadView& graph,
    Span<const GpuTaskDependencyEdge> edges,
    const TaskDependencyAdjacency& adjacency,
    Alloc::ScratchArena& scratchArena,
    GraphicsVector<GpuTaskId>& outOrder,
    GraphicsVector<GpuTaskId>& outCyclePath,
    GraphicsVector<GpuTaskDependencyEdge>& outCycleEdges
);

void BuildSchedulingEdges(
    Span<const GpuTaskDependencyEdge> rawEdges,
    const TaskDependencyAdjacency& adjacency,
    Span<const GpuTaskId> topologicalOrder,
    Alloc::ScratchArena& scratchArena,
    GraphicsVector<GpuTaskDependencyEdge>& outSchedulingEdges
);

void BuildSchedulingTaskAdjacency(
    Span<const GpuTaskDependencyEdge> schedulingEdges,
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

